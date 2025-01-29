#version 300 es

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 velocity;

uniform mat4 uProjection;

out vec2 fragVelocity;
out vec4 particleColor;

void main() {
    gl_Position = uProjection * vec4(position, 0.0, 1.0);
    
    // Calculate a size that looks good in our projection
    float baseSize = 14.0;  // Base size in pixels (slightly increased from 12.0)
    gl_PointSize = baseSize;
    
    fragVelocity = velocity;
    
    float speed = length(velocity);
    
    // Balance color sensitivity to speed
    float normalizedSpeed = speed / 20.0;  // Adjust for speed range
    
    // More aggressive curve for later transition
    normalizedSpeed = pow(normalizedSpeed, 2.0);  // Stronger curve to delay transition
    
    // Base color: #8b5cf6 (deeper magenta)
    vec3 baseColor = vec3(139.0/255.0, 92.0/255.0, 246.0/255.0);
    
    // Target color: #6d28d9 (darker magenta that fits aesthetic)
    vec3 targetColor = vec3(109.0/255.0, 40.0/255.0, 217.0/255.0);
    
    // Mix between base color and target color based on speed
    // Adjust smoothstep range to make darker color appear only at higher speeds
    vec3 color = mix(
        baseColor,           // Base magenta
        targetColor,         // Darker magenta
        smoothstep(0.6, 0.9, normalizedSpeed)  // Much later transition to darker color
    );
    
    // Use 0.9 alpha for base particles
    particleColor = vec4(color, 0.9);
} 