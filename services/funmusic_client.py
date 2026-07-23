"""
第四步音乐后端：阿里云百炼 Fun-Music（fun-music-v1）。

走云端（DashScope），不占本地 GPU；与理解/ASR/LLM 三步共用同一个
DASHSCOPE_API_KEY，无需额外申请密钥。

- API: POST https://dashscope.aliyuncs.com/api/v1/services/audio/music/generation
- model: fun-music-v1 / fun-music-preview
- prompt 与 lyrics 二选一：同时传入时仅 lyrics 生效、prompt 被忽略
  （这与 MiniMax「prompt=风格 + lyrics=歌词」并用的语义不同，见下方策略）
- is_instrumental=True 时 lyrics/gender 无效，生成纯音乐
- gender: male / female（仅 fun-music-v1），默认 female
- 响应 output.audio.url 是 OSS URL（24h 有效），需用 requests 二次下载到本地

设计：
- mode=mock：返本地静音 wav（无 key 跑通链路）
- mode=real：真调 fun-music API，下载 mp3 到 outputs/
- 接口形状：text_to_music(prompt, duration_sec, lyrics="") -> Path
  与 MiniMaxMusicClient 完全一致，便于在 CloudPipeline 中可插拔替换。

调用策略（与 MiniMax 的差异已在这里抹平）：
- is_instrumental=True  -> 只传 prompt（纯音乐，prompt 即风格描述）
- is_instrumental=False + 有歌词 -> 只传 lyrics（fun-music 用歌词谱曲演唱，
  风格由模型自行决定；prompt 会被忽略，故不传）
- is_instrumental=False + 无歌词 -> 只传 prompt（模型自动写词并演唱）
"""
import json
import logging
import time
from datetime import datetime
from pathlib import Path

import requests

logger = logging.getLogger("funmusic")


def _music_filename(prefix: str, ext: str) -> str:
    """模型简称-日期时间.扩展名，如 funmusic-20260723-124633.mp3"""
    return f"{prefix}-{datetime.now().strftime('%Y%m%d-%H%M%S')}.{ext}"


# DashScope 服务端点（华北2-北京）。现有域名仍可用；如需更快可切业务空间专属域名
_DEFAULT_BASE_URL = "https://dashscope.aliyuncs.com/api/v1"
_MUSIC_PATH = "/services/audio/music/generation"


