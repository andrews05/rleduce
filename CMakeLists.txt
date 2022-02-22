cmake_minimum_required(VERSION 3.0)

project(rleduce)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(Graphite EXCLUDE_FROM_ALL)

add_executable(rleduce src/main.cpp)

target_link_libraries(rleduce Graphite)

target_include_directories(rleduce PUBLIC Graphite)
