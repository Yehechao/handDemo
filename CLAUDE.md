# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

手部数据手套算法演示项目：将 18 路 AD 输入转换为五指弯曲角度，并通过 OpenCV 显示 20 点手部骨架。

**关键约束**：仅支持 Windows（串口依赖 Win32 API），使用 C++17。

## 构建命令

```powershell
# CMake 配置（首次或修改 CMakeLists.txt 后）
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# CMake 编译 Release
cmake --build build --config Release

# 运行
.\build\Release\handDemo.exe

# 或直接用 MSBuild（已有 .sln）
MSBuild.exe handDemo.sln /p:Configuration=Release /p:Platform=x64
```

OpenCV 已内置在仓库 `opencv/` 目录，无需额外安装。构建后 DLL 会自动复制到输出目录。

## 架构分层

代码按职责拆为四个独立模块，依赖方向为 `main.cpp → 其他三个模块`：

```
main.cpp（demo 入口）
  ├── serial_port_io.h/.cpp    — 串口接收层
  ├── hand_algorithm.h/.cpp    — 算法核心层
  └── hand_skeleton_viewer.h/.cpp — 骨架显示层
```

### 1. config.h — 全局配置
所有可调参数集中在此：通道数（18）、AD 范围（0-4096）、采样时长、滤波窗口、死区、补偿参数、通道/手指映射关系、角度上限。所有常量在 `handdemo` 命名空间内，以 `constexpr` 定义。

### 2. hand_algorithm — 算法核心
`HandAngleAlgorithm` 类提供两步校准状态机（Closed → Fist）和实时角度计算：
- 均值滤波（15 帧滑动窗口）
- 弯曲比归一化（基于 closed/fist 两套基准）
- 弱通道补偿（默认关闭）
- ratio 稳定层（死区滤波）
- 输出 `HandAngleOutput`（五指弯曲角，单位度，float）

### 3. hand_skeleton_viewer — 骨架显示
`HandSkeletonViewer` 类负责 20 点手部骨架的 OpenCV 可视化：
- 内置 closed/fist 两套 20 点模板
- 根据角度做链式前向运动学重建
- 轻量 2.5D 投影
- `960x540` 白底窗口

### 4. serial_port_io — 串口通信
`SerialFrameReceiver` 类按 `USB\VID_1A86&PID_7523` 搜索串口设备，读取并解析 AD 数据帧，内置自动重连和超时告警。

### 5. main.cpp — Demo 入口
串接串口、算法、显示三层：
- 按键 `Space` = 开始校准采样（2 秒），`C` = 重置校准，`Q/Esc` = 退出
- 校准顺序：Closed（伸直）→ Fist（握拳）

## 命名空间与类型约定

- 所有代码在 `namespace handdemo` 内
- 输出结构体 `HandAngleOutput` 使用下划线命名（如 `little_finger`），此为协议字段，不改动
- 其余代码使用 `camelCase`（局部变量和函数）

## 输出格式

```cpp
struct HandAngleOutput {
    float little_finger[3];  // [MCP, PIP, DIP]
    float ring_finger[3];
    float middle_finger[3];
    float index_finger[3];
    float thumb[2];          // [MCP, IP]
};
```

手指顺序：小指 → 无名指 → 中指 → 食指 → 拇指。单位统一为度，float 类型。

## 测试

当前无自动化测试套件。验证方式为编译 Release 后连接硬件运行 demo，检查两步校准流程和角度输出。

## 代码风格

- 4 空格缩进
- UTF-8 编码（MSVC 通过 `/utf-8` 编译选项强制）
- 仅在逻辑不直观处加注释
- 算法代码与 I/O 代码严格分离
