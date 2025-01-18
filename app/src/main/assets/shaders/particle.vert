#version 300 es

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 velocity;

uniform mat4 uProjection;

out vec2 fragVelocity;
out vec4 particleColor;

void main() {
    gl_Position = uProjection * vec4(position, 0.0, 1.0);
    gl_PointSize = 3.0;
    fragVelocity = velocity;
    
    float speed = length(velocity);
    
    // Balance color sensitivity to speed
    float normalizedSpeed = speed / 20.0;  // Increased from 15.0 for more gradual change
    
    // Gentle curve for smooth transition
    normalizedSpeed = pow(normalizedSpeed, 1.2);  // Slightly stronger curve
    
    // More balanced color transition
    vec3 color = mix(
        vec3(0.0, 0.8, 1.0),     // Bright neon blue (#00CCFF)
        vec3(1.0, 0.2, 0.8),     // Bright neon pink (#FF33CC)
        smoothstep(0.15, 0.7, normalizedSpeed)  // Wider, more gradual transition
    );
    
    particleColor = vec4(color, 1.0);
} 