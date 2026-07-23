"""
云端理解流水线编排器。

把 DashScopeClient 的三个能力 + 一个可插拔的音乐生成后端串成一条流水线：

  环境音   --(qwen3-omni-flash)-->  场景 / 情绪 / 节奏描述
  用户语音 --(qwen3-asr-flash)-->  文字        （或用户直接给文字，跳过 ASR）
  场景 + 文字 --(qwen-plus)-->     歌词 + 音乐风格(JSON)
  音乐风格   --(音乐后端)-->        最终音乐

音乐后端是「可插拔」的：只要实现 text_to_music(prompt, duration_sec) -> Path
接口即可。默认用 MiniMax Music API，通过 PIPELINE_MUSIC_BACKEND 可切换为
ACE-Step / YuE / MusicGen 等其它后端，且其 mock/remote 模式由
PIPELINE_MUSIC_MODE 控制。
"""
import logging
import time
from pathlib import Path
from typing import Optional

from .dashscope_client import DashScopeClient

logger = logging.getLogger("pipeline")


def build_music_backend(name: str, api_key: str = "", mode: str = "mock", **kwargs):
    """按配置名选音乐后端工厂。返回的对象必须实现 text_to_music(prompt, duration_sec)->Path
    name=minimax -> MiniMax Music（需 MINIMAX_API_KEY）
    name=funmusic -> 阿里云百炼 Fun-Music fun-music-v1（复用 DASHSCOPE_API_KEY）"""
    name = (name or "minimax").lower().strip()
    if name == "minimax":
        from .minimax_music_client import MiniMaxMusicClient
        minimax_kwargs = {k: v for k, v in kwargs.items() if k in ("model", "is_instrumental", "output_dir")}
        return MiniMaxMusicClient(api_key=api_key, mode=mode, **minimax_kwargs)
    if name in ("funmusic", "fun-music", "fun_music"):
        from .funmusic_client import FunMusicClient
        fun_kwargs = {k: v for k, v in kwargs.items()
                      if k in ("model", "gender", "is_instrumental", "audio_format", "base_url", "output_dir")}
        return FunMusicClient(api_key=api_key, mode=mode, **fun_kwargs)
    raise ValueError(f"不支持的音乐后端: {name}，目前支持 minimax / funmusic")


