// Copyright (c) 2026 Matrix 墨现科技. All rights reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace handdemo {

// ==================== 1. 基础协议参数 ====================

// 手套每帧固定输入的 AD 通道数。
constexpr std::size_t kChannelCount = 19;

// 单路 AD 的合法输入范围。
constexpr int16_t kAdMinValue = 0;
constexpr int16_t kAdMaxValue = 4096;

// ==================== 2. 校准流程参数 ====================

// 单阶段校准采样时长（毫秒）。
constexpr int kSamplingDurationMs = 2000;

// 单次校准允许缓存的最大帧数。
constexpr std::size_t kMaxSamplingFrameCount = 5000;

// ==================== 3. 均值滤波参数 ====================

// 算法内部实时均值滤波窗口大小。
constexpr std::size_t kMeanFilterWindowFrameCount = 10;
// 拇指门控比例独立滤波窗口大小。
constexpr std::size_t kThumbGateFilterWindowSize = 10;
// 弯曲 ratio 的死区。
constexpr double kFlexDeadbandRatio = 0.0;

// 校准阶段目标值相对闭合值的正向变化小于该阈值时，判定该通道无效并按 0 度处理。
constexpr double kInvalidChannelSpanThresholdValue = 10.0;

// ==================== 4. 展开（Spread）参数 ====================

// 展开通道编号列表（CH4/8/12/16）。
constexpr std::array<int, 4> kSpreadChannelIndexList = {4, 8, 12, 16};

// 单组指缝对的展开映射定义。
struct SpreadPairConfig {
    int channelIndex;
    double openRootAngle;
};

// 三组四指指缝展开配置，顺序为 ringPinky、middleRing、indexMiddle，展开最大角 25 度。
constexpr std::array<SpreadPairConfig, 3> kSpreadPairConfigList = {{
    {4,  25.0},
    {8,  25.0},
    {12, 25.0},
}};

// 拇指外展/内收最大角度（度）。
constexpr double kThumbOpenPalmAngle = 45.0;
constexpr double kThumbInwardPalmAngle = 45.0;
// 拇指开合方向通道，可选 CH18 或 CH19。
constexpr int kThumbInwardGateChannel = 18;

// 拇指内收门控 smoothstep 映射区间。
constexpr double kThumbFlexGateStartRatio = 0.1;
constexpr double kThumbFlexGateEndRatio = 0.3;

// 拇指内收门控死区。
constexpr double kThumbGateDeadbandRatio = 0.025;

// 展开 ratio 死区。
constexpr double kSpreadDeadbandRatio = 0.02;

// 相邻第一指节弯曲触发展开角收束参数，中节/末节不触发。
constexpr double kFoldSpreadSuppressStartRatio = 0.10;
constexpr double kFoldSpreadSuppressEndRatio = 0.60;

// ==================== 5. 通道映射参数 ====================

// 所有参与弯曲校准和滤波输出的通道编号集合。
constexpr std::array<int, 14> kFlexChannelIndexList = {
    18, 17,
    15, 14, 13,
    11, 10, 9,
    7, 6, 5,
    3, 2, 1,
};

// 四指内部数组顺序。
enum class FourFingerIndex : std::size_t {
    Index = 0,
    Middle = 1,
    Ring = 2,
    Little = 3,
};

// 单根四指的弯曲通道映射定义。
struct FingerChannelModel {
    int rootFlexChannel;     // MCP 根节
    int jointFlexChannel1;   // PIP 中节
    int jointFlexChannel2;   // DIP 末节
};

// 四指弯曲通道映射，顺序为食指、中指、无名指、小指。
constexpr std::array<FingerChannelModel, 4> kFingerChannelModelByIndex = {{
    {15, 14, 13},
    {11, 10, 9},
    {7, 6, 5},
    {3, 2, 1},
}};

// ==================== 6. 输出角度协议参数 ====================

// 单根四指的弯曲角上限定义（度）。
struct FingerFlexAngleModel {
    double rootHoldDeltaAngle;    // MCP 最大弯曲角
    double jointHoldDeltaAngle1;  // PIP 最大弯曲角
    double jointHoldDeltaAngle2;  // DIP 最大弯曲角
};

// 四指弯曲角上限：食指/中指/无名指/小指 MCP=85, PIP=85, DIP=90。
constexpr std::array<FingerFlexAngleModel, 4> kFingerFlexAngleModelByIndex = {{
    {85.0, 85.0, 90.0},
    {85.0, 85.0, 90.0},
    {85.0, 85.0, 90.0},
    {85.0, 85.0, 90.0},
}};

// 拇指两级弯曲角上限定义（度）。
struct ThumbFlexAngleModel {
    double mcpHoldDeltaAngle;  // MCP 最大弯曲角
    double ipHoldDeltaAngle;   // IP 最大弯曲角
};

constexpr ThumbFlexAngleModel kThumbFlexAngleModel = {
    85.0,
    85.0,
};

// ==================== 7. 串扰补偿（Crosstalk）参数 ====================

// 第四阶段串扰补偿校准的采样时长（毫秒）。
constexpr int kCrosstalkSamplingDurationMs = 4000;

// 作为自变量的驱动通道。
constexpr std::array<int, 3> kCrosstalkDriverChannelList = {17, 19, 16};

// 被补偿的目标通道（CH15~CH1，不含 CH18）。
constexpr std::array<int, 15> kCrosstalkTargetChannelList = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1,
};

// 不参与串扰拟合的通道。
constexpr int kCrosstalkExcludedChannel = 18;

// 是否拟合截距项 d。
constexpr bool kCrosstalkFitIntercept = true;

// 截距绝对值上限，超过此值的通道标记为异常。
constexpr double kCrosstalkMaxAbsIntercept = 30.0;

}  // namespace handdemo
