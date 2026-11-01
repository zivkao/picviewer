#include "viewer.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPainter>
#include <QUrl>
#include <QWheelEvent>
#include <QtMath>

#include <cmath>

namespace {

const QColor Background(32, 33, 36);
const QColor CheckerLight(74, 76, 80);
const QColor CheckerDark(58, 60, 64);
constexpr int CheckerSize = 12;

const QColor MessageText(168, 172, 180);
const QColor DropAccent(120, 175, 255);
const QColor DropVeil(120, 175, 255, 28);

QPixmap makeChecker()
{
    QPixmap pixmap(CheckerSize * 2, CheckerSize * 2);
    pixmap.fill(CheckerDark);
    QPainter painter(&pixmap);
    painter.fillRect(0, 0, CheckerSize, CheckerSize, CheckerLight);
    painter.fillRect(CheckerSize, CheckerSize, CheckerSize, CheckerSize, CheckerLight);
    return pixmap;
}

} // namespace

QString describeOrientation(const QTransform &orientation)
{
    if (orientation.isIdentity())
        return {};

    // Every combination of quarter turns and mirrors is one mirror (or none)
    // followed by one rotation, so take the mirror out and read the angle
    // that remains. QTransform::rotate is exact at multiples of 90 degrees,
    // so the entries are 0 and +-1 and the angle comes out whole.
    const bool mirrored = orientation.determinant() < 0;
    const QTransform rotation = mirrored ? QTransform::fromScale(-1, 1) * orientation
                                         : orientation;
    int degrees = qRound(qRadiansToDegrees(std::atan2(rotation.m12(), rotation.m11())));
    degrees = (degrees % 360 + 360) % 360;

    if (!mirrored)
        return QStringLiteral("rotated %1°").arg(degrees);
    if (degrees == 0)
        return QStringLiteral("flipped horizontally");
    if (degrees == 180)
        return QStringLiteral("flipped vertically");
    return QStringLiteral("rotated %1°, flipped").arg(degrees);
}

QStringList droppedPaths(const QMimeData *mime)
{
    if (!mime || !mime->hasUrls())
        return {};

    QStringList paths;
    for (const QUrl &url : mime->urls()) {
        const QString local = url.toLocalFile();
        if (!local.isEmpty())
            paths << local;
    }
    return paths;
}

