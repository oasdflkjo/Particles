#include "Renderer.h"

#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <GLES3/gl3.h>
#include <memory>
#include <vector>
#include <android/imagedecoder.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <thread>
#include <time.h>
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include "AndroidOut.h"
#include "Shader.h"
#include "Utility.h"

//! executes glGetString and outputs the result to logcat
#define PRINT_GL_STRING(s) {aout << #s": "<< glGetString(s) << std::endl;}

/*!
 * @brief if glGetString returns a space separated list of elements, prints each one on a new line
 *
 * This works by creating an istringstream of the input c-style string. Then that is used to create
 * a vector -- each element of the vector is a new element in the input string. Finally a foreach
 * loop consumes this and outputs it to logcat using @a aout
 */
#define PRINT_GL_STRING_AS_LIST(s) { \
std::istringstream extensionStream((const char *) glGetString(s));\
std::vector<std::string> extensionList(\
        std::istream_iterator<std::string>{extensionStream},\
        std::istream_iterator<std::string>());\
aout << #s":\n";\
for (auto& extension: extensionList) {\
    aout << extension << "\n";\
}\
aout << std::endl;\
}

//! Color for cornflower blue. Can be sent directly to glClearColor
#define CORNFLOWER_BLUE 100 / 255.f, 149 / 255.f, 237 / 255.f, 1

// Base number of particles (will be scaled based on refresh rate)
static constexpr int BASE_PARTICLE_COUNT = 100000;

Renderer::~Renderer() {
    if (buffersInitialized_) {
        glDeleteBuffers(NUM_BUFFERS, positionBuffers_);
        glDeleteBuffers(NUM_BUFFERS, velocityBuffers_);
        glDeleteVertexArrays(NUM_BUFFERS, particleVAOs_);
    }
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context_ != EGL_NO_CONTEXT) {
            eglDestroyContext(display_, context_);
            context_ = EGL_NO_CONTEXT;
        }
        if (surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(display_, surface_);
            surface_ = EGL_NO_SURFACE;
        }
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
    }
}

void Renderer::render() {
    static float targetFPS = getRefreshRate();
    static const auto targetFrameTime = std::chrono::nanoseconds(static_cast<long long>(1000000000.0f / targetFPS));
    static auto lastFrameTime = std::chrono::steady_clock::now();
    static auto lastMissedFrameReport = std::chrono::steady_clock::now();
    static const auto reportThreshold = std::chrono::seconds(1);  // Report at most once per second
    
    // Use a shorter spin time for more precise timing
    static const auto spinThreshold = std::chrono::microseconds(200); // Reduced from 500
    
    auto now = std::chrono::steady_clock::now();
    auto frameTime = now - lastFrameTime;
    auto sleepTime = targetFrameTime - frameTime;
    
    // Check if we missed the frame timing
    if (frameTime > targetFrameTime) {
        auto currentTime = std::chrono::steady_clock::now();
        if (currentTime - lastMissedFrameReport > reportThreshold) {
            float actualMs = std::chrono::duration<float, std::milli>(frameTime).count();
            float targetMs = std::chrono::duration<float, std::milli>(targetFrameTime).count();
            float overMs = actualMs - targetMs;
            aout << "Missed frame timing by " << overMs 
                 << "ms (target: " << targetMs 
                 << "ms, actual: " << actualMs 
                 << "ms)" << std::endl;
            lastMissedFrameReport = currentTime;
        }
    }
    
    if (sleepTime > spinThreshold) {
        // Use clock_nanosleep for more precise sleeping
        struct timespec req, rem;
        auto sleepNs = std::chrono::duration_cast<std::chrono::nanoseconds>(sleepTime - spinThreshold);
        req.tv_sec = sleepNs.count() / 1000000000;
        req.tv_nsec = sleepNs.count() % 1000000000;
        clock_nanosleep(CLOCK_MONOTONIC, 0, &req, &rem);
        
        now = std::chrono::steady_clock::now();
        frameTime = now - lastFrameTime;
        sleepTime = targetFrameTime - frameTime;
    }
    
    // Spin-wait with yield for the remainder
    while (frameTime < targetFrameTime) {
        sched_yield();  // Allow other high-priority threads to run if needed
        now = std::chrono::steady_clock::now();
        frameTime = now - lastFrameTime;
    }
    
    lastFrameTime = now;
    
    updateRenderArea();
    
    // Clear to background color
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // Pure black background
    glClear(GL_COLOR_BUFFER_BIT);
    
    if (computeShader_ && particleShader_) {
        // Wait for previous compute to finish if there's a fence
        if (computeFence_) {
            // Wait with a timeout of 16ms (roughly one frame at 60Hz)
            GLenum waitResult = glClientWaitSync(computeFence_, GL_SYNC_FLUSH_COMMANDS_BIT, 16000000);
            if (waitResult == GL_TIMEOUT_EXPIRED) {
                aout << "Warning: Compute shader took longer than 16ms" << std::endl;
            }
            glDeleteSync(computeFence_);
            computeFence_ = nullptr;
            
            // Previous compute is now complete, update indices
            displayBufferIndex_ = computeBufferIndex_;
            computeBufferIndex_ = currentBufferIndex_;
            currentBufferIndex_ = displayBufferIndex_;
        }
        
        // Start compute for next frame
        updateParticles();
        
        // Create fence for this compute operation
        computeFence_ = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        
        // Render current frame
        renderParticles();
    }

    if (eglSwapBuffers(display_, surface_) != EGL_TRUE) {
        aout << "Failed to swap buffers: 0x" << std::hex << eglGetError() << std::endl;
    }
}

