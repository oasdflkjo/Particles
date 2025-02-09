#ifndef PARTICLES_PARTICLESYSTEM_H
#define PARTICLES_PARTICLESYSTEM_H

#include <GLES3/gl31.h>
#include <memory>
#include "Shader.h"

class ParticleSystem {
public:
    ParticleSystem(int maxParticles = 100000);
    ~ParticleSystem();

    void init(float refreshRate, std::unique_ptr<Shader>& computeShader, std::unique_ptr<Shader>& particleShader);
    void update(float deltaTime, const float* gravityPoint);
    void render();
    
private:
    static constexpr int NUM_BUFFERS = 2;  // Changed to 2 for double buffering
    GLuint positionBuffers_[NUM_BUFFERS];
    GLuint velocityBuffers_[NUM_BUFFERS];
    GLuint particleVAOs_[NUM_BUFFERS];
    
    int currentBuffer_ = 0;  // Index of current buffer
    int numParticles_;
    bool buffersInitialized_ = false;

    // Shader pointers
    std::unique_ptr<Shader>* computeShader_ = nullptr;
    std::unique_ptr<Shader>* particleShader_ = nullptr;

    // Cached uniform locations
    GLint gravityLoc_ = -1;
    GLint deltaTimeLoc_ = -1;
    
    void initializeParticles(float aspectRatio);
    void swapBuffers() { currentBuffer_ = 1 - currentBuffer_; }  // Simple buffer swap
};

#endif // PARTICLES_PARTICLESYSTEM_H 