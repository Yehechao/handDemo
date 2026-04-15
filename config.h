#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace handdemo {

// ==================== 1. 基础协议参数 ====================

// kChannelCount: 手套每帧固定输入的 AD 通道数。
constexpr std::size_t kChannelCount = 18;

// kAdMinValue/kAdMaxValue: 单路 AD 的合法输入范围。
constexpr int16_t kAdMinValue = 0;
constexpr int16_t kAdMaxValue = 4096;

// ==================== 2. 校准流程参数 ====================

// kSamplingDurationMs: 每个校准阶段的采样时长，单位毫秒。
// 纯弯曲版本只保留 closed 和 fist 两步校准。
constexpr int kSamplingDurationMs = 2000;

// kMaxSamplingFrameCount: 单次校准允许缓存的最大帧数。
constexpr std::size_t kMaxSamplingFrameCount = 5000;

// ==================== 3. One Euro 滤波参数 ====================

// kOneEuroDerivativeCutoff: One Euro 导数滤波截止频率。
constexpr double kOneEuroDerivativeCutoff = 10.0;

// kOneEuroFlexMinCutoff: 弯曲通道的最小截止频率。
constexpr double kOneEuroFlexMinCutoff = 70.0;

// kOneEuroFlexBeta: 弯曲通道的动态响应系数。
constexpr double kOneEuroFlexBeta = 0.015;

// ==================== 4. ratio 稳定层参数 ====================

// kFlexDeadbandRatio: 弯曲 ratio 的死区。
constexpr double kFlexDeadbandRatio = 0.010;

// kSmallMoveAlpha: 小幅动作时稳定层的平滑系数。
constexpr double kSmallMoveAlpha = 0.22;

// kLargeMoveAlpha: 大幅动作时稳定层的平滑系数。
constexpr double kLargeMoveAlpha = 0.60;

// kLargeMoveThresholdRatio: 区分“小动作”和“大动作”的阈值。
constexpr double kLargeMoveThresholdRatio = 0.08;

// kZeroSnapInRatio/kZeroSnapOutRatio: 接近 0 时的吸附阈值。
constexpr double kZeroSnapInRatio = 0.012;
constexpr double kZeroSnapOutRatio = 0.028;

// kOneSnapInRatio/kOneSnapOutRatio: 接近 1 时的吸附阈值。
constexpr double kOneSnapInRatio = 0.988;
constexpr double kOneSnapOutRatio = 0.972;

// ==================== 5. 弯曲补偿参数 ====================

// kEnableFlexCompensation: 是否启用弯曲弱通道补偿。
constexpr bool kEnableFlexCompensation = true;

// kFlexWeakThresholdRatio: 弯曲通道被判定为偏弱的阈值比例。
constexpr double kFlexWeakThresholdRatio = 0.85;

// kFlexMinSpanRatio: 弯曲补偿时的最小参考跨度比例。
constexpr double kFlexMinSpanRatio = 0.35;

// kFlexMaxBoostFactor: 弯曲补偿允许的最大增强倍数。
constexpr double kFlexMaxBoostFactor = 1.60;

// kFlexCurveBlendRatio: 弯曲补偿结果和原始 ratio 的混合比例。
constexpr double kFlexCurveBlendRatio = 1.00;

// ==================== 6. 通道映射参数 ====================

// kFlexChannelIndexList: 所有参与弯曲校准、滤波输出和弯曲补偿的通道编号集合。
// CH4/CH8/CH12/CH16 只保留接收，不进入算法处理。
constexpr std::array<int, 14> kFlexChannelIndexList = {
    18, 17,
    15, 14, 13,
    11, 10, 9,
    7, 6, 5,
    3, 2, 1,
};

// FourFingerIndex: 四指内部数组顺序。
enum class FourFingerIndex : std::size_t {
    Index = 0,
    Middle = 1,
    Ring = 2,
    Little = 3,
};

// FingerChannelModel: 单根四指的弯曲通道映射定义。
struct FingerChannelModel {
    // rootFlexChannel: MCP 根节弯曲通道。
    int rootFlexChannel;

    // jointFlexChannel1: PIP 弯曲通道。
    int jointFlexChannel1;

    // jointFlexChannel2: DIP 弯曲通道。
    int jointFlexChannel2;
};

// kFingerChannelModelByIndex: 四指弯曲通道映射，顺序为食指、中指、无名指、小指。
constexpr std::array<FingerChannelModel, 4> kFingerChannelModelByIndex = {{
    {15, 14, 13},
    {11, 10, 9},
    {7, 6, 5},
    {3, 2, 1},
}};

// ==================== 7. 输出角度协议参数 ====================

// FingerFlexAngleModel: 单根四指的弯曲角上限定义，单位为度。
struct FingerFlexAngleModel {
    // rootHoldDeltaAngle: MCP 最大弯曲角。
    double rootHoldDeltaAngle;

    // jointHoldDeltaAngle1: PIP 最大弯曲角。
    double jointHoldDeltaAngle1;

    // jointHoldDeltaAngle2: DIP 最大弯曲角。
    double jointHoldDeltaAngle2;
};

// kFingerFlexAngleModelByIndex: 四指弯曲角上限，顺序为食指、中指、无名指、小指。
constexpr std::array<FingerFlexAngleModel, 4> kFingerFlexAngleModelByIndex = {{
    {90.0, 100.0, 85.0},
    {90.0, 100.0, 85.0},
    {85.0, 100.0, 85.0},
    {85.0, 95.0, 85.0},
}};

// ThumbFlexAngleModel: 拇指两级弯曲角上限定义，单位为度。
struct ThumbFlexAngleModel {
    // mcpHoldDeltaAngle: 拇指 MCP 最大弯曲角。
    double mcpHoldDeltaAngle;

    // ipHoldDeltaAngle: 拇指 IP 最大弯曲角。
    double ipHoldDeltaAngle;
};

// kThumbFlexAngleModel: 拇指弯曲角上限。
constexpr ThumbFlexAngleModel kThumbFlexAngleModel = {
    60.0,
    80.0,
};

}  // namespace handdemo
