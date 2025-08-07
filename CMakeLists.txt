cmake_minimum_required(VERSION 3.10)
project(BlackHoleSimulation)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)


add_library(black_hole_lib
        src/black_hole.cpp
        src/black_hole.h
)

add_executable(black_hole
        src/main.cpp
)

target_link_libraries(black_hole PUBLIC
        OpenGL::GL
        GLEW::GLEW
        glfw
        black_hole_lib
)