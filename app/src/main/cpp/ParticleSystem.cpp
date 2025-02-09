#include "ParticleSystem.h"
#include "AndroidOut.h"
#include <cmath>
#include <vector>

// Constants for particle counts
const int HIGH_REFRESH_PARTICLE_COUNT = 400000;  // For >= 90Hz displays
const int LOW_REFRESH_PARTICLE_COUNT = 25000;   // For < 90Hz displays

ParticleSystem::ParticleSystem(int maxParticles) : numParticles_(maxParticles) {}

ParticleSystem::~ParticleSystem() {
    if (buffersInitialized_) {
        glDeleteBuffers(NUM_BUFFERS, positionBuffers_);
        glDeleteBuffers(NUM_BUFFERS, velocityBuffers_);
        glDeleteVertexArrays(NUM_BUFFERS, particleVAOs_);
    }
}

void ParticleSystem::init(float refreshRate, std::unique_ptr<Shader>& computeShader, std::unique_ptr<Shader>& particleShader) {
    computeShader_ = &computeShader;
    particleShader_ = &particleShader;
    
    // Set particle count based on refresh rate
    numParticles_ = refreshRate >= 90.0f ? HIGH_REFRESH_PARTICLE_COUNT : LOW_REFRESH_PARTICLE_COUNT;
    
    // Initialize grid layout
    float aspectRatio = 4.0f / 3.0f;
    int particlesPerCol = static_cast<int>(sqrt(numParticles_ / aspectRatio));
    int particlesPerRow = static_cast<int>(particlesPerCol * aspectRatio);
    numParticles_ = particlesPerRow * particlesPerCol;
    
    initializeParticles(aspectRatio);
    
    // Configure blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);  // Pure additive blending for bright particles
    
    // Cache uniform locations
    if (computeShader_ && *computeShader_) {
        gravityLoc_ = glGetUniformLocation((*computeShader_)->program(), "gravityPoint");
        deltaTimeLoc_ = glGetUniformLocation((*computeShader_)->program(), "deltaTime");
    }
}

void ParticleSystem::update(float deltaTime, const float* gravityPoint) {
    if (!computeShader_ || !*computeShader_) return;
    
    (*computeShader_)->activate();
    
    // Read from current buffer, write to next buffer
    int nextBuffer = 1 - currentBuffer_;
    
    // Bind current buffer as input (binding 0)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, positionBuffers_[currentBuffer_]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, velocityBuffers_[currentBuffer_]);
    
    // Bind next buffer as output (binding 2 and 3)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, positionBuffers_[nextBuffer]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, velocityBuffers_[nextBuffer]);
    
    // Set uniforms
    if (gravityLoc_ != -1) glUniform2fv(gravityLoc_, 1, gravityPoint);
    if (deltaTimeLoc_ != -1) glUniform1f(deltaTimeLoc_, deltaTime);
    
    // Optimize workgroup size
    static GLint maxWorkGroupSize = 0;
    if (maxWorkGroupSize == 0) {
        GLint workGroupSizes[3];
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &workGroupSizes[0]);
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1, &workGroupSizes[1]);
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2, &workGroupSizes[2]);
        
        // Use power of 2 for better alignment
        maxWorkGroupSize = 256;  // Start with ideal size
        while (maxWorkGroupSize > workGroupSizes[0]) {
            maxWorkGroupSize >>= 1;
        }
    }
    
    int numGroups = (numParticles_ + maxWorkGroupSize - 1) / maxWorkGroupSize;
    glDispatchCompute(numGroups, 1, 1);
    
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    (*computeShader_)->deactivate();
    
    // After compute is done, swap buffers
    swapBuffers();
}

void ParticleSystem::render() {
    if (!particleShader_ || !*particleShader_) return;
    
    (*particleShader_)->activate();
    
    // Pre-configured blend state, no need to check/enable every frame
    glDrawArrays(GL_POINTS, 0, numParticles_);
    
    (*particleShader_)->deactivate();
}

void ParticleSystem::initializeParticles(float aspectRatio) {
    float initialSpread = 16.0f;
    float spacingY = initialSpread / (sqrt(numParticles_ / aspectRatio) - 1);
    float spacingX = (initialSpread * aspectRatio) / (static_cast<int>(sqrt(numParticles_ * aspectRatio)) - 1);
    
    float startX = -initialSpread * aspectRatio / 2.0f;
    float startY = -initialSpread / 2.0f;
    
    std::vector<float> positions(numParticles_ * 2);
    std::vector<float> velocities(numParticles_ * 2, 0.0f);  // Initialize all velocities to zero
    
    int particlesPerRow = static_cast<int>(sqrt(numParticles_ * aspectRatio));
    for(int i = 0; i < numParticles_; i++) {
        int row = i / particlesPerRow;
        int col = i % particlesPerRow;
        
        // Exact grid positioning without random offsets
        float xPos = startX + (col * spacingX);
        float yPos = startY + (row * spacingY);
        
        positions[i * 2] = xPos;
        positions[i * 2 + 1] = yPos;
        // Velocities are already initialized to zero in the vector constructor
    }
    
    if (!buffersInitialized_) {
        glGenBuffers(NUM_BUFFERS, positionBuffers_);
        glGenBuffers(NUM_BUFFERS, velocityBuffers_);
        glGenVertexArrays(NUM_BUFFERS, particleVAOs_);
        
        for (int i = 0; i < NUM_BUFFERS; i++) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, positionBuffers_[i]);
            glBufferData(GL_SHADER_STORAGE_BUFFER, numParticles_ * 2 * sizeof(float), positions.data(), GL_DYNAMIC_DRAW);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, positionBuffers_[i]);
            
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, velocityBuffers_[i]);
            glBufferData(GL_SHADER_STORAGE_BUFFER, numParticles_ * 2 * sizeof(float), velocities.data(), GL_DYNAMIC_DRAW);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, velocityBuffers_[i]);
            
            glBindVertexArray(particleVAOs_[i]);
            glBindBuffer(GL_ARRAY_BUFFER, positionBuffers_[i]);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
            glEnableVertexAttribArray(0);
            
            glBindBuffer(GL_ARRAY_BUFFER, velocityBuffers_[i]);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
            glEnableVertexAttribArray(1);
        }
        
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        
        buffersInitialized_ = true;
    }
} 