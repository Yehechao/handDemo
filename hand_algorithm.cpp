#include "hand_algorithm.h"

#include <cmath>
#include <limits>

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

int toChannelArrayIndex(int channelIndex) {
    return channelIndex - 1;
}

double calculateChannelRatio(double currentValue, double startValue, double endValue) {
    // 对齐 Python calculateChannelRatio：
    // Closed 是 0 比例基线，Fist/Spread 是 1 比例上限。
    if (endValue <= startValue + 1.0) {
        return 0.0;
    }
    if (currentValue <= startValue) {
        return 0.0;
    }
    if (currentValue >= endValue) {
        return 1.0;
    }
    const double ratioValue = (currentValue - startValue) / (endValue - startValue);
    return clampValue(ratioValue, 0.0, 1.0);
}

float convertAngleToFloat(double degreeValue) {
    const double roundedValue = std::round(degreeValue * 10.0) / 10.0;
    const double minValue = -static_cast<double>(std::numeric_limits<float>::max());
    const double maxValue = static_cast<double>(std::numeric_limits<float>::max());
    return static_cast<float>(clampValue(roundedValue, minValue, maxValue));
}

bool isFlexChannelIndex(int channelIndex) {
    for (int flexChannelIndex : kFlexChannelIndexList) {
        if (flexChannelIndex == channelIndex) {
            return true;
        }
    }
    return false;
}

double getSmoothstepRatio(double startRatio, double endRatio, double value) {
    if (value <= startRatio) {
        return 0.0;
    }
    if (value >= endRatio) {
        return 1.0;
    }
    const double t = (value - startRatio) / (endRatio - startRatio);
    return t * t * (3.0 - 2.0 * t);
}

double buildEffectiveSpreadAngle(double spreadRatio, double rootFlexRatio, const SpreadPairConfig& spreadConfig) {
    const double rawSpreadAngle = spreadRatio * spreadConfig.openRootAngle * spreadConfig.angleScale;
    const double suppressRatio = getSmoothstepRatio(
        kFoldSpreadSuppressStartRatio,
        kFoldSpreadSuppressEndRatio,
        rootFlexRatio);
    return rawSpreadAngle * (1.0 - suppressRatio);
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
    for (auto& stableState : spreadStableStateByChannel_) {
        stableState = {};
    }
    thumbGateStableState_ = {};
    thumbGateFilterDeque_.clear();
}