void Renderer::initRenderer() {
    aout << "Starting initRenderer" << std::endl;
    
    // Choose your render attributes
    constexpr EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_SWAP_BEHAVIOR_PRESERVED_BIT,
            EGL_BLUE_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_RED_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_NONE
    };

    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) {
        aout << "Failed to get display" << std::endl;
        return;
    }

    if (!eglInitialize(display_, nullptr, nullptr)) {
        aout << "Failed to initialize display" << std::endl;
        return;
    }

    EGLint numConfigs;
    if (!eglChooseConfig(display_, attribs, nullptr, 0, &numConfigs)) {
        aout << "Failed to get config count" << std::endl;
        return;
    }

    std::unique_ptr<EGLConfig[]> supportedConfigs(new EGLConfig[numConfigs]);
    if (!eglChooseConfig(display_, attribs, supportedConfigs.get(), numConfigs, &numConfigs)) {
        aout << "Failed to get configs" << std::endl;
        return;
    }

    auto config = supportedConfigs[0]; // Just take the first config for now

    EGLint format;
    eglGetConfigAttrib(display_, config, EGL_NATIVE_VISUAL_ID, &format);
    surface_ = eglCreateWindowSurface(display_, config, app_->window, nullptr);
    if (surface_ == EGL_NO_SURFACE) {
        aout << "Failed to create surface" << std::endl;
        return;
    }

    // Create a GLES 3.1 context
    const EGLint contextAttribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 1,
        EGL_NONE
    };
    
    context_ = eglCreateContext(display_, config, nullptr, contextAttribs);
    if (context_ == EGL_NO_CONTEXT) {
        aout << "Failed to create OpenGL ES 3.1 context, error: " << eglGetError() << std::endl;
        return;
    }

    if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
        aout << "Failed to make context current" << std::endl;
        return;
    }

    // Disable VSync
    if (!eglSwapInterval(display_, 1)) {
        aout << "Failed to set swap interval 0, error: " << eglGetError() << std::endl;
    }

    // Print OpenGL info
    aout << "OpenGL Vendor: " << glGetString(GL_VENDOR) << std::endl;
    aout << "OpenGL Renderer: " << glGetString(GL_RENDERER) << std::endl;
    aout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;

    // Check for compute shader support
    GLint maxComputeWorkGroupCount[3] = {0};
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &maxComputeWorkGroupCount[0]);
    if (glGetError() != GL_NO_ERROR) {
        aout << "Device does not support compute shaders!" << std::endl;
        return;
    }

    // Initialize width and height
    width_ = -1;
    height_ = -1;

    // Setup GL state first
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // Pure black background
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Initialize shaders
    auto assetManager = app_->activity->assetManager;
    if (!assetManager) {
        aout << "Failed to get asset manager" << std::endl;
        return;
    }
    
    // Load particle shaders
    aout << "Loading particle vertex shader..." << std::endl;
    std::string vertSrc, fragSrc, computeSrc;
    
    if (!Utility::loadAsset(assetManager, "shaders/particle.vert", vertSrc)) {
        aout << "Failed to load vertex shader" << std::endl;
        return;
    }
    
    if (!Utility::loadAsset(assetManager, "shaders/particle.frag", fragSrc)) {
        aout << "Failed to load fragment shader" << std::endl;
        return;
    }
    
    particleShader_ = std::unique_ptr<Shader>(
        Shader::loadShader(vertSrc, fragSrc, "position", "", "uProjection"));
    if (!particleShader_) {
        aout << "Failed to create particle shader" << std::endl;
        return;
    }
    
    // Load compute shader
    aout << "Loading compute shader..." << std::endl;
    if (!Utility::loadAsset(assetManager, "shaders/particle.comp", computeSrc)) {
        aout << "Failed to load compute shader" << std::endl;
        return;
    }
    
    computeShader_ = std::unique_ptr<Shader>(
        Shader::loadComputeShader(computeSrc));
    if (!computeShader_) {
        aout << "Failed to create compute shader" << std::endl;
        return;
    }

    // Initialize particle system
    initParticleSystem();
    aout << "Particle system initialized" << std::endl;

    // Set initial gravity point to center (0,0)
    gravityPoint_[0] = 0.0f;
    gravityPoint_[1] = 0.0f;
    aout << "Initial gravity point set to: (" << gravityPoint_[0] << ", " << gravityPoint_[1] << ")" << std::endl;

    aout << "Renderer initialization complete" << std::endl;
}

