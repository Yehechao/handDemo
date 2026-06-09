# SDK 优化应对方案

> 本文给后续执行代码修改的 agent 使用。目标是把当前算法 demo 收敛为可交付的算法 SDK 接口层。本轮明确不修改 `CMakeLists.txt`。

## 1. 本轮边界

必须做：

- 新增 SDK 对外头文件和 wrapper 实现文件。
- 对外 API 使用 C 风格接口，不把 C++ 类和 STL 类型暴露给客户。
- 明确错误码、生命周期、校准阶段顺序、校准质量结果。
- 保留现有算法公式，不做算法调参。
- 保留 `main.cpp`、`serial_port_io.*` 作为 demo/sample 代码，不纳入算法 SDK 核心。

不做：

- 不修改 `CMakeLists.txt`。
- 不移动现有源码目录。
- 不重写串口协议。
- 不增加 OpenCV 相关能力。
- 不做旧接口兼容包装。`HandAngleAlgorithm` 可以继续作为内部实现类存在，但客户 SDK 入口只认 `matrix_hand_sdk.h`。

## 2. 新增文件

在当前根目录新增两个文件：

```text
matrix_hand_sdk.h
matrix_hand_sdk.cpp
```

原因：

- 当前不调整目录和构建脚本，新增根目录文件是最短路径。
- 后续需要整理目录时，再把这两个文件移动到 `include/` 和 `src/`。

## 3. 对外 API 设计

### 3.1 头文件原则

`matrix_hand_sdk.h` 必须满足：

- 不包含 `hand_algorithm.h`。
- 不包含 `windows.h`。
- 不包含 `std::vector`、`std::array`、`std::deque`、`std::string`。
- 只包含 `<stdint.h>`。
- C++ 编译时使用 `extern "C"`。
- 所有结构体只使用 POD 字段。

### 3.2 常量

```cpp
#define MATRIX_HAND_CHANNEL_COUNT 19
#define MATRIX_HAND_MAX_INVALID_CHANNEL_COUNT 19
```

### 3.3 导出宏

```cpp
#if defined(_WIN32) && defined(MATRIX_HAND_SDK_EXPORTS)
#define MATRIX_HAND_API __declspec(dllexport)
#elif defined(_WIN32)
#define MATRIX_HAND_API __declspec(dllimport)
#else
#define MATRIX_HAND_API
#endif
```

说明：本轮只加宏，不修改构建脚本。

### 3.4 句柄

```cpp
typedef void* MatrixHandHandle;
```

对外不暴露 C++ 类对象。

### 3.5 错误码

```cpp
typedef enum MatrixHandStatus {
    MATRIX_HAND_OK = 0,
    MATRIX_HAND_ERROR_NULL_HANDLE = 1,
    MATRIX_HAND_ERROR_NULL_POINTER = 2,
    MATRIX_HAND_ERROR_INVALID_CONFIG = 3,
    MATRIX_HAND_ERROR_BAD_STATE = 4,
    MATRIX_HAND_ERROR_NOT_READY = 5,
    MATRIX_HAND_ERROR_CALIBRATION_NOT_ACTIVE = 6,
    MATRIX_HAND_ERROR_CALIBRATION_EMPTY = 7,
    MATRIX_HAND_ERROR_CALIBRATION_BAD_STAGE = 8,
    MATRIX_HAND_ERROR_XTALK_FIT_FAILED = 9
} MatrixHandStatus;
```

所有 SDK 函数返回 `int`，实际值取 `MatrixHandStatus`。

### 3.6 校准阶段

```cpp
typedef enum MatrixHandCalibrationStage {
    MATRIX_HAND_CALIBRATION_CLOSED = 0,
    MATRIX_HAND_CALIBRATION_FIST = 1,
    MATRIX_HAND_CALIBRATION_SPREAD = 2,
    MATRIX_HAND_CALIBRATION_CROSSTALK = 3
} MatrixHandCalibrationStage;
```

SDK 层必须强制顺序：

```text
Closed -> Fist -> Spread -> Crosstalk
```

Crosstalk 可选，但只能在 Spread 成功后开始。

### 3.7 运行时配置

```cpp
typedef struct MatrixHandRuntimeConfig {
    uint32_t mean_filter_window_frame_count;
    uint32_t thumb_gate_filter_window_size;
    int32_t thumb_inward_gate_channel;
    double thumb_gate_deadband_ratio;
    double spread_deadband_ratio;
    int32_t crosstalk_fit_intercept;
    double crosstalk_max_abs_intercept;
} MatrixHandRuntimeConfig;
```

