#version 140

// Detail map: how much the background changes around each pixel of a
// half-size texture, at three sizes, one per channel. Red holds the fine
// detail that the light frost removes, such as small text; green the medium
// detail that the quarter-size frost removes next; blue the coarse detail
// that only the heavier frost removes, such as large text and shapes. Flat
// areas and soft gradients give nothing. The values are scaled up by four
// to keep their precision in 8-bit textures.

uniform sampler2D texUnit;     // the full-size background, unblurred
uniform sampler2D frostUnit;   // its half-size light frost
uniform sampler2D quarterUnit; // its quarter-size frost
uniform sampler2D heavierUnit; // its heavier frost, at a quarter size
uniform vec2 halfpixel;        // half a pixel of the full-size background

in vec2 texcoord0;
out vec4 fragColor;

void main(void)
{
    const vec3 luminance = vec3(0.2126, 0.7152, 0.0722);
    float light = dot(texture(frostUnit, texcoord0).rgb, luminance);
    float fine = 0.0;
    for (int y = -1; y <= 1; y += 2) {
        for (int x = -1; x <= 1; x += 2) {
            fine += abs(dot(texture(texUnit, texcoord0 + vec2(x, y) * halfpixel).rgb, luminance) - light);
        }
    }
    float quarter = dot(texture(quarterUnit, texcoord0).rgb, luminance);
    float medium = abs(light - quarter);
    float coarse = abs(quarter - dot(texture(heavierUnit, texcoord0).rgb, luminance));
    fragColor = vec4(min(4.0 * vec3(fine * 0.25, medium, coarse), vec3(1.0)), 1.0);
}
