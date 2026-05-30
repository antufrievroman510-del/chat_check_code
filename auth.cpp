#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <sstream>
#include <vector>
#include <iomanip>

#include "auth.h"
#include "xorstr.hpp" 
#include "protect.h"  
#include "VMProtectSDK.h" // [ДОБАВЛЕНО]: Заголовок SDK

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")

std::string DecodeUnicodeEscapes(const std::string& input) {
    std::string output;
    for (size_t i = 0; i < input.length(); ) {
        if (input[i] == '\\' && i + 1 < input.length() && input[i + 1] == 'u' && i + 5 < input.length()) {
            std::string hex_str = input.substr(i + 2, 4);
            try {
                int codepoint = std::stoi(hex_str, nullptr, 16);
                if (codepoint <= 0x7F) {
                    output += static_cast<char>(codepoint);
                }
                else if (codepoint <= 0x7FF) {
                    output += static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
                    output += static_cast<char>(0x80 | (codepoint & 0x3F));
                }
                else {
                    output += static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
                    output += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                    output += static_cast<char>(0x80 | (codepoint & 0x3F));
                }
            }
            catch (...) { output += "\\u" + hex_str; }
            i += 6;
        }
        else {
            output += input[i];
            i++;
        }
    }
    return output;
}

bool IsSafeEnvironment() {
    return true;
}

std::string GetHWID() {
    // [ВНЕДРЕНО]: Максимальная защита генерации HWID
    VMProtectBeginUltra("GetHWID");

    static std::string cached_hwid = "";

    if (!cached_hwid.empty()) {
        VMProtectEnd(); // Закрываем маркер перед выходом
        return cached_hwid;
    }

    MUTATE_SIGNATURE;

    std::string base_prefix = XOR("PWNZ-");

    DWORD volSerial = 0;
    GetVolumeInformationA(XOR("C:\\"), NULL, 0, &volSerial, NULL, NULL, NULL, 0);

    char compName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(compName);
    GetComputerNameA(compName, &size);

    std::stringstream ss;
    ss << std::hex << std::uppercase << volSerial << XOR("-") << compName;

    cached_hwid = base_prefix + ss.str();

    VMProtectEnd(); // Закрываем маркер
    return cached_hwid;
}

