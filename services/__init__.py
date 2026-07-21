"""AI 后端客户端的包初始化文件。

云端理解流水线：DashScopeClient + CloudPipeline + build_music_backend
"""
from .dashscope_client import DashScopeClient
from .cloud_pipeline import CloudPipeline, build_music_backend, make_output_url

__all__ = ["DashScopeClient", "CloudPipeline", "build_music_backend", "make_output_url"]
