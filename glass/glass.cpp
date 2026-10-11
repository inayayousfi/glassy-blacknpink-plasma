/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2011 Philipp Knechtges <philipp-dev@knechtges.com>
    SPDX-FileCopyrightText: 2018 Alex Nemeth <alex.nemeth329@gmail.com>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

// Glassy Black & Pink glass: KWin's blur, trimmed from 4v3ngR/kwin-effects-glass to
// the one look this desktop uses. Windows that request a blur behind them get
// a lightly frosted copy of the background, bent by the refraction shader in
// glass.frag, tooltips included.

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

#include <chrono>
#include <cmath>
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
// The stronger frosts: the half-size copy blurred once more down to a
// quarter, which turns small text into soft word shapes, and, for larger
// text and shapes, blurred down to an eighth and spread back up to a
// quarter. The shader picks how far to go by the size and strength of what
// lies behind.
constexpr int s_frostLevels = 4;
constexpr float s_frostOffset = 3.0f;
// The fine detail map is drawn at half size and smoothed down to a
// sixteenth, where it tells the shader how busy the background is around
// each point.
constexpr int s_detailLevels = 4;
constexpr float s_detailOffset = 4.0f;
// The frost and the busyness measure reach this far, so repaints must reach
// it too.
constexpr int s_frostReach = 64;
// When the background gets busier or calmer, the frost eases toward its new
// amount with this time constant, in seconds, instead of jumping to it.
constexpr float s_frostEase = 0.25f;
// The frost counts as settled once it is this close to its target.
constexpr float s_frostSettled = 0.004f;

