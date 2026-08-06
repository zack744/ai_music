"""
云端理解流水线使用的 DashScope（阿里云百炼）客户端。

三个能力，对应流水线的第 1/2/3 步：
  1. understand_audio            qwen3-omni-flash 全模态理解环境音 -> 场景/情绪/节奏描述
  2. transcribe                  qwen3-asr-flash 语音识别(ASR) -> 文字
  3. generate_lyrics_and_style   qwen-plus 文本模型，融合场景+用户的话
                                  -> 歌词 + 音乐风格描述(JSON)

为什么不用 qwen-audio-turbo：
  官方明确"目前仅供免费体验，免费额度用完后不可调用"，无法作为长期方案。
  qwen3-omni-flash 是新一代全模态替代品，支持音频+文本混合输入，100万 token
  90天免费（百炼新人额度），用 MultiModalConversation.call 即可。

设计要点：
  - mode="mock" 或未配置 api_key 时，三步都返本地占位结果，保证无 key 也能跑通链路。
  - mode="real" 用 dashscope SDK 调真实接口；本地音频文件由 SDK 自动上传 OSS
    再交给模型，因此前端录的 / 上传的本地音频可直接使用。
  - dashscope 采用「懒导入」，mock 模式下即使没装 SDK 也能运行。
  - qwen3-omni-flash 是混合思考模型；理解任务显式 enable_thinking=False，
    避免被 reasoning_content 干扰。
"""
import json
import logging
import shutil
import subprocess
import tempfile
from pathlib import Path

logger = logging.getLogger("dashscope")

_ASR_MAX_SEC = 280
_ASR_CHUNK_SEC = 150


# 给全模态模型的提问模板：输出结构化的场景/情绪/节奏描述
_AUDIO_UNDERSTAND_PROMPT = (
    "请仔细听这段环境音，用一段简洁的中文描述："
    "1) 这是什么场景/地点；2) 整体情绪氛围；3) 大致的节奏感与适合的音乐风格。"
    "请直接输出描述，不要多余的寒暄。"
)

# 给 Qwen 文本模型：融合场景 + 用户的话，产出歌词与音乐风格(JSON)
# 目标总时长约 60–90 秒，方便 ESP32 整首下载到 PSRAM（约 1–1.5MB@128kbps）
_LYRICS_SYSTEM = (
    "你是一位专业的词曲创作助手。根据给定的「环境场景描述」和「用户想说的话」，"
    "创作一段短歌词，并给出一句用于音乐生成的风格描述"
    "（包含曲风、速度BPM、主要乐器、情绪），再给一个英文歌名。"
    "硬性要求：整首歌目标时长约 60–90 秒；歌词务必精短——"
    "【主歌】2–4 行、【副歌】2–4 行，总字数（中文）控制在 80 字以内，不要桥段/outro。"
    "只输出 JSON，不要输出任何解释性文字。"
    "JSON 字段：lyrics(字符串，含【主歌】【副歌】标记)、"
    "music_style(字符串，一句话风格描述，尽量用英文以便音乐模型理解；"
    "结尾可带 about 90 seconds)、"
    "mood(字符串)、"
    "song_title(字符串，英文歌名，2–5 个单词，Title Case；"
    "仅用 ASCII 字母与空格，不要标点/数字/中文；贴合情绪与场景，例如 Soft Rain Lullaby)。"
)


