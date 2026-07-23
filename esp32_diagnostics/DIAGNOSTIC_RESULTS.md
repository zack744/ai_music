# ESP32-S3 N16R8 最小诊断结果

> 测试日期：2026-07-22  
> 板卡：VIEWE UEDX24320028E-WB-A（ESP32-S3-N16R8）  
> 串口：COM8

## 测试环境

| 项目 | 版本/配置 |
|---|---|
| Platform | pioarduino 55.03.39 |
| Arduino-ESP32 | 3.3.9 |
| ESP-IDF | 5.5.4 |
| Flash | 16MB，QIO，80MHz |
| PSRAM | 8MB，Octal/OPI，80MHz |
| DCache | 32KB，32-byte line |
| WiFi/LWIP 优先 PSRAM | 否 |
| 显示/LVGL/音频 | 均未加载 |

## 结果

| 阶段 | 内容 | 结果 |
|---|---|---|
| A | 2MB PSRAM，8轮写入/读回校验 | PASS，0 error |
| B0 | 使用已保存凭据连接 `asus` | PASS，IP=192.168.50.65 |
| B1 | WiFi 在线时，2MB PSRAM 12轮写入/读回 | PASS，0 error |
| C1 | HTTP 下载 602419 bytes，仅内部 SRAM 接收 | PASS |
| C2 | HTTP 4KB 内部 SRAM 中转并复制到 PSRAM | PASS，checksum=fc99e7f4 |
| 存活 | 测试完成后持续在线约110秒 | PASS，无 panic/重启 |

## 结论

1. 板载 Octal PSRAM 硬件正常，不是板卡性能不足。
2. ESP32-S3 可稳定并发运行 WiFi 与大块 PSRAM 读写。
3. 新版 Arduino 3.3.9 / ESP-IDF 5.5.4 可稳定完成当前播放器所需的网络下载和 PSRAM 缓冲路径。
4. 旧固件的 Cache panic 属于 Arduino 3.1.1 / ESP-IDF 5.3.2 软件栈或其预编译库组合问题。
5. 完整固件下一步应迁移到 pioarduino 55.03.39，而不是继续修补旧版底层库。

## 原始日志

- `../logs/serial_diag_55_03_39_20260722.txt`
- `build_diag_55_03_39.log`
- `upload_diag_55_03_39.log`
