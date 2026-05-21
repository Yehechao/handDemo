# handDemo C++ 纯算法版本

这个项目是手套 AD 数据到真实角度输出的 C++ 版本，当前目标是和 Python 项目的核心算法保持一致。C++ 只保留串口 demo 和纯角度输出，不包含 Python 主程序里的动画 UI。

## 1. 项目结构

```text
config.h              通道映射、角度上限、滤波、门控和收束参数
hand_algorithm.h      纯算法接口与输出结构
hand_algorithm.cpp    校准、滤波、ratio、角度计算
serial_port_io.*      串口搜索、接收和 AD 帧解析
main.cpp              控制台 demo 入口
CMakeLists.txt        CMake 构建入口
opencv/               当前工程依赖的 OpenCV 文件
```

## 2. 编译运行

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

## 3. Demo 操作

程序启动后会按 `VID/PID` 搜索目标串口设备，并持续读取 19 路 AD 数据。

按键：

- `Space`：开始当前阶段校准
- `C`：清空校准并重新开始
- `Q` / `Esc`：退出程序

校准顺序：

```text
Closed -> Fist -> Spread
```

对应姿态：

```text
手闭合/基准 -> 手握拳 -> 手展开
```

三步校准完成后，程序持续输出五指弯曲角和展开角。

## 4. 核心接口

核心类定义在 [hand_algorithm.h](./hand_algorithm.h)：

```cpp
class HandAngleAlgorithm {
public:
    bool setRuntimeConfig(const RuntimeConfig& runtimeConfig);
    void reset();
    void beginCalibration(CalibrationStage stage);
    bool pushCalibrationFrame(const int16_t adValues[kChannelCount]);
    bool finishCalibration();
    bool isReady() const;
    bool processFrame(const int16_t adValues[kChannelCount], HandAngleOutput& outputValue);
};
```

输入固定为 19 路 AD：

```cpp
const int16_t adValues[kChannelCount]
```

其中：

- `kChannelCount = 19`
- 每路 AD 范围约为 `0 ~ 4096`
- 通道顺序对应 `CH1 ~ CH19`

## 5. 输出结构

```cpp
struct HandAngleOutput {
    float little_finger[4];  // [MCP, PIP, DIP, pinky-ring_spread]
    float ring_finger[4];    // [MCP, PIP, DIP, ring-middle_spread]
    float middle_finger[3];  // [MCP, PIP, DIP]
    float index_finger[4];   // [MCP, PIP, DIP, index-middle_spread]
    float thumb[3];          // [MCP, IP, thumb-index_spread] 正=外展, 负=内收
};
```

数值单位统一为 `度`，当前输出保留 1 位小数。

注意：当前 C++ 输出中，`thumb[0]` 使用 `CH18`，`thumb[1]` 使用 `CH17`；如外部系统需要严格按 Python 展示顺序消费，应在接入层确认拇指两个弯曲字段的对应关系。

## 6. 当前算法逻辑

### 弯曲角

弯曲通道使用 `Closed -> Fist` 校准范围计算 ratio：

```text
angle = flexRatio * maxFlexAngle
```

四指通道：

```text
食指:   CH15, CH14, CH13
中指:   CH11, CH10, CH9
无名指: CH7,  CH6,  CH5
小指:   CH3,  CH2,  CH1
```

当前四指角度上限：

```text
MCP/PIP/DIP = 90 / 90 / 90
```

当前拇指角度上限：

```text
CH17 / CH18 = 85 / 85
```

### 四指展开角收束

展开通道先根据 `Closed -> Spread` 计算原始展开角，然后在算法层按相邻两侧第一指节弯曲收束：

```text
rawSpreadAngle = spreadRatio * maxSpreadAngle * angleScale
suppressRatio = smoothstep(0.10, 0.60, max(leftRootFlexRatio, rightRootFlexRatio))
effectiveSpreadAngle = rawSpreadAngle * (1 - suppressRatio)
```