规则：

- `matrix_hand_set_config()` 只能在初始状态调用。
- 已开始校准、已完成校准或正在采样时调用，返回 `MATRIX_HAND_ERROR_BAD_STATE`。
- 配置非法时返回 `MATRIX_HAND_ERROR_INVALID_CONFIG`。
- 配置失败不能修改内部状态。

### 3.8 输出角结构体

```cpp
typedef struct MatrixHandAngleOutput {
    float little_finger[4];
    float ring_finger[4];
    float middle_finger[3];
    float index_finger[4];
    float thumb[3];
} MatrixHandAngleOutput;
```

字段顺序和当前 `HandAngleOutput` 保持一致。

### 3.9 校准结果结构体

```cpp
typedef struct MatrixHandCalibrationResult {
    int32_t stage;
    int32_t frame_count;
    int32_t valid_channel_count;
    int32_t invalid_channel_count;
    int32_t invalid_channels[MATRIX_HAND_MAX_INVALID_CHANNEL_COUNT];
    int32_t xtalk_unstable_count;
    int32_t xtalk_unstable_channels[MATRIX_HAND_MAX_INVALID_CHANNEL_COUNT];
} MatrixHandCalibrationResult;
```

填充规则：

- 所有数组先填 0。
- Closed 阶段不做通道跨度判断，`valid_channel_count = 19`，`invalid_channel_count = 0`。
- Fist 阶段判断弯曲相关通道、CH16、拇指门控通道。
- Spread 阶段判断 `kSpreadChannelIndexList`。
- Crosstalk 阶段填充 `xtalk_unstable_channels`。
- 通道编号使用 1-based，即 CH1 到 CH19。

## 4. SDK 函数清单

`matrix_hand_sdk.h` 对外只暴露以下函数：

```cpp
MATRIX_HAND_API MatrixHandHandle matrix_hand_create(void);
MATRIX_HAND_API void matrix_hand_destroy(MatrixHandHandle handle);

MATRIX_HAND_API int matrix_hand_reset(MatrixHandHandle handle);
MATRIX_HAND_API int matrix_hand_set_config(MatrixHandHandle handle, const MatrixHandRuntimeConfig* config);

MATRIX_HAND_API int matrix_hand_begin_calibration(MatrixHandHandle handle, MatrixHandCalibrationStage stage);
MATRIX_HAND_API int matrix_hand_push_calibration_frame(MatrixHandHandle handle, const int16_t ad[MATRIX_HAND_CHANNEL_COUNT]);
MATRIX_HAND_API int matrix_hand_finish_calibration(MatrixHandHandle handle, MatrixHandCalibrationResult* result);

MATRIX_HAND_API int matrix_hand_is_ready(MatrixHandHandle handle, int32_t* ready);
MATRIX_HAND_API int matrix_hand_process_frame(
    MatrixHandHandle handle,
    const int16_t ad[MATRIX_HAND_CHANNEL_COUNT],
    MatrixHandAngleOutput* output);

MATRIX_HAND_API int matrix_hand_get_current_ad(MatrixHandHandle handle, int32_t filtered, double ad[MATRIX_HAND_CHANNEL_COUNT]);
MATRIX_HAND_API const char* matrix_hand_status_text(int status);
```

## 5. wrapper 实现方案

### 5.1 内部上下文

在 `matrix_hand_sdk.cpp` 中定义内部结构体：

```cpp
struct MatrixHandContext {
    handdemo::HandAngleAlgorithm algorithm;
    handdemo::RuntimeConfig config;
    bool hasConfig = false;

    bool calibrationActive = false;
    handdemo::CalibrationStage activeStage = handdemo::CalibrationStage::Closed;
    int completedStepCount = 0;

    std::array<double, handdemo::kChannelCount> activeSum{};
    std::size_t activeFrameCount = 0;

    std::array<double, handdemo::kChannelCount> closedAvg{};
    std::array<double, handdemo::kChannelCount> fistAvg{};
    std::array<double, handdemo::kChannelCount> spreadAvg{};
    bool hasClosed = false;
    bool hasFist = false;
    bool hasSpread = false;
};
```

说明：

