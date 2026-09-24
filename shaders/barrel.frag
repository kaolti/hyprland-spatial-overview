#version 300 es

precision highp float;

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

uniform sampler2D tex;
uniform vec2 fullSize;
uniform float distort;
uniform float contrast;   // edge overscan, 1.0 disables it
uniform float brightness; // edge feather width

// Lens character toward the screen edges. All 0 means none; the plugin
// scales them with the zoom-out transition.
uniform float edgeBlur;      // 0..1, disc blur radius at the corners
uniform float edgeBlurStart; // 0..1 of the way from the center to a corner
uniform float vignette;      // 0..1
uniform float chromatic;     // 0..1, red/blue split along the radius

// Canvas chrome is composed after the lens in this same final-frame pass, so
// it can never become a warped source texture or a duplicated reflection.
const int MAX_MINIMAP_WINDOWS = 64;
uniform vec4 minimapPanel;
uniform vec4 minimapArrangeButton;
uniform vec4 minimapViewport;
uniform vec4 minimapWindows[MAX_MINIMAP_WINDOWS];
uniform float minimapWindowDim[MAX_MINIMAP_WINDOWS]; // 0 keeps a marker at full emphasis
uniform int minimapWindowCount;
uniform int minimapFocusedIndex;
uniform vec3 minimapActiveColor;
uniform vec3 minimapInactiveColor;
uniform float minimapOpacity;
uniform float minimapTransition;

// The navigator palette arrives as a premultiplied texture with a fixed
// screen rectangle, so search text is never bent by the lens.
uniform sampler2D hudTex;
uniform vec4 hudRect;
uniform float hudAlpha;

// Screen-corner readouts: up to four regions of one atlas texture.
const int MAX_HUD_REGIONS = 4;
uniform sampler2D hudAtlas;
uniform vec4 hudAtlasRects[MAX_HUD_REGIONS]; // device px, top-left origin
uniform vec4 hudAtlasUVs[MAX_HUD_REGIONS];   // x, y, w, h in the atlas
uniform int hudAtlasCount;

float roundedDistance(vec2 point, vec4 rect, float radius) {
    vec2 halfSize = rect.zw * 0.5;
    vec2 centered = point - (rect.xy + halfSize);
    vec2 delta = abs(centered) - halfSize + vec2(radius);
    return length(max(delta, vec2(0.0))) + min(max(delta.x, delta.y), 0.0) - radius;
}

float roundedMask(vec2 point, vec4 rect, float radius) {
    return 1.0 - smoothstep(-0.75, 0.75, roundedDistance(point, rect, radius));
}

float roundedBorderMask(vec2 point, vec4 rect, float radius, float thickness) {
    float outer = roundedMask(point, rect, radius);
    vec4 innerRect = vec4(rect.xy + vec2(thickness), max(rect.zw - vec2(thickness * 2.0), vec2(0.0)));
    float inner = roundedMask(point, innerRect, max(radius - thickness, 0.0));
    return max(outer - inner, 0.0);
}

vec4 blendOverlay(vec4 base, vec3 color, float alpha) {
    alpha = clamp(alpha, 0.0, 1.0);
    return vec4(mix(base.rgb, color, alpha), base.a);
}

vec4 composeMinimap(vec4 base) {
    if ((minimapPanel.z <= 0.0 || minimapPanel.w <= 0.0) &&
        (minimapArrangeButton.z <= 0.0 || minimapArrangeButton.w <= 0.0) || minimapTransition <= 0.001)
        return base;

    vec2 point = gl_FragCoord.xy;
    vec4 color = base;
    float panelRadius = max(6.0, min(minimapPanel.z, minimapPanel.w) * 0.075);
    float panelMask = roundedMask(point, minimapPanel, panelRadius);
    if (panelMask > 0.0) {
        color = blendOverlay(color, vec3(0.025, 0.030, 0.045), minimapOpacity * minimapTransition * panelMask);

        for (int i = 0; i < MAX_MINIMAP_WINDOWS; ++i) {
            if (i >= minimapWindowCount)
                break;
            bool focused = i == minimapFocusedIndex;
            float markerMask = roundedMask(point, minimapWindows[i], 3.0);
            color = blendOverlay(color, focused ? minimapActiveColor : minimapInactiveColor,
                                 (focused ? 0.92 : 0.58) * (1.0 - minimapWindowDim[i]) * minimapTransition * markerMask);
        }

        float viewportRadius = 5.0;
        float viewportFill = roundedMask(point, minimapViewport, viewportRadius);
        color = blendOverlay(color, minimapActiveColor, 0.12 * minimapTransition * viewportFill);
        float viewportBorder = roundedBorderMask(point, minimapViewport, viewportRadius, 2.0);
        color = blendOverlay(color, minimapActiveColor, 0.95 * minimapTransition * viewportBorder);

        float panelBorder = roundedBorderMask(point, minimapPanel, panelRadius, 1.0);
        color = blendOverlay(color, minimapInactiveColor, 0.50 * minimapTransition * panelBorder);
    }

    float buttonRadius = max(5.0, min(minimapArrangeButton.z, minimapArrangeButton.w) * 0.22);
    float buttonMask = roundedMask(point, minimapArrangeButton, buttonRadius);
    if (buttonMask > 0.0) {
        color = blendOverlay(color, vec3(0.025, 0.030, 0.045), minimapOpacity * minimapTransition * buttonMask);
        float buttonBorder = roundedBorderMask(point, minimapArrangeButton, buttonRadius, 1.0);
        color = blendOverlay(color, minimapInactiveColor, 0.65 * minimapTransition * buttonBorder);

        // Six small cells make the action recognizable as "arrange" without
        // relying on a font inside the compositor's final screen shader.
        vec2 inset = minimapArrangeButton.zw * 0.23;
        vec2 cell = vec2((minimapArrangeButton.z - inset.x * 2.0) * 0.42,
                         (minimapArrangeButton.w - inset.y * 2.0) * 0.25);
        vec2 gap = vec2((minimapArrangeButton.z - inset.x * 2.0) - cell.x * 2.0,
                        ((minimapArrangeButton.w - inset.y * 2.0) - cell.y * 3.0) * 0.5);
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 2; ++column) {
                vec2 pos = minimapArrangeButton.xy + inset + vec2(float(column) * (cell.x + gap.x), float(row) * (cell.y + gap.y));
                float cellMask = roundedMask(point, vec4(pos, cell), 1.5);
                color = blendOverlay(color, minimapActiveColor, 0.88 * minimapTransition * cellMask);
            }
        }
    }

    return color;
}

