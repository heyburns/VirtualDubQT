cmake_minimum_required(VERSION 3.16)
project(VirtualDubQt VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui Multimedia DBus Concurrent)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat>=60
    libavcodec>=60
    libavutil>=58
    libswscale>=7
    libswresample>=4
    libavfilter>=9
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
    src/VirtualDub/VDQtAudioFilterSystem.h
    src/VirtualDub/VDQtAudioFilterSystem.cpp
    src/VirtualDub/VDQtFilterSystem.h
    src/VirtualDub/VDQtFilterSystem.cpp
    src/VirtualDub/VDQtPluginHost.h
    src/VirtualDub/VDQtPluginHost.cpp
    src/VirtualDub/VDQtScriptEngine.h
    src/VirtualDub/VDQtScriptEngine.cpp
    src/VirtualDub/VDQtCodecSettings.h
    src/VirtualDub/VDQtCodecSettings.cpp
    src/VirtualDub/VDQtCodecEngine.h
    src/VirtualDub/VDQtCodecEngine.cpp
    src/VirtualDub/VDQtVideoExporter.h
    src/VirtualDub/VDQtVideoExporter.cpp
    src/VirtualDub/VDQtSourceSafety.h
    src/VirtualDub/VDQtSourceSafety.cpp
    src/VirtualDub/VDQtTimeline.h
    src/VirtualDub/VDQtTimeline.cpp
    src/VirtualDub/VDQtProjectFile.h
    src/VirtualDub/VDQtProjectFile.cpp
    src/VirtualDub/VDQtJobQueue.h
    src/VirtualDub/VDQtJobQueue.cpp
    src/VirtualDub/VDQtJobControl.h
    src/VirtualDub/VDQtJobControl.cpp
    src/VirtualDub/VDQtBatchWizard.h
    src/VirtualDub/VDQtBatchWizard.cpp
    src/VirtualDub/VDQtFrameServer.h
    src/VirtualDub/VDQtFrameServer.cpp
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

option(ENABLE_SANITIZERS "Build VirtualDubQt with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

function(vdqt_enable_diagnostics target_name)
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE
            -Wall -Wextra -Wpedantic -Wreturn-type -Wnull-dereference
        )
    endif()
    if (ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE
            -fno-omit-frame-pointer -fsanitize=address,undefined
        )
        target_link_options(${target_name} PRIVATE
            -fno-omit-frame-pointer -fsanitize=address,undefined
        )
    endif()
endfunction()

vdqt_enable_diagnostics(VirtualDubQt)

