#include "hand_algorithm.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace handdemo {

namespace {

double clampValue(double rawValue, double minValue, double maxValue) {
    if (rawValue < minValue) {
        return minValue;
    }
    if (rawValue > maxValue) {
        return maxValue;
    }
    return rawValue;
}

double calculateMedian(const std::vector<double>& valueList) {
    if (valueList.empty()) {
        return 0.0;
    }
    std::vector<double> sortedValueList = valueList;
    std::sort(sortedValueList.begin(), sortedValueList.end());
    const std::size_t middleIndex = sortedValueList.size() / 2U;
    if ((sortedValueList.size() % 2U) == 1U) {
        return sortedValueList[middleIndex];
    }
    return (sortedValueList[middleIndex - 1U] + sortedValueList[middleIndex]) * 0.5;
}

int toChannelArrayIndex(int channelIndex) {
    return channelIndex - 1;
}

double calculateChannelRatio(double currentValue, double startValue, double endValue) {
    // 弯曲通道统一采用单向正向模型：
    // Closed 是 0 度基线，只有当前值高于 Closed 才允许产生弯曲。
    if (currentValue <= startValue) {
        return 0.0;
    }

    const double deltaValue = endValue - startValue;
    if (deltaValue <= 1.0) {
        return 0.0;
    }

    return clampValue((currentValue - startValue) / deltaValue, 0.0, 1.0);
}

float convertAngleToFloat(double degreeValue) {
    const double roundedValue = std::round(degreeValue * 10.0) / 10.0;
    const double minValue = -static_cast<double>(std::numeric_limits<float>::max());
    const double maxValue = static_cast<double>(std::numeric_limits<float>::max());
    return static_cast<float>(clampValue(roundedValue, minValue, maxValue));
}

void clearOutputValueList(float angleValueList[], std::size_t valueCount) {
    for (std::size_t valueIndex = 0; valueIndex < valueCount; ++valueIndex) {
        angleValueList[valueIndex] = 0.0f;
    }
}

}  // namespace

HandAngleAlgorithm::HandAngleAlgorithm() {
    reset();
}

void HandAngleAlgorithm::resetSamplingState() {
    samplingState_.isActive = false;
    samplingState_.stage = CalibrationStage::Closed;
    samplingState_.sumValueList.fill(0.0);
    samplingState_.frameCount = 0;
}

void HandAngleAlgorithm::resetFilterState() {
    rawFilterState_.filteredValueList.fill(0.0);
    rawFilterState_.sumValueList.fill(0.0);
    rawFilterState_.frameWindowList.clear();

    for (auto& stableState : flexStableStateByChannel_) {
        stableState = {};
    }
}

void HandAngleAlgorithm::resetCompensationState() {
    for (auto& compensationState : flexCompensationStateByChannel_) {
        compensationState = {};
    }
}

void HandAngleAlgorithm::reset() {
    hasClosedCalibration_ = false;
    hasFistCalibration_ = false;
    closedCalibrationValueList_.fill(0.0);
    fistCalibrationValueList_.fill(0.0);
    resetSamplingState();
    resetFilterState();
    resetCompensationState();
}

void HandAngleAlgorithm::beginCalibration(CalibrationStage stage) {
    resetSamplingState();
    samplingState_.isActive = true;
    samplingState_.stage = stage;
}

bool HandAngleAlgorithm::pushCalibrationFrame(const int16_t adValues[kChannelCount]) {
    if (!samplingState_.isActive || adValues == nullptr) {
        return false;
    }
    if (samplingState_.frameCount >= kMaxSamplingFrameCount) {
        return true;
    }

    for (std::size_t channelIndex = 0; channelIndex < kChannelCount; ++channelIndex) {
        samplingState_.sumValueList[channelIndex] += static_cast<double>(adValues[channelIndex]);
    }
    ++samplingState_.frameCount;
    return true;
}

std::array<double, kChannelCount> HandAngleAlgorithm::buildAverageCalibrationFrame() const {
    std::array<double, kChannelCount> averageValueList{};
    averageValueList.fill(0.0);
    if (samplingState_.frameCount == 0) {
        return averageValueList;
    }
    for (std::size_t channelIndex = 0; channelIndex < kChannelCount; ++channelIndex) {
        averageValueList[channelIndex] = samplingState_.sumValueList[channelIndex] / static_cast<double>(samplingState_.frameCount);
    }
    return averageValueList;
}

