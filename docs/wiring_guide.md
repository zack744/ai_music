# ESP32 外设接线指南

> 板子: **VIEWE UEDX24320028E-WB-A V1.1** (ESP32-S3-N16R8)  
> 官方仓库: https://gitee.com/VIEWESMART/UEDX24320028ESP32-2.8inch-Touch-Display  
> 固件引脚: `esp32_firmware_idf55/src/config.h`  
> 更新: 2026-07-23

---

## 0. 板子背面排针（官方 PCB 丝印）

板子**背面**上下各一排孔（约 20P）。丝印在孔旁边。

### 上排（靠近 TF 卡 / ESP 模组一侧）

从左到右（对着背面看，TF 在左上）：

`IO21 IO12 IO11 IO10 IO9 IO20 IO19 IO8 IO18 IO17 IO16 IO15 IO6 IO5 IO4 CHIP-en 3.3V GND`

### 下排（靠近 viewe Logo / 按键一侧）

从左到右：

`5V LEDA LEDK IO0 IO3 IO14 IO15? IO47 IO48 IO38 IO39 IO41 IO42 TX RX IO2 IO1 GND`  
（以你板子丝印为准；原理图 J2/J3 为完整定义）

### 电源孔

| 电源 | 位置 | 数量 |
|------|------|------|
| **3.3V** | 上排靠右（`3.3V` 丝印） | **通常就 1 个排针孔** |
| **GND** | 上排最右 + 下排最右 | 多个 |
| **5V** | 下排最左（`5V` 丝印） | 1 个 |

所以你说「3V3 只有一个接口」是对的。  
**麦 VDD + 功放 SD 都要 3.3V：从这一个 3V3 分两根线出去即可**（面包板 / 一拖二杜邦线）。

官方原理图 `Schematic/UEDX24320028E-WB-A V1.1 sch.png` 里 J2/J3 也标了  
`VDD 3V3`、`USB-5V`、`GND`。

---

## 1. 和固件一致的接线（config.h）

### 1.1 MAX98357A 功放

| 板子丝印 | MAX98357A | 说明 |
|----------|-----------|------|
| **5V**（下排左） | VIN | 功放供电 |
| **GND** | GND | 共地 |
| **IO14**（下排） | BCLK | 位时钟 |
| **IO5**（上排） | LRC | 左右时钟 |
| **IO6**（上排） | DIN | 数据 ESP→功放 |
| **3.3V**（上排） | SD | 使能；与麦共用 3V3 |
| GAIN | 悬空 | 默认增益 |
| OUT+ / OUT− | 喇叭 | |

### 1.2 INMP441 麦克风

| 板子丝印 | INMP441 | 说明 |
|----------|---------|------|
| **3.3V**（上排，与功放 SD 共用） | VDD | 麦供电 |
| **GND** | GND | 共地 |
| **IO10**（上排） | SCK / BCLK | 位时钟 |
| **IO11**（上排） | WS | 左右时钟 |
| **IO12**（上排） | SD / DOUT | 数据 麦→ESP |
| **GND** | L/R | 左声道 |

> 官方 PCB **上排有 IO10 / IO11 / IO12 丝印**，可直接接，无需改固件。

---

## 2. 一览

| 外设 | BCLK | WS/LRC | DATA | 电源 |
|------|------|--------|------|------|
| 麦克风 | IO10 | IO11 | IO12 | 3V3 + GND |
| 功放 | IO14 | IO5 | IO6 | 5V + GND；SD→3V3 |

---

## 3. 3V3 怎么分

```
板子 3.3V ──┬── INMP441 VDD
            └── MAX98357A SD
板子 GND  ──┬── 麦 GND、L/R
            ├── 功放 GND
            └── （喇叭不需要单独地）
板子 5V   ──── MAX98357A VIN
```

---

## 4. 不要占用

| 丝印 | 原因 |
|------|------|
| IO13 / BL | 背光（原理图 IO13-BL-EN） |
| IO15–IO18 | TF/SD |
| IO1 / IO3 | 触摸 I2C |
| IO19 / IO20 | USB |
| IO42/40/45/41/39 | LCD SPI |
| IO0 | RGB LED / BOOT |
| IO38 | 蜂鸣器 |
| TX / RX | 调试串口 |

---

## 5. 注意

1. 断电再插线  
2. 功放 **VIN=5V**，麦 **VDD=3V3**  
3. 3V3 孔少：分线共用即可  
4. 两路 I2S 时钟不要并  
5. 串口应看到：`[play] MAX98357A I2S1 init OK`  

官方资料：  
- 原理图: `Schematic/UEDX24320028E-WB-A V1.1 sch.png`  
- 规格书: `information/UEDX24320028E-WB-A V1.0 SPEC.pdf`  
- 背面标注图: `image/moudle.jpg`
