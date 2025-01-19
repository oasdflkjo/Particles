#ifndef PARTICLES_DEBUGUTILS_H
#define PARTICLES_DEBUGUTILS_H

#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <GLES3/gl3.h>
#include <chrono>
#include <memory>
#include <string>
#include "Shader.h"

// Debug configuration flags
#define DEBUG_FPS_COUNTER 1

#if DEBUG_FPS_COUNTER
class FPSCounter {
public:
    FPSCounter();
    ~FPSCounter();

    void init(android_app* app);
    void update();
    void render(float worldWidth, float worldHeight);
    Shader* getShader() const { return textShader_.get(); }

private:
    GLuint textVAO_;
    GLuint textVBO_;
    std::unique_ptr<Shader> textShader_;
    std::string fpsText_;
    unsigned int frameCount_;
    std::chrono::steady_clock::time_point lastFPSUpdate_;
    float currentFPS_;

    void initTextRendering(android_app* app);
};
#endif // DEBUG_FPS_COUNTER

#endif // PARTICLES_DEBUGUTILS_H 