- wrapper 自己记录校准均值，用于填充校准质量结果。
- `HandAngleAlgorithm` 仍负责真实算法计算。
- wrapper 不访问 `HandAngleAlgorithm` 私有字段。

### 5.2 create/destroy

实现规则：

- `matrix_hand_create()` 使用 `new MatrixHandContext()`。
- 创建失败时返回空指针。
- `matrix_hand_destroy()` 允许传入空指针，直接返回。

### 5.3 reset

实现规则：

- 空句柄返回 `MATRIX_HAND_ERROR_NULL_HANDLE`。
- 调用 `algorithm.reset()`。
- 清空 wrapper 内的采样状态、阶段计数、均值缓存。
- 保留当前配置值；如配置已设置，则 reset 后继续使用该配置。

### 5.4 set_config

实现规则：

- 空句柄返回 `NULL_HANDLE`。
- 空配置返回 `NULL_POINTER`。
- 若 `calibrationActive == true` 或 `completedStepCount > 0` 或 `algorithm.isReady() == true`，返回 `BAD_STATE`。
- 先把 `MatrixHandRuntimeConfig` 转成 `handdemo::RuntimeConfig`。
- 调用 `algorithm.setRuntimeConfig()`。
- 返回 false 时转为 `INVALID_CONFIG`。
- 成功后保存配置。

### 5.5 begin_calibration

实现规则：

- 空句柄返回 `NULL_HANDLE`。
- 已有校准正在进行时返回 `BAD_STATE`。
- 阶段必须匹配当前进度：
  - `completedStepCount == 0` 只能开始 Closed。
  - `completedStepCount == 1` 只能开始 Fist。
  - `completedStepCount == 2` 只能开始 Spread。
  - `completedStepCount == 3` 只能开始 Crosstalk。
  - `completedStepCount > 3` 返回 `CALIBRATION_BAD_STAGE`。
- 通过后调用 `algorithm.beginCalibration()`。
- 清空 `activeSum`，`activeFrameCount = 0`，记录 active stage。

### 5.6 push_calibration_frame

实现规则：

- 空句柄返回 `NULL_HANDLE`。
- 空 AD 返回 `NULL_POINTER`。
- 未处于校准采样时返回 `CALIBRATION_NOT_ACTIVE`。
- 先调用 `algorithm.pushCalibrationFrame(ad)`。
- 如果算法返回 false，返回 `CALIBRATION_NOT_ACTIVE`。
- 同步累计 `activeSum` 和 `activeFrameCount`。

### 5.7 finish_calibration

实现规则：

- 空句柄返回 `NULL_HANDLE`。
- 空结果返回 `NULL_POINTER`。
- 未采样返回 `CALIBRATION_NOT_ACTIVE`。
- `activeFrameCount == 0` 返回 `CALIBRATION_EMPTY`。
- 先把 result 全部清零。
- 调用 `algorithm.finishCalibration()`。
- 算法返回 false 时：
  - Crosstalk 阶段返回 `XTALK_FIT_FAILED`。
  - 其他阶段返回 `CALIBRATION_EMPTY`。
- 根据 `activeSum / activeFrameCount` 得到本阶段均值。
- 更新 `closedAvg/fistAvg/spreadAvg` 和 `hasClosed/hasFist/hasSpread`。
- 填充 `MatrixHandCalibrationResult`。
- `completedStepCount++`。
- 清空 active 状态。

### 5.8 is_ready

实现规则：

- 空句柄返回 `NULL_HANDLE`。
- 空输出指针返回 `NULL_POINTER`。
- `*ready = algorithm.isReady() ? 1 : 0`。

### 5.9 process_frame

实现规则：

- 空句柄返回 `NULL_HANDLE`。
- 空 AD 或输出指针返回 `NULL_POINTER`。
- 未 ready 时：
  - 输出结构体清零。
  - 返回 `NOT_READY`。
- ready 后调用 `algorithm.processFrame()`。
- 算法返回 false 时返回 `NOT_READY`。
- 成功返回 `OK`。

### 5.10 get_current_ad

实现规则：

- 空句柄返回 `NULL_HANDLE`。
- 空输出数组返回 `NULL_POINTER`。
- 调用 `algorithm.getCurrentAd(filtered != 0)`。
- 拷贝 19 个 double 到客户数组。

### 5.11 status_text

实现规则：

- 使用 `switch(status)` 返回静态字符串。
- 不分配内存。
- 未知错误码返回 `"UNKNOWN_STATUS"`。

