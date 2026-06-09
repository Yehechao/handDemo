# C++ SDK 算法适配开发文档

## 目标

把当前 C++ 项目 `D:/yhc_code/handDemo_c/handDemo` 的纯算法部分，对齐 Python 上位机当前版本的算法输出。

本轮只关注：

- 从 AD 输入到关节角度输出。
- 校准数据如何建立。
- 串扰补偿如何拟合和实时应用。
- 滤波、死区、无效通道判定、最大角度配置如何保持一致。

本轮不关注：

- UI。
- 动画。
- 3D 骨架。
- MANO 手模。
- 打包。

## 当前 C++ 已实现能力

C++ 当前核心文件：

- `D:/yhc_code/handDemo_c/handDemo/hand_algorithm.h`
- `D:/yhc_code/handDemo_c/handDemo/hand_algorithm.cpp`
- `D:/yhc_code/handDemo_c/handDemo/config.h`
- `D:/yhc_code/handDemo_c/handDemo/main.cpp`
- `D:/yhc_code/handDemo_c/handDemo/serial_port_io.cpp`

当前 C++ 已经实现：

- 19 路 AD 输入。
- `Closed / Fist / Spread` 三步校准。
- 每阶段采样多帧并求平均。
- AD 均值滤波。
- AD 归一化为 `0~1` ratio。
- ratio 死区稳定。
- 四指 MCP/PIP/DIP 角度输出。
- 四指展开角输出。
- 大拇指两段弯曲角输出。
- 大拇指开合角输出，正值为外展，负值为内收。
- 大拇指内收门控，默认使用 CH18。

当前 C++ 还不是 Python 当前算法的完整适配版，下面列出必须补齐的内容。

## Python 参考源码

Python 当前项目路径：

- `D:/yhc_code/handDemo_py`

重点参考文件：

- `D:/yhc_code/handDemo_py/config.json`
- `D:/yhc_code/handDemo_py/core/kinematics.py`
- `D:/yhc_code/handDemo_py/core/hand_landmark_ui.py`
- `D:/yhc_code/handDemo_py/core/serial_protocol.py`
- `D:/yhc_code/handDemo_py/core/serial_shared.py`
- `D:/yhc_code/handDemo_py/core/config_validator.py`

核心参考函数：

- `core/kinematics.py::calculateChannelRatio`
- `core/kinematics.py::getInvalidChannelReason`
- `core/kinematics.py::getInvalidChannelType`
- `core/kinematics.py::applyCrosstalkCompensationToChannelValueList`
- `core/kinematics.py::calculateFlexRatioValue`
- `core/kinematics.py::getFlexRatio`
- `core/kinematics.py::getSpreadRatio`
- `core/kinematics.py::getThumbInwardGateRatio`
- `core/kinematics.py::getThumbInwardAmplitudeRatio`
- `core/kinematics.py::buildSpreadState`
- `core/kinematics.py::buildMotionState`
- `core/kinematics.py::buildEffectiveSpreadRatioByPairName`
- `core/kinematics.py::getThumbDisplaySpreadAngle`
- `core/kinematics.py::buildAngleInfoDict`
- `core/hand_landmark_ui.py::fitCrosstalkCoefficientForTargetChannel`
- `core/hand_landmark_ui.py::buildCrosstalkCoefficientByTargetChannel`
- `core/hand_landmark_ui.py::applyCrosstalkCalibrationFrameList`
- `core/hand_landmark_ui.py::applyStageCalibrationValue`

## 必须实现功能 1：第四阶段串扰补偿校准

### 当前差异

Python 当前校准阶段是：

```text
closed -> fist -> open -> crosstalk
```

C++ 当前只有：

```text
Closed -> Fist -> Spread
```

C++ 缺少 `Crosstalk` 阶段。

### Python 行为

Python 第四阶段会采集完整时间内的所有 AD 帧，不取单帧。

拟合公式：

```text
ΔP = aΔT1 + bΔT2 + cΔT3 + d
```

默认驱动通道：

```text
CH17, CH19, CH16
```

默认目标通道：

```text
CH15, CH14, CH13, CH12,
CH11, CH10, CH9, CH8,
CH7, CH6, CH5, CH4,
CH3, CH2, CH1
```

排除通道：

```text
CH18
```

其中：

