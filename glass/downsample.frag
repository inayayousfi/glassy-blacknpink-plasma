#version 140

// One Dual Kawase downsampling pass: average the background around each pixel
// of the half-size texture.

uniform sampler2D texUnit;
uniform float offset;
uniform vec2 halfpixel;

in vec2 texcoord0;
out vec4 fragColor;

void main(void)
{
    vec2 uv = texcoord0;
    vec4 sum = texture(texUnit, uv) * 4.0;
    sum += texture(texUnit, uv + vec2(-halfpixel.x * 2.0, 0.0) * offset);
    sum += texture(texUnit, uv + vec2(-halfpixel.x, halfpixel.y) * offset) * 2.0;
    sum += texture(texUnit, uv + vec2(0.0, halfpixel.y * 2.0) * offset);
    sum += texture(texUnit, uv + vec2(halfpixel.x, halfpixel.y) * offset) * 2.0;
    sum += texture(texUnit, uv + vec2(halfpixel.x * 2.0, 0.0) * offset);
    sum += texture(texUnit, uv + vec2(halfpixel.x, -halfpixel.y) * offset) * 2.0;
    sum += texture(texUnit, uv + vec2(0.0, -halfpixel.y * 2.0) * offset);
    sum += texture(texUnit, uv + vec2(-halfpixel.x, -halfpixel.y) * offset) * 2.0;
    fragColor = sum / 16.0;
}
