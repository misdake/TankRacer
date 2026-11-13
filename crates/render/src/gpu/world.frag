#version 300 es
precision highp float;
uniform sampler2D uTexture;
uniform bool uTextured;
uniform vec4 uColor;
uniform float uCutoff;
in vec4 vColor;
in vec2 vUv;
out vec4 outColor;
void main() {
    vec4 color = vColor * uColor;
    if (uTextured) color *= texture(uTexture, vUv);
    if (uCutoff >= 0.0 && color.a < uCutoff) discard;
    outColor = color;
}
