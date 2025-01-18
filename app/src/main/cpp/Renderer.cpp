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

// Vertex shader, you'd typically load this from assets
static const char *vertex = R"vertex(#version 300 es
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;

out vec2 fragUV;

uniform mat4 uProjection;

void main() {
    fragUV = inUV;
    gl_Position = uProjection * vec4(inPosition, 1.0);
}
)vertex";

// Fragment shader, you'd typically load this from assets
static const char *fragment = R"fragment(#version 300 es
precision mediump float;

in vec2 fragUV;

uniform sampler2D uTexture;

out vec4 outColor;

void main() {
    outColor = texture(uTexture, fragUV);
}
)fragment";

// Add compute shader source
static const char* computeShaderSrc = R"compute(#version 310 es
layout(local_size_x = 256) in;

struct Particle {
    vec2 position;
    vec2 velocity;
};

layout(std430, binding = 0) buffer ParticleBuffer {
    Particle particles[];
};

uniform vec2 gravityPoint;
uniform float deltaTime;

void main() {
    uint index = gl_GlobalInvocationID.x;
    uint particleCount = uint(particles.length());
    
    if (index >= particleCount) return;
    
    vec2 pos = particles[index].position;
    vec2 vel = particles[index].velocity;
    
    // Simple gravity towards mouse
    vec2 toGravity = gravityPoint - pos;
    float dist = length(toGravity);
    vec2 dir = toGravity / max(dist, 1.0);
    
    // Simple physics
    float strength = 2000.0;
    vel += dir * strength * deltaTime;
    vel *= 0.99; // drag
    
    pos += vel * deltaTime;
    
    // Store results
    particles[index].position = pos;
    particles[index].velocity = vel;
}
)compute";

// Add particle vertex shader source
static const char* particleVertexSrc = R"vertex(#version 300 es
layout(location = 0) in vec2 position;
uniform mat4 uProjection;

void main() {
    // Pass through position directly, no projection yet
    vec2 pos = position / vec2(1000.0);  // Scale down positions to fit in [-1,1]
    gl_Position = vec4(pos, 0.0, 1.0);
    gl_PointSize = 8.0;
}
)vertex";

// Add particle fragment shader source
static const char* particleFragmentSrc = R"fragment(#version 300 es
precision mediump float;
out vec4 fragColor;

void main() {
    // Create circular particles with soft edges
    vec2 coord = gl_PointCoord * 2.0 - 1.0;
    float r = dot(coord, coord);
    if (r > 1.0) discard;
    
    // Fade out at edges
    float alpha = 1.0 - r;
    fragColor = vec4(1.0, 0.5, 0.2, alpha);  // Orange with fade
}
)fragment";

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

