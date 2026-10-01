cmake_minimum_required(VERSION 3.19)

function(generate_compile_flags TARGET_NAME DST_DIR)
    message(STATUS "Generate compile_flags.txt: ${DST_DIR}")
    set(COMPILE_FLAGS "")
    get_target_property(include_dirs ${TARGET_NAME} INCLUDE_DIRECTORIES)
    foreach(dir IN LISTS include_dirs)
        string(APPEND COMPILE_FLAGS "-I${dir}\n")
    endforeach()
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/compile_flags.txt.in" "${DST_DIR}/compile_flags.txt" @ONLY)
endfunction()

project(sokol-app CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CONFIGURATION_TYPES "Debug;Release")

file(GLOB_RECURSE SOURCES "src/*.c" "src/*.cc" "src/*.mm")
file(GLOB_RECURSE RESOURCES "resources/*")

add_subdirectory(packages)

if (APPLE)
    file(GLOB_RECURSE SOURCES "platforms/*.mm" ${SOURCES})

    set(CMAKE_XCODE_GENERATE_SCHEME TRUE)
    set(CMAKE_OSX_ARCHITECTURES "arm64")

    add_executable(app MACOSX_BUNDLE ${SOURCES})
    set_target_properties(app PROPERTIES MACOSX_BUNDLE_BUNDLE_NAME "Miata")
    set_target_properties(app PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER "com.fum1h1ro.miata")
    set_target_properties(app PROPERTIES MACOSX_BUNDLE_ICON_FILE "AppIcon") # resources/AppIcon.icns(rake icon で作る)
    target_compile_options(app PRIVATE -fobjc-arc -fmodules -Wconversion)
    target_link_libraries(app
        "-framework Cocoa"
        "-framework Quartz"
        "-framework AudioToolbox"
    )

    foreach(res IN LISTS RESOURCES)
        target_sources(app PRIVATE ${res})
        set_source_files_properties(${res} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    endforeach()
endif (APPLE)

if (WIN32)
endif (WIN32)

set_target_properties(app PROPERTIES OUTPUT_NAME "Miata")
target_link_libraries(app lua)
target_include_directories(app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/packages/lua
    ${CMAKE_CURRENT_SOURCE_DIR}/packages/RxCpp/Rx/v2/src
)

generate_compile_flags(app ${CMAKE_CURRENT_SOURCE_DIR})

#message(STATUS ${RESOURCES})


