cmake_minimum_required(VERSION 3.19)
project(VirtualDubQt VERSION 0.1.1 LANGUAGES CXX)

# Single-configuration builds should not accidentally ship an unoptimized GUI.
# Honor explicit Debug/RelWithDebInfo choices and multi-configuration generators.
# CMake presets are optional and separately require CMake 3.21 or newer.
if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# The native Qt port is intentionally a self-contained target. The repository
# also retains much of the original Windows source as a parity reference, but it
# is not globbed or compiled here; every maintained source is listed explicitly.
include(CTest)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui Multimedia DBus Concurrent)
find_package(PkgConfig REQUIRED)
# Version floors match the modern FFmpeg channel-layout/frame APIs used by the
# decoder, exporter, and audio pipeline. PkgConfig::FFMPEG carries include and
# transitive link flags to every target below.
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

# Application modules are kept in one list so the production target and source
# reviews have a single authoritative inventory. Tests below select the smaller
# subset required for each isolated executable.
set(VIRTUALDUB_QT_SOURCES
    src/VirtualDub/VDQtApplicationIcon.h
    src/VirtualDub/VDQtApplicationIcon.cpp
    src/VirtualDub/VDQtFrameDecodeWorker.h
    src/VirtualDub/VDQtFrameDecodeWorker.cpp
    src/VirtualDub/VDQtVideoDecoder.h
    src/VirtualDub/VDQtVideoDecoder.cpp
    src/VirtualDub/VDQtSourceDependencies.h
    src/VirtualDub/VDQtSourceDependencies.cpp
    src/VirtualDub/VDQtAudioPlayer.h
    src/VirtualDub/VDQtAudioPlayer.cpp
    src/VirtualDub/VDQtAudioExport.h
    src/VirtualDub/VDQtAudioExport.cpp
    src/VirtualDub/VDQtAudioFilterSystem.h
    src/VirtualDub/VDQtAudioFilterSystem.cpp
    src/VirtualDub/VDQtFilterSystem.h
    src/VirtualDub/VDQtFilterFrameContext.h
    src/VirtualDub/VDQtFilterSystem.cpp
    src/VirtualDub/VDQtFilterValidation.h
    src/VirtualDub/VDQtFilterValidation.cpp
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
    src/VirtualDub/VDQtOutputTransaction.h
    src/VirtualDub/VDQtOutputTransaction.cpp
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

# The GUI and its real-controller regressions use exactly the same native
# implementation and compile definitions. Compile that implementation once per
# configuration, not once per executable. A static library introduces no new
# installed runtime dependency. Isolated testing variants below stay separate.
add_library(vdqt_core STATIC ${VIRTUALDUB_QT_SOURCES})
target_compile_definitions(vdqt_core PUBLIC
    QT_NO_KEYWORDS
    VDQT_VERSION_STRING="${PROJECT_VERSION}"
)
target_include_directories(vdqt_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${AVISYNTH_INCLUDE_DIR}
)
add_executable(VirtualDubQt src/main.cpp)
target_link_libraries(VirtualDubQt PRIVATE vdqt_core)

option(ENABLE_SANITIZERS "Build VirtualDubQt with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# Keep warning/sanitizer policy identical across the application and tests.
function(vdqt_enable_diagnostics target_name)
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE
            -Wall -Wextra -Wpedantic -Wreturn-type -Wnull-dereference
        )
    endif()
    if (ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE
            -fno-omit-frame-pointer -fsanitize=address,undefined,float-cast-overflow
        )
        target_link_options(${target_name} PRIVATE
            -fno-omit-frame-pointer -fsanitize=address,undefined,float-cast-overflow
        )
    endif()
endfunction()

vdqt_enable_diagnostics(vdqt_core)
vdqt_enable_diagnostics(VirtualDubQt)

target_link_libraries(vdqt_core PUBLIC
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    Qt6::Multimedia
    Qt6::DBus
    Qt6::Concurrent
    ${AVISYNTH_LIBRARY}
    PkgConfig::FFMPEG
)
set_target_properties(VirtualDubQt PROPERTIES
    BUILD_RPATH "${AVISYNTH_LIBRARY_DIR}"
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/virtualdubqt"
)

# Compatibility alias for build scripts
add_custom_target(VirtualDub_Port DEPENDS VirtualDubQt)

# -----------------------------------------------------------------------------
# Linux installation and CPack release artifacts
# -----------------------------------------------------------------------------

install(TARGETS VirtualDubQt
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(FILES packaging/virtualdubqt.desktop
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
)
install(FILES packaging/virtualdubqt.png
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps"
)
install(FILES LICENSE
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/virtualdubqt"
)

# AviSynth+ does not currently ship in the target Debian and Fedora repositories,
# so include its runtime library while leaving plug-ins under the user's control.
file(REAL_PATH "${AVISYNTH_LIBRARY}" AVISYNTH_REAL_LIBRARY)
if(IS_SYMLINK "${AVISYNTH_LIBRARY}")
    file(READ_SYMLINK "${AVISYNTH_LIBRARY}" AVISYNTH_LIBRARY_LINK_TARGET)
    get_filename_component(AVISYNTH_SONAME "${AVISYNTH_LIBRARY_LINK_TARGET}" NAME)
else()
    get_filename_component(AVISYNTH_SONAME "${AVISYNTH_REAL_LIBRARY}" NAME)
endif()
install(FILES "${AVISYNTH_REAL_LIBRARY}"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/virtualdubqt"
    RENAME "${AVISYNTH_SONAME}"
)

set(CPACK_PACKAGE_NAME "virtualdubqt")
set(CPACK_PACKAGE_VENDOR "VirtualDubQT contributors")
set(CPACK_PACKAGE_CONTACT "Alan Burns <alan.burns@icloud.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Native Linux video capture, processing, and export application"
)
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/heyburns/VirtualDubQT")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "VirtualDubQT")
set(CPACK_STRIP_FILES ON)

set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "video")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")

include(CPack)

# -----------------------------------------------------------------------------
# Regression, performance, ABI, script, and parity test executables
# -----------------------------------------------------------------------------

if (BUILD_TESTING)
    add_executable(playback_clock_tests tests/test_playback_clock.cpp)
    target_include_directories(playback_clock_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    vdqt_enable_diagnostics(playback_clock_tests)
    add_test(NAME playback_clock_tests COMMAND playback_clock_tests)
    set_tests_properties(playback_clock_tests PROPERTIES LABELS "playback;audio;contracts" TIMEOUT 10)

    add_executable(display_contract_tests tests/test_display_contracts.cpp)
    target_link_libraries(display_contract_tests PRIVATE vdqt_core)
    vdqt_enable_diagnostics(display_contract_tests)
    add_test(NAME display_contract_tests COMMAND display_contract_tests)
    set_tests_properties(display_contract_tests PROPERTIES LABELS "display;lifetime;contracts" TIMEOUT 10)
    add_executable(decoder_index_tests
        tests/test_decoder_index.cpp
        tests/support/VDQtTestFixtures.cpp)
    target_link_libraries(decoder_index_tests PRIVATE vdqt_core)
    vdqt_enable_diagnostics(decoder_index_tests)
    add_test(NAME decoder_index_tests COMMAND decoder_index_tests)
    set_tests_properties(decoder_index_tests PROPERTIES
        LABELS "decoder;index;performance" TIMEOUT 60)

    add_executable(timeline_contract_tests
        tests/test_timeline_contract.cpp
        src/VirtualDub/VDQtTimeline.cpp)
    target_include_directories(timeline_contract_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(timeline_contract_tests PRIVATE Qt6::Core)
    vdqt_enable_diagnostics(timeline_contract_tests)
    add_test(NAME timeline_contract_tests COMMAND timeline_contract_tests)
    set_tests_properties(timeline_contract_tests PROPERTIES
        LABELS "timeline;contracts" TIMEOUT 30)

    add_executable(source_safety_tests
        tests/test_source_safety.cpp
        src/VirtualDub/VDQtSourceDependencies.cpp
        src/VirtualDub/VDQtSourceSafety.cpp)
    target_include_directories(source_safety_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(source_safety_tests PRIVATE Qt6::Core)
    vdqt_enable_diagnostics(source_safety_tests)
    add_test(NAME source_safety_tests COMMAND source_safety_tests)
    add_test(NAME source_safety_scaling COMMAND source_safety_tests --scaling-test)
    set_tests_properties(source_safety_tests source_safety_scaling PROPERTIES
        LABELS "filesystem;dependencies" TIMEOUT 120)

    add_executable(output_transaction_tests
        tests/test_output_transaction.cpp
        src/VirtualDub/VDQtOutputTransaction.cpp
        src/VirtualDub/VDQtOutputTransaction.h)
    target_include_directories(output_transaction_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(output_transaction_tests PRIVATE Qt6::Core)
    vdqt_enable_diagnostics(output_transaction_tests)
    add_test(NAME output_transaction_tests COMMAND output_transaction_tests)
    set_tests_properties(output_transaction_tests PROPERTIES LABELS "filesystem;recovery" TIMEOUT 30)

    # Exercise the real controller, including its asynchronous preview worker.
    # Smaller decoder-only suites cannot cover main-window integration failures.
    add_executable(application_baseline_tests
        tests/test_application_baseline.cpp
        tests/VDQtOperationRegressions.h
        tests/VDQtOperationRegressions.cpp
        tests/support/VDQtTestFixtures.h
        tests/support/VDQtTestFixtures.cpp
    )
    target_link_libraries(application_baseline_tests PRIVATE vdqt_core)
    vdqt_enable_diagnostics(application_baseline_tests)
    set_target_properties(application_baseline_tests PROPERTIES
        BUILD_RPATH "${AVISYNTH_LIBRARY_DIR}")
    add_test(NAME application_baseline_tests COMMAND application_baseline_tests)
    set_tests_properties(application_baseline_tests PROPERTIES
        LABELS "integration;baseline" TIMEOUT 120
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;VD_DISABLE_AUDIO_OUTPUT=1")
    foreach(operation_case IN ITEMS source snapshot audio audio_inclusion audio_export queue outputs safety empty_timeline unknown_timeline sparse_eof index_reuse indexed_navigation image_sequence filter_failure filter_context log_lifetime recovery edit_preview project_validation numeric_json append_state)
        add_test(NAME operation_${operation_case}
            COMMAND application_baseline_tests --operation-test ${operation_case})
        set_tests_properties(operation_${operation_case} PROPERTIES
            LABELS "integration;lifetime;isolation" TIMEOUT 120
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;VD_DISABLE_AUDIO_OUTPUT=1")
    endforeach()

    add_executable(audio_filter_contract_tests
        tests/test_audio_filter_contracts.cpp
        tests/support/VDQtTestFixtures.cpp)
    target_link_libraries(audio_filter_contract_tests PRIVATE vdqt_core)
    vdqt_enable_diagnostics(audio_filter_contract_tests)
    add_test(NAME audio_filter_contract_tests COMMAND audio_filter_contract_tests)
    set_tests_properties(audio_filter_contract_tests PROPERTIES
        LABELS "audio;contracts" TIMEOUT 120 ENVIRONMENT "VD_DISABLE_AUDIO_OUTPUT=1")

    add_executable(filter_contract_tests tests/test_filter_contracts.cpp)
    target_link_libraries(filter_contract_tests PRIVATE vdqt_core)
    vdqt_enable_diagnostics(filter_contract_tests)
    foreach(filter_contract IN ITEMS field layout failure sequence identity validation)
        add_test(NAME filter_contract_${filter_contract}
            COMMAND filter_contract_tests ${filter_contract})
        set_tests_properties(filter_contract_${filter_contract} PROPERTIES
            LABELS "filters;contracts" TIMEOUT 30)
    endforeach()

    # Large integration suite covering workflows that cross module boundaries.
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
        src/VirtualDub/VDQtFilterValidation.cpp
        src/VirtualDub/VDQtFilterValidation.h
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
        src/VirtualDub/VDQtSourceDependencies.cpp
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

    # Focused guardrails stay separate so performance regressions are obvious.
    add_executable(playback_performance_tests
        src/VirtualDub/test_playback_performance.cpp
        src/VirtualDub/VDQtVideoDecoder.cpp
        src/VirtualDub/VDQtSourceDependencies.cpp
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
        src/VirtualDub/VDQtSourceDependencies.cpp
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
    )
    target_link_libraries(filter_performance_tests PRIVATE vdqt_core)
    vdqt_enable_diagnostics(filter_performance_tests)
    add_test(NAME filter_performance_tests COMMAND filter_performance_tests)
    set_tests_properties(filter_performance_tests PROPERTIES
        LABELS "performance;filters"
        TIMEOUT 20
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(filter_memory_tests
        tests/test_filter_memory.cpp
        tests/support/VDQtTestFixtures.cpp
    )
    target_link_libraries(filter_memory_tests PRIVATE vdqt_core)
    vdqt_enable_diagnostics(filter_memory_tests)
    foreach(filter_case IN ITEMS temporal ivtc television warp)
        # Process isolation keeps one ASan crash from hiding the other cases.
        add_test(NAME filter_memory_${filter_case} COMMAND filter_memory_tests ${filter_case})
        set_tests_properties(filter_memory_${filter_case} PROPERTIES
            LABELS "filters;memory" TIMEOUT 180)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ENABLE_SANITIZERS)
            add_test(NAME filter_allocation_${filter_case}
                COMMAND filter_memory_tests ${filter_case} --allocation-failure)
            set_tests_properties(filter_allocation_${filter_case} PROPERTIES
                LABELS "filters;memory;allocation" TIMEOUT 30)
        endif()
    endforeach()

    # Build a real shared VDX module rather than mocking the dlopen ABI.
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
    )
    target_link_libraries(plugin_host_tests PRIVATE vdqt_core)
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
    )
    target_compile_definitions(parity_contract_tests PRIVATE
        VDQT_PARITY_REFERENCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/virtualdub2-reference.json"
    )
    target_link_libraries(parity_contract_tests PRIVATE vdqt_core)
    vdqt_enable_diagnostics(parity_contract_tests)
    add_test(NAME parity_contract_tests COMMAND parity_contract_tests)
    set_tests_properties(parity_contract_tests PROPERTIES
        LABELS "parity;compatibility;scripting;filters;audio"
        TIMEOUT 15
    )

    # Final smoke test crosses the process boundary and exercises main.cpp's
    # unattended script path under an isolated configuration directory.
    add_executable(cli_media_smoke_tests
        tests/test_cli_media_smoke.cpp
        tests/support/VDQtTestFixtures.cpp)
    target_link_libraries(cli_media_smoke_tests PRIVATE Qt6::Core Qt6::Gui)
    vdqt_enable_diagnostics(cli_media_smoke_tests)
    add_test(NAME cli_media_smoke_tests
        COMMAND cli_media_smoke_tests $<TARGET_FILE:VirtualDubQt>)
    set_tests_properties(cli_media_smoke_tests PROPERTIES
        LABELS "integration;cli;smoke" TIMEOUT 90)

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
