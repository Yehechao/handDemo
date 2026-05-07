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

// hardwareIdText: 对齐 Python 项目 config.json，当前目标硬件固定为 STM32 虚拟串口
constexpr wchar_t hardwareIdText[] = L"USB\\VID_0483&PID_5740";
// baudRateValue: Python 侧没有显式设置波特率，虚拟串口场景这里用常见默认值即可
constexpr DWORD baudRateValue = 9600;
// reconnectDelayMs: 未连接或断开后的重试间隔
constexpr int reconnectDelayMs = 100;
// maxReadCycleCountPerPoll: 每次轮询尽量多读几次，把驱动层已到达的数据尽快吃干净。
constexpr int maxReadCycleCountPerPoll = 32;
// searchLogIntervalMs: 未找到目标串口时的提示节流间隔
constexpr int searchLogIntervalMs = 3000;
// noFrameLogIntervalMs: 连续无有效帧时的提示阈值
constexpr int noFrameLogIntervalMs = 5000;
// frameHeaderByteList: Python 项目里使用的固定二进制帧头，小端字节序为 A5 5A
constexpr char frameHeaderByteList[] = {
    static_cast<char>(0xA5),
    static_cast<char>(0x5A),
};
// expectedFrameTypeValue: 当前协议固定帧类型
constexpr uint8_t expectedFrameTypeValue = 0x01;
// oldDataTypeValue/newDataTypeValue: 当前兼容的两种数据类型
constexpr uint8_t oldDataTypeValue = 0x01;
constexpr uint8_t newDataTypeValue = 0x70;
// pressureSensorByteCount: 压感区总字节数，当前版本先跳过不用
constexpr std::size_t pressureSensorByteCount = 23U * 20U;
// freedomAngleValueCount: 自由度角度总数，最后 1 路仍保留占位
constexpr std::size_t freedomAngleValueCount = 19U;
// rotationAngleValueCount: 姿态角总数，当前版本先跳过不用
constexpr std::size_t rotationAngleValueCount = 4U;
// frameMetaByteCount: 帧头、帧类型、帧长度、数据类型一共 6 字节
constexpr std::size_t frameMetaByteCount = 2U + 1U + 2U + 1U;
// freedomAngleOffset/checksumOffset/frameByteCount: 二进制协议固定布局
constexpr std::size_t freedomAngleOffset = frameMetaByteCount + pressureSensorByteCount;
constexpr std::size_t rotationAngleOffset = freedomAngleOffset + freedomAngleValueCount * sizeof(int16_t);
constexpr std::size_t checksumOffset = rotationAngleOffset + rotationAngleValueCount * sizeof(int16_t);
constexpr std::size_t frameByteCount = checksumOffset + sizeof(uint16_t);
// maxReceiveBufferByteCount: 接收缓冲区上限，避免错误字节流无限增长
constexpr std::size_t maxReceiveBufferByteCount = 65536U;

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

const std::string& getFrameHeaderText() {
    static const std::string frameHeaderText(frameHeaderByteList, sizeof(frameHeaderByteList));
    return frameHeaderText;
}

bool isSupportedDataTypeValue(uint8_t dataTypeValue) {
    return dataTypeValue == oldDataTypeValue || dataTypeValue == newDataTypeValue;
}

uint16_t readUint16LittleEndian(const std::string& bufferText, std::size_t offsetValue) {
    const uint8_t lowByteValue = static_cast<uint8_t>(bufferText[offsetValue]);
    const uint8_t highByteValue = static_cast<uint8_t>(bufferText[offsetValue + 1U]);
    return static_cast<uint16_t>(lowByteValue | (static_cast<uint16_t>(highByteValue) << 8U));
}

int16_t readInt16LittleEndian(const std::string& bufferText, std::size_t offsetValue) {
    return static_cast<int16_t>(readUint16LittleEndian(bufferText, offsetValue));
}

