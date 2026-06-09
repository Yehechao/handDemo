# handDemo — 手部数据手套 C++ 算法 SDK

> Copyright (c) 2026 Matrix 墨现科技. All rights reserved.

## 概述

本项目是手部数据手套的纯算法 C++ SDK，负责将 19 路 AD 传感器原始值转换为五指关节角度输出。可打包为 DLL 供客户集成。

- 输入：19 路 AD 原始值（int16_t[19]）
- 校准：闭合 → 握拳 → 展开 → 串扰补偿（四步）
- 输出：`HandAngleOutput` 结构体（四指 MCP/PIP/DIP + 展开角，拇指 MCP/IP + 开合角）

## 构建

```bash
# CMake
cd build && cmake .. && cmake --build . --config Release

# Visual Studio
# 打开 handDemo.sln → 项目 MatrixHand，工具集 v143，C++17
```

- Debug 链接 `opencv_world480d.lib`，Release 链接 `opencv_world480.lib`
- 额外链接 `Setupapi.lib`、`Advapi32.lib`
- 编译选项 `/utf-8`
- 所有 C++ 源码保存为 **UTF-8 with BOM**

## 架构

```
[串口硬件] ──▶ SerialFrameReceiver.poll() ──▶ int16_t[19] AD帧
                                                    │
                                                    ▼
                                         HandAngleAlgorithm.processFrame()
                                                    │
                              ┌─────────────────────┼─────────────────────┐
                              ▼                     ▼                     ▼
                         均值滤波              实时串扰补偿            buildOutput
                       (窗口=10帧)        (ΔP=aΔT1+bΔT2+cΔT3+d)    (ratio→角度)
```

### 校准流程

```
beginCalibration(Closed) → pushCalibrationFrame × N → finishCalibration()
→ beginCalibration(Fist)  → pushCalibrationFrame × N → finishCalibration()
→ beginCalibration(Spread) → pushCalibrationFrame × N → finishCalibration()
→ beginCalibration(Crosstalk) → pushCalibrationFrame × N → finishCalibration()  // 可选
→ isReady() == true
→ processFrame() × N  // 实时输出
```

三步校准（Closed/Fist/Spread）完成后 `isReady()` 即返回 true。第四步 Crosstalk 为可选，完成后才启用串扰补偿。

## SDK 公开 API

| 方法 | 说明 |
|------|------|
| `setRuntimeConfig(config)` | 覆盖默认参数（在校准前调用） |
| `beginCalibration(stage)` | 开始某阶段校准 |
| `pushCalibrationFrame(ad)`  | 推入一帧 AD 数据 |
| `finishCalibration()` | 结束当前阶段校准 |
| `isReady()` | 三步校准是否完成 |
| `processFrame(ad, output)` | 实时计算角度 |
| `reset()` | 清空全部状态 |
| `getCurrentAd(filtered)` | 获取当前帧 AD 值，true=滤波后，false=原始 |
| `getXtalkUnstableChList()` | 获取串扰 d 异常通道列表 |

## 核心类

### `HandAngleAlgorithm`（hand_algorithm.h/cpp）
所有算法逻辑，无 UI 和串口依赖。

- 常量配置：`config.h`（通道映射、最大角度、死区参数等）
- 运行时可覆盖项：`RuntimeConfig`（滤波窗口、门控通道、死区比、串扰参数）
- channelIndex 统一使用 **1-based**（CH1~CH19），内部通过 `toChannelArrayIndex()` 转为 0-based

### `SerialFrameReceiver`（serial_port_io.h/cpp）
串口自动发现 + 二进制帧协议解析，非阻塞轮询。

### `main.cpp`
控制台测试入口，键盘驱动四步校准 + 实时角度打印。

## 关键参数（config.h）

| 参数 | 值 | 说明 |
|------|----|------|
| 滤波窗口 | 10 帧 | 所有通道共用 |
| 四指弯曲角上限 | MCP=85°, PIP=85°, DIP=90° | 食指/中指/无名指/小指 |
| 拇指弯曲角上限 | MCP=85°, IP=85° | |
| 展开角上限 | 25° | 三组指缝 |
| 拇指开合角 | ±45° | 正=外展，负=内收 |
| 弯曲死区 | 0.015 | flexDeadbandRatio |
| 展开死区 | 0.02 | spreadDeadbandRatio |
| 门控死区 | 0.025 | thumbGateDeadbandRatio |
| 无效通道阈值 | 10.0 | stageDelta < 10 判为无效 |
| 串扰驱动通道 | CH17, CH19, CH16 | |
| 串扰目标通道 | CH15~CH1（15通道） | |
| 串扰排除通道 | CH18 | |
| 串扰截距上限 | 30.0 | \|d\| > 30 标记为异常 |

## 设计约束

- 所有通道共用同一个均值滤波窗口，不做 flex/spread/thumbGate 分组。
- 串扰补偿只修正进入归一化前的 AD 值，不直接修改角度。
- Crosstalk 校准非必须，三步校准即可开始实时输出。

## Python 参考

Python 参考实现位于 `D:/yhc_code/handDemo_py`。C++ 算法与其纯算法部分功能一致（不含 UI、动画、3D 骨架、MANO）。

## 编码约定

- 类名 `PascalCase`，函数/变量 `camelCase`，常量 `kCamelCase`
- 成员变量以 `_` 结尾
- 公开 API 命名保持清晰，内部命名使用缩写（Calib/Ch/Val/Frm/Coef/Xtalk）
- C++ 源文件保存为 UTF-8 with BOM