static std::unique_ptr<GLShader> loadShader(const QString &name)
{
    QFile file(QStringLiteral(":/" GLASS_EFFECT_ID "/%1.frag").arg(name));
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
    void postPaintScreen() override;
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
        // [0] holds the background behind the window, [1] its half-size blur,
        // [2] the quarter-size frost, [3] the eighth-size step of the
        // heavier frost, which heavier holds at a quarter size.
        std::unique_ptr<GLTexture> textures[s_frostLevels];
        std::unique_ptr<GLFramebuffer> framebuffers[s_frostLevels];
        std::unique_ptr<GLTexture> heavier;
        std::unique_ptr<GLFramebuffer> heavierFramebuffer;
        // The fine detail map at a half, a quarter, an eighth and a sixteenth
        // of the size.
        std::unique_ptr<GLTexture> detail[s_detailLevels];
        std::unique_ptr<GLFramebuffer> detailFramebuffers[s_detailLevels];
        // The frost the background calls for, in one pixel, and two buffers
        // that read it back in turn: each frame reads the one written the
        // frame before, so the effect never waits for the GPU.
        std::unique_ptr<GLTexture> amount;
        std::unique_ptr<GLFramebuffer> amountFramebuffer;
        GLuint readback[2] = {0, 0};
        bool readbackFilled[2] = {false, false};
        int readbackIndex = 0;

        // The frost the glass shows, eased toward the measured one; below
        // zero until the first measure.
        float frost = -1;
        std::chrono::steady_clock::time_point lastFrame;
        // Whether the effect asked for the frame being drawn, to follow an
        // easing or to read the newest measure.
        bool followUp = false;

        RenderTargets() = default;
        RenderTargets(const RenderTargets &) = delete;
        RenderTargets &operator=(const RenderTargets &) = delete;
        ~RenderTargets()
        {
            release();
        }

        void release()
        {
            if (readback[0]) {
                glDeleteBuffers(2, readback);
                readback[0] = readback[1] = 0;
            }
            readbackFilled[0] = readbackFilled[1] = false;
            amountFramebuffer.reset();
            amount.reset();
            heavierFramebuffer.reset();
            heavier.reset();
            for (int i = 0; i < s_frostLevels; ++i) {
                framebuffers[i].reset();
                textures[i].reset();
            }
            for (int i = 0; i < s_detailLevels; ++i) {
                detailFramebuffers[i].reset();
                detail[i].reset();
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
    std::unique_ptr<GLShader> m_upsample;
    std::unique_ptr<GLShader> m_detail;
    std::unique_ptr<GLShader> m_busy;
    std::unique_ptr<GLShader> m_glass;
    bool m_valid = false;
    RenderView *m_currentView = nullptr;
    std::unordered_map<EffectWindow *, Glass> m_windows;
    std::unordered_map<EffectWindow *, QMetaObject::Connection> m_surfaceConnections;
    std::unordered_map<EffectWindow *, QMetaObject::Connection> m_decorationConnections;
    // Areas whose frost is still easing, repainted after this frame.
    Region m_easing;
};

GlassEffect::GlassEffect()
{
    initResources();
    m_downsample = loadShader(QStringLiteral("downsample"));
    m_upsample = loadShader(QStringLiteral("upsample"));
    m_detail = loadShader(QStringLiteral("detail"));
    m_busy = loadShader(QStringLiteral("busy"));
    m_glass = loadShader(QStringLiteral("glass"));
    if (!m_downsample || !m_upsample || !m_detail || !m_busy || !m_glass) {
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
    // A title can change the glass, as the island's does when it opens.
    connect(w->window(), &Window::captionChanged, this, [this, w]() {
        updateRegion(w);
        w->addRepaintFull();
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
    // One connection per window, replaced when the window gets a new
    // decoration; Qt cannot keep a connection to a lambda unique by itself.
    if (auto it = m_decorationConnections.find(w); it != m_decorationConnections.end()) {
        disconnect(it->second);
        m_decorationConnections.erase(it);
    }
    if (KDecoration3::Decoration *decoration = w->decoration()) {
        m_decorationConnections[w] = connect(decoration, &KDecoration3::Decoration::blurRegionChanged, this, [this, w]() {
            updateRegion(w);
        });
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
    if (auto it = m_decorationConnections.find(w); it != m_decorationConnections.end()) {
        disconnect(it->second);
        m_decorationConnections.erase(it);
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

// Whether a surface holds text to read, which makes the glass frost from a
// calmer background: application windows, dialogs, menus, popups,
// notifications and the open island do; the shell, with icons and a few
// words, does not: panels, the dock, on-screen displays and the closed island
// pill.
static bool holdsText(EffectWindow *w)
{
    // The island is one window whose title follows its state.
    static const QString island = QStringLiteral("Glassy Black & Pink Island");
    if (w->caption().startsWith(island)) {
        return w->caption() != island + QStringLiteral(" Compact");
    }
    return w->isNormalWindow() || w->isDialog() || w->isMenu() || w->isDropdownMenu() || w->isPopupMenu() || w->isComboBox()
        || w->isPopupWindow() || w->isNotification() || w->isCriticalNotification();
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
    }
    glass.item->setPixelsToExpandRepaintsBelowOpaqueRegions(s_frostReach);
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

void GlassEffect::postPaintScreen()
{
    effects->postPaintScreen();
    if (!m_easing.isEmpty()) {
        effects->addRepaint(m_easing);
        m_easing = Region();
    }
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
        auto allocate = [&](std::unique_ptr<GLTexture> &texture, std::unique_ptr<GLFramebuffer> &framebuffer, int shrink) {
            texture = GLTexture::allocate(textureFormat, (backgroundRect.size() / (1 << shrink)).expandedTo(QSize(1, 1)));
            if (!texture) {
                qCWarning(GLASS) << "Failed to allocate an offscreen texture";
                return false;
            }
            texture->setFilter(GL_LINEAR);
            texture->setWrapMode(GL_CLAMP_TO_EDGE);
            framebuffer = std::make_unique<GLFramebuffer>(texture.get());
            if (!framebuffer->valid()) {
                qCWarning(GLASS) << "Failed to create an offscreen framebuffer";
                return false;
            }
            EglContext::currentContext()->pushFramebuffer(framebuffer.get());
            glClear(GL_COLOR_BUFFER_BIT);
            EglContext::currentContext()->popFramebuffer();
            return true;
        };
        for (int i = 0; i < s_frostLevels; ++i) {
            if (!allocate(targets.textures[i], targets.framebuffers[i], i)) {
                targets.release();
                return;
            }
        }
        if (!allocate(targets.heavier, targets.heavierFramebuffer, 2)) {
            targets.release();
            return;
        }
        for (int i = 0; i < s_detailLevels; ++i) {
            if (!allocate(targets.detail[i], targets.detailFramebuffers[i], i + 1)) {
                targets.release();
                return;
            }
        }
        targets.amount = GLTexture::allocate(GL_RGBA8, QSize(1, 1));
        if (!targets.amount) {
            qCWarning(GLASS) << "Failed to allocate the frost amount texture";
            targets.release();
            return;
        }
        targets.amountFramebuffer = std::make_unique<GLFramebuffer>(targets.amount.get());
        if (!targets.amountFramebuffer->valid()) {
            qCWarning(GLASS) << "Failed to create the frost amount framebuffer";
            targets.release();
            return;
        }
        glGenBuffers(2, targets.readback);
        for (const GLuint buffer : targets.readback) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
            glBufferData(GL_PIXEL_PACK_BUFFER, 4, nullptr, GL_STREAM_READ);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
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

    // Stronger frosts: halve the half-size copy down to a quarter and an
    // eighth, then spread the eighth back up to a quarter.
    m_downsample->setUniform("offset", s_frostOffset);
    for (int i = 2; i < s_frostLevels; ++i) {
        GLTexture *read = targets.textures[i - 1].get();
        m_downsample->setUniform("halfpixel", QVector2D(0.5 / read->width(), 0.5 / read->height()));
        read->bind();
        GLFramebuffer::pushFramebuffer(targets.framebuffers[i].get());
        vbo->draw(GL_TRIANGLES, 0, 6);
        GLFramebuffer::popFramebuffer();
    }
    ShaderManager::instance()->popShader();
    ShaderManager::instance()->pushShader(m_upsample.get());
    m_upsample->setUniform("modelViewProjectionMatrix", offscreenProjection);
    m_upsample->setUniform("offset", s_frostOffset);
    {
        GLTexture *read = targets.textures[3].get();
        m_upsample->setUniform("halfpixel", QVector2D(0.5 / read->width(), 0.5 / read->height()));
        read->bind();
        GLFramebuffer::pushFramebuffer(targets.heavierFramebuffer.get());
        vbo->draw(GL_TRIANGLES, 0, 6);
        GLFramebuffer::popFramebuffer();
    }
    ShaderManager::instance()->popShader();

    // Detail: map how much the background changes at three sizes at half
    // size, then smooth the map down to a sixteenth.
    ShaderManager::instance()->pushShader(m_detail.get());
    m_detail->setUniform("modelViewProjectionMatrix", offscreenProjection);
    m_detail->setUniform("halfpixel", QVector2D(0.5 / background->width(), 0.5 / background->height()));
    m_detail->setUniform("frostUnit", 1);
    m_detail->setUniform("quarterUnit", 2);
    m_detail->setUniform("heavierUnit", 3);
    glActiveTexture(GL_TEXTURE3);
    targets.heavier->bind();
    glActiveTexture(GL_TEXTURE2);
    targets.textures[2]->bind();
    glActiveTexture(GL_TEXTURE1);
    targets.textures[1]->bind();
    glActiveTexture(GL_TEXTURE0);
    background->bind();
    GLFramebuffer::pushFramebuffer(targets.detailFramebuffers[0].get());
    vbo->draw(GL_TRIANGLES, 0, 6);
    GLFramebuffer::popFramebuffer();
    glActiveTexture(GL_TEXTURE3);
    targets.heavier->unbind();
    glActiveTexture(GL_TEXTURE2);
    targets.textures[2]->unbind();
    glActiveTexture(GL_TEXTURE1);
    targets.textures[1]->unbind();
    glActiveTexture(GL_TEXTURE0);
    ShaderManager::instance()->popShader();
    ShaderManager::instance()->pushShader(m_downsample.get());
    m_downsample->setUniform("offset", s_detailOffset);
    for (int i = 1; i < s_detailLevels; ++i) {
        GLTexture *read = targets.detail[i - 1].get();
        m_downsample->setUniform("halfpixel", QVector2D(0.5 / read->width(), 0.5 / read->height()));
        read->bind();
        GLFramebuffer::pushFramebuffer(targets.detailFramebuffers[i].get());
        vbo->draw(GL_TRIANGLES, 0, 6);
        GLFramebuffer::popFramebuffer();
    }
    ShaderManager::instance()->popShader();

    // The pane in device pixels, measured from the bottom left of the glass
    // like the texture coordinates: its center and half size.
    const QRectF devicePaneRect(QPointF(pane.x() - backgroundRect.x(),
                                        (backgroundRect.y() + backgroundRect.height()) - (pane.y() + pane.height())) * viewport.scale(),
                                QSizeF(pane.width(), pane.height()) * viewport.scale());
    const QVector4D devicePane(devicePaneRect.center().x(), devicePaneRect.center().y(),
                               devicePaneRect.width() * 0.5, devicePaneRect.height() * 0.5);

    // Frost amount: measure how much the pane should frost into one pixel and
    // start reading it back. The frame reads the measure written the frame
    // before, except the first, which waits for its own.
    ShaderManager::instance()->pushShader(m_busy.get());
    m_busy->setUniform("modelViewProjectionMatrix", offscreenProjection);
    m_busy->setUniform("pane", devicePane);
    m_busy->setUniform("blurSize", QVector2D(scaledBackgroundRect.width(), scaledBackgroundRect.height()));
    m_busy->setUniform("content", holdsText(w) ? 1.0f : 0.0f);
    targets.detail[s_detailLevels - 1]->bind();
    GLFramebuffer::pushFramebuffer(targets.amountFramebuffer.get());
    vbo->draw(GL_TRIANGLES, 0, 6);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, targets.readback[targets.readbackIndex]);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    GLFramebuffer::popFramebuffer();
    targets.readbackFilled[targets.readbackIndex] = true;
    ShaderManager::instance()->popShader();
    const int previous = 1 - targets.readbackIndex;
    const int ready = targets.frost < 0 ? targets.readbackIndex : previous;
    float target = -1;
    if (targets.readbackFilled[ready]) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, targets.readback[ready]);
        if (const void *mapped = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, 4, GL_MAP_READ_BIT)) {
            target = static_cast<const unsigned char *>(mapped)[0] / 255.0f;
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    targets.readbackIndex = previous;

    // Ease the frost toward the measure, by the time since the last frame.
    const auto now = std::chrono::steady_clock::now();
    if (targets.frost < 0) {
        targets.frost = std::max(target, 0.0f);
    } else if (target >= 0) {
        const float elapsed = std::clamp(std::chrono::duration<float>(now - targets.lastFrame).count(), 0.0f, 0.1f);
        targets.frost += (target - targets.frost) * (1.0f - std::exp(-elapsed / s_frostEase));
        if (std::abs(target - targets.frost) <= s_frostSettled) {
            targets.frost = target;
        }
    }
    targets.lastFrame = now;
    // Keep repainting while the frost eases, and once after any other change
    // to read the measure it caused.
    const bool followUp = targets.followUp;
    targets.followUp = !followUp || target < 0 || target != targets.frost;
    if (targets.followUp) {
        m_easing += Rect(backgroundRect);
    }

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
    m_glass->setUniform("pane", devicePane);
    m_glass->setUniform("opacity", float(data.opacity() * data.opacity()));
    m_glass->setUniform("refraction", 1);
    m_glass->setUniform("frost", targets.frost);
    m_glass->setUniform("sharpUnit", 1);
    m_glass->setUniform("frostUnit", 2);
    m_glass->setUniform("heavierUnit", 3);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    // The rim refracts the unblurred background; the face mixes in the frosting.
    glActiveTexture(GL_TEXTURE3);
    targets.heavier->bind();
    glActiveTexture(GL_TEXTURE2);
    targets.textures[2]->bind();
    glActiveTexture(GL_TEXTURE1);
    targets.textures[0]->bind();
    glActiveTexture(GL_TEXTURE0);
    frosted->bind();
    vbo->draw(GL_TRIANGLES, 6, screenVertexCount);
    glActiveTexture(GL_TEXTURE3);
    targets.heavier->unbind();
    glActiveTexture(GL_TEXTURE2);
    targets.textures[2]->unbind();
    glActiveTexture(GL_TEXTURE1);
    targets.textures[0]->unbind();
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_BLEND);
    ShaderManager::instance()->popShader();
    vbo->unbindArrays();
}

KWIN_EFFECT_FACTORY_SUPPORTED(GlassEffect, "metadata.json", return GlassEffect::supported();)

} // namespace KWin

#include "glass.moc"
