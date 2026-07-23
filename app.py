"""
ai_music 后端编排服务（Flask）。

云端理解流水线（DashScopeClient + CloudPipeline）：
  配置：PIPELINE_MODE / DASHSCOPE_API_KEY / DS_*_MODEL
       PIPELINE_MUSIC_BACKEND / PIPELINE_MUSIC_MODE / MINIMAX_API_KEY
  路由：GET  /api/pipeline/health
       POST /api/generate/pipeline     环境音理解+ASR+歌词+音乐生成
       GET  /api/samples               内置音频素材列表

启动：
  pip install -r requirements.txt
  cp .env.example .env
  python app.py
"""
import logging
import os
from datetime import datetime
from pathlib import Path

from dotenv import load_dotenv
from flask import Flask, jsonify, render_template, request, send_from_directory

from services import CloudPipeline, DashScopeClient, build_music_backend, make_output_url

load_dotenv(override=True)

# ===== 日志配置 ===========================================================
# 同时输出到终端和 pipeline.log 文件，方便事后追踪每步结果和耗时
_LOG_FORMAT = "%(asctime)s [%(name)s] %(levelname)s | %(message)s"
_LOG_DATEFMT = "%H:%M:%S"
_log_dir = Path(__file__).parent / "logs"
_log_dir.mkdir(exist_ok=True)
_file_handler = logging.FileHandler(_log_dir / "pipeline.log", encoding="utf-8")
_file_handler.setLevel(logging.INFO)
_file_handler.setFormatter(logging.Formatter(_LOG_FORMAT, _LOG_DATEFMT))

logging.basicConfig(
    level=logging.INFO,
    format=_LOG_FORMAT,
    datefmt=_LOG_DATEFMT,
    handlers=[logging.StreamHandler(), _file_handler],
)
logger = logging.getLogger("ai_music")

app = Flask(__name__)
app.config["MAX_CONTENT_LENGTH"] = 40 * 1024 * 1024  # 上传 40MB（可能两路音频）

UPLOAD_DIR = Path(__file__).parent / "static" / "uploads"
ESP32_UPLOAD_DIR = UPLOAD_DIR / "esp32"
SAMPLES_DIR = Path(__file__).parent / "audio_samples"
OUTPUTS_DIR = Path(__file__).parent / "static" / "outputs"
UPLOAD_DIR.mkdir(parents=True, exist_ok=True)
ESP32_UPLOAD_DIR.mkdir(parents=True, exist_ok=True)
OUTPUTS_DIR.mkdir(parents=True, exist_ok=True)

# ===== 云端理解流水线 =====================================================
# 云端三步（理解/ASR/LLM）由 PIPELINE_MODE + DASHSCOPE_API_KEY 控制
ds_client = DashScopeClient(
    api_key=os.getenv("DASHSCOPE_API_KEY", ""),
    mode=os.getenv("PIPELINE_MODE", "mock"),
    audio_model=os.getenv("DS_AUDIO_MODEL", "qwen3-omni-flash"),
    asr_model=os.getenv("DS_ASR_MODEL", "qwen3-asr-flash"),
    llm_model=os.getenv("DS_LLM_MODEL", "qwen-plus"),
)
# 音乐生成后端：根据 PIPELINE_MUSIC_BACKEND 动态构建（minimax / funmusic）
pipeline_music_backend_name = os.getenv("PIPELINE_MUSIC_BACKEND", "minimax")
# funmusic 复用百炼 DASHSCOPE_API_KEY；minimax 用 MINIMAX_API_KEY
if pipeline_music_backend_name.lower().strip() in ("funmusic", "fun-music", "fun_music"):
    _music_api_key = os.getenv("DASHSCOPE_API_KEY", "")
    _music_extra = {
        "model": os.getenv("FUNMUSIC_MODEL", "fun-music-v1"),
        "gender": os.getenv("FUNMUSIC_GENDER", "female"),
        "is_instrumental": os.getenv("PIPELINE_MUSIC_IS_INSTRUMENTAL", "false").lower().strip() == "true",
        "audio_format": os.getenv("FUNMUSIC_FORMAT", "mp3"),
    }
else:
    _music_api_key = os.getenv("MINIMAX_API_KEY", "")
    _music_extra = {
        "is_instrumental": os.getenv("PIPELINE_MUSIC_IS_INSTRUMENTAL", "false").lower().strip() == "true",
    }
pipeline_music = build_music_backend(
    name=pipeline_music_backend_name,
    api_key=_music_api_key,
    mode=os.getenv("PIPELINE_MUSIC_MODE", "mock"),
    **_music_extra,
)
# 外部设备（ESP32）访问本服务的基准 URL；为空时 output_url 返回相对路径
BASE_URL = os.getenv("BASE_URL", "").rstrip("/")
pipeline = CloudPipeline(
    ds=ds_client,
    music=pipeline_music,
    music_backend_name=pipeline_music_backend_name,
    mode=os.getenv("PIPELINE_MODE", "mock"),
    base_url=BASE_URL,
)


# --- 页面 ----------------------------------------------------------------