ImageView::ImageView(QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
    , m_item(new QGraphicsPixmapItem)
    , m_checker(makeChecker())
{
    m_item->setTransformationMode(Qt::SmoothTransformation);
    m_scene->addItem(m_item);
    setScene(m_scene);

    setRenderHints(QPainter::SmoothPixmapTransform | QPainter::Antialiasing);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setBackgroundBrush(Background);

    // Drops must be handled here rather than on the main window. A
    // QGraphicsView turns on acceptDrops for its viewport so it can offer
    // drags to graphics items, and the viewport marks the event handled
    // either way -- so a drop on the canvas never reaches the window.
    setAcceptDrops(true);
}

// -- content ---------------------------------------------------------------

void ImageView::setImage(const QImage &image, bool keepView)
{
    m_hasAlpha = image.hasAlphaChannel();
    m_message.clear();
    m_item->setPixmap(QPixmap::fromImage(image));

    // keepView means the same picture at better quality -- the full decode
    // replacing a RAW's embedded preview -- so a turn made while the preview
    // was up should survive. Anything else is a different picture.
    if (!keepView)
        m_orientation = QTransform();   // a new image starts upright
    updateItemTransform();

    if (keepView && !m_fitMode)
        applyTransformationMode();
    else
        fit();
}

void ImageView::clearImage()
{
    m_item->setPixmap(QPixmap());
    m_orientation = QTransform();
    m_item->setTransform(QTransform());
    m_scene->setSceneRect(QRectF());
    viewport()->update();
}

// -- orientation -------------------------------------------------------------
//
// Applied to the item, never to the view. The view's transform *is* the zoom:
// scaleFactor() reads its m11, and fit, 1:1 and every display-scaling sum rest
// on that. A quarter turn makes m11 zero, so rotating the view would read as a
// zoom of nothing.

void ImageView::rotateClockwise() { reorient(QTransform().rotate(90)); }
void ImageView::rotateCounterClockwise() { reorient(QTransform().rotate(-90)); }
void ImageView::flipHorizontally() { reorient(QTransform::fromScale(-1, 1)); }
void ImageView::flipVertically() { reorient(QTransform::fromScale(1, -1)); }

void ImageView::reorient(const QTransform &step)
{
    if (!hasImage())
        return;
    // Zoomed in on a detail, the detail should still be under the centre
    // after the turn, not wherever the scroll position now happens to land.
    const QPointF focus = m_item->mapFromScene(mapToScene(viewport()->rect().center()));

    // Right-multiplied: Qt maps points as p * M, so this applies the step
    // after what is already there -- in screen terms, to the image as it is
    // currently shown. Flip-then-rotate and rotate-then-flip differ, and the
    // user means the order they pressed the keys in.
    m_orientation = m_orientation * step;
    updateItemTransform();
    if (m_fitMode) {
        fit();
    } else {
        centerOn(m_item->mapToScene(focus));
        applyTransformationMode();
    }
    viewport()->update();
}

void ImageView::updateItemTransform()
{
    // About the image's own centre, so a turn leaves it where it was.
    const QPointF centre = m_item->boundingRect().center();
    m_item->setTransform(QTransform::fromTranslate(-centre.x(), -centre.y())
                         * m_orientation
                         * QTransform::fromTranslate(centre.x(), centre.y()));
    // The scene is the image as shown, so a quarter turn swaps its sides and
    // fit measures the right way round.
    m_scene->setSceneRect(m_item->sceneBoundingRect());
}

bool ImageView::hasImage() const
{
    return !m_item->pixmap().isNull();
}

void ImageView::setMessage(const QString &text)
{
    m_message = text;
    viewport()->update();
}

// -- zoom ------------------------------------------------------------------

void ImageView::fit()
{
    if (!hasImage())
        return;

    // Shrink oversized images to the viewport; leave small ones at 1:1.
    m_fitMode = true;
    resetTransform();

    // Both sides in physical pixels. "Fits" means the image's pixels fit the
    // screen's pixels: on a display scaled to 200%, a 1000-pixel image fits a
    // viewport 600 logical pixels wide, because that viewport is 1200 real
    // pixels across. Comparing against the logical size would shrink it.
    const QRectF rect = m_scene->sceneRect();
    const qreal ratio = pixelRatio();
    const QSizeF screen = QSizeF(viewport()->size()) * ratio;
    if (rect.width() > screen.width() || rect.height() > screen.height())
        fitInView(rect, Qt::KeepAspectRatio);
    else
        setTransform(oneToOne());

    centerOn(rect.center());
    afterZoom();
}

void ImageView::zoomToActual()
{
    if (!hasImage())
        return;
    m_fitMode = false;
    setTransform(oneToOne());
    afterZoom();
}

QTransform ImageView::oneToOne() const
{
    // One image pixel per screen pixel. The transform is in logical pixels, so
    // on a scaled display that is a scale of 1/ratio, not the identity.
    const qreal ratio = pixelRatio();
    return QTransform::fromScale(1.0 / ratio, 1.0 / ratio);
}

void ImageView::applyZoom(qreal zoom)
{
    const QPointF centre = mapToScene(viewport()->rect().center());
    const qreal ratio = pixelRatio();
    setTransform(QTransform::fromScale(zoom / ratio, zoom / ratio));
    centerOn(centre);
}

void ImageView::toggleFit()
{
    if (m_fitMode && qAbs(scaleFactor() - 1.0) > 1e-3)
        zoomToActual();
    else
        fit();
}

void ImageView::zoomBy(qreal factor)
{
    if (!hasImage())
        return;

    const qreal current = scaleFactor();
    const qreal clamped = qBound(MinScale, current * factor, MaxScale);
    if (qAbs(clamped - current) < 1e-6)
        return;

    m_fitMode = false;
    const qreal step = clamped / current;
    scale(step, step);
    afterZoom();
}

void ImageView::afterZoom()
{
    m_ratioAtZoom = pixelRatio();
    applyTransformationMode();
    emit scaleChanged(scaleFactor());
}

void ImageView::applyTransformationMode()
{
    // Nearest-neighbour when zoomed far in, so pixels being inspected stay
    // pixels -- and at exactly 1:1, where there is nothing to resample and
    // smoothing could only blur, if the image lands half a pixel off the
    // device grid.
    const qreal zoom = scaleFactor();
    const bool exact = qAbs(zoom - 1.0) < 1e-6;
    m_item->setTransformationMode(zoom >= CrispThreshold || exact
                                      ? Qt::FastTransformation
                                      : Qt::SmoothTransformation);
}

bool ImageView::event(QEvent *event)
{
    // Dragged to a screen with different scaling. The transform was computed
    // for the old ratio, so the same transform now means a different zoom;
    // recompute it so the user's zoom survives the move.
    if (event->type() == QEvent::DevicePixelRatioChange && hasImage()) {
        const qreal zoom = transform().m11() * m_ratioAtZoom;
        if (m_fitMode)
            fit();
        else {
            applyZoom(zoom);
            afterZoom();
        }
    }
    return QGraphicsView::event(event);
}

// -- events ----------------------------------------------------------------

void ImageView::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (delta != 0) {
        zoomBy(delta > 0 ? ZoomStep : 1.0 / ZoomStep);
        event->accept();
    } else {
        QGraphicsView::wheelEvent(event);
    }
}

