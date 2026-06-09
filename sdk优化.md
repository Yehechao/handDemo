# SDK 剩余优化项

> 本文件作为唯一沟通文档使用。只记录当前仍需修复的问题、修复方案和审查验收标准。已完成的 SDK 设计、接口清单、文档规划不再重复。

## 1. P0：修复 `matrix_hand_sdk.cpp` 默认编译失败

### 问题

`matrix_hand_sdk.h` 在 Windows 下默认把 `MATRIX_HAND_API` 定义为 `__declspec(dllimport)`。  
`matrix_hand_sdk.cpp` 直接包含该头文件并定义 SDK 函数，未定义 `MATRIX_HAND_SDK_EXPORTS` 时会触发 MSVC 编译错误：

```text
C2491: 不允许 dllimport 函数 的定义
```

### 修复方案

在 `matrix_hand_sdk.cpp` 的 `#include "matrix_hand_sdk.h"` 之前定义 `MATRIX_HAND_SDK_EXPORTS`：

```cpp
#ifndef MATRIX_HAND_SDK_EXPORTS
#define MATRIX_HAND_SDK_EXPORTS
#endif
#include "matrix_hand_sdk.h"
```

### 验收

执行：

```powershell
cmd /c """D:\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 >nul && cl /nologo /std:c++17 /EHsc /utf-8 /c matrix_hand_sdk.cpp /FoNUL"
```

通过标准：

- 命令退出码为 0。
- 不需要额外传入 `/DMATRIX_HAND_SDK_EXPORTS`。
- 不出现 `C2491`。

## 2. P1：非法校准阶段值不能被当成 Closed

### 问题

`toInternalStage()` 的 default 分支返回 `CalibrationStage::Closed`。  
C API 调用方如果传入非法枚举值，例如 `(MatrixHandCalibrationStage)99`，初始状态下会被当成 Closed 通过校验。

### 修复方案

在 `matrix_hand_begin_calibration()` 中先检查 SDK 阶段值是否合法：

```cpp
bool isValidSdkStage(MatrixHandCalibrationStage stage) {
    return stage == MATRIX_HAND_CALIBRATION_CLOSED ||
           stage == MATRIX_HAND_CALIBRATION_FIST ||
           stage == MATRIX_HAND_CALIBRATION_SPREAD ||
           stage == MATRIX_HAND_CALIBRATION_CROSSTALK;
}
```

非法值直接返回：

```cpp
MATRIX_HAND_ERROR_CALIBRATION_BAD_STAGE
```

### 验收

流程：

```text
create
begin stage=99
begin Closed
```

通过标准：

- `begin stage=99` 返回 `MATRIX_HAND_ERROR_CALIBRATION_BAD_STAGE`。
- 不进入采样状态。
- 后续 `begin Closed` 返回 `MATRIX_HAND_OK`。

## 3. P2：空采样 finish 后必须收尾状态

### 问题

`matrix_hand_finish_calibration()` 在 `activeFrameCount == 0` 时直接返回 `MATRIX_HAND_ERROR_CALIBRATION_EMPTY`，但没有清理 wrapper 和算法层采样状态。

结果是调用方再次 `begin_calibration()` 会因为 `calibrationActive == true` 得到 `MATRIX_HAND_ERROR_BAD_STATE`。

### 修复方案

空采样 finish 视为当前阶段失败并结束当前采样。返回前必须：

- 调用 `ctx->algorithm.finishCalibration()`，让算法层重置采样状态。
- 设置 `ctx->calibrationActive = false`。
- 清空 `ctx->activeSum`。
- 设置 `ctx->activeFrameCount = 0`。
- 不推进 `completedStepCount`。

### 验收

流程：

```text
create
begin Closed
finish Closed，不 push 帧
begin Closed
```

通过标准：

- 第一次 `finish` 返回 `MATRIX_HAND_ERROR_CALIBRATION_EMPTY`。
- 第二次 `begin Closed` 返回 `MATRIX_HAND_OK`。
- 阶段计数不推进。

## 4. P2：wrapper 采样统计必须和算法层上限一致

### 问题

算法层 `pushCalibrationFrame()` 超过 `kMaxSamplingFrameCount` 后返回 true，但不再累计内部采样。  
wrapper 当前继续累计 `activeSum` 和 `activeFrameCount`，会导致 `MatrixHandCalibrationResult` 的均值和算法内部实际校准均值不一致。

### 修复方案

wrapper 只在以下条件成立时累计：

```cpp
ctx->activeFrameCount < handdemo::kMaxSamplingFrameCount
```

超过上限后：

- 可以继续返回 `MATRIX_HAND_OK`。
- 不再累计 `activeSum`。
- 不再递增 `activeFrameCount`。

### 验收

流程：