std::array<double, kChannelCount> HandAngleAlgorithm::buildStageCalibrationTemplate(CalibrationStage stage) const {
    if (stage == CalibrationStage::Fist && hasFistCalibration_) {
        return fistCalibrationValueList_;
    }
    if (hasClosedCalibration_) {
        return closedCalibrationValueList_;
    }

    std::array<double, kChannelCount> emptyValueList{};
    emptyValueList.fill(0.0);
    return emptyValueList;
}

void HandAngleAlgorithm::applyStageCalibrationValue(
    CalibrationStage stage,
    const std::array<double, kChannelCount>& averageValueList) {
    if (stage == CalibrationStage::Closed) {
        closedCalibrationValueList_ = averageValueList;
        hasClosedCalibration_ = true;
        return;
    }

    if (!hasClosedCalibration_) {
        fistCalibrationValueList_ = averageValueList;
        hasFistCalibration_ = true;
        return;
    }

    auto targetValueList = buildStageCalibrationTemplate(CalibrationStage::Fist);
    if (!hasFistCalibration_) {
        targetValueList = closedCalibrationValueList_;
    }
    for (int channelIndex : kFlexChannelIndexList) {
        targetValueList[toChannelArrayIndex(channelIndex)] = averageValueList[toChannelArrayIndex(channelIndex)];
    }
    fistCalibrationValueList_ = targetValueList;
    hasFistCalibration_ = true;
}

bool HandAngleAlgorithm::finishCalibration() {
    if (!samplingState_.isActive || samplingState_.frameCount == 0) {
        resetSamplingState();
        return false;
    }

    const auto averageValueList = buildAverageCalibrationFrame();
    applyStageCalibrationValue(samplingState_.stage, averageValueList);
    rebuildChannelCompensation();
    resetSamplingState();
    return true;
}

bool HandAngleAlgorithm::isReady() const {
    return hasClosedCalibration_ && hasFistCalibration_;
}

void HandAngleAlgorithm::rebuildChannelCompensation() {
    resetCompensationState();

    if (!kEnableFlexCompensation || !hasClosedCalibration_ || !hasFistCalibration_) {
        return;
    }

    std::vector<double> spanValueList;
    spanValueList.reserve(kFlexChannelIndexList.size());
    for (int channelIndex : kFlexChannelIndexList) {
        spanValueList.push_back(std::abs(
            fistCalibrationValueList_[toChannelArrayIndex(channelIndex)] -
            closedCalibrationValueList_[toChannelArrayIndex(channelIndex)]));
    }

    const double referenceSpanValue = calculateMedian(spanValueList);
    if (referenceSpanValue < 1.0) {
        return;
    }

    const double weakThresholdValue = referenceSpanValue * kFlexWeakThresholdRatio;
    const double minSpanValue = referenceSpanValue * kFlexMinSpanRatio;

    for (int channelIndex : kFlexChannelIndexList) {
        const double channelSpanValue = std::abs(
            fistCalibrationValueList_[toChannelArrayIndex(channelIndex)] -
            closedCalibrationValueList_[toChannelArrayIndex(channelIndex)]);
        if (channelSpanValue >= weakThresholdValue) {
            continue;
        }

        CompensationState compensationState;
        compensationState.isActive = true;
        compensationState.boostFactorValue = clampValue(
            referenceSpanValue / std::max(channelSpanValue, minSpanValue),
            1.0,
            kFlexMaxBoostFactor);
        compensationState.gammaValue = 1.0 / compensationState.boostFactorValue;
        flexCompensationStateByChannel_[toChannelArrayIndex(channelIndex)] = compensationState;
    }
}

