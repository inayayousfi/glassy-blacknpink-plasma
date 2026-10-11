#version 140

// Draws the frosted background through the glass, as premultiplied color for
// KWin's GL_ONE/GL_ONE_MINUS_SRC_ALPHA blend.
//
// The glass combines ideas and formulas from these projects:
//
// - liquid-glass-js by Eamon Liu, src/liquid-glass.js: the rim profile, the
//   refraction along the edge normal, the color fringing and the rim light.
//   Copyright (c) 2026 Eamon Liu. MIT License; see LICENSE-liquid-glass-js.
// - @ybouane/liquidglass by ybouane, src/shaders.ts: the Fresnel reflection
//   and the inner stroke. MIT License; see LICENSE-ybouane-liquidglass.
// - "Liquid Glass Frutiger Aero UI" by sentinelcmd on godotshaders.com: the
//   top sheen. CC0 1.0 Universal.
// - "Liquid Glass in the Browser" on kube.io and Htet Lin Aung's
//   liquid-glass (github.com/Hla-aung/liquid-glass): the squircle surface with
//   exponent 4 and the refraction along the edge normal, fading to nothing on
//   the flat face. Ideas only; no code is copied.
// - OverShifted/LiquidGlass, assets/shaders/BatchRenderer2D.glsl, the
//   shader this glass started from. Copyright (c) 2026 Sepehr Kalanaki.
//   MIT License; see LICENSE-OverShifted-LiquidGlass.

uniform sampler2D texUnit; // the half-size frosted background
uniform sampler2D sharpUnit; // the full-size background, unblurred
uniform sampler2D frostUnit; // the quarter-size frost
uniform sampler2D heavierUnit; // the heavier frost, at a quarter size
uniform float offset;
uniform vec2 halfpixel;
uniform vec4 cornerRadius; // device pixels: top left, top right, bottom right, bottom left
uniform vec2 blurSize;     // device pixels covered by the glass
uniform vec4 pane;         // center and half size of the whole pane, in the same pixels
uniform float opacity;
uniform int refraction;    // 0 draws the frosting alone, inside the rounded rectangle
uniform float frost;       // how far the face frosts, 0 to 1 over three steps, eased over time by the effect

in vec2 texcoord0;
out vec4 fragColor;

float roundedRectangleDistance(vec2 position, vec2 halfSize, vec4 radii)
{
    float radius = position.x > 0.0
        ? (position.y > 0.0 ? radii.y : radii.w)
        : (position.y > 0.0 ? radii.x : radii.z);
    vec2 corner = abs(position) - halfSize + radius;
    return min(max(corner.x, corner.y), 0.0) + length(max(corner, 0.0)) - radius;
}

vec4 frosting(vec2 uv)
{
    vec4 sum = texture(texUnit, uv + vec2(-halfpixel.x * 2.0, 0.0) * offset);
    sum += texture(texUnit, uv + vec2(-halfpixel.x, halfpixel.y) * offset) * 2.0;
    sum += texture(texUnit, uv + vec2(0.0, halfpixel.y * 2.0) * offset);
    sum += texture(texUnit, uv + vec2(halfpixel.x, halfpixel.y) * offset) * 2.0;
    sum += texture(texUnit, uv + vec2(halfpixel.x * 2.0, 0.0) * offset);
    sum += texture(texUnit, uv + vec2(halfpixel.x, -halfpixel.y) * offset) * 2.0;
    sum += texture(texUnit, uv + vec2(0.0, -halfpixel.y * 2.0) * offset);
    sum += texture(texUnit, uv + vec2(-halfpixel.x, -halfpixel.y) * offset) * 2.0;
    return sum / 12.0;
}

float random(vec2 coordinate)
{
    return fract(sin(dot(coordinate, vec2(12.9898, 78.233))) * 43758.5453);
}

// Rim surface: a squircle rising from the edge (t = 0) to the flat face
// (t = 1), the curve liquid-glass-js and kube.io use; exponent 4 blends the
// face into the rim smoothly. Its slope lights the rim.
const float rimCurvature = 4.0;

float rimProfile(float t)
{
    t = clamp(t, 0.0, 1.0);
    return pow(1.0 - pow(1.0 - t, rimCurvature), 1.0 / rimCurvature);
}