void ImageView::mouseDoubleClickEvent(QMouseEvent *event)
{
    toggleFit();
    event->accept();
}

void ImageView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_fitMode)
        fit();
}

// -- drag and drop ---------------------------------------------------------

void ImageView::dragEnterEvent(QDragEnterEvent *event)
{
    if (!droppedPaths(event->mimeData()).isEmpty()) {
        setHover(true);
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void ImageView::dragMoveEvent(QDragMoveEvent *event)
{
    // Without this the drag is refused mid-flight and the cursor shows the
    // "no entry" sign even though dragEnterEvent accepted.
    if (m_dragHover) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void ImageView::dragLeaveEvent(QDragLeaveEvent *event)
{
    setHover(false);
    event->accept();
}

void ImageView::dropEvent(QDropEvent *event)
{
    setHover(false);
    const QStringList paths = droppedPaths(event->mimeData());
    if (paths.isEmpty()) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
    emit filesDropped(paths);
}

void ImageView::setHover(bool hovering)
{
    if (m_dragHover != hovering) {
        m_dragHover = hovering;
        viewport()->update();
    }
}

// -- painting --------------------------------------------------------------

void ImageView::paintEvent(QPaintEvent *event)
{
    QGraphicsView::paintEvent(event);

    if (!m_message.isEmpty() && !hasImage()) {
        QPainter painter(viewport());
        painter.setPen(MessageText);
        QFont font = painter.font();
        font.setPointSize(qMax(10, font.pointSize() + 1));
        painter.setFont(font);
        painter.drawText(viewport()->rect().adjusted(40, 40, -40, -40),
                         Qt::AlignCenter | Qt::TextWordWrap, m_message);
    }

    if (!m_dragHover)
        return;

    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), DropVeil);
    painter.setPen(QPen(DropAccent, 2, Qt::DashLine));
    const QRect border = viewport()->rect().adjusted(8, 8, -9, -9);
    painter.drawRoundedRect(border, 10, 10);

    QFont font = painter.font();
    font.setPointSize(qMax(11, font.pointSize() + 3));
    painter.setFont(font);
    painter.drawText(border, Qt::AlignCenter, QStringLiteral("Drop to open"));
}

void ImageView::drawBackground(QPainter *painter, const QRectF &rect)
{
    painter->fillRect(rect, Background);
    if (m_hasAlpha && hasImage()) {
        // Checkerboard only under the image, so transparent regions read as
        // transparent rather than as dark grey.
        painter->save();
        painter->setBrushOrigin(QPointF(0, 0));
        painter->fillRect(rect.intersected(m_scene->sceneRect()), QBrush(m_checker));
        painter->restore();
    }
}
