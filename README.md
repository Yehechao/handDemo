# handDemo C++ 算法 SDK 开发说明

## 项目目标

本项目目标是把 Python 上位机当前版本的纯算法链路迁移成 C++ SDK，供客户在自己的程序中直接调用。

当前只关注算法：

- 输入：19 路 AD 原始值。
- 校准：闭合、握拳、展开、串扰补偿。
- 输出：各手指关节角度。

当前不关注：

- UI。
- 动画。
- 3D 骨架。
- MANO 手模。
- 打包发布。

## 当前代码状态

核心代码：

- `config.h`：固定配置、通道映射、最大角度。
- `hand_algorithm.h`：算法类接口。
- `hand_algorithm.cpp`：校准、滤波、AD 到角度计算。
- `serial_port_io.*`：串口读取和协议解析。
- `main.cpp`：当前控制台测试入口。
- `ALGORITHM_PORTING_TODO.md`：详细迁移任务文档。

当前 C++ 已实现：

- 19 路 AD 输入。
- `Closed / Fist / Spread` 三步校准。
- 校准阶段多帧平均。
- AD 均值滤波。
- AD 归一化为 `0~1` ratio。
- ratio 死区稳定。
- 四指 MCP/PIP/DIP 角度输出。
- 四指展开角输出。
- 大拇指两段弯曲角输出。
- 大拇指开合角输出，正值外展，负值内收。

当前 C++ 还没有完全对齐 Python 最新算法。

## 必须补齐的算法功能

详细实现方式见：

- `ALGORITHM_PORTING_TODO.md`

必须补齐：

1. 增加第四阶段 `Crosstalk` 串扰补偿校准。
2. 使用完整串扰采样序列拟合 `ΔP = aΔT1 + bΔT2 + cΔT3 + d`。
3. 实时计算角度前，对 `CH15~CH1` 做串扰补偿。
4. 增加无效通道判定：目标姿态相对闭合变化小于阈值时，该通道角度输出 0。
5. 同步 Python 当前最大角度配置。
6. 去掉 C++ 旧版展开 `angleScale`，展开角只使用 `openRootAngle * ratio`。
7. 同步 Python 当前滤波窗口和死区参数。

## Python 参考路径

Python 项目：

- `D:/yhc_code/handDemo_py`

重点参考：

- `D:/yhc_code/handDemo_py/config.json`
- `D:/yhc_code/handDemo_py/core/kinematics.py`
- `D:/yhc_code/handDemo_py/core/hand_landmark_ui.py`
- `D:/yhc_code/handDemo_py/core/serial_protocol.py`

核心函数：

- `core/kinematics.py::applyCrosstalkCompensationToChannelValueList`
- `core/kinematics.py::calculateChannelRatio`
- `core/kinematics.py::getInvalidChannelReason`
- `core/kinematics.py::getFlexRatio`
- `core/kinematics.py::getSpreadRatio`
- `core/kinematics.py::getThumbInwardGateRatio`
- `core/kinematics.py::buildMotionState`
- `core/hand_landmark_ui.py::fitCrosstalkCoefficientForTargetChannel`
- `core/hand_landmark_ui.py::buildCrosstalkCoefficientByTargetChannel`
- `core/hand_landmark_ui.py::applyStageCalibrationValue`

## C++ 命名精简规范

当前 C++ 函数和变量名偏长，例如：

- `buildAverageCalibrationFrame`
- `applyStageCalibrationValue`
- `closedCalibrationValueList_`
- `spreadStableStateByChannel_`
- `thumbInwardGateChannel`

后续开发 SDK 时建议统一精简。原则是：**短，但不能失去含义**。

### 推荐缩写

- `Calibration` 简写为 `Calib`
- `Channel` 简写为 `Ch`
- `Value` 简写为 `Val`
- `Frame` 简写为 `Frm`
- `Coefficient` 简写为 `Coef`
- `Crosstalk` 简写为 `Xtalk`
- `Ratio` 保持 `Ratio`
- `Angle` 保持 `Angle`
- `Thumb` 保持 `Thumb`
- `Spread` 保持 `Spread`
- `Flex` 保持 `Flex`

