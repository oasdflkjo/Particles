# Add game-activity as a static library
add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)

# Export the native_app_glue include directory
target_include_directories(native_app_glue PUBLIC
    ${ANDROID_NDK}/sources/android/native_app_glue)

# Link against the game activity library
find_package(game-activity REQUIRED CONFIG)

target_link_libraries(${CMAKE_PROJECT_NAME}
    PRIVATE
    native_app_glue
    game-activity::game-activity
    ${log-lib}
    ${android-lib}
    ${EGL-lib}
    ${GLESv3-lib}
    ${jnigraphics-lib}
    mediandk
) 