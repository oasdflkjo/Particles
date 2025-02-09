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

# Debug configuration
if(CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_options(particles PRIVATE
        -g                      # Include debug symbols
        -O2                     # Moderate optimization level
        -fno-omit-frame-pointer # Keep frame pointer for better debugging
        -ftree-vectorize        # Enable vectorization
        -ffast-math             # Enable fast math operations
        -Wall                   # Enable all warnings
        -Werror                 # Treat warnings as errors
        -fno-rtti              # Disable RTTI
        -fno-exceptions        # Disable exceptions
        -fno-limit-debug-info  # Don't limit debug info
    )
else()
    target_compile_options(particles PRIVATE
        -O3                     # Maximum optimization
        -flto                   # Link-time optimization
        -ffast-math             # Enable fast math operations
        -Wall                   # Enable all warnings
        -Werror                # Treat warnings as errors
        -fno-rtti              # Disable RTTI
        -fno-exceptions        # Disable exceptions
        -fomit-frame-pointer   # Omit frame pointer for max performance
        -ffunction-sections    # Place each function in its own section
        -fdata-sections        # Place each data item in its own section
    )
endif()

# Add linker flags for release builds
if(NOT CMAKE_BUILD_TYPE MATCHES Debug)
    target_link_options(particles PRIVATE
        -Wl,--gc-sections      # Remove unused sections
        -flto                  # Link-time optimization
    )
endif()

target_compile_options(particles PRIVATE
    -target ${ANDROID_LLVM_TRIPLE}
    -Wall
    -Werror
    -fno-rtti  # Keep RTTI disabled as we don't use it
)

# Set the minimum Android API level
set(CMAKE_ANDROID_API_MIN 30)