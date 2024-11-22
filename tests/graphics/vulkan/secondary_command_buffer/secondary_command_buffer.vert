#version 460

vec2 kPositions[4] = vec2[](
    vec2(-1.0,  1.0),
    vec2(-1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2( 1.0, -1.0)
);

void main() {
    gl_Position = vec4(kPositions[gl_VertexIndex], 0.0, 1.0);
}
