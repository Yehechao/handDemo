# 校准说明

> Copyright (c) 2026 Matrix 墨现科技. All rights reserved.

## 四阶段校准

校准需按顺序完成：

```
Closed（手指伸直）→ Fist（握拳）→ Spread（手展开）→ Crosstalk（串扰补偿）
```

四步全部完成后才可输出角度。Crosstalk 完成后启用串扰补偿。

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
- 该阶段为**必须**。未完成 Crosstalk 校准不允许实时输出角度。
- 拟合失败（如数据无变化导致矩阵奇异）时 `finishCalibration()` 返回 `false`。
- 拟合成功后启用实时串扰补偿，修正 AD 值。

## 调用顺序

```cpp
HandAngleAlgorithm algorithm;
RuntimeConfig config;
algorithm.setRuntimeConfig(config);

// Step 1: Closed
algorithm.beginCalibration(CalibrationStage::Closed);
for (...) { algorithm.pushCalibrationFrame(ad); }
algorithm.finishCalibration();

// Step 2: Fist
algorithm.beginCalibration(CalibrationStage::Fist);
for (...) { algorithm.pushCalibrationFrame(ad); }
algorithm.finishCalibration();

// Step 3: Spread
algorithm.beginCalibration(CalibrationStage::Spread);
for (...) { algorithm.pushCalibrationFrame(ad); }
algorithm.finishCalibration();

// Step 4: Crosstalk（必须）
algorithm.beginCalibration(CalibrationStage::Crosstalk);
for (...) { algorithm.pushCalibrationFrame(ad); }
algorithm.finishCalibration();

// 此时 isReady() 返回 true

// 实时输出
HandAngleOutput output;
if (algorithm.isReady()) {
    algorithm.processFrame(ad, output);
}
```

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
- 异常通道可通过 `getXtalkUnstableChList()` 获取，通道编号为 1-based。
- 异常通道**不影响补偿计算**，仅作提示用途。