uint16_t computeFrameChecksum(const std::string& frameText) {
    uint32_t checksumValue = 0;
    for (std::size_t byteIndex = 0; byteIndex < checksumOffset; ++byteIndex) {
        checksumValue += static_cast<uint8_t>(frameText[byteIndex]);
    }
    return static_cast<uint16_t>(checksumValue & 0xFFFFU);
}

std::size_t findFrameHeaderIndex(const std::string& receiveBuffer) {
    return receiveBuffer.find(getFrameHeaderText());
}

void trimProtocolReceiveBuffer(std::string& receiveBuffer) {
    if (receiveBuffer.size() <= maxReceiveBufferByteCount) {
        return;
    }

    const std::size_t frameHeaderIndex = receiveBuffer.rfind(getFrameHeaderText());
    if (frameHeaderIndex != std::string::npos) {
        std::string trimmedBuffer = receiveBuffer.substr(frameHeaderIndex);
        if (trimmedBuffer.size() <= maxReceiveBufferByteCount) {
            receiveBuffer.swap(trimmedBuffer);
            return;
        }
    }

    const std::size_t keepByteCount = std::max(frameByteCount * 2U, maxReceiveBufferByteCount / 2U);
    receiveBuffer.erase(0, receiveBuffer.size() - keepByteCount);
}

bool parseProtocolFrame(
    const std::string& frameText,
    bool hasExpectedFrameLengthValue,
    uint16_t expectedFrameLengthValue,
    bool hasExpectedDataTypeValue,
    uint8_t expectedDataTypeValue,
    std::array<int16_t, kChannelCount>& frameValueList,
    uint16_t& parsedFrameLengthValue,
    uint8_t& parsedDataTypeValue) {
    if (frameText.size() != frameByteCount) {
        return false;
    }

    if (frameText.compare(0, getFrameHeaderText().size(), getFrameHeaderText()) != 0) {
        return false;
    }

    if (static_cast<uint8_t>(frameText[2]) != expectedFrameTypeValue) {
        return false;
    }

    parsedFrameLengthValue = readUint16LittleEndian(frameText, 3U);
    if (parsedFrameLengthValue == 0U) {
        return false;
    }
    if (hasExpectedFrameLengthValue && parsedFrameLengthValue != expectedFrameLengthValue) {
        return false;
    }

    parsedDataTypeValue = static_cast<uint8_t>(frameText[5]);
    if (!isSupportedDataTypeValue(parsedDataTypeValue)) {
        return false;
    }
    if (hasExpectedDataTypeValue && parsedDataTypeValue != expectedDataTypeValue) {
        return false;
    }

    const uint16_t actualChecksumValue = readUint16LittleEndian(frameText, checksumOffset);
    if (actualChecksumValue != computeFrameChecksum(frameText)) {
        return false;
    }

    for (std::size_t channelIndex = 0; channelIndex < kChannelCount; ++channelIndex) {
        const int16_t channelValue = readInt16LittleEndian(frameText, freedomAngleOffset + channelIndex * sizeof(int16_t));
        if (channelValue < kAdMinValue || channelValue > kAdMaxValue) {
            return false;
        }
        frameValueList[channelIndex] = channelValue;
    }
    return true;
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

std::string getLastErrorMessageText(DWORD errorCodeValue) {
    LPWSTR wideMessageBuffer = nullptr;
    const DWORD messageLength = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCodeValue,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&wideMessageBuffer),
        0,
        nullptr);
    if (messageLength == 0 || wideMessageBuffer == nullptr) {
        return "未知错误";
    }

    std::wstring wideMessageText(wideMessageBuffer, messageLength);
    LocalFree(wideMessageBuffer);
    while (!wideMessageText.empty()) {
        const wchar_t lastChar = wideMessageText.back();
        if (lastChar == L'\r' || lastChar == L'\n' || lastChar == L' ') {
            wideMessageText.pop_back();
            continue;
        }
        break;
    }
    return toUtf8String(wideMessageText);
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

