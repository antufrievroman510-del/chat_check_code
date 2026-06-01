#pragma once

// Защита от конфликтов Winsock - должно быть ДО любых других заголовков
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

// Критически важно: определить это перед windows.h чтобы предотвратить автоматическое включение winsock.h
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

// Сначала Windows API
#include <windows.h>

// Теперь безопасно включаем Winsock2 (после windows.h с защитой _WINSOCKAPI_)
#include <winsock2.h>

// Затем дополнительные сетевые заголовки
#include <ws2tcpip.h>

// Стандартные типы для сети
#include <cstdint>
#include <string>