class DashScopeClient:
    def __init__(
        self,
        api_key: str = "",
        mode: str = "mock",
        # 默认模型已升级到 qwen3 系列（qwen-audio-turbo 已不再可用）
        audio_model: str = "qwen3-omni-flash",
        asr_model: str = "qwen3-asr-flash",
        llm_model: str = "qwen-plus",
    ):
        self.api_key = (api_key or "").strip()
        # 没填 key 一律降级为 mock，避免真实调用直接报错
        self.mode = "mock" if (mode != "real" or not self.api_key) else "real"
        self.audio_model = audio_model
        self.asr_model = asr_model
        self.llm_model = llm_model

    # ---- 健康检查 --------------------------------------------------------
    def health(self) -> dict:
        if self.mode == "mock":
            return {"status": "ok", "mode": "mock", "provider": "dashscope", "models": "none"}
        return {
            "status": "ok",
            "mode": "real",
            "provider": "dashscope",
            "models": {
                "audio": self.audio_model,
                "asr": self.asr_model,
                "llm": self.llm_model,
            },
        }

    # ---- 第一步：理解环境音 ----------------------------------------------
    def understand_audio(self, audio_path: Path) -> str:
        if self.mode == "mock":
            return (
                "（mock）一段安静的夜晚雨声，窗外淅淅沥沥。情绪偏忧郁、内敛，"
                "节奏缓慢舒展，适合慢速钢琴伴奏的抒情曲。"
            )
        return self._real_understand_audio(audio_path)

    def _real_understand_audio(self, audio_path: Path) -> str:
        import dashscope
        from dashscope import MultiModalConversation

        dashscope.api_key = self.api_key
        logger.info("DashScope 音频理解 | model=%s file=%s", self.audio_model, audio_path.name)
        # qwen3-omni-flash 是混合思考模型：理解任务显式关闭思考，避免被
        # reasoning_content 干扰输出。modalities 只取文本，不要语音回复。
        messages = [{
            "role": "user",
            "content": [
                {"audio": str(audio_path)},
                {"text": _AUDIO_UNDERSTAND_PROMPT},
            ],
        }]
        resp = MultiModalConversation.call(
            model=self.audio_model,
            messages=messages,
            modalities=["text"],
            enable_thinking=False,
        )
        result = _extract_multimodal_text(resp, tag="Qwen-Omni")
        logger.info("DashScope 音频理解完成 | resp_code=%s output_len=%d",
                     resp.status_code, len(result))
        return result

    # ---- 第二步：语音识别(ASR) -------------------------------------------
    def transcribe(self, audio_path: Path) -> str:
        if self.mode == "mock":
            return "（mock）今天考试没考好，有点难过。"
        return self._real_transcribe(audio_path)

    def _real_transcribe(self, audio_path: Path) -> str:
        # qwen3-asr-flash 单次限制 ≤10MB 且 ≤5分钟；超长音频按固定时长切片
        # 分别识别后拼接，规避 "The audio is too long"
        logger.info("DashScope ASR | model=%s file=%s", self.asr_model, audio_path.name)
        dur = _probe_duration(audio_path)
        if 0 < dur <= _ASR_MAX_SEC:
            logger.info("ASR 时长=%.1fs <= %ds，单次识别", dur, _ASR_MAX_SEC)
            return self._asr_once(audio_path)
        if dur > _ASR_MAX_SEC:
            logger.info("ASR 音频较长(%.1fs)，切片识别 chunk=%ds", dur, _ASR_CHUNK_SEC)
            return self._asr_chunked(audio_path)
        logger.warning("ASR 无法探测时长，尝试单次识别 | file=%s", audio_path.name)
        return self._asr_once(audio_path)

    def _asr_once(self, audio_path: Path) -> str:
        import dashscope
        from dashscope import MultiModalConversation

        dashscope.api_key = self.api_key
        messages = [{
            "role": "user",
            "content": [
                {"audio": str(audio_path)},
                {"text": ""},
            ],
        }]
        resp = MultiModalConversation.call(
            model=self.asr_model,
            messages=messages,
            result_format="message",
            asr_options={"enable_itn": False},
        )
        if resp.status_code != 200:
            logger.error("DashScope ASR 失败 | code=%s msg=%s", resp.code, resp.message)
            raise RuntimeError(f"ASR 失败: {resp.code} {resp.message}")
        result = _extract_multimodal_text(resp, tag="Qwen-ASR")
        logger.info("DashScope ASR 完成 | file=%s output_len=%d text=%s",
                     audio_path.name, len(result), result[:80])
        return result

    def _asr_chunked(self, audio_path: Path) -> str:
        chunks, tmpdir = _split_audio(audio_path, _ASR_CHUNK_SEC)
        try:
            texts = []
            last_err = None
            for i, c in enumerate(chunks, 1):
                cdur = _probe_duration(c)
                if 0 < cdur < 1.0:
                    logger.info("ASR 分片 %d/%d | %s 过短(%.1fs)，跳过", i, len(chunks), c.name, cdur)
                    continue
                logger.info("ASR 分片 %d/%d | %s (%.1fs)", i, len(chunks), c.name, cdur if cdur > 0 else -1)
                try:
                    texts.append(self._asr_once(c))
                except RuntimeError as e:
                    last_err = e
                    logger.warning("ASR 分片 %d/%d 失败，跳过 | %s", i, len(chunks), e)
            if not texts and last_err is not None:
                raise last_err
            return "".join(texts)
        finally:
            shutil.rmtree(tmpdir, ignore_errors=True)

    # ---- 第三步：歌词 + 音乐风格 -----------------------------------------
    def generate_lyrics_and_style(self, scene_desc: str, user_text: str) -> dict:
        if self.mode == "mock":
            words = user_text or "心里的话说不出口"
            return {
                "lyrics": (
                    "【主歌】\n窗外的雨滴轻轻落下\n"
                    + words
                    + "\n\n【副歌】\n就让这场雨陪我一会儿\n把难过都淋湿在风里"
                ),
                "music_style": "Slow sad piano ballad, soft strings, 70 bpm, melancholic, rain ambient",
                "mood": "melancholic",
                "song_title": "Soft Rain Lullaby",
            }
        return self._real_generate(scene_desc, user_text)

    def _real_generate(self, scene_desc: str, user_text: str) -> dict:
        import dashscope
        from dashscope import Generation

        dashscope.api_key = self.api_key
        logger.info("DashScope LLM | model=%s scene_len=%d user_text_len=%d",
                     self.llm_model, len(scene_desc), len(user_text or ""))
        user_prompt = (
            "【环境场景描述】\n" + scene_desc + "\n\n"
            "【用户想说的话】\n"
            + (user_text or "（用户未提供具体的话，请自由发挥）")
            + "\n\n请输出 JSON。"
        )
        messages = [
            {"role": "system", "content": _LYRICS_SYSTEM},
            {"role": "user", "content": user_prompt},
        ]
        resp = Generation.call(
            model=self.llm_model,
            messages=messages,
            result_format="message",
        )
        if resp.status_code != 200:
            logger.error("DashScope LLM 失败 | code=%s msg=%s", resp.code, resp.message)
            raise RuntimeError("LLM 调用失败: " + str(resp.code) + " " + str(resp.message))
        content = resp.output.choices[0].message.content
        result = _parse_json_loose(content)
        logger.info("DashScope LLM 完成 | keys=%s style=%s title=%s",
                     list(result.keys()), result.get("music_style", "")[:80],
                     result.get("song_title", "")[:40])
        return result


