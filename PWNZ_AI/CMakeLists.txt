cmake_minimum_required(VERSION 3.20)
project(PWNZ_AI)

# --- НАСТРОЙКИ ПЛАТФОРМЫ ---
# Явно указываем набор инструментов v143 (Visual Studio 2022), чтобы избежать ошибки MSB8020 с v145
set(CMAKE_VS_PLATFORM_TOOLSET "v143")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Определяем флаги для MSVC и GCC/Clang
if(MSVC)
    add_compile_options(/std:c++23 /arch:AVX2 /O2 /Zi /permissive-)
    # Отключаем предупреждения для чистоты компиляции
    add_compile_options(/W4 /WX- /wd4267 /wd4244 /wd4996)
else()
    # Для GCC/MinGW в Linux/Wine эти флаги не применяются
    add_compile_options(-std=c++23 -O2 -g -Wall -Wextra)
endif()

# --- ПУТИ К БИБЛИОТЕКАМ ---
# Используем библиотеки из репозитория source_logic для Linux-сборки
set(IMGUI_DIR "/workspace/source_logic/source_logic/modules/imgui-1.92.8" CACHE PATH "Path to ImGui")

# ИСПОЛЬЗУЕМ локальную библиотеку macku2pc внутри проекта PWNZ_AI
set(MAKCU_DIR "${CMAKE_SOURCE_DIR}/macku2pc" CACHE PATH "Path to Makcu C++ library source")

# Проверка существования директории makcu2pc
if(NOT EXISTS "${MAKCU_DIR}")
    message(FATAL_ERROR "Makcu library directory not found at ${MAKCU_DIR}. Please ensure macku2pc folder exists in project root.")
endif()

include_directories("${CMAKE_SOURCE_DIR}")
include_directories("${IMGUI_DIR}")
include_directories("${IMGUI_DIR}/backends")
# Подключаем заголовки Makcu из внутренней папки
include_directories("${MAKCU_DIR}/include")

# --- СОЗДАНИЕ ОТДЕЛЬНОЙ БИБЛИОТЕКИ MAKCU2PC ---
# Собираем исходники Makcu в отдельную статическую библиотеку для изоляции
set(MAKCU_SOURCES 
    "${MAKCU_DIR}/src/makcu.cpp"
    "${MAKCU_DIR}/src/makcu_c.cpp"
    "${MAKCU_DIR}/src/serialport.cpp"
)

add_library(Makcu2PC_Lib STATIC ${MAKCU_SOURCES})
target_include_directories(Makcu2PC_Lib PUBLIC "${MAKCU_DIR}/include")

# Для Linux добавляем необходимые системные библиотеки
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
endif()

# Временная сборка без BYTETracker и ONNX для проверки интеграции Makcu
# Примечание: main.cpp использует BYTETracker.h - нужен стаб или моки
# ImGui файлы берутся из backends подкаталога
add_executable(PWNZ_AI
    "main.cpp"
    "capture.cpp"
    "capture.h"
    "detector.cpp"
    "detector.h"
    "overlay.cpp"
    "overlay.h"
    "aimbot.h"
    "aimbot.cpp"
    "auth.h"
    "auth.cpp"
    "head_smoother.h"
    "aim_kalman.h"
    "AimbotTarget.cpp"
    "AimbotTarget.h"
    "network_2pc.h"
    "AimMath.h"
    "AimMath.cpp"
    "MouseController.h"
    "MouseController.cpp"
    "HardwareController.h"
    "HardwareController.cpp"
    "MakcuInput.h"
    "MakcuInput.cpp"
    "KMBoxNet.h"
    "KMBoxNet.cpp"
    "HardwareBackend.h"
    "HardwareBackend.cpp"
    "IMouseInput.h"
    "SendInputMouse.h"
    "SendInputMouse.cpp"
    "KMboxMouse.h"
    "KMboxMouse.cpp"
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
)

# Подключаем нашу статическую библиотеку Makcu2PC к проекту
target_link_libraries(PWNZ_AI PRIVATE Makcu2PC_Lib)

# Системные библиотеки для Linux
if(UNIX AND NOT APPLE)
    target_link_libraries(PWNZ_AI PRIVATE 
        Threads::Threads
        stdc++
    )
endif()

# Windows библиотеки (для кросс-компиляции или документации)
if(WIN32 OR MSVC)
    target_link_libraries(PWNZ_AI PRIVATE
        d3d11.lib dxgi.lib d3dcompiler.lib dwmapi.lib setupapi.lib user32.lib gdi32.lib
        windowsapp.lib ws2_32.lib
    )
endif()
