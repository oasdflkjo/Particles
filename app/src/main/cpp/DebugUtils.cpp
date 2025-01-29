#include "DebugUtils.h"
#include "AndroidOut.h"
#include "Utility.h"
#include <sstream>
#include <iomanip>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#if DEBUG_FPS_COUNTER

FPSCounter::FPSCounter()
    : textVAO_(0)
    , textVBO_(0)
    , fpsText_("0.0")
    , frameCount_(0)
    , lastFPSUpdate_(std::chrono::steady_clock::now())
    , currentFPS_(0.0f) {
    aout << "FPSCounter constructed" << std::endl;
}

FPSCounter::~FPSCounter() {
    if (textVAO_) {
        glDeleteVertexArrays(1, &textVAO_);
        textVAO_ = 0;
    }
    if (textVBO_) {
        glDeleteBuffers(1, &textVBO_);
        textVBO_ = 0;
    }
}

void FPSCounter::init(android_app* app) {
    aout << "Initializing FPS counter..." << std::endl;
    initTextRendering(app);
}

void FPSCounter::update() {
    frameCount_++;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFPSUpdate_).count();
    
    if (elapsed >= 1000) { // Update FPS every second
        currentFPS_ = static_cast<float>(frameCount_ * 1000) / static_cast<float>(elapsed);
        frameCount_ = 0;
        lastFPSUpdate_ = now;
        
        // Update FPS text with one decimal place
        std::stringstream ss;
        ss << std::fixed << std::setprecision(1) << currentFPS_;
        fpsText_ = ss.str();
        aout << "FPS Updated: " << fpsText_ << std::endl;
    }
}

void FPSCounter::render(float worldWidth, float worldHeight) {
    if (!textShader_) {
        aout << "Text shader not initialized!" << std::endl;
        return;
    }

    // Enable blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    textShader_->activate();
    
    // Set color to bright cyan for better visibility
    GLint colorLoc = glGetUniformLocation(textShader_->program(), "uColor");
    if (colorLoc != -1) {
        glUniform4f(colorLoc, 0.0f, 1.0f, 1.0f, 1.0f);  // Bright cyan color
    } else {
        aout << "Warning: Could not find uColor uniform" << std::endl;
    }

    // Draw each digit of the FPS value
    float digitWidth = 0.5f;    // Width of each digit
    float spacing = 0.1f;       // Space between digits
    float totalWidth = (digitWidth + spacing) * static_cast<float>(fpsText_.length());
    
    // Position in top-right corner with some padding
    float startX = worldWidth * 0.4f - totalWidth;  // Align to right side
    float posY = worldHeight * 0.4f;                // Near top of screen
    
    GLint digitLoc = glGetUniformLocation(textShader_->program(), "uDigit");
    if (digitLoc == -1) {
        aout << "Warning: Could not find uDigit uniform" << std::endl;
    }
    
    aout << "Rendering FPS: " << fpsText_ << " at position (" << startX << ", " << posY << ")" << std::endl;
    
    // Draw each character in the FPS text
    for (size_t i = 0; i < fpsText_.length(); i++) {
        char c = fpsText_[i];
        // Skip non-digit characters for now
        if (!isdigit(c)) continue;
        
        GLint posLoc = glGetUniformLocation(textShader_->program(), "uPosition");
        if (posLoc != -1) {
            float posX = startX + (digitWidth + spacing) * static_cast<float>(i);
            glUniform2f(posLoc, posX, posY);
        } else {
            aout << "Warning: Could not find uPosition uniform" << std::endl;
        }
        
        GLint scaleLoc = glGetUniformLocation(textShader_->program(), "uScale");
        if (scaleLoc != -1) {
            glUniform2f(scaleLoc, digitWidth, digitWidth * 1.5f);  // Keep aspect ratio
        } else {
            aout << "Warning: Could not find uScale uniform" << std::endl;
        }
        
        // Set the digit value
        if (digitLoc != -1) {
            int digit = c - '0';  // Convert char to int
            glUniform1i(digitLoc, digit);
        }
        
        glBindVertexArray(textVAO_);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
    }
    
    // Check for any OpenGL errors
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        aout << "OpenGL error during FPS render: 0x" << std::hex << error << std::endl;
    }
    
    textShader_->deactivate();
}

void FPSCounter::initTextRendering(android_app* app) {
    aout << "Initializing text rendering..." << std::endl;
    
    // Create a simple quad for text rendering
    float vertices[] = {
        0.0f, 0.0f,  // Bottom-left
        1.0f, 0.0f,  // Bottom-right
        0.0f, 1.0f,  // Top-left
        1.0f, 1.0f   // Top-right
    };

    glGenVertexArrays(1, &textVAO_);
    glGenBuffers(1, &textVBO_);
    
    glBindVertexArray(textVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, textVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Load text shaders
    try {
        auto assetManager = app->activity->assetManager;
        std::string vertSrc = Utility::loadAsset(assetManager, "shaders/text.vert");
        std::string fragSrc = Utility::loadAsset(assetManager, "shaders/text.frag");
        
        aout << "Loading text shaders..." << std::endl;
        textShader_ = std::unique_ptr<Shader>(
            Shader::loadShader(vertSrc, fragSrc, "position", "", "uProjection"));
            
        if (textShader_) {
            aout << "Text shader created successfully" << std::endl;
        } else {
            aout << "Failed to create text shader!" << std::endl;
        }
    } catch (const std::exception& e) {
        aout << "Failed to load text shaders: " << e.what() << std::endl;
    }
    
    // Check for any OpenGL errors
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        aout << "OpenGL error during text init: 0x" << std::hex << error << std::endl;
    }
}

#endif // DEBUG_FPS_COUNTER 