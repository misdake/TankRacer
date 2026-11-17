#version 300 es
precision highp float;
uniform sampler2D uTexture;
uniform bool uTextured;
uniform vec4 uColor;
uniform float uCutoff;
uniform bool uTexturePremultiplied;
uniform bool uOutputPremultiplied;
uniform bool uGeometryCulling;
uniform bool uShaded;
uniform bool uNormalLayer;
uniform float uLayerGain;
in vec4 vColor;
in vec2 vUv;
flat in float vDoubleSided;
in vec2 vLayerUv;
flat in float vLayerMask;
out vec4 outColor;
void main() {
    if (uGeometryCulling && vDoubleSided < 0.5 && !gl_FrontFacing) discard;
    vec4 diffuse = vColor;
    if (uShaded) diffuse.rgb = floor(floor(diffuse.rgb * 255.0 + 0.5) * 0.5) / 255.0;
    vec4 color = diffuse * uColor;
    if (uTextured) {
        vec4 texel = texture(uTexture, vUv);
        if (uTexturePremultiplied) {
            if (uOutputPremultiplied) color.rgb *= texel.rgb * color.a;
            else color.rgb *= texel.a > 0.0 ? texel.rgb / texel.a : vec3(0.0);
        } else {
            color.rgb *= texel.rgb;
            if (uOutputPremultiplied) color.rgb *= texel.a * color.a;
        }
        color.a *= texel.a;
    } else if (uOutputPremultiplied) color.rgb *= color.a;
    if (uCutoff >= 0.0 && color.a < uCutoff) discard;
    // Base then SRCALPHA/ONE, fused into the same visible fragment. The atlas
    // stores premultiplied RGB. Authored layers use opaque/keyed base passes.
    // Keeping the base's depth/cutout avoids a second, leaking coplanar draw.
    if(uNormalLayer && vLayerMask>0.5) color.rgb+=texture(uTexture,vLayerUv).rgb*uLayerGain;
    outColor = color;
}
