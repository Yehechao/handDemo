# handDemo C++ 算法与 20 点骨架显示

- **算法部分**：把 18 路 AD 输入转换成五指弯曲角输出
- **OpenCV 绘图部分**：把角度输出驱动到 20 点骨架，并显示轻量 2.5D 效果

当前仓库已经把这两部分拆开：

- [hand_algorithm.h](./hand_algorithm.h) / [hand_algorithm.cpp](./hand_algorithm.cpp)
  负责两步校准、滤波、补偿和弯曲角输出
- [hand_skeleton_viewer.h](./hand_skeleton_viewer.h) / [hand_skeleton_viewer.cpp](./hand_skeleton_viewer.cpp)
  负责 20 点骨架模板、局部角重建和 OpenCV 显示
- [serial_port_io.h](./serial_port_io.h) / [serial_port_io.cpp](./serial_port_io.cpp)
  负责按 `VID/PID` 搜索串口、读取并解析 AD 帧
- [main.cpp](./main.cpp)
  只是 demo 入口，负责把串口、算法和骨架显示串起来

## 1. 项目结构

```text
hand_algorithm.*        纯算法核心
hand_skeleton_viewer.*  OpenCV 20 点骨架显示
serial_port_io.*        串口接收与重连
config.h                通道映射、角度上限、滤波和稳定层参数
main.cpp                控制台 demo
CMakeLists.txt          CMake 构建入口
handDemo.sln            Visual Studio 工程
```

## 2. CMake 编译方式

当前仓库已经内置 OpenCV 头文件和 `.lib`，路径固定为：

```text
opencv/include
opencv/lib
```

### 配置

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

### 编译 Release

```powershell
cmake --build build --config Release
```

### 运行 Release

```powershell
.\build\Release\handDemo.exe
```


```

说明：

- `CMakeLists.txt` 会直接使用仓库内的 `opencv/include` 和 `opencv/lib`
- 如果能找到 `opencv_world480.dll` / `opencv_world480d.dll`，会在构建后自动复制到输出目录
- 如果仓库内没有 DLL，编译仍可完成，但运行前需要把对应 DLL 放到可执行文件目录或加入 `PATH`

## 3. Demo 运行方式

程序启动后会按 `USB\\VID_1A86&PID_7523` 搜索目标串口设备。

按键逻辑：

- `Space`：开始当前阶段 2 秒校准
- `C`：清空当前校准状态并重新开始
- `Q` / `Esc`：退出程序

校准顺序固定为：

```text
Closed -> Fist
```

也就是：

```text
手指伸直 -> 手握拳
```

两步校准完成后，程序会持续接收 AD 数据，输出角度，并驱动 OpenCV 20 点骨架。

## 4. 算法调用文档

### 4.1 核心类

核心算法类定义在 [hand_algorithm.h](./hand_algorithm.h)：

```cpp
class HandAngleAlgorithm {
public:
    void reset();
    void beginCalibration(CalibrationStage stage);
    bool pushCalibrationFrame(const int16_t adValues[kChannelCount]);
    bool finishCalibration();
    bool isReady() const;
    bool processFrame(const int16_t adValues[kChannelCount], HandAngleOutput& outputValue);
};
```

### 4.2 调用顺序

推荐调用顺序如下：

1. 创建 `HandAngleAlgorithm`
2. `beginCalibration(CalibrationStage::Closed)`
3. 连续 2 秒调用 `pushCalibrationFrame(...)`
4. `finishCalibration()`
5. `beginCalibration(CalibrationStage::Fist)`
6. 连续 2 秒调用 `pushCalibrationFrame(...)`
7. `finishCalibration()`
8. `isReady()` 为 `true` 后，持续调用 `processFrame(...)`

### 4.3 输入格式

算法输入固定为 18 路 AD 数据：

```cpp
const int16_t adValues[kChannelCount]
```

其中：

- `kChannelCount = 18`
- 每一路 AD 范围是 `0 ~ 4096`
- 输入排列顺序必须和串口文本一帧的 18 个字段一致

### 4.4 输出结构体

```cpp
struct HandAngleOutput {
    float little_finger[3];
    float ring_finger[3];
    float middle_finger[3];
    float index_finger[3];
    float thumb[2];
};
```

字段语义：

- `little_finger / ring_finger / middle_finger / index_finger`
  - `[0]`：MCP 弯曲角
  - `[1]`：PIP 弯曲角
  - `[2]`：DIP 弯曲角
- `thumb`
  - `[0]`：MCP 弯曲角
  - `[1]`：IP 弯曲角

输出顺序固定为：

```text
little_finger -> ring_finger -> middle_finger -> index_finger -> thumb
```

数值单位：

- 单位统一为 `度`
- 类型统一为 `float`
- 当前输出保留 1 位小数

### 4.5 当前算法保留内容

- 两步校准状态机
- 原始 AD 值滤波
- `closed -> fist` 的弯曲 ratio 归一化
- 弱通道补偿
- ratio 稳定层

### 4.6 当前算法明确不包含的内容

- 展开角输出
- 拇指方向字段
- 所有展开通道计算链
- UI 业务逻辑
- DLL 导出包装

## 5. OpenCV 骨架显示调用文档

### 5.1 核心类

骨架显示类定义在 [hand_skeleton_viewer.h](./hand_skeleton_viewer.h)：

```cpp
class HandSkeletonViewer {
public:
    void resetPose();
    void updateFromAngles(const HandAngleOutput& outputValue);
    int showWindowFrame();
    bool shouldQuitFromKey(int keyValue) const;
    void closeWindow();
};
```

### 5.2 模块职责

这个模块不做算法，也不做串口，只做显示层：

- 内置 20 点 `closed` 模板
- 内置 20 点 `fist` 模板
- 用两套模板预计算局部角模型
- 根据 `HandAngleOutput` 做链式前向运动学重建
- 对最终点位做一层轻量 2.5D 投影
- 用 OpenCV 在 `960x540` 白底窗口中绘制骨架

### 5.3 调用顺序

典型调用方式：

```cpp
handdemo::HandSkeletonViewer viewer;
handdemo::HandAngleOutput outputValue{};

