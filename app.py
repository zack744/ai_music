"""
ai_music 后端编排服务（Flask）。

云端理解流水线（DashScopeClient + CloudPipeline）：
  配置：PIPELINE_MODE / DASHSCOPE_API_KEY / DS_*_MODEL
       PIPELINE_MUSIC_BACKEND / PIPELINE_MUSIC_MODE / MINIMAX_API_KEY
       SITE_PASSWORD / API_ACCESS_KEY / SECRET_KEY（简易鉴权）
  路由：GET  /api/pipeline/health
       POST /api/generate/pipeline     环境音理解+ASR+歌词+音乐生成
       GET  /api/samples               内置音频素材列表
       GET/POST /login  POST /logout     简易密码登录

启动：
  pip install -r requirements.txt
  cp .env.example .env
  python app.py
生产（Linux）：
  gunicorn -b 127.0.0.1:5000 -w 1 --timeout 360 "app:app"
"""
import hmac
import logging
import os
import secrets
from datetime import datetime
from pathlib import Path

from dotenv import load_dotenv
from flask import (
    Flask,
    jsonify,
    redirect,
    render_template,
    request,
    send_from_directory,
    session,
    url_for,
)

from services import (
    CloudPipeline,
    DashScopeClient,
    build_music_backend,
    make_output_url,
    read_song_title,
)

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


class _QuietPollFilter(logging.Filter):
    """过滤页面轮询/健康检查的 werkzeug access log，避免冲掉生成过程日志。"""

    _DROP = (
        "GET /api/logs",
        "GET /api/history",
        "GET /api/uploads",
        "GET /api/pipeline/health",
        "GET /api/samples",
        "GET /favicon.ico",
        '"GET / HTTP',
    )

    def filter(self, record: logging.LogRecord) -> bool:
        try:
            msg = record.getMessage()
        except Exception:
            return True
        return not any(s in msg for s in self._DROP)


logging.getLogger("werkzeug").addFilter(_QuietPollFilter())

app = Flask(__name__)
app.config["MAX_CONTENT_LENGTH"] = 40 * 1024 * 1024  # 上传 40MB（可能两路音频）
app.config["SECRET_KEY"] = (
    os.getenv("SECRET_KEY", "").strip()
    or secrets.token_hex(32)
)
# 会话 7 天；公网演示够用
app.config["PERMANENT_SESSION_LIFETIME"] = 7 * 24 * 3600
app.config["SESSION_COOKIE_HTTPONLY"] = True
app.config["SESSION_COOKIE_SAMESITE"] = "Lax"

# 简易鉴权：SITE_PASSWORD 为空则关闭（本地开发）；公网务必设置
SITE_PASSWORD = (os.getenv("SITE_PASSWORD", "") or "").strip()
# ESP32 / 脚本用 Header X-API-Key；未单独配置时与 SITE_PASSWORD 相同
API_ACCESS_KEY = (os.getenv("API_ACCESS_KEY", "") or "").strip() or SITE_PASSWORD
AUTH_ENABLED = bool(SITE_PASSWORD)

_AUTH_EXEMPT_ENDPOINTS = frozenset({"login", "static"})

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

# 音乐后端 / mock 模式可运行时切换（网页切换，无需改 .env / 重启）
def _env_mode(key: str, default: str = "mock") -> str:
    v = (os.getenv(key, default) or default).lower().strip()
    return "real" if v == "real" else "mock"


_music_runtime = {
    "backend": (os.getenv("PIPELINE_MUSIC_BACKEND", "funmusic") or "funmusic").lower().strip(),
    "is_instrumental": os.getenv("PIPELINE_MUSIC_IS_INSTRUMENTAL", "false").lower().strip() == "true",
    "pipeline_mode": _env_mode("PIPELINE_MODE", "mock"),
    "music_mode": _env_mode("PIPELINE_MUSIC_MODE", "mock"),
}
if _music_runtime["backend"] in ("fun-music", "fun_music"):
    _music_runtime["backend"] = "funmusic"
if _music_runtime["backend"] not in ("funmusic", "minimax"):
    _music_runtime["backend"] = "funmusic"


def _normalize_music_backend(name: str) -> str:
    n = (name or "").lower().strip()
    if n in ("funmusic", "fun-music", "fun_music"):
        return "funmusic"
    if n == "minimax":
        return "minimax"
    raise ValueError("不支持的音乐后端，可选 funmusic / minimax")


