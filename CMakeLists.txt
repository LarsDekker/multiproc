cmake_minimum_required(VERSION 3.10)
project(multiproc)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(Curses REQUIRED)
include_directories(${CURSES_INCLUDE_DIR})

add_executable(multiproc multiproc.cpp)
target_link_libraries(multiproc ${CURSES_LIBRARIES})