## 6. 需要修改 hand_algorithm 的点

### 6.1 收紧校准阶段顺序

当前问题：

- `setStageCalib()` 在没有 Closed 时，非 Closed 阶段也可能写入 Fist。

修改方案：

- 在 `finishCalibration()` 中增加阶段前置检查：
  - Fist 必须已有 Closed。
  - Spread 必须已有 Closed 和 Fist。
  - Crosstalk 必须 `isReady()`。
- 不满足时，`resetSamplingState()` 后返回 false。
- 删除 `setStageCalib()` 中“没有 Closed 时写入 Fist”的逻辑。

### 6.2 串扰拟合失败不能启用补偿

当前问题：

- Crosstalk 阶段调用 `fitXtalkCoefs()` 后直接 `hasXtalk_ = true`。
- 如果矩阵奇异导致所有目标通道拟合失败，也会启用串扰补偿状态。

修改方案：

- 把 `fitXtalkCoefs()` 返回值改为有效拟合通道数。
- `fitXtalkCoefs()` 内每个 target 通道 `coef.isValid == true` 时计数加 1。
- `finishCalibration()` 中：
  - `validCount == 0` 时 `hasXtalk_ = false`，返回 false。
  - `validCount > 0` 时 `hasXtalk_ = true`，保存 `xtalkBase_`。
- 新增 public 方法：

```cpp
std::size_t getXtalkValidTargetChannelCount() const;
```

用于 SDK result 填充和测试验证。

### 6.3 不改变算法公式

以下内容本轮不改：

- ratio 计算公式。
- 均值滤波窗口逻辑。
- 死区稳定逻辑。
- 展开角收束逻辑。
- 拇指内收/外展公式。
- 串扰最小二乘公式。

## 7. 校准质量计算方案

### 7.1 通道有效性判定

判定公式：

```text
delta = stageAvg[ch] - closedAvg[ch]
无效条件：delta <= 0 或 delta < kInvalidChannelSpanThresholdValue
```

### 7.2 Fist 阶段检查通道

检查集合：

- `kFlexChannelIndexList`
- CH16
- `runtimeConfig.thumbInwardGateChannel`

去重后填充结果。

### 7.3 Spread 阶段检查通道

检查集合：

- `kSpreadChannelIndexList`

### 7.4 Closed 阶段

Closed 是基线阶段，不做跨度判定。

### 7.5 Crosstalk 阶段

填充：

- `xtalk_unstable_count`
- `xtalk_unstable_channels`

来源：

- `algorithm.getXtalkUnstableChList()`

## 8. 文档调整方案

### 8.1 README

把 README 的 SDK 公开 API 改为 `matrix_hand_sdk.h` 中的 C API。

删除算法 SDK 对 OpenCV 的依赖描述。OpenCV 不属于当前算法核心依赖。

### 8.2 新增 docs/api.md

内容包含：

- handle 生命周期。
- 每个函数的参数、返回码、状态要求。
- 错误码表。
- 输出角字段说明。

### 8.3 新增 docs/calibration.md

内容包含：

- Closed/Fist/Spread/Crosstalk 四阶段姿态。
- 每阶段调用顺序。
- 校准结果字段含义。
- 无效通道和串扰异常通道解释。

## 9. 执行任务清单

1. 新增 `matrix_hand_sdk.h`。
2. 新增 `matrix_hand_sdk.cpp`。
3. 修改 `hand_algorithm.h`，增加 `getXtalkValidTargetChannelCount()` 声明，并调整 `fitXtalkCoefs()` 返回值声明。
4. 修改 `hand_algorithm.cpp`，收紧校准阶段顺序，修正串扰拟合失败状态。
5. 修改 README，公开 API 改为 SDK C API，删除 OpenCV 作为算法 SDK 依赖的描述。
6. 新增 `docs/api.md`。
7. 新增 `docs/calibration.md`。
8. 按 `验收.md` 完成检查。

## 10. 实施思考

这次优化的核心不是重写算法，而是把算法从 demo 中“封装出来”。最重要的原则是：客户只能看到稳定、简单、无 STL/Windows 污染的 SDK 入口；算法内部继续保持现有实现，避免引入新的业务偏移。

阶段顺序和错误码必须收紧，因为这直接决定现场集成是否可定位问题。校准结果必须结构化，因为“角度为 0”既可能是动作真实结果，也可能是通道无效，没有结果结构体就无法区分。