// position and halfSize describe the pane; pixel is the same point measured
// in the glass texture.
vec4 liquidGlass(vec2 position, vec2 halfSize, vec2 pixel)
{
    float minHalfSize = min(halfSize.x, halfSize.y);
    vec4 radii = clamp(cornerRadius, vec4(0.0), vec4(minHalfSize));
    float radius = position.x > 0.0
        ? (position.y > 0.0 ? radii.y : radii.w)
        : (position.y > 0.0 ? radii.x : radii.z);
    float shapeDistance = roundedRectangleDistance(position, halfSize, radii);
    float shapeCoverage = 1.0 - smoothstep(-0.5, 0.5, shapeDistance);
    float inside = max(0.0, -shapeDistance);

    // The rim band, where the image wraps into the edge.
    float bezel = clamp(minHalfSize * 0.45, 10.0, 72.0);

    // Every layer of the band is the pane's outline set in by its depth, with
    // a corner that grows rounder the deeper the layer lies: the outline keeps
    // the pane's own radius, and each layer adds its depth to it. The bend
    // then turns around the corner gradually, without a crease along the
    // diagonal. edges is the distance to the two nearest straight edges, and
    // the corner of the layer at depth d is a circle of radius radius + d
    // whose center lies radius + 2d in from both edges. Solving for the
    // center's inset c gives the depth (c - radius) / 2.
    vec2 edges = halfSize - abs(position);
    float spread = edges.x + edges.y + 0.25 * radius;
    float squares = dot(edges, edges) - 0.25 * radius * radius;
    float center = (spread + sqrt(max(spread * spread - 1.75 * squares, 0.0))) / 1.75;
    vec2 toCenter = vec2(center) - edges;
    vec2 outward;
    float depth;
    if (toCenter.x > 0.0 && toCenter.y > 0.0) {
        depth = 0.5 * (center - radius);
        outward = normalize(toCenter) * sign(position);
    } else if (edges.x < edges.y) {
        depth = edges.x;
        outward = vec2(sign(position.x), 0.0);
    } else {
        depth = edges.y;
        outward = vec2(0.0, sign(position.y));
    }
    float t = clamp(max(depth, 0.0) / bezel, 0.0, 1.0);

    // Rim surface slope, for the lighting.
    float halfStep = 0.5 / bezel;
    float profileSlope = t < 1.0 ? (rimProfile(t + halfStep) - rimProfile(t - halfStep)) / (2.0 * halfStep) : 0.0;
    float slope = 0.8 * profileSlope;
    vec3 normal = normalize(vec3(outward * slope, 1.0));

    // Rounded glass resting on content magnifies its face and compresses its
    // rim: each point is drawn from 15% closer to the center, and that pull
    // fades across the band along a circular profile to nothing at the edge.
    // The image swells in the middle and wraps into the edge while staying
    // continuous with the content outside. Red and blue split slightly apart
    // where it bends most.
    float fade = clamp(1.0 - t, 0.0, 1.0);
    float lift = sqrt(1.0 - fade * fade);
    const float pull = 0.15;
    vec2 shift = -position * pull * lift;
    // Near the edge the pull fades by more than a pixel per pixel, so each
    // pixel spans a stretch of background too wide for one sharp sample; the
    // frosted copy is already averaged over it, so it shows there instead.
    float outerFade = clamp(fade + 1.0 / bezel, 0.0, 1.0);
    float stretch = pull * length(position) * (lift - sqrt(1.0 - outerFade * outerFade));
    float sharpness = 1.0 - smoothstep(1.0, 3.0, stretch);
    const float fringe = 0.04;
    vec2 texel = 1.0 / max(blurSize, vec2(1.0));
    vec2 baseUV = pixel * texel;
    vec2 minUV = 0.5 * texel;
    vec2 maxUV = vec2(1.0) - 0.5 * texel;
    vec4 sharp = texture(sharpUnit, clamp(baseUV + shift * texel, minUV, maxUV));
    sharp.r = texture(sharpUnit, clamp(baseUV + shift * (1.0 + fringe) * texel, minUV, maxUV)).r;
    sharp.b = texture(sharpUnit, clamp(baseUV + shift * (1.0 - fringe) * texel, minUV, maxUV)).b;
    vec2 frostUV = clamp(baseUV + shift * texel, minUV, maxUV);
    vec4 lightFrost = texture(texUnit, frostUV);
    vec4 quarterFrost = texture(frostUnit, frostUV);
    vec4 heavierFrost = texture(heavierUnit, frostUV);

    // The face goes from the sharp background through the light frost and the
    // quarter frost to the heavier frost, by the size and strength of what
    // lies behind; busy.frag measures that.
    vec4 clear = mix(lightFrost, sharp, sharpness);
    float level = frost * 3.0;
    vec4 color = level < 1.0 ? mix(clear, lightFrost, level)
        : level < 2.0 ? mix(lightFrost, quarterFrost, level - 1.0)
        : mix(quarterFrost, heavierFrost, level - 2.0);

    // KWin draws this glass before the window's content, which is mostly
    // light text and icons. The glass dims in proportion to the background
    // brightness, up to 30% over white, so mid-bright colors and words behind
    // it lose strength too; dark backgrounds are barely touched.
    // Wide taps on the frosting follow the neighborhood rather than fine
    // detail, which would print shifted copies of it; lighting is
    // applied afterward so the rim and highlights keep their brightness.
    vec2 readabilityReach = 28.0 * texel;
    vec2 taps[4] = vec2[](vec2(readabilityReach.x, 0.0), -vec2(readabilityReach.x, 0.0),
                          vec2(0.0, readabilityReach.y), -vec2(0.0, readabilityReach.y));
    vec3 neighborhood = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        neighborhood += 0.25 * texture(frostUnit, clamp(baseUV + taps[i], minUV, maxUV)).rgb;
    }
    float backgroundLuminance = dot(max(neighborhood, color.rgb), vec3(0.2126, 0.7152, 0.0722));
    color.rgb *= 1.0 - 0.3 * backgroundLuminance;

    // The light comes from the top left. lit is 1 where the rim faces it and
    // 0 where it faces away, at the bottom right, and changes evenly with the
    // direction the rim faces, so no corner flares or creases.
    vec2 light = normalize(vec2(-1.0, 1.0));
    float lit = 0.5 + 0.5 * dot(outward, light);

    // The rim band away from the light, along the bottom and the right, shades
    // toward the edge, so the glass reads as a raised slab. The shadow deepens
    // over brighter backgrounds.
    color.rgb *= 1.0 - 0.3 * (1.0 - lit) * (1.0 - t) * (1.0 - t) * (0.4 + 0.6 * backgroundLuminance);

    // Rim light from liquid-glass-js: brightest where the rim faces the light,
    // fading out toward the bottom right. It stays in the outer part of the
    // band, and it is screen-blended so it lightens without clipping.
    float rimWeight = pow(clamp(1.0 - t, 0.0, 1.0), 3.0);
    float rimLight = rimWeight * lit * lit * 0.45;
    color.rgb += rimLight * (1.0 - color.rgb);

    // Fresnel and inner stroke from @ybouane/liquidglass: steep glass reflects
    // more, and a thin line just inside the edge catches light, more on top.
    float fresnel = pow(1.0 - abs(normal.z), 4.0);
    color.rgb = mix(color.rgb, vec3(1.0), fresnel * 0.2);
    const float strokeWidth = 1.5;
    float stroke = smoothstep(-strokeWidth - 1.0, -strokeWidth, shapeDistance)
        * (1.0 - smoothstep(-1.0, 0.0, shapeDistance));
    stroke *= 0.4 + 0.6 * (0.5 + 0.5 * position.y / max(halfSize.y, 1.0));
    color.rgb += vec3(stroke * 0.2);

    // Top sheen from the Godot shader: a soft lift over the upper part of the
    // face, fading out toward the rim.
    float fromTop = (halfSize.y - position.y) / max(2.0 * halfSize.y, 1.0);
    float face = clamp(inside / max(minHalfSize * 0.5, 1.0), 0.0, 1.0);
    color.rgb += vec3(smoothstep(0.4, 0.0, fromTop) * 0.05 * face);

    // Dither below one 8-bit step to avoid banding without visible grain.
    color.rgb += vec3(random(gl_FragCoord.xy) - 0.5) / 255.0;
    color.rgb = clamp(color.rgb, 0.0, 1.0);

    return vec4(color.rgb * shapeCoverage, shapeCoverage);
}

void main(void)
{
    vec2 pixel = texcoord0 * blurSize;
    vec2 halfSize = pane.zw;
    vec2 position = pixel - pane.xy;
    vec4 color;
    if (refraction == 1) {
        color = liquidGlass(position, halfSize, pixel);
    } else if (roundedRectangleDistance(position, halfSize, cornerRadius) <= 0.0) {
        color = frosting(texcoord0);
        color.rgb *= color.a;
    } else {
        color = vec4(0.0);
    }
    fragColor = color * opacity;
}
