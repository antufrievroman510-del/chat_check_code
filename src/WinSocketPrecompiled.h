#pragma once

// Этот файл должен подключаться ПЕРВЫМ во всех файлах, использующих сеть или Windows API
// Он предотвращает конфликты имен между Winsock и пользовательским кодом

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX

    // Подключаем winsock2.h ДО windows.h, чтобы избежать конфликта с winsock.h
    #include <winsock2.h>
    #include <ws2tcpip.h>

    // Теперь можно безопасно подключать windows.h
    #include <windows.h>

    // Линковка библиотеки сокетов
    #pragma comment(lib, "Ws2_32.lib")
#else
    // POSIX системы (Linux, macOS)
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    
    // Определяем типы для совместимости с Windows кодом
    #ifndef SOCKET
        #define SOCKET int
    #endif
    #ifndef INVALID_SOCKET
        #define INVALID_SOCKET -1
    #endif
    #ifndef SOCKET_ERROR
        #define SOCKET_ERROR -1
    #endif
    #ifndef closesocket
        #define closesocket close
    #endif
#endif
