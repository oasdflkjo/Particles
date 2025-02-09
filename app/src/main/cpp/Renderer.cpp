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
        glDeleteBuffers(1, &positionBuffer_);
        glDeleteBuffers(1, &velocityBuffer_);
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
    static float targetFPS = 0.0f;
    static const auto getRefreshRate = [this]() -> float {
        if (app_ && app_->activity && app_->activity->vm) {
            JNIEnv* env;
            app_->activity->vm->AttachCurrentThread(&env, nullptr);
            
            // Get the NativeActivity instance
            jobject activity = app_->activity->javaGameActivity;
            
            // Get the WindowManager service
            jclass activityClass = env->FindClass("android/app/NativeActivity");
            jmethodID getWindowManager = env->GetMethodID(activityClass, "getWindowManager", "()Landroid/view/WindowManager;");
            jobject windowManager = env->CallObjectMethod(activity, getWindowManager);
            
            // Get the default display
            jclass windowManagerClass = env->FindClass("android/view/WindowManager");
            jmethodID getDefaultDisplay = env->GetMethodID(windowManagerClass, "getDefaultDisplay", "()Landroid/view/Display;");
            jobject display = env->CallObjectMethod(windowManager, getDefaultDisplay);
            
            // Get the refresh rate
            jclass displayClass = env->FindClass("android/view/Display");
            jmethodID getRefreshRate = env->GetMethodID(displayClass, "getRefreshRate", "()F");
            float rate = env->CallFloatMethod(display, getRefreshRate);
            
            // Clean up local references
            env->DeleteLocalRef(displayClass);
            env->DeleteLocalRef(display);
            env->DeleteLocalRef(windowManagerClass);
            env->DeleteLocalRef(windowManager);
            env->DeleteLocalRef(activityClass);
            
            app_->activity->vm->DetachCurrentThread();
            
            if (rate > 0.0f) {
                aout << "Display refresh rate: " << rate << " Hz" << std::endl;
                return rate;
            }
        }
        // Default to 60 Hz if we can't get the refresh rate
        aout << "Could not get refresh rate, defaulting to 60 Hz" << std::endl;
        return 60.0f;
    };
    
    // Initialize target FPS on first frame
    if (targetFPS == 0.0f) {
        targetFPS = getRefreshRate();
    }
    
    // Frame timing using sleep + minimal spin approach
    static const auto targetFrameTime = std::chrono::nanoseconds(static_cast<long long>(1000000000.0f / targetFPS));
    static const auto spinThreshold = std::chrono::microseconds(500); // Spin only for last 0.5ms
    static auto lastFrameTime = std::chrono::steady_clock::now();
    
    auto now = std::chrono::steady_clock::now();
    auto frameTime = now - lastFrameTime;
    auto sleepTime = targetFrameTime - frameTime;
    
    if (sleepTime > spinThreshold) {
        // Sleep for most of the remaining time
        std::this_thread::sleep_for(sleepTime - spinThreshold);
        
        // Update timing after sleep
        now = std::chrono::steady_clock::now();
        frameTime = now - lastFrameTime;
        sleepTime = targetFrameTime - frameTime;
    }
    
    // Minimal spin-wait for the remainder
    while (frameTime < targetFrameTime) {
        now = std::chrono::steady_clock::now();
        frameTime = now - lastFrameTime;
    }
    
    lastFrameTime = now;
    
    updateRenderArea();
    
    // Clear to background color
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // Pure black background
    glClear(GL_COLOR_BUFFER_BIT);
    
    if (computeShader_ && particleShader_) {
        updateParticles();
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
    if (!eglSwapInterval(display_, 0)) {
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
    
    // Initialize shaders with error checking
    try {
        auto assetManager = app_->activity->assetManager;
        if (!assetManager) {
            throw std::runtime_error("Failed to get asset manager");
        }
        
        try {
            // Load particle shaders
            aout << "Loading particle vertex shader..." << std::endl;
            std::string vertSrc = Utility::loadAsset(assetManager, "shaders/particle.vert");
            std::string fragSrc = Utility::loadAsset(assetManager, "shaders/particle.frag");
            
            particleShader_ = std::unique_ptr<Shader>(
                Shader::loadShader(vertSrc, fragSrc, "position", "", "uProjection"));
            if (!particleShader_) {
                throw std::runtime_error("Failed to create particle shader");
            }
            
            // Load compute shader
            aout << "Loading compute shader..." << std::endl;
            std::string computeSrc = Utility::loadAsset(assetManager, "shaders/particle.comp");
            computeShader_ = std::unique_ptr<Shader>(
                Shader::loadComputeShader(computeSrc));
            if (!computeShader_) {
                throw std::runtime_error("Failed to create compute shader");
            }
            
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Error loading shader files: ") + e.what());
        }

        // Initialize particle system
        initParticleSystem();
        aout << "Particle system initialized" << std::endl;

    } catch (const std::exception& e) {
        aout << "Error during initialization: " << e.what() << std::endl;
        return;
    }

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
            GLint projLoc = glGetUniformLocation(particleShader_->program(), "uProjection");
            if (projLoc != -1) {
                glUniformMatrix4fv(projLoc, 1, GL_FALSE, projectionMatrix);
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
                aout << "Updated gravity point to: (" << gravityPoint_[0] << ", " << gravityPoint_[1] 
                     << ") from screen coords: (" << x << ", " << y << ")" << std::endl;
                break;

            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                aout << "(" << pointer.id << ", " << x << ", " << y << ") "
                     << "Pointer Down";
                break;

            case AMOTION_EVENT_ACTION_CANCEL:
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_POINTER_UP:
                aout << "(" << pointer.id << ", " << x << ", " << y << ") "
                     << "Pointer Up";
                break;

            default:
                // Handle other pointer events
                if ((action & AMOTION_EVENT_ACTION_MASK) == AMOTION_EVENT_ACTION_MOVE) {
                    for (auto index = 0; index < motionEvent.pointerCount; index++) {
                        pointer = motionEvent.pointers[index];
                        x = GameActivityPointerAxes_getX(&pointer);
                        y = GameActivityPointerAxes_getY(&pointer);
                        aout << "(" << pointer.id << ", " << x << ", " << y << ")";
                        if (index != (motionEvent.pointerCount - 1)) aout << ",";
                        aout << " ";
                    }
                    aout << "Pointer Move";
                } else {
                    aout << "Unknown MotionEvent Action: " << action;
                }
        }
        aout << std::endl;
    }

    android_app_clear_motion_events(inputBuffer);
    android_app_clear_key_events(inputBuffer);
}