# ---- 辅助函数 ------------------------------------------------------------

def _extract_multimodal_text(resp, tag: str = "") -> str:
    """从 MultiModalConversation 响应里抠出文本。"""
    if resp.status_code != 200:
        raise RuntimeError(
            (tag + " 调用失败: " if tag else "调用失败: ")
            + str(resp.code) + " " + str(resp.message)
        )
    content = resp.output.choices[0].message.content
    if isinstance(content, list):
        return "".join(c.get("text", "") for c in content if isinstance(c, dict))
    return str(content)


def _probe_duration(audio_path: Path) -> float:
    """用 ffprobe 取音频时长(秒)；失败返 -1。"""
    try:
        out = subprocess.check_output(
            ["ffprobe", "-v", "error", "-show_entries", "format=duration",
             "-of", "default=noprint_wrappers=1:nokey=1", str(audio_path)],
            stderr=subprocess.STDOUT, timeout=30,
        ).decode().strip()
        return float(out)
    except Exception as e:
        logger.warning("ffprobe 探测时长失败 | file=%s err=%s", audio_path.name, e)
        return -1.0


def _split_audio(audio_path: Path, chunk_sec: int):
    """用 ffmpeg 按固定时长切片，返回 (分片列表, 临时目录)。"""
    tmpdir = Path(tempfile.mkdtemp(prefix="asr_chunk_"))
    pattern = str(tmpdir / "chunk_%03d.mp3")
    subprocess.check_call(
        ["ffmpeg", "-y", "-i", str(audio_path),
         "-f", "segment", "-segment_time", str(chunk_sec),
         "-c:a", "libmp3lame", "-b:a", "64k", "-ar", "16000", "-ac", "1",
         pattern],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600,
    )
    chunks = sorted(tmpdir.glob("chunk_*.mp3"))
    return chunks, tmpdir


def _parse_json_loose(text: str) -> dict:
    """宽松解析 LLM 输出的 JSON（兼容 ```json 代码块、前后多余文字）。"""
    s = (text or "").strip()
    if s.startswith("```"):
        lines = s.splitlines()
        if lines and lines[0].startswith("```"):
            lines = lines[1:]
        if lines and lines[-1].strip() == "```":
            lines = lines[:-1]
        s = "\n".join(lines).strip()
    start = s.find("{")
    end = s.rfind("}")
    if start != -1 and end != -1 and end > start:
        s = s[start:end + 1]
    try:
        return json.loads(s)
    except Exception:
        return {
            "lyrics": text,
            "music_style": text,
            "mood": "unknown",
            "song_title": "Untitled Melody",
            "_raw": text,
        }