def _build_music_client(backend: str, is_instrumental: bool, music_mode: str | None = None):
    """按当前运行时配置构建音乐后端实例。"""
    mode = music_mode if music_mode is not None else _music_runtime["music_mode"]
    mode = "real" if mode == "real" else "mock"
    if backend == "funmusic":
        return build_music_backend(
            name="funmusic",
            api_key=os.getenv("DASHSCOPE_API_KEY", ""),
            mode=mode,
            model=os.getenv("FUNMUSIC_MODEL", "fun-music-v1"),
            gender=os.getenv("FUNMUSIC_GENDER", "female"),
            is_instrumental=is_instrumental,
            audio_format=os.getenv("FUNMUSIC_FORMAT", "mp3"),
        )
    return build_music_backend(
        name="minimax",
        api_key=os.getenv("MINIMAX_API_KEY", ""),
        mode=mode,
        is_instrumental=is_instrumental,
    )


def _apply_ds_mode(mode: str) -> str:
    """设置云端三步 mock/real。real 且无 key 时客户端仍会降级 mock。"""
    mode = "real" if mode == "real" else "mock"
    if mode == "real" and ds_client.api_key:
        ds_client.mode = "real"
    else:
        ds_client.mode = "mock"
        mode = "mock" if mode == "real" and not ds_client.api_key else mode
    return ds_client.mode


def _music_config_payload() -> dict:
    music = pipeline.music
    mock_on = (
        _music_runtime["pipeline_mode"] == "mock"
        and _music_runtime["music_mode"] == "mock"
    )
    return {
        "backend": _music_runtime["backend"],
        "is_instrumental": _music_runtime["is_instrumental"],
        "pipeline_mode": _music_runtime["pipeline_mode"],
        "music_mode": getattr(music, "mode", _music_runtime["music_mode"]),
        "mock": mock_on,
        "model": getattr(music, "model", None),
        "gender": getattr(music, "gender", None),
        "options": [
            {"id": "funmusic", "label": "Fun-Music（百炼）"},
            {"id": "minimax", "label": "MiniMax Music"},
        ],
    }


def apply_music_backend(
    backend: str,
    is_instrumental: bool | None = None,
    mock: bool | None = None,
) -> dict:
    """切换音乐后端 / mock，热更新 pipeline（进程内生效，不写 .env）。"""
    name = _normalize_music_backend(backend)
    instr = _music_runtime["is_instrumental"] if is_instrumental is None else bool(is_instrumental)

    if mock is True:
        pipe_mode = music_mode = "mock"
    elif mock is False:
        pipe_mode = music_mode = "real"
    else:
        pipe_mode = _music_runtime["pipeline_mode"]
        music_mode = _music_runtime["music_mode"]

    actual_pipe = _apply_ds_mode(pipe_mode)
    client = _build_music_client(name, instr, music_mode)
    actual_music = getattr(client, "mode", music_mode)

    _music_runtime["backend"] = name
    _music_runtime["is_instrumental"] = instr
    _music_runtime["pipeline_mode"] = actual_pipe
    _music_runtime["music_mode"] = actual_music
    pipeline.music = client
    pipeline.music_backend_name = name
    pipeline.mode = actual_pipe
    logger.info(
        "流水线配置已更新 | backend=%s instrumental=%s pipeline_mode=%s music_mode=%s model=%s",
        name, instr, actual_pipe, actual_music, getattr(client, "model", "?"),
    )
    return _music_config_payload()


# 外部设备（ESP32）访问本服务的基准 URL；为空时 output_url 返回相对路径
BASE_URL = os.getenv("BASE_URL", "").rstrip("/")
_apply_ds_mode(_music_runtime["pipeline_mode"])
pipeline = CloudPipeline(
    ds=ds_client,
    music=_build_music_client(_music_runtime["backend"], _music_runtime["is_instrumental"]),
    music_backend_name=_music_runtime["backend"],
    mode=_music_runtime["pipeline_mode"],
    base_url=BASE_URL,
)
_music_runtime["pipeline_mode"] = ds_client.mode
_music_runtime["music_mode"] = getattr(pipeline.music, "mode", _music_runtime["music_mode"])
pipeline.mode = _music_runtime["pipeline_mode"]


# --- 简易鉴权 ------------------------------------------------------------

def _safe_equal(a: str, b: str) -> bool:
    if not a or not b:
        return False
    return hmac.compare_digest(a.encode("utf-8"), b.encode("utf-8"))


def _request_api_key() -> str:
    key = (request.headers.get("X-API-Key") or "").strip()
    if key:
        return key
    auth = (request.headers.get("Authorization") or "").strip()
    if auth.lower().startswith("bearer "):
        return auth[7:].strip()
    return (request.args.get("api_key") or "").strip()


