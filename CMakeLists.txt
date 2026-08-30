cmake_minimum_required(VERSION 3.21)
project(XorFileProcessorQt VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_executable(XorFileProcessor WIN32
    src/main.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/fileprocessor.h
    src/fileprocessor.cpp
)

target_link_libraries(XorFileProcessor PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

target_compile_definitions(XorFileProcessor PRIVATE QT_NO_KEYWORDS)

if (MSVC)
    target_compile_options(XorFileProcessor PRIVATE /W4 /permissive-)
else()
    target_compile_options(XorFileProcessor PRIVATE -Wall -Wextra -Wpedantic)
endif()
