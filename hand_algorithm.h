#pragma once

#include <array>
#include <cstdint>
#include <deque>

#include "config.h"

namespace handdemo {

enum class CalibrationStage : int32_t {
    Closed = 0,
    Fist = 1,
};

struct HandAngleOutput {
    float little_finger[3];
    float ring_finger[3];
    float middle_finger[3];
    float index_finger[3];
    float thumb[2];
};

class HandAngleAlgorithm {
public:
    HandAngleAlgorithm();

    // reset: 清空全部校准状态、滤波状态和补偿状态
    void reset();
    // beginCalibration: 开始某一个校准阶段，后续持续喂入 2 秒 AD 帧
    void beginCalibration(CalibrationStage stage);
    // pushCalibrationFrame: 采样期间向算法层送入一帧 AD 数据
    bool pushCalibrationFrame(const int16_t adValues[kChannelCount]);
    // finishCalibration: 结束当前校准阶段，按平均值写入该阶段参考基准
    bool finishCalibration();
    // isReady: 两步校准都完成后返回 true
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

    struct CompensationState {
        bool isActive = false;
        double boostFactorValue = 1.0;
        double gammaValue = 1.0;
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
    void resetCompensationState();

    std::array<double, kChannelCount> buildAverageCalibrationFrame() const;
    std::array<double, kChannelCount> buildStageCalibrationTemplate(CalibrationStage stage) const;
    void applyStageCalibrationValue(CalibrationStage stage, const std::array<double, kChannelCount>& averageValueList);
    void rebuildChannelCompensation();

    std::array<double, kChannelCount> filterFrameValueList(const int16_t adValues[kChannelCount]);
    std::array<double, kChannelCount> getMeanFilteredFrameValueList(const int16_t adValues[kChannelCount]);

    double getFlexRatio(int channelIndex, double currentValue);
    void buildOutputValue(const std::array<double, kChannelCount>& channelValueList, HandAngleOutput& outputValue);

    double stabilizeRatio(RatioStableState& stableState, double ratioValue, double deadbandRatio);
    double applyCompensationRatio(double ratioValue, const CompensationState& compensationState, double curveBlendRatio) const;

    std::array<double, kChannelCount> closedCalibrationValueList_{};
    std::array<double, kChannelCount> fistCalibrationValueList_{};
    bool hasClosedCalibration_ = false;
    bool hasFistCalibration_ = false;

    SamplingState samplingState_{};
    RawFilterState rawFilterState_{};
    std::array<RatioStableState, kChannelCount> flexStableStateByChannel_{};
    std::array<CompensationState, kChannelCount> flexCompensationStateByChannel_{};
};

}  
