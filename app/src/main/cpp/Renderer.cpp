#include "Renderer.h"

#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <GLES3/gl3.h>
#include <memory>
#include <vector>
#include <android/imagedecoder.h>
#include <chrono>

#include "AndroidOut.h"
#include "Shader.h"
#include "Utility.h"
#include "TextureAsset.h"

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

/*!
 * Half the height of the projection matrix. This gives you a renderable area of height 4 ranging
 * from -2 to 2
 */
static constexpr float kProjectionHalfHeight = 2.f;

/*!
 * The near plane distance for the projection matrix. Since this is an orthographic projection
 * matrix, it's convenient to have negative values for sorting (and avoiding z-fighting at 0).
 */
static constexpr float kProjectionNearPlane = -1.f;

/*!
 * The far plane distance for the projection matrix. Since this is an orthographic porjection
 * matrix, it's convenient to have the far plane equidistant from 0 as the near plane.
 */
static constexpr float kProjectionFarPlane = 1.f;

static constexpr int PARTICLES_PER_ROW = 1600;
static constexpr int PARTICLES_PER_COL = 800;
static constexpr int NUM_PARTICLES = PARTICLES_PER_ROW * PARTICLES_PER_COL;

#define DEBUG_GRID 0  // Toggle debug grid

Renderer::~Renderer() {
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
    updateRenderArea();
    
    glClear(GL_COLOR_BUFFER_BIT);
    
    if (computeShader_ && particleShader_) {
        updateParticles();
        renderParticles();
        renderDebugGrid();
    }

    auto swapResult = eglSwapBuffers(display_, surface_);
    assert(swapResult == EGL_TRUE);
}

void Renderer::initRenderer() {
    aout << "Starting initRenderer" << std::endl;
    
    // Choose your render attributes
    constexpr EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
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
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // Pure black background (changed from dark blue)
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
            
            // Load grid shader
            std::string gridVertSrc = Utility::loadAsset(assetManager, "shaders/grid.vert");
            std::string gridFragSrc = Utility::loadAsset(assetManager, "shaders/grid.frag");
            gridShader_ = std::unique_ptr<Shader>(
                Shader::loadShader(gridVertSrc, gridFragSrc, "position", "", "uProjection"));
            if (!gridShader_) {
                throw std::runtime_error("Failed to create grid shader");
            }
            
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Error loading shader files: ") + e.what());
        }

        // Test shaders
        particleShader_->activate();
        particleShader_->deactivate();
        aout << "Particle shader loaded and tested" << std::endl;

        computeShader_->activate();
        computeShader_->deactivate();
        aout << "Compute shader loaded and tested" << std::endl;

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
        
        // Set world view to match our grid (-5 to +5)
        float worldHeight = 10.0f;  // -5 to +5 in Y
        float worldWidth = worldHeight * (float)width_ / height_;
        
        // OpenGL projection matrix centered at 0,0
        projectionMatrix[0] = 2.0f / worldWidth;   // Scale X
        projectionMatrix[5] = 2.0f / worldHeight;  // Scale Y
        projectionMatrix[10] = -1.0f;
        projectionMatrix[15] = 1.0f;
        
        worldWidth_ = worldWidth;
        worldHeight_ = worldHeight;
        
        // Update projection for both shaders
        if (particleShader_) {
            particleShader_->activate();
            GLint projLoc = glGetUniformLocation(particleShader_->program(), "uProjection");
            if (projLoc != -1) {
                glUniformMatrix4fv(projLoc, 1, GL_FALSE, projectionMatrix);
            }
            particleShader_->deactivate();
        }
        
        if (gridShader_) {
            gridShader_->activate();
            GLint projLoc = glGetUniformLocation(gridShader_->program(), "uProjection");
            if (projLoc != -1) {
                glUniformMatrix4fv(projLoc, 1, GL_FALSE, projectionMatrix);
            }
            gridShader_->deactivate();
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

        // Convert screen coordinates to world coordinates
        float worldX = (x / width_ - 0.5f) * worldWidth_;
        float worldY = -(y / height_ - 0.5f) * worldHeight_;  // Flip Y coordinate
        
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
    aout << "Creating particle buffer for " << NUM_PARTICLES << " particles" << std::endl;
    
    glGenBuffers(1, &particleBuffer_);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, particleBuffer_);
    
    // Match particle grid to our visible world coordinates
    float worldHeight = 10.0f;  // -5 to +5 vertically (matches our grid)
    float worldWidth = worldHeight * (float)width_ / height_;  // Maintain aspect ratio
    
    // Debug output
    aout << "World dimensions: " << worldWidth << " x " << worldHeight << std::endl;
    aout << "Particles: " << PARTICLES_PER_ROW << " x " << PARTICLES_PER_COL << std::endl;
    
    // Calculate spacing to distribute particles evenly
    float spacingY = worldHeight / (PARTICLES_PER_COL - 1);
    float spacingX = worldWidth / (PARTICLES_PER_ROW - 1);
    
    // Calculate start positions to center the grid
    float startX = -worldWidth / 2.0f;
    float startY = -worldHeight / 2.0f;
    
    aout << "Spacing: " << spacingX << " x " << spacingY << std::endl;
    aout << "Start position: " << startX << ", " << startY << std::endl;
    
    std::vector<float> particleData(NUM_PARTICLES * 4, 0.0f);
    
    // Debug first and last particle positions
    float firstX = startX;
    float firstY = startY;
    float lastX = startX + (PARTICLES_PER_ROW - 1) * spacingX;
    float lastY = startY + (PARTICLES_PER_COL - 1) * spacingY;
    aout << "First particle at: " << firstX << ", " << firstY << std::endl;
    aout << "Last particle at: " << lastX << ", " << lastY << std::endl;
    
    for(int i = 0; i < NUM_PARTICLES; i++) {
        int row = i / PARTICLES_PER_ROW;
        int col = i % PARTICLES_PER_ROW;
        
        // Position relative to visible world space
        float xPos = startX + (col * spacingX);
        float yPos = startY + (row * spacingY);
        
        // Very small initial velocities
        float randAngle = (float)rand() / RAND_MAX * 2.0f * M_PI;
        float randSpeed = ((float)rand() / RAND_MAX * 0.5f);  // Even smaller initial speed
        
        particleData[i*4 + 0] = xPos;
        particleData[i*4 + 1] = yPos;
        particleData[i*4 + 2] = cos(randAngle) * randSpeed;
        particleData[i*4 + 3] = sin(randAngle) * randSpeed;
    }
    
    glBufferData(GL_SHADER_STORAGE_BUFFER, 
                 particleData.size() * sizeof(float),
                 particleData.data(), 
                 GL_DYNAMIC_DRAW);
    
    // Verify buffer creation
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        aout << "Error after buffer creation: 0x" << std::hex << error << std::endl;
    }
    
    glGenVertexArrays(1, &particleVAO_);
    glBindVertexArray(particleVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, particleBuffer_);
    
    // Position attribute (vec2)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    
    // Velocity attribute (vec2)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 
                         (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    // Verify VAO setup
    error = glGetError();
    if (error != GL_NO_ERROR) {
        aout << "Error after VAO setup: 0x" << std::hex << error << std::endl;
    }
}

