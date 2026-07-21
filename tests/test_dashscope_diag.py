import os
import sys
from dotenv import load_dotenv
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
load_dotenv(PROJECT_ROOT / ".env")

api_key = os.getenv("DASHSCOPE_API_KEY", "").strip()

print(f"DASHSCOPE_API_KEY 配置:")
print(f"  长度: {len(api_key)}")
print(f"  前缀: {api_key[:20]}..." if api_key else "  空")
print(f"  是否以 sk- 开头: {api_key.startswith('sk-')}")

if not api_key or not api_key.startswith('sk-'):
    print("\n❌ API Key 无效！请确保填入的是百炼「通用 API Key」（以 sk- 开头）")
    print("   Token Plan / Coding Plan 专属 Key 不消耗免费额度，也无法调用通用 API")
    sys.exit(1)

print("\n测试 DashScope API 连通性...")

try:
    import dashscope
    from dashscope import Generation
    
    dashscope.api_key = api_key
    
    print("调用 qwen-plus 做简单测试...")
    resp = Generation.call(
        model="qwen-plus",
        messages=[{"role": "user", "content": "hello"}],
        result_format="message",
    )
    
    print(f"HTTP 状态码: {resp.status_code}")
    if resp.status_code == 200:
        print("✅ DashScope API Key 有效！")
        print(f"响应: {resp.output.choices[0].message.content[:100]}...")
    else:
        print(f"❌ 调用失败: code={resp.code} message={resp.message}")
        
except ImportError:
    print("❌ dashscope SDK 未安装")
except Exception as e:
    print(f"❌ 错误: {type(e).__name__}: {e}")