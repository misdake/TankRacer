#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUv;
layout(location = 3) in float aBillboardOffset;
layout(location = 4) in float aDoubleSided;
layout(location = 5) in mat4 aModel;
uniform mat4 uMatrix;
uniform mat4 uModel;
uniform bool uInstanced;
uniform vec2 uBillboardRight;
out vec4 vColor;
out vec2 vUv;
flat out float vDoubleSided;
void main() {
    vec3 position = ((uInstanced ? aModel : uModel) * vec4(aPosition, 1.0)).xyz;
    position.xz += uBillboardRight * aBillboardOffset;
    gl_Position = uMatrix * vec4(position, 1.0);
    vColor = aColor;
    vUv = aUv;
    vDoubleSided = aDoubleSided;
}