void Renderer::initParticleSystem() {
    // Get the refresh rate first
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
    
    if (refreshRate <= 0.0f) {
        refreshRate = 60.0f;  // Default to 60Hz if we couldn't get the rate
    }

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
        // Generate buffers
        glGenBuffers(1, &positionBuffer_);
        glGenBuffers(1, &velocityBuffer_);
        
        // Pre-allocate position buffer
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, positionBuffer_);
        glBufferData(GL_SHADER_STORAGE_BUFFER, numParticles_ * 2 * sizeof(float), positions.data(), GL_DYNAMIC_DRAW);
        
        // Pre-allocate velocity buffer
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, velocityBuffer_);
        glBufferData(GL_SHADER_STORAGE_BUFFER, numParticles_ * 2 * sizeof(float), velocities.data(), GL_DYNAMIC_DRAW);
        
        buffersInitialized_ = true;
    }
    
    // Upload initial particle data
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, positionBuffer_);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, numParticles_ * 2 * sizeof(float), positions.data());
    
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, velocityBuffer_);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, numParticles_ * 2 * sizeof(float), velocities.data());
    
    // Set up VAO
    glGenVertexArrays(1, &particleVAO_);
    glBindVertexArray(particleVAO_);
    
    // Position attribute (vec2)
    glBindBuffer(GL_ARRAY_BUFFER, positionBuffer_);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    
    // Velocity attribute (vec2)
    glBindBuffer(GL_ARRAY_BUFFER, velocityBuffer_);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(1);
    
    // Verify setup
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        aout << "Error after buffer setup: 0x" << std::hex << error << std::endl;
    } else {
        aout << "Buffer setup successful" << std::endl;
        aout << "Initialized " << numParticles_ << " particles" << std::endl;
        aout << "First few particle positions:" << std::endl;
        for (int i = 0; i < 5 && i < numParticles_; i++) {
            aout << "Particle " << i << " pos: (" << positions[i * 2] << ", " 
                 << positions[i * 2 + 1] << ")" << std::endl;
        }
    }
}

void Renderer::updateParticles() {
    if (!computeShader_) return;
    
    computeShader_->activate();
    
    // Calculate delta time and apply time scale
    auto currentTime = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime_).count() * timeScale_;
    lastFrameTime_ = currentTime;
    
    // Bind both buffers to their respective binding points
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, positionBuffer_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, velocityBuffer_);
    
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
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    computeShader_->deactivate();
}

void Renderer::renderParticles() {
    if (!particleShader_) return;
    
    particleShader_->activate();
    
    // Use alpha blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glBindVertexArray(particleVAO_);
    
    // Draw particles
    glDrawArrays(GL_POINTS, 0, numParticles_);
    
    // Check for errors
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        aout << "Error after particle draw: 0x" << std::hex << error << std::endl;
    }
    
    particleShader_->deactivate();
}