vec4 composeHud(vec4 base) {
    if (hudRect.z <= 0.0 || hudRect.w <= 0.0 || hudAlpha <= 0.001)
        return base;

    vec2 uv = (gl_FragCoord.xy - hudRect.xy) / hudRect.zw;
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0)
        return base;

    vec4 hud = texture(hudTex, uv) * hudAlpha;
    return vec4(base.rgb * (1.0 - hud.a) + hud.rgb, base.a);
}

vec4 composeHudAtlas(vec4 base) {
    if (hudAtlasCount <= 0 || hudAlpha <= 0.001)
        return base;

    for (int i = 0; i < MAX_HUD_REGIONS; ++i) {
        if (i >= hudAtlasCount)
            break;
        vec4 rect = hudAtlasRects[i];
        vec2 local = (gl_FragCoord.xy - rect.xy) / rect.zw;
        if (local.x < 0.0 || local.y < 0.0 || local.x > 1.0 || local.y > 1.0)
            continue;
        vec4 hud = texture(hudAtlas, hudAtlasUVs[i].xy + local * hudAtlasUVs[i].zw) * hudAlpha;
        base = vec4(base.rgb * (1.0 - hud.a) + hud.rgb, base.a);
    }
    return base;
}

const float GOLDEN_ANGLE = 2.39996323;
const int   BLUR_TAPS    = 24;

// One lens sample; with a fringe, red and blue come from either side of the
// point along the radius.
vec3 lensSample(vec2 uv, vec2 fringe) {
    if (fringe.x == 0.0 && fringe.y == 0.0)
        return texture(tex, clamp(uv, 0.0, 1.0)).rgb;
    return vec3(texture(tex, clamp(uv + fringe, 0.0, 1.0)).r,
                texture(tex, clamp(uv, 0.0, 1.0)).g,
                texture(tex, clamp(uv - fringe, 0.0, 1.0)).b);
}

void main() {
    vec4 color;

    // Installing/removing the screen shader is discrete, but these identity
    // uniforms make that switch visually lossless at either end of the camera
    // animation.
    bool lens = abs(distort) >= 0.000001 || abs(contrast - 1.0) >= 0.000001 || edgeBlur > 0.0001 || vignette > 0.0001 || chromatic > 0.0001;
    if (!lens) {
        color = texture(tex, v_texcoord);
    } else {
        vec2 centered = v_texcoord * 2.0 - 1.0;
        float radius2 = dot(centered, centered);

        // Radial lens mapping. Signed strength lets the user move continuously
        // between barrel and pincushion distortion.
        vec2 warped = centered * (1.0 + distort * radius2) / max(contrast, 0.001);
        vec2 uv = warped * 0.5 + 0.5;

        vec2 edgeDistance = min(uv, 1.0 - uv);
        float edgeAlpha = brightness <= 0.0 ? 1.0 : smoothstep(0.0, brightness, min(edgeDistance.x, edgeDistance.y));

        // 0 at the center of the screen, 1 in its corners.
        float radius = sqrt(radius2) * 0.70710678;
        float ramp = smoothstep(min(edgeBlurStart, 0.99), 1.0, radius);
        vec2 outward = radius > 0.0001 ? centered / sqrt(radius2) : vec2(0.0);
        // The fringe has its own, wider falloff: it grows from near the
        // center, quadratically, as in a real lens.
        float fringeRamp = smoothstep(0.1, 1.0, radius);
        vec2 fringe = outward * chromatic * fringeRamp * fringeRamp * 0.014;

        vec3 rgb;
        float blur = edgeBlur * ramp;
        if (blur > 0.001) {
            // Golden-angle disc, denser toward its center, like a lens bokeh.
            float radiusPx = blur * 0.02 * fullSize.y;
            vec2 texel = 1.0 / max(fullSize, vec2(1.0));
            vec3 sum = vec3(0.0);
            for (int i = 0; i < BLUR_TAPS; ++i) {
                float t = (float(i) + 0.5) / float(BLUR_TAPS);
                float angle = float(i) * GOLDEN_ANGLE;
                sum += lensSample(uv + vec2(cos(angle), sin(angle)) * sqrt(t) * radiusPx * texel, fringe);
            }
            rgb = sum / float(BLUR_TAPS);
        } else {
            rgb = lensSample(uv, fringe);
        }

        float shade = 1.0 - vignette * smoothstep(0.2, 1.0, radius);
        color = vec4(rgb * edgeAlpha * shade, texture(tex, clamp(uv, 0.0, 1.0)).a);
    }

    fragColor = composeHud(composeHudAtlas(composeMinimap(color)));
}