std::string SendAuthRequest(const std::string& username, const std::string& password, bool is_register, std::string& out_expiry) {
    // [ВНЕДРЕНО]: Защита сетевых запросов к серверу лицензий
    VMProtectBeginUltra("SendAuthRequest");

    MUTATE_SIGNATURE;

    if (!IsSafeEnvironment()) {
        VMProtectEnd();
        return reinterpret_cast<const char*>(u8"ОШИБКА: Обнаружен отладчик");
    }

    std::string hwid = GetHWID();
    std::string json_data = XOR("{\"username\":\"") + username + XOR("\",\"password\":\"") + password + XOR("\",\"hwid\":\"") + hwid + XOR("\"}");

    HMODULE hWinHttp = LoadLibraryA(XOR("winhttp.dll"));
    if (!hWinHttp) {
        VMProtectEnd();
        return reinterpret_cast<const char*>(u8"ОШИБКА: Системная библиотека WinHTTP не найдена");
    }

    typedef HINTERNET(WINAPI* WinHttpOpenFn)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
    typedef HINTERNET(WINAPI* WinHttpConnectFn)(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD);
    typedef HINTERNET(WINAPI* WinHttpOpenRequestFn)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD);
    typedef BOOL(WINAPI* WinHttpSendRequestFn)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
    typedef BOOL(WINAPI* WinHttpReceiveResponseFn)(HINTERNET, LPVOID);
    typedef BOOL(WINAPI* WinHttpQueryDataAvailableFn)(HINTERNET, LPDWORD);
    typedef BOOL(WINAPI* WinHttpReadDataFn)(HINTERNET, LPVOID, DWORD, LPDWORD);
    typedef BOOL(WINAPI* WinHttpCloseHandleFn)(HINTERNET);
    typedef BOOL(WINAPI* WinHttpSetTimeoutsFn)(HINTERNET, int, int, int, int);

    auto pWinHttpOpen = (WinHttpOpenFn)GetProcAddress(hWinHttp, XOR("WinHttpOpen"));
    auto pWinHttpConnect = (WinHttpConnectFn)GetProcAddress(hWinHttp, XOR("WinHttpConnect"));
    auto pWinHttpOpenRequest = (WinHttpOpenRequestFn)GetProcAddress(hWinHttp, XOR("WinHttpOpenRequest"));
    auto pWinHttpSendRequest = (WinHttpSendRequestFn)GetProcAddress(hWinHttp, XOR("WinHttpSendRequest"));
    auto pWinHttpReceiveResponse = (WinHttpReceiveResponseFn)GetProcAddress(hWinHttp, XOR("WinHttpReceiveResponse"));
    auto pWinHttpQueryDataAvailable = (WinHttpQueryDataAvailableFn)GetProcAddress(hWinHttp, XOR("WinHttpQueryDataAvailable"));
    auto pWinHttpReadData = (WinHttpReadDataFn)GetProcAddress(hWinHttp, XOR("WinHttpReadData"));
    auto pWinHttpCloseHandle = (WinHttpCloseHandleFn)GetProcAddress(hWinHttp, XOR("WinHttpCloseHandle"));
    auto pWinHttpSetTimeouts = (WinHttpSetTimeoutsFn)GetProcAddress(hWinHttp, XOR("WinHttpSetTimeouts"));

    if (!pWinHttpOpen || !pWinHttpConnect || !pWinHttpOpenRequest || !pWinHttpSendRequest) {
        FreeLibrary(hWinHttp);
        VMProtectEnd();
        return reinterpret_cast<const char*>(u8"ОШИБКА: Сбой API авторизации");
    }

    HINTERNET hSession = pWinHttpOpen(L"PWNZ_Auth/2.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { FreeLibrary(hWinHttp); VMProtectEnd(); return reinterpret_cast<const char*>(u8"ОШИБКА: Нет доступа к интернету"); }

    if (pWinHttpSetTimeouts) {
        pWinHttpSetTimeouts(hSession, 15000, 15000, 60000, 60000);
    }

    HINTERNET hConnect = pWinHttpConnect(hSession, L"pwnz-auth.onrender.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    LPCWSTR endpoint = is_register ? L"/api/register" : L"/api/auth";
    HINTERNET hRequest = pWinHttpOpenRequest(hConnect, L"POST", endpoint, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

    bool bResults = false;
    if (hRequest) {
        std::wstring headers = L"Content-Type: application/json\r\n";
        bResults = pWinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(), (LPVOID)json_data.c_str(), (DWORD)json_data.length(), (DWORD)json_data.length(), 0);
    }

    if (bResults) bResults = pWinHttpReceiveResponse(hRequest, NULL);

    std::string response_str = "";
    if (bResults) {
        DWORD dwSize = 0; DWORD dwDownloaded = 0; LPSTR pszOutBuffer;
        do {
            dwSize = 0;
            if (!pWinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (dwSize == 0) break;
            pszOutBuffer = new char[dwSize + 1]; ZeroMemory(pszOutBuffer, dwSize + 1);
            if (pWinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded)) response_str += pszOutBuffer;
            delete[] pszOutBuffer;
        } while (dwSize > 0);
    }
    else {
        DWORD errCode = GetLastError();
        response_str = reinterpret_cast<const char*>(u8"ОШИБКА СЕТИ (Код: ") + std::to_string(errCode) + ")";
    }

    if (hRequest) pWinHttpCloseHandle(hRequest);
    if (hConnect) pWinHttpCloseHandle(hConnect);
    if (hSession) pWinHttpCloseHandle(hSession);
    FreeLibrary(hWinHttp);

    if (response_str.find(XOR("\"SUCCESS\"")) != std::string::npos) {
        size_t exp_pos = response_str.find(XOR("\"expiry\""));
        if (exp_pos != std::string::npos) {
            size_t start = response_str.find(XOR("\""), exp_pos + 8);
            if (start != std::string::npos) {
                size_t end = response_str.find(XOR("\""), start + 1);
                if (end != std::string::npos) {
                    out_expiry = response_str.substr(start + 1, end - start - 1);
                }
            }
        }
        VMProtectEnd();
        return XOR("SUCCESS");
    }
    else {
        size_t msg_pos = response_str.find(XOR("\"msg\""));
        if (msg_pos != std::string::npos) {
            size_t start = response_str.find(XOR("\""), msg_pos + 6);
            if (start != std::string::npos) {
                size_t end = response_str.find(XOR("\""), start + 1);
                if (end != std::string::npos) {
                    std::string raw_err = response_str.substr(start + 1, end - start - 1);
                    VMProtectEnd();
                    return DecodeUnicodeEscapes(raw_err);
                }
            }
        }
    }

    if (!bResults) {
        VMProtectEnd();
        return response_str;
    }

    VMProtectEnd();
    return reinterpret_cast<const char*>(u8"Ошибка авторизации сервера");
}