#version 300 es

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 velocity;

uniform mat4 uProjection;

out vec2 fragVelocity;
out vec4 particleColor;

void main() {
    gl_Position = uProjection * vec4(position, 0.0, 1.0);
    
    // Calculate a size that looks good in our projection
    float baseSize = 14.0;  // Base size in pixels
    gl_PointSize = baseSize;
    
    fragVelocity = velocity;
    
    float speed = length(velocity);
    
    // Adjust speed normalization for more visible transitions
    float normalizedSpeed = speed / 10.0;  // Reduced from 20.0 to make transition happen at lower speeds
    
    // Less aggressive curve for more gradual transition
    normalizedSpeed = pow(normalizedSpeed, 1.5);  // Reduced from 2.0 for more gradual change
    
    // Base color: #9061f9 (slightly darker purple)
    vec3 baseColor = vec3(144.0/255.0, 97.0/255.0, 249.0/255.0);
    
    // Target color: #7c3aed (deep rich purple)
    vec3 targetColor = vec3(124.0/255.0, 58.0/255.0, 237.0/255.0);
    
    // Mix between base color and target color based on speed
    vec3 color = mix(
        baseColor,           // Darker purple
        targetColor,         // Deep rich purple
        smoothstep(0.3, 0.7, normalizedSpeed)  // Wider transition range, starts earlier
    );
    
    // Use 0.9 alpha for base particles
    particleColor = vec4(color, 0.9);
} 