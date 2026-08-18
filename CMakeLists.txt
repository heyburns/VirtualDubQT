cmake_minimum_required(VERSION 3.16)
project(VirtualDubQt VERSION 0.1.0 LANGUAGES CXX C)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui OpenGLWidgets Multimedia DBus)
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

add_compile_definitions(QT_NO_KEYWORDS)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vd2
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Kasumi/h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Kasumi/source
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VDLib/h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VDLib/source
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VDFilters/h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VDFilters/source
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VDFilters/source/autogen
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VirtualDub/h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VirtualDub/source
    ${AVISYNTH_INCLUDE_DIR}
)

set(SYSTEM_SOURCES
    src/system/bitmath.cpp
    src/system/cache.cpp
    src/system/cmdline.cpp
    src/system/cpuaccel.cpp
    src/system/date.cpp
    src/system/debug.cpp
    src/system/Error.cpp
    src/system/error_win32.cpp
    src/system/event.cpp
    src/system/file.cpp
    src/system/fileasync.cpp
    src/system/filestream.cpp
    src/system/filesys.cpp
    src/system/filewatcher.cpp
    src/system/Fraction.cpp
    src/system/halffloat.cpp
    src/system/hash.cpp
    src/system/int128.cpp
    src/system/linearalloc.cpp
    src/system/list.cpp
    src/system/log.cpp
    src/system/math.cpp
    src/system/memory.cpp
    src/system/process.cpp
    src/system/profile.cpp
    src/system/progress.cpp
    src/system/protscope.cpp
    src/system/refcount.cpp
    src/system/registry.cpp
    src/system/strutil.cpp
    src/system/text.cpp
    src/system/thread.cpp
    src/system/thunk.cpp
    src/system/time.cpp
    src/system/tls.cpp
    src/system/vdalloc.cpp
    src/system/VDNamespace.cpp
    src/system/VDScheduler.cpp
    src/system/vdstl.cpp
    src/system/vdstl_hash.cpp
    src/system/vdstl_hashtable.cpp
    src/system/VDString.cpp
    src/system/vectors.cpp
    src/system/w32assist.cpp
    src/system/zip.cpp
)

set(KASUMI_SOURCES
    src/Kasumi/alphablt.cpp
    src/Kasumi/blitter.cpp
    src/Kasumi/blt.cpp
    src/Kasumi/blt_reference.cpp
    src/Kasumi/blt_reference_pal.cpp
    src/Kasumi/blt_reference_rgb.cpp
    src/Kasumi/blt_reference_yuv.cpp
    src/Kasumi/blt_reference_yuv2yuv.cpp
    src/Kasumi/blt_reference_yuvrev.cpp
    src/Kasumi/blt_setup.cpp
    src/Kasumi/blt_spanutils.cpp
    src/Kasumi/blt_uberblit.cpp
    src/Kasumi/pixel.cpp
    src/Kasumi/pixmaputils.cpp
    src/Kasumi/region.cpp
    src/Kasumi/resample.cpp
    src/Kasumi/resample_kernels.cpp
    src/Kasumi/resample_stages.cpp
    src/Kasumi/resample_stages_reference.cpp
    src/Kasumi/resample_stages_x64.cpp
    src/Kasumi/stretchblt_reference.cpp
    src/Kasumi/tables.cpp
    src/Kasumi/text.cpp
    src/Kasumi/triblt.cpp
    src/Kasumi/uberblit.cpp
    src/Kasumi/uberblit_16f.cpp
    src/Kasumi/uberblit_gen.cpp
    src/Kasumi/uberblit_resample.cpp
    src/Kasumi/uberblit_resample_special.cpp
    src/Kasumi/uberblit_rgb64.cpp
    src/Kasumi/uberblit_swizzle.cpp
    src/Kasumi/uberblit_v210.cpp
    src/Kasumi/uberblit_ycbcr_generic.cpp
)

set(VDLIB_SOURCES
    src/VDLib/source/Dialog.cpp
    src/VDLib/source/Job.cpp
    src/VDLib/source/ParameterCurve.cpp
    src/VDLib/source/PortableRegistry.cpp
    src/VDLib/source/UIProxies.cpp
    src/VDLib/source/fft.cpp
    src/VDXFrame/source/VideoFilter.cpp
    src/VDXFrame/source/VideoFilterDialog.cpp
)

