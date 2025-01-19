#version 300 es
precision mediump float;

in vec2 texCoord;
uniform vec4 uColor;
uniform int uDigit;

out vec4 fragColor;

// Function to check if a point is inside a segment
float segment(vec2 uv, vec2 pos, vec2 size) {
    vec2 d = abs(uv - pos) - size;
    float outside = length(max(d, 0.0));
    float inside = min(max(d.x, d.y), 0.0);
    return outside + inside;
}

void main() {
    // Scale and center UV coordinates
    vec2 uv = (texCoord * 2.0 - 1.0);
    
    float thickness = 0.15;  // Segment thickness
    float length = 0.4;      // Segment length
    float alpha = 0.0;
    
    // Horizontal segments
    float h1 = segment(uv, vec2(0.0,  0.5), vec2(length, thickness));  // Top
    float h2 = segment(uv, vec2(0.0,  0.0), vec2(length, thickness));  // Middle
    float h3 = segment(uv, vec2(0.0, -0.5), vec2(length, thickness));  // Bottom
    
    // Vertical segments
    float v1 = segment(uv, vec2(-0.3,  0.25), vec2(thickness, 0.2));  // Top-left
    float v2 = segment(uv, vec2( 0.3,  0.25), vec2(thickness, 0.2));  // Top-right
    float v3 = segment(uv, vec2(-0.3, -0.25), vec2(thickness, 0.2));  // Bottom-left
    float v4 = segment(uv, vec2( 0.3, -0.25), vec2(thickness, 0.2));  // Bottom-right
    
    // Combine segments based on digit
    if (uDigit == 0) alpha = min(min(min(h1, h3), min(v1, v2)), min(v3, v4));
    else if (uDigit == 1) alpha = min(v2, v4);
    else if (uDigit == 2) alpha = min(min(min(h1, h2), h3), min(v2, v3));
    else if (uDigit == 3) alpha = min(min(min(h1, h2), h3), min(v2, v4));
    else if (uDigit == 4) alpha = min(min(h2, v1), min(v2, v4));
    else if (uDigit == 5) alpha = min(min(min(h1, h2), h3), min(v1, v4));
    else if (uDigit == 6) alpha = min(min(min(h1, h2), h3), min(min(v1, v3), v4));
    else if (uDigit == 7) alpha = min(h1, min(v2, v4));
    else if (uDigit == 8) alpha = min(min(min(h1, h2), h3), min(min(v1, v2), min(v3, v4)));
    else if (uDigit == 9) alpha = min(min(min(h1, h2), h3), min(min(v1, v2), v4));
    
    // Make the segments more visible
    alpha = 1.0 - smoothstep(0.0, 0.02, alpha);
    fragColor = uColor * vec4(1.0, 1.0, 1.0, alpha);
} 