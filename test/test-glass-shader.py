#!/usr/bin/env python3
"""Render the glass shader over synthetic scenes offscreen, never capture the desktop."""
import os
from pathlib import Path

os.environ["QT_QPA_PLATFORM"] = "offscreen"

from PySide6.QtCore import QSize
from PySide6.QtGui import QColor, QGuiApplication, QOffscreenSurface, QOpenGLContext, QSurfaceFormat, QVector2D, QVector4D
from PySide6.QtOpenGL import QOpenGLFramebufferObject, QOpenGLShader, QOpenGLShaderProgram, QOpenGLTexture, QOpenGLVertexArrayObject
from PySide6.QtGui import QImage

app = QGuiApplication([])
fmt = QSurfaceFormat()
fmt.setVersion(3, 3)
fmt.setProfile(QSurfaceFormat.CoreProfile)
context = QOpenGLContext()
context.setFormat(fmt)
assert context.create(), "Cannot create offscreen OpenGL context"
surface = QOffscreenSurface()
surface.setFormat(context.format())
surface.create()
assert context.makeCurrent(surface)
functions = context.functions()
functions.initializeOpenGLFunctions()

glass = (Path(__file__).resolve().parent.parent / "glass/glass.frag").read_text()
program = QOpenGLShaderProgram()
vertex = """#version 140
out vec2 texcoord0;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    texcoord0 = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
"""
fragment = glass
assert program.addShaderFromSourceCode(QOpenGLShader.Vertex, vertex), program.log()
assert program.addShaderFromSourceCode(QOpenGLShader.Fragment, fragment), program.log()
assert program.link(), program.log()
assert program.bind()

def pattern(width, height, color):
    image = QImage(width, height, QImage.Format_RGBA8888)
    for y in range(height):
        for x in range(width):
            image.setPixelColor(x, y, QColor(color(x, y)))
    # Like KWin's offscreen textures: linear filtering, no smaller copies.
    texture = QOpenGLTexture(image, QOpenGLTexture.DontGenerateMipMaps)
    texture.setMinMagFilters(QOpenGLTexture.Linear, QOpenGLTexture.Linear)
    texture.setWrapMode(QOpenGLTexture.ClampToEdge)
    return texture

# Unless a test says otherwise, the same scene stands in for the sharp
# background and every frosting, and the glass does not frost.
uniforms = {"texUnit": 0, "sharpUnit": 1, "frostUnit": 2, "heavierUnit": 3, "frost": 0.0, "offset": 0.0, "cornerRadius": QVector4D(18, 18, 18, 18), "opacity": 1.0}

vao = QOpenGLVertexArrayObject()
assert vao.create()

def render(texture, width, height, refraction=1, light=None, heavy=None):
    fbo = QOpenGLFramebufferObject(QSize(width, height))
    assert fbo.isValid() and fbo.bind()
    assert program.bind()
    vao.bind()
    (light or texture).bind(0)
    texture.bind(1)
    (heavy or texture).bind(2)
    (heavy or texture).bind(3)
    size = {"blurSize": QVector2D(width, height), "halfpixel": QVector2D(0.5 / width, 0.5 / height),
            "pane": QVector4D(width / 2, height / 2, width / 2, height / 2), "refraction": refraction}
    for name, value in {**uniforms, **size}.items():
        location = program.uniformLocation(name)
        if isinstance(value, int):
            functions.glUniform1i(location, value)
        elif isinstance(value, float):
            functions.glUniform1f(location, value)
        else:
            program.setUniformValue(location, value)
    functions.glViewport(0, 0, width, height)
    functions.glClearColor(0, 0, 0, 0)
    functions.glClear(0x4000)
    functions.glDrawArrays(0x0004, 0, 3)
    assert functions.glGetError() == 0, "Offscreen draw failed"
    image = fbo.toImage(True, 0).convertToFormat(QImage.Format_RGBA8888_Premultiplied)
    fbo.release()
    return image