```text
begin Closed 后 push 5001 帧
finish Closed
```

通过标准：

- `result.frame_count == 5000`。
- wrapper 均值只使用前 5000 帧。

## 5. 重新审查标准

修完以上问题后，直接按本节重新审查，不再维护单独的 `验收.md` 或 `审查.md`。

### 5.1 编译验收

必须执行：

```powershell
cmd /c """D:\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 >nul && cl /nologo /std:c++17 /EHsc /utf-8 /c matrix_hand_sdk.cpp /FoNUL"
```

通过标准：

- SDK wrapper 默认编译。
- 命令退出码为 0。
- 不需要额外传入 `/DMATRIX_HAND_SDK_EXPORTS`。
- 不出现 `C2491`。

必须执行：

```powershell
cmd /c """D:\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 >nul && cl /nologo /std:c++17 /EHsc /utf-8 /c hand_algorithm.cpp /FoNUL"
```

通过标准：

- 算法文件编译。
- 命令退出码为 0。

### 5.2 接口边界验收

公开头文件检查：

```powershell
rg -n "hand_algorithm|windows.h|std::|vector|array|deque|string" matrix_hand_sdk.h
```

通过标准：

- 无输出。

串口隔离检查：

```powershell
rg -n "SerialFrameReceiver|serial_port_io|ConnectionMode|windows.h|SetupDi|CreateFileW" matrix_hand_sdk.h matrix_hand_sdk.cpp
```

通过标准：

- 无输出。
- 不再使用 `HANDLE` 作为检索词，因为 `MATRIX_HAND_ERROR_NULL_HANDLE` 会造成误报。

OpenCV 隔离检查：

```powershell
rg -ni "opencv" matrix_hand_sdk.h matrix_hand_sdk.cpp hand_algorithm.h hand_algorithm.cpp README.md docs
```

通过标准：

- `matrix_hand_sdk.h`、`matrix_hand_sdk.cpp`、`hand_algorithm.h`、`hand_algorithm.cpp` 不命中。
- README 不把 OpenCV 描述为算法 SDK 依赖。

### 5.3 状态机场景验收

必须验证正常链路：

```text
create
set_config
begin Closed
push Closed frame
finish Closed
begin Fist
push Fist frame
finish Fist
begin Spread
push Spread frame
finish Spread
is_ready
process_frame
destroy
```

通过标准：

- 每一步返回 `MATRIX_HAND_OK`。
- Spread 完成后 `ready == 1`。
- `process_frame` 返回 `MATRIX_HAND_OK`。

必须验证非法阶段值：

```text
create
begin stage=99
begin Closed
```

通过标准：

- `begin stage=99` 返回 `MATRIX_HAND_ERROR_CALIBRATION_BAD_STAGE`。
- 后续 `begin Closed` 返回 `MATRIX_HAND_OK`。
- 非法阶段值不会进入采样状态。

必须验证空采样 finish：

```text
create
begin Closed
finish Closed，不 push 任何帧
begin Closed
```

通过标准：

- 第一次 `finish` 返回 `MATRIX_HAND_ERROR_CALIBRATION_EMPTY`。
- 第二次 `begin Closed` 返回 `MATRIX_HAND_OK`。
- 阶段计数不推进。

必须验证未 ready 输出：

```text
create 后直接 process_frame
只完成 Closed 后 process_frame
只完成 Closed/Fist 后 process_frame
```

通过标准：

- 返回 `MATRIX_HAND_ERROR_NOT_READY`。
- 输出结构体全部清零。

### 5.4 校准结果验收

无效通道场景：

```text
Closed: 所有通道 = 1000
Fist: 某个弯曲通道仍为 1000，其余相关通道 > 1010
Spread: 某个展开通道仍为 1000，其余展开通道 > 1010
```

通过标准：

- Fist 无效通道出现在 `invalid_channels`。
- Spread 无效通道出现在 `invalid_channels`。
- 通道编号为 1-based。

最大采样帧数场景：

```text
begin Closed 后 push 5001 帧
finish Closed
```

通过标准：

- `result.frame_count == 5000`。
- wrapper 均值只使用前 5000 帧。

串扰失败场景：

```text
完成 Closed/Fist/Spread 后
begin Crosstalk
push 多帧完全相同数据
finish Crosstalk
```

通过标准：

- `finish` 返回 `MATRIX_HAND_ERROR_XTALK_FIT_FAILED`。
- `getXtalkValidTargetChannelCount()` 返回 0。
- 串扰补偿不启用。

## 6. 最终结论模板

后续审查只在本文件追加结论即可：

```text
## 审查结论

结论：通过 / 不通过

P0：
1. ...

P1：
1. ...

P2：
1. ...

已执行命令：
1. ...

剩余风险：
1. ...
```
