#version 300 es
precision mediump float;

out vec4 fragColor;

void main() {
    // Draw a circular point
    vec2 coord = gl_PointCoord * 2.0 - 1.0;
    float r = dot(coord, coord);
    if (r > 1.0) discard;
    
    fragColor = vec4(1.0, 0.8, 0.3, 1.0); // Yellow-ish particles
} 