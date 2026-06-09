// Copyright (c) 2026 Matrix 墨现科技. All rights reserved.

#include "matrix_hand_sdk.h"

#include <array>
#include <cstring>
#include <vector>

#include "hand_algorithm.h"

namespace {

using handdemo::HandAngleAlgorithm;
using handdemo::RuntimeConfig;
using handdemo::CalibrationStage;
using handdemo::kChannelCount;

struct MatrixHandContext {
    HandAngleAlgorithm algorithm;
    RuntimeConfig config;
    bool hasConfig = false;

    bool calibrationActive = false;
    CalibrationStage activeStage = CalibrationStage::Closed;
    int completedStepCount = 0;

    std::array<double, kChannelCount> activeSum{};
    std::size_t activeFrameCount = 0;

    std::array<double, kChannelCount> closedAvg{};
    std::array<double, kChannelCount> fistAvg{};
    std::array<double, kChannelCount> spreadAvg{};
    bool hasClosed = false;
    bool hasFist = false;
    bool hasSpread = false;
};

CalibrationStage toInternalStage(MatrixHandCalibrationStage stage) {
    switch (stage) {
        case MATRIX_HAND_CALIBRATION_CLOSED:    return CalibrationStage::Closed;
        case MATRIX_HAND_CALIBRATION_FIST:      return CalibrationStage::Fist;
        case MATRIX_HAND_CALIBRATION_SPREAD:    return CalibrationStage::Spread;
        case MATRIX_HAND_CALIBRATION_CROSSTALK: return CalibrationStage::Crosstalk;
        default:                                return CalibrationStage::Closed;
    }
}

MatrixHandCalibrationStage toSdkStage(CalibrationStage stage) {
    switch (stage) {
        case CalibrationStage::Closed:    return MATRIX_HAND_CALIBRATION_CLOSED;
        case CalibrationStage::Fist:      return MATRIX_HAND_CALIBRATION_FIST;
        case CalibrationStage::Spread:    return MATRIX_HAND_CALIBRATION_SPREAD;
        case CalibrationStage::Crosstalk: return MATRIX_HAND_CALIBRATION_CROSSTALK;
        default:                          return MATRIX_HAND_CALIBRATION_CLOSED;
    }
}

std::array<double, kChannelCount> computeAverage(
    const std::array<double, kChannelCount>& sum,
    std::size_t count) {
    std::array<double, kChannelCount> avg{};
    if (count == 0) {
        return avg;
    }
    for (std::size_t i = 0; i < kChannelCount; ++i) {
        avg[i] = sum[i] / static_cast<double>(count);
    }
    return avg;
}

bool isChannelInList(int channelIndex, const int* list, int count) {
    for (int i = 0; i < count; ++i) {
        if (list[i] == channelIndex) {
            return true;
        }
    }
    return false;
}

// 依据 Closed 基准判定某通道在当前阶段是否有效。
// delta = stageAvg[ch-1] - closedAvg[ch-1]；delta <= 0 或 delta < 10.0 时无效。
bool isChannelValidForResult(
    int channelIndex,
    const std::array<double, kChannelCount>& stageAvg,
    const std::array<double, kChannelCount>& closedAvg) {
    const int idx = channelIndex - 1;
    const double delta = stageAvg[idx] - closedAvg[idx];
    if (delta <= 0.0) {
        return false;
    }
    if (delta < handdemo::kInvalidChannelSpanThresholdValue) {
        return false;
    }
    return true;
}

void fillFistInvalidChannels(
    const std::array<double, kChannelCount>& fistAvg,
    const std::array<double, kChannelCount>& closedAvg,
    int thumbGateChannel,
    MatrixHandCalibrationResult* result) {
    // 集合：flex 通道 + CH16 + 门控通道（去重）
    int candidates[20];
    int candidateCount = 0;

    // flex 通道
    for (int ch : handdemo::kFlexChannelIndexList) {
        if (!isChannelInList(ch, candidates, candidateCount)) {
            candidates[candidateCount++] = ch;
        }
    }
    // CH16
    if (!isChannelInList(16, candidates, candidateCount)) {
        candidates[candidateCount++] = 16;
    }
    // 门控通道
    if (!isChannelInList(thumbGateChannel, candidates, candidateCount)) {
        candidates[candidateCount++] = thumbGateChannel;
    }

    result->valid_channel_count = 0;
    result->invalid_channel_count = 0;
    for (int i = 0; i < candidateCount; ++i) {
        if (isChannelValidForResult(candidates[i], fistAvg, closedAvg)) {
            ++result->valid_channel_count;
        } else {
            if (result->invalid_channel_count < MATRIX_HAND_MAX_INVALID_CHANNEL_COUNT) {
                result->invalid_channels[result->invalid_channel_count] = candidates[i];
                ++result->invalid_channel_count;
            }
        }
    }
}

void fillSpreadInvalidChannels(
    const std::array<double, kChannelCount>& spreadAvg,
    const std::array<double, kChannelCount>& closedAvg,
    MatrixHandCalibrationResult* result) {
    result->valid_channel_count = 0;
    result->invalid_channel_count = 0;
    for (int ch : handdemo::kSpreadChannelIndexList) {
        if (isChannelValidForResult(ch, spreadAvg, closedAvg)) {
            ++result->valid_channel_count;
        } else {
            if (result->invalid_channel_count < MATRIX_HAND_MAX_INVALID_CHANNEL_COUNT) {
                result->invalid_channels[result->invalid_channel_count] = ch;
                ++result->invalid_channel_count;
            }
        }
    }
}

}  // namespace