class CloudPipeline:
    def __init__(
        self,
        ds: DashScopeClient,
        music,
        music_backend_name: str = "minimax",
        mode: str = "mock",
        base_url: str = "",
    ):
        self.ds = ds
        # music: 任意具备 text_to_music(prompt, duration_sec)->Path 的客户端
        self.music = music
        self.music_backend_name = music_backend_name
        # mode 管控「云端三步」(理解/ASR/LLM)；音乐后端的 mock/real 由其自身 mode 决定
        self.mode = "mock" if mode != "real" else "real"
        # base_url 非空时，output_url 拼成完整 URL（供 ESP32 等外部设备访问）；
        # 为空时返回相对路径 /outputs/xxx，兼容浏览器同源访问。
        self.base_url = (base_url or "").rstrip("/")

    def health(self) -> dict:
        h = self.ds.health()
        h["music_backend"] = self.music_backend_name
        h["music_mode"] = getattr(self.music, "mode", "?")
        return h

    def run(
        self,
        env_audio_path: Path,
        speech_audio_path: Optional[Path] = None,
        user_text: Optional[str] = None,
        duration_sec: int = 90,
    ) -> dict:
        steps = {}
        pipeline_start = time.time()

        # 第一步：理解环境音
        logger.info("[1/4] 环境音理解 | model=%s input=%s", self.ds.audio_model, env_audio_path.name)
        t0 = time.time()
        scene = self.ds.understand_audio(env_audio_path)
        elapsed_1 = time.time() - t0
        logger.info("[1/4] 完成 | 耗时=%.2fs | 输出=%s", elapsed_1, _truncate(scene, 120))
        steps["1_audio_understanding"] = {
            "scene": scene,
            "model": self.ds.audio_model,
            "elapsed_sec": round(elapsed_1, 2),
        }

        # 第二步：语音识别（有语音文件才做，否则直接用文字，降低复杂度）
        if speech_audio_path is not None:
            logger.info("[2/4] 语音识别 | model=%s input=%s", self.ds.asr_model, speech_audio_path.name)
            t0 = time.time()
            asr_text = self.ds.transcribe(speech_audio_path)
            elapsed_2 = time.time() - t0
            logger.info("[2/4] 完成 | 耗时=%.2fs | 输出=%s", elapsed_2, _truncate(asr_text, 120))
            steps["2_asr"] = {
                "text": asr_text,
                "model": self.ds.asr_model,
                "source": "audio",
                "elapsed_sec": round(elapsed_2, 2),
            }
        else:
            asr_text = user_text or ""
            logger.info("[2/4] 跳过ASR | 直接使用文字输入=%s", _truncate(asr_text, 80))
            steps["2_asr"] = {
                "text": asr_text,
                "model": None,
                "source": "text",
                "elapsed_sec": 0,
            }

        # 第三步：歌词 + 音乐风格
        logger.info("[3/4] 歌词创作 | model=%s", self.ds.llm_model)
        t0 = time.time()
        ls = self.ds.generate_lyrics_and_style(scene, asr_text)
        elapsed_3 = time.time() - t0
        logger.info("[3/4] 完成 | 耗时=%.2fs | style=%s | mood=%s",
                     elapsed_3, _truncate(ls.get("music_style", ""), 100), ls.get("mood", "?"))
        logger.info("[3/4] 歌词预览:\n%s", _truncate(ls.get("lyrics", ""), 300))
        steps["3_lyrics_and_style"] = {
            "model": self.ds.llm_model,
            "result": ls,
            "elapsed_sec": round(elapsed_3, 2),
        }

        # 第四步：音乐生成--用 music_style 当 prompt，歌词一并传给后端
        music_prompt = ls.get("music_style", "") or str(ls)
        music_lyrics = ls.get("lyrics", "")
        logger.info("[4/4] 音乐生成 | backend=%s mode=%s lyrics=%s prompt=%s",
                     self.music_backend_name, getattr(self.music, "mode", "?"),
                     "有" if music_lyrics else "无", _truncate(music_prompt, 100))
        t0 = time.time()
        out_path = self.music.text_to_music(music_prompt, duration_sec, lyrics=music_lyrics)
        elapsed_4 = time.time() - t0
        logger.info("[4/4] 完成 | 耗时=%.2fs | 输出=%s (%s bytes)",
                     elapsed_4, out_path.name, out_path.stat().st_size if out_path.exists() else "?")
        output_url = make_output_url(self.base_url, out_path.name)
        steps["4_music_generation"] = {
            "backend": self.music_backend_name,
            "mode": getattr(self.music, "mode", "?"),
            "prompt": music_prompt,
            "output_url": output_url,
            "elapsed_sec": round(elapsed_4, 2),
        }

        total = time.time() - pipeline_start
        logger.info("流水线总耗时=%.2fs (步骤: %.2f + %.2f + %.2f + %.2f)",
                     total, elapsed_1, elapsed_2 if speech_audio_path else 0, elapsed_3, elapsed_4)

        return {
            "task": "cloud-pipeline",
            "mode": self.mode,
            "duration_sec": duration_sec,
            "steps": steps,
            "final_prompt": music_prompt,
            "lyrics": ls.get("lyrics", ""),
            "music_style": music_prompt,
            "output_url": output_url,
            "total_elapsed_sec": round(total, 2),
        }


def _truncate(text: str, max_len: int = 100) -> str:
    """截断文本用于日志，超长加省略号，None 返空串。"""
    if not text:
        return ""
    text = str(text).replace("\n", " ").strip()
    return text[:max_len] + "..." if len(text) > max_len else text


def make_output_url(base_url: str, filename: str) -> str:
    """拼接输出音频 URL。
    base_url 为空 -> 返回相对路径 /outputs/xxx（兼容浏览器同源访问）；
    base_url 非空 -> 返回完整 URL，供 ESP32 等外部设备访问。
    """
    base = (base_url or "").rstrip("/")
    rel = "/outputs/" + filename
    return base + rel if base else rel