def _auth_ok() -> bool:
    if not AUTH_ENABLED:
        return True
    if session.get("authed") is True:
        return True
    return _safe_equal(_request_api_key(), API_ACCESS_KEY)


def _wants_json() -> bool:
    if request.path.startswith("/api/"):
        return True
    best = request.accept_mimetypes.best_match(["application/json", "text/html"])
    return best == "application/json" and (
        request.accept_mimetypes[best] > request.accept_mimetypes["text/html"]
    )


@app.before_request
def _require_auth():
    if not AUTH_ENABLED:
        return None
    ep = request.endpoint or ""
    if ep in _AUTH_EXEMPT_ENDPOINTS or ep.startswith("static"):
        return None
    if _auth_ok():
        return None
    if _wants_json() or request.path.startswith(("/api/", "/outputs/", "/uploads/")):
        return jsonify({"error": "未授权，请登录或提供 X-API-Key"}), 401
    nxt = request.full_path if request.query_string else request.path
    if nxt.endswith("?"):
        nxt = nxt[:-1]
    return redirect(url_for("login", next=nxt))


# --- 页面 ----------------------------------------------------------------

@app.get("/")
def index():
    return render_template("index.html", auth_enabled=AUTH_ENABLED)


@app.route("/login", methods=["GET", "POST"])
def login():
    if not AUTH_ENABLED:
        return redirect(url_for("index"))
    if session.get("authed"):
        return redirect(url_for("index"))

    error = ""
    if request.method == "POST":
        password = (request.form.get("password") or "").strip()
        if _safe_equal(password, SITE_PASSWORD):
            session.clear()
            session["authed"] = True
            session.permanent = True
            nxt = (request.form.get("next") or request.args.get("next") or "/").strip()
            if not nxt.startswith("/") or nxt.startswith("//"):
                nxt = "/"
            logger.info("登录成功 | ip=%s", request.remote_addr or "?")
            return redirect(nxt)
        error = "密码错误"
        logger.warning("登录失败 | ip=%s", request.remote_addr or "?")

    nxt = (request.args.get("next") or "/").strip()
    if not nxt.startswith("/") or nxt.startswith("//"):
        nxt = "/"
    return render_template("login.html", error=error, next=nxt), (401 if error else 200)


@app.post("/logout")
def logout():
    session.clear()
    return redirect(url_for("login") if AUTH_ENABLED else url_for("index"))


# ===== API ===============================================================

@app.get("/api/pipeline/health")
def api_pipeline_health():
    h = pipeline.health()
    h.update(_music_config_payload())
    return jsonify(h)


@app.get("/api/pipeline/music-backend")
def api_get_music_backend():
    """当前音乐后端配置（供网页切换控件初始化）。"""
    return jsonify(_music_config_payload())


def _parse_bool_field(data: dict, form_key: str):
    """从 JSON/form 解析可选 bool；未提供返回 None。"""
    raw = data.get(form_key, request.form.get(form_key))
    if raw is None or raw == "":
        return None
    if isinstance(raw, bool):
        return raw
    return str(raw).lower().strip() in ("1", "true", "yes", "on")


@app.post("/api/pipeline/music-backend")
def api_set_music_backend():
    """网页热切换 funmusic / minimax、纯音乐、mock。无需改 .env。"""
    data = request.get_json(silent=True) or {}
    backend = (data.get("backend") or request.form.get("backend") or _music_runtime["backend"]).strip()
    if not backend:
        return jsonify({"error": "backend 不能为空（funmusic / minimax）"}), 400
    instr = _parse_bool_field(data, "is_instrumental")
    mock = _parse_bool_field(data, "mock")
    try:
        cfg = apply_music_backend(backend, instr, mock)
    except ValueError as e:
        return jsonify({"error": str(e)}), 400
    except Exception as e:
        logger.error("切换音乐后端失败: %s", e, exc_info=True)
        return jsonify({"error": "切换失败: " + str(e)}), 500
    return jsonify({"ok": True, **cfg})


@app.get("/api/samples")
def api_samples():
    """列出 audio_samples/ 下的免费音频，给前端做下拉选项。"""
    files = []
    for p in sorted(SAMPLES_DIR.glob("*.mp3")):
        files.append({"name": p.stem, "url": "/static-samples/" + p.name})
    return jsonify(files)


