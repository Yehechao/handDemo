#include "serial_port_io.h"

#include <algorithm>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>

#include <cwchar>
#include <cwctype>
#include <thread>
#include <sstream>
#include <vector>

namespace handdemo {

namespace {

enum class ReadFrameResult {
    NoFrame = 0,
    HasFrame = 1,
    Disconnected = 2,
};

// hardwareIdText: 串口接收器内部固定查找的目标硬件 VID/PID
constexpr wchar_t hardwareIdText[] = L"USB\\VID_1A86&PID_7523";
// baudRateValue: 串口接收器内部固定波特率
constexpr DWORD baudRateValue = 460800;
// reconnectDelayMs: 未连接或断开后的重试间隔
constexpr int reconnectDelayMs = 100;
// idleSleepMs: 无新数据时的短暂休眠
constexpr int idleSleepMs = 2;
// searchLogIntervalMs: 未找到目标串口时的提示节流间隔
constexpr int searchLogIntervalMs = 3000;
// noFrameLogIntervalMs: 连续无有效帧时的提示阈值
constexpr int noFrameLogIntervalMs = 5000;

std::wstring toUpperText(const std::wstring& rawText) {
    std::wstring upperText = rawText;
    std::transform(
        upperText.begin(),
        upperText.end(),
        upperText.begin(),
        [](wchar_t charValue) { return static_cast<wchar_t>(std::towupper(charValue)); });
    return upperText;
}

std::wstring extractHexQuadAfterToken(const std::wstring& upperText, const std::wstring& tokenText) {
    std::size_t tokenIndex = upperText.find(tokenText);
    if (tokenIndex == std::wstring::npos) {
        return L"";
    }

    tokenIndex += tokenText.size();
    while (tokenIndex < upperText.size()) {
        const wchar_t currentChar = upperText[tokenIndex];
        if ((currentChar >= L'0' && currentChar <= L'9') || (currentChar >= L'A' && currentChar <= L'F')) {
            break;
        }
        ++tokenIndex;
    }

    if (tokenIndex + 4 > upperText.size()) {
        return L"";
    }

    std::wstring hexText = upperText.substr(tokenIndex, 4);
    for (wchar_t currentChar : hexText) {
        const bool isDigit = (currentChar >= L'0' && currentChar <= L'9');
        const bool isHexLetter = (currentChar >= L'A' && currentChar <= L'F');
        if (!isDigit && !isHexLetter) {
            return L"";
        }
    }
    return hexText;
}

std::wstring normalizeHardwareId(const std::wstring& rawHardwareIdText) {
    const std::wstring upperText = toUpperText(rawHardwareIdText);
    const std::wstring vendorIdText = extractHexQuadAfterToken(upperText, L"VID");
    const std::wstring productIdText = extractHexQuadAfterToken(upperText, L"PID");
    if (vendorIdText.empty() || productIdText.empty()) {
        return upperText;
    }
    return L"VID_" + vendorIdText + L"&PID_" + productIdText;
}

std::wstring getRegistryStringValue(HKEY registryKey, const wchar_t* valueName) {
    DWORD dataType = 0;
    DWORD byteCount = 0;
    if (RegQueryValueExW(registryKey, valueName, nullptr, &dataType, nullptr, &byteCount) != ERROR_SUCCESS ||
        (dataType != REG_SZ && dataType != REG_EXPAND_SZ)) {
        return L"";
    }

    std::vector<wchar_t> buffer(byteCount / sizeof(wchar_t) + 1, L'\0');
    if (RegQueryValueExW(
            registryKey,
            valueName,
            nullptr,
            &dataType,
            reinterpret_cast<LPBYTE>(buffer.data()),
            &byteCount) != ERROR_SUCCESS) {
        return L"";
    }
    return std::wstring(buffer.data());
}

std::wstring getNormalizedHardwareIdFromMultiStringProperty(
    HDEVINFO deviceInfoList,
    SP_DEVINFO_DATA& deviceInfoData,
    DWORD propertyKey) {
    DWORD dataType = 0;
    DWORD requiredByteCount = 0;
    SetupDiGetDeviceRegistryPropertyW(
        deviceInfoList,
        &deviceInfoData,
        propertyKey,
        &dataType,
        nullptr,
        0,
        &requiredByteCount);

    if (requiredByteCount == 0 || dataType != REG_MULTI_SZ) {
        return L"";
    }

    std::vector<wchar_t> buffer(requiredByteCount / sizeof(wchar_t) + 2, L'\0');
    if (!SetupDiGetDeviceRegistryPropertyW(
            deviceInfoList,
            &deviceInfoData,
            propertyKey,
            &dataType,
            reinterpret_cast<PBYTE>(buffer.data()),
            static_cast<DWORD>(buffer.size() * sizeof(wchar_t)),
            nullptr)) {
        return L"";
    }

    for (const wchar_t* currentText = buffer.data(); *currentText != L'\0'; currentText += std::wcslen(currentText) + 1) {
        const std::wstring normalizedText = normalizeHardwareId(currentText);
        if (normalizedText.find(L"VID_") != std::wstring::npos &&
            normalizedText.find(L"PID_") != std::wstring::npos) {
            return normalizedText;
        }
    }
    return L"";
}

std::wstring getNormalizedHardwareIdFromDeviceInstanceId(HDEVINFO deviceInfoList, SP_DEVINFO_DATA& deviceInfoData) {
    DWORD requiredCharCount = 0;
    SetupDiGetDeviceInstanceIdW(deviceInfoList, &deviceInfoData, nullptr, 0, &requiredCharCount);
    if (requiredCharCount == 0) {
        return L"";
    }

    std::vector<wchar_t> buffer(requiredCharCount + 1, L'\0');
    if (!SetupDiGetDeviceInstanceIdW(
            deviceInfoList,
            &deviceInfoData,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            nullptr)) {
        return L"";
    }

    const std::wstring normalizedText = normalizeHardwareId(buffer.data());
    if (normalizedText.find(L"VID_") == std::wstring::npos ||
        normalizedText.find(L"PID_") == std::wstring::npos) {
        return L"";
    }
    return normalizedText;
}

std::wstring getDeviceNormalizedHardwareId(HDEVINFO deviceInfoList, SP_DEVINFO_DATA& deviceInfoData) {
    const std::wstring instanceHardwareId = getNormalizedHardwareIdFromDeviceInstanceId(deviceInfoList, deviceInfoData);
    if (!instanceHardwareId.empty()) {
        return instanceHardwareId;
    }

    const std::wstring registryHardwareId = getNormalizedHardwareIdFromMultiStringProperty(
        deviceInfoList,
        deviceInfoData,
        SPDRP_HARDWAREID);
    if (!registryHardwareId.empty()) {
        return registryHardwareId;
    }

    return getNormalizedHardwareIdFromMultiStringProperty(
        deviceInfoList,
        deviceInfoData,
        SPDRP_COMPATIBLEIDS);
}

std::wstring getPortName(HDEVINFO deviceInfoList, SP_DEVINFO_DATA& deviceInfoData) {
    HKEY registryKey = SetupDiOpenDevRegKey(
        deviceInfoList,
        &deviceInfoData,
        DICS_FLAG_GLOBAL,
        0,
        DIREG_DEV,
        KEY_READ);
    if (registryKey == INVALID_HANDLE_VALUE) {
        return L"";
    }

    const std::wstring portName = getRegistryStringValue(registryKey, L"PortName");
    RegCloseKey(registryKey);
    return portName;
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

std::string toUtf8String(const std::wstring& wideText) {
    if (wideText.empty()) {
        return "";
    }

    const int byteCount = WideCharToMultiByte(
        CP_UTF8,
        0,
        wideText.c_str(),
        static_cast<int>(wideText.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (byteCount <= 0) {
        return "";
    }

    std::string utf8Text(static_cast<std::size_t>(byteCount), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        wideText.c_str(),
        static_cast<int>(wideText.size()),
        &utf8Text[0],
        byteCount,
        nullptr,
        nullptr);
    return utf8Text;
}

std::wstring findMatchedPortName(const std::wstring& targetHardwareIdText) {
    const std::wstring normalizedTargetHardwareId = normalizeHardwareId(targetHardwareIdText);
    HDEVINFO deviceInfoList = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (deviceInfoList == INVALID_HANDLE_VALUE) {
        return L"";
    }

    std::wstring matchedPortName;
    SP_DEVINFO_DATA deviceInfoData{};
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    for (DWORD deviceIndex = 0; SetupDiEnumDeviceInfo(deviceInfoList, deviceIndex, &deviceInfoData); ++deviceIndex) {
        const std::wstring currentHardwareId = getDeviceNormalizedHardwareId(deviceInfoList, deviceInfoData);
        if (currentHardwareId != normalizedTargetHardwareId) {
            continue;
        }

        matchedPortName = getPortName(deviceInfoList, deviceInfoData);
        if (!matchedPortName.empty()) {
            break;
        }
    }

    SetupDiDestroyDeviceInfoList(deviceInfoList);
    return matchedPortName;
}

HANDLE openSerialPort(const std::wstring& portName, DWORD baudRateValue) {
    const std::wstring fullPortName = L"\\\\.\\" + portName;
    HANDLE serialHandle = CreateFileW(
        fullPortName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (serialHandle == INVALID_HANDLE_VALUE) {
        return INVALID_HANDLE_VALUE;
    }

    DCB deviceControlBlock{};
    deviceControlBlock.DCBlength = sizeof(DCB);
    if (!GetCommState(serialHandle, &deviceControlBlock)) {
        CloseHandle(serialHandle);
        return INVALID_HANDLE_VALUE;
    }

    deviceControlBlock.BaudRate = baudRateValue;
    deviceControlBlock.ByteSize = 8;
    deviceControlBlock.Parity = NOPARITY;
    deviceControlBlock.StopBits = ONESTOPBIT;
    deviceControlBlock.fBinary = TRUE;
    deviceControlBlock.fParity = FALSE;
    if (!SetCommState(serialHandle, &deviceControlBlock)) {
        CloseHandle(serialHandle);
        return INVALID_HANDLE_VALUE;
    }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 1;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 1;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 5;
    if (!SetCommTimeouts(serialHandle, &timeouts)) {
        CloseHandle(serialHandle);
        return INVALID_HANDLE_VALUE;
    }

    SetupComm(serialHandle, 4096, 4096);
    PurgeComm(serialHandle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return serialHandle;
}

ReadFrameResult tryReadLatestFrame(
    HANDLE serialHandle,
    std::string& receiveBuffer,
    std::array<int16_t, kChannelCount>& latestFrameValueList) {
    char tempBuffer[512];
    DWORD readByteCount = 0;
    if (!ReadFile(serialHandle, tempBuffer, static_cast<DWORD>(sizeof(tempBuffer)), &readByteCount, nullptr)) {
        return ReadFrameResult::Disconnected;
    }

    if (readByteCount > 0) {
        receiveBuffer.append(tempBuffer, tempBuffer + readByteCount);
        if (receiveBuffer.size() > 65536) {
            const std::size_t lineIndex = receiveBuffer.find_last_of('\n');
            if (lineIndex != std::string::npos) {
                receiveBuffer = receiveBuffer.substr(lineIndex + 1);
            } else {
                receiveBuffer = receiveBuffer.substr(receiveBuffer.size() / 2);
            }
        }
    }

    bool hasValidFrame = false;
    std::size_t lineEndIndex = receiveBuffer.find('\n');
    while (lineEndIndex != std::string::npos) {
        std::string lineText = receiveBuffer.substr(0, lineEndIndex);
        receiveBuffer.erase(0, lineEndIndex + 1);
        if (!lineText.empty() && lineText.back() == '\r') {
            lineText.pop_back();
        }

        std::array<int16_t, kChannelCount> parsedFrameValueList{};
        if (!lineText.empty() && parseFrameLine(lineText, parsedFrameValueList)) {
            latestFrameValueList = parsedFrameValueList;
            hasValidFrame = true;
        }
        lineEndIndex = receiveBuffer.find('\n');
    }

    return hasValidFrame ? ReadFrameResult::HasFrame : ReadFrameResult::NoFrame;
}

}  // namespace

SerialFrameReceiver::SerialFrameReceiver() {
    const auto initialTimePoint = std::chrono::steady_clock::now();
    lastSearchLogTimePoint_ = initialTimePoint - std::chrono::milliseconds(searchLogIntervalMs);
    lastNoFrameLogTimePoint_ = initialTimePoint;
    lastValidFrameTimePoint_ = initialTimePoint;
}

SerialFrameReceiver::~SerialFrameReceiver() {
    closePort();
}

void SerialFrameReceiver::closePort() {
    if (serialHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(serialHandle_);
        serialHandle_ = INVALID_HANDLE_VALUE;
    }
    currentPortNameText_.clear();
    receiveBuffer_.clear();
    hasLoggedFirstFrame_ = false;
}

SerialPollResult SerialFrameReceiver::poll(std::array<int16_t, kChannelCount>& latestFrameValueList) {
    SerialPollResult pollResult;
    const auto currentTimePoint = std::chrono::steady_clock::now();

    if (serialHandle_ == INVALID_HANDLE_VALUE) {
        const std::wstring matchedPortName = findMatchedPortName(hardwareIdText);
        if (matchedPortName.empty()) {
            if (std::chrono::duration_cast<std::chrono::milliseconds>(currentTimePoint - lastSearchLogTimePoint_).count() >= searchLogIntervalMs) {
                pollResult.hasStatusMessage = true;
                pollResult.statusMessage = "搜索状态: 尚未找到目标硬件，继续轮询中...";
                lastSearchLogTimePoint_ = currentTimePoint;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(reconnectDelayMs));
            return pollResult;
        }

        serialHandle_ = openSerialPort(matchedPortName, baudRateValue);
        if (serialHandle_ == INVALID_HANDLE_VALUE) {
            pollResult.hasStatusMessage = true;
            pollResult.statusMessage = "打开串口失败: " + toUtf8String(matchedPortName);
            std::this_thread::sleep_for(std::chrono::milliseconds(reconnectDelayMs));
            return pollResult;
        }

        currentPortNameText_ = toUtf8String(matchedPortName);
        receiveBuffer_.clear();
        hasLoggedFirstFrame_ = false;
        lastNoFrameLogTimePoint_ = currentTimePoint;
        lastValidFrameTimePoint_ = currentTimePoint;
        pollResult.hasStatusMessage = true;
        pollResult.statusMessage = "已连接硬件: " + currentPortNameText_;
        return pollResult;
    }

    const ReadFrameResult readResult = tryReadLatestFrame(serialHandle_, receiveBuffer_, latestFrameValueList);
    if (readResult == ReadFrameResult::Disconnected) {
        closePort();
        pollResult.hasStatusMessage = true;
        pollResult.statusMessage = "串口已断开，正在重连...";
        std::this_thread::sleep_for(std::chrono::milliseconds(reconnectDelayMs));
        return pollResult;
    }

    if (readResult == ReadFrameResult::HasFrame) {
        lastValidFrameTimePoint_ = currentTimePoint;
        lastNoFrameLogTimePoint_ = currentTimePoint;
        pollResult.hasFrame = true;
        if (!hasLoggedFirstFrame_) {
            pollResult.hasStatusMessage = true;
            pollResult.statusMessage = "已收到首帧有效 AD 数据。";
            hasLoggedFirstFrame_ = true;
        }
        return pollResult;
    }

    if (std::chrono::duration_cast<std::chrono::milliseconds>(currentTimePoint - lastValidFrameTimePoint_).count() >= noFrameLogIntervalMs &&
        std::chrono::duration_cast<std::chrono::milliseconds>(currentTimePoint - lastNoFrameLogTimePoint_).count() >= noFrameLogIntervalMs) {
        std::ostringstream logStream;
        logStream
            << "串口状态: " << currentPortNameText_
            << " 已连接，但已连续 "
            << std::chrono::duration_cast<std::chrono::milliseconds>(currentTimePoint - lastValidFrameTimePoint_).count()
            << "ms 未收到有效帧，缓存字节数=" << receiveBuffer_.size();
        pollResult.hasStatusMessage = true;
        pollResult.statusMessage = logStream.str();
        lastNoFrameLogTimePoint_ = currentTimePoint;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(idleSleepMs));
    return pollResult;
}

std::string SerialFrameReceiver::getTargetHardwareIdText() const {
    return toUtf8String(hardwareIdText);
}

}  // namespace handdemo
