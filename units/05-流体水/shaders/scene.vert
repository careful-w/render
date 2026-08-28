#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat3 uNormalMat;
uniform mat4 uLightSpace;

out vec3 vWorldPos;
out vec3 vNormal;
out vec4 vLightPos;

void main() {
    vec4 world  = uModel * vec4(aPos, 1.0);
    vWorldPos   = world.xyz;
    vNormal     = uNormalMat * aNormal;
    vLightPos   = uLightSpace * world;
    gl_Position = uProj * uView * world;
}
