#ifndef ANDROIDGLINVESTIGATIONS_RENDERER_H
#define ANDROIDGLINVESTIGATIONS_RENDERER_H

#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <memory>
#include <vector>
#include <chrono>
#include <string>
#include "Model.h"
#include "Shader.h"

struct android_app;

class Renderer {
public:
    explicit Renderer(android_app *pApp) :
            app_(pApp),
            display_(EGL_NO_DISPLAY),
            surface_(EGL_NO_SURFACE),
            context_(EGL_NO_CONTEXT),
            width_(0),
            height_(0),
            gravityPoint_{0.0f, 0.0f},
            timeScale_(0.80f),
            positionBuffer_(0),
            velocityBuffer_(0),
            particleVAO_(0),
            numParticles_(0) {
        lastFrameTime_ = std::chrono::steady_clock::now();
        initRenderer();
    }

    virtual ~Renderer();

    void handleInput();
    void render();

private:
    void initRenderer();
    void updateRenderArea();
    void initParticleSystem();
    void updateParticles();
    void renderParticles();

    android_app *app_;
    EGLDisplay display_;
    EGLSurface surface_;
    EGLContext context_;
    GLint width_;
    GLint height_;
    float worldWidth_;
    float worldHeight_;
    float gravityPoint_[2];
    float timeScale_;  // Time scale factor (0.75 = 75% speed)

    // Particle system
    GLuint positionBuffer_;
    GLuint velocityBuffer_;
    GLuint particleVAO_;
    int numParticles_;

    // Shaders
    std::unique_ptr<Shader> computeShader_;
    std::unique_ptr<Shader> particleShader_;

    // Timing
    std::chrono::steady_clock::time_point lastFrameTime_;

    // Add buffer size tracking
    static constexpr int MAX_PARTICLES = 100000;  // Adjust based on your needs
    static constexpr int POSITION_BUFFER_SIZE = MAX_PARTICLES * 2 * sizeof(float);  // vec2 positions
    static constexpr int VELOCITY_BUFFER_SIZE = MAX_PARTICLES * 2 * sizeof(float);  // vec2 velocities
    
    // Add buffer state tracking
    bool buffersInitialized_ = false;
};

#endif //ANDROIDGLINVESTIGATIONS_RENDERER_H