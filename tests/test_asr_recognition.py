"""
ASR 识别效果测试脚本。

用从网上下载的人声语音样本，直接调用项目的 DashScopeClient.transcribe()
（qwen3-asr-flash 模型），测试 ASR 文字识别效果。

用法：
  python tests/test_asr_recognition.py
"""
import os
import sys
import time
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT))

from dotenv import load_dotenv
load_dotenv(PROJECT_ROOT / ".env", override=True)

from services import DashScopeClient

# 测试音频：从 sample-files.com 下载的人声录音
AUDIO_FILE = PROJECT_ROOT / "static" / "uploads" / "voice_sample.wav"


def main():
    if not AUDIO_FILE.exists():
        print(f"❌ 测试音频不存在: {AUDIO_FILE}")
        sys.exit(1)

    size_kb = AUDIO_FILE.stat().st_size // 1024
    print(f"📁 音频文件: {AUDIO_FILE.name}  ({size_kb} KB)")
    print(f"   路径: {AUDIO_FILE}")
    print()

    # 构建 DashScope 客户端（与 app.py 一致的配置方式）
    client = DashScopeClient(
        api_key=os.getenv("DASHSCOPE_API_KEY", ""),
        mode=os.getenv("PIPELINE_MODE", "mock"),
        asr_model=os.getenv("DS_ASR_MODEL", "qwen3-asr-flash"),
    )

    print(f"🔧 ASR 模型: {client.asr_model}")
    print(f"   运行模式: {client.mode}")
    print()

    if client.mode != "real":
        print("⚠️  当前为 mock 模式，不会真实调用 ASR 接口。")
        print("   请确认 .env 中 PIPELINE_MODE=real 且 DASHSCOPE_API_KEY 已填写。")
        sys.exit(1)

    print("=" * 60)
    print(" 开始调用 ASR 接口...")
    print("=" * 60)

    t0 = time.time()
    try:
        result = client.transcribe(AUDIO_FILE)
    except Exception as e:
        elapsed = time.time() - t0
        print(f"\n❌ ASR 调用失败（耗时 {elapsed:.2f}s）")
        print(f"   错误: {type(e).__name__}: {e}")
        sys.exit(1)

    elapsed = time.time() - t0

    print(f"\n⏱  耗时: {elapsed:.2f}s")
    print(f"\n📝 ASR 识别结果:")
    print("-" * 60)
    print(result)
    print("-" * 60)
    print(f"\n字数: {len(result)}")
    print("\n✅ ASR 识别完成。")


if __name__ == "__main__":
    main()
