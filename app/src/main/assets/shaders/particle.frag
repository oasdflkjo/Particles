#version 300 es
precision mediump float;

in vec4 particleColor;
out vec4 fragColor;

void main() {
    // Just output the color directly from vertex shader
    fragColor = particleColor;
} 