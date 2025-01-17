# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

cmake_minimum_required(VERSION 3.22.1)

project("particles")

# Add include directories for NDK
include_directories(${ANDROID_NDK}/toolchains/llvm/prebuilt/${ANDROID_HOST_TAG}/sysroot/usr/include)
include_directories(${ANDROID_NDK}/sources/android/native_app_glue)
include_directories(${ANDROID_NDK}/platforms/android-${ANDROID_PLATFORM_LEVEL}/arch-${ANDROID_ARCH}/usr/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Creates your game shared library
add_library(particles SHARED
        main.cpp
        AndroidOut.cpp
        Renderer.cpp
        Shader.cpp
        TextureAsset.cpp
        Utility.cpp)

# Searches for a package provided by the game activity dependency
find_package(game-activity REQUIRED CONFIG)

# Configure libraries CMake uses to link your target library.
target_link_libraries(particles
        game-activity::game-activity
        EGL
        GLESv3
        jnigraphics
        android
        log)

# Set C++ standard
set_target_properties(particles PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

# Add compiler definitions
target_compile_definitions(particles PRIVATE
    ANDROID
    GLM_FORCE_SIZE_T_LENGTH
)