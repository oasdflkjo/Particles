#version 300 es

layout(location = 0) in vec2 position;

uniform mat4 uProjection;
uniform vec2 uPosition;
uniform vec2 uScale;

out vec2 texCoord;

void main() {
    // Convert position from [0,1] to [-1,1] for proper scaling
    vec2 pos = position * 2.0 - 1.0;
    // Apply scale and position
    pos = pos * uScale + uPosition;
    gl_Position = uProjection * vec4(pos, 0.0, 1.0);
    texCoord = position;
} 