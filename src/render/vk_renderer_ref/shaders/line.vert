#version 450

layout(push_constant) uniform PushConstants {
    vec2 screenSize;
    vec2 padding;
    vec4 tint;
} pc;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fsColor;

void main() {
    vec2 ndc = ((inPosition / pc.screenSize) * 2.0) - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    fsColor = inColor * pc.tint;
}