@app.get("/api/history")
def api_history():
    """列出 outputs/ 下已生成的音乐，给 ESP32 / 网页历史区用。按生成时间倒序。"""
    songs = []
    files = list(OUTPUTS_DIR.glob("*.mp3")) + list(OUTPUTS_DIR.glob("*.wav"))
    for p in sorted(files, key=lambda x: x.stat().st_mtime, reverse=True):
        if p.name.startswith("."):
            continue
        st = p.stat()
        title = read_song_title(p)
        songs.append({
            "name": title or p.name,
            "file": p.name,
            "song_title": title or None,
            "url": make_output_url(BASE_URL, p.name),
            "size": st.st_size,
            "created": datetime.fromtimestamp(st.st_mtime).strftime("%Y-%m-%d %H:%M:%S"),
        })
    return jsonify({"songs": songs})


def _is_pipeline_log_line(line: str) -> bool:
    """首页只展示生成过程相关日志，过滤健康检查/轮询/心跳。"""
    if not line or not line.strip():
        return False
    # 明确丢弃：页面轮询、健康检查、静态心跳
    drop_markers = (
        "GET /api/logs",
        "GET /api/history",
        "GET /api/uploads",
        "GET /api/pipeline/health",
        "GET /api/samples",
        "GET /favicon.ico",
        "GET / HTTP",
        "Debugger is active",
        "Debugger PIN",
        "Restarting with stat",
        "Detected change in",
        "Running on ",
        "WARNING: This is a development server",
        "Press CTRL+C to quit",
        "Serving Flask app",
        "Debug mode:",
        " *  ",
    )
    if any(m in line for m in drop_markers):
        return False
    # 保留：业务 logger + 生成请求 + ESP32 下载成片 + 错误
    keep_markers = (
        "[ai_music]",
        "[pipeline]",
        "[dashscope]",
        "[funmusic]",
        "[minimax]",
        "POST /api/generate/pipeline",
        "GET /outputs/",
        "流水线",
        "ERROR",
        "Traceback",
        "ReadTimeout",
        "File \"",
        "raise ",
        "requests.exceptions",
    )
    return any(m in line for m in keep_markers)


@app.get("/api/logs")
def api_logs():
    """返回 pipeline 生成过程相关日志（已过滤轮询/健康检查）。"""
    log_path = _log_dir / "pipeline.log"
    try:
        lines = int(request.args.get("lines") or 200)
    except (TypeError, ValueError):
        lines = 200
    lines = max(20, min(lines, 2000))
    if not log_path.exists():
        return jsonify({"lines": [], "path": str(log_path), "mtime": None, "filtered": 0})
    try:
        text = log_path.read_text(encoding="utf-8", errors="replace")
    except OSError as e:
        return jsonify({"error": str(e), "lines": []}), 500
    all_lines = text.splitlines()
    # 先从尾部多取一些，再过滤，保证过滤后仍有足够行数
    scan = all_lines[-max(lines * 8, 500):] if len(all_lines) > lines * 8 else all_lines
    filtered = [ln for ln in scan if _is_pipeline_log_line(ln)]
    tail = filtered[-lines:] if len(filtered) > lines else filtered
    st = log_path.stat()
    return jsonify({
        "lines": tail,
        "total": len(all_lines),
        "filtered": len(filtered),
        "path": "logs/pipeline.log",
        "mtime": datetime.fromtimestamp(st.st_mtime).strftime("%Y-%m-%d %H:%M:%S"),
    })


@app.delete("/api/logs")
def api_clear_logs():
    """清空 pipeline.log，方便重新观察一轮生成过程。"""
    log_path = _log_dir / "pipeline.log"
    try:
        with open(log_path, "w", encoding="utf-8") as f:
            f.write("")
        # 重新打开 FileHandler，避免句柄仍指向旧内容
        for h in list(logging.root.handlers) + list(logger.handlers):
            if isinstance(h, logging.FileHandler) and Path(getattr(h, "baseFilename", "")).resolve() == log_path.resolve():
                h.close()
                h.stream = open(h.baseFilename, h.mode, encoding=h.encoding)
        logger.info("日志已清空 | by=web")
    except OSError as e:
        return jsonify({"error": str(e)}), 500
    return jsonify({"ok": True, "cleared": "logs/pipeline.log"})