MATRIX_HAND_API MatrixHandHandle matrix_hand_create(void) {
    MatrixHandContext* ctx = new (std::nothrow) MatrixHandContext();
    return static_cast<MatrixHandHandle>(ctx);
}

MATRIX_HAND_API void matrix_hand_destroy(MatrixHandHandle handle) {
    if (handle == nullptr) {
        return;
    }
    MatrixHandContext* ctx = static_cast<MatrixHandContext*>(handle);
    delete ctx;
}

MATRIX_HAND_API int matrix_hand_reset(MatrixHandHandle handle) {
    if (handle == nullptr) {
        return MATRIX_HAND_ERROR_NULL_HANDLE;
    }
    MatrixHandContext* ctx = static_cast<MatrixHandContext*>(handle);

    ctx->algorithm.reset();

    ctx->calibrationActive = false;
    ctx->activeStage = CalibrationStage::Closed;
    ctx->completedStepCount = 0;

    ctx->activeSum.fill(0.0);
    ctx->activeFrameCount = 0;

    ctx->closedAvg.fill(0.0);
    ctx->fistAvg.fill(0.0);
    ctx->spreadAvg.fill(0.0);
    ctx->hasClosed = false;
    ctx->hasFist = false;
    ctx->hasSpread = false;

    // 重置后重新应用已保存的配置
    if (ctx->hasConfig) {
        ctx->algorithm.setRuntimeConfig(ctx->config);
    }

    return MATRIX_HAND_OK;
}