while (true) {
    // outputValue 由算法层持续更新
    viewer.updateFromAngles(outputValue);

    int keyValue = viewer.showWindowFrame();
    if (viewer.shouldQuitFromKey(keyValue)) {
        break;
    }
}

viewer.closeWindow();
```

### 5.4 20 点骨架拓扑

当前使用固定 20 点拓扑：

```text
(0,1) (1,2) (2,3)
(0,4) (4,5) (5,6) (6,7)
(4,8) (8,9) (9,10) (10,11)
(8,12) (12,13) (13,14) (14,15)
(12,16) (16,17) (17,18) (18,19)
(0,16)
```

弯折段映射固定为：

- 拇指：`1-2`、`2-3`
- 食指：`4-5`、`5-6`、`6-7`
- 中指：`8-9`、`9-10`、`10-11`
- 无名指：`12-13`、`13-14`、`14-15`
- 小指：`16-17`、`17-18`、`18-19`

### 5.5 显示层逻辑说明

当前 2.5D 观感不是靠“骨段缩短”，而是分成两层：

1. 用 `closed / fist` 两套模板建立局部角模型
2. 先按二维前向运动学重建骨架
3. 所有点重建完成后，再统一做一次轻量投影变换

这意味着：

- 算法输出仍然是纯角度
- OpenCV 层只负责把角度解释成更自然的手指卷曲显示
- 如果以后要调“立体感方向”，主要改的是 `hand_skeleton_viewer.cpp` 里的投影常量

## 6. main.cpp 的定位

[main.cpp](./main.cpp) 只是业务演示，不是给外部软件直接复用的接口层。

它现在做的事情只有：

- 轮询串口
- 响应 `Space / C / Q`
- 驱动两步校准
- 把 AD 帧送给 `HandAngleAlgorithm`
- 把角度结果送给 `HandSkeletonViewer`

如果后续软件同事要接入正式系统，建议直接复用：

- `HandAngleAlgorithm`
- `HandSkeletonViewer`

而不是直接依赖 `main.cpp`。

## 7. 建议的软件接入方式

如果外部程序已经有自己的串口层或设备层，推荐这样接：

1. 外部系统负责拿到 18 路 AD 数据
2. 把 AD 数据送给 `HandAngleAlgorithm`
3. 拿到 `HandAngleOutput`
4. 再把 `HandAngleOutput` 送给 `HandSkeletonViewer`

这样算法和显示都能复用，业务层替换成本最低。

## 8. 后续如果要包 DLL

当前 `hand_algorithm.*` 已经是 DLL-ready 的纯算法接口，建议只额外做一层很薄的导出包装。

推荐导出函数：

```cpp
create / destroy
reset
beginCalibration
pushCalibrationFrame
finishCalibration
isReady
processFrame
```

如果外部还需要骨架显示，也可以单独再包一层 Viewer 包装，但不要把串口搜索、控制台交互和 OpenCV 窗口控制直接塞进算法 DLL。