std::array<double, kChannelCount> HandAngleAlgorithm::getMeanFilteredFrameValueList(const int16_t adValues[kChannelCount]) {
    std::array<double, kChannelCount> currentFrameValueList{};
    for (std::size_t channelIndex = 0; channelIndex < kChannelCount; ++channelIndex) {
        currentFrameValueList[channelIndex] = static_cast<double>(adValues[channelIndex]);
    }

    rawFilterState_.frameWindowList.push_back(currentFrameValueList);
    for (std::size_t channelIndex = 0; channelIndex < kChannelCount; ++channelIndex) {
        rawFilterState_.sumValueList[channelIndex] += currentFrameValueList[channelIndex];
    }

    if (rawFilterState_.frameWindowList.size() > kMeanFilterWindowFrameCount) {
        const auto& expiredFrameValueList = rawFilterState_.frameWindowList.front();
        for (std::size_t channelIndex = 0; channelIndex < kChannelCount; ++channelIndex) {
            rawFilterState_.sumValueList[channelIndex] -= expiredFrameValueList[channelIndex];
        }
        rawFilterState_.frameWindowList.pop_front();
    }

    const double frameCountValue = static_cast<double>(rawFilterState_.frameWindowList.size());
    if (frameCountValue <= 0.0) {
        rawFilterState_.filteredValueList = currentFrameValueList;
        return rawFilterState_.filteredValueList;
    }

    // 串口输入层不做平滑，算法内部保留“前 14 帧 + 当前帧”的 15 帧实时均值窗口。
    for (std::size_t channelIndex = 0; channelIndex < kChannelCount; ++channelIndex) {
        rawFilterState_.filteredValueList[channelIndex] = rawFilterState_.sumValueList[channelIndex] / frameCountValue;
    }
    return rawFilterState_.filteredValueList;
}

std::array<double, kChannelCount> HandAngleAlgorithm::filterFrameValueList(const int16_t adValues[kChannelCount]) {
    return getMeanFilteredFrameValueList(adValues);
}

double HandAngleAlgorithm::stabilizeRatio(RatioStableState& stableState, double ratioValue, double deadbandRatio) {
    const double clampedRatioValue = clampValue(ratioValue, 0.0, 1.0);
    if (!stableState.isInitialized) {
        stableState.isInitialized = true;
        stableState.stableRatio = clampedRatioValue;
        return stableState.stableRatio;
    }

    const double previousStableRatio = stableState.stableRatio;
    const double deltaRatioValue = std::abs(clampedRatioValue - previousStableRatio);
    if (deltaRatioValue <= deadbandRatio) {
        return previousStableRatio;
    }

    stableState.stableRatio = clampedRatioValue;
    return stableState.stableRatio;
}

double HandAngleAlgorithm::applyCompensationRatio(
    double ratioValue,
    const CompensationState& compensationState,
    double curveBlendRatio) const {
    if (!compensationState.isActive) {
        return clampValue(ratioValue, 0.0, 1.0);
    }
    const double rawRatioValue = clampValue(ratioValue, 0.0, 1.0);
    const double enhancedRatioValue = std::pow(rawRatioValue, compensationState.gammaValue);
    const double mixedRatioValue = rawRatioValue + (enhancedRatioValue - rawRatioValue) * curveBlendRatio;
    return clampValue(mixedRatioValue, 0.0, 1.0);
}

double HandAngleAlgorithm::getFlexRatio(int channelIndex, double currentValue) {
    if (!hasClosedCalibration_ || !hasFistCalibration_) {
        return 0.0;
    }

    double ratioValue = calculateChannelRatio(
        currentValue,
        closedCalibrationValueList_[toChannelArrayIndex(channelIndex)],
        fistCalibrationValueList_[toChannelArrayIndex(channelIndex)]);

    if (kEnableFlexCompensation) {
        ratioValue = applyCompensationRatio(
            ratioValue,
            flexCompensationStateByChannel_[toChannelArrayIndex(channelIndex)],
            kFlexCurveBlendRatio);
    }
    return stabilizeRatio(
        flexStableStateByChannel_[toChannelArrayIndex(channelIndex)],
        ratioValue,
        kFlexDeadbandRatio);
}