void HandAngleAlgorithm::reset() {
    hasClosedCalibration_ = false;
    hasFistCalibration_ = false;
    hasSpreadCalibration_ = false;
    closedCalibrationValueList_.fill(0.0);
    fistCalibrationValueList_.fill(0.0);
    spreadCalibrationValueList_.fill(0.0);
    resetSamplingState();
    resetFilterState();
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
    if (stage == CalibrationStage::Spread && hasSpreadCalibration_) {
        return spreadCalibrationValueList_;
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

    if (stage == CalibrationStage::Fist) {
        auto targetValueList = buildStageCalibrationTemplate(CalibrationStage::Fist);
        if (!hasFistCalibration_) {
            targetValueList = closedCalibrationValueList_;
        }
        for (int channelIndex : kFlexChannelIndexList) {
            targetValueList[toChannelArrayIndex(channelIndex)] = averageValueList[toChannelArrayIndex(channelIndex)];
        }
        targetValueList[toChannelArrayIndex(16)] = averageValueList[toChannelArrayIndex(16)];
        if (!isFlexChannelIndex(kThumbInwardGateChannel) && kThumbInwardGateChannel != 16) {
            targetValueList[toChannelArrayIndex(kThumbInwardGateChannel)] =
                averageValueList[toChannelArrayIndex(kThumbInwardGateChannel)];
        }
        fistCalibrationValueList_ = targetValueList;
        hasFistCalibration_ = true;
        return;
    }

    // Spread: 以 fist 模板为基底，仅覆盖展开通道
    auto targetValueList = buildStageCalibrationTemplate(CalibrationStage::Fist);
    for (int channelIndex : kSpreadChannelIndexList) {
        targetValueList[toChannelArrayIndex(channelIndex)] = averageValueList[toChannelArrayIndex(channelIndex)];
    }
    spreadCalibrationValueList_ = targetValueList;
    hasSpreadCalibration_ = true;
}

bool HandAngleAlgorithm::finishCalibration() {
    if (!samplingState_.isActive || samplingState_.frameCount == 0) {
        resetSamplingState();
        return false;
    }

    const auto averageValueList = buildAverageCalibrationFrame();
    applyStageCalibrationValue(samplingState_.stage, averageValueList);
    resetSamplingState();
    return true;
}

bool HandAngleAlgorithm::isReady() const {
    return hasClosedCalibration_ && hasFistCalibration_ && hasSpreadCalibration_;
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

double HandAngleAlgorithm::getFlexRatio(int channelIndex, double currentValue) {
    if (!hasClosedCalibration_ || !hasFistCalibration_) {
        return 0.0;
    }

    double ratioValue = calculateChannelRatio(
        currentValue,
        closedCalibrationValueList_[toChannelArrayIndex(channelIndex)],
        fistCalibrationValueList_[toChannelArrayIndex(channelIndex)]);

    return stabilizeRatio(
        flexStableStateByChannel_[toChannelArrayIndex(channelIndex)],
        ratioValue,
        kFlexDeadbandRatio);
}

double HandAngleAlgorithm::getSpreadRatio(int channelIndex, double currentValue) {
    if (!hasClosedCalibration_ || !hasSpreadCalibration_) {
        return 0.0;
    }
    double ratioValue = calculateChannelRatio(
        currentValue,
        closedCalibrationValueList_[toChannelArrayIndex(channelIndex)],
        spreadCalibrationValueList_[toChannelArrayIndex(channelIndex)]);
    return stabilizeRatio(
        spreadStableStateByChannel_[toChannelArrayIndex(channelIndex)],
        ratioValue,
        kSpreadDeadbandRatio);
}

double HandAngleAlgorithm::getThumbGateRatio(const std::array<double, kChannelCount>& channelValueList) {
    if (!hasClosedCalibration_ || !hasFistCalibration_) {
        return 0.0;
    }
    // 第一层：AD 值 → 原始门控比例 [0, 1]
    // 门控通道由 kThumbInwardGateChannel 配置，可选 CH18 或 CH19。
    double rawRatio = calculateChannelRatio(
        channelValueList[toChannelArrayIndex(kThumbInwardGateChannel)],
        closedCalibrationValueList_[toChannelArrayIndex(kThumbInwardGateChannel)],
        fistCalibrationValueList_[toChannelArrayIndex(kThumbInwardGateChannel)]);

    // 第二层：对门控比例做独立移动平均滤波（对齐 Python getFilteredDerivedSignalValue）
    thumbGateFilterDeque_.push_back(rawRatio);
    if (thumbGateFilterDeque_.size() > kThumbGateFilterWindowSize) {
        thumbGateFilterDeque_.pop_front();
    }
    double filteredRatio = 0.0;
    for (double value : thumbGateFilterDeque_) {
        filteredRatio += value;
    }
    filteredRatio /= static_cast<double>(thumbGateFilterDeque_.size());

    // 第三层：死区稳定（对齐 Python stabilizeRatio）
    filteredRatio = stabilizeRatio(thumbGateStableState_, filteredRatio, kThumbGateDeadbandRatio);

    // 第四层：smoothstep 重新映射 [startRatio, endRatio] → [0, 1]
    return getSmoothstepRatio(kThumbFlexGateStartRatio, kThumbFlexGateEndRatio, filteredRatio);
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

    const double indexRootFlexRatio = getFlexRatio(
        indexChannelModel.rootFlexChannel,
        channelValueList[toChannelArrayIndex(indexChannelModel.rootFlexChannel)]);
    const double indexJointFlexRatio1 = getFlexRatio(
        indexChannelModel.jointFlexChannel1,
        channelValueList[toChannelArrayIndex(indexChannelModel.jointFlexChannel1)]);
    const double indexJointFlexRatio2 = getFlexRatio(
        indexChannelModel.jointFlexChannel2,
        channelValueList[toChannelArrayIndex(indexChannelModel.jointFlexChannel2)]);
    const double middleRootFlexRatio = getFlexRatio(
        middleChannelModel.rootFlexChannel,
        channelValueList[toChannelArrayIndex(middleChannelModel.rootFlexChannel)]);
    const double middleJointFlexRatio1 = getFlexRatio(
        middleChannelModel.jointFlexChannel1,
        channelValueList[toChannelArrayIndex(middleChannelModel.jointFlexChannel1)]);
    const double middleJointFlexRatio2 = getFlexRatio(
        middleChannelModel.jointFlexChannel2,
        channelValueList[toChannelArrayIndex(middleChannelModel.jointFlexChannel2)]);
    const double ringRootFlexRatio = getFlexRatio(
        ringChannelModel.rootFlexChannel,
        channelValueList[toChannelArrayIndex(ringChannelModel.rootFlexChannel)]);
    const double ringJointFlexRatio1 = getFlexRatio(
        ringChannelModel.jointFlexChannel1,
        channelValueList[toChannelArrayIndex(ringChannelModel.jointFlexChannel1)]);
    const double ringJointFlexRatio2 = getFlexRatio(
        ringChannelModel.jointFlexChannel2,
        channelValueList[toChannelArrayIndex(ringChannelModel.jointFlexChannel2)]);
    const double littleRootFlexRatio = getFlexRatio(
        littleChannelModel.rootFlexChannel,
        channelValueList[toChannelArrayIndex(littleChannelModel.rootFlexChannel)]);
    const double littleJointFlexRatio1 = getFlexRatio(
        littleChannelModel.jointFlexChannel1,
        channelValueList[toChannelArrayIndex(littleChannelModel.jointFlexChannel1)]);
    const double littleJointFlexRatio2 = getFlexRatio(
        littleChannelModel.jointFlexChannel2,
        channelValueList[toChannelArrayIndex(littleChannelModel.jointFlexChannel2)]);

    fillFingerOutput(
        outputValue.index_finger,
        indexRootFlexRatio,
        indexJointFlexRatio1,
        indexJointFlexRatio2,
        kFingerFlexAngleModelByIndex[static_cast<std::size_t>(FourFingerIndex::Index)]);

    fillFingerOutput(
        outputValue.middle_finger,
        middleRootFlexRatio,
        middleJointFlexRatio1,
        middleJointFlexRatio2,
        kFingerFlexAngleModelByIndex[static_cast<std::size_t>(FourFingerIndex::Middle)]);

    fillFingerOutput(
        outputValue.ring_finger,
        ringRootFlexRatio,
        ringJointFlexRatio1,
        ringJointFlexRatio2,
        kFingerFlexAngleModelByIndex[static_cast<std::size_t>(FourFingerIndex::Ring)]);

    fillFingerOutput(
        outputValue.little_finger,
        littleRootFlexRatio,
        littleJointFlexRatio1,
        littleJointFlexRatio2,
        kFingerFlexAngleModelByIndex[static_cast<std::size_t>(FourFingerIndex::Little)]);

    outputValue.thumb[0] = convertAngleToFloat(getFlexRatio(18, channelValueList[toChannelArrayIndex(18)]) * kThumbFlexAngleModel.mcpHoldDeltaAngle);
    outputValue.thumb[1] = convertAngleToFloat(getFlexRatio(17, channelValueList[toChannelArrayIndex(17)]) * kThumbFlexAngleModel.ipHoldDeltaAngle);

    // 四指展开角：先按 AD 计算原始展开角，再按对应外侧手指根节弯曲做算法层收束。
    const SpreadPairConfig& ringPinkySpreadConfig = kSpreadPairConfigList[0];
    const SpreadPairConfig& middleRingSpreadConfig = kSpreadPairConfigList[1];
    const SpreadPairConfig& indexMiddleSpreadConfig = kSpreadPairConfigList[2];
    outputValue.index_finger[3] = convertAngleToFloat(
        buildEffectiveSpreadAngle(
            getSpreadRatio(indexMiddleSpreadConfig.channelIndex, channelValueList[toChannelArrayIndex(indexMiddleSpreadConfig.channelIndex)]),
            indexRootFlexRatio,
            indexMiddleSpreadConfig));
    outputValue.ring_finger[3] = convertAngleToFloat(
        buildEffectiveSpreadAngle(
            getSpreadRatio(middleRingSpreadConfig.channelIndex, channelValueList[toChannelArrayIndex(middleRingSpreadConfig.channelIndex)]),
            ringRootFlexRatio,
            middleRingSpreadConfig));
    outputValue.little_finger[3] = convertAngleToFloat(
        buildEffectiveSpreadAngle(
            getSpreadRatio(ringPinkySpreadConfig.channelIndex, channelValueList[toChannelArrayIndex(ringPinkySpreadConfig.channelIndex)]),
            littleRootFlexRatio,
            ringPinkySpreadConfig));

    // 拇指展开（有符号，正=外展，负=内收）
    double spreadRatio16 = getSpreadRatio(16, channelValueList[toChannelArrayIndex(16)]);
    double gateRatio = getThumbGateRatio(channelValueList);
    double amplitudeRatio = getFlexRatio(16, channelValueList[toChannelArrayIndex(16)]);
    double outwardRatio = spreadRatio16 * (1.0 - gateRatio);
    double inwardRatio = amplitudeRatio * gateRatio;
    outputValue.thumb[2] = convertAngleToFloat(
        kThumbOpenPalmAngle * outwardRatio - kThumbInwardPalmAngle * inwardRatio);
}

bool HandAngleAlgorithm::processFrame(const int16_t adValues[kChannelCount], HandAngleOutput& outputValue) {
    if (adValues == nullptr || !isReady()) {
        clearOutputValueList(outputValue.little_finger, 4);
        clearOutputValueList(outputValue.ring_finger, 4);
        clearOutputValueList(outputValue.middle_finger, 3);
        clearOutputValueList(outputValue.index_finger, 4);
        clearOutputValueList(outputValue.thumb, 3);
        return false;
    }

    rawFilterState_.filteredValueList = filterFrameValueList(adValues);
    buildOutputValue(rawFilterState_.filteredValueList, outputValue);
    return true;
}

}  // namespace handdemo
