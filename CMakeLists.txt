cmake_minimum_required(VERSION 3.16)

project(ImageTool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt and compiler flags for GCC/MinGW
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # Suppress Qt 6 noexcept static_assert issues with GCC 8
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=all -w")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_executable(ImageTool
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/stb_image.h
)

target_include_directories(ImageTool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ImageTool PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
