cmake_minimum_required(VERSION 3.16)

project(blackhole_exe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)

add_executable(blackhole_exe
        src/main.cpp
        src/blackhole_widget.h
        src/blackhole_widget.cpp
        src/resources.qrc
)

target_link_libraries(blackhole_exe PRIVATE
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
)

# Resource paths debugging
get_target_property(RCC_SOURCES blackhole_exe AUTORCC_SOURCES)
message(STATUS "AUTORCC Sources: ${RCC_SOURCES}")

if(APPLE)
    set_target_properties(blackhole_exe PROPERTIES MACOSX_BUNDLE TRUE)
endif()