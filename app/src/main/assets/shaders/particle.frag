#version 300 es
precision mediump float;

in vec4 particleColor;
out vec4 fragColor;

void main() {
    // Calculate distance from center of point sprite
    vec2 coord = gl_PointCoord - vec2(0.5);
    float r = length(coord) * 2.0;
    
    // Create smooth circle with anti-aliased edges
    float alpha = 1.0 - smoothstep(0.0, 1.0, r);
    
    // Output color with calculated alpha for smooth dots
    fragColor = vec4(particleColor.rgb, particleColor.a * alpha);
} 