```text
ΔT1 = 当前帧 CH17 - baseline CH17
ΔT2 = 当前帧 CH19 - baseline CH19
ΔT3 = 当前帧 CH16 - baseline CH16
ΔP  = 当前帧目标 CH - baseline 目标 CH
```

baseline 使用第四阶段采样序列的第一帧。

### C++ 实现建议

在 `hand_algorithm.h` 中扩展校准阶段：

```cpp
enum class CalibrationStage : int32_t {
    Closed = 0,
    Fist = 1,
    Spread = 2,
    Crosstalk = 3,
};
```

新增串扰系数结构：

```cpp
struct CrosstalkCoefficient {
    double a = 0.0;
    double b = 0.0;
    double c = 0.0;
    double d = 0.0;
    bool isValid = false;
};
```

新增成员变量：

```cpp
std::array<CrosstalkCoefficient, kChannelCount> crosstalkCoefficientByChannel_{};
std::array<double, kChannelCount> crosstalkBaselineValueList_{};
bool hasCrosstalkCalibration_ = false;
```

当前 `SamplingState` 只累计 sum，无法拟合串扰。需要在采样状态中保留完整采样帧：

```cpp
std::deque<std::array<double, kChannelCount>> frameValueList;
```

`pushCalibrationFrame()` 在 `Crosstalk` 阶段也要保存完整帧。

`finishCalibration()` 遇到 `Crosstalk` 时，不再只调用平均值逻辑，而是调用串扰拟合逻辑。

### 拟合方式

Python 使用最小二乘拟合。C++ 可以用普通方程求解 4 个参数。

设计矩阵每一行：

```text
[ΔT1, ΔT2, ΔT3, 1]
```

目标：

```text
ΔP
```

求：

```text
[a, b, c, d]
```

因为只有 4 个参数，C++ 不需要引入大型库。直接构造 `4x4` 的 `X^T X` 和 `4x1` 的 `X^T y`，再用高斯消元即可。

如果 `fitIntercept=false`，则只拟合 `[a,b,c]`，`d=0`。当前 Python 配置默认 `fitIntercept=true`。

### 参考配置

Python `config.json`：

```json
"crosstalkCompensation": {
    "enabled": true,
    "fitIntercept": true,
    "maxAbsIntercept": 30.0,
    "calibrationDurationSecond": 4.0,
    "driverChannelList": [17, 19, 16],
    "targetChannelList": [15,14,13,12,11,10,9,8,7,6,5,4,3,2,1],
    "excludedChannelList": [18],
    "coefficientByTargetChannel": {}
}
```

## 必须实现功能 2：实时串扰补偿

### 当前差异

C++ 当前实时流程：

```text
raw AD -> 均值滤波 -> ratio -> angle
```

Python 当前实时流程：

```text
raw AD -> 滤波 -> 串扰补偿 -> ratio -> angle
```

### Python 行为

参考：

- `core/kinematics.py::applyCrosstalkCompensationToChannelValueList`
- `core/kinematics.py::buildMotionState`

实时对每个目标通道计算：

```text
predictedDelta = d
predictedDelta += a * (CH17 - baseline CH17)
predictedDelta += b * (CH19 - baseline CH19)
predictedDelta += c * (CH16 - baseline CH16)
```

然后：

```text
correctedTargetCH = rawTargetCH - predictedDelta
```

补偿后的 AD 再进入 `closed/fist/open` 归一化。

### C++ 实现建议

新增函数：

```cpp
std::array<double, kChannelCount> applyCrosstalkCompensation(
    const std::array<double, kChannelCount>& channelValueList) const;
```

在 `processFrame()` 中修改顺序：

```cpp
auto filteredValueList = filterFrameValueList(adValues);
auto correctedValueList = applyCrosstalkCompensation(filteredValueList);
buildOutputValue(correctedValueList, outputValue);
```

注意：串扰补偿只改算法输入 AD，不直接改最终角度。

## 必须实现功能 3：无效通道判定

### 当前差异

Python 会判断某个通道的标定跨度是否有效。

C++ 当前只在 `endValue <= startValue + 1.0` 时返回 0，没有使用 Python 的无效通道规则。

### Python 行为

参考：

- `core/kinematics.py::getInvalidChannelReason`
- `core/kinematics.py::getInvalidChannelType`

规则：

```text
stageDelta = stageValue - closedValue
```

如果：

```text
stageDelta <= 0
```

则通道无效。

如果：

```text
stageDelta < invalidChannelSpanThresholdValue
```

也无效。

Python 当前阈值：

