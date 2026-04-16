// D3D11 fullscreen-quad shader for D3D11Widget (Path B refactor).
//
// Vertex stage transforms a quad already in NDC ([-1,1] xy) — no MVP matrix
// needed because the geometry covers the whole back buffer. Aspect-ratio
// letterboxing happens in the rasterizer viewport (Phase 2.5), not here.
//
// Pixel stage samples a single 2D BGRA texture (the decoded video frame),
// then applies brightness/contrast/saturation/gamma per ColorParams cbuffer
// (Phase 3 — ported from resources/shaders/video.frag).

struct VsIn {
    float2 pos : POSITION;
    float2 uv  : TEXCOORD0;
};

struct PsIn {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

PsIn vs_main(VsIn input) {
    PsIn output;
    output.pos = float4(input.pos, 0.0f, 1.0f);
    output.uv  = input.uv;
    return output;
}

Texture2D    g_tex : register(t0);
SamplerState g_smp : register(s0);

cbuffer ColorParams : register(b0) {
    float brightness;       // -1.0 .. 1.0   (0.0 neutral)
    float contrast;         //  0.0 .. 2.0   (1.0 neutral)
    float saturation;       //  0.0 .. 2.0   (1.0 neutral)
    float gamma;            //  0.1 .. 4.0   (1.0 neutral)
    int   colorSpace;       // Batch 3.1 — 0 = BT.709 (no transform), 1 = BT.2020
    int   transferFunc;     // Batch 3.2+3.3 — 0 = sRGB, 1 = PQ, 2 = HLG
    int   tonemapMode;      // Batch 3.4 — 0 = Off (saturate), 1 = Reinhard, 2 = ACES, 3 = Hable
    int   hdrOutput;        // Batch 3.5 — 0 = SDR swap chain, 1 = scRGB (linear output, no tonemap/gamma)
};

// Batch 3.1 — BT.2020 → BT.709 gamut transform (both D65 whitepoint).
// Ported from OBS libobs/data/format_conversion.effect :: rec2020_to_rec709
// coefficients. Applied when colorSpace == 1 so that HDR10 / BT.2020 source
// content decoded to BGRA by the sidecar gets its chromaticities mapped
// into the BT.709 gamut our SDR monitors advertise.
// Batch 3.2 note: matrix now operates on LINEAR light when the PQ/HLG
// transferFunc paths have run upstream, which is the mathematically
// correct position for a gamut conversion. SDR input (transferFunc==0)
// still runs the matrix on gamma-encoded RGB, but SDR content isn't
// normally tagged BT.2020 so that branch rarely triggers in practice.
static const float3x3 kBT2020toBT709 = float3x3(
     1.66049100f, -0.58764110f, -0.07284990f,
    -0.12455050f,  1.13289990f, -0.00834940f,
    -0.01815080f, -0.10057890f,  1.11872970f
);

// Batch 3.2 — SMPTE ST.2084 (PQ) inverse electro-optical transfer function.
// Ported from OBS libobs/data/color.effect :: st2084_to_linear.
// Maps PQ-encoded [0,1] code values to absolute linear luminance where
// 1.0 = 10000 nits. HDR10 streams ship PQ-encoded; we decode here so
// downstream gamut + tonemap math operates on physically-meaningful light.
float st2084_to_linear_channel(float u)
{
    float c = pow(abs(u), 1.0f / 78.84375f);
    return pow(abs(max(c - 0.8359375f, 0.0f) / (18.8515625f - 18.6875f * c)),
               1.0f / 0.1593017578f);
}

float3 st2084_to_linear(float3 rgb)
{
    return float3(st2084_to_linear_channel(rgb.r),
                  st2084_to_linear_channel(rgb.g),
                  st2084_to_linear_channel(rgb.b));
}

// PQ-decoded values represent 0..10000 nits. Two output targets:
//   SDR swap chain → scale so 100 nits = 1.0, tonemap compresses
//                    highlights above 100 nits.
//   scRGB HDR swap chain → scRGB convention is 1.0 = 80 nits, peak
//                    10000 nits = 125.0. Scale by 125 so HDR displays
//                    receive correctly-scaled linear light.
// Phase 3 REVIEW P1 fix (2026-04-15): original Batch 3.5 left the SDR
// scale (100) active in the HDR output path, which produced ~64% of
// intended brightness on HDR10-on-HDR-display (a 10000-nit peak landed
// at 100 instead of the correct 125). Selection now branches on
// hdrOutput — SDR path unchanged; HDR path gets the scRGB-correct
// multiplier.
static const float kPqToSdrScale  = 100.0f;
static const float kPqToScRgbScale = 125.0f;  // 10000 nits / 80 nits-per-unit

// Batch 3.3 — ARIB STD-B67 (HLG) inverse OETF + OOTF.
// Ported from OBS libobs/data/color.effect :: hlg_to_linear_channel +
// hlg_to_linear. HLG is defined as a relative transfer function; decoded
// output is normalized against the reference display's peak luminance Lw.
// For a 1000-nit HDR reference (the BT.2100 default) the OOTF system
// gamma is 1.2 → exponent = 1.2 - 1.0 = 0.2, matching OBS's default.
// Luminance weights (0.2627 / 0.678 / 0.0593) are the BT.2020 primaries
// — HLG is defined against BT.2020 the same way PQ is.
float hlg_to_linear_channel(float u)
{
    float ln2_i = 1.0f / log(2.0f);              // HLSL log = natural log
    float m = ln2_i / 0.17883277f;
    float a = -ln2_i * 0.55991073f / 0.17883277f;
    return (u <= 0.5f) ? ((u * u) / 3.0f)
                       : ((exp2(u * m + a) + 0.28466892f) / 12.0f);
}

float3 hlg_to_linear(float3 v, float exponent)
{
    float3 rgb = float3(hlg_to_linear_channel(v.r),
                        hlg_to_linear_channel(v.g),
                        hlg_to_linear_channel(v.b));
    float Ys = dot(rgb, float3(0.2627f, 0.678f, 0.0593f));
    rgb *= pow(Ys, exponent);
    return rgb;
}

static const float kHlgOotfExponent = 0.2f;    // BT.2100 HLG @ Lw=1000 nits

// Batch 3.4 — tonemap operators. Each maps linear-light HDR RGB (where
// 1.0 = SDR reference and values above 1.0 represent highlights) to a
// display-ready [0, 1] range. Applied AFTER PQ/HLG decode + gamut
// matrix so all operators see consistent linear-light input. Replaces
// the plain saturate() for the HDR paths; saturate() stays as the
// default (tonemapMode == 0) for SDR content and for users who want
// hard-clip behavior.

// Reinhard (vanilla) — x / (1 + x). Simple, gentle, slightly dim
// highlights. Good baseline; under-performs on specular highlights
// vs ACES/Hable.
float3 reinhard(float3 x)
{
    return x / (1.0f + x);
}

// ACES — Krzysztof Narkowicz's "ACES filmic" fit. Punchier mid-tones
// and film-like highlight roll-off. Industry-standard default for HDR
// → SDR in game engines.
// Reference: https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
// Coefficients verified against Kodi's system/shaders/GL/1.5/gl_tonemap.glsl:17-25.
float3 aces_narkowicz(float3 x)
{
    const float A = 2.51f;
    const float B = 0.03f;
    const float C = 2.43f;
    const float D = 0.59f;
    const float E = 0.14f;
    return saturate((x * (A * x + B)) / (x * (C * x + D) + E));
}

// Hable — Uncharted 2 "filmic" curve by John Hable. Smoother shadows,
// more highlight compression than ACES; the look many HDR-to-SDR
// workflows default to in post.
// Reference: http://filmicworlds.com/blog/filmic-tonemapping-operators/
// Coefficients verified against Kodi's gl_tonemap.glsl:29-38 (same A..F).
static const float3 kHableW = float3(11.2f, 11.2f, 11.2f);  // white point
float3 hable_partial(float3 x)
{
    const float A = 0.15f;
    const float B = 0.50f;
    const float C = 0.10f;
    const float D = 0.20f;
    const float E = 0.02f;
    const float F = 0.30f;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 hable(float3 x)
{
    // Exposure bias of 2.0 matches Uncharted 2's original paper; keeps
    // mid-tones from feeling muddy after the curve.
    float3 curr = hable_partial(x * 2.0f);
    float3 whiteScale = 1.0f / hable_partial(kHableW);
    return saturate(curr * whiteScale);
}

// Batch 3.5 — sRGB → linear decode. Used when writing to the scRGB HDR
// swap chain (DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 on a
// R16G16B16A16_FLOAT back buffer) with SDR source content — the buffer
// expects physically-meaningful linear light. Piecewise formula from the
// sRGB spec (IEC 61966-2-1). For HDR source (transferFunc != 0) we're
// already in linear from the PQ/HLG decode at the top of ps_main.
float3 srgb_to_linear(float3 c)
{
    float3 low  = c / 12.92f;
    float3 high = pow((c + 0.055f) / 1.055f, 2.4f);
    float3 sel  = step(0.04045f, c);   // 0 where c <= 0.04045, else 1
    return lerp(low, high, sel);
}

// PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — subtitle overlay pixel shader.
// Samples the BGRA overlay texture (uploaded each frame from the sidecar's
// overlay SHM) and outputs the texel as-is. The alpha-blend render state
// (SRC_ALPHA / INV_SRC_ALPHA) does the src-over composite onto the
// previously-drawn video quad.
//
// No color transforms here — brightness / contrast / gamma are tuned for
// source video; applying them to libass-authored subtitle colors would
// distort them.
float4 ps_overlay(PsIn input) : SV_TARGET {
    return g_tex.Sample(g_smp, input.uv);
}

float4 ps_main(PsIn input) : SV_TARGET {
    float3 rgb = g_tex.Sample(g_smp, input.uv).rgb;

    // Batch 3.2 — transfer function decode. Runs FIRST so every downstream
    // operation (gamut matrix, brightness, contrast, saturation) sees
    // physically-meaningful linear light for HDR sources. Static branch
    // on transferFunc; FXC predicates it. SDR default (transferFunc == 0)
    // falls through unchanged — input is already display-ready sRGB.
    if (transferFunc == 1) {
        // PQ / HDR10: SMPTE ST.2084 EOTF to linear, then rescale based on
        // output. SDR swap chain → scale to 100-nit reference (Batch 3.4
        // tonemap compresses highlights above 1.0). scRGB HDR swap chain
        // → scale to scRGB convention (1.0 = 80 nits, 10000 nits = 125).
        // Phase 3 REVIEW P1 fix: pre-fix branch used kPqToSdrScale in both
        // output paths, under-scaling HDR output brightness.
        rgb = st2084_to_linear(rgb) * ((hdrOutput == 1) ? kPqToScRgbScale : kPqToSdrScale);
    } else if (transferFunc == 2) {
        // HLG / BT.2100: inverse HLG OETF + OOTF applied with the 1000-nit
        // reference exponent. OBS's `hlg_to_linear` returns values where
        // 1.0 corresponds to the 1000-nit display peak — unlike PQ's
        // [0,1]=10000-nit normalization, HLG's output is already within
        // the SDR mid-tone range, so no extra scale is applied here.
        // Highlights above 1.0 still clip hard at the final saturate()
        // until Batch 3.4's tonemap replaces the clip.
        rgb = hlg_to_linear(rgb, kHlgOotfExponent);
    }

    // Batch 3.1 — gamut transform. Static branch on a cbuffer uniform;
    // FXC's unbranched `if` compiles to a predicated select, costs nothing
    // when colorSpace == 0 (the SDR / BT.709 default path).
    if (colorSpace == 1) {
        rgb = mul(kBT2020toBT709, rgb);
    }

    // Brightness (additive, display space)
    rgb += brightness;

    // Contrast (pivot at 0.5)
    rgb = (rgb - 0.5f) * contrast + 0.5f;

    // Saturation (luminance-preserving, BT.709 weights)
    float lum = dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
    rgb = lerp(float3(lum, lum, lum), rgb, saturation);

    // Batch 3.5 — HDR output path (scRGB swap chain on an HDR-capable
    // monitor). Skip the SDR tonemap + gamma encode entirely and output
    // linear light directly; the display / OS compositor handles the
    // HDR encoding. For SDR source (transferFunc == 0) the sampled
    // value is still sRGB-gamma-encoded, so decode to linear before
    // output. For HDR source (transferFunc != 0) we're already in
    // linear from the PQ/HLG decode + gamut matrix upstream.
    if (hdrOutput == 1) {
        if (transferFunc == 0) rgb = srgb_to_linear(rgb);
        return float4(rgb, 1.0f);
    }

    // Batch 3.4 — tonemap. Replaces the plain saturate() for HDR content:
    // input here may have values >> 1.0 (HDR highlights); tonemap
    // operators map [0, ∞) → [0, 1] with graceful highlight roll-off.
    // tonemapMode == 0 falls back to saturate() (the pre-3.4 hard clip),
    // which is the correct default for SDR content.
    if      (tonemapMode == 1) rgb = reinhard(rgb);
    else if (tonemapMode == 2) rgb = aces_narkowicz(rgb);
    else if (tonemapMode == 3) rgb = hable(rgb);
    else                       rgb = saturate(rgb);

    // Gamma — user-facing display-gamma correction, applied after tonemap
    // so the shaper isn't double-counted. Default gamma = 1.0 = no-op.
    rgb = pow(rgb, 1.0f / gamma);

    return float4(rgb, 1.0f);
}
