#version 440

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 0, std140) uniform ColorParams {
    float brightness;   // -1.0 to 1.0 (0.0 = neutral)
    float contrast;     // 0.0 to 2.0 (1.0 = neutral)
    float saturation;   // 0.0 to 2.0 (1.0 = neutral)
    float gamma;        // 0.1 to 4.0 (1.0 = neutral)
    int   colorSpace;   // 0=BT.601, 1=BT.709, 2=BT.2020 (reserved for Phase 2)
    int   transferFunc; // 0=sRGB, 1=BT.1886, 2=PQ, 3=HLG (reserved for Phase 2)
    float pad1;
    float pad2;
};

layout(binding = 1) uniform sampler2D tex;

void main()
{
    vec3 rgb = texture(tex, v_texcoord).rgb;

    // Brightness (additive, in display space)
    rgb += brightness;

    // Contrast (pivot at 0.5)
    rgb = (rgb - 0.5) * contrast + 0.5;

    // Saturation (luminance-preserving)
    float lum = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    rgb = mix(vec3(lum), rgb, saturation);

    // Gamma correction
    rgb = clamp(rgb, 0.0, 1.0);
    rgb = pow(rgb, vec3(1.0 / gamma));

    fragColor = vec4(rgb, 1.0);
}
