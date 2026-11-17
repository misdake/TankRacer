#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUv;
layout(location = 3) in float aBillboardOffset;
layout(location = 4) in float aDoubleSided;
layout(location = 5) in mat4 aModel;
layout(location = 9) in vec4 aUvScroll;
layout(location = 10) in vec4 aLayerNormal;
uniform mat4 uMatrix;
uniform mat4 uModel;
uniform bool uInstanced;
uniform vec2 uUvPhase;
uniform vec2 uBillboardRight;
uniform bool uNormalLayer;
uniform ivec3 uLayerRows[2];
uniform vec4 uLayerAtlas;
out vec4 vColor;
out vec2 vUv;
flat out float vDoubleSided;
out vec2 vLayerUv;
flat out float vLayerMask;
int normalCoordinate(ivec3 n, ivec3 row) {
    // Explicit unsigned multiplication/addition preserve original i32 wrapping.
    uint sum=uint(n.x)*uint(row.x)+uint(n.y)*uint(row.y)+uint(n.z)*uint(row.z);
    return (int(sum)>>9)+32768;
}
void main() {
    vec3 position = ((uInstanced ? aModel : uModel) * vec4(aPosition, 1.0)).xyz;
    position.xz += uBillboardRight * aBillboardOffset;
    gl_Position = uMatrix * vec4(position, 1.0);
    vColor = aColor;
    vUv = aUv;
    if (aUvScroll.w > 0.0) {
        // Source UV writes wrap to a byte at each corner, before interpolation.
        vUv.y = aUvScroll.z + mod(aUv.y - aUvScroll.z + dot(aUvScroll.xy, uUvPhase), aUvScroll.w);
    }
    vDoubleSided = aDoubleSided;
    vLayerMask=uNormalLayer ? aLayerNormal.w : 0.0;
    vLayerUv=vec2(0.0);
    if(uNormalLayer && aLayerNormal.w>0.5) {
        ivec3 normal=ivec3(aLayerNormal.xyz);
        vec2 uv=vec2(normalCoordinate(normal,uLayerRows[0]),normalCoordinate(normal,uLayerRows[1]))/65536.0;
        vLayerUv=uLayerAtlas.xy+clamp(uv,0.0,1.0)*uLayerAtlas.zw;
    }
}
