"""
fun-music-v1 vs MiniMax 同提示词对比脚本。

用法：
  .venv\\Scripts\\python.exe compare_music.py
  .venv\\Scripts\\python.exe compare_music.py --prompt "你的风格描述" --skip-vocals

对比策略（两类）：
  1. 纯音乐·同提示词（核心对比）
     fun-music: is_instrumental=True, 只传 prompt
     MiniMax:   is_instrumental=True, 只传 prompt
     两边输入完全一致，最干净的 A/B。
  2. 人声·同歌词（补充对比）
     fun-music: 传 lyrics（prompt 被忽略，风格由模型自决）
     MiniMax:   传 lyrics + 同一段风格 prompt（MiniMax 会按风格唱歌词）
     注意语义不对称：fun-music 无法在指定风格下唱指定歌词。

输出落盘 static/outputs/compare_*，并打印对比表。
依赖 .env 中的 DASHSCOPE_API_KEY（fun-music）与 MINIMAX_API_KEY（minimax）。
"""
import argparse
import logging
import os
import sys
import time
from pathlib import Path

from dotenv import load_dotenv

load_dotenv(override=True)

sys.path.insert(0, str(Path(__file__).parent))

from services.funmusic_client import FunMusicClient
from services.minimax_music_client import MiniMaxMusicClient

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(name)s] %(levelname)s | %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger("compare")

OUTPUT_DIR = Path(__file__).parent / "static" / "outputs"
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

# 默认测试用提示词（风格描述）--贴近项目「环境音->音乐」的自然治愈主题
DEFAULT_PROMPT = (
    "舒缓治愈的自然系氛围音乐，木吉他与轻柔钢琴交织，温暖明亮，"
    "慢板 80bpm，适合清晨森林漫步与冥想放松"
)

# 默认测试用歌词（人声对比）--带结构标签，fun-music 与 MiniMax 均支持
DEFAULT_LYRICS = (
    "[verse]\n"
    "清晨的阳光穿过树叶\n"
    "鸟鸣在枝头轻轻跳跃\n"
    "风吹过发梢带来远方\n"
    "一切刚好 不必匆忙\n"
    "\n"
    "[chorus]\n"
    "慢慢走 慢慢看\n"
    "让心跟着风一起转\n"
    "把烦恼丢进溪水里面\n"
    "拥抱每一个晴天\n"
)


def build_funmusic(instrumental: bool) -> FunMusicClient:
    return FunMusicClient(
        api_key=os.getenv("DASHSCOPE_API_KEY", ""),
        mode="real",
        model=os.getenv("FUNMUSIC_MODEL", "fun-music-v1"),
        gender=os.getenv("FUNMUSIC_GENDER", "female"),
        is_instrumental=instrumental,
        audio_format=os.getenv("FUNMUSIC_FORMAT", "mp3"),
        output_dir=OUTPUT_DIR,
    )


def build_minimax(instrumental: bool) -> MiniMaxMusicClient:
    return MiniMaxMusicClient(
        api_key=os.getenv("MINIMAX_API_KEY", ""),
        mode="real",
        model="music-2.6-free",
        is_instrumental=instrumental,
        output_dir=OUTPUT_DIR,
    )


def run_one(label: str, client, prompt: str, lyrics: str, out_name: str) -> dict:
    """跑一次生成，返回结果摘要。失败不抛，记录 error。"""
    t0 = time.time()
    try:
        out_path = client.text_to_music(prompt, duration_sec=30, lyrics=lyrics)
        elapsed = time.time() - t0
        size = out_path.stat().st_size if out_path.exists() else 0
        # 重命名为可读的对比文件名
        final = OUTPUT_DIR / out_name
        if out_path != final:
            if final.exists():
                final.unlink()
            out_path.rename(final)
        logger.info("[%s] 成功 | %s | 耗时=%.1fs | %s bytes", label, final.name, elapsed, size)
        return {"label": label, "status": "ok", "file": final.name,
                "elapsed_sec": round(elapsed, 1), "size_bytes": size, "error": ""}
    except Exception as e:
        elapsed = time.time() - t0
        msg = str(e)[:200]
        logger.error("[%s] 失败 | 耗时=%.1fs | %s", label, elapsed, msg)
        return {"label": label, "status": "error", "file": "",
                "elapsed_sec": round(elapsed, 1), "size_bytes": 0, "error": msg}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--prompt", default=DEFAULT_PROMPT, help="风格提示词（纯音乐对比用）")
    ap.add_argument("--lyrics", default=DEFAULT_LYRICS, help="歌词（人声对比用）")
    ap.add_argument("--skip-vocals", action="store_true", help="跳过人声对比，只跑纯音乐")
    ap.add_argument("--skip-instr", action="store_true", help="跳过纯音乐对比，只跑人声")
    args = ap.parse_args()

    rows = []
    print("=" * 70)
    print("fun-music-v1  vs  MiniMax music-2.6-free  同提示词对比")
    print("=" * 70)
    print("【风格提示词】" + args.prompt)
    if not args.skip_vocals:
        print("【歌词】\n" + args.lyrics)
    print("-" * 70)

    # 1) 纯音乐·同提示词（核心）
    if not args.skip_instr:
        print("\n[1] 纯音乐·同提示词（两边都只传 prompt）")
        rows.append(run_one("fun-music·纯音乐", build_funmusic(True),
                            args.prompt, "", "compare_funmusic_instr.mp3"))
        time.sleep(2)
        rows.append(run_one("MiniMax·纯音乐", build_minimax(True),
                            args.prompt, "", "compare_minimax_instr.mp3"))

    # 2) 人声·同歌词（补充，语义不对称见说明）
    if not args.skip_vocals:
        print("\n[2] 人声·同歌词（fun-music 仅传歌词；MiniMax 传歌词+风格prompt）")
        rows.append(run_one("fun-music·人声", build_funmusic(False),
                            args.prompt, args.lyrics, "compare_funmusic_vocals.mp3"))
        time.sleep(20)  # MiniMax free 档 RPM=3，留足间隔避免限流
        rows.append(run_one("MiniMax·人声", build_minimax(False),
                            args.prompt, args.lyrics, "compare_minimax_vocals.mp3"))

    # 汇总表
    print("\n" + "=" * 70)
    print("对比汇总")
    print("=" * 70)
    print(f"{'项目':<22}{'状态':<8}{'耗时(s)':<10}{'大小(KB)':<12}{'文件'}")
    for r in rows:
        kb = f"{r['size_bytes']//1024}" if r["size_bytes"] else "-"
        print(f"{r['label']:<22}{r['status']:<8}{r['elapsed_sec']:<10}{kb:<12}{r['file'] or r['error'][:40]}")
    print("-" * 70)
    ok = [r for r in rows if r["status"] == "ok"]
    if ok:
        print("试听文件：")
        for r in ok:
            print(f"  http://localhost:5000/outputs/{r['file']}  ({r['label']})")
    errs = [r for r in rows if r["status"] == "error"]
    if errs:
        print("\n失败详情：")
        for r in errs:
            print(f"  [{r['label']}] {r['error']}")


if __name__ == "__main__":
    main()