void Renderer::updateRenderArea() {
    EGLint width;
    eglQuerySurface(display_, surface_, EGL_WIDTH, &width);
    EGLint height;
    eglQuerySurface(display_, surface_, EGL_HEIGHT, &height);

    if (width != width_ || height != height_) {
        width_ = width;
        height_ = height;
        
        glViewport(0, 0, width_, height_);
        
        // Calculate orthographic projection matrix
        float projectionMatrix[16] = {0};
        
        // Use aspect ratio for scaling, but maintain original zoom level (was -5 to +5 = 10 units total)
        float aspectRatio = (float)width_ / height_;
        float baseScale = 2.0f / 20.0f;  // Changed from 10.0f to 20.0f to zoom out
        
        // Scale Y by baseScale and X by baseScale * aspect ratio to maintain proper display proportions
        projectionMatrix[0] = baseScale / aspectRatio;  // Scale X
        projectionMatrix[5] = baseScale;                // Scale Y
        projectionMatrix[10] = -1.0f;
        projectionMatrix[15] = 1.0f;
        
        worldWidth_ = FLT_MAX;   // Use float limits instead of artificial bounds
        worldHeight_ = FLT_MAX;
        
        // Update projection for particle shader
        if (particleShader_) {
            particleShader_->activate();
            if (!particleShader_->setProjectionMatrix(projectionMatrix)) {
                aout << "Failed to set projection matrix" << std::endl;
            }
            particleShader_->deactivate();
        }
    }
}

void Renderer::handleInput() {
    auto *inputBuffer = android_app_swap_input_buffers(app_);
    if (!inputBuffer) {
        return;
    }

    for (auto i = 0; i < inputBuffer->motionEventsCount; i++) {
        auto &motionEvent = inputBuffer->motionEvents[i];
        auto action = motionEvent.action;

        // Get the pointer index
        auto pointerIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        auto &pointer = motionEvent.pointers[pointerIndex];
        
        // Get screen coordinates
        auto x = GameActivityPointerAxes_getX(&pointer);
        auto y = GameActivityPointerAxes_getY(&pointer);

        // Convert screen coordinates to world coordinates using the same scale as our projection matrix
        float baseScale = 20.0f;  // Changed from 10.0f to 20.0f to match projection matrix
        float aspectRatio = (float)width_ / height_;
        float worldX = ((x / width_ - 0.5f) * baseScale * aspectRatio);
        float worldY = -((y / height_ - 0.5f) * baseScale);  // Flip Y coordinate
        
        switch (action & AMOTION_EVENT_ACTION_MASK) {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_MOVE:
                // Update gravity point
                gravityPoint_[0] = worldX;
                gravityPoint_[1] = worldY;
                break;

            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                break;

            case AMOTION_EVENT_ACTION_CANCEL:
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_POINTER_UP:
                break;

            default:
                // Handle other pointer events
                if ((action & AMOTION_EVENT_ACTION_MASK) == AMOTION_EVENT_ACTION_MOVE) {
                    for (auto index = 0; index < motionEvent.pointerCount; index++) {
                        pointer = motionEvent.pointers[index];
                        x = GameActivityPointerAxes_getX(&pointer);
                        y = GameActivityPointerAxes_getY(&pointer);
                    }
                }
        }
        aout << std::endl;
    }

    android_app_clear_motion_events(inputBuffer);
    android_app_clear_key_events(inputBuffer);
}

