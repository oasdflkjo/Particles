#ifndef ANDROIDGLINVESTIGATIONS_RENDERER_H
#define ANDROIDGLINVESTIGATIONS_RENDERER_H

#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <memory>
#include <vector>
#include <chrono>
#include <string>
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
            currentBufferIndex_(0),
            computeBufferIndex_(1),
            displayBufferIndex_(2),
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
    void swapParticleBuffers();

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

    // Triple buffering for particle system
    static constexpr int NUM_BUFFERS = 3;
    GLuint positionBuffers_[NUM_BUFFERS];
    GLuint velocityBuffers_[NUM_BUFFERS];
    GLuint particleVAOs_[NUM_BUFFERS];
    
    // Buffer indices for triple buffering
    int currentBufferIndex_;    // Buffer currently being rendered
    int computeBufferIndex_;    // Buffer being computed
    int displayBufferIndex_;    // Buffer ready for display
    int numParticles_;

    // Synchronization fence for compute shader
    GLsync computeFence_ = nullptr;

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