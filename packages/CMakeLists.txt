cmake_minimum_required(VERSION 3.19)
set(CMAKE_CXX_STANDARD 20)

project(imgui_lib CXX C)

file(GLOB SOURCES "imgui/*.cpp")
list(APPEND SOURCES "imgui/backends/imgui_impl_metal.mm")
add_library(imgui STATIC ${SOURCES})
target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/imgui)


file(GLOB SOURCES "lua/*.c")
add_library(lua STATIC ${SOURCES})
target_include_directories(lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lua)
