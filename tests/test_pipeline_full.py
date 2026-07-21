import requests
import json

print("测试完整流水线...")
print("POST /api/generate/pipeline")
print("环境音: forest_birds_505195.mp3")
print("用户文字: 今天心情很好，想听听大自然的声音")
print("-" * 50)

url = "http://localhost:5000/api/generate/pipeline"
data = {
    "env_sample": "forest_birds_505195",
    "user_text": "今天心情很好，想听听大自然的声音",
    "duration_sec": 30,
}

try:
    resp = requests.post(url, data=data, timeout=300)
    print(f"HTTP 状态码: {resp.status_code}")
    
    if resp.status_code == 200:
        result = resp.json()
        print("\n✅ 流水线执行成功！")
        print("\n--- 步骤结果 ---")
        
        steps = result.get("steps", {})
        
        if "1_audio_understanding" in steps:
            step1 = steps["1_audio_understanding"]
            print(f"\n[步骤1] 环境音理解")
            print(f"  模型: {step1['model']}")
            print(f"  场景描述: {step1['scene']}")
        
        if "2_asr" in steps:
            step2 = steps["2_asr"]
            print(f"\n[步骤2] 语音识别")
            print(f"  来源: {step2['source']}")
            print(f"  文本: {step2['text']}")
        
        if "3_lyrics_and_style" in steps:
            step3 = steps["3_lyrics_and_style"]
            ls = step3["result"]
            print(f"\n[步骤3] 歌词创作")
            print(f"  模型: {step3['model']}")
            print(f"  歌词:")
            print(ls.get("lyrics", "")[:200])
            if len(ls.get("lyrics", "")) > 200:
                print("...(截断)")
            print(f"  音乐风格: {ls.get('music_style', '')}")
        
        if "4_music_generation" in steps:
            step4 = steps["4_music_generation"]
            print(f"\n[步骤4] 音乐生成")
            print(f"  后端: {step4['backend']}")
            print(f"  模式: {step4['mode']}")
            print(f"  Prompt: {step4['prompt']}")
            print(f"  输出: {step4['output_url']}")
        
        print(f"\n--- 最终结果 ---")
        print(f"任务: {result.get('task')}")
        print(f"模式: {result.get('mode')}")
        print(f"最终风格: {result.get('final_prompt')}")
        print(f"输出URL: {result.get('output_url')}")
        
    else:
        print(f"\n❌ 流水线执行失败:")
        print(f"响应: {resp.text[:1000]}")
        
except requests.exceptions.Timeout:
    print("\n❌ 请求超时")
except Exception as e:
    print(f"\n❌ 错误: {type(e).__name__}: {e}")