MATRIX_HAND_API int matrix_hand_set_config(MatrixHandHandle handle, const MatrixHandRuntimeConfig* config) {
    if (handle == nullptr) {
        return MATRIX_HAND_ERROR_NULL_HANDLE;
    }
    if (config == nullptr) {
        return MATRIX_HAND_ERROR_NULL_POINTER;
    }

    MatrixHandContext* ctx = static_cast<MatrixHandContext*>(handle);

    // 只能在初始状态设置配置
    if (ctx->calibrationActive || ctx->completedStepCount > 0 || ctx->algorithm.isReady()) {
        return MATRIX_HAND_ERROR_BAD_STATE;
    }

    // 预校验配置合法性
    if (config->mean_filter_window_frame_count == 0 ||
        config->thumb_gate_filter_window_size == 0 ||
        (config->thumb_inward_gate_channel != 18 && config->thumb_inward_gate_channel != 19) ||
        config->thumb_gate_deadband_ratio < 0.0 ||
        config->thumb_gate_deadband_ratio > 1.0 ||
        config->spread_deadband_ratio < 0.0 ||
        config->spread_deadband_ratio > 1.0 ||
        config->crosstalk_max_abs_intercept < 0.0) {
        return MATRIX_HAND_ERROR_INVALID_CONFIG;
    }

    RuntimeConfig internalConfig;
    internalConfig.meanFilterWindowFrameCount = config->mean_filter_window_frame_count;
    internalConfig.thumbGateFilterWindowSize = config->thumb_gate_filter_window_size;
    internalConfig.thumbInwardGateChannel = config->thumb_inward_gate_channel;
    internalConfig.thumbGateDeadbandRatio = config->thumb_gate_deadband_ratio;
    internalConfig.spreadDeadbandRatio = config->spread_deadband_ratio;
    internalConfig.crosstalkFitIntercept = (config->crosstalk_fit_intercept != 0);
    internalConfig.crosstalkMaxAbsIntercept = config->crosstalk_max_abs_intercept;

    bool ok = ctx->algorithm.setRuntimeConfig(internalConfig);
    if (!ok) {
        return MATRIX_HAND_ERROR_INVALID_CONFIG;
    }

    ctx->config = internalConfig;
    ctx->hasConfig = true;
    return MATRIX_HAND_OK;
}

MATRIX_HAND_API int matrix_hand_begin_calibration(MatrixHandHandle handle, MatrixHandCalibrationStage stage) {
    if (handle == nullptr) {
        return MATRIX_HAND_ERROR_NULL_HANDLE;
    }

    MatrixHandContext* ctx = static_cast<MatrixHandContext*>(handle);

    if (ctx->calibrationActive) {
        return MATRIX_HAND_ERROR_BAD_STATE;
    }

    // 阶段顺序校验
    CalibrationStage internalStage = toInternalStage(stage);
    bool stageOk = false;
    switch (ctx->completedStepCount) {
        case 0: stageOk = (internalStage == CalibrationStage::Closed); break;
        case 1: stageOk = (internalStage == CalibrationStage::Fist); break;
        case 2: stageOk = (internalStage == CalibrationStage::Spread); break;
        case 3: stageOk = (internalStage == CalibrationStage::Crosstalk); break;
        default: stageOk = false; break;
    }

    if (!stageOk) {
        return MATRIX_HAND_ERROR_CALIBRATION_BAD_STAGE;
    }

    ctx->algorithm.beginCalibration(internalStage);
    ctx->calibrationActive = true;
    ctx->activeStage = internalStage;
    ctx->activeSum.fill(0.0);
    ctx->activeFrameCount = 0;
    return MATRIX_HAND_OK;
}

MATRIX_HAND_API int matrix_hand_push_calibration_frame(MatrixHandHandle handle, const int16_t ad[MATRIX_HAND_CHANNEL_COUNT]) {
    if (handle == nullptr) {
        return MATRIX_HAND_ERROR_NULL_HANDLE;
    }
    if (ad == nullptr) {
        return MATRIX_HAND_ERROR_NULL_POINTER;
    }

    MatrixHandContext* ctx = static_cast<MatrixHandContext*>(handle);

    if (!ctx->calibrationActive) {
        return MATRIX_HAND_ERROR_CALIBRATION_NOT_ACTIVE;
    }

    bool ok = ctx->algorithm.pushCalibrationFrame(ad);
    if (!ok) {
        return MATRIX_HAND_ERROR_CALIBRATION_NOT_ACTIVE;
    }

    for (std::size_t i = 0; i < kChannelCount; ++i) {
        ctx->activeSum[i] += static_cast<double>(ad[i]);
    }
    ++ctx->activeFrameCount;
    return MATRIX_HAND_OK;
}

MATRIX_HAND_API int matrix_hand_finish_calibration(MatrixHandHandle handle, MatrixHandCalibrationResult* result) {
    if (handle == nullptr) {
        return MATRIX_HAND_ERROR_NULL_HANDLE;
    }
    if (result == nullptr) {
        return MATRIX_HAND_ERROR_NULL_POINTER;
    }

    MatrixHandContext* ctx = static_cast<MatrixHandContext*>(handle);

    if (!ctx->calibrationActive) {
        return MATRIX_HAND_ERROR_CALIBRATION_NOT_ACTIVE;
    }
    if (ctx->activeFrameCount == 0) {
        return MATRIX_HAND_ERROR_CALIBRATION_EMPTY;
    }

    // 清零结果
    std::memset(result, 0, sizeof(MatrixHandCalibrationResult));
    result->stage = static_cast<int32_t>(toSdkStage(ctx->activeStage));
    result->frame_count = static_cast<int32_t>(ctx->activeFrameCount);

    // 计算本阶段均值
    const auto stageAvg = computeAverage(ctx->activeSum, ctx->activeFrameCount);

    // 调用算法层结束校准
    bool algoOk = ctx->algorithm.finishCalibration();

    if (ctx->activeStage == CalibrationStage::Crosstalk) {
        if (!algoOk) {
            ctx->calibrationActive = false;
            ctx->activeSum.fill(0.0);
            ctx->activeFrameCount = 0;
            return MATRIX_HAND_ERROR_XTALK_FIT_FAILED;
        }

        // 填充串扰异常通道
        const auto unstableChList = ctx->algorithm.getXtalkUnstableChList();
        result->xtalk_unstable_count = static_cast<int32_t>(unstableChList.size());
        for (std::size_t i = 0; i < unstableChList.size() && i < MATRIX_HAND_MAX_INVALID_CHANNEL_COUNT; ++i) {
            result->xtalk_unstable_channels[i] = unstableChList[i];
        }
        result->valid_channel_count = static_cast<int32_t>(ctx->algorithm.getXtalkValidTargetChannelCount());

        ctx->completedStepCount++;
        ctx->calibrationActive = false;
        ctx->activeSum.fill(0.0);
        ctx->activeFrameCount = 0;
        return MATRIX_HAND_OK;
    }

    // 非 Crosstalk 阶段
    if (!algoOk) {
        ctx->calibrationActive = false;
        ctx->activeSum.fill(0.0);
        ctx->activeFrameCount = 0;
        return MATRIX_HAND_ERROR_CALIBRATION_EMPTY;
    }

    // 保存阶段均值并填充校准结果
    if (ctx->activeStage == CalibrationStage::Closed) {
        ctx->closedAvg = stageAvg;
        ctx->hasClosed = true;
        // Closed 不做通道跨度判定
        result->valid_channel_count = MATRIX_HAND_CHANNEL_COUNT;
        result->invalid_channel_count = 0;
    } else if (ctx->activeStage == CalibrationStage::Fist) {
        ctx->fistAvg = stageAvg;
        ctx->hasFist = true;
        fillFistInvalidChannels(ctx->fistAvg, ctx->closedAvg,
                                ctx->config.thumbInwardGateChannel, result);
    } else if (ctx->activeStage == CalibrationStage::Spread) {
        ctx->spreadAvg = stageAvg;
        ctx->hasSpread = true;
        fillSpreadInvalidChannels(ctx->spreadAvg, ctx->closedAvg, result);
    }

    ctx->completedStepCount++;
    ctx->calibrationActive = false;
    ctx->activeSum.fill(0.0);
    ctx->activeFrameCount = 0;
    return MATRIX_HAND_OK;
}