class FunMusicClient:
    def __init__(
        self,
        api_key: str = "",
        mode: str = "mock",
        model: str = "fun-music-v1",
        gender: str = "female",
        is_instrumental: bool = False,
        audio_format: str = "mp3",
        base_url: str = _DEFAULT_BASE_URL,
        output_dir: Path = Path("static/outputs"),
    ):
        self.api_key = (api_key or "").strip()
        self.mode = "mock" if (mode != "real" or not self.api_key) else "real"
        self.model = model
        self.gender = gender if gender in ("male", "female") else "female"
        self.is_instrumental = is_instrumental
        self.audio_format = audio_format
        self.base_url = (base_url or _DEFAULT_BASE_URL).rstrip("/")
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)

    def health(self) -> dict:
        if self.mode == "mock":
            return {"status": "ok", "mode": "mock", "provider": "funmusic", "model": "none"}
        return {
            "status": "ok",
            "mode": "real",
            "provider": "funmusic",
            "model": self.model,
            "gender": self.gender,
            "is_instrumental": self.is_instrumental,
        }

    def text_to_music(self, prompt: str, duration_sec: int = 90, lyrics: str = "") -> Path:
        """duration_sec 是 UI 期望值。fun-music API 无硬时长字段，时长主要由歌词长度决定
        （上游 LLM 已约束短歌词，目标约 60–90s）。lyrics 非空且非纯音乐时传 lyrics。"""
        if self.mode == "mock":
            return self._mock_generate(prompt, duration_sec)
        return self._real_generate(prompt, duration_sec, lyrics)

    def _mock_generate(self, prompt: str, duration_sec: int) -> Path:
        """mock 优先复制内置 mp3（ESP32 只能播 mp3），否则写短静音 wav。"""
        import shutil
        out_path = self.output_dir / _music_filename("funmusic-mock", "mp3")
        root = Path(__file__).resolve().parent.parent
        candidates = list((root / "audio_samples").glob("*.mp3"))
        candidates += list((root / "static" / "outputs").glob("*.mp3"))
        for src in candidates:
            if src.name.startswith("."):
                continue
            try:
                shutil.copyfile(src, out_path)
                logger.info("Fun-Music mock 复制素材 | src=%s -> %s", src.name, out_path.name)
                return out_path
            except OSError:
                continue
        import wave
        import struct
        sample_rate = 44100
        n_samples = int(sample_rate * min(duration_sec, 5))
        out_path = self.output_dir / _music_filename("funmusic-mock", "wav")
        with wave.open(str(out_path), "wb") as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(sample_rate)
            w.writeframes(struct.pack("<" + "h" * n_samples * 2, *([0] * n_samples * 2)))
        logger.warning("Fun-Music mock 无可用 mp3 素材，已写静音 wav=%s", out_path.name)
        return out_path

    def _build_input(self, prompt: str, lyrics: str) -> dict:
        """按 fun-music 语义组装 input 对象。"""
        inp = {
            "is_instrumental": self.is_instrumental,
            "format": self.audio_format,
            "enable_aigc_watermark": False,
        }
        if self.is_instrumental:
            # 纯音乐：只传 prompt，lyrics/gender 无效
            inp["prompt"] = prompt
        elif lyrics:
            # 有歌词：lyrics 生效，prompt 会被忽略，故不传
            inp["lyrics"] = lyrics
        else:
            # 无人声歌词：传 prompt，模型自动写词演唱
            inp["prompt"] = prompt
            inp["gender"] = self.gender
        # 非纯音乐且有歌词时，gender 仍可指定演唱性别
        if not self.is_instrumental and lyrics:
            inp["gender"] = self.gender
        return inp

    def _real_generate(self, prompt: str, duration_sec: int, lyrics: str = "") -> Path:
        url = self.base_url + _MUSIC_PATH
        headers = {
            "Authorization": f"Bearer {self.api_key}",
            "Content-Type": "application/json",
        }
        payload = {
            "model": self.model,
            "input": self._build_input(prompt, lyrics),
        }
        logger.info("Fun-Music API 调用 | model=%s instrumental=%s gender=%s lyrics=%s prompt=%s",
                     self.model, self.is_instrumental, self.gender,
                     "有" if lyrics else "无", (prompt or "")[:100])
        resp = requests.post(url, headers=headers, json=payload, timeout=300)
        if resp.status_code != 200:
            logger.error("Fun-Music API 失败 | status=%s body=%s", resp.status_code, resp.text[:400])
            raise RuntimeError(
                f"Fun-Music music_generation 失败: {resp.status_code} {resp.text[:400]}"
            )
        data = resp.json()
        audio_url = (data.get("output", {}) or {}).get("audio", {}).get("url")
        if not audio_url:
            logger.error("Fun-Music 响应缺 audio.url | resp=%s", json.dumps(data)[:400])
            raise RuntimeError(f"Fun-Music 响应缺 audio URL: {json.dumps(data)[:400]}")

        usage = data.get("usage", {}) or {}
        extra = (data.get("output", {}) or {}).get("extra_info", {}) or {}
        logger.info("Fun-Music API 返回 | duration=%ss channels=%s sample_rate=%s url=%s",
                     usage.get("duration", "?"), extra.get("channels", "?"),
                     extra.get("sample_rate", "?"), audio_url[:80])

        ext = "wav" if self.audio_format == "wav" else "mp3"
        out_path = self.output_dir / _music_filename("funmusic", ext)
        logger.info("Fun-Music 下载音频 -> %s", out_path.name)
        audio_resp = requests.get(audio_url, timeout=120, stream=True)
        audio_resp.raise_for_status()
        with open(out_path, "wb") as f:
            for chunk in audio_resp.iter_content(chunk_size=8192):
                if chunk:
                    f.write(chunk)
        logger.info("Fun-Music 下载完成 | file=%s size=%s bytes", out_path.name, out_path.stat().st_size)
        return out_path
