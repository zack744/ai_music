# -*- coding: utf-8 -*-
"""Generate simplified-Chinese LVGL fonts (simhei) for ai_music UI.
Produces two sizes: 16px (body/small) + 22px (title/button)."""
import subprocess, sys, os

symbols = (
    "音乐主菜单生成歌曲历史录心里话环境音按开始取消下一步"
    "上传中云端创作请稍候正在播放暂停继续返回确定上下是否"
    "成功失败连接断开准备结束等待进度加载信号强度录制"
    "传暂继续模拟完下载期待载入提示信息空点击选择骤已完成失败麦克风音量时长网络错误重试缓冲删除无载连服器响应键退出保存设置密码用户址端口"
    "待我的今天昨周"
    "。、！？：％－／［］（）1234567890"
)

font = r"C:\Windows\Fonts\simhei.ttf"
node_js = r"E:\npm-global\node_modules\lv_font_conv\lv_font_conv.js"

print("symbols count:", len(set(symbols)))

for size in (16, 22):
    out = rf"D:\project\ai_music\esp32_firmware\src\lv_font_zh_{size}.c"
    cmd = [
        "node", node_js,
        "--no-compress", "--no-prefilter",
        "--bpp", "4",
        "--size", str(size),
        "--font", font,
        "-r", "0x20-0x7f",
        "--symbols", symbols,
        "--format", "lvgl",
        "-o", out,
        "--force-fast-kern-format",
    ]
    print(f"=== size {size} ===")
    r = subprocess.run(cmd, capture_output=True, encoding="utf-8")
    print("returncode:", r.returncode)
    if r.stdout: print("STDOUT:", r.stdout)
    if r.stderr: print("STDERR:", r.stderr)
    if os.path.exists(out):
        print("OK:", out, os.path.getsize(out), "bytes")
    else:
        print("FAIL: output not created")
        sys.exit(1)
