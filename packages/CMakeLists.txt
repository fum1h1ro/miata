cmake_minimum_required(VERSION 3.19)
set(CMAKE_CXX_STANDARD 20)

project(imgui_lib CXX C)

file(GLOB SOURCES "lua/*.c")
add_library(lua STATIC ${SOURCES})
target_include_directories(lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lua)