收束对应关系：

```text
CH12 食指-中指展开 -> max(食指根节 CH15, 中指根节 CH11)
CH8  中指-无名指展开 -> max(中指根节 CH11, 无名指根节 CH7)
CH4  无名指-小指展开 -> max(无名指根节 CH7, 小指根节 CH3)
```

中节/末节弯曲不触发展开收束，拇指 CH16 仍走独立开合和门控逻辑。

### 拇指开合

`CH16` 控制拇指开合幅度，门控通道用于判断向外/向内方向。

门控通道在 [config.h](./config.h) 中配置：

```cpp
constexpr int kThumbInwardGateChannel = 18;
```

可选：

```text
18 或 19
```

默认是 `CH18`。可通过 `RuntimeConfig` 在初始化时覆盖为 `CH19`，不调用时继续使用 `config.h` 默认值。

拇指开合公式：

```text
outwardRatio = CH16_open_ratio * (1 - gateRatio)
inwardRatio  = CH16_fist_ratio * gateRatio
thumbSpreadAngle = 45 * outwardRatio - 45 * inwardRatio
```

输出符号：

```text
正数 = 外展
负数 = 内收
```

## 7. 滤波与稳定

当前 C++ 配置：

```cpp
constexpr std::size_t kMeanFilterWindowFrameCount = 15;
constexpr std::size_t kThumbGateFilterWindowSize = 10;
```

含义：

- `kMeanFilterWindowFrameCount`：19 路原始 AD 输入的实时均值滤波窗口。
- `kThumbGateFilterWindowSize`：拇指门控 ratio 的独立均值滤波窗口。

ratio 死区：

```cpp
constexpr double kFlexDeadbandRatio = 0.0;
constexpr double kSpreadDeadbandRatio = 0.0;
constexpr double kThumbGateDeadbandRatio = 0.0;
```

`0.0` 表示不额外压制细小 ratio 变化。

如需在打包 DLL 后由外部配置文件覆盖部分参数，可在校准前调用：

```cpp
HandAngleAlgorithm algorithm;

RuntimeConfig runtimeConfig;
runtimeConfig.meanFilterWindowFrameCount = 15;
runtimeConfig.thumbGateFilterWindowSize = 10;
runtimeConfig.thumbInwardGateChannel = 18;
runtimeConfig.thumbGateDeadbandRatio = 0.0;
runtimeConfig.spreadDeadbandRatio = 0.0;

if (!algorithm.setRuntimeConfig(runtimeConfig)) {
    // 配置非法：窗口必须大于 0，门控通道只允许 18 或 19，deadband 范围为 0.0~1.0
}
```

调用成功后算法会清空旧校准、滤波和稳定状态；因此建议在开始三步校准前调用。

## 8. 与 Python 版本的关系

当前 C++ 算法已对齐 Python 的主要计算逻辑：

- 19 路通道输入
- 三步校准：`Closed / Fist / Spread`
- 拇指开合门控可选 `CH18 / CH19`
- 四指展开角在算法层按根节弯曲收束
- 四指弯曲角上限 `90 / 90 / 90`
- 拇指弯曲角上限 `85 / 85`

已知差异：

- C++ 原始 AD 均值滤波默认 15 帧，可通过 `RuntimeConfig` 覆盖。
- Python 默认滤波窗口来自 `config.json`，当前为 10 帧，并可在 UI 中调整。
- C++ 当前没有 Python 的 UI、动画、语言切换和通道监视器。

## 9. 接入建议

外部系统如果已有自己的串口或设备层，建议只复用：

```cpp
HandAngleAlgorithm
HandAngleOutput
```

典型流程：

```text
1. 创建 HandAngleAlgorithm
2. 依次完成 Closed / Fist / Spread 三步校准
3. isReady() 为 true 后持续调用 processFrame()
4. 使用 HandAngleOutput 中的真实角度
```

`main.cpp` 只是控制台 demo，不建议作为正式业务接口直接依赖。