```text
sampling.invalidChannelSpanThresholdValue = 10.0
```

无效通道输出角度为 0。

### C++ 实现建议

在 `config.h` 新增：

```cpp
constexpr double kInvalidChannelSpanThresholdValue = 10.0;
```

新增函数：

```cpp
bool isChannelValidForStage(int channelIndex, CalibrationStage stage) const;
```

在 `getFlexRatio()` 中：

```cpp
if (!isChannelValidForStage(channelIndex, CalibrationStage::Fist)) {
    return 0.0;
}
```

在 `getSpreadRatio()` 中：

```cpp
if (!isChannelValidForStage(channelIndex, CalibrationStage::Spread)) {
    return 0.0;
}
```

## 必须实现功能 4：同步 Python 当前最大角度

### 当前差异

C++ `config.h` 里的角度和 Python 当前配置不完全一致。

Python 当前 `config.json`：

```json
"index":  {"openRootAngle": 25, "holdRootAngle": 85, "holdJointAngleList": [85, 90]},
"middle": {"openRootAngle": 25, "holdRootAngle": 85, "holdJointAngleList": [85, 90]},
"ring":   {"openRootAngle": 25, "holdRootAngle": 85, "holdJointAngleList": [85, 90]},
"pinky":  {"openRootAngle": 25, "holdRootAngle": 85, "holdJointAngleList": [85, 90]},
"thumb":  {"openPalmAngle": 45, "thumbInwardPalmAngle": 45, "holdSegment23Angle": 85, "holdSegment34Angle": 85}
```

C++ 当前：

- 四指弯曲是 `90,90,90`。
- 中指/无名指展开配置里有旧的 `20` 和 `angleScale`。
- 拇指第二段弯曲是 `90`，Python 是 `85`。

### C++ 实现建议

修改 `config.h`：

```cpp
constexpr std::array<FingerFlexAngleModel, 4> kFingerFlexAngleModelByIndex = {{
    {85.0, 85.0, 90.0},
    {85.0, 85.0, 90.0},
    {85.0, 85.0, 90.0},
    {85.0, 85.0, 90.0},
}};
```

拇指：

```cpp
constexpr ThumbFlexAngleModel kThumbFlexAngleModel = {
    85.0,
    85.0,
};
```

展开最大角统一：

```cpp
ringPinky: 25
middleRing: 25
indexMiddle: 25
```

## 必须实现功能 5：去掉旧版展开 angleScale

### 当前差异

Python 当前不再使用旧的 `fingerSpreadAngleScaleByFingerName`。

C++ 当前仍然：

```cpp
rawSpreadAngle = spreadRatio * openRootAngle * angleScale;
```

这会导致 C++ 展开角和 Python 不一致。

### C++ 实现建议

`SpreadPairConfig` 删除或忽略 `angleScale`。

修改：

```cpp
const double rawSpreadAngle = spreadRatio * spreadConfig.openRootAngle;
```

保留弯曲压制展开逻辑：

```cpp
rawSpreadAngle * (1.0 - suppressRatio)
```

## 必须实现功能 6：同步 Python 滤波和死区参数

### 当前差异

Python 当前默认：

```text
movingAverageFlexWindowSize = 10
movingAverageSpreadWindowSize = 10
movingAverageThumbGateWindowSize = 10
flexDeadbandRatio = 0.015
spreadDeadbandRatio = 0.02
thumbGateDeadbandRatio = 0.025
```

C++ 当前：

```text
kMeanFilterWindowFrameCount = 15
kFlexDeadbandRatio = 0.0
kSpreadDeadbandRatio = 0.0
kThumbGateDeadbandRatio = 0.0
```

### C++ 实现建议

最短路径：

```cpp
constexpr std::size_t kMeanFilterWindowFrameCount = 10;
constexpr std::size_t kThumbGateFilterWindowSize = 10;
constexpr double kFlexDeadbandRatio = 0.015;
constexpr double kSpreadDeadbandRatio = 0.02;
constexpr double kThumbGateDeadbandRatio = 0.025;
```

如果要严格对齐 Python，需要分成 flex/spread/thumbGate 三组滤波窗口。当前 SDK 若只输出角度，可以先保持整帧均值窗口为 10，但必须在文档和测试中确认输出误差是否可接受。

## 必须实现功能 7：同步大拇指开合计算

### 当前差异

C++ 已经接近 Python，但要确认两个 ratio 来源一致。