MATRIX_HAND_API int matrix_hand_is_ready(MatrixHandHandle handle, int32_t* ready) {
    if (handle == nullptr) {
        return MATRIX_HAND_ERROR_NULL_HANDLE;
    }
    if (ready == nullptr) {
        return MATRIX_HAND_ERROR_NULL_POINTER;
    }

    MatrixHandContext* ctx = static_cast<MatrixHandContext*>(handle);
    *ready = ctx->algorithm.isReady() ? 1 : 0;
    return MATRIX_HAND_OK;
}

MATRIX_HAND_API int matrix_hand_process_frame(
    MatrixHandHandle handle,
    const int16_t ad[MATRIX_HAND_CHANNEL_COUNT],
    MatrixHandAngleOutput* output) {
    if (handle == nullptr) {
        return MATRIX_HAND_ERROR_NULL_HANDLE;
    }
    if (ad == nullptr || output == nullptr) {
        return MATRIX_HAND_ERROR_NULL_POINTER;
    }

    MatrixHandContext* ctx = static_cast<MatrixHandContext*>(handle);

    if (!ctx->algorithm.isReady()) {
        std::memset(output, 0, sizeof(MatrixHandAngleOutput));
        return MATRIX_HAND_ERROR_NOT_READY;
    }

    handdemo::HandAngleOutput internalOutput{};
    bool ok = ctx->algorithm.processFrame(ad, internalOutput);
    if (!ok) {
        std::memset(output, 0, sizeof(MatrixHandAngleOutput));
        return MATRIX_HAND_ERROR_NOT_READY;
    }

    std::memcpy(output->little_finger, internalOutput.little_finger, sizeof(internalOutput.little_finger));
    std::memcpy(output->ring_finger, internalOutput.ring_finger, sizeof(internalOutput.ring_finger));
    std::memcpy(output->middle_finger, internalOutput.middle_finger, sizeof(internalOutput.middle_finger));
    std::memcpy(output->index_finger, internalOutput.index_finger, sizeof(internalOutput.index_finger));
    std::memcpy(output->thumb, internalOutput.thumb, sizeof(internalOutput.thumb));
    return MATRIX_HAND_OK;
}

MATRIX_HAND_API int matrix_hand_get_current_ad(MatrixHandHandle handle, int32_t filtered, double ad[MATRIX_HAND_CHANNEL_COUNT]) {
    if (handle == nullptr) {
        return MATRIX_HAND_ERROR_NULL_HANDLE;
    }
    if (ad == nullptr) {
        return MATRIX_HAND_ERROR_NULL_POINTER;
    }

    MatrixHandContext* ctx = static_cast<MatrixHandContext*>(handle);
    const auto internalAd = ctx->algorithm.getCurrentAd(filtered != 0);
    for (std::size_t i = 0; i < kChannelCount; ++i) {
        ad[i] = internalAd[i];
    }
    return MATRIX_HAND_OK;
}

MATRIX_HAND_API const char* matrix_hand_status_text(int status) {
    switch (static_cast<MatrixHandStatus>(status)) {
        case MATRIX_HAND_OK:                         return "OK";
        case MATRIX_HAND_ERROR_NULL_HANDLE:           return "NULL_HANDLE";
        case MATRIX_HAND_ERROR_NULL_POINTER:          return "NULL_POINTER";
        case MATRIX_HAND_ERROR_INVALID_CONFIG:        return "INVALID_CONFIG";
        case MATRIX_HAND_ERROR_BAD_STATE:              return "BAD_STATE";
        case MATRIX_HAND_ERROR_NOT_READY:              return "NOT_READY";
        case MATRIX_HAND_ERROR_CALIBRATION_NOT_ACTIVE: return "CALIBRATION_NOT_ACTIVE";
        case MATRIX_HAND_ERROR_CALIBRATION_EMPTY:      return "CALIBRATION_EMPTY";
        case MATRIX_HAND_ERROR_CALIBRATION_BAD_STAGE:  return "CALIBRATION_BAD_STAGE";
        case MATRIX_HAND_ERROR_XTALK_FIT_FAILED:       return "XTALK_FIT_FAILED";
        default:                                       return "UNKNOWN_STATUS";
    }
}
