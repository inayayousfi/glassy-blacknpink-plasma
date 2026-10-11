/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2011 Philipp Knechtges <philipp-dev@knechtges.com>
    SPDX-FileCopyrightText: 2018 Alex Nemeth <alex.nemeth329@gmail.com>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

// Glassy Black & Pink glass: KWin's blur, trimmed from 4v3ngR/kwin-effects-glass to
// the one look this desktop uses. Windows that request a blur behind them get
// a lightly frosted copy of the background, bent by the refraction shader in
// glass.frag. Tooltips keep the frosting without refraction.

#include "core/pixelgrid.h"
#include "core/region.h"
#include "core/rendertarget.h"
#include "core/renderviewport.h"
#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "opengl/eglcontext.h"
#include "opengl/glutils.h"
#include "scene/backgroundeffectitem.h"
#include "scene/windowitem.h"
#include "wayland/backgroundeffect_v1.h"
#include "wayland/surface.h"
#include "wayland_server.h"
#include "window.h"

#include <KDecoration3/Decoration>

#include <QFile>
#include <QWindow>

#include <unordered_map>

Q_LOGGING_CATEGORY(GLASS, "glassy_blacknpink_glass", QtWarningMsg)

// The resource must be registered from the global namespace.
static void initResources()
{
    Q_INIT_RESOURCE(glass);
}

namespace KWin
{

// Corner radii in logical pixels, matching the Glassy Black & Pink Plasma style.
constexpr float s_windowRadius = 18;
constexpr float s_dockRadius = 16;
constexpr float s_menuRadius = 12;
// One half-size pass at this sampling distance gives the light frosting.
// It equals the old effect at blur strength 1 (offset 1.5) and fine-tune 3 (x0.89).
constexpr float s_blurOffset = 1.335f;
// The blur reads this far past the glass, so repaints must reach it too.
constexpr int s_blurReach = 10;

static std::unique_ptr<GLShader> loadShader(const QString &name)
{
    QFile file(QStringLiteral(":/glassy-blacknpink-glass/%1.frag").arg(name));
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(GLASS) << "Missing shader" << name;
        return nullptr;
    }
    auto shader = ShaderManager::instance()->generateCustomShader(ShaderTrait::MapTexture, QByteArray(), file.readAll());
    if (!shader) {
        qCWarning(GLASS) << "Failed to compile shader" << name;
        return nullptr;
    }
    return shader;
}

class GlassEffect : public Effect
{
    Q_OBJECT

public:
    GlassEffect();
    ~GlassEffect() override;

    static bool supported()
    {
        return effects->isOpenGLCompositing();
    }

    void prePaintScreen(ScreenPrePaintData &data) override;
    void prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data) override;
    void drawWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const Region &deviceRegion, WindowPaintData &data) override;

    bool provides(Feature feature) override
    {
        return feature == Blur || Effect::provides(feature);
    }
    bool isActive() const override
    {
        return m_valid && !effects->isScreenLocked();
    }
    bool blocksDirectScanout() const override
    {
        return false;
    }
    int requestedEffectChainPosition() const override
    {
        return 20;
    }
    // The installer reads this to confirm that the shaders compiled.
    QString debug(const QString &) const override
    {
        return m_valid ? QStringLiteral("valid=1") : QStringLiteral("valid=0");
    }

    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct RenderTargets
    {
        // [0] holds the background behind the window, [1] its half-size blur.
        std::unique_ptr<GLTexture> textures[2];
        std::unique_ptr<GLFramebuffer> framebuffers[2];

        void release()
        {
            for (int i = 0; i < 2; ++i) {
                framebuffers[i].reset();
                textures[i].reset();
            }
        }
    };

    struct Glass
    {
        // The region the client requested, relative to its contents; empty
        // means all of them.
        std::optional<Region> content;
        // The title bar and borders that the window decoration requested,
        // relative to the frame.
        Region frame;
        std::unique_ptr<BackgroundEffectItem> item;
        // Each view can have its own color format, so each gets its own targets.
        std::unordered_map<RenderView *, RenderTargets> targets;
    };

    void addWindow(EffectWindow *w);
    void watchDecoration(EffectWindow *w);
    void removeWindow(EffectWindow *w);
    void updateRegion(EffectWindow *w);
    Region shape(EffectWindow *w, const Glass &glass) const;
    Rect slab(EffectWindow *w, const Glass &glass) const;
    bool shouldDraw(const EffectWindow *w, int mask, const WindowPaintData &data) const;
    void draw(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, Glass &glass, const Region &deviceRegion, const WindowPaintData &data);

    std::unique_ptr<GLShader> m_downsample;
    std::unique_ptr<GLShader> m_glass;
    bool m_valid = false;
    RenderView *m_currentView = nullptr;
    std::unordered_map<EffectWindow *, Glass> m_windows;
    std::unordered_map<EffectWindow *, QMetaObject::Connection> m_surfaceConnections;
};

