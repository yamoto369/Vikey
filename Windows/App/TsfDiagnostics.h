#pragma once

#include <windows.h>
#include <string>
#include <cwchar>

namespace vikey { namespace tsf { namespace diagnostics {

inline std::wstring logFilePath() {
    wchar_t exePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (!length || length >= MAX_PATH) return {};
    std::wstring directory(exePath, length);
    const size_t slash = directory.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    directory.resize(slash);
    directory += L"\\Logs";
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return {};
    directory += L"\\tsf.log";
    return directory;
}

inline void write(const wchar_t* message) {
    const std::wstring path = logFilePath();
    if (path.empty()) return;
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t line[2048] = {};
    const int chars = swprintf_s(line, _countof(line),
        L"%04u-%02u-%02u %02u:%02u:%02u.%03u %s\r\n",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
        time.wSecond, time.wMilliseconds, message ? message : L"");
    if (chars > 0) {
        const int bytesNeeded = WideCharToMultiByte(CP_UTF8, 0, line, chars,
            nullptr, 0, nullptr, nullptr);
        if (bytesNeeded > 0) {
            std::string utf8(static_cast<size_t>(bytesNeeded), '\0');
            WideCharToMultiByte(CP_UTF8, 0, line, chars,
                &utf8[0], bytesNeeded, nullptr, nullptr);
            DWORD written = 0;
            WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        }
    }
    CloseHandle(file);
}

inline void result(const wchar_t* stage, HRESULT hr, DWORD win32Error = 0) {
    wchar_t message[512] = {};
    swprintf_s(message, _countof(message), L"%s: HRESULT=0x%08lX, Win32=%lu",
        stage, static_cast<unsigned long>(hr), static_cast<unsigned long>(win32Error));
    write(message);
}

}}} // namespace vikey::tsf::diagnostics
