#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace handdemo {

// ==================== 1. 基础协议参数 ====================

// kChannelCount: 手套每帧固定输入的 AD 通道数。
constexpr std::size_t kChannelCount = 19;

// kAdMinValue/kAdMaxValue: 单路 AD 的合法输入范围。
constexpr int16_t kAdMinValue = 0;
constexpr int16_t kAdMaxValue = 4096;

// ==================== 2. 校准流程参数 ====================

// kSamplingDurationMs: 三步校准中每个阶段的采样时长，单位毫秒。
constexpr int kSamplingDurationMs = 2000;

// kMaxSamplingFrameCount: 单次校准允许缓存的最大帧数。
constexpr std::size_t kMaxSamplingFrameCount = 5000;

// ==================== 3. 均值滤波参数 ====================

// kMeanFilterWindowFrameCount: 算法内部实时均值滤波窗口大小，表示最近 15 帧。
constexpr std::size_t kMeanFilterWindowFrameCount = 15;
// kThumbGateFilterWindowSize: 拇指门控比例独立滤波窗口大小，
constexpr std::size_t kThumbGateFilterWindowSize = 10;
// kFlexDeadbandRatio: 弯曲 ratio 的死区。
constexpr double kFlexDeadbandRatio = 0.0;

// ==================== 4. 展开（Spread）参数 ====================

// kSpreadChannelIndexList: 展开通道编号列表（CH4/8/12/16）。
constexpr std::array<int, 4> kSpreadChannelIndexList = {4, 8, 12, 16};

// SpreadPairConfig: 单组指缝对的展开映射定义。
struct SpreadPairConfig {
    // channelIndex: 传感器通道号。
    int channelIndex;
    // openRootAngle: 外层手指最大展开角，单位度。
    double openRootAngle;
    // angleScale: 显示缩放系数。
    double angleScale;
};

// kSpreadPairConfigList: 三组四指指缝展开配置，顺序为 ringPinky、middleRing、indexMiddle。
constexpr std::array<SpreadPairConfig, 3> kSpreadPairConfigList = {{
    {4,  25.0, 1.40},  // ringPinky:   外指=小指
    {8,  20.0, 1.30},  // middleRing:  外指=无名指
    {12, 25.0, 1.12},  // indexMiddle: 外指=食指
}};

// kThumbOpenPalmAngle/kThumbInwardPalmAngle: 拇指外展/内收最大角度，单位度。
constexpr double kThumbOpenPalmAngle = 45.0;
constexpr double kThumbInwardPalmAngle = 45.0;
// kThumbInwardGateChannel: 拇指开合方向通道，CH18或者CH19
constexpr int kThumbInwardGateChannel = 18;

// kThumbFlexGateStartRatio/kThumbFlexGateEndRatio: 拇指内收门控 smoothstep 映射区间。
constexpr double kThumbFlexGateStartRatio = 0.18;
constexpr double kThumbFlexGateEndRatio = 0.45;

// kThumbGateDeadbandRatio: 拇指内收门控死区。
constexpr double kThumbGateDeadbandRatio = 0.0;

// kSpreadDeadbandRatio: 展开 ratio 死区。
constexpr double kSpreadDeadbandRatio = 0.0;

// kFoldSpreadSuppress*: 根节弯曲触发展开角算法收束，中节/末节不触发。
constexpr double kFoldSpreadSuppressStartRatio = 0.25;
constexpr double kFoldSpreadSuppressEndRatio = 0.75;

// ==================== 5. 通道映射参数 ====================

// kFlexChannelIndexList: 所有参与弯曲校准和滤波输出的通道编号集合。
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

// ==================== 6. 输出角度协议参数 ====================

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
// 对齐 Python kinematics.fingerModelByFingerName:
// index/middle/ring/pinky: holdRootAngle=90, holdJointAngleList=[90, 90]
constexpr std::array<FingerFlexAngleModel, 4> kFingerFlexAngleModelByIndex = {{
    {90.0, 90.0, 90.0},  // 食指
    {90.0, 90.0, 90.0},  // 中指
    {90.0, 90.0, 90.0},  // 无名指
    {90.0, 90.0, 90.0},  // 小指
}};

// ThumbFlexAngleModel: 拇指两级弯曲角上限定义，单位为度。
struct ThumbFlexAngleModel {
    // mcpHoldDeltaAngle: 拇指 MCP 最大弯曲角。
    double mcpHoldDeltaAngle;

    // ipHoldDeltaAngle: 拇指 IP 最大弯曲角。
    double ipHoldDeltaAngle;
};

// holdSegment23Angle=85 (CH17), holdSegment34Angle=85 (CH18)
constexpr ThumbFlexAngleModel kThumbFlexAngleModel = {
    85.0,
    90.0,
};

}  // namespace handdemo
