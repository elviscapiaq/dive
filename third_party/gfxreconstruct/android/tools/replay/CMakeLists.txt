# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

cmake_minimum_required(VERSION 3.4.1)

project(gfxrecon-replay)

get_filename_component(GFXRECON_SOURCE_DIR ../../.. ABSOLUTE)

add_library(native_app_glue STATIC
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)

# Export ANativeActivity_onCreate(),
# Refer to: https://github.com/android-ndk/ndk/issues/381.
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")

include(../../framework/cmake-config/PlatformConfig.cmake)
add_subdirectory(../../framework/util ${CMAKE_SOURCE_DIR}/../../framework/util/build/tools/replay/${ANDROID_ABI})
add_subdirectory(../../framework/graphics ${CMAKE_SOURCE_DIR}/../../framework/graphics/build/tools/replay/${ANDROID_ABI})
add_subdirectory(../../framework/format ${CMAKE_SOURCE_DIR}/../../framework/format/build/tools/replay/${ANDROID_ABI})
add_subdirectory(../../framework/decode ${CMAKE_SOURCE_DIR}/../../framework/decode/build/tools/replay/${ANDROID_ABI})
add_subdirectory(../../framework/application ${CMAKE_SOURCE_DIR}/../../framework/application/build/tools/replay/${ANDROID_ABI})

# GOOGLE: Add subdirectories for gfxr_ext, putting binaries into the same dir as their base counterparts
add_subdirectory(../../../../../gfxr_ext/decode ${CMAKE_SOURCE_DIR}/../../framework/decode_ext/build/tools/replay/${ANDROID_ABI})

add_library(gfxrecon-replay
            SHARED
                ${GFXRECON_SOURCE_DIR}/tools/tool_settings.h
                ${GFXRECON_SOURCE_DIR}/tools/replay/parse_dump_resources_cli.h
                ${GFXRECON_SOURCE_DIR}/tools/replay/parse_dump_resources_cli.cpp
                ${GFXRECON_SOURCE_DIR}/tools/replay/replay_settings.h
                ${GFXRECON_SOURCE_DIR}/tools/replay/android_main.cpp)

# GOOGLE: Add dive root folder
target_include_directories(gfxrecon-replay
                           PUBLIC
                               ${ANDROID_NDK}/sources/android/native_app_glue
                               ${GFXRECON_SOURCE_DIR}/external/precompiled/android/include
                               ${CMAKE_BINARY_DIR}
                               ${GFXRECON_SOURCE_DIR}/../..)

# GOOGLE: Add gfxr_decode_ext_lib
target_link_libraries(
        gfxrecon-replay
        nlohmann_json
        gfxrecon_application
        gfxrecon_decode
        gfxr_decode_ext_lib
        gfxrecon_graphics
        gfxrecon_format
        gfxrecon_util
        platform_specific
        native_app_glue
        android
        log)
