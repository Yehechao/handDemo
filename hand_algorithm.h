#pragma once

#include <array>
#include <cstdint>
#include <deque>

#include "config.h"

namespace handdemo {

enum class CalibrationStage : int32_t {
    Closed = 0,
    Fist = 1,
    Spread = 2,
};

struct HandAngleOutput {
    float little_finger[4];  // [MCP, PIP, DIP, pinky-ring_spread]
    float ring_finger[4];    // [MCP, PIP, DIP, ring-middle_spread]
    float middle_finger[3];  // [MCP, PIP, DIP]
    float index_finger[4];   // [MCP, PIP, DIP, index-middle_spread]
    float thumb[3];          // [MCP, IP, thumb-index_spread]  正=外展, 负=内收
};

class HandAngleAlgorithm {
public:
    HandAngleAlgorithm();

    // reset: 清空全部校准状态和滤波状态
    void reset();
    void beginCalibration(CalibrationStage stage);
    bool pushCalibrationFrame(const int16_t adValues[kChannelCount]);
    bool finishCalibration();
    // isReady: 三步校准（Closed/Fist/Spread）都完成后返回 true
    bool isReady() const;
    // processFrame: 传入一帧 AD 数据并输出弯曲角结构体，未完成校准时返回 false
    bool processFrame(const int16_t adValues[kChannelCount], HandAngleOutput& outputValue);

private:
    struct SamplingState {
        bool isActive = false;
        CalibrationStage stage = CalibrationStage::Closed;
        std::array<double, kChannelCount> sumValueList{};
        std::size_t frameCount = 0;
    };

    struct RatioStableState {
        bool isInitialized = false;
        double stableRatio = 0.0;
    };

    struct RawFilterState {
        std::array<double, kChannelCount> filteredValueList{};
        std::array<double, kChannelCount> sumValueList{};
        std::deque<std::array<double, kChannelCount>> frameWindowList;
    };

    void resetFilterState();
    void resetSamplingState();

    std::array<double, kChannelCount> buildAverageCalibrationFrame() const;
    std::array<double, kChannelCount> buildStageCalibrationTemplate(CalibrationStage stage) const;
    void applyStageCalibrationValue(CalibrationStage stage, const std::array<double, kChannelCount>& averageValueList);

    std::array<double, kChannelCount> filterFrameValueList(const int16_t adValues[kChannelCount]);
    std::array<double, kChannelCount> getMeanFilteredFrameValueList(const int16_t adValues[kChannelCount]);

    double getFlexRatio(int channelIndex, double currentValue);
    void buildOutputValue(const std::array<double, kChannelCount>& channelValueList, HandAngleOutput& outputValue);

    double stabilizeRatio(RatioStableState& stableState, double ratioValue, double deadbandRatio);
    double getSpreadRatio(int channelIndex, double currentValue);
    double getThumbGateRatio(double ch18Value);

    std::array<double, kChannelCount> closedCalibrationValueList_{};
    std::array<double, kChannelCount> fistCalibrationValueList_{};
    std::array<double, kChannelCount> spreadCalibrationValueList_{};
    bool hasClosedCalibration_ = false;
    bool hasFistCalibration_ = false;
    bool hasSpreadCalibration_ = false;

    SamplingState samplingState_{};
    RawFilterState rawFilterState_{};
    std::array<RatioStableState, kChannelCount> flexStableStateByChannel_{};
    std::array<RatioStableState, kChannelCount> spreadStableStateByChannel_{};
    RatioStableState thumbGateStableState_{};
    std::deque<double> thumbGateFilterDeque_{};
};

}  
