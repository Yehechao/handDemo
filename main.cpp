#include <windows.h>

#include <conio.h>
#include <array>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "hand_algorithm.h"
#include "hand_skeleton_viewer.h"
// 真实串口
// #include "serial_port_io.h"

namespace {

using handdemo::CalibrationStage;
using handdemo::HandAngleAlgorithm;
using handdemo::HandAngleOutput;
using handdemo::HandSkeletonViewer;
using handdemo::kAdMaxValue;
using handdemo::kAdMinValue;
using handdemo::kChannelCount;

struct RecordedStepData {
    std::vector<std::array<int16_t, kChannelCount>> step1FrameList;
    std::vector<std::array<int16_t, kChannelCount>> step2FrameList;
    std::vector<std::array<int16_t, kChannelCount>> step3FrameList;
};

enum class KeyboardCommand {
    None = 0,
    QuitProgram = 1,
};

KeyboardCommand parseKeyValue(int keyValue) {
    if (keyValue < 0) {
        return KeyboardCommand::None;
    }

    if (keyValue == 27) {
        return KeyboardCommand::QuitProgram;
    }

    const int lowerKeyValue = std::tolower(keyValue);
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

bool parseFrameLine(const std::string& lineText, std::array<int16_t, kChannelCount>& frameValueList) {
    std::stringstream lineStream(lineText);
    std::string partText;
    std::size_t channelIndex = 0;
    while (std::getline(lineStream, partText, ',')) {
        if (channelIndex >= kChannelCount) {
            return false;
        }

        try {
            const int channelValue = std::stoi(partText);
            if (channelValue < kAdMinValue || channelValue > kAdMaxValue) {
                return false;
            }
            frameValueList[channelIndex] = static_cast<int16_t>(channelValue);
        } catch (...) {
            return false;
        }
        ++channelIndex;
    }

    return channelIndex == kChannelCount;
}

bool loadRecordedStepData(const std::string& filePathText, RecordedStepData& recordedStepData) {
    std::ifstream fileObject(filePathText);
    if (!fileObject.is_open()) {
        return false;
    }

    std::vector<std::array<int16_t, kChannelCount>>* currentFrameList = nullptr;
    std::string lineText;
    while (std::getline(fileObject, lineText)) {
        if (!lineText.empty() && lineText.back() == '\r') {
            lineText.pop_back();
        }

        if (lineText.empty()) {
            continue;
        }

        if (lineText == "step1") {
            currentFrameList = &recordedStepData.step1FrameList;
            continue;
        }
        if (lineText == "step2") {
            currentFrameList = &recordedStepData.step2FrameList;
            continue;
        }
        if (lineText == "step3") {
            currentFrameList = &recordedStepData.step3FrameList;
            continue;
        }

        if (currentFrameList == nullptr) {
            continue;
        }

        std::array<int16_t, kChannelCount> frameValueList{};
        if (!parseFrameLine(lineText, frameValueList)) {
            return false;
        }
        currentFrameList->push_back(frameValueList);
    }

    return !recordedStepData.step1FrameList.empty() &&
        !recordedStepData.step2FrameList.empty() &&
        !recordedStepData.step3FrameList.empty();
}

bool runCalibrationStep(
    HandAngleAlgorithm& algorithm,
    CalibrationStage stage,
    const std::vector<std::array<int16_t, kChannelCount>>& frameList) {
    algorithm.beginCalibration(stage);
    for (const auto& frameValueList : frameList) {
        algorithm.pushCalibrationFrame(frameValueList.data());
    }
    return algorithm.finishCalibration();
}

}  // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    const std::string inputFilePathText = "D:\\yhc_code\\handDemo_c\\handDemo\\ADC_20260415_200242.txt";
    RecordedStepData recordedStepData;
    if (!loadRecordedStepData(inputFilePathText, recordedStepData)) {
        std::cout << "加载模拟 AD 数据失败: " << inputFilePathText << std::endl;
        return 1;
    }

    HandAngleAlgorithm algorithm;
    HandSkeletonViewer handSkeletonViewer;
    HandAngleOutput outputValue{};

    // 真实串口
    // handdemo::SerialFrameReceiver serialFrameReceiver;
    // std::array<int16_t, kChannelCount> latestFrameValueList{};

    std::cout << "当前模式: 使用录制 txt 模拟输入，不走真实串口。" << std::endl;
    std::cout << "数据文件: " << inputFilePathText << std::endl;
    std::cout << "step1 帧数=" << recordedStepData.step1FrameList.size()
              << "，step2 帧数=" << recordedStepData.step2FrameList.size()
              << "，step3 帧数=" << recordedStepData.step3FrameList.size() << std::endl;
    std::cout << "按键说明: Q 或 Esc 退出。" << std::endl;
    std::cout << "开始执行两步校准..." << std::endl;

    if (!runCalibrationStep(algorithm, CalibrationStage::Closed, recordedStepData.step1FrameList)) {
        std::cout << "step1 校准失败。" << std::endl;
        return 1;
    }
    std::cout << "step1 完成: 手指伸直。" << std::endl;

    if (!runCalibrationStep(algorithm, CalibrationStage::Fist, recordedStepData.step2FrameList)) {
        std::cout << "step2 校准失败。" << std::endl;
        return 1;
    }
    std::cout << "step2 完成: 手握拳。" << std::endl;

    if (!algorithm.isReady()) {
        std::cout << "两步校准未完成，程序退出。" << std::endl;
        return 1;
    }

    std::cout << "开始循环播放 step3，自由动作模拟中..." << std::endl;

    std::size_t playbackFrameIndex = 0;
    // 40 FPS: 每帧间隔固定为 25ms。
    const auto frameIntervalValue = std::chrono::milliseconds(25);

    while (true) {
        const int keyValue = handSkeletonViewer.showWindowFrame();
        KeyboardCommand keyboardCommand = parseKeyValue(keyValue);
        if (keyboardCommand == KeyboardCommand::None) {
            keyboardCommand = pollConsoleKeyboardCommand();
        }

        if (keyboardCommand == KeyboardCommand::QuitProgram) {
            break;
        }

        const auto& currentFrameValueList = recordedStepData.step3FrameList[playbackFrameIndex];
        if (algorithm.processFrame(currentFrameValueList.data(), outputValue)) {
            handSkeletonViewer.updateFromAngles(outputValue);
        }

        ++playbackFrameIndex;
        if (playbackFrameIndex >= recordedStepData.step3FrameList.size()) {
            playbackFrameIndex = 0;
        }

        std::this_thread::sleep_for(frameIntervalValue);
    }

    handSkeletonViewer.closeWindow();
    std::cout << "程序已退出。" << std::endl;
    return 0;
}