GlassEffect::GlassEffect()
{
    initResources();
    m_downsample = loadShader(QStringLiteral("downsample"));
    m_glass = loadShader(QStringLiteral("glass"));
    if (!m_downsample || !m_glass) {
        return;
    }

    waylandServer()->backgroundEffectManager()->addBlurCapability();
    connect(effects, &EffectsHandler::windowAdded, this, &GlassEffect::addWindow);
    connect(effects, &EffectsHandler::windowDeleted, this, &GlassEffect::removeWindow);
    connect(effects, &EffectsHandler::viewRemoved, this, [this](RenderView *view) {
        effects->makeOpenGLContextCurrent();
        for (auto &[window, glass] : m_windows) {
            glass.targets.erase(view);
        }
    });
    for (EffectWindow *window : effects->stackingOrder()) {
        addWindow(window);
    }
    m_valid = true;
}

GlassEffect::~GlassEffect()
{
    if (m_valid) {
        waylandServer()->backgroundEffectManager()->removeBlurCapability();
    }
}

void GlassEffect::addWindow(EffectWindow *w)
{
    if (SurfaceInterface *surface = w->surface()) {
        m_surfaceConnections[w] = connect(surface, &SurfaceInterface::blurChanged, this, [this, w]() {
            updateRegion(w);
        });
    }
    connect(w, &EffectWindow::windowFrameGeometryChanged, this, [this, w]() {
        updateRegion(w);
    });
    connect(w, &EffectWindow::windowDecorationChanged, this, [this, w]() {
        watchDecoration(w);
        updateRegion(w);
    });
    watchDecoration(w);
    // KWin's own windows request blur through a property instead of Wayland.
    if (QWindow *internal = w->internalWindow()) {
        internal->installEventFilter(this);
    }
    updateRegion(w);
}

void GlassEffect::watchDecoration(EffectWindow *w)
{
    if (KDecoration3::Decoration *decoration = w->decoration()) {
        connect(decoration, &KDecoration3::Decoration::blurRegionChanged, this, [this, w]() {
            updateRegion(w);
        }, Qt::UniqueConnection);
    }
}

void GlassEffect::removeWindow(EffectWindow *w)
{
    if (auto it = m_windows.find(w); it != m_windows.end()) {
        effects->makeOpenGLContextCurrent();
        m_windows.erase(it);
    }
    if (auto it = m_surfaceConnections.find(w); it != m_surfaceConnections.end()) {
        disconnect(it->second);
        m_surfaceConnections.erase(it);
    }
}

bool GlassEffect::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::DynamicPropertyChange
        && static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName() == "kwin_blur") {
        if (EffectWindow *w = effects->findWindow(qobject_cast<QWindow *>(watched))) {
            updateRegion(w);
        }
    }
    return false;
}

void GlassEffect::updateRegion(EffectWindow *w)
{
    std::optional<Region> requested;
    if (SurfaceInterface *surface = w->surface()) {
        const RegionF surfaceRegion = surface->blurRegion();
        if (!surfaceRegion.isEmpty()) {
            Region region;
            for (const RectF &rect : surfaceRegion.rects()) {
                region += rect.toAlignedRect();
            }
            requested = region;
        }
    }
    if (QWindow *internal = w->internalWindow()) {
        const QVariant property = internal->property("kwin_blur");
        if (property.isValid()) {
            requested = property.value<Region>();
        }
    }

    Region frame;
    if (KDecoration3::Decoration *decoration = w->decoration(); decoration && w->decorationHasAlpha()
        && !decoration->blurRegion().isNull()) {
        frame = (Region(Rect(decoration->rect().toAlignedRect())) - w->contentsRect().toAlignedRect())
            & Region(decoration->blurRegion());
    }

    if (!requested && frame.isEmpty()) {
        if (auto it = m_windows.find(w); it != m_windows.end()) {
            effects->makeOpenGLContextCurrent();
            m_windows.erase(it);
        }
        return;
    }

    Glass &glass = m_windows[w];
    glass.content = requested;
    glass.frame = frame;
    if (!glass.item) {
        glass.item = std::make_unique<BackgroundEffectItem>(w->windowItem());
        glass.item->setPixelsToExpandRepaintsBelowOpaqueRegions(s_blurReach);
    }
    glass.item->setEffectBoundingRect(shape(w, glass).boundingRect());
}

