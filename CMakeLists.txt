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

# ビルドの版(ウィンドウのタイトルに出す。Application::InitializeImpl)。構成(configure)時に git から取る:
#   MIATA_BUILD_DESCRIBE  コミットの短い SHA。未コミットの変更つきなら末尾に -dirty(git describe --always --dirty)
#   MIATA_BUILD_BRANCH    ブランチ名(HEAD が切り離されていれば detached)
#   MIATA_BUILD_NUMBER    HEAD までのコミット数(CFBundleVersion 用の、数字だけのビルド番号)
#   MIATA_BUILD_LABEL     タイトルに出す文字列: "<SHA>  <ブランチ>  <Debug|Release>"
# rake build:* は、ビルドのたびに cmake を再実行する(Rakefile)ので、そのつど最新になる。make を直接使うと、構成が走らず、
# 前回の値のまま。git が無い・リポジトリでないときは "unknown"。
set(MIATA_BUILD_DESCRIBE "unknown")
set(MIATA_BUILD_BRANCH "")
set(MIATA_BUILD_NUMBER "0")
find_package(Git QUIET)
if (GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --always --dirty --abbrev=7
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE git_result OUTPUT_VARIABLE git_output ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (git_result EQUAL 0 AND NOT git_output STREQUAL "")
        set(MIATA_BUILD_DESCRIBE "${git_output}")
        execute_process(COMMAND "${GIT_EXECUTABLE}" symbolic-ref --short -q HEAD
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE git_result OUTPUT_VARIABLE git_output ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
        if (git_result EQUAL 0 AND NOT git_output STREQUAL "")
            # ブランチ名に &・<・> などが入っていても、Info.plist(XML)を壊さないようにする
            string(REGEX REPLACE "[&<>\"']" "_" MIATA_BUILD_BRANCH "${git_output}")
        else ()
            set(MIATA_BUILD_BRANCH "detached")
        endif ()
        execute_process(COMMAND "${GIT_EXECUTABLE}" rev-list --count HEAD
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE git_result OUTPUT_VARIABLE git_output ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
        if (git_result EQUAL 0 AND git_output MATCHES "^[0-9]+$")
            set(MIATA_BUILD_NUMBER "${git_output}")
        endif ()
    endif ()
endif ()
set(MIATA_BUILD_LABEL "${MIATA_BUILD_DESCRIBE}")
if (NOT MIATA_BUILD_BRANCH STREQUAL "")
    string(APPEND MIATA_BUILD_LABEL "  ${MIATA_BUILD_BRANCH}")
endif ()
if (CMAKE_BUILD_TYPE) # 未定義・空(Xcode など、構成が実行時に決まるもの)なら、足さない
    string(APPEND MIATA_BUILD_LABEL "  ${CMAKE_BUILD_TYPE}")
endif ()
message(STATUS "Build label: ${MIATA_BUILD_LABEL} (build number ${MIATA_BUILD_NUMBER})")

if (APPLE)
    file(GLOB_RECURSE SOURCES "platforms/*.mm" ${SOURCES})

    set(CMAKE_XCODE_GENERATE_SCHEME TRUE)
    set(CMAKE_OSX_ARCHITECTURES "arm64")

    add_executable(app MACOSX_BUNDLE ${SOURCES})
    set_target_properties(app PROPERTIES MACOSX_BUNDLE_BUNDLE_NAME "Miata")
    set_target_properties(app PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER "com.fum1h1ro.miata")
    set_target_properties(app PROPERTIES MACOSX_BUNDLE_ICON_FILE "AppIcon") # resources/AppIcon.icns(rake icon で作る)
    # ビルドの版(上)。CFBundleShortVersionString = SHA(-dirty)、CFBundleVersion = コミット数、CFBundleLongVersionString =
    # タイトルに出す文字列(起動時に pl_get_build_label が NSBundle から読む)。Finder の「情報を見る」とクラッシュレポートにも出る
    set_target_properties(app PROPERTIES MACOSX_BUNDLE_SHORT_VERSION_STRING "${MIATA_BUILD_DESCRIBE}")
    set_target_properties(app PROPERTIES MACOSX_BUNDLE_BUNDLE_VERSION "${MIATA_BUILD_NUMBER}")
    set_target_properties(app PROPERTIES MACOSX_BUNDLE_LONG_VERSION_STRING "${MIATA_BUILD_LABEL}")
    # Info.plist は、CMake の既定のテンプレートに NSAppleEventsUsageDescription を足したもの(Finder に Apple Events で
    # ゴミ箱への移動を頼むときの、許可のダイアログに出る説明。pl_trash_file_via_finder)。resources/ には置かない(.app に同梱されてしまう)
    set_target_properties(app PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in")
    target_compile_options(app PRIVATE -fobjc-arc -fmodules -Wconversion)
    target_link_libraries(app
        "-framework Cocoa"
        "-framework Quartz"
        "-framework WebKit"
        "-framework AudioToolbox"
        "-framework UniformTypeIdentifiers" # ファイル名の頭のアイコン(views/FileIconCache.h)。-fmodules の自動リンクに頼らない
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