void HandAngleAlgorithm::buildOutputValue(const std::array<double, kChannelCount>& channelValueList, HandAngleOutput& outputValue) {
    const auto fillFingerOutput = [](
        float targetValueList[3],
        double rootFlexRatio,
        double jointFlexRatio1,
        double jointFlexRatio2,
        const FingerFlexAngleModel& angleModel) {
        targetValueList[0] = convertAngleToFloat(rootFlexRatio * angleModel.rootHoldDeltaAngle);
        targetValueList[1] = convertAngleToFloat(jointFlexRatio1 * angleModel.jointHoldDeltaAngle1);
        targetValueList[2] = convertAngleToFloat(jointFlexRatio2 * angleModel.jointHoldDeltaAngle2);
    };

    const FingerChannelModel& indexChannelModel = kFingerChannelModelByIndex[static_cast<std::size_t>(FourFingerIndex::Index)];
    const FingerChannelModel& middleChannelModel = kFingerChannelModelByIndex[static_cast<std::size_t>(FourFingerIndex::Middle)];
    const FingerChannelModel& ringChannelModel = kFingerChannelModelByIndex[static_cast<std::size_t>(FourFingerIndex::Ring)];
    const FingerChannelModel& littleChannelModel = kFingerChannelModelByIndex[static_cast<std::size_t>(FourFingerIndex::Little)];

    fillFingerOutput(
        outputValue.index_finger,
        getFlexRatio(indexChannelModel.rootFlexChannel, channelValueList[toChannelArrayIndex(indexChannelModel.rootFlexChannel)]),
        getFlexRatio(indexChannelModel.jointFlexChannel1, channelValueList[toChannelArrayIndex(indexChannelModel.jointFlexChannel1)]),
        getFlexRatio(indexChannelModel.jointFlexChannel2, channelValueList[toChannelArrayIndex(indexChannelModel.jointFlexChannel2)]),
        kFingerFlexAngleModelByIndex[static_cast<std::size_t>(FourFingerIndex::Index)]);

    fillFingerOutput(
        outputValue.middle_finger,
        getFlexRatio(middleChannelModel.rootFlexChannel, channelValueList[toChannelArrayIndex(middleChannelModel.rootFlexChannel)]),
        getFlexRatio(middleChannelModel.jointFlexChannel1, channelValueList[toChannelArrayIndex(middleChannelModel.jointFlexChannel1)]),
        getFlexRatio(middleChannelModel.jointFlexChannel2, channelValueList[toChannelArrayIndex(middleChannelModel.jointFlexChannel2)]),
        kFingerFlexAngleModelByIndex[static_cast<std::size_t>(FourFingerIndex::Middle)]);

    fillFingerOutput(
        outputValue.ring_finger,
        getFlexRatio(ringChannelModel.rootFlexChannel, channelValueList[toChannelArrayIndex(ringChannelModel.rootFlexChannel)]),
        getFlexRatio(ringChannelModel.jointFlexChannel1, channelValueList[toChannelArrayIndex(ringChannelModel.jointFlexChannel1)]),
        getFlexRatio(ringChannelModel.jointFlexChannel2, channelValueList[toChannelArrayIndex(ringChannelModel.jointFlexChannel2)]),
        kFingerFlexAngleModelByIndex[static_cast<std::size_t>(FourFingerIndex::Ring)]);

    fillFingerOutput(
        outputValue.little_finger,
        getFlexRatio(littleChannelModel.rootFlexChannel, channelValueList[toChannelArrayIndex(littleChannelModel.rootFlexChannel)]),
        getFlexRatio(littleChannelModel.jointFlexChannel1, channelValueList[toChannelArrayIndex(littleChannelModel.jointFlexChannel1)]),
        getFlexRatio(littleChannelModel.jointFlexChannel2, channelValueList[toChannelArrayIndex(littleChannelModel.jointFlexChannel2)]),
        kFingerFlexAngleModelByIndex[static_cast<std::size_t>(FourFingerIndex::Little)]);

    outputValue.thumb[0] = convertAngleToFloat(getFlexRatio(18, channelValueList[toChannelArrayIndex(18)]) * kThumbFlexAngleModel.mcpHoldDeltaAngle);
    outputValue.thumb[1] = convertAngleToFloat(getFlexRatio(17, channelValueList[toChannelArrayIndex(17)]) * kThumbFlexAngleModel.ipHoldDeltaAngle);
}

bool HandAngleAlgorithm::processFrame(const int16_t adValues[kChannelCount], HandAngleOutput& outputValue) {
    if (adValues == nullptr || !isReady()) {
        clearOutputValueList(outputValue.little_finger, 3);
        clearOutputValueList(outputValue.ring_finger, 3);
        clearOutputValueList(outputValue.middle_finger, 3);
        clearOutputValueList(outputValue.index_finger, 3);
        clearOutputValueList(outputValue.thumb, 2);
        return false;
    }

    rawFilterState_.filteredValueList = filterFrameValueList(adValues);
    buildOutputValue(rawFilterState_.filteredValueList, outputValue);
    return true;
}

}  // namespace handdemo
