cmake_minimum_required(VERSION 3.20)
project(PWNZ_Vision_Pro_UDP VERSION 1.0 LANGUAGES CXX)

# Установка стандарта C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Настройки для Windows
if(WIN32)
    set(CMAKE_WIN32_EXECUTABLE ON)
    add_definitions(-DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS)
endif()

# --- Настройка Dear ImGui ---
# Путь к библиотеке ImGui (должна лежать в папке imgui_lib рядом с этим файлом)
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/imgui_lib")

# Проверка наличия папки ImGui
if(NOT EXISTS "${IMGUI_DIR}/imgui.cpp")
    message(FATAL_ERROR "Библиотека ImGui не найдена в папке: ${IMGUI_DIR}\nПожалуйста, убедитесь, что файлы ImGui загружены в эту папку.")
endif()

# Сбор списка файлов ImGui
file(GLOB IMGUI_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
)

# Файлы бэкенда для DirectX 9 и Win32
file(GLOB IMGUI_BACKENDS
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx9.cpp"
)

# --- Исходные файлы проекта ---
file(GLOB PROJECT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

# Создание исполняемого файла
add_executable(PWNZ_Vision_Pro WIN32
    ${PROJECT_SOURCES}
    ${IMGUI_SOURCES}
    ${IMGUI_BACKENDS}
)

# Добавление путей включения (Include Directories)
target_include_directories(PWNZ_Vision_Pro PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# Линковка необходимых библиотек Windows
target_link_libraries(PWNZ_Vision_Pro PRIVATE
    d3d9        # DirectX 9
    dwmapi      # Desktop Window Manager
    imm32       # Input Method Manager
    ws2_32      # Winsock 2 (для UDP сети)
    ole32       # OLE
    shell32     # Shell API
)

# Копирование config.txt в директорию сборки (если существует)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config.txt")
    add_custom_command(TARGET PWNZ_Vision_Pro POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config.txt"
        $<TARGET_FILE_DIR:PWNZ_Vision_Pro>/config.txt
    )
endif()

# Установка целевой папки для исполняемого файла
set_target_properties(PWNZ_Vision_Pro PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
)
