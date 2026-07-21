"""
Probe 脚本 · 第 1 步：qwen3-omni-flash 理解环境音

对 audio_samples/ 下 5 个 freesound 素材逐一调用百炼全模态模型，输出：
  - 场景/情绪/节奏描述（模型的 text 回复）
  - 耗时（毫秒）
  - 消耗 token（input/output）

输出：
  - 终端表格
  - 落到 static/outputs/understanding_test/<name>.json

不要 print 任何 API Key。KEY 通过 .env 的 DASHSCOPE_API_KEY 读取。
"""
import json
import os
import sys
import time
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT))

from dotenv import load_dotenv
load_dotenv(PROJECT_ROOT / ".env")  # 静默读 .env，不会 print KEY 值

# 不在终端 print 任何 KEY 长度/前缀/具体值；只校验"已填"
KEY = os.getenv("DASHSCOPE_API_KEY", "").strip()
if not KEY:
    print("❌ DASHSCOPE_API_KEY 未填。回退 mock 模式演示链路。")
    sys.exit(1)
# 只校验"已填"，不显示长度/前缀/任何 KEY 痕迹
print("✅ DASHSCOPE_API_KEY 已加载（不显示值）")

import dashscope
from dashscope import MultiModalConversation

dashscope.api_key = KEY

AUDIO_SAMPLES_DIR = PROJECT_ROOT / "audio_samples"
OUT_DIR = PROJECT_ROOT / "static" / "outputs" / "understanding_test"
OUT_DIR.mkdir(parents=True, exist_ok=True)

# 给环境音的提问模板（与 services/dashscope_client.py 一致）
PROMPT = (
    "请仔细听这段环境音，用一段简洁的中文描述："
    "1) 这是什么场景/地点；2) 整体情绪氛围；3) 大致的节奏感与适合的音乐风格。"
    "请直接输出描述，不要多余的寒暄。"
)

samples = sorted(AUDIO_SAMPLES_DIR.glob("*.mp3"))
print(f"\n找到 {len(samples)} 个环境音素材\n")

results = []
for wav in samples:
    print(f"  ▶ {wav.name}  ({wav.stat().st_size // 1024} KB)")
    t0 = time.time()
    try:
        resp = MultiModalConversation.call(
            model="qwen3-omni-flash",
            messages=[{
                "role": "user",
                "content": [
                    {"audio": str(wav)},
                    {"text": PROMPT},
                ],
            }],
            modalities=["text"],
            enable_thinking=False,
        )
        elapsed = (time.time() - t0) * 1000

        if resp.status_code != 200:
            print(f"    ❌ 失败: {resp.code} {resp.message}")
            results.append({
                "file": wav.name,
                "error": f"{resp.code} {resp.message}",
            })
            continue

        # 抠文本
        content = resp.output.choices[0].message.content
        if isinstance(content, list):
            text = "".join(c.get("text", "") for c in content if isinstance(c, dict))
        else:
            text = str(content)

        # 抠 usage
        usage = getattr(resp, "usage", None) or {}
        in_tok = usage.get("input_tokens", "?")
        out_tok = usage.get("output_tokens", "?")
        print(f"    ⏱ {elapsed:.0f}ms | in={in_tok} out={out_tok} tok")
        print(f"    📝 {text[:120]}{'...' if len(text) > 120 else ''}")

        results.append({
            "file": wav.name,
            "size_kb": wav.stat().st_size // 1024,
            "model": "qwen3-omni-flash",
            "elapsed_ms": round(elapsed),
            "input_tokens": in_tok,
            "output_tokens": out_tok,
            "description": text,
        })

        # 落盘
        (OUT_DIR / f"{wav.stem}.json").write_text(
            json.dumps(results[-1], ensure_ascii=False, indent=2),
            encoding="utf-8",
        )

    except Exception as e:
        print(f"    ❌ 异常: {type(e).__name__}: {e}")
        results.append({"file": wav.name, "error": f"{type(e).__name__}: {e}"})

    # 间隔一下，避免突发限流
    time.sleep(0.5)

# 汇总
print("\n" + "=" * 60)
print(" 汇总")
print("=" * 60)
ok = [r for r in results if "error" not in r]
fail = [r for r in results if "error" in r]
print(f"  ✅ 成功: {len(ok)} / 失败: {len(fail)}")
if ok:
    total_in = sum(r.get("input_tokens", 0) for r in ok if isinstance(r.get("input_tokens"), int))
    total_out = sum(r.get("output_tokens", 0) for r in ok if isinstance(r.get("output_tokens"), int))
    print(f"  累计 token: in={total_in} / out={total_out} / 合计={total_in+total_out}")
    if total_in + total_out > 0:
        # 100万免费额度
        print(f"  免费额度剩余预估：~{1_000_000 - (total_in + total_out):,} token")
if fail:
    print(f"\n  失败样本：")
    for r in fail:
        print(f"    - {r['file']}: {r['error']}")

# 写总报告
(OUT_DIR / "_summary.json").write_text(
    json.dumps(results, ensure_ascii=False, indent=2),
    encoding="utf-8",
)
print(f"\n详细结果：{OUT_DIR}")
