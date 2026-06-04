cmake_minimum_required(VERSION 3.20)
project(PWNZ_AI)

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
# Пути по умолчанию для Windows (можно переопределить через -D при запуске cmake)
set(ONNX_DIR "C:/BOGX_DEV/ONNX_1_18" CACHE PATH "Path to ONNX Runtime")
set(IMGUI_DIR "C:/BOGX_DEV/Imgui" CACHE PATH "Path to ImGui")
set(BYTE_TRACK_DIR "C:/BOGX_DEV/byte-track-eigen-main" CACHE PATH "Path to ByteTrack")
set(EIGEN_DIR "C:/BOGX_DEV/eigen-master" CACHE PATH "Path to Eigen")
set(HIDAPI_DIR "C:/BOGX_DEV/hid_api" CACHE PATH "Path to HIDAPI")
set(MAKCU_DIR "${CMAKE_SOURCE_DIR}/../macku2pc" CACHE PATH "Path to Makcu C++ library source")

# Проверка существования директории makcu2pc
if(NOT EXISTS "${MAKCU_DIR}")
    message(FATAL_ERROR "Makcu library directory not found at ${MAKCU_DIR}. Please ensure macku2pc folder exists in parent directory.")
endif()

include_directories("${CMAKE_SOURCE_DIR}")
include_directories("${ONNX_DIR}/include")
include_directories("${IMGUI_DIR}")
include_directories("${BYTE_TRACK_DIR}/include")
include_directories("${EIGEN_DIR}")
include_directories("${HIDAPI_DIR}/include")
include_directories("${MAKCU_DIR}/include")

# [ДОБАВЛЕНО] Исходники Makcu2PC (компилируем библиотеку из исходников)
set(MAKCU_SOURCES 
    "${MAKCU_DIR}/src/makcu.cpp"
    "${MAKCU_DIR}/src/makcu_c.cpp"
    "${MAKCU_DIR}/src/serialport.cpp"
)

# [ДОБАВЛЕНО] Исходники ByteTrack-Eigen (KalmanFilter, STrack, BYTETracker, lapjv и т.д.)
file(GLOB BYTE_TRACK_SOURCES "${BYTE_TRACK_DIR}/src/*.cpp")

add_executable(PWNZ_AI WIN32
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
    "MakcuWrapper.h"
    "MakcuWrapper.cpp"
    "KMBoxNet.h"
    "KMBoxNet.cpp"
    "HardwareBackend.h"
    "HardwareBackend.cpp"
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_impl_dx11.cpp"
    "${IMGUI_DIR}/imgui_impl_win32.cpp"
    "${IMGUI_DIR}/implot.cpp"
    "${IMGUI_DIR}/implot_items.cpp"
    ${BYTE_TRACK_SOURCES}
    ${MAKCU_SOURCES}
)

target_link_libraries(PWNZ_AI PRIVATE
    d3d11.lib dxgi.lib d3dcompiler.lib dwmapi.lib setupapi.lib user32.lib gdi32.lib
    windowsapp.lib ws2_32.lib
    "${ONNX_DIR}/lib/onnxruntime.lib"
    "${ONNX_DIR}/lib/DirectML.lib"
    "${CMAKE_SOURCE_DIR}/VMProtectSDK64.lib"
    "${HIDAPI_DIR}/x64/hidapi.lib"
)

set_target_properties(PWNZ_AI PROPERTIES LINK_FLAGS "/DEBUG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS")

add_custom_command(TARGET PWNZ_AI POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ONNX_DIR}/lib/onnxruntime.dll" $<TARGET_FILE_DIR:PWNZ_AI>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ONNX_DIR}/lib/onnxruntime_providers_shared.dll" $<TARGET_FILE_DIR:PWNZ_AI>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ONNX_DIR}/lib/DirectML.dll" $<TARGET_FILE_DIR:PWNZ_AI>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${HIDAPI_DIR}/x64/hidapi.dll" $<TARGET_FILE_DIR:PWNZ_AI>
)
