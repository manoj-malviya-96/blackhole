cmake_minimum_required(VERSION 3.10)
project(black_hole)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm CONFIG REQUIRED)

include(FetchContent)
FetchContent_Declare(
        kors_logger
        GIT_REPOSITORY https://github.com/igorkorsukov/kors_logger.git
        GIT_TAG main
)
FetchContent_MakeAvailable(kors_logger)

add_library(blackhole_lib
        src/black_hole.cpp
        src/black_hole.h
        src/shader.cpp
        src/shader.h
)

target_link_libraries(blackhole_lib PUBLIC
        OpenGL::GL
        GLEW::GLEW
        glfw
        glm::glm
        kors_logger
)

target_include_directories(blackhole_lib PUBLIC
        ${kors_logger_SOURCE_DIR}
)

add_executable(blackhole_exe
        src/main.cpp
)

target_link_libraries(blackhole_exe PUBLIC
        blackhole_lib
)

FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(Catch2)
enable_testing()
add_executable(blackhole_test
        src/shader.test.cpp
)
target_link_libraries(blackhole_test PRIVATE
        blackhole_lib
        Catch2::Catch2WithMain
)

add_test(NAME tests COMMAND blackhole_test)