@app.delete("/api/history/<path:filename>")
def api_delete_history(filename: str):
    """删除 outputs/ 下指定成片。"""
    name = Path(filename).name
    if not name or name.startswith(".") or ".." in filename:
        return jsonify({"error": "非法文件名"}), 400
    path = OUTPUTS_DIR / name
    if not path.exists() or not path.is_file():
        return jsonify({"error": "文件不存在"}), 404
    if path.suffix.lower() not in (".mp3", ".wav"):
        return jsonify({"error": "仅允许删除 mp3/wav"}), 400
    try:
        path.unlink()
        meta = path.with_suffix(path.suffix + ".meta.json")
        if meta.is_file():
            meta.unlink()
    except OSError as e:
        return jsonify({"error": str(e)}), 500
    logger.info("删除历史歌曲 | file=%s", name)
    return jsonify({"ok": True, "deleted": name})


@app.delete("/api/uploads/<path:filename>")
def api_delete_upload(filename: str):
    """删除 uploads/ 下录音（支持 browser 根目录或 esp32/ 子目录）。"""
    # 只允许相对 uploads 的单层名，或 esp32/xxx
    raw = filename.replace("\\", "/").lstrip("/")
    if ".." in raw or raw.startswith("/"):
        return jsonify({"error": "非法路径"}), 400
    parts = Path(raw).parts
    if len(parts) == 1:
        path = UPLOAD_DIR / parts[0]
    elif len(parts) == 2 and parts[0] == "esp32":
        path = ESP32_UPLOAD_DIR / parts[1]
    else:
        return jsonify({"error": "路径不在允许范围"}), 400
    if not path.exists() or not path.is_file():
        return jsonify({"error": "文件不存在"}), 404
    if path.name == ".gitkeep":
        return jsonify({"error": "禁止删除"}), 400
    try:
        path.unlink()
    except OSError as e:
        return jsonify({"error": str(e)}), 500
    logger.info("删除上传录音 | file=%s", raw)
    return jsonify({"ok": True, "deleted": raw})


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
    # 环境音可选：未上传且未选内置素材时 env_path 保持 None，流水线自动跳过理解步骤

    # 解析"想说的话"：语音优先，没有语音则必须给文字
    speech_path = None
    if speech_file and speech_file.filename:
        speech_path = save_dir / _safe_name(speech_file.filename)
        speech_file.save(speech_path)
    if speech_path is None and not user_text:
        return jsonify({"error": "想说的话不能为空（上传语音或输入文字）"}), 400
    if len(user_text) > int(os.getenv("MAX_PROMPT_CHARS", "500")):
        return jsonify({"error": "文字过长"}), 400

    client_ip = request.remote_addr or "?"
    src_label = "ESP32" if is_esp32 else "浏览器"
    try:
        env_info = (
            f"{env_path.name}({env_path.stat().st_size}B)"
            if env_path and env_path.exists() else None
        )
        logger.info(
            "收到生成请求 | 来源=%s ip=%s 环境音=%s 语音=%s 文字=%s 时长=%ss",
            src_label, client_ip, env_info,
            (f"{speech_path.name}({speech_path.stat().st_size}B)"
             if speech_path and speech_path.exists() else None),
            user_text[:50] if user_text else None, duration,
        )
        if is_esp32:
            logger.info(
                "ESP32 已上传文件 | env=%s speech=%s",
                env_path.name if env_path else "(无)", speech_path.name if speech_path else "(无)",
            )
        result = pipeline.run(env_path, speech_path, user_text or None, duration)
    except Exception as e:
        logger.error("流水线失败 | 来源=%s ip=%s err=%s", src_label, client_ip, e, exc_info=True)
        return jsonify({"error": "流水线调用失败: " + str(e)}), 502
    result["scheme"] = "cloud"
    logger.info(
        "流水线完成并返回客户端 | 来源=%s ip=%s 输出=%s 歌名=%s 总耗时=%ss",
        src_label, client_ip, result.get("output_url", "?"),
        result.get("song_title", "?"),
        result.get("total_elapsed_sec", "?"),
    )
    return jsonify(result)


# --- 静态文件服务 --------------------------------------------------------

@app.get("/outputs/<path:filename>")
def serve_output(filename: str):
    name = Path(filename).name
    logger.info("成片下载请求 | ip=%s file=%s", request.remote_addr or "?", name)
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
    # 本地开发可用 debug；公网请用 gunicorn，勿开 debug
    _debug = os.getenv("FLASK_DEBUG", "0").lower().strip() in ("1", "true", "yes")
    if AUTH_ENABLED:
        logger.info("简易鉴权已开启（Session 密码 + X-API-Key）")
    else:
        logger.warning("简易鉴权未开启：SITE_PASSWORD 为空（仅适合本地）")
    app.run(host="0.0.0.0", port=int(os.getenv("PORT", "5000")), debug=_debug)
