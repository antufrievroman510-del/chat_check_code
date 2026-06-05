cmake_minimum_required(VERSION 3.15)
project(PWNZ_Vision_Pro_UDP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Настройки для Windows
if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX)
endif()

# Пути к ImGui
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui_lib)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)

# Бэкенд Win32 + DirectX 9 (стандартный пример из ImGui)
set(IMGUI_BACKENDS
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx9.cpp
)

# Исходные файлы проекта
set(PROJECT_SOURCES
    src/main.cpp
    ${IMGUI_SOURCES}
    ${IMGUI_BACKENDS}
)

# Создаем исполняемый файл
add_executable(PWNZ_Vision_Pro WIN32 ${PROJECT_SOURCES})

# Подключаем заголовочные файлы
target_include_directories(PWNZ_Vision_Pro PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Библиотеки Windows
if(WIN32)
    target_link_libraries(PWNZ_Vision_Pro PRIVATE
        d3d9
        ws2_32
        imm32
    )
endif()

# Копирование config.txt в директорию сборки (если существует)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/config.txt)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.txt ${CMAKE_BINARY_DIR}/config.txt COPYONLY)
endif()

# Установка целевой папки для исполняемого файла
set_target_properties(PWNZ_Vision_Pro PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