Region GlassEffect::shape(EffectWindow *w, const Glass &glass) const
{
    const QRect contents = w->contentsRect().toAlignedRect();
    Region region = glass.frame;
    if (!glass.content) {
        return region;
    }
    if (glass.content->isEmpty()) {
        return region | Rect(contents);
    }
    return region | (glass.content->translated(contents.x(), contents.y()) & Rect(contents));
}

// The pane of glass whose rim and lighting the shader draws. A decorated
// window is one pane from its title bar to its bottom edge, even where only
// part of it is glass.
Rect GlassEffect::slab(EffectWindow *w, const Glass &glass) const
{
    if (w->decoration()) {
        return Rect(QPoint(0, 0), w->frameGeometry().size().toSize());
    }
    return shape(w, glass).boundingRect();
}

static float cornerRadius(EffectWindow *w)
{
    if (w->isFullScreen() || w->window()->maximizeMode() == MaximizeFull) {
        return 0;
    }
    float radius = s_windowRadius;
    if (w->isDock()) {
        radius = s_dockRadius;
    } else if (!w->isTooltip() && !w->isOnScreenDisplay()
               && (w->isMenu() || w->isDropdownMenu() || w->isPopupMenu() || w->isPopupWindow())) {
        radius = s_menuRadius;
    }
    // A radius wider than the window would fold its corners over each other.
    const QRectF frame = w->frameGeometry();
    if (radius * 2 > frame.width() || radius * 2 > frame.height()) {
        radius = w->isDock() ? 0 : std::min(frame.width(), frame.height()) / 2;
    }
    return radius;
}

void GlassEffect::prePaintScreen(ScreenPrePaintData &data)
{
    m_currentView = data.view;
    effects->prePaintScreen(data);
}

void GlassEffect::prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data)
{
    effects->prePaintWindow(view, w, data);
    if (m_windows.contains(w)) {
        data.setTranslucent();
    }
}

bool GlassEffect::shouldDraw(const EffectWindow *w, int mask, const WindowPaintData &data) const
{
    // Effects that animate a window, such as sliding popups, set this role to keep its glass.
    const bool forced = w->data(WindowForceBlurRole).toBool();
    if ((effects->activeFullScreenEffect() && !forced) || w->isDesktop()) {
        return false;
    }
    const bool scaled = !qFuzzyCompare(data.xScale(), 1.0) && !qFuzzyCompare(data.yScale(), 1.0);
    const bool translated = data.xTranslation() || data.yTranslation();
    return forced || !(scaled || translated || (mask & PAINT_WINDOW_TRANSFORMED));
}

void GlassEffect::drawWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const Region &deviceRegion, WindowPaintData &data)
{
    if (auto it = m_windows.find(w); it != m_windows.end() && shouldDraw(w, mask, data)) {
        draw(renderTarget, viewport, w, it->second, deviceRegion, data);
    }
    effects->drawWindow(renderTarget, viewport, w, mask, deviceRegion, data);
}

