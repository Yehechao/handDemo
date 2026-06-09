// Copyright (c) 2026 Matrix 墨现科技. All rights reserved.

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

#include "config.h"

namespace handdemo {

enum class ConnectionMode {
    Wired = 0,
    Wireless = 1,
};

// 有线硬件 ID 列表：右手 PID_5740，左手 PID_5739
constexpr const wchar_t* kWiredHardwareIdList[] = {
    L"USB\\VID_0483&PID_5740",
    L"USB\\VID_0483&PID_5739",
};
// 无线硬件 ID 列表：CH340 接收器
constexpr const wchar_t* kWirelessHardwareIdList[] = {
    L"USB\\VID_1A86&PID_7523",
};
// 串口波特率
constexpr DWORD kSerialBaudRate = 460800;

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

    // setConnectionMode: 切换有线/无线模式，关闭当前串口并触发重扫
    void setConnectionMode(ConnectionMode mode);
    // getConnectionMode: 返回当前连接模式
    ConnectionMode getConnectionMode() const;

    // poll: 处理搜索、连接、读串口、重连和无帧告警，并在有新帧时返回最新 AD 数据
    SerialPollResult poll(std::array<int16_t, kChannelCount>& latestFrameValueList);

private:
    void closePort();

    HANDLE serialHandle_ = INVALID_HANDLE_VALUE;
    std::string currentPortNameText_;
    std::string receiveBuffer_;
    uint16_t expectedFrameLengthValue_ = 0;
    bool hasExpectedFrameLengthValue_ = false;
    uint8_t expectedDataTypeValue_ = 0;
    bool hasExpectedDataTypeValue_ = false;
    bool hasLoggedFirstFrame_ = false;
    ConnectionMode connectionMode_ = ConnectionMode::Wired;
    std::chrono::steady_clock::time_point lastSearchLogTimePoint_{};
    std::chrono::steady_clock::time_point lastNoFrameLogTimePoint_{};
    std::chrono::steady_clock::time_point lastValidFrameTimePoint_{};
    std::chrono::steady_clock::time_point lastDeviceScanTimePoint_{};
};

}  // namespace handdemo
