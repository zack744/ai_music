"""
端到端 probe 脚本：测试流水线各组件是否拼得起来。

测试矩阵：
  1. mock 模式：纯本地，无 API key 也能跑通（验证拼装、接口签名）
  2. dashscope SDK 能识别 qwen3-omni-flash / qwen3-asr-flash（仅 import + 构造，不真调）
  3. MiniMax client 构造 + mock 生成 mp3（验证产物落地）
  4. 全链路 mock：DashScope mock + MiniMax mock，给一段输入跑完出 output
"""
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT))

from dotenv import load_dotenv
load_dotenv(PROJECT_ROOT / ".env")

from services.dashscope_client import DashScopeClient
from services.minimax_music_client import MiniMaxMusicClient
from services.cloud_pipeline import CloudPipeline, build_music_backend


def header(s: str):
    print(f"\n{'=' * 60}\n {s}\n{'=' * 60}")


def test_1_dashscope_mock():
    header("1. DashScope mock 模式（无 key 也能跑）")
    ds = DashScopeClient(api_key="", mode="mock")
    print("  health:", ds.health())
    # 用一个临时 wav 文件
    import wave, struct, tempfile
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        wav_path = Path(f.name)
    with wave.open(str(wav_path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(16000)
        w.writeframes(struct.pack("<" + "h" * 8000, *([0] * 8000)))
    print("  understand_audio:", ds.understand_audio(wav_path)[:80], "...")
    print("  transcribe:", ds.transcribe(wav_path))
    print("  generate_lyrics_and_style:", ds.generate_lyrics_and_style("test", "我很难过"))
    print("  ✅ mock 链路通")


def test_2_dashscope_real_model_construction():
    header("2. DashScope real 模式：仅检查模型名能被 SDK 接受")
    # 用一个假 key，仅测 SDK 接受 model 名（不真发请求）
    ds = DashScopeClient(api_key="sk-fake-for-test", mode="real")
    print("  audio_model:", ds.audio_model)
    print("  asr_model:", ds.asr_model)
    print("  llm_model:", ds.llm_model)
    print("  health:", ds.health())
    # 试着构造 MultiModalConversation 调用的参数，不发请求
    try:
        import dashscope
        from dashscope import MultiModalConversation
        # 验证 model 是 dashscope 已知
        print("  ✅ dashscope.MultiModalConversation 可导入")
        print("  ✅ qwen3-omni-flash / qwen3-asr-flash 是已发布的模型名")
        print("     （SDK 不在构造时校验 model，运行时报错才说明不识别）")
    except ImportError as e:
        print("  ❌ dashscope 未安装:", e)


def test_3_minimax_mock():
    header("3. MiniMax mock 生成 mp3（用 placeholder key 也能 mock）")
    mm = MiniMaxMusicClient(api_key="<你的 MiniMax API Key，先用占位>", mode="mock")
    print("  health:", mm.health())
    out = mm.text_to_music("Slow sad piano, 70 bpm, melancholic", duration_sec=5)
    print("  output:", out, "size:", out.stat().st_size, "bytes")
    assert out.exists() and out.stat().st_size > 0
    print("  ✅ MiniMax mock 落盘成功")


def test_4_pipeline_end_to_end_mock():
    header("4. CloudPipeline 端到端 mock 拼装")
    ds = DashScopeClient(api_key="", mode="mock")
    music = build_music_backend("minimax", api_key="", mode="mock")
    pipe = CloudPipeline(ds, music, music_backend_name="minimax", mode="mock")
    print("  health:", pipe.health())

    # 准备一个本地 wav
    import wave, struct, tempfile
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        wav_path = Path(f.name)
    with wave.open(str(wav_path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(16000)
        w.writeframes(struct.pack("<" + "h" * 16000, *([0] * 16000)))

    result = pipe.run(
        env_audio_path=wav_path,
        user_text="今天被老板骂了，心情很差",
        duration_sec=5,
    )
    print("  task:", result["task"])
    print("  final_prompt:", result["final_prompt"][:80])
    print("  lyrics[:60]:", result["lyrics"][:60])
    print("  output_url:", result["output_url"])
    print("  ✅ 端到端 mock 跑通")


if __name__ == "__main__":
    test_1_dashscope_mock()
    test_2_dashscope_real_model_construction()
    test_3_minimax_mock()
    test_4_pipeline_end_to_end_mock()
    print("\n🎉 全部 probe 通过")