void Renderer::initParticleSystem() {
    // Get the refresh rate
    float refreshRate = getRefreshRate();
    
    // Simple binary scaling - either 90fps capable (1.8x particles) or not
    float scaleFactor = refreshRate >= 90.0f ? 2.0f : 1.0f;
    
    // Calculate total particles
    numParticles_ = static_cast<int>(BASE_PARTICLE_COUNT * scaleFactor);
    
    // Calculate grid dimensions to maintain roughly 4:3 aspect ratio
    float aspectRatio = 4.0f / 3.0f;
    int particlesPerCol = static_cast<int>(sqrt(numParticles_ / aspectRatio));
    int particlesPerRow = static_cast<int>(particlesPerCol * aspectRatio);
    
    // Adjust to match total count as closely as possible
    numParticles_ = particlesPerRow * particlesPerCol;
    
    aout << "Display refresh rate: " << refreshRate << " Hz" << std::endl;
    aout << "Particle scale factor: " << scaleFactor << std::endl;
    aout << "Creating particle buffers for " << numParticles_ << " particles" << std::endl;
    aout << "Grid size: " << particlesPerRow << " x " << particlesPerCol << std::endl;
    
    // Initialize particles in a grid pattern with a reasonable initial spread
    float initialSpread = 16.0f;  // Match our view area (20 units tall, but leave some margin)
    
    // Calculate spacing to distribute particles evenly
    float spacingY = initialSpread / (particlesPerCol - 1);
    float spacingX = (initialSpread * aspectRatio) / (particlesPerRow - 1);
    
    // Calculate start positions to center the grid
    float startX = -initialSpread * aspectRatio / 2.0f;
    float startY = -initialSpread / 2.0f;
    
    // Allocate aligned memory for positions and velocities
    alignas(16) std::vector<float> positions(numParticles_ * 2);
    alignas(16) std::vector<float> velocities(numParticles_ * 2);
    
    // Initialize random number generator with a seed
    srand(static_cast<unsigned>(time(nullptr)));
    
    // Initialize particles in a grid pattern
    for(int i = 0; i < numParticles_; i++) {
        int row = i / particlesPerRow;
        int col = i % particlesPerRow;
        
        // Position relative to center
        float xPos = startX + (col * spacingX);
        float yPos = startY + (row * spacingY);
        
        // Add small random offset to prevent perfect grid alignment
        float randX = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        float randY = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        xPos += (randX - 0.5f) * (spacingX * 0.5f);
        yPos += (randY - 0.5f) * (spacingY * 0.5f);
        
        // Initial velocities scaled to view area
        float randAngle = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.0f * M_PI;
        float randSpeed = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 4.0f;  // Increased initial speed
        
        // Store in SoA format
        positions[i * 2] = xPos;
        positions[i * 2 + 1] = yPos;
        velocities[i * 2] = cos(randAngle) * randSpeed;
        velocities[i * 2 + 1] = sin(randAngle) * randSpeed;
        
        // Debug output for first few particles
        if (i < 5) {
            aout << "Particle " << i << " pos: (" << xPos << ", " << yPos << ") vel: (" 
                 << cos(randAngle) * randSpeed << ", " << sin(randAngle) * randSpeed << ")" << std::endl;
        }
    }
    
    // Pre-allocate buffers at maximum size if not already done
    if (!buffersInitialized_) {
        // Generate buffers for triple buffering
        glGenBuffers(NUM_BUFFERS, positionBuffers_);
        glGenBuffers(NUM_BUFFERS, velocityBuffers_);
        glGenVertexArrays(NUM_BUFFERS, particleVAOs_);
        
        for (int i = 0; i < NUM_BUFFERS; i++) {
            // Initialize position buffer
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, positionBuffers_[i]);
            glBufferData(GL_SHADER_STORAGE_BUFFER, numParticles_ * 2 * sizeof(float), positions.data(), GL_DYNAMIC_DRAW);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, positionBuffers_[i]);
            
            // Initialize velocity buffer
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, velocityBuffers_[i]);
            glBufferData(GL_SHADER_STORAGE_BUFFER, numParticles_ * 2 * sizeof(float), velocities.data(), GL_DYNAMIC_DRAW);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, velocityBuffers_[i]);
            
            // Set up VAO with persistent attribute bindings
            glBindVertexArray(particleVAOs_[i]);
            
            // Position attribute (vec2)
            glBindBuffer(GL_ARRAY_BUFFER, positionBuffers_[i]);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
            glEnableVertexAttribArray(0);
            
            // Velocity attribute (vec2)
            glBindBuffer(GL_ARRAY_BUFFER, velocityBuffers_[i]);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
            glEnableVertexAttribArray(1);
        }
        
        // Reset bindings
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        
        buffersInitialized_ = true;
    }
    
    // Initialize all buffers with the same initial data
    for (int i = 0; i < NUM_BUFFERS; i++) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, positionBuffers_[i]);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, numParticles_ * 2 * sizeof(float), positions.data());
        
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, velocityBuffers_[i]);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, numParticles_ * 2 * sizeof(float), velocities.data());
    }
    
    // Verify setup
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        aout << "Error after buffer setup: 0x" << std::hex << error << std::endl;
    } else {
        aout << "Buffer setup successful with triple buffering" << std::endl;
        aout << "Initialized " << numParticles_ << " particles in " << NUM_BUFFERS << " buffers" << std::endl;
    }
}

