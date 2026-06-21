cmake_minimum_required(VERSION 3.16)

project(ImageTool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt 6.10+ 与 GCC 8.1 的 noexcept 兼容性修复：
# GCC 8.1 对 noexcept(false) lambda 的 noexcept() 求值有 bug，始终返回 true，
# 导致 Q_DECLARE_EQUALITY_COMPARABLE_NON_NOEXCEPT 中的 static_assert 失败。
# 通过定义 Q_CC_GHS 让 Qt 跳过该断言检查（GHS 编译器存在相同 bug）。
add_compile_definitions(
    QT_DISABLE_NOEXCEPT_FOR_EQUALITY
    Q_CC_GHS
)

# Qt and compiler flags for GCC/MinGW
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
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