Python：

```text
thumbSpreadRatio = getSpreadRatio(CH16)
inwardGateRatio = getThumbInwardGateRatio(CH18 or CH19)
thumbInwardAmplitudeRatio = calculateChannelRatio(CH16, closed, fist)
thumbOutwardRatio = thumbSpreadRatio * (1 - inwardGateRatio)
thumbInwardRatio = thumbInwardAmplitudeRatio * inwardGateRatio
thumbDisplayAngle = openPalmAngle * outwardRatio - thumbInwardPalmAngle * inwardRatio
```

C++ 当前基本一致，但需要配合串扰补偿和无效通道判定重新检查。

### C++ 实现建议

保留当前结构，但注意：

- `CH16` 在 open 标定中用于外展。
- `CH16` 在 fist 标定中也用于内收幅度。
- `CH18/CH19` 只作为门控比例。
- 先串扰补偿，再计算上述 ratio。

## 建议输出结构扩展

当前 C++ 输出：

```cpp
struct HandAngleOutput {
    float little_finger[4];
    float ring_finger[4];
    float middle_finger[3];
    float index_finger[4];
    float thumb[3];
};
```

如果 SDK 客户只需要角度，这个结构可以保留。

如果客户后续需要调试，建议新增可选调试输出，不影响原结构：

```cpp
struct HandAlgorithmDebugInfo {
    double rawChannelValueList[kChannelCount];
    double filteredChannelValueList[kChannelCount];
    double correctedChannelValueList[kChannelCount];
    double flexRatioByChannel[kChannelCount];
    double spreadRatioByChannel[kChannelCount];
    double thumbOutwardRatio;
    double thumbInwardRatio;
};
```

这不是必须项，但对后续 SDK 验收非常有用。

## 验收标准

### 1. 三步基础角度验收

用同一组 AD 数据，在 Python 和 C++ 中完成：

```text
closed -> fist -> open
```

然后输入同一帧 AD，检查输出角度是否一致。

允许误差建议：

```text
abs(cppAngle - pythonAngle) <= 0.2 度
```

### 2. 串扰拟合验收

用同一段 `crosstalk` 采样帧，Python 和 C++ 拟合出每个目标通道的：

```text
a,b,c,d
```

允许误差建议：

```text
abs(cppCoeff - pythonCoeff) <= 1e-6
```

如果 C++ 使用不同数值求解方式，误差可以放宽到：

```text
1e-4
```

### 3. 实时串扰补偿验收

输入同一帧 AD，比较补偿后的目标通道：

```text
corrected CH15~CH1
```

允许误差：

```text
abs(cppCorrected - pythonCorrected) <= 1e-4
```

### 4. 无效通道验收

构造某通道：

```text
fistValue - closedValue <= 0
```

或者：

```text
fistValue - closedValue < 10
```

该通道角度必须输出 0。

### 5. 展开角验收

确认 C++ 不再使用旧 `angleScale`。

同一 ratio 下：

```text
spreadAngle = openRootAngle * ratio * suppress
```

不要再乘 `1.12 / 1.30 / 1.40`。

## 推荐开发顺序

1. 先同步 `config.h` 中的最大角度、滤波窗口、死区参数。
2. 去掉展开 `angleScale`。
3. 增加无效通道判定。
4. 增加 `Crosstalk` 校准阶段和完整采样帧缓存。
5. 实现 `4x4` 最小二乘拟合 `a,b,c,d`。
6. 在实时 `processFrame()` 中加入串扰补偿。
7. 写一个小型 C++ 测试入口，对比 Python 输出。

## 不要做的事

- 不要迁移 Python 的 3D 骨架、MANO、动画逻辑。
- 不要把 UI 逻辑塞进 SDK 算法类。
- 不要为了兼容旧版保留两套展开算法。
- 不要让串扰补偿直接修改最终角度，它只应该修改进入归一化前的 AD。
- 不要改变通道编号含义，C++ 和 Python 都使用 `CH1~CH19` 的 1-based 通道编号。

## 当前最重要缺口总结

如果只做 SDK 给客户用，当前必须补齐这四件事：

1. `Crosstalk` 第四阶段校准。
2. `CH15~CH1` 的 `a,b,c,d` 串扰补偿。
3. Python 当前配置参数同步。
4. 无效通道判定。

完成以上后，C++ 才能认为是当前 Python 纯算法版本的可交付 SDK 基础。