void Renderer::updateParticles() {
    if (!computeShader_) return;
    
    computeShader_->activate();
    
    // Calculate delta time with 120 FPS cap
    auto currentTime = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime_).count();
    
    // Cap deltaTime to 1/120 seconds (approximately 0.00833)
    const float targetFrameTime = 1.0f / 120.0f;
    deltaTime = std::min(deltaTime, targetFrameTime);
    
    lastFrameTime_ = currentTime;
    
    // IMPORTANT: Bind buffer before setting uniforms
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, particleBuffer_);
    
    // Get uniform locations AFTER activating shader
    GLint gravityLoc = glGetUniformLocation(computeShader_->program(), "gravityPoint");
    GLint deltaTimeLoc = glGetUniformLocation(computeShader_->program(), "deltaTime");
    
    if (gravityLoc != -1) {
        glUniform2fv(gravityLoc, 1, gravityPoint_);
    }
    
    if (deltaTimeLoc != -1) {
        glUniform1f(deltaTimeLoc, deltaTime);
    }
    
    // Dispatch compute shader
    int numGroups = (NUM_PARTICLES + 255) / 256;
    glDispatchCompute(numGroups, 1, 1);
    
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    computeShader_->deactivate();
}

void Renderer::renderParticles() {
    if (!particleShader_) return;
    
    particleShader_->activate();
    
    // Clear to black
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // Pure black background
    glClear(GL_COLOR_BUFFER_BIT);
    
    // Use alpha blending instead of additive
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glBindVertexArray(particleVAO_);
    
    // Draw particles
    glDrawArrays(GL_POINTS, 0, NUM_PARTICLES);
    
    // Check for errors
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        aout << "Error after particle draw: 0x" << std::hex << error << std::endl;
    }
    
    particleShader_->deactivate();
}

void Renderer::renderDebugGrid() {
#if DEBUG_GRID
    if (!gridShader_) return;
    
    // Generate grid lines if not initialized
    if (debugGridVAO_ == 0) {
        std::vector<float> gridLines;
        float spacing = 1.0f;    // One unit in world space
        float extent = 5.0f;     // Show -5 to +5 in both directions
        
        // Generate horizontal lines
        for (float y = -extent; y <= extent; y += spacing) {
            gridLines.push_back(-extent);  // Start x
            gridLines.push_back(y);        // y
            gridLines.push_back(extent);   // End x
            gridLines.push_back(y);        // y
        }
        
        // Generate vertical lines
        for (float x = -extent; x <= extent; x += spacing) {
            gridLines.push_back(x);        // x
            gridLines.push_back(-extent);  // Start y
            gridLines.push_back(x);        // x
            gridLines.push_back(extent);   // End y
        }
        
        // Add origin marker point
        gridLines.push_back(0.0f);
        gridLines.push_back(0.0f);
        
        glGenVertexArrays(1, &debugGridVAO_);
        glGenBuffers(1, &debugGridVBO_);
        
        glBindVertexArray(debugGridVAO_);
        glBindBuffer(GL_ARRAY_BUFFER, debugGridVBO_);
        glBufferData(GL_ARRAY_BUFFER, gridLines.size() * sizeof(float), 
                    gridLines.data(), GL_STATIC_DRAW);
        
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(0);
        
        // Store total number of vertices (2 per line segment)
        numGridLines_ = gridLines.size() / 2;  // Each vertex is vec2 (x,y)
    }
    
    gridShader_->activate();
    glBindVertexArray(debugGridVAO_);
    
    // Make lines brighter and thicker
    GLint colorLoc = glGetUniformLocation(gridShader_->program(), "uColor");
    
    // Draw the grid lines
    glUniform4f(colorLoc, 0.3f, 0.3f, 0.3f, 1.0f);  // Brighter white
    glDrawArrays(GL_LINES, 0, numGridLines_ - 1);  // Draw all lines except last vertex pair
    
    // Draw origin point in red
    glUniform4f(colorLoc, 1.0f, 0.0f, 0.0f, 1.0f);
    glDrawArrays(GL_POINTS, numGridLines_ - 1, 1);  // Draw last vertex as point
    
    gridShader_->deactivate();
#endif
}