@app.get("/")
def index():
    return render_template("index.html")


# ===== API ===============================================================

@app.get("/api/pipeline/health")
def api_pipeline_health():
    return jsonify(pipeline.health())


@app.get("/api/samples")
def api_samples():
    """列出 audio_samples/ 下的免费音频，给前端做下拉选项。"""
    files = []
    for p in sorted(SAMPLES_DIR.glob("*.mp3")):
        files.append({"name": p.stem, "url": "/static-samples/" + p.name})
    return jsonify(files)


@app.get("/api/history")
def api_history():
    """列出 outputs/ 下已生成的音乐，给 ESP32 历史列表页用。按生成时间倒序。"""
    songs = []
    for p in sorted(OUTPUTS_DIR.glob("*.mp3"), key=lambda x: x.stat().st_mtime, reverse=True):
        st = p.stat()
        songs.append({
            "name": p.name,
            "url": make_output_url(BASE_URL, p.name),
            "size": st.st_size,
            "created": datetime.fromtimestamp(st.st_mtime).strftime("%Y-%m-%d %H:%M:%S"),
        })
    return jsonify({"songs": songs})


@app.get("/api/uploads")
def api_uploads():
    """列出 uploads/ 下的音频文件，区分浏览器和 ESP32 来源。"""
    def _list_dir(directory):
        files = []
        for p in sorted(directory.glob("*"), key=lambda x: x.stat().st_mtime, reverse=True):
            if not p.is_file():
                continue
            if p.name == ".gitkeep":
                continue
            st = p.stat()
            rel = "/uploads/" + p.relative_to(UPLOAD_DIR).as_posix()
            files.append({
                "name": p.name,
                "url": rel,
                "size": st.st_size,
                "created": datetime.fromtimestamp(st.st_mtime).strftime("%Y-%m-%d %H:%M:%S"),
            })
        return files
    return jsonify({
        "browser": _list_dir(UPLOAD_DIR),
        "esp32": _list_dir(ESP32_UPLOAD_DIR),
    })


@app.post("/api/generate/pipeline")
def api_generate_pipeline():
    """云端理解流水线：环境音 + (语音|文字) -> 理解 -> 歌词/风格 -> 音乐。"""
    # 期望时长：默认 90s。两端 API 均无硬时长字段，实际由短歌词/风格提示间接控制
    duration = max(30, min(int(request.form.get("duration_sec") or 90), 90))
    source = (request.form.get("source") or "").strip()
    is_esp32 = source == "esp32"
    save_dir = ESP32_UPLOAD_DIR if is_esp32 else UPLOAD_DIR
    env_file = request.files.get("env_audio")
    env_sample = (request.form.get("env_sample") or "").strip()
    speech_file = request.files.get("speech_audio")
    user_text = (request.form.get("user_text") or "").strip()

    # 解析环境音：上传文件优先，否则用内置素材
    env_path = None
    if env_file and env_file.filename:
        env_path = save_dir / _safe_name(env_file.filename)
        env_file.save(env_path)
    elif env_sample:
        p = SAMPLES_DIR / (env_sample + ".mp3")
        if p.exists():
            env_path = p
    if env_path is None:
        return jsonify({"error": "环境音不能为空（上传文件或选择内置素材）"}), 400

    # 解析"想说的话"：语音优先，没有语音则必须给文字
    speech_path = None
    if speech_file and speech_file.filename:
        speech_path = save_dir / _safe_name(speech_file.filename)
        speech_file.save(speech_path)
    if speech_path is None and not user_text:
        return jsonify({"error": "想说的话不能为空（上传语音或输入文字）"}), 400
    if len(user_text) > int(os.getenv("MAX_PROMPT_CHARS", "500")):
        return jsonify({"error": "文字过长"}), 400

    try:
        logger.info("流水线启动 | 环境音=%s 语音=%s 文字=%s 时长=%ss",
                     env_path.name, speech_path.name if speech_path else None,
                     user_text[:50] if user_text else None, duration)
        result = pipeline.run(env_path, speech_path, user_text or None, duration)
    except Exception as e:
        logger.error("流水线失败: %s", e, exc_info=True)
        return jsonify({"error": "流水线调用失败: " + str(e)}), 502
    result["scheme"] = "cloud"
    logger.info("流水线完成 | 输出=%s", result.get("output_url", "?"))
    return jsonify(result)


# --- 静态文件服务 --------------------------------------------------------

@app.get("/outputs/<path:filename>")
def serve_output(filename: str):
    return send_from_directory(OUTPUTS_DIR, filename)


@app.get("/uploads/<path:filename>")
def serve_upload(filename: str):
    return send_from_directory(UPLOAD_DIR, filename)


@app.get("/static-samples/<path:filename>")
def serve_sample(filename: str):
    return send_from_directory(SAMPLES_DIR, filename)


def _safe_name(filename: str) -> str:
    """去掉路径成分 + 加时间戳前缀，防同名覆盖/穿越。"""
    name = Path(filename).name
    return datetime.now().strftime("%H%M%S_%f") + "_" + name


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)
