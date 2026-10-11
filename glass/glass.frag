#version 140

// Draws the frosted background through the glass, as premultiplied color for
// KWin's GL_ONE/GL_ONE_MINUS_SRC_ALPHA blend.
//
// The refraction and lighting are adapted from OverShifted/LiquidGlass,
// assets/shaders/BatchRenderer2D.glsl. Copyright (c) 2026 Sepehr Kalanaki.
// Licensed under the MIT License; see LICENSE-OverShifted-LiquidGlass.

uniform sampler2D texUnit; // the half-size frosted background
uniform float offset;
uniform vec2 halfpixel;
uniform vec4 cornerRadius; // device pixels: top left, top right, bottom right, bottom left
uniform vec2 blurSize;     // device pixels covered by the glass
uniform vec4 pane;         // center and half size of the whole pane, in the same pixels
uniform float opacity;
uniform int refraction;    // 0 draws the frosting alone, inside the rounded rectangle

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

// position and halfSize describe the pane; pixel is the same point measured
// in the glass texture.
vec4 liquidGlass(vec2 position, vec2 halfSize, vec2 pixel)
{
    float minHalfSize = min(halfSize.x, halfSize.y);
    // The rim band is the part of the slab that bends light across the edge.
    float bevelWidth = clamp(minHalfSize * 0.45, 10.0, 72.0);

    // One exact rounded-rectangle distance field drives the silhouette, the
    // rim optics, and the rim lighting.
    vec4 radii = clamp(cornerRadius, vec4(0.0), vec4(minHalfSize));
    float radius = position.x > 0.0
        ? (position.y > 0.0 ? radii.y : radii.w)
        : (position.y > 0.0 ? radii.x : radii.z);
    vec2 corner = abs(position) - (halfSize - vec2(radius));
    float shapeDistance = length(max(corner, 0.0))
        + min(max(corner.x, corner.y), 0.0) - radius;
    vec2 outward = corner.x > 0.0 && corner.y > 0.0
        ? normalize(corner)
        : (corner.x > corner.y ? vec2(1.0, 0.0) : vec2(0.0, 1.0));
    outward *= vec2(position.x < 0.0 ? -1.0 : 1.0, position.y < 0.0 ? -1.0 : 1.0);
    float rimDistance = max(0.0, -shapeDistance);
    float shapeCoverage = 1.0 - smoothstep(-0.5, 0.5, shapeDistance);

    // A band wider than its corner radius folds into a separate facet at the
    // corner. Keep the band wide along the edges and narrow it to the corner
    // radius as it approaches each corner, so the rim bends continuously.
    float alongEdge = max(0.0, -min(corner.x, corner.y));
    bevelWidth = mix(max(min(radius, bevelWidth), 2.0), bevelWidth,
        smoothstep(0.0, bevelWidth * 1.5, alongEdge));

    // The glass is a slab with a rounded, convex rim and a shallow dome over
    // its face. Their normals light the surface. The dome is a smooth oval
    // so the face has no creases of its own.
    float bevelHeight = bevelWidth * 0.85;
    float bevel = clamp(rimDistance / bevelWidth, 0.02, 1.0);
    float bevelRise = 1.0 - bevel;
    float bevelProfile = sqrt(1.0 - bevelRise * bevelRise);
    float rimSlope = min(bevelHeight / bevelWidth * bevelRise / bevelProfile, 8.0);
    float domeHeight = min(minHalfSize * 0.06, 10.0);
    vec2 domePosition = position / max(halfSize, vec2(1.0));
    vec2 domeGradient = 2.0 * domeHeight * domePosition / max(halfSize, vec2(1.0));
    vec3 normal = normalize(vec3(outward * rimSlope + domeGradient, 1.0));

    // Rounded glass resting on content magnifies its face and compresses
    // its rim: the image swells toward the middle and wraps into the edge
    // while staying continuous with the content outside. Draw each point
    // toward the center, fading along the rim profile to nothing at the
    // edge. The magnification is the same in both directions so text on the
    // face keeps its proportions; only the rim compresses.
    vec2 displacement = -position * 0.12 * bevelProfile;
    vec2 texel = 1.0 / max(blurSize, vec2(1.0));
    vec2 baseUV = pixel * texel;
    vec2 minUV = 0.5 * texel;
    vec2 maxUV = vec2(1.0) - 0.5 * texel;
    vec2 sampleUV = clamp(baseUV + displacement * texel, minUV, maxUV);
    vec4 color = texture(texUnit, sampleUV);

    // A very small symmetric diffusion gives the surface microscopic
    // roughness without replacing the coherent refracted image with blur.
    vec2 roughnessOffset = vec2(1.25, -0.85) * texel;
    vec4 roughTransmission = 0.5 * (
        texture(texUnit, clamp(sampleUV + roughnessOffset, minUV, maxUV))
        + texture(texUnit, clamp(sampleUV - roughnessOffset, minUV, maxUV)));
    color.rgb = mix(color.rgb, roughTransmission.rgb, 0.08);

    // KWin draws this glass before the window's content, which is mostly
    // light text and icons. A continuous tone curve dims the glass in
    // proportion to the square of the surrounding background brightness:
    // dark areas are barely touched and bright ones come down the most, so
    // that content stays readable without a threshold. Wide taps follow the
    // neighborhood rather than fine detail; lighting is applied afterward so
    // the rim and highlights keep their brightness.
    vec2 readabilityReach = 28.0 * texel;
    vec3 neighborhood = 0.25 * (
        texture(texUnit, clamp(baseUV + vec2(readabilityReach.x, 0.0), minUV, maxUV)).rgb
        + texture(texUnit, clamp(baseUV - vec2(readabilityReach.x, 0.0), minUV, maxUV)).rgb
        + texture(texUnit, clamp(baseUV + vec2(0.0, readabilityReach.y), minUV, maxUV)).rgb
        + texture(texUnit, clamp(baseUV - vec2(0.0, readabilityReach.y), minUV, maxUV)).rgb);
    float backgroundLuminance = dot(max(neighborhood, color.rgb), vec3(0.2126, 0.7152, 0.0722));
    color.rgb *= 1.0 - 0.44 * backgroundLuminance * backgroundLuminance;

    // Light the curved surface from the top left. Shading follows the real
    // surface normal, so the dome brightens toward the light and the rim
    // facing away falls into a soft shade.
    vec3 keyLight = normalize(vec3(-0.70, 0.70, 0.72));
    vec3 view = vec3(0.0, 0.0, 1.0);
    color.rgb *= 1.0 + 0.22 * (dot(normal, keyLight) - keyLight.z);
    // The real dome is too shallow to light the face visibly over a dark
    // desktop, so a broader copy of it lifts the top left and shades the
    // bottom right.
    vec3 bodyNormal = normalize(vec3(domePosition * 0.9, 1.0));
    float bodyLight = smoothstep(-0.20, 0.92, dot(bodyNormal, keyLight));
    color.rgb *= 1.0 - 0.09 * (1.0 - bodyLight);
    color.rgb = mix(color.rgb, vec3(1.0), 0.060 * bodyLight);

    // Glass reflects more at grazing angles, so the steep rim mirrors a soft
    // bright environment on every side. This is what gives the edge depth.
    float cosine = clamp(normal.z, 0.0, 1.0);
    float fresnel = 0.04 + 0.96 * pow(1.0 - cosine, 5.0);
    vec3 reflected = reflect(-view, normal);
    float environment = 0.30 + 0.70 * smoothstep(-0.70, 0.90, dot(reflected, keyLight));
    color.rgb = mix(color.rgb, vec3(environment), clamp(fresnel, 0.0, 0.45));

    // Light catches the steep part of the rim as a thin line. It is
    // brightest where the rim faces the light and reappears, weaker, on the
    // opposite rim where light leaves the glass after crossing it. A broad
    // angular falloff keeps the line continuous along straight edges.
    float steepness = smoothstep(0.30, 0.85, 1.0 - cosine);
    float lightAlignment = dot(outward, normalize(keyLight.xy));
    float keyLobe = pow(smoothstep(-0.35, 1.0, lightAlignment), 2.0);
    float exitLobe = pow(smoothstep(-0.35, 1.0, -lightAlignment), 2.0);
    float rimLight = steepness * (0.04 + 0.47 * keyLobe + 0.085 * exitLobe);
    // Light entering through the lit shoulder spreads into the rim, and the
    // curved body gathers it again just inside the opposite edge.
    float shoulderGlow = 0.085 * keyLobe * bevelRise * bevelRise;
    float exitCaustic = 0.04 * exitLobe * pow(bevelRise, 1.5) * (1.0 - steepness);
    color.rgb = mix(color.rgb, vec3(1.0), clamp(rimLight + shoulderGlow + exitCaustic, 0.0, 0.65));

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