void Renderer::updateParticles() {
    if (!computeShader_) return;
    
    if (!computeShader_->activate()) {
        aout << "Failed to activate compute shader" << std::endl;
        return;
    }
    
    // Calculate delta time and apply time scale
    auto currentTime = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime_).count() * timeScale_;
    lastFrameTime_ = currentTime;
    
    // Bind compute buffers - read from current buffer, write to compute buffer
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, positionBuffers_[computeBufferIndex_]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, velocityBuffers_[computeBufferIndex_]);
    
    // Copy current state to compute buffer before modification
    // First copy positions
    glBindBuffer(GL_COPY_READ_BUFFER, positionBuffers_[currentBufferIndex_]);
    glBindBuffer(GL_COPY_WRITE_BUFFER, positionBuffers_[computeBufferIndex_]);
    glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, numParticles_ * 2 * sizeof(float));
    
    // Then copy velocities
    glBindBuffer(GL_COPY_READ_BUFFER, velocityBuffers_[currentBufferIndex_]);
    glBindBuffer(GL_COPY_WRITE_BUFFER, velocityBuffers_[computeBufferIndex_]);
    glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, numParticles_ * 2 * sizeof(float));
    
    // Set uniforms
    GLint gravityLoc = glGetUniformLocation(computeShader_->program(), "gravityPoint");
    GLint deltaTimeLoc = glGetUniformLocation(computeShader_->program(), "deltaTime");
    
    if (gravityLoc != -1) {
        glUniform2fv(gravityLoc, 1, gravityPoint_);
    }
    
    if (deltaTimeLoc != -1) {
        glUniform1f(deltaTimeLoc, deltaTime);
    }
    
    // Dispatch compute shader
    int numGroups = (numParticles_ + 255) / 256;
    glDispatchCompute(numGroups, 1, 1);
    
    // Ensure compute shader writes are visible
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
    computeShader_->deactivate();
}

void Renderer::renderParticles() {
    if (!particleShader_) return;
    
    if (!particleShader_->activate()) {
        aout << "Failed to activate particle shader" << std::endl;
        return;
    }
    
    // Use alpha blending - only set once per frame
    static bool blendingSet = false;
    if (!blendingSet) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        blendingSet = true;
    }
    
    // Bind the current VAO for rendering
    glBindVertexArray(particleVAOs_[currentBufferIndex_]);
    
    // Draw particles
    glDrawArrays(GL_POINTS, 0, numParticles_);
    
    particleShader_->deactivate();
}

float Renderer::getRefreshRate() {
    float refreshRate = 0.0f;
    if (app_ && app_->activity && app_->activity->vm) {
        JNIEnv* env;
        app_->activity->vm->AttachCurrentThread(&env, nullptr);
        
        jobject activity = app_->activity->javaGameActivity;
        jclass activityClass = env->FindClass("android/app/NativeActivity");
        jmethodID getWindowManager = env->GetMethodID(activityClass, "getWindowManager", "()Landroid/view/WindowManager;");
        jobject windowManager = env->CallObjectMethod(activity, getWindowManager);
        
        jclass windowManagerClass = env->FindClass("android/view/WindowManager");
        jmethodID getDefaultDisplay = env->GetMethodID(windowManagerClass, "getDefaultDisplay", "()Landroid/view/Display;");
        jobject display = env->CallObjectMethod(windowManager, getDefaultDisplay);
        
        jclass displayClass = env->FindClass("android/view/Display");
        jmethodID getRefreshRate = env->GetMethodID(displayClass, "getRefreshRate", "()F");
        refreshRate = env->CallFloatMethod(display, getRefreshRate);
        
        env->DeleteLocalRef(displayClass);
        env->DeleteLocalRef(display);
        env->DeleteLocalRef(windowManagerClass);
        env->DeleteLocalRef(windowManager);
        env->DeleteLocalRef(activityClass);
        
        app_->activity->vm->DetachCurrentThread();
    }
    
    return refreshRate > 0.0f ? refreshRate : 60.0f;  // Default to 60Hz if we couldn't get the rate
}