checker = pattern(256, 128, lambda x, y: "white" if (x // 8 + y // 8) % 2 else "black")
plain = render(checker, 256, 128, 0)
liquid = render(checker, 256, 128)

def differences(first, second, rows, columns=range(32, 224)):
    return sum(abs(first.pixelColor(x, y).red() - second.pixelColor(x, y).red()) > 20
               for y in rows for x in columns)

assert differences(plain, liquid, range(4, 20)) > 100, "Glass did not refract the edge"
# The face is lit and dimmed, so compare which squares are light, not values.
center = [(x, y) for y in range(60, 68) for x in range(124, 132)]
levels = [liquid.pixelColor(x, y).red() for x, y in center]
midpoint = (min(levels) + max(levels)) / 2
assert max(levels) - min(levels) > 60 and all(
    (plain.pixelColor(x, y).red() > 127) == (level > midpoint)
    for (x, y), level in zip(center, levels)), "Glass moved the center anchor"
assert max(liquid.pixelColor(x, y).alpha() for y in range(1, 4) for x in range(1, 4)) < 8, \
    "Glass did not make pixels outside its adaptive rounded rectangle transparent"

# Premultiplied output must never carry more color than coverage, or KWin's
# GL_ONE blend adds stray light over the desktop.
white = pattern(64, 64, lambda x, y: "white")
for image in (liquid, render(white, 256, 128)):
    # pixelColor() unpremultiplies, so read the stored RGBA bytes instead.
    data = bytes(image.constBits())[:image.sizeInBytes()]
    assert all(max(data[i:i + 3]) <= data[i + 3] for i in range(0, len(data), 4)), \
        "Glass produced premultiplied color brighter than its coverage"

# Rounded glass refracts rather than stretches: its rim compresses the
# background and its face magnifies it. Through horizontal stripes 4 px tall,
# count stripe edges down the middle column of the rim and of the face.
stripes = pattern(512, 256, lambda x, y: "white" if (y // 4) % 2 else "black")
lens = render(stripes, 512, 256)

def stripe_edges(rows):
    levels = [lens.pixelColor(256, y).red() for y in rows]
    midpoint = (min(levels) + max(levels)) / 2
    return sum((a > midpoint) != (b > midpoint) for a, b in zip(levels, levels[1:]))

rim_rows, face_rows = range(2, 40), range(80, 176)
assert stripe_edges(rim_rows) > len(rim_rows) / 4, "Glass rim stretches the background instead of compressing it"
assert stripe_edges(face_rows) < len(face_rows) / 4, "Glass face does not magnify the background"

# The tips of a long pill are antialiased over about one pixel, like its sides.
pill = render(white, 400, 40)
row = [pill.pixelColor(x, 20).alpha() for x in range(40)]
assert sum(1 for alpha in row if 8 < alpha < 247) <= 2, f"Pill tip edge is soft: {row}"

# The glass carries no black tint: over white, every surface dims by about 30%
# at most, and over a dark background it is barely touched, within 3 of 255
# levels.
bright_face = render(white, 256, 128).pixelColor(128, 64).red()
assert 165 <= bright_face <= 205, f"Glass over white is not dimmed by about 30%: {bright_face}"
dark_face = render(pattern(64, 64, lambda x, y: "#2a2a2a"), 256, 128).pixelColor(128, 64).red()
assert dark_face >= 0x2a - 3, f"Glass darkened a dark background: {dark_face}"

# Fully frosted glass shows the heaviest frost; unfrosted glass shows the
# background sharply.
stripes = pattern(256, 128, lambda x, y: "white" if (x // 4) % 2 else "black")
gray = pattern(256, 128, lambda x, y: "#808080")
def face_spread(image):
    values = [image.pixelColor(x, 64).red() for x in range(96, 160)]
    return max(values) - min(values)
uniforms["frost"] = 1.0
frosted_face = render(stripes, 256, 128, heavy=gray)
uniforms["frost"] = 0.0
clear_face = render(stripes, 256, 128)
assert face_spread(frosted_face) <= 12, f"Frosted glass did not show the heavy frost: spread {face_spread(frosted_face)}"
assert face_spread(clear_face) >= 100, f"Unfrosted glass blurred the background: spread {face_spread(clear_face)}"

# The frost amount follows the size and strength of what lies behind the
# pane. The detail map holds fine, medium and coarse detail, scaled by 4, as
# measured in KWin: soft shapes leave the glass clear, dense 22 px text
# frosts glass that holds text past the light frost but short of the
# heavier one, and a few large bold words on a dark page frost it most of
# the way, even when they cover only half of the pane.
busy = QOpenGLShaderProgram()
assert busy.addShaderFromSourceCode(QOpenGLShader.Vertex, vertex), busy.log()
assert busy.addShaderFromSourceCode(QOpenGLShader.Fragment, (Path(__file__).resolve().parent.parent / "glass/busy.frag").read_text()), busy.log()
assert busy.link(), busy.log()

def frost_amount(detail, content, covered=1.0):
    fine, medium, coarse = (round(value * 255) for value in detail)
    texture = pattern(32, 16, lambda x, y: QColor(fine, medium, coarse) if x < 32 * covered else QColor(0, 0, 0))
    fbo = QOpenGLFramebufferObject(QSize(1, 1))
    assert fbo.isValid() and fbo.bind() and busy.bind()
    vao.bind()
    texture.bind(0)
    functions.glUniform1i(busy.uniformLocation("texUnit"), 0)
    functions.glUniform1f(busy.uniformLocation("content"), content)
    busy.setUniformValue(busy.uniformLocation("pane"), QVector4D(128, 64, 120, 60))
    busy.setUniformValue(busy.uniformLocation("blurSize"), QVector2D(256, 128))
    functions.glViewport(0, 0, 1, 1)
    functions.glDrawArrays(0x0004, 0, 3)
    assert functions.glGetError() == 0, "Offscreen draw failed"
    amount = fbo.toImage(True, 0).pixelColor(0, 0).red() / 255
    fbo.release()
    return amount

soft_shapes = (0.004, 0.03, 0.11)
dense_text = (0.11, 0.19, 0.29)
large_words = (0.10, 0.27, 0.41)
for content in (0.0, 1.0):
    assert frost_amount(soft_shapes, content) == 0, f"Soft shapes frosted the glass: {frost_amount(soft_shapes, content)}"
dense = frost_amount(dense_text, 1.0)
assert 1.3 / 3 <= dense <= 2.2 / 3, f"Dense text did not frost between the light and the heavier frost: {dense}"
assert frost_amount(large_words, 1.0) >= 2.5 / 3, f"Large words did not frost most of the way: {frost_amount(large_words, 1.0)}"
assert frost_amount(large_words, 1.0, covered=0.5) >= 2.0 / 3, \
    f"Large words over half the pane were diluted by the empty half: {frost_amount(large_words, 1.0, covered=0.5)}"
assert program.bind()

assert "OverShifted/LiquidGlass" in glass, "LiquidGlass source attribution is missing"
print("Glass shader checks passed: compressed rim and magnified face, anchored center, crisp tips, valid premultiplied output, no black tint, frost only where the background is busy.")

program.release()
vao.destroy()
