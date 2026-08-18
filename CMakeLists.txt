cmake_minimum_required(VERSION 3.16)
project(VirtualDubQt VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui Multimedia DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat>=60
    libavcodec>=60
    libavutil>=58
    libswscale>=7
    libswresample>=4
)

find_path(AVISYNTH_INCLUDE_DIR
    NAMES avisynth/avisynth_c.h
    HINTS "$ENV{HOME}/.local/include"
)
find_library(AVISYNTH_LIBRARY
    NAMES avisynth
    HINTS "$ENV{HOME}/.local/lib64" "$ENV{HOME}/.local/lib"
)
if (NOT AVISYNTH_INCLUDE_DIR OR NOT AVISYNTH_LIBRARY)
    message(FATAL_ERROR "AviSynth+ development headers and library were not found")
endif()
get_filename_component(AVISYNTH_LIBRARY_DIR "${AVISYNTH_LIBRARY}" DIRECTORY)

set(VIRTUALDUB_QT_SOURCES
    src/VirtualDub/VDQtFrameDecodeWorker.h
    src/VirtualDub/VDQtFrameDecodeWorker.cpp
    src/VirtualDub/VDQtVideoDecoder.h
    src/VirtualDub/VDQtVideoDecoder.cpp
    src/VirtualDub/VDQtAudioPlayer.h
    src/VirtualDub/VDQtAudioPlayer.cpp
    src/VirtualDub/VDQtFilterSystem.h
    src/VirtualDub/VDQtFilterSystem.cpp
    src/VirtualDub/VDQtCodecSettings.h
    src/VirtualDub/VDQtCodecSettings.cpp
    src/VirtualDub/VDQtCodecEngine.h
    src/VirtualDub/VDQtCodecEngine.cpp
    src/VirtualDub/VDQtVideoExporter.h
    src/VirtualDub/VDQtVideoExporter.cpp
    src/VirtualDub/VDQtVideoDisplay.h
    src/VirtualDub/VDQtVideoDisplay.cpp
    src/VirtualDub/VDQtPositionControl.h
    src/VirtualDub/VDQtPositionControl.cpp
    src/VirtualDub/VDQtDialogs.h
    src/VirtualDub/VDQtDialogs.cpp
    src/VirtualDub/VDQtMainWindow.h
    src/VirtualDub/VDQtMainWindow.cpp
)

add_executable(VirtualDubQt
    src/main.cpp
    ${VIRTUALDUB_QT_SOURCES}
)
target_compile_definitions(VirtualDubQt PRIVATE QT_NO_KEYWORDS)
target_include_directories(VirtualDubQt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${AVISYNTH_INCLUDE_DIR}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(VirtualDubQt PRIVATE
        -Wall -Wextra -Wpedantic -Wreturn-type -Wnull-dereference
    )
endif()
option(ENABLE_SANITIZERS "Build VirtualDubQt with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if (ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(VirtualDubQt PRIVATE -fno-omit-frame-pointer -fsanitize=address,undefined)
    target_link_options(VirtualDubQt PRIVATE -fno-omit-frame-pointer -fsanitize=address,undefined)
endif()

target_link_libraries(VirtualDubQt PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    Qt6::Multimedia
    Qt6::DBus
    ${AVISYNTH_LIBRARY}
    PkgConfig::FFMPEG
)
set_target_properties(VirtualDubQt PROPERTIES BUILD_RPATH "${AVISYNTH_LIBRARY_DIR}")

# Compatibility alias for build scripts
add_custom_target(VirtualDub_Port DEPENDS VirtualDubQt)

if (BUILD_TESTING)
    add_executable(stabilization_tests
        src/VirtualDub/test_stabilization.cpp
        src/VirtualDub/VDQtFrameDecodeWorker.cpp
        src/VirtualDub/VDQtFrameDecodeWorker.h
        src/VirtualDub/VDQtAudioPlayer.cpp
        src/VirtualDub/VDQtAudioPlayer.h
        src/VirtualDub/VDQtCodecEngine.cpp
        src/VirtualDub/VDQtCodecEngine.h
        src/VirtualDub/VDQtCodecSettings.cpp
        src/VirtualDub/VDQtCodecSettings.h
        src/VirtualDub/VDQtFilterSystem.cpp
        src/VirtualDub/VDQtFilterSystem.h
        src/VirtualDub/VDQtPositionControl.cpp
        src/VirtualDub/VDQtPositionControl.h
        src/VirtualDub/VDQtVideoDecoder.cpp
        src/VirtualDub/VDQtVideoDecoder.h
    )
    target_compile_definitions(stabilization_tests PRIVATE
        QT_NO_KEYWORDS
        VDQT_AUDIO_TESTING=1
    )
    target_include_directories(stabilization_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${AVISYNTH_INCLUDE_DIR}
    )
    target_link_libraries(stabilization_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Multimedia
        ${AVISYNTH_LIBRARY}
        PkgConfig::FFMPEG
    )
    if (ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(stabilization_tests PRIVATE
            -fno-omit-frame-pointer -fsanitize=address,undefined)
        target_link_options(stabilization_tests PRIVATE
            -fno-omit-frame-pointer -fsanitize=address,undefined)
    endif()
    set_target_properties(stabilization_tests PROPERTIES BUILD_RPATH "${AVISYNTH_LIBRARY_DIR}")
    add_test(NAME stabilization_tests COMMAND stabilization_tests)
endif()
