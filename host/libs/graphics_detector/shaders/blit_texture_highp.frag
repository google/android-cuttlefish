#version 460

layout(set = 0, binding = 0) uniform highp sampler2D uTexture;

layout(location = 0) noperspective in vec2 iUV;

layout(location = 0) out vec4 oColor;

void main() {
    oColor = texture(uTexture, iUV);
}