static constexpr int PARTICLES_PER_ROW = 100;  // Adjust this for performance
static constexpr int PARTICLES_PER_COL = 100;  // This will give us 10,000 particles
static constexpr int NUM_PARTICLES = PARTICLES_PER_ROW * PARTICLES_PER_COL;

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
    glClearColor(0.0f, 0.0f, 0.1f, 1.0f);  // Dark blue background
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Initialize shaders with error checking
    try {
        // Load main shader
        shader_ = std::unique_ptr<Shader>(
                Shader::loadShader(vertex, fragment, "inPosition", "inUV", "uProjection"));
        if (!shader_) {
            throw std::runtime_error("Failed to create main shader");
        }
        aout << "Main shader loaded" << std::endl;

        // Test activate without setting uniforms first
        shader_->activate();
        shader_->deactivate();
        aout << "Shader activation test passed" << std::endl;

        // Initialize particle shaders with more error checking
        aout << "Loading particle shader..." << std::endl;
        
        // Load vertex shader first
        GLuint vertexShader = Shader::loadShader(GL_VERTEX_SHADER, particleVertexSrc);
        if (!vertexShader) {
            throw std::runtime_error("Failed to compile particle vertex shader");
        }
        aout << "Particle vertex shader compiled" << std::endl;

        // Load fragment shader
        GLuint fragmentShader = Shader::loadShader(GL_FRAGMENT_SHADER, particleFragmentSrc);
        if (!fragmentShader) {
            glDeleteShader(vertexShader);
            throw std::runtime_error("Failed to compile particle fragment shader");
        }
        aout << "Particle fragment shader compiled" << std::endl;

        // Create and link program
        GLuint program = glCreateProgram();
        if (!program) {
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);
            throw std::runtime_error("Failed to create particle shader program");
        }

        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);

        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE) {
            GLint logLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
            std::string errorLog;
            if (logLength > 0) {
                std::vector<char> log(logLength);
                glGetProgramInfoLog(program, logLength, nullptr, log.data());
                errorLog = log.data();
            }
            glDeleteProgram(program);
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);
            throw std::runtime_error("Failed to link particle shader: " + errorLog);
        }

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        // Create shader object
        particleShader_ = std::unique_ptr<Shader>(new Shader(program, 0, -1, -1));
        aout << "Particle shader created" << std::endl;

        // Test particle shader activation
        particleShader_->activate();
        if (glGetError() != GL_NO_ERROR) {
            throw std::runtime_error("Failed to activate particle shader");
        }
        particleShader_->deactivate();
        aout << "Particle shader loaded and tested" << std::endl;

        // Initialize compute shader with error checking
        aout << "Loading compute shader..." << std::endl;
        computeShader_ = std::unique_ptr<Shader>(
            Shader::loadComputeShader(computeShaderSrc));  // Use loadComputeShader directly
        if (!computeShader_) {
            throw std::runtime_error("Failed to create compute shader");
        }

        // Test compute shader activation
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

    // Set initial gravity point
    gravityPoint_[0] = 0.0f;
    gravityPoint_[1] = 0.0f;

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
        
        // Calculate viewport to maintain square aspect ratio
        int size = std::min(width_, height_);
        int x = (width_ - size) / 2;
        int y = (height_ - size) / 2;
        
        // Set viewport to be square, centered in the window
        glViewport(x, y, size, size);
        
        aout << "Setting viewport: x=" << x << " y=" << y 
             << " size=" << size << std::endl;

        shaderNeedsNewProjectionMatrix_ = true;
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
        auto pointerIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

        auto &pointer = motionEvent.pointers[pointerIndex];
        auto x = GameActivityPointerAxes_getX(&pointer);
        auto y = GameActivityPointerAxes_getY(&pointer);

        // Convert screen coordinates to centered pixel coordinates
        float glX = x - width_ / 2.0f;
        float glY = height_ / 2.0f - y;  // Flip Y and center
        
        switch (action & AMOTION_EVENT_ACTION_MASK) {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_MOVE:
                gravityPoint_[0] = glX;
                gravityPoint_[1] = glY;
                aout << "Touch at pixel coords: (" << glX << ", " << glY << ")" << std::endl;
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
    aout << "Creating particle buffer" << std::endl;
    
    glGenBuffers(1, &particleBuffer_);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, particleBuffer_);
    
    std::vector<float> particleData(NUM_PARTICLES * 4); // pos.xy, vel.xy
    
    // Create a simple 100x100 grid of particles
    int gridSize = static_cast<int>(sqrt(NUM_PARTICLES));
    float spacing = 10.0f;  // Pixels between particles
    
    for(int i = 0; i < NUM_PARTICLES; i++) {
        int x = i % gridSize;
        int y = i / gridSize;
        
        // Center the grid
        float xPos = (x - gridSize/2) * spacing;
        float yPos = (y - gridSize/2) * spacing;
        
        particleData[i*4 + 0] = xPos;
        particleData[i*4 + 1] = yPos;
        particleData[i*4 + 2] = 0.0f;  // Initial velocity
        particleData[i*4 + 3] = 0.0f;
    }
    
    glBufferData(GL_SHADER_STORAGE_BUFFER, 
                 particleData.size() * sizeof(float),
                 particleData.data(), 
                 GL_DYNAMIC_DRAW);
    
    glGenVertexArrays(1, &particleVAO_);
    glBindVertexArray(particleVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, particleBuffer_);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
}

void Renderer::updateParticles() {
    if (!computeShader_) return;
    
    computeShader_->activate();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, particleBuffer_);
    
    // Use system clock for timing
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float deltaTime = std::chrono::duration<float>(currentTime - startTime).count();
    startTime = currentTime;
    
    // Set uniforms and check locations
    GLint gravityLoc = glGetUniformLocation(computeShader_->program(), "gravityPoint");
    GLint deltaTimeLoc = glGetUniformLocation(computeShader_->program(), "deltaTime");
    
    aout << "Gravity location: " << gravityLoc << ", DeltaTime location: " << deltaTimeLoc << std::endl;
    aout << "Current gravity point: " << gravityPoint_[0] << ", " << gravityPoint_[1] << std::endl;
    aout << "Delta time: " << deltaTime << std::endl;
    
    if (gravityLoc != -1) {
        glUniform2fv(gravityLoc, 1, gravityPoint_);
    }
    if (deltaTimeLoc != -1) {
        glUniform1f(deltaTimeLoc, deltaTime);
    }
    
    // Dispatch compute shader with logging
    int numGroups = NUM_PARTICLES / 256 + 1;
    aout << "Dispatching compute shader with " << numGroups << " work groups" << std::endl;
    glDispatchCompute(numGroups, 1, 1);
    
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        aout << "Error after dispatch: 0x" << std::hex << error << std::endl;
    }
    
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    computeShader_->deactivate();
}

void Renderer::renderParticles() {
    if (!particleShader_) return;
    
    particleShader_->activate();
    
    // Clear to dark color to see particles better
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    // Enable point sprites - in OpenGL ES 3.0, point size is controlled by gl_PointSize
    // No need to enable GL_PROGRAM_POINT_SIZE as it's always enabled
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glBindVertexArray(particleVAO_);
    
    // Debug: verify state before draw
    GLint currentVAO = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &currentVAO);
    aout << "Current VAO: " << currentVAO << " (expected: " << particleVAO_ << ")" << std::endl;
    
    GLint currentProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
    aout << "Current program: " << currentProgram << " (expected: " << particleShader_->program() << ")" << std::endl;
    
    // Draw particles
    glDrawArrays(GL_POINTS, 0, NUM_PARTICLES);
    
    // Check for errors
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        aout << "Error after particle draw: 0x" << std::hex << error << std::endl;
    }
    
    particleShader_->deactivate();
}