# API 参考手册

> Copyright (c) 2026 Matrix 墨现科技. All rights reserved.

## Handle 生命周期

```c
MatrixHandHandle handle = matrix_hand_create();
// 使用 handle ...
matrix_hand_destroy(handle);
```

- `matrix_hand_create()` 失败时返回 `NULL`。
- `matrix_hand_destroy(NULL)` 安全，直接返回。
- Handle 不可拷贝、不可共享，每个实例独立使用。

## 函数列表

### matrix_hand_create

```c
MatrixHandHandle matrix_hand_create(void);
```

创建 SDK 实例。成功返回句柄，失败返回 `NULL`。

---

### matrix_hand_destroy

```c
void matrix_hand_destroy(MatrixHandHandle handle);
```

销毁实例。允许传入 `NULL`。

---

### matrix_hand_reset

```c
int matrix_hand_reset(MatrixHandHandle handle);
```

清空全部校准状态和滤波状态，保留已设置的配置。

**返回码**：

| 情况 | 返回 |
|------|------|
| handle 为 NULL | `MATRIX_HAND_ERROR_NULL_HANDLE` |
| 成功 | `MATRIX_HAND_OK` |

---

### matrix_hand_set_config

```c
int matrix_hand_set_config(MatrixHandHandle handle, const MatrixHandRuntimeConfig* config);
```

设置运行时参数。**只能在初始化状态调用**（校准开始前或 reset 后）。

**状态约束**：

- 已开始校准 → `MATRIX_HAND_ERROR_BAD_STATE`
- 已完成部分校准 → `MATRIX_HAND_ERROR_BAD_STATE`
- 校准已就绪 → `MATRIX_HAND_ERROR_BAD_STATE`

**配置合法性**：

| 字段 | 合法范围 |
|------|----------|
| `mean_filter_window_frame_count` | > 0 |
| `thumb_gate_filter_window_size` | > 0 |
| `thumb_inward_gate_channel` | 18 或 19 |
| `thumb_gate_deadband_ratio` | [0, 1] |
| `spread_deadband_ratio` | [0, 1] |
| `crosstalk_max_abs_intercept` | ≥ 0 |

配置失败不修改已有校准状态。

---

### matrix_hand_begin_calibration

```c
int matrix_hand_begin_calibration(MatrixHandHandle handle, MatrixHandCalibrationStage stage);
```

开始某阶段校准。阶段必须严格按顺序：

1. `MATRIX_HAND_CALIBRATION_CLOSED`
2. `MATRIX_HAND_CALIBRATION_FIST`
3. `MATRIX_HAND_CALIBRATION_SPREAD`
4. `MATRIX_HAND_CALIBRATION_CROSSTALK`（可选）

跳阶段调用返回 `MATRIX_HAND_ERROR_CALIBRATION_BAD_STAGE`。

---

### matrix_hand_push_calibration_frame

```c
int matrix_hand_push_calibration_frame(MatrixHandHandle handle, const int16_t ad[MATRIX_HAND_CHANNEL_COUNT]);
```

推入一帧 19 路 AD 采样数据。仅在 `begin_calibration` 和 `finish_calibration` 之间有效。

**参数**：

- `ad`：19 路 int16_t 原始 AD 值，不可为 NULL。

---

### matrix_hand_finish_calibration

```c
int matrix_hand_finish_calibration(MatrixHandHandle handle, MatrixHandCalibrationResult* result);
```

结束当前阶段校准，填充校准质量结果。

**串扰阶段特殊处理**：

- 所有帧完全相同导致矩阵奇异时，返回 `MATRIX_HAND_ERROR_XTALK_FIT_FAILED`。
- 串扰拟合失败不会启用补偿。

---

### matrix_hand_is_ready

```c
int matrix_hand_is_ready(MatrixHandHandle handle, int32_t* ready);
```

查询三步校准是否完成。Closed/Fist/Spread 完成后 `*ready = 1`。

---

### matrix_hand_process_frame

```c
int matrix_hand_process_frame(
    MatrixHandHandle handle,
    const int16_t ad[MATRIX_HAND_CHANNEL_COUNT],
    MatrixHandAngleOutput* output);
```

处理一帧 AD 数据并输出关节角度。未 ready 时返回 `MATRIX_HAND_ERROR_NOT_READY`，输出结构体清零。

---

### matrix_hand_get_current_ad

```c
int matrix_hand_get_current_ad(MatrixHandHandle handle, int32_t filtered, double ad[MATRIX_HAND_CHANNEL_COUNT]);
```

获取最近一帧 AD 值。`filtered=0` 返回原始值，`filtered≠0` 返回均值滤波后的值。不受校准状态限制。

---

### matrix_hand_status_text

```c
const char* matrix_hand_status_text(int status);
```

将错误码转为可读文本。未知错误码返回 `"UNKNOWN_STATUS"`。不分配内存。

## 错误码表

| 错误码 | 值 | 含义 |
|--------|-----|------|
| `MATRIX_HAND_OK` | 0 | 成功 |
| `MATRIX_HAND_ERROR_NULL_HANDLE` | 1 | 句柄为空 |
| `MATRIX_HAND_ERROR_NULL_POINTER` | 2 | 指针参数为空 |
| `MATRIX_HAND_ERROR_INVALID_CONFIG` | 3 | 配置参数非法 |
| `MATRIX_HAND_ERROR_BAD_STATE` | 4 | 当前状态不允许此操作 |
| `MATRIX_HAND_ERROR_NOT_READY` | 5 | 校准未完成 |
| `MATRIX_HAND_ERROR_CALIBRATION_NOT_ACTIVE` | 6 | 采样未激活 |
| `MATRIX_HAND_ERROR_CALIBRATION_EMPTY` | 7 | 采样帧数为空 |
| `MATRIX_HAND_ERROR_CALIBRATION_BAD_STAGE` | 8 | 校准阶段顺序错误 |
| `MATRIX_HAND_ERROR_XTALK_FIT_FAILED` | 9 | 串扰拟合失败 |

## 输出角字段说明

| 字段 | 长度 | 内容 |
|------|------|------|
| `little_finger` | 4 | 小指 [MCP弯曲, PIP弯曲, DIP弯曲, pinky-ring展开] |
| `ring_finger` | 4 | 无名指 [MCP弯曲, PIP弯曲, DIP弯曲, ring-middle展开] |
| `middle_finger` | 3 | 中指 [MCP弯曲, PIP弯曲, DIP弯曲] |
| `index_finger` | 4 | 食指 [MCP弯曲, PIP弯曲, DIP弯曲, index-middle展开] |
| `thumb` | 3 | 拇指 [MCP弯曲, IP弯曲, 开合角] |

- 弯曲角单位为度，范围 [0, 上限]。
- 四指展开角单位为度，范围 [0, 25]。
- 拇指开合角单位为度，正=外展，负=内收，范围 [-45, 45]。
- 无效通道输出 0 度。