HANDLE openSerialPort(const std::wstring& portName, DWORD baudRateValue, std::string& errorMessageText) {
    const std::wstring fullPortName = L"\\\\.\\" + portName;
    HANDLE serialHandle = CreateFileW(
        fullPortName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (serialHandle == INVALID_HANDLE_VALUE) {
        const DWORD errorCodeValue = GetLastError();
        errorMessageText =
            "CreateFileW 失败，错误码=" + std::to_string(errorCodeValue) +
            "，原因=" + getLastErrorMessageText(errorCodeValue);
        return INVALID_HANDLE_VALUE;
    }

    DCB deviceControlBlock{};
    deviceControlBlock.DCBlength = sizeof(DCB);
    if (!GetCommState(serialHandle, &deviceControlBlock)) {
        const DWORD errorCodeValue = GetLastError();
        CloseHandle(serialHandle);
        errorMessageText =
            "GetCommState 失败，错误码=" + std::to_string(errorCodeValue) +
            "，原因=" + getLastErrorMessageText(errorCodeValue);
        return INVALID_HANDLE_VALUE;
    }

    deviceControlBlock.BaudRate = baudRateValue;
    deviceControlBlock.ByteSize = 8;
    deviceControlBlock.Parity = NOPARITY;
    deviceControlBlock.StopBits = ONESTOPBIT;
    deviceControlBlock.fBinary = TRUE;
    deviceControlBlock.fParity = FALSE;
    deviceControlBlock.fOutxCtsFlow = FALSE;
    deviceControlBlock.fOutxDsrFlow = FALSE;
    deviceControlBlock.fDtrControl = DTR_CONTROL_ENABLE;
    deviceControlBlock.fDsrSensitivity = FALSE;
    deviceControlBlock.fTXContinueOnXoff = TRUE;
    deviceControlBlock.fOutX = FALSE;
    deviceControlBlock.fInX = FALSE;
    deviceControlBlock.fErrorChar = FALSE;
    deviceControlBlock.fNull = FALSE;
    deviceControlBlock.fRtsControl = RTS_CONTROL_ENABLE;
    deviceControlBlock.fAbortOnError = FALSE;
    if (!SetCommState(serialHandle, &deviceControlBlock)) {
        const DWORD errorCodeValue = GetLastError();
        CloseHandle(serialHandle);
        errorMessageText =
            "SetCommState 失败，错误码=" + std::to_string(errorCodeValue) +
            "，原因=" + getLastErrorMessageText(errorCodeValue);
        return INVALID_HANDLE_VALUE;
    }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 0;
    if (!SetCommTimeouts(serialHandle, &timeouts)) {
        const DWORD errorCodeValue = GetLastError();
        CloseHandle(serialHandle);
        errorMessageText =
            "SetCommTimeouts 失败，错误码=" + std::to_string(errorCodeValue) +
            "，原因=" + getLastErrorMessageText(errorCodeValue);
        return INVALID_HANDLE_VALUE;
    }

    if (!SetupComm(serialHandle, 4096, 4096)) {
        const DWORD errorCodeValue = GetLastError();
        CloseHandle(serialHandle);
        errorMessageText =
            "SetupComm 失败，错误码=" + std::to_string(errorCodeValue) +
            "，原因=" + getLastErrorMessageText(errorCodeValue);
        return INVALID_HANDLE_VALUE;
    }

    if (!PurgeComm(serialHandle, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT)) {
        const DWORD errorCodeValue = GetLastError();
        CloseHandle(serialHandle);
        errorMessageText =
            "PurgeComm 失败，错误码=" + std::to_string(errorCodeValue) +
            "，原因=" + getLastErrorMessageText(errorCodeValue);
        return INVALID_HANDLE_VALUE;
    }

    errorMessageText.clear();
    return serialHandle;
}

ReadFrameResult tryReadLatestFrame(
    HANDLE serialHandle,
    std::string& receiveBuffer,
    bool& hasExpectedFrameLengthValue,
    uint16_t& expectedFrameLengthValue,
    bool& hasExpectedDataTypeValue,
    uint8_t& expectedDataTypeValue,
    std::array<int16_t, kChannelCount>& latestFrameValueList) {
    char tempBuffer[512];
    for (int readCycleIndex = 0; readCycleIndex < maxReadCycleCountPerPoll; ++readCycleIndex) {
        DWORD readByteCount = 0;
        if (!ReadFile(serialHandle, tempBuffer, static_cast<DWORD>(sizeof(tempBuffer)), &readByteCount, nullptr)) {
            return ReadFrameResult::Disconnected;
        }

        if (readByteCount == 0) {
            break;
        }

        receiveBuffer.append(tempBuffer, tempBuffer + readByteCount);
        trimProtocolReceiveBuffer(receiveBuffer);
    }

    bool hasValidFrame = false;
    std::size_t frameHeaderIndex = findFrameHeaderIndex(receiveBuffer);
    while (frameHeaderIndex != std::string::npos) {
        if (frameHeaderIndex > 0U) {
            receiveBuffer.erase(0, frameHeaderIndex);
        }

        if (receiveBuffer.size() < frameByteCount) {
            break;
        }

        std::string candidateFrameText = receiveBuffer.substr(0, frameByteCount);
        std::array<int16_t, kChannelCount> parsedFrameValueList{};
        uint16_t parsedFrameLengthValue = 0;
        uint8_t parsedDataTypeValue = 0;
        if (!parseProtocolFrame(
                candidateFrameText,
                hasExpectedFrameLengthValue,
                expectedFrameLengthValue,
                hasExpectedDataTypeValue,
                expectedDataTypeValue,
                parsedFrameValueList,
                parsedFrameLengthValue,
                parsedDataTypeValue)) {
            receiveBuffer.erase(0, 1);
            frameHeaderIndex = findFrameHeaderIndex(receiveBuffer);
            continue;
        }

        if (!hasExpectedFrameLengthValue) {
            expectedFrameLengthValue = parsedFrameLengthValue;
            hasExpectedFrameLengthValue = true;
        }
        if (!hasExpectedDataTypeValue) {
            expectedDataTypeValue = parsedDataTypeValue;
            hasExpectedDataTypeValue = true;
        }

        latestFrameValueList = parsedFrameValueList;
        hasValidFrame = true;
        receiveBuffer.erase(0, frameByteCount);
        frameHeaderIndex = findFrameHeaderIndex(receiveBuffer);
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
    expectedFrameLengthValue_ = 0;
    hasExpectedFrameLengthValue_ = false;
    expectedDataTypeValue_ = 0;
    hasExpectedDataTypeValue_ = false;
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

        std::string openErrorMessageText;
        serialHandle_ = openSerialPort(matchedPortName, baudRateValue, openErrorMessageText);
        if (serialHandle_ == INVALID_HANDLE_VALUE) {
            pollResult.hasStatusMessage = true;
            pollResult.statusMessage =
                "打开串口失败: " + toUtf8String(matchedPortName) +
                "，" + openErrorMessageText;
            std::this_thread::sleep_for(std::chrono::milliseconds(reconnectDelayMs));
            return pollResult;
        }

        currentPortNameText_ = toUtf8String(matchedPortName);
        receiveBuffer_.clear();
        expectedFrameLengthValue_ = 0;
        hasExpectedFrameLengthValue_ = false;
        expectedDataTypeValue_ = 0;
        hasExpectedDataTypeValue_ = false;
        hasLoggedFirstFrame_ = false;
        lastNoFrameLogTimePoint_ = currentTimePoint;
        lastValidFrameTimePoint_ = currentTimePoint;
        pollResult.hasStatusMessage = true;
        pollResult.statusMessage = "已连接硬件: " + currentPortNameText_;
        return pollResult;
    }

    const ReadFrameResult readResult = tryReadLatestFrame(
        serialHandle_,
        receiveBuffer_,
        hasExpectedFrameLengthValue_,
        expectedFrameLengthValue_,
        hasExpectedDataTypeValue_,
        expectedDataTypeValue_,
        latestFrameValueList);
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

    return pollResult;
}

std::string SerialFrameReceiver::getTargetHardwareIdText() const {
    return toUtf8String(hardwareIdText);
}

}  // namespace handdemo