target_link_libraries(VirtualDubQt PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    Qt6::Multimedia
    Qt6::DBus
    Qt6::Concurrent
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
        src/VirtualDub/VDQtAudioFilterSystem.cpp
        src/VirtualDub/VDQtAudioFilterSystem.h
        src/VirtualDub/VDQtCodecEngine.cpp
        src/VirtualDub/VDQtCodecEngine.h
        src/VirtualDub/VDQtCodecSettings.cpp
        src/VirtualDub/VDQtCodecSettings.h
        src/VirtualDub/VDQtFilterSystem.cpp
        src/VirtualDub/VDQtFilterSystem.h
        src/VirtualDub/VDQtPluginHost.cpp
        src/VirtualDub/VDQtPluginHost.h
        src/VirtualDub/VDQtScriptEngine.cpp
        src/VirtualDub/VDQtScriptEngine.h
        src/VirtualDub/VDQtVideoExporter.cpp
        src/VirtualDub/VDQtVideoExporter.h
        src/VirtualDub/VDQtSourceSafety.cpp
        src/VirtualDub/VDQtSourceSafety.h
        src/VirtualDub/VDQtTimeline.cpp
        src/VirtualDub/VDQtTimeline.h
        src/VirtualDub/VDQtProjectFile.cpp
        src/VirtualDub/VDQtProjectFile.h
        src/VirtualDub/VDQtJobQueue.cpp
        src/VirtualDub/VDQtJobQueue.h
        src/VirtualDub/VDQtJobControl.cpp
        src/VirtualDub/VDQtJobControl.h
        src/VirtualDub/VDQtBatchWizard.cpp
        src/VirtualDub/VDQtBatchWizard.h
        src/VirtualDub/VDQtFrameServer.cpp
        src/VirtualDub/VDQtFrameServer.h
        src/VirtualDub/VDQtDialogs.cpp
        src/VirtualDub/VDQtDialogs.h
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
        Qt6::Concurrent
        ${AVISYNTH_LIBRARY}
        PkgConfig::FFMPEG
    )
    vdqt_enable_diagnostics(stabilization_tests)
    set_target_properties(stabilization_tests PROPERTIES BUILD_RPATH "${AVISYNTH_LIBRARY_DIR}")
    add_test(NAME stabilization_tests COMMAND stabilization_tests)

    add_executable(playback_performance_tests
        src/VirtualDub/test_playback_performance.cpp
        src/VirtualDub/VDQtVideoDecoder.cpp
        src/VirtualDub/VDQtVideoDecoder.h
    )
    target_compile_definitions(playback_performance_tests PRIVATE QT_NO_KEYWORDS)
    target_include_directories(playback_performance_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${AVISYNTH_INCLUDE_DIR}
    )
    target_link_libraries(playback_performance_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        ${AVISYNTH_LIBRARY}
        PkgConfig::FFMPEG
    )
    vdqt_enable_diagnostics(playback_performance_tests)
    set_target_properties(playback_performance_tests PROPERTIES
        BUILD_RPATH "${AVISYNTH_LIBRARY_DIR}"
    )
    add_test(NAME playback_performance_tests COMMAND playback_performance_tests)
    set_tests_properties(playback_performance_tests PROPERTIES
        LABELS "performance;playback"
        TIMEOUT 45
    )

    add_executable(audio_backend_tests
        src/VirtualDub/test_audio_backend.cpp
        src/VirtualDub/VDQtAudioPlayer.cpp
        src/VirtualDub/VDQtAudioPlayer.h
        src/VirtualDub/VDQtAudioFilterSystem.cpp
        src/VirtualDub/VDQtAudioFilterSystem.h
        src/VirtualDub/VDQtVideoDecoder.cpp
        src/VirtualDub/VDQtVideoDecoder.h
    )
    target_compile_definitions(audio_backend_tests PRIVATE QT_NO_KEYWORDS)
    target_include_directories(audio_backend_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${AVISYNTH_INCLUDE_DIR}
    )
    target_link_libraries(audio_backend_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Multimedia
        ${AVISYNTH_LIBRARY}
        PkgConfig::FFMPEG
    )
    vdqt_enable_diagnostics(audio_backend_tests)
    set_target_properties(audio_backend_tests PROPERTIES
        BUILD_RPATH "${AVISYNTH_LIBRARY_DIR}"
    )
    add_test(NAME audio_backend_tests COMMAND audio_backend_tests)
    set_tests_properties(audio_backend_tests PROPERTIES
        LABELS "audio;hardware"
        SKIP_RETURN_CODE 77
        TIMEOUT 15
    )

    add_executable(filter_performance_tests
        src/VirtualDub/test_filter_performance.cpp
        src/VirtualDub/VDQtFilterSystem.cpp
        src/VirtualDub/VDQtFilterSystem.h
        src/VirtualDub/VDQtPluginHost.cpp
        src/VirtualDub/VDQtPluginHost.h
    )
    target_compile_definitions(filter_performance_tests PRIVATE QT_NO_KEYWORDS)
    target_include_directories(filter_performance_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(filter_performance_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Concurrent
    )
    vdqt_enable_diagnostics(filter_performance_tests)
    add_test(NAME filter_performance_tests COMMAND filter_performance_tests)
    set_tests_properties(filter_performance_tests PROPERTIES
        LABELS "performance;filters"
        TIMEOUT 20
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_library(vdqt_test_plugin MODULE
        src/VirtualDub/test_vdx_plugin.cpp
    )
    target_include_directories(vdqt_test_plugin PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    set_target_properties(vdqt_test_plugin PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-plugins"
    )
    vdqt_enable_diagnostics(vdqt_test_plugin)

    add_executable(plugin_host_tests
        src/VirtualDub/test_plugin_host.cpp
        src/VirtualDub/VDQtFilterSystem.cpp
        src/VirtualDub/VDQtFilterSystem.h
        src/VirtualDub/VDQtPluginHost.cpp
        src/VirtualDub/VDQtPluginHost.h
    )
    target_compile_definitions(plugin_host_tests PRIVATE QT_NO_KEYWORDS)
    target_include_directories(plugin_host_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(plugin_host_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Concurrent
    )
    add_dependencies(plugin_host_tests vdqt_test_plugin)
    vdqt_enable_diagnostics(plugin_host_tests)
    add_test(NAME plugin_host_tests COMMAND plugin_host_tests
        $<TARGET_FILE_DIR:vdqt_test_plugin>)
    set_tests_properties(plugin_host_tests PROPERTIES
        LABELS "plugins;compatibility"
        TIMEOUT 20
    )

    add_executable(script_engine_tests
        src/VirtualDub/test_script_engine.cpp
        src/VirtualDub/VDQtScriptEngine.cpp
        src/VirtualDub/VDQtScriptEngine.h
    )
    target_compile_definitions(script_engine_tests PRIVATE QT_NO_KEYWORDS)
    target_link_libraries(script_engine_tests PRIVATE Qt6::Core)
    vdqt_enable_diagnostics(script_engine_tests)
    add_test(NAME script_engine_tests COMMAND script_engine_tests)
    set_tests_properties(script_engine_tests PROPERTIES
        LABELS "scripting;compatibility"
        TIMEOUT 10
    )

    add_executable(parity_contract_tests
        src/VirtualDub/test_parity_contract.cpp
        src/VirtualDub/VDQtScriptEngine.cpp
        src/VirtualDub/VDQtScriptEngine.h
        src/VirtualDub/VDQtTimeline.cpp
        src/VirtualDub/VDQtTimeline.h
        src/VirtualDub/VDQtFilterSystem.cpp
        src/VirtualDub/VDQtFilterSystem.h
        src/VirtualDub/VDQtPluginHost.cpp
        src/VirtualDub/VDQtPluginHost.h
        src/VirtualDub/VDQtAudioFilterSystem.cpp
        src/VirtualDub/VDQtAudioFilterSystem.h
    )
    target_compile_definitions(parity_contract_tests PRIVATE
        QT_NO_KEYWORDS
        VDQT_PARITY_REFERENCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/virtualdub2-reference.json"
    )
    target_include_directories(parity_contract_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(parity_contract_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        PkgConfig::FFMPEG
    )
    vdqt_enable_diagnostics(parity_contract_tests)
    add_test(NAME parity_contract_tests COMMAND parity_contract_tests)
    set_tests_properties(parity_contract_tests PROPERTIES
        LABELS "parity;compatibility;scripting;filters;audio"
        TIMEOUT 15
    )

    add_test(NAME generated_vcf_execution_test
        COMMAND VirtualDubQt
            --script ${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/generated-processing.vcf
            --exit
    )
    set_tests_properties(generated_vcf_execution_test PROPERTIES
        LABELS "parity;compatibility;scripting;integration"
        TIMEOUT 15
        ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-state/data;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-state/config"
    )
endif()
