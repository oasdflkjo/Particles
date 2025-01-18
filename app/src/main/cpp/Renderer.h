#ifndef ANDROIDGLINVESTIGATIONS_RENDERER_H
#define ANDROIDGLINVESTIGATIONS_RENDERER_H

#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <memory>
#include <vector>
#include <chrono>

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
            particleBuffer_(0),
            particleVAO_(0),
            shader_(nullptr),
            computeShader_(nullptr),
            particleShader_(nullptr) {
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
    EGLint width_;
    EGLint height_;

    std::unique_ptr<Shader> shader_;
    std::vector<Model> models_;

    std::unique_ptr<Shader> computeShader_;
    std::unique_ptr<Shader> particleShader_;
    GLuint particleBuffer_;
    GLuint particleVAO_;
    float gravityPoint_[2];
    
    using TimePoint = std::chrono::steady_clock::time_point;
    TimePoint lastFrameTime_;
    
    float worldWidth_ = 0.0f;
    float worldHeight_ = 0.0f;
};

#endif //ANDROIDGLINVESTIGATIONS_RENDERER_H