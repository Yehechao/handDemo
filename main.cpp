#include <windows.h>

#include <conio.h>
#include <array>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <string>

#include "hand_algorithm.h"
#include "serial_port_io.h"

namespace {

using handdemo::CalibrationStage;
using handdemo::HandAngleAlgorithm;
using handdemo::HandAngleOutput;
using handdemo::RuntimeConfig;
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
    if (stage == CalibrationStage::Closed) {
        return "手指伸直";
    }
    if (stage == CalibrationStage::Fist) {
        return "手握拳";
    }
    return "手展开";
}

KeyboardCommand parseKeyValue(int keyValue) {
    if (keyValue < 0) {
        return KeyboardCommand::None;
    }
    if (keyValue == 27 || std::tolower(keyValue) == 'q') {
        return KeyboardCommand::QuitProgram;
    }
    if (keyValue == ' ') {
        return KeyboardCommand::StartCalibration;
    }
    if (std::tolower(keyValue) == 'c') {
        return KeyboardCommand::ResetCalibration;
    }
    return KeyboardCommand::None;
}

KeyboardCommand pollKeyboardCommand() {
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
    if (completedCalibrationStepCount == 2) {
        stage = CalibrationStage::Spread;
        return true;
    }
    return false;
}

int getCalibrationStepNumber(CalibrationStage stage) {
    if (stage == CalibrationStage::Closed) {
        return 1;
    }
    if (stage == CalibrationStage::Fist) {
        return 2;
    }
    return 3;
}

void beginSamplingStage(HandAngleAlgorithm& algorithm, SamplingRuntimeState& state, CalibrationStage stage) {
    algorithm.beginCalibration(stage);
    state.isActive = true;
    state.stage = stage;
    state.startTimePoint = std::chrono::steady_clock::now();
    state.collectedFrameCount = 0;
}

void resetCalibration(HandAngleAlgorithm& algorithm, SamplingRuntimeState& state, int& stepCount) {
    algorithm.reset();
    state = {};
    stepCount = 0;
}

void printStartupGuide(const SerialFrameReceiver& receiver) {
    std::cout << "目标硬件: " << receiver.getTargetHardwareIdText() << std::endl;
    std::cout << "按键: Space=开始校准  C=重置  Q=退出" << std::endl;
    std::cout << "校准顺序: step1=手指伸直 -> step2=手握拳 -> step3=手展开" << std::endl;
    std::cout << "请按空格开始 step1。" << std::endl;
}

void printCalibrationFinished(CalibrationStage stage, std::size_t frameCount) {
    std::cout << "step" << getCalibrationStepNumber(stage)
              << " 完成 姿态=" << getCalibrationStageText(stage)
              << " 帧数=" << frameCount << std::endl;
}

void printFingerAngles(const char* fingerName, const float angles[4], int flexCount, int spreadIndex) {
    std::cout << std::fixed << std::setprecision(1);
    if (flexCount == 3) {
        std::cout << fingerName << " 弯曲: MCP=" << angles[0] << " PIP=" << angles[1] << " DIP=" << angles[2];
    } else {
        std::cout << fingerName << " 弯曲: MCP=" << angles[0] << " IP=" << angles[1];
    }
    if (spreadIndex >= 0 && spreadIndex < 4) {
        std::cout << "  展开: " << angles[spreadIndex];
    }
    std::cout << std::endl;
}

void printOutputValue(const HandAngleOutput& outputValue) {
    std::cout << "----------" << std::endl;
    printFingerAngles("食指  ", outputValue.index_finger, 3, 3);
    printFingerAngles("中指  ", outputValue.middle_finger, 3, -1);
    printFingerAngles("无名指", outputValue.ring_finger, 3, 3);
    printFingerAngles("小指  ", outputValue.little_finger, 3, 3);
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "拇指  " << "弯曲: MCP=" << outputValue.thumb[0] << " IP=" << outputValue.thumb[1]
              << "  展开: " << std::showpos << outputValue.thumb[2] << std::noshowpos
              << (outputValue.thumb[2] >= 0 ? "(外展)" : "(内收)") << std::endl;
}


}  // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    HandAngleAlgorithm algorithm;
    RuntimeConfig runtimeConfig;
    runtimeConfig.meanFilterWindowFrameCount = 15;
    runtimeConfig.thumbGateFilterWindowSize = 10;
    runtimeConfig.thumbInwardGateChannel = 18;
    runtimeConfig.thumbGateDeadbandRatio = 0.0;
    runtimeConfig.spreadDeadbandRatio = 0.0;
    algorithm.setRuntimeConfig(runtimeConfig);
   
    HandAngleOutput outputValue{};
    SerialFrameReceiver serialFrameReceiver;
    std::array<int16_t, kChannelCount> latestFrameValueList{};

    SamplingRuntimeState samplingRuntimeState;
    int completedCalibrationStepCount = 0;

    printStartupGuide(serialFrameReceiver);

    while (true) {
        KeyboardCommand keyboardCommand = pollKeyboardCommand();

        if (keyboardCommand == KeyboardCommand::QuitProgram) {
            break;
        }

        if (keyboardCommand == KeyboardCommand::ResetCalibration) {
            resetCalibration(algorithm, samplingRuntimeState, completedCalibrationStepCount);
            std::cout << "已重置校准，请按空格重新开始 step1。" << std::endl;
        } else if (keyboardCommand == KeyboardCommand::StartCalibration) {
            if (samplingRuntimeState.isActive) {
                std::cout << "当前阶段正在采样中，请等待 2 秒采样结束。" << std::endl;
            } else {
                CalibrationStage nextStage = CalibrationStage::Closed;
                if (tryGetNextCalibrationStage(completedCalibrationStepCount, nextStage)) {
                    beginSamplingStage(algorithm, samplingRuntimeState, nextStage);
                    std::cout << "开始 step" << getCalibrationStepNumber(nextStage)
                              << " 校准 姿态=" << getCalibrationStageText(nextStage)
                              << " 持续 " << kSamplingDurationMs << "ms" << std::endl;
                } else {
                    std::cout << "三步校准已完成，正在实时输出角度。" << std::endl;
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
                    std::cout << "step" << getCalibrationStepNumber(finishedStage)
                              << " 校准失败 2 秒内有效帧不足 帧数=" << collectedFrameCount
                              << " 请按 C 重试" << std::endl;
                    continue;
                }

                ++completedCalibrationStepCount;
                printCalibrationFinished(finishedStage, collectedFrameCount);
                if (finishedStage == CalibrationStage::Closed) {
                    std::cout << "请保持手握拳，按空格开始 step2。" << std::endl;
                } else if (finishedStage == CalibrationStage::Fist) {
                    std::cout << "请保持手展开，按空格开始 step3。" << std::endl;
                } else {
                    std::cout << "三步校准完成，开始实时输出角度。" << std::endl;
                }
            }
        }

        if (!serialPollResult.hasFrame || samplingRuntimeState.isActive || !algorithm.isReady()) {
            continue;
        }

        if (algorithm.processFrame(latestFrameValueList.data(), outputValue)) {
            printOutputValue(outputValue);
        }
    }

    std::cout << "程序已退出。" << std::endl;
    return 0;
}