### 推荐命名示例

函数名建议：

- `buildAverageCalibrationFrame()` 改为 `avgCalibFrm()`
- `applyStageCalibrationValue()` 改为 `setStageCalib()`
- `buildStageCalibrationTemplate()` 改为 `stageCalibTpl()`
- `calculateChannelRatio()` 改为 `calcChRatio()`
- `applyCrosstalkCompensation()` 改为 `applyXtalk()`
- `fitCrosstalkCoefficientForTargetChannel()` 改为 `fitXtalkCoef()`
- `buildCrosstalkCoefficientByTargetChannel()` 改为 `fitXtalkCoefs()`
- `getThumbGateRatio()` 改为 `thumbGateRatio()`
- `buildOutputValue()` 改为 `buildOutput()`

成员变量建议：

- `closedCalibrationValueList_` 改为 `closedCalib_`
- `fistCalibrationValueList_` 改为 `fistCalib_`
- `spreadCalibrationValueList_` 改为 `openCalib_`
- `hasClosedCalibration_` 改为 `hasClosed_`
- `hasFistCalibration_` 改为 `hasFist_`
- `hasSpreadCalibration_` 改为 `hasOpen_`
- `crosstalkCoefficientByChannel_` 改为 `xtalkCoef_`
- `crosstalkBaselineValueList_` 改为 `xtalkBase_`
- `rawFilterState_` 改为 `rawFilter_`
- `flexStableStateByChannel_` 改为 `flexStable_`
- `spreadStableStateByChannel_` 改为 `spreadStable_`

结构体建议：

- `HandAngleAlgorithm` 可保留，作为 SDK 主类名。
- `HandAngleOutput` 可保留，作为客户接口输出结构。
- `RuntimeConfig` 可保留，作为客户配置结构。
- `CrosstalkCoefficient` 建议改为 `XtalkCoef`。
- `RatioStableState` 建议改为 `RatioState`。
- `SamplingState` 建议改为 `SampleState`。
- `RawFilterState` 建议改为 `FilterState`。

### 命名边界

不要为了短而使用难懂缩写：

- 不建议 `c`, `v`, `r`, `s` 这种单字母成员变量。
- 不建议把公开 API 改得太晦涩。
- 客户会直接调用的类、结构体和函数，可以比内部变量稍长一点。

推荐规则：

- 公开 API：清楚优先，例如 `processFrame()`、`beginCalibration()` 可以保留。
- 内部函数：短一点，例如 `calcChRatio()`、`applyXtalk()`。
- 内部成员变量：短一点，例如 `closedCalib_`、`xtalkCoef_`。

## 推荐开发顺序

1. 先按 Python 当前 `config.json` 同步 `config.h` 参数。
2. 精简内部命名，但先不要改变公开 API。
3. 去掉展开 `angleScale`。
4. 增加无效通道判定。
5. 增加 `Crosstalk` 校准阶段和采样帧缓存。
6. 实现 `a,b,c,d` 最小二乘拟合。
7. 在实时 `processFrame()` 中加入串扰补偿。
8. 用同一组 AD 数据对比 Python 和 C++ 输出角度。

## 验收标准

角度输出：

```text
abs(cppAngle - pythonAngle) <= 0.2 度
```

串扰系数：

```text
abs(cppCoef - pythonCoef) <= 1e-4
```

串扰补偿后的 AD：

```text
abs(cppCorrectedAd - pythonCorrectedAd) <= 1e-4
```

无效通道：

```text
stageAd - closedAd <= 0 或 < 10 时，角度必须输出 0
```

## 注意事项

- SDK 只输出算法结果，不要引入 UI 和动画逻辑。
- 串扰补偿只修正 AD，不直接修正角度。
- 通道编号保持 `CH1~CH19`，对外文档使用 1-based 编号。
- C++ 源码文件保存为 UTF-8 with BOM。
- Python 参考代码不使用命令行 args，C++ 测试入口也尽量保持固定变量配置，避免额外复杂度。
