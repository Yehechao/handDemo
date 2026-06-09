// Copyright (c) 2026 Matrix 墨现科技. All rights reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "config.h"

namespace handdemo {

enum class CalibrationStage : int32_t {
    Closed = 0,
    Fist = 1,
    Spread = 2,
    Crosstalk = 3,
};

struct HandAngleOutput {
    float little_finger[4];  // [MCP, PIP, DIP, pinky-ring_spread]
    float ring_finger[4];    // [MCP, PIP, DIP, ring-middle_spread]
    float middle_finger[3];  // [MCP, PIP, DIP]
    float index_finger[4];   // [MCP, PIP, DIP, index-middle_spread]
    float thumb[3];          // [MCP, IP, thumb-index_spread]  正=外展, 负=内收
};

struct RuntimeConfig {
    std::size_t meanFilterWindowFrameCount = kMeanFilterWindowFrameCount;
    std::size_t thumbGateFilterWindowSize = kThumbGateFilterWindowSize;
    int thumbInwardGateChannel = kThumbInwardGateChannel;
    double thumbGateDeadbandRatio = kThumbGateDeadbandRatio;
    double spreadDeadbandRatio = kSpreadDeadbandRatio;
    bool crosstalkFitIntercept = kCrosstalkFitIntercept;
    double crosstalkMaxAbsIntercept = kCrosstalkMaxAbsIntercept;
};

// XtalkCoef: 单通道串扰补偿系数，ΔP = aΔT1 + bΔT2 + cΔT3 + d。
struct XtalkCoef {
    double a = 0.0;
    double b = 0.0;
    double c = 0.0;
    double d = 0.0;
    bool isValid = false;
};

class HandAngleAlgorithm {
public:
    HandAngleAlgorithm();

    // setRuntimeConfig: 覆盖部分默认参数，在校准前调用；
    bool setRuntimeConfig(const RuntimeConfig& runtimeConfig);
    // reset: 清空全部校准状态和滤波状态
    void reset();
    void beginCalibration(CalibrationStage stage);
    bool pushCalibrationFrame(const int16_t adValues[kChannelCount]);
    bool finishCalibration();
    // isReady: Closed/Fist/Spread 三步校准都完成后返回 true（Crosstalk 可选，不阻塞）
    bool isReady() const;
    // processFrame: 传入一帧 AD 数据并输出弯曲角结构体，未完成校准时返回 false
    bool processFrame(const int16_t adValues[kChannelCount], HandAngleOutput& outputValue);
    // getCurrentAd: 获取最近一帧 AD 值，不受校准状态影响。
    // filtered=true 返回均值滤波后的值，filtered=false 返回原始 AD。
    std::array<double, kChannelCount> getCurrentAd(bool filtered = true) const;
    // getXtalkUnstableChList: 返回串扰校准后 |d| 超限的异常通道列表（1-based CH 编号），客户可据此判断校准稳定性
    std::vector<int> getXtalkUnstableChList() const;
    // getXtalkValidTargetChannelCount: 返回串扰拟合成功的有效通道数，为 0 表示串扰校准失败
    std::size_t getXtalkValidTargetChannelCount() const;

private:
    struct SampleState {
        bool isActive = false;
        CalibrationStage stage = CalibrationStage::Closed;
        std::array<double, kChannelCount> sumValueList{};
        std::size_t frameCount = 0;
        // frameValueList: 仅 Crosstalk 阶段使用，保存完整帧序列供最小二乘拟合
        std::deque<std::array<double, kChannelCount>> frameValueList;
    };

    struct RatioState {
        bool isInitialized = false;
        double stableRatio = 0.0;
    };

    struct FilterState {
        std::array<double, kChannelCount> filteredValueList{};
        std::array<double, kChannelCount> sumValueList{};
        std::deque<std::array<double, kChannelCount>> frameWindowList;
    };

    void resetFilterState();
    void resetSamplingState();

    std::array<double, kChannelCount> avgCalibFrm() const;
    std::array<double, kChannelCount> stageCalibTpl(CalibrationStage stage) const;
    void setStageCalib(CalibrationStage stage, const std::array<double, kChannelCount>& averageValueList);

    std::array<double, kChannelCount> filterFrm(const int16_t adValues[kChannelCount]);
    std::array<double, kChannelCount> meanFilteredFrm(const int16_t adValues[kChannelCount]);

    double getFlexRatio(int channelIndex, double currentValue);
    void buildOutputValue(const std::array<double, kChannelCount>& channelValueList, HandAngleOutput& outputValue);

    double stabilizeRatio(RatioState& stableState, double ratioValue, double deadbandRatio);
    double getSpreadRatio(int channelIndex, double currentValue);
    double getThumbGateRatio(const std::array<double, kChannelCount>& channelValueList);
    double getThumbInwardAmplitudeRatio(const std::array<double, kChannelCount>& channelValueList);
    bool isChannelValidForStage(int channelIndex, CalibrationStage stage) const;

    // 串扰补偿，返回有效拟合通道数
    std::size_t fitXtalkCoefs(const std::deque<std::array<double, kChannelCount>>& frameList);
    XtalkCoef fitXtalkCoefForChannel(const std::deque<std::array<double, kChannelCount>>& frameList, int channelIndex) const;
    std::array<double, kChannelCount> applyXtalk(const std::array<double, kChannelCount>& channelValueList) const;

    std::array<double, kChannelCount> closedCalib_{};
    std::array<double, kChannelCount> fistCalib_{};
    std::array<double, kChannelCount> openCalib_{};
    bool hasClosed_ = false;
    bool hasFist_ = false;
    bool hasOpen_ = false;

    RuntimeConfig runtimeConfig_{};
    SampleState samplingState_{};
    FilterState rawFilter_{};
    std::array<int16_t, kChannelCount> latestRawAd_{};
    std::array<RatioState, kChannelCount> flexStable_{};
    std::array<RatioState, kChannelCount> spreadStable_{};
    RatioState thumbGateStableState_{};
    std::deque<double> thumbGateFilterDeque_{};
    RatioState thumbInwardAmplitudeStable_{};

    // 串扰补偿
    std::array<XtalkCoef, kChannelCount> xtalkCoef_{};
    std::array<double, kChannelCount> xtalkBase_{};
    bool hasXtalk_ = false;
    std::size_t xtalkValidTargetChannelCount_ = 0;
    std::vector<int> xtalkUnstableChList_{};
};

}  
