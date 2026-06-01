#pragma once

// Защита от повторного включения и конфликтов Winsock
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

// Сначала winsock2.h, потом windows.h, потом ws2tcpip.h
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

// Стандартные типы для сети
#include <cstdint>
#include <string>
