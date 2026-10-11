#version 140

// How much a pane of glass should frost, as one value for the whole pane,
// written to a single pixel. The effect reads it back and eases the glass
// toward it over time.
//
// The glass frosts only as much as the background needs to stop competing
// with what the surface shows. The detail map holds how much the background
// changes at three sizes, from small text to large text and shapes. Read
// from a grid over the pane, the strongest parts count most, so a few large
// words on an empty page weigh like a page full of them. Each frost step
// removes one size of detail: the light frost the fine detail, the quarter
// frost the medium detail, the heavier frost the coarse detail. The glass
// takes each step only while what would remain after the step before is
// still strong enough to read, so small text gets a light blur and large
// text a stronger one. Glass that holds text frosts from weaker detail than
// the shell.

uniform sampler2D texUnit; // the detail map
uniform vec4 pane;         // center and half size of the pane, in glass pixels
uniform vec2 blurSize;     // glass pixels covered by the detail map
uniform float content;     // 1 when the surface holds text to read, 0 for the shell

in vec2 texcoord0;
out vec4 fragColor;

void main(void)
{
    vec2 texel = 1.0 / max(blurSize, vec2(1.0));
    // A mean of fourth powers leans toward the strongest cells of the grid.
    vec3 strong = vec3(0.0);
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 6; ++x) {
            vec2 cell = (vec2(x, y) + 0.5) / 6.0 * 2.0 - 1.0;
            vec3 cellDetail = texture(texUnit, clamp((pane.xy + cell * pane.zw) * texel, vec2(0.0), vec2(1.0))).rgb;
            vec3 squared = cellDetail * cellDetail;
            strong += squared * squared;
        }
    }
    vec3 detail = sqrt(sqrt(strong / 36.0));
    // What remains readable through the sharp glass, the light frost and the
    // quarter frost.
    vec3 remaining = vec3(detail.r + detail.g + detail.b, detail.g + detail.b, detail.b);
    // Each step starts and completes at these strengths, measured as four
    // times the luminance change: with glass that holds text, dense 22 px
    // text (0.59, 0.48, 0.29) takes the light frost and most of the quarter
    // frost, a few large bold words on a dark page (0.78, 0.68, 0.41) go
    // most of the way to the heavier frost, and soft shapes (0.14, 0.14,
    // 0.11) stay sharp.
    const vec3 start = vec3(0.214, 0.4, 0.429);
    const vec3 complete = vec3(0.571, 0.857, 0.643);
    float scale = mix(1.0, 0.7, content);
    vec3 steps = smoothstep(start * scale, complete * scale, remaining);
    fragColor = vec4(vec3((steps.x + steps.y + steps.z) / 3.0), 1.0);
}
