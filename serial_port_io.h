#pragma once

#include <windows.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

#include "config.h"

namespace handdemo {

struct SerialPollResult {
    bool hasFrame = false;
    bool hasStatusMessage = false;
    std::string statusMessage;
};

class SerialFrameReceiver {
public:
    SerialFrameReceiver();
    ~SerialFrameReceiver();

    // getTargetHardwareIdText: 返回当前串口接收器内部使用的目标硬件标识，便于业务层打印启动信息
    std::string getTargetHardwareIdText() const;

    // poll: 处理搜索、连接、读串口、重连和无帧告警，并在有新帧时返回最新 AD 数据
    SerialPollResult poll(std::array<int16_t, kChannelCount>& latestFrameValueList);

private:
    void closePort();

    HANDLE serialHandle_ = INVALID_HANDLE_VALUE;
    std::string currentPortNameText_;
    std::string receiveBuffer_;
    bool hasLoggedFirstFrame_ = false;
    std::chrono::steady_clock::time_point lastSearchLogTimePoint_{};
    std::chrono::steady_clock::time_point lastNoFrameLogTimePoint_{};
    std::chrono::steady_clock::time_point lastValidFrameTimePoint_{};
};

}  // namespace handdemo
