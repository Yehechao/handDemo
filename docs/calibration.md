# 校准说明

> Copyright (c) 2026 Matrix 墨现科技. All rights reserved.

## 四阶段校准

校准需按顺序完成：

```
Closed（手指伸直）→ Fist（握拳）→ Spread（手展开）→ Crosstalk（串扰补偿，可选）
```

前三步完成后即可输出角度，第四步为可选增强。

### 阶段 1：Closed（手指伸直）

手掌平放、五指自然伸直，传感器读取闭合基线。

- 采样时长建议 2000 ms。
- 该阶段不做通道有效性判定，所有 19 路通道视为有效。

### 阶段 2：Fist（握拳）

手掌握拳，传感器读取最大弯曲值。

- 采样时长建议 2000 ms。
- 该阶段判定弯曲相关通道 + CH16 + 门控通道的有效性。
- 无效通道（相对闭合基线变化 < 10）将在输出时角度置零。

### 阶段 3：Spread（手展开）

五指张开，传感器读取最大展开值。

- 采样时长建议 2000 ms。
- 该阶段判定展开通道（CH4/8/12/16）的有效性。

### 阶段 4：Crosstalk（串扰补偿）

大拇指进行弯曲动作，系统采集多帧数据拟合串扰系数。

- 采样时长建议 4000 ms。
- 该阶段为**可选**。跳过不影响前三步角度输出。
- 拟合失败（如数据无变化导致矩阵奇异）时返回 `MATRIX_HAND_ERROR_XTALK_FIT_FAILED`。
- 拟合成功后启用实时串扰补偿，修正 AD 值。

## 调用顺序

```c
MatrixHandHandle handle = matrix_hand_create();

// 可选：设置配置参数
MatrixHandRuntimeConfig cfg = {10, 10, 18, 0.025, 0.02, 1, 30.0};
matrix_hand_set_config(handle, &cfg);

// Step 1: Closed
matrix_hand_begin_calibration(handle, MATRIX_HAND_CALIBRATION_CLOSED);
for (...) { matrix_hand_push_calibration_frame(handle, ad); }
matrix_hand_finish_calibration(handle, &result);

// Step 2: Fist
matrix_hand_begin_calibration(handle, MATRIX_HAND_CALIBRATION_FIST);
for (...) { matrix_hand_push_calibration_frame(handle, ad); }
matrix_hand_finish_calibration(handle, &result);

// Step 3: Spread
matrix_hand_begin_calibration(handle, MATRIX_HAND_CALIBRATION_SPREAD);
for (...) { matrix_hand_push_calibration_frame(handle, ad); }
matrix_hand_finish_calibration(handle, &result);

// 此时 is_ready 返回 1

// Step 4: Crosstalk（可选）
matrix_hand_begin_calibration(handle, MATRIX_HAND_CALIBRATION_CROSSTALK);
for (...) { matrix_hand_push_calibration_frame(handle, ad); }
matrix_hand_finish_calibration(handle, &result);

// 实时输出
int32_t ready = 0;
matrix_hand_is_ready(handle, &ready);
if (ready) {
    MatrixHandAngleOutput output;
    matrix_hand_process_frame(handle, ad, &output);
}

matrix_hand_destroy(handle);
```

## MatrixHandCalibrationResult 字段

| 字段 | 类型 | 说明 |
|------|------|------|
| `stage` | int32_t | 当前阶段枚举值 |
| `frame_count` | int32_t | 本阶段采集帧数 |
| `valid_channel_count` | int32_t | 有效通道数 |
| `invalid_channel_count` | int32_t | 无效通道数 |
| `invalid_channels` | int32_t[19] | 无效通道编号列表（1-based，CH1~CH19） |
| `xtalk_unstable_count` | int32_t | 串扰截距异常通道数（仅 Crosstalk） |
| `xtalk_unstable_channels` | int32_t[19] | 串扰截距异常通道列表（仅 Crosstalk） |

## 无效通道判定

判定公式：

```
delta = stageAvg[ch] - closedAvg[ch]
无效条件：delta ≤ 0 或 delta < 10.0
```

- Closed 阶段为基线，不做无效判定，`invalid_channel_count = 0`。
- Fist 阶段检查弯曲通道 + CH16 + 门控通道。
- Spread 阶段检查展开通道（CH4/8/12/16）。
- 无效通道在实时输出时弯曲角/展开角输出 0。

## 串扰异常通道

- 串扰拟合的截距项 `d` 的绝对值超过 `crosstalk_max_abs_intercept`（默认 30）时，标记为异常通道。
- 异常通道信息写入 `xtalk_unstable_channels`，客户可据此判断校准质量。
- 异常通道**不影响补偿计算**，仅作提示用途。
