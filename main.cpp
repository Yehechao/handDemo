#include <windows.h>

#include <conio.h>
#include <array>
#include <chrono>
#include <cctype>
#include <iostream>
#include <string>

#include "hand_algorithm.h"
#include "hand_skeleton_viewer.h"
#include "serial_port_io.h"

namespace {

using handdemo::CalibrationStage;
using handdemo::HandAngleAlgorithm;
using handdemo::HandAngleOutput;
using handdemo::HandSkeletonViewer;
using handdemo::SerialFrameReceiver;
using handdemo::SerialPollResult;
using handdemo::kChannelCount;
using handdemo::kSamplingDurationMs;

struct SamplingRuntimeState {
    bool isActive = false;
    CalibrationStage stage = CalibrationStage::Closed;
    std::chrono::steady_clock::time_point startTimePoint{};
    std::size_t collectedFrameCount = 0;
};

enum class KeyboardCommand {
    None = 0,
    StartCalibration = 1,
    ResetCalibration = 2,
    QuitProgram = 3,
};

const char* getCalibrationStageText(CalibrationStage stage) {
    return stage == CalibrationStage::Closed ? "手指伸直" : "手握拳";
}

KeyboardCommand parseKeyValue(int keyValue) {
    if (keyValue < 0) {
        return KeyboardCommand::None;
    }

    if (keyValue == 27) {
        return KeyboardCommand::QuitProgram;
    }

    if (keyValue == ' ') {
        return KeyboardCommand::StartCalibration;
    }

    const int lowerKeyValue = std::tolower(keyValue);
    if (lowerKeyValue == 'c') {
        return KeyboardCommand::ResetCalibration;
    }
    if (lowerKeyValue == 'q') {
        return KeyboardCommand::QuitProgram;
    }

    return KeyboardCommand::None;
}

KeyboardCommand pollConsoleKeyboardCommand() {
    if (!_kbhit()) {
        return KeyboardCommand::None;
    }

    int keyValue = _getch();
    if (keyValue == 0 || keyValue == 224) {
        keyValue = _getch();
    }

    return parseKeyValue(keyValue);
}

bool tryGetNextCalibrationStage(int completedCalibrationStepCount, CalibrationStage& stage) {
    if (completedCalibrationStepCount == 0) {
        stage = CalibrationStage::Closed;
        return true;
    }
    if (completedCalibrationStepCount == 1) {
        stage = CalibrationStage::Fist;
        return true;
    }
    return false;
}

int getCalibrationStepNumber(CalibrationStage stage) {
    return stage == CalibrationStage::Closed ? 1 : 2;
}

void beginSamplingStage(
    HandAngleAlgorithm& algorithm,
    SamplingRuntimeState& samplingRuntimeState,
    CalibrationStage stage) {
    algorithm.beginCalibration(stage);
    samplingRuntimeState.isActive = true;
    samplingRuntimeState.stage = stage;
    samplingRuntimeState.startTimePoint = std::chrono::steady_clock::now();
    samplingRuntimeState.collectedFrameCount = 0;
}

void resetCalibrationState(
    HandAngleAlgorithm& algorithm,
    HandSkeletonViewer& handSkeletonViewer,
    SamplingRuntimeState& samplingRuntimeState,
    int& completedCalibrationStepCount) {
    algorithm.reset();
    handSkeletonViewer.resetPose();
    samplingRuntimeState = {};
    completedCalibrationStepCount = 0;
}

void printStartupGuide(const SerialFrameReceiver& serialFrameReceiver) {
    std::cout << "当前模式: 实时串口二进制协议输入。" << std::endl;
    std::cout << "目标硬件: " << serialFrameReceiver.getTargetHardwareIdText() << std::endl;
    std::cout << "按键说明:" << std::endl;
    std::cout << "  Space: 开始当前阶段 2 秒校准" << std::endl;
    std::cout << "  C: 清空校准并重新开始" << std::endl;
    std::cout << "  Q 或 Esc: 退出程序" << std::endl;
    std::cout << "校准顺序: step1=手指伸直 -> step2=手握拳" << std::endl;
    std::cout << "请先按空格开始 step1。" << std::endl;
}

void printCalibrationStageFinished(CalibrationStage stage, std::size_t collectedFrameCount) {
    std::cout
        << "step" << getCalibrationStepNumber(stage)
        << " 完成，姿态=" << getCalibrationStageText(stage)
        << "，采样帧数=" << collectedFrameCount
        << std::endl;
}

}  // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    HandAngleAlgorithm algorithm;
    HandSkeletonViewer handSkeletonViewer;
    HandAngleOutput outputValue{};
    SerialFrameReceiver serialFrameReceiver;
    std::array<int16_t, kChannelCount> latestFrameValueList{};

    SamplingRuntimeState samplingRuntimeState;
    int completedCalibrationStepCount = 0;

    printStartupGuide(serialFrameReceiver);

    while (true) {
        const int keyValue = handSkeletonViewer.showWindowFrame();
        KeyboardCommand keyboardCommand = parseKeyValue(keyValue);
        if (keyboardCommand == KeyboardCommand::None) {
            keyboardCommand = pollConsoleKeyboardCommand();
        }

        if (keyboardCommand == KeyboardCommand::QuitProgram) {
            break;
        }

        if (keyboardCommand == KeyboardCommand::ResetCalibration) {
            resetCalibrationState(
                algorithm,
                handSkeletonViewer,
                samplingRuntimeState,
                completedCalibrationStepCount);
            std::cout << "已清空校准状态，请按空格重新开始 step1。" << std::endl;
        } else if (keyboardCommand == KeyboardCommand::StartCalibration) {
            if (samplingRuntimeState.isActive) {
                std::cout << "当前阶段正在采样中，请等待 2 秒采样结束。" << std::endl;
            } else {
                CalibrationStage nextStage = CalibrationStage::Closed;
                if (tryGetNextCalibrationStage(completedCalibrationStepCount, nextStage)) {
                    beginSamplingStage(algorithm, samplingRuntimeState, nextStage);
                    std::cout
                        << "开始 step" << getCalibrationStepNumber(nextStage)
                        << " 校准，姿态=" << getCalibrationStageText(nextStage)
                        << "，持续 " << kSamplingDurationMs << "ms。"
                        << std::endl;
                } else {
                    std::cout << "两步校准已经完成，程序正在实时输出角度。" << std::endl;
                }
            }
        }

        const SerialPollResult serialPollResult = serialFrameReceiver.poll(latestFrameValueList);
        if (serialPollResult.hasStatusMessage) {
            std::cout << serialPollResult.statusMessage << std::endl;
        }

        if (serialPollResult.hasFrame && samplingRuntimeState.isActive) {
            if (algorithm.pushCalibrationFrame(latestFrameValueList.data())) {
                ++samplingRuntimeState.collectedFrameCount;
            }
        }

        if (samplingRuntimeState.isActive) {
            const auto elapsedTimeValue = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - samplingRuntimeState.startTimePoint);
            if (elapsedTimeValue.count() >= kSamplingDurationMs) {
                const CalibrationStage finishedStage = samplingRuntimeState.stage;
                const std::size_t collectedFrameCount = samplingRuntimeState.collectedFrameCount;
                samplingRuntimeState.isActive = false;

                if (!algorithm.finishCalibration()) {
                    std::cout
                        << "step" << getCalibrationStepNumber(finishedStage)
                        << " 校准失败，2 秒内有效帧不足。当前采样帧数="
                        << collectedFrameCount
                        << "。请检查串口后按 C 重试。"
                        << std::endl;
                    continue;
                }

                ++completedCalibrationStepCount;
                printCalibrationStageFinished(finishedStage, collectedFrameCount);
                if (finishedStage == CalibrationStage::Closed) {
                    std::cout << "请保持手握拳，然后再按一次空格开始 step2。" << std::endl;
                } else {
                    std::cout << "两步校准完成，开始实时处理串口 AD 数据。" << std::endl;
                }
            }
        }

        if (!serialPollResult.hasFrame || samplingRuntimeState.isActive || !algorithm.isReady()) {
            continue;
        }

        if (algorithm.processFrame(latestFrameValueList.data(), outputValue)) {
            handSkeletonViewer.updateFromAngles(outputValue);
        }
    }

    handSkeletonViewer.closeWindow();
    std::cout << "程序已退出。" << std::endl;
    return 0;
}
