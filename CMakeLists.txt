cmake_minimum_required(VERSION 3.10)
project(multiproc)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set static linking only on Linux
if(UNIX AND NOT APPLE)
    set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
endif()

find_package(Curses REQUIRED)
add_executable(multiproc multiproc.cpp)
target_link_libraries(multiproc ${CURSES_LIBRARIES})