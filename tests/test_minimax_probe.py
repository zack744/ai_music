"""
试探 MiniMax Music Generation API：
- 从环境变量读 key（绝不打印明文）
- 调一次 music_generation 端点
- 把响应 body 完整打出来（不打印请求 header）
"""
import os
import sys
import json
import requests
from pathlib import Path
from dotenv import load_dotenv

PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT))
load_dotenv(PROJECT_ROOT / ".env")

# 从环境变量找 MiniMax 相关的 key（不打印明文）
api_key = None
key_name = None
for k in os.environ:
    kl = k.lower()
    if ("minimax" in kl or "minimax" in kl) and ("api" in kl or "token" in kl or "key" in kl or "plan" in kl):
        v = os.environ[k]
        if v and len(v) > 8:  # 过滤空/太短的
            api_key = v
            key_name = k
            break

if not api_key:
    print("[ERR] No MiniMax key found in .env. Looking for env vars containing 'minimax' or 'minimax':")
    for k in os.environ:
        if "minimax" in k.lower() or "minimax" in k.lower():
            print(f"  - {k} (len={len(os.environ[k])})")
    print("Add one like: MINIMAX_API_KEY=<your key>  in .env")
    sys.exit(1)

print(f"[OK] Found key: {key_name}  (length: {len(api_key)}, prefix: {api_key[:6]}...)")
print()

# 调 Music Generation
url = "https://api.minimaxi.com/v1/music_generation"
payload = {
    "model": "music-2.6-free",
    "is_instrumental": True,
    "prompt": "simple piano, gentle, 70 bpm, calm, instrumental",
    "audio_setting": {"sample_rate": 44100, "bitrate": 256000, "format": "mp3"},
    "output_format": "url",
}
# 注意：只把 Key 放进 header，header 不打印
headers = {
    "Authorization": f"Bearer {api_key}",
    "Content-Type": "application/json",
}

print(f"[..] POST {url}")
print(f"[..] payload (no secrets): model={payload['model']} prompt={payload['prompt']!r}")
try:
    r = requests.post(url, headers=headers, json=payload, timeout=180)
    print(f"[OK] status: {r.status_code}")
    print(f"[OK] response headers (selected):")
    for h in ["x-request-id", "content-type", "x-ratelimit-remaining"]:
        if h in r.headers:
            print(f"        {h}: {r.headers[h]}")
    print()
    body = r.text
    # 响应可能很大，截断显示
    print(f"[OK] response body (first 4000 chars):")
    print(body[:4000])
    print()
    # 解析可能的 JSON 看 audio 字段
    try:
        j = r.json()
        print(f"[OK] parsed JSON keys: {list(j.keys())}")
        # 找 audio 字段
        for path in [["data", "audio"], ["data", "audio_url"], ["audio"], ["audio_url"], ["data", "url"]]:
            cur = j
            ok = True
            for p in path:
                if isinstance(cur, dict) and p in cur:
                    cur = cur[p]
                else:
                    ok = False
                    break
            if ok:
                print(f"[OK] audio field found at {path} = {cur!r}")
                break
    except Exception as e:
        print(f"[..] response is not JSON: {e}")
except requests.exceptions.Timeout:
    print("[ERR] timeout (>180s) — MiniMax API 慢，可能在排队")
except requests.exceptions.HTTPError as e:
    print(f"[ERR] HTTP error: {e}")
    if e.response is not None:
        print(f"     response body: {e.response.text[:2000]}")
except Exception as e:
    print(f"[ERR] {type(e).__name__}: {e}")
