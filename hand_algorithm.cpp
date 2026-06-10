// Copyright (c) 2026 Matrix 墨现科技. All rights reserved.

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

int toChannelArrayIndex(int channelIndex) {
    return channelIndex - 1;
}

double calcChRatio(double currentValue, double startValue, double endValue) {
    // Closed 为 0 比例基线，Fist/Spread 为 1 比例上限。
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

bool isValidThumbInwardGateChannel(int channelIndex) {
    return channelIndex == 18 || channelIndex == 19;
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

double buildEffectiveSpreadAngle(double spreadRatio, double adjacentRootFlexRatio, const SpreadPairConfig& spreadConfig) {
    // 展开角 = openRootAngle * ratio * suppress。
    const double rawSpreadAngle = spreadRatio * spreadConfig.openRootAngle;
    const double suppressRatio = getSmoothstepRatio(
        kFoldSpreadSuppressStartRatio,
        kFoldSpreadSuppressEndRatio,
        adjacentRootFlexRatio);
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

bool HandAngleAlgorithm::setRuntimeConfig(const RuntimeConfig& runtimeConfig) {
    if (runtimeConfig.meanFilterWindowFrameCount == 0 ||
        runtimeConfig.thumbGateFilterWindowSize == 0 ||
        !isValidThumbInwardGateChannel(runtimeConfig.thumbInwardGateChannel) ||
        !std::isfinite(runtimeConfig.thumbGateDeadbandRatio) ||
        !std::isfinite(runtimeConfig.spreadDeadbandRatio) ||
        !std::isfinite(runtimeConfig.crosstalkMaxAbsIntercept) ||
        runtimeConfig.thumbGateDeadbandRatio < 0.0 ||
        runtimeConfig.thumbGateDeadbandRatio > 1.0 ||
        runtimeConfig.spreadDeadbandRatio < 0.0 ||
        runtimeConfig.spreadDeadbandRatio > 1.0 ||
        runtimeConfig.crosstalkMaxAbsIntercept < 0.0) {
        return false;
    }

    runtimeConfig_ = runtimeConfig;
    reset();
    return true;
}

void HandAngleAlgorithm::resetSamplingState() {
    samplingState_.isActive = false;
    samplingState_.stage = CalibrationStage::Closed;
    samplingState_.sumValueList.fill(0.0);
    samplingState_.frameCount = 0;
    samplingState_.frameValueList.clear();
}

void HandAngleAlgorithm::resetFilterState() {
    rawFilter_.filteredValueList.fill(0.0);
    rawFilter_.sumValueList.fill(0.0);
    rawFilter_.frameWindowList.clear();

    for (auto& stableState : flexStable_) {
        stableState = {};
    }
    for (auto& stableState : spreadStable_) {
        stableState = {};
    }
    thumbGateStableState_ = {};
    thumbGateFilterDeque_.clear();
    latestRawAd_.fill(0);
    thumbInwardAmplitudeStable_ = {};
}

void HandAngleAlgorithm::reset() {
    hasClosed_ = false;
    hasFist_ = false;
    hasOpen_ = false;
    hasXtalk_ = false;
    xtalkValidTargetChannelCount_ = 0;
    closedCalib_.fill(0.0);
    fistCalib_.fill(0.0);
    openCalib_.fill(0.0);
    xtalkCoef_.fill({});
    xtalkBase_.fill(0.0);
    xtalkUnstableChList_.clear();
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

    // Crosstalk 阶段：保存完整帧序列供最小二乘拟合
    if (samplingState_.stage == CalibrationStage::Crosstalk) {
        std::array<double, kChannelCount> frameCopy{};
        for (std::size_t i = 0; i < kChannelCount; ++i) {
            frameCopy[i] = static_cast<double>(adValues[i]);
        }
        samplingState_.frameValueList.push_back(frameCopy);
    }

    for (std::size_t channelIndex = 0; channelIndex < kChannelCount; ++channelIndex) {
        samplingState_.sumValueList[channelIndex] += static_cast<double>(adValues[channelIndex]);
    }
    ++samplingState_.frameCount;
    return true;
}

std::array<double, kChannelCount> HandAngleAlgorithm::avgCalibFrm() const {
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

std::array<double, kChannelCount> HandAngleAlgorithm::stageCalibTpl(CalibrationStage stage) const {
    if (stage == CalibrationStage::Fist && hasFist_) {
        return fistCalib_;
    }
    if (stage == CalibrationStage::Spread && hasOpen_) {
        return openCalib_;
    }
    if (hasClosed_) {
        return closedCalib_;
    }

    std::array<double, kChannelCount> emptyValueList{};
    emptyValueList.fill(0.0);
    return emptyValueList;
}

void HandAngleAlgorithm::setStageCalib(
    CalibrationStage stage,
    const std::array<double, kChannelCount>& averageValueList) {
    if (stage == CalibrationStage::Closed) {
        closedCalib_ = averageValueList;
        hasClosed_ = true;
        return;
    }

    // 阶段前置守卫：防止任意代码路径绕过 finishCalibration 的顺序检查直接写入状态
    if (stage == CalibrationStage::Fist && !hasClosed_) {
        return;
    }
    if (stage == CalibrationStage::Spread && (!hasClosed_ || !hasFist_)) {
        return;
    }

    if (stage == CalibrationStage::Fist) {
        auto targetValueList = stageCalibTpl(CalibrationStage::Fist);
        if (!hasFist_) {
            targetValueList = closedCalib_;
        }
        for (int channelIndex : kFlexChannelIndexList) {
            targetValueList[toChannelArrayIndex(channelIndex)] = averageValueList[toChannelArrayIndex(channelIndex)];
        }
        targetValueList[toChannelArrayIndex(16)] = averageValueList[toChannelArrayIndex(16)];
        if (!isFlexChannelIndex(runtimeConfig_.thumbInwardGateChannel) && runtimeConfig_.thumbInwardGateChannel != 16) {
            targetValueList[toChannelArrayIndex(runtimeConfig_.thumbInwardGateChannel)] =
                averageValueList[toChannelArrayIndex(runtimeConfig_.thumbInwardGateChannel)];
        }
        fistCalib_ = targetValueList;
        hasFist_ = true;
        return;
    }

    // Spread: 以 fist 模板为基底，仅覆盖展开通道
    auto targetValueList = stageCalibTpl(CalibrationStage::Fist);
    for (int channelIndex : kSpreadChannelIndexList) {
        targetValueList[toChannelArrayIndex(channelIndex)] = averageValueList[toChannelArrayIndex(channelIndex)];
    }
    openCalib_ = targetValueList;
    hasOpen_ = true;
}

bool HandAngleAlgorithm::finishCalibration() {
    if (!samplingState_.isActive || samplingState_.frameCount == 0) {
        resetSamplingState();
        return false;
    }

    if (samplingState_.stage == CalibrationStage::Crosstalk) {
        // Crosstalk 必须在三步校准完成后才能进行
        if (!isReady()) {
            resetSamplingState();
            return false;
        }
        xtalkValidTargetChannelCount_ = fitXtalkCoefs(samplingState_.frameValueList);
        if (xtalkValidTargetChannelCount_ == 0) {
            hasXtalk_ = false;
            resetSamplingState();
            return false;
        }
        hasXtalk_ = true;
        // 保存基线（第一帧）供实时补偿使用
        if (!samplingState_.frameValueList.empty()) {
            xtalkBase_ = samplingState_.frameValueList.front();
        }
        resetSamplingState();
        return true;
    }

    // 非 Crosstalk 阶段的前置检查
    if (samplingState_.stage == CalibrationStage::Fist && !hasClosed_) {
        resetSamplingState();
        return false;
    }
    if (samplingState_.stage == CalibrationStage::Spread && (!hasClosed_ || !hasFist_)) {
        resetSamplingState();
        return false;
    }

    const auto averageValueList = avgCalibFrm();
    setStageCalib(samplingState_.stage, averageValueList);
    resetSamplingState();
    return true;
}

bool HandAngleAlgorithm::isReady() const {
    return hasClosed_ && hasFist_ && hasOpen_;
}

std::array<double, kChannelCount> HandAngleAlgorithm::meanFilteredFrm(const int16_t adValues[kChannelCount]) {
    std::array<double, kChannelCount> currentFrameValueList{};
    for (std::size_t channelIndex = 0; channelIndex < kChannelCount; ++channelIndex) {
        currentFrameValueList[channelIndex] = static_cast<double>(adValues[channelIndex]);
    }

    rawFilter_.frameWindowList.push_back(currentFrameValueList);
    for (std::size_t channelIndex = 0; channelIndex < kChannelCount; ++channelIndex) {
        rawFilter_.sumValueList[channelIndex] += currentFrameValueList[channelIndex];
    }

    if (rawFilter_.frameWindowList.size() > runtimeConfig_.meanFilterWindowFrameCount) {
        const auto& expiredFrameValueList = rawFilter_.frameWindowList.front();
        for (std::size_t channelIndex = 0; channelIndex < kChannelCount; ++channelIndex) {
            rawFilter_.sumValueList[channelIndex] -= expiredFrameValueList[channelIndex];
        }
        rawFilter_.frameWindowList.pop_front();
    }

    const double frameCountValue = static_cast<double>(rawFilter_.frameWindowList.size());
    if (frameCountValue <= 0.0) {
        rawFilter_.filteredValueList = currentFrameValueList;
        return rawFilter_.filteredValueList;
    }

    // 串口输入层不做平滑，算法内部保留可配置帧数的实时均值窗口。
    for (std::size_t channelIndex = 0; channelIndex < kChannelCount; ++channelIndex) {
        rawFilter_.filteredValueList[channelIndex] = rawFilter_.sumValueList[channelIndex] / frameCountValue;
    }
    return rawFilter_.filteredValueList;
}

std::array<double, kChannelCount> HandAngleAlgorithm::filterFrm(const int16_t adValues[kChannelCount]) {
    return meanFilteredFrm(adValues);
}

double HandAngleAlgorithm::stabilizeRatio(RatioState& stableState, double ratioValue, double deadbandRatio) {
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

bool HandAngleAlgorithm::isChannelValidForStage(int channelIndex, CalibrationStage stage) const {
    // stageDelta <= 0 或 stageDelta < kInvalidChannelSpanThresholdValue 时通道无效。
    if (!hasClosed_) {
        return true;  // 无闭合基准时不做无效判定
    }

    const int arrayIndex = toChannelArrayIndex(channelIndex);
    const double closedVal = closedCalib_[arrayIndex];

    double stageVal = 0.0;
    if (stage == CalibrationStage::Fist && hasFist_) {
        stageVal = fistCalib_[arrayIndex];
    } else if (stage == CalibrationStage::Spread && hasOpen_) {
        stageVal = openCalib_[arrayIndex];
    } else {
        return true;
    }

    const double delta = stageVal - closedVal;
    if (delta <= 0.0) {
        return false;
    }
    if (delta < kInvalidChannelSpanThresholdValue) {
        return false;
    }
    return true;
}

double HandAngleAlgorithm::getFlexRatio(int channelIndex, double currentValue) {
    if (!isChannelValidForStage(channelIndex, CalibrationStage::Fist)) {
        return 0.0;
    }

    if (!hasClosed_ || !hasFist_) {
        return 0.0;
    }

    double ratioValue = calcChRatio(
        currentValue,
        closedCalib_[toChannelArrayIndex(channelIndex)],
        fistCalib_[toChannelArrayIndex(channelIndex)]);

    return stabilizeRatio(
        flexStable_[toChannelArrayIndex(channelIndex)],
        ratioValue,
        kFlexDeadbandRatio);
}

double HandAngleAlgorithm::getSpreadRatio(int channelIndex, double currentValue) {
    if (!isChannelValidForStage(channelIndex, CalibrationStage::Spread)) {
        return 0.0;
    }

    if (!hasClosed_ || !hasOpen_) {
        return 0.0;
    }
    double ratioValue = calcChRatio(
        currentValue,
        closedCalib_[toChannelArrayIndex(channelIndex)],
        openCalib_[toChannelArrayIndex(channelIndex)]);
    return stabilizeRatio(
        spreadStable_[toChannelArrayIndex(channelIndex)],
        ratioValue,
        runtimeConfig_.spreadDeadbandRatio);
}

double HandAngleAlgorithm::getThumbGateRatio(const std::array<double, kChannelCount>& channelValueList) {
    if (!hasClosed_ || !hasFist_) {
        return 0.0;
    }
    // 先判定门控通道在 fist 阶段是否有效
    if (!isChannelValidForStage(runtimeConfig_.thumbInwardGateChannel, CalibrationStage::Fist)) {
        return 0.0;
    }
    // 第一层：AD 值 → 原始门控比例 [0, 1]
    // 门控通道可通过运行时配置覆盖，默认来自 config.h。
    double rawRatio = calcChRatio(
        channelValueList[toChannelArrayIndex(runtimeConfig_.thumbInwardGateChannel)],
        closedCalib_[toChannelArrayIndex(runtimeConfig_.thumbInwardGateChannel)],
        fistCalib_[toChannelArrayIndex(runtimeConfig_.thumbInwardGateChannel)]);

    // 第二层：对门控比例做独立移动平均滤波
    thumbGateFilterDeque_.push_back(rawRatio);
    if (thumbGateFilterDeque_.size() > runtimeConfig_.thumbGateFilterWindowSize) {
        thumbGateFilterDeque_.pop_front();
    }
    double filteredRatio = 0.0;
    for (double value : thumbGateFilterDeque_) {
        filteredRatio += value;
    }
    filteredRatio /= static_cast<double>(thumbGateFilterDeque_.size());

    // 第三层：死区稳定
    filteredRatio = stabilizeRatio(thumbGateStableState_, filteredRatio, runtimeConfig_.thumbGateDeadbandRatio);

    // 第四层：smoothstep 重新映射 [startRatio, endRatio] → [0, 1]
    return getSmoothstepRatio(kThumbFlexGateStartRatio, kThumbFlexGateEndRatio, filteredRatio);
}

double HandAngleAlgorithm::getThumbInwardAmplitudeRatio(const std::array<double, kChannelCount>& channelValueList) {
    // CH16 内收幅度，使用独立稳定状态，死区使用 spreadDeadbandRatio。
    constexpr int thumbSpreadChannel = 16;
    if (!hasClosed_ || !hasFist_) {
        return 0.0;
    }
    if (!isChannelValidForStage(thumbSpreadChannel, CalibrationStage::Fist)) {
        return 0.0;
    }
    double ratioValue = calcChRatio(
        channelValueList[toChannelArrayIndex(thumbSpreadChannel)],
        closedCalib_[toChannelArrayIndex(thumbSpreadChannel)],
        fistCalib_[toChannelArrayIndex(thumbSpreadChannel)]);
    return stabilizeRatio(thumbInwardAmplitudeStable_, ratioValue, runtimeConfig_.spreadDeadbandRatio);
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

    // 四指展开角：先按 AD 计算原始展开角，再按相邻两侧第一指节弯曲做算法层收束。
    const SpreadPairConfig& ringPinkySpreadConfig = kSpreadPairConfigList[0];
    const SpreadPairConfig& middleRingSpreadConfig = kSpreadPairConfigList[1];
    const SpreadPairConfig& indexMiddleSpreadConfig = kSpreadPairConfigList[2];
    outputValue.index_finger[3] = convertAngleToFloat(
        buildEffectiveSpreadAngle(
            getSpreadRatio(indexMiddleSpreadConfig.channelIndex, channelValueList[toChannelArrayIndex(indexMiddleSpreadConfig.channelIndex)]),
            std::max(indexRootFlexRatio, middleRootFlexRatio),
            indexMiddleSpreadConfig));
    outputValue.ring_finger[3] = convertAngleToFloat(
        buildEffectiveSpreadAngle(
            getSpreadRatio(middleRingSpreadConfig.channelIndex, channelValueList[toChannelArrayIndex(middleRingSpreadConfig.channelIndex)]),
            std::max(middleRootFlexRatio, ringRootFlexRatio),
            middleRingSpreadConfig));
    outputValue.little_finger[3] = convertAngleToFloat(
        buildEffectiveSpreadAngle(
            getSpreadRatio(ringPinkySpreadConfig.channelIndex, channelValueList[toChannelArrayIndex(ringPinkySpreadConfig.channelIndex)]),
            std::max(ringRootFlexRatio, littleRootFlexRatio),
            ringPinkySpreadConfig));

    // 拇指展开（有符号，正=外展，负=内收）
    double spreadRatio16 = getSpreadRatio(16, channelValueList[toChannelArrayIndex(16)]);
    double gateRatio = getThumbGateRatio(channelValueList);
    double amplitudeRatio = getThumbInwardAmplitudeRatio(channelValueList);
    double outwardRatio = spreadRatio16 * (1.0 - gateRatio);
    double inwardRatio = amplitudeRatio * gateRatio;
    outputValue.thumb[2] = convertAngleToFloat(
        kThumbOpenPalmAngle * outwardRatio - kThumbInwardPalmAngle * inwardRatio);
}

bool HandAngleAlgorithm::processFrame(const int16_t adValues[kChannelCount], HandAngleOutput& outputValue) {
    if (adValues == nullptr) {
        clearOutputValueList(outputValue.little_finger, 4);
        clearOutputValueList(outputValue.ring_finger, 4);
        clearOutputValueList(outputValue.middle_finger, 3);
        clearOutputValueList(outputValue.index_finger, 4);
        clearOutputValueList(outputValue.thumb, 3);
        return false;
    }

    // 缓存原始 AD + 均值滤波：不受校准状态影响，确保 getCurrentAd 随时可用
    for (std::size_t i = 0; i < kChannelCount; ++i) {
        latestRawAd_[i] = adValues[i];
    }
    rawFilter_.filteredValueList = filterFrm(adValues);

    if (!isReady()) {
        clearOutputValueList(outputValue.little_finger, 4);
        clearOutputValueList(outputValue.ring_finger, 4);
        clearOutputValueList(outputValue.middle_finger, 3);
        clearOutputValueList(outputValue.index_finger, 4);
        clearOutputValueList(outputValue.thumb, 3);
        return false;
    }

    // 实时串扰补偿：在滤波后、ratio 计算前扣除预测串扰。
    const auto correctedValueList = applyXtalk(rawFilter_.filteredValueList);
    buildOutputValue(correctedValueList, outputValue);
    return true;
}

std::array<double, kChannelCount> HandAngleAlgorithm::getCurrentAd(bool filtered) const {
    if (filtered) {
        return rawFilter_.filteredValueList;
    }
    std::array<double, kChannelCount> resultValueList{};
    for (std::size_t i = 0; i < kChannelCount; ++i) {
        resultValueList[i] = static_cast<double>(latestRawAd_[i]);
    }
    return resultValueList;
}

// ==================== 串扰补偿实现 ====================

namespace {

// 高斯消元求解 Ax = b，增广矩阵 [A | b]，返回解向量，奇异矩阵返回空 vector。
std::vector<double> solveLinearSystem(std::vector<std::vector<double>> augmentedMatrix, std::size_t sizeValue) {
    // 部分选主元（列主元）+ Gauss-Jordan 消元
    for (std::size_t pivotIndex = 0; pivotIndex < sizeValue; ++pivotIndex) {
        std::size_t bestRowIndex = pivotIndex;
        double bestAbsValue = std::abs(augmentedMatrix[pivotIndex][pivotIndex]);
        for (std::size_t rowIndex = pivotIndex + 1; rowIndex < sizeValue; ++rowIndex) {
            const double candidateAbsValue = std::abs(augmentedMatrix[rowIndex][pivotIndex]);
            if (candidateAbsValue > bestAbsValue) {
                bestAbsValue = candidateAbsValue;
                bestRowIndex = rowIndex;
            }
        }

        if (bestAbsValue <= 1e-9) {
            return {};
        }

        if (bestRowIndex != pivotIndex) {
            std::swap(augmentedMatrix[pivotIndex], augmentedMatrix[bestRowIndex]);
        }

        const double pivotValue = augmentedMatrix[pivotIndex][pivotIndex];
        for (std::size_t columnIndex = pivotIndex; columnIndex <= sizeValue; ++columnIndex) {
            augmentedMatrix[pivotIndex][columnIndex] /= pivotValue;
        }

        for (std::size_t rowIndex = 0; rowIndex < sizeValue; ++rowIndex) {
            if (rowIndex == pivotIndex) {
                continue;
            }
            const double factorValue = augmentedMatrix[rowIndex][pivotIndex];
            if (std::abs(factorValue) <= 1e-12) {
                continue;
            }
            for (std::size_t columnIndex = pivotIndex; columnIndex <= sizeValue; ++columnIndex) {
                augmentedMatrix[rowIndex][columnIndex] -= factorValue * augmentedMatrix[pivotIndex][columnIndex];
            }
        }
    }

    std::vector<double> solutionList;
    solutionList.reserve(sizeValue);
    for (std::size_t rowIndex = 0; rowIndex < sizeValue; ++rowIndex) {
        solutionList.push_back(augmentedMatrix[rowIndex][sizeValue]);
    }
    return solutionList;
}

}  // namespace

std::array<double, kChannelCount> HandAngleAlgorithm::applyXtalk(
    const std::array<double, kChannelCount>& channelValueList) const {
    if (!hasXtalk_) {
        return channelValueList;
    }

    auto correctedValueList = channelValueList;

    for (int targetChannelIndex : kCrosstalkTargetChannelList) {
        const int targetArrayIndex = toChannelArrayIndex(targetChannelIndex);
        const XtalkCoef& coef = xtalkCoef_[targetArrayIndex];
        if (!coef.isValid) {
            continue;
        }

        // predictedDelta = d + a * ΔT1 + b * ΔT2 + c * ΔT3
        double predictedDelta = coef.d;
        // ΔT1: CH17 → array index 16
        predictedDelta += coef.a * (channelValueList[16] - xtalkBase_[16]);
        // ΔT2: CH19 → array index 18
        predictedDelta += coef.b * (channelValueList[18] - xtalkBase_[18]);
        // ΔT3: CH16 → array index 15
        predictedDelta += coef.c * (channelValueList[15] - xtalkBase_[15]);

        correctedValueList[targetArrayIndex] = channelValueList[targetArrayIndex] - predictedDelta;
    }

    return correctedValueList;
}

XtalkCoef HandAngleAlgorithm::fitXtalkCoefForChannel(
    const std::deque<std::array<double, kChannelCount>>& frameList,
    int targetChannelIndex) const {
    // 最小二乘拟合 ΔP = aΔT1 + bΔT2 + cΔT3 + d。
    XtalkCoef zeroCoef;

    const std::size_t driverCount = kCrosstalkDriverChannelList.size();
    const bool fitIntercept = runtimeConfig_.crosstalkFitIntercept;
    const std::size_t featureCount = driverCount + (fitIntercept ? 1U : 0U);
    if (featureCount == 0 || frameList.size() < featureCount) {
        return zeroCoef;
    }

    const std::array<double, kChannelCount>& baselineValueList = frameList.front();
    const int targetArrayIndex = toChannelArrayIndex(targetChannelIndex);

    // 构建法方程 X^T X 和 X^T y
    std::vector<std::vector<double>> normalMatrix(featureCount, std::vector<double>(featureCount, 0.0));
    std::vector<double> normalVector(featureCount, 0.0);

    for (const auto& frameValueList : frameList) {
        // 构造特征向量 [ΔT1, ΔT2, ΔT3, (可选)1]
        std::vector<double> featureValueList;
        featureValueList.reserve(featureCount);
        for (int driverChannelIndex : kCrosstalkDriverChannelList) {
            const int driverArrayIndex = toChannelArrayIndex(driverChannelIndex);
            featureValueList.push_back(frameValueList[driverArrayIndex] - baselineValueList[driverArrayIndex]);
        }
        if (fitIntercept) {
            featureValueList.push_back(1.0);
        }

        const double targetDeltaValue = frameValueList[targetArrayIndex] - baselineValueList[targetArrayIndex];

        for (std::size_t rowIndex = 0; rowIndex < featureCount; ++rowIndex) {
            normalVector[rowIndex] += featureValueList[rowIndex] * targetDeltaValue;
            for (std::size_t columnIndex = 0; columnIndex < featureCount; ++columnIndex) {
                normalMatrix[rowIndex][columnIndex] += featureValueList[rowIndex] * featureValueList[columnIndex];
            }
        }
    }

    // 高斯消元
    std::vector<std::vector<double>> augmentedMatrix(featureCount, std::vector<double>(featureCount + 1, 0.0));
    for (std::size_t rowIndex = 0; rowIndex < featureCount; ++rowIndex) {
        for (std::size_t columnIndex = 0; columnIndex < featureCount; ++columnIndex) {
            augmentedMatrix[rowIndex][columnIndex] = normalMatrix[rowIndex][columnIndex];
        }
        augmentedMatrix[rowIndex][featureCount] = normalVector[rowIndex];
    }

    const auto solutionList = solveLinearSystem(augmentedMatrix, featureCount);
    if (solutionList.empty()) {
        return zeroCoef;
    }

    XtalkCoef coef;
    coef.isValid = true;

    // 系数映射：a=ΔT1(CH17), b=ΔT2(CH19), c=ΔT3(CH16)
    const std::array<const char*, 3> coefficientNameList = {"a", "b", "c"};
    for (std::size_t coefIndex = 0; coefIndex < driverCount && coefIndex < solutionList.size(); ++coefIndex) {
        switch (coefIndex) {
            case 0: coef.a = solutionList[coefIndex]; break;
            case 1: coef.b = solutionList[coefIndex]; break;
            case 2: coef.c = solutionList[coefIndex]; break;
        }
    }

    if (fitIntercept) {
        coef.d = solutionList.back();
    }

    return coef;
}

std::size_t HandAngleAlgorithm::fitXtalkCoefs(
    const std::deque<std::array<double, kChannelCount>>& frameList) {
    xtalkUnstableChList_.clear();
    std::size_t validCount = 0;

    if (frameList.empty()) {
        return 0;
    }

    for (int targetChannelIndex : kCrosstalkTargetChannelList) {
        if (targetChannelIndex == kCrosstalkExcludedChannel) {
            continue;
        }
        XtalkCoef coef = fitXtalkCoefForChannel(frameList, targetChannelIndex);
        xtalkCoef_[toChannelArrayIndex(targetChannelIndex)] = coef;

        if (coef.isValid) {
            ++validCount;
        }

        // 记录 |d| 超限的异常通道
        if (coef.isValid && runtimeConfig_.crosstalkFitIntercept &&
            std::abs(coef.d) > runtimeConfig_.crosstalkMaxAbsIntercept) {
            xtalkUnstableChList_.push_back(targetChannelIndex);
        }
    }

    return validCount;
}

std::vector<int> HandAngleAlgorithm::getXtalkUnstableChList() const {
    return xtalkUnstableChList_;
}

std::size_t HandAngleAlgorithm::getXtalkValidTargetChannelCount() const {
    return xtalkValidTargetChannelCount_;
}

}  // namespace handdemo
