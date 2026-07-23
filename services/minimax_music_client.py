"""
第四步音乐后端：MiniMax Music Generation API。

走云端，不占本地 GPU。
- API: POST https://api.minimaxi.com/v1/music_generation
- model: music-2.6-free / music-2.6
- is_instrumental=False（默认）时传 lyrics，MiniMax 会演唱歌词
- is_instrumental=True 时不传 lyrics，生成纯音乐
- 响应 data.audio 是 OSS URL（24h 有效），需用 requests 二次下载到本地

设计：
- mode=mock：返一个本地生成的静音 wav（用于无 key 跑通链路）
- mode=real：真调 MiniMax API，下载 MP3 到 outputs/
- 接口形状：text_to_music(prompt, duration_sec, lyrics="") -> Path
  lyrics 非空且 is_instrumental=False 时，MiniMax 会把歌词唱出来。
"""
import json
import logging
import time
from datetime import datetime
from pathlib import Path

import requests

logger = logging.getLogger("minimax")


def _music_filename(prefix: str, ext: str) -> str:
    """模型简称-日期时间.扩展名，如 minimax-20260723-124633.mp3"""
    return f"{prefix}-{datetime.now().strftime('%Y%m%d-%H%M%S')}.{ext}"


# MiniMax API 基础地址（国内）
_MINIMAX_BASE_URL = "https://api.minimaxi.com/v1"


class MiniMaxMusicClient:
    def __init__(
        self,
        api_key: str = "",
        mode: str = "mock",
        # music-2.6-free: 纯音乐 / 无歌词，免费档；music-2.6: 带歌词 / 需 token plan
        model: str = "music-2.6-free",
        is_instrumental: bool = False,
        output_dir: Path = Path("static/outputs"),
    ):
        self.api_key = (api_key or "").strip()
        self.mode = "mock" if (mode != "real" or not self.api_key) else "real"
        self.model = model
        self.is_instrumental = is_instrumental
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)

    def health(self) -> dict:
        if self.mode == "mock":
            return {"status": "ok", "mode": "mock", "provider": "minimax", "model": "none"}
        return {
            "status": "ok",
            "mode": "real",
            "provider": "minimax",
            "model": self.model,
        }

    def text_to_music(self, prompt: str, duration_sec: int = 90, lyrics: str = "") -> Path:
        """duration_sec 是期望时长（秒）。官方 API 无硬时长字段，靠短歌词 + prompt 提示间接控长。
        lyrics 非空且 is_instrumental=False 时，MiniMax 会演唱歌词。"""
        if self.mode == "mock":
            return self._mock_generate(prompt, duration_sec)
        return self._real_generate(prompt, duration_sec, lyrics)

    def _mock_generate(self, prompt: str, duration_sec: int) -> Path:
        """mock 优先复制内置 mp3（ESP32 只能播 mp3），否则写短静音 wav。"""
        import shutil
        out_path = self.output_dir / _music_filename("minimax-mock", "mp3")
        root = Path(__file__).resolve().parent.parent
        candidates = list((root / "audio_samples").glob("*.mp3"))
        candidates += list((root / "static" / "outputs").glob("*.mp3"))
        for src in candidates:
            if src.name.startswith("."):
                continue
            try:
                shutil.copyfile(src, out_path)
                logger.info("MiniMax mock 复制素材 | src=%s -> %s", src.name, out_path.name)
                return out_path
            except OSError:
                continue
        import wave
        import struct
        sample_rate = 44100
        n_samples = int(sample_rate * min(duration_sec, 5))
        out_path = self.output_dir / _music_filename("minimax-mock", "wav")
        with wave.open(str(out_path), "wb") as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(sample_rate)
            w.writeframes(struct.pack("<" + "h" * n_samples * 2, *([0] * n_samples * 2)))
        logger.warning("MiniMax mock 无可用 mp3 素材，已写静音 wav=%s", out_path.name)
        return out_path

    def _real_generate(self, prompt: str, duration_sec: int, lyrics: str = "") -> Path:
        url = f"{_MINIMAX_BASE_URL}/music_generation"
        headers = {
            "Authorization": f"Bearer {self.api_key}",
            "Content-Type": "application/json",
        }
        # 官方 schema 无 duration 字段；用 prompt 软提示 + 短歌词间接控到约 90s
        target_sec = max(30, min(int(duration_sec or 90), 90))
        music_prompt = (prompt or "").strip()
        if "second" not in music_prompt.lower() and "秒" not in music_prompt:
            music_prompt = f"{music_prompt}, about {target_sec} seconds".strip(", ")
        payload = {
            "model": self.model,
            "is_instrumental": self.is_instrumental,
            "prompt": music_prompt,
            "audio_setting": {
                "sample_rate": 44100,
                "bitrate": 128000,  # 更小体积，便于 ESP32 整首下载
                "format": "mp3",
            },
            "output_format": "url",  # 返 OSS URL，避免 base64 大字段
        }
        # 有人声模式且提供了歌词 -> 传给 MiniMax 演唱
        if not self.is_instrumental and lyrics:
            payload["lyrics"] = lyrics
        logger.info("MiniMax API 调用 | model=%s instrumental=%s target~%ss lyrics=%s prompt=%s",
                     self.model, self.is_instrumental, target_sec,
                     "有" if lyrics else "无", music_prompt[:100])
        resp = requests.post(url, headers=headers, json=payload, timeout=180)
        if resp.status_code != 200:
            logger.error("MiniMax API 失败 | status=%s body=%s", resp.status_code, resp.text[:300])
            raise RuntimeError(
                f"MiniMax music_generation 失败: {resp.status_code} {resp.text[:300]}"
            )
        data = resp.json()
        audio_url = (
            data.get("data", {}).get("audio")
            or data.get("audio")
            or data.get("data", {}).get("audio_url")
        )
        if not audio_url:
            logger.error("MiniMax 响应缺 audio URL | resp=%s", json.dumps(data)[:300])
            raise RuntimeError(
                f"MiniMax 响应缺 audio URL: {json.dumps(data)[:300]}"
            )

        extra = data.get("extra_info", {})
        logger.info("MiniMax API 返回 | duration=%sms size=%s bytes url=%s",
                     extra.get("music_duration", "?"), extra.get("music_size", "?"), audio_url[:80])

        # 下载 OSS URL 到本地
        out_path = self.output_dir / _music_filename("minimax", "mp3")
        logger.info("MiniMax 下载音频 -> %s", out_path.name)
        audio_resp = requests.get(audio_url, timeout=120, stream=True)
        audio_resp.raise_for_status()
        with open(out_path, "wb") as f:
            for chunk in audio_resp.iter_content(chunk_size=8192):
                if chunk:
                    f.write(chunk)
        logger.info("MiniMax 下载完成 | file=%s size=%s bytes", out_path.name, out_path.stat().st_size)
        return out_path