void GlassEffect::draw(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, Glass &glass, const Region &deviceRegion, const WindowPaintData &data)
{
    Region glassShape = shape(w, glass);
    glassShape.translate(w->pos().toPoint());
    Rect pane = slab(w, glass).translated(w->pos().toPoint());
    if (data.xScale() != 1 || data.yScale() != 1) {
        const QPoint origin = glassShape.boundingRect().topLeft();
        auto transform = [&](const Rect &rect) {
            const QPointF topLeft(origin.x() + (rect.x() - origin.x()) * data.xScale() + data.xTranslation(),
                                  origin.y() + (rect.y() - origin.y()) * data.yScale() + data.yTranslation());
            const QPoint bottomRight(std::floor(topLeft.x() + rect.width() * data.xScale()) - 1,
                                     std::floor(topLeft.y() + rect.height() * data.yScale()) - 1);
            return Rect(QPoint(std::floor(topLeft.x()), std::floor(topLeft.y())), bottomRight);
        };
        Region scaled;
        for (const Rect &rect : glassShape.rects()) {
            scaled += transform(rect);
        }
        glassShape = scaled;
        pane = transform(pane);
    } else if (data.xTranslation() || data.yTranslation()) {
        const QPoint translation(std::round(data.xTranslation()), std::round(data.yTranslation()));
        glassShape.translate(translation);
        pane.translate(translation);
    }

    const QRect backgroundRect = glassShape.boundingRect();
    const QRect scaledBackgroundRect = snapToPixelGrid(QRectF(backgroundRect.x() * viewport.scale(),
                                                              backgroundRect.y() * viewport.scale(),
                                                              backgroundRect.width() * viewport.scale(),
                                                              backgroundRect.height() * viewport.scale()));
    const QRect deviceBackgroundRect = viewport.mapToDeviceCoordinates(Rect(backgroundRect)).rounded();

    // Draw only the part of the glass that this repaint touches.
    QList<RectF> screenShape;
    for (const Rect &shapeRect : glassShape.rects()) {
        const RectF deviceShapeRect = shapeRect.translated(-backgroundRect.topLeft()).scaled(viewport.scale()).rounded();
        if (deviceRegion == Region::infinite()) {
            screenShape.append(deviceShapeRect);
            continue;
        }
        for (const Rect &clipRect : deviceRegion.rects()) {
            const RectF deviceClipRect = clipRect.translated(-deviceBackgroundRect.topLeft());
            if (const QRectF clipped = deviceClipRect.intersected(deviceShapeRect); !clipped.isEmpty()) {
                screenShape.append(clipped);
            }
        }
    }
    if (screenShape.isEmpty()) {
        return;
    }

    const GLenum textureFormat = renderTarget.texture() ? renderTarget.texture()->internalFormat() : GL_RGBA8;
    RenderTargets &targets = glass.targets[m_currentView];
    if (!targets.textures[0] || targets.textures[0]->size() != backgroundRect.size() || targets.textures[0]->internalFormat() != textureFormat) {
        targets.release();
        glClearColor(0, 0, 0, 0);
        for (int i = 0; i < 2; ++i) {
            targets.textures[i] = GLTexture::allocate(textureFormat, backgroundRect.size() / (1 << i));
            if (!targets.textures[i]) {
                qCWarning(GLASS) << "Failed to allocate an offscreen texture";
                targets.release();
                return;
            }
            targets.textures[i]->setFilter(GL_LINEAR);
            targets.textures[i]->setWrapMode(GL_CLAMP_TO_EDGE);
            targets.framebuffers[i] = std::make_unique<GLFramebuffer>(targets.textures[i].get());
            if (!targets.framebuffers[i]->valid()) {
                qCWarning(GLASS) << "Failed to create an offscreen framebuffer";
                targets.release();
                return;
            }
            EglContext::currentContext()->pushFramebuffer(targets.framebuffers[i].get());
            glClear(GL_COLOR_BUFFER_BIT);
            EglContext::currentContext()->popFramebuffer();
        }
    }

    // Copy the background behind the glass. The texture keeps earlier frames,
    // so only the part repainted now needs refreshing.
    const Region dirtyRegion = viewport.mapFromDeviceCoordinatesContained(deviceRegion) & backgroundRect;
    for (const Rect &dirtyRect : dirtyRegion.rects()) {
        targets.framebuffers[0]->blitFromRenderTarget(renderTarget, viewport, dirtyRect, dirtyRect.translated(-backgroundRect.topLeft()));
    }

    // The first 6 vertices cover the offscreen texture; the rest cover the glass on screen.
    GLVertexBuffer *vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setAttribLayout(std::span(GLVertexBuffer::GLVertex2DLayout), sizeof(GLVertex2D));
    const int screenVertexCount = screenShape.size() * 6;
    auto result = vbo->map<GLVertex2D>(6 + screenVertexCount);
    if (!result) {
        qCWarning(GLASS) << "Failed to map vertex buffer";
        return;
    }
    auto map = *result;
    size_t index = 0;
    auto appendQuad = [&](const QRectF &rect, const QSizeF &textureSize) {
        const float x0 = rect.left();
        const float y0 = rect.top();
        const float x1 = rect.right();
        const float y1 = rect.bottom();
        const float u0 = x0 / textureSize.width();
        const float v0 = 1.0f - y0 / textureSize.height();
        const float u1 = x1 / textureSize.width();
        const float v1 = 1.0f - y1 / textureSize.height();
        const QVector4D corners[] = {{x0, y0, u0, v0}, {x1, y1, u1, v1}, {x0, y1, u0, v1},
                                     {x0, y0, u0, v0}, {x1, y0, u1, v0}, {x1, y1, u1, v1}};
        for (const QVector4D &corner : corners) {
            map[index++] = GLVertex2D{.position = corner.toVector2D(), .texcoord = QVector2D(corner.z(), corner.w())};
        }
    };
    appendQuad(QRectF(QPointF(0, 0), backgroundRect.size()), backgroundRect.size());
    for (const RectF &rect : screenShape) {
        appendQuad(rect, scaledBackgroundRect.size());
    }
    vbo->unmap();
    vbo->bindArrays();

    // Frost: draw the background at half size.
    QMatrix4x4 offscreenProjection;
    offscreenProjection.ortho(QRectF(0.0, 0.0, backgroundRect.width(), backgroundRect.height()));
    GLTexture *background = targets.textures[0].get();
    ShaderManager::instance()->pushShader(m_downsample.get());
    m_downsample->setUniform("modelViewProjectionMatrix", offscreenProjection);
    m_downsample->setUniform("offset", s_blurOffset);
    m_downsample->setUniform("halfpixel", QVector2D(0.5 / background->width(), 0.5 / background->height()));
    glActiveTexture(GL_TEXTURE0);
    background->bind();
    GLFramebuffer::pushFramebuffer(targets.framebuffers[1].get());
    vbo->draw(GL_TRIANGLES, 0, 6);
    GLFramebuffer::popFramebuffer();
    ShaderManager::instance()->popShader();

    // Refract: draw the frosted background through the glass onto the screen.
    const float radius = cornerRadius(w);
    const BorderRadius borderRadius(radius, radius, radius, radius);
    w->window()->setBorderRadius(borderRadius);
    GLTexture *frosted = targets.textures[1].get();
    QMatrix4x4 screenProjection = viewport.projectionMatrix();
    screenProjection.translate(scaledBackgroundRect.x(), scaledBackgroundRect.y());
    ShaderManager::instance()->pushShader(m_glass.get());
    m_glass->setUniform("modelViewProjectionMatrix", screenProjection);
    m_glass->setUniform("offset", s_blurOffset);
    m_glass->setUniform("halfpixel", QVector2D(0.5 / frosted->width(), 0.5 / frosted->height()));
    m_glass->setUniform("cornerRadius", borderRadius.scaled(viewport.scale()).rounded().toVector());
    m_glass->setUniform("blurSize", QVector2D(scaledBackgroundRect.width(), scaledBackgroundRect.height()));
    // The pane in device pixels, measured from the bottom left of the glass
    // like the texture coordinates.
    const QRectF devicePane(QPointF(pane.x() - backgroundRect.x(),
                                    (backgroundRect.y() + backgroundRect.height()) - (pane.y() + pane.height())) * viewport.scale(),
                            QSizeF(pane.width(), pane.height()) * viewport.scale());
    m_glass->setUniform("pane", QVector4D(devicePane.center().x(), devicePane.center().y(),
                                          devicePane.width() * 0.5, devicePane.height() * 0.5));
    m_glass->setUniform("opacity", float(data.opacity() * data.opacity()));
    m_glass->setUniform("refraction", w->isTooltip() ? 0 : 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    frosted->bind();
    vbo->draw(GL_TRIANGLES, 6, screenVertexCount);
    glDisable(GL_BLEND);
    ShaderManager::instance()->popShader();
    vbo->unbindArrays();
}

KWIN_EFFECT_FACTORY_SUPPORTED(GlassEffect, "metadata.json", return GlassEffect::supported();)

} // namespace KWin

#include "glass.moc"