set(VDFILTERS_SOURCES
    src/VDFilters/source/Blur.cpp
    src/VDFilters/source/DSP.cpp
    src/VDFilters/source/SingleValueDialog.cpp
    src/VDFilters/source/VFAliasFormat.cpp
    src/VDFilters/source/VFBlendLayers.cpp
    src/VDFilters/source/VFBlur.cpp
    src/VDFilters/source/VFBoxBlur.cpp
    src/VDFilters/source/VFBrightCont.cpp
    src/VDFilters/source/VFChromaSmoother.cpp
    src/VDFilters/source/VFConvertFormat.cpp
    src/VDFilters/source/VFDeinterlace.cpp
    src/VDFilters/source/VFDrawText.cpp
    src/VDFilters/source/VFFieldBob.cpp
    src/VDFilters/source/VFFieldBob2.cpp
    src/VDFilters/source/VFFieldDelay.cpp
    src/VDFilters/source/VFFieldSwap.cpp
    src/VDFilters/source/VFFlipHorizontally.cpp
    src/VDFilters/source/VFFlipVertically.cpp
    src/VDFilters/source/VFGammaCorrect.cpp
    src/VDFilters/source/VFGrayscale.cpp
    src/VDFilters/source/VFHSV.cpp
    src/VDFilters/source/VFIVTC.cpp
    src/VDFilters/source/VFInterlace.cpp
    src/VDFilters/source/VFInterpolate.cpp
    src/VDFilters/source/VFInvert.cpp
    src/VDFilters/source/VFList.cpp
    src/VDFilters/source/VFMergeLayers.cpp
    src/VDFilters/source/VFMotionBlur.cpp
    src/VDFilters/source/VFNull.cpp
    src/VDFilters/source/VFPerspective.cpp
    src/VDFilters/source/VFReduce2.cpp
    src/VDFilters/source/VFReduce2HQ.cpp
    src/VDFilters/source/VFRotate.cpp
    src/VDFilters/source/VFRotate2.cpp
    src/VDFilters/source/VFSharpen.cpp
    src/VDFilters/source/VFSmoother.cpp
    src/VDFilters/source/VFTV.cpp
    src/VDFilters/source/VFTemporalSmoother.cpp
    src/VDFilters/source/VFThreshold.cpp
    src/VDFilters/source/VFWarpResize.cpp
    src/VDFilters/source/VFWarpSharp.cpp
)

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

set(MAIN_SOURCES
    src/vdwin32_shim.cpp
    src/vdwin32_shim.h
    src/main.cpp
)

add_executable(VirtualDubQt
    ${MAIN_SOURCES}
    ${SYSTEM_SOURCES}
    ${KASUMI_SOURCES}
    ${VDLIB_SOURCES}
    ${VDFILTERS_SOURCES}
    ${VIRTUALDUB_QT_SOURCES}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(VirtualDubQt PRIVATE
        -Wall -Wextra -Wpedantic -Wreturn-type -Wnull-dereference
    )
endif()
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # The imported VirtualDub sources contain Microsoft's i64/ui64 integer
    # suffixes. Clang supports them through its documented MS compatibility
    # mode; without this option an otherwise valid Linux build fails halfway.
    target_compile_options(VirtualDubQt PRIVATE -fms-extensions)
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
    Qt6::OpenGLWidgets
    Qt6::Multimedia
    Qt6::DBus
    ${AVISYNTH_LIBRARY}
    PkgConfig::FFMPEG
)
set_target_properties(VirtualDubQt PROPERTIES BUILD_RPATH "${AVISYNTH_LIBRARY_DIR}")

# Compatibility alias for build scripts
add_custom_target(VirtualDub_Port DEPENDS VirtualDubQt)

option(BUILD_CODEC_TROUBLESHOOTING_SUITE
       "Build the developer-only exhaustive codec troubleshooting executable"
       OFF)
if (BUILD_CODEC_TROUBLESHOOTING_SUITE)
    add_executable(test_all_codecs_suite
        src/vdwin32_shim.cpp
        src/vdwin32_shim.h
        src/VirtualDub/test_all_codecs_suite.cpp
        ${SYSTEM_SOURCES}
        ${KASUMI_SOURCES}
        ${VDLIB_SOURCES}
        ${VDFILTERS_SOURCES}
        ${VIRTUALDUB_QT_SOURCES}
    )

    target_link_libraries(test_all_codecs_suite PRIVATE
        Qt6::Widgets
        Qt6::Core
        Qt6::Gui
        Qt6::OpenGLWidgets
        Qt6::Multimedia
        Qt6::DBus
        ${AVISYNTH_LIBRARY}
        PkgConfig::FFMPEG
    )
    set_target_properties(test_all_codecs_suite PROPERTIES BUILD_RPATH "${AVISYNTH_LIBRARY_DIR}")
endif()

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
        src/system/event.cpp
        src/system/Fraction.cpp
    )
    target_link_libraries(stabilization_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Multimedia
        ${AVISYNTH_LIBRARY}
        PkgConfig::FFMPEG
    )
    target_compile_definitions(stabilization_tests PRIVATE VDQT_AUDIO_TESTING=1)
    if (ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(stabilization_tests PRIVATE
            -fno-omit-frame-pointer -fsanitize=address,undefined)
        target_link_options(stabilization_tests PRIVATE
            -fno-omit-frame-pointer -fsanitize=address,undefined)
    endif()
    set_target_properties(stabilization_tests PROPERTIES BUILD_RPATH "${AVISYNTH_LIBRARY_DIR}")
    add_test(NAME stabilization_tests COMMAND stabilization_tests)
endif()
