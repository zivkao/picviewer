#include "viewer.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPainter>
#include <QUrl>
#include <QWheelEvent>

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
    m_scene->setSceneRect(QRectF(image.rect()));

    if (keepView && !m_fitMode)
        applyTransformationMode();
    else
        fit();
}

void ImageView::clearImage()
{
    m_item->setPixmap(QPixmap());
    m_scene->setSceneRect(QRectF());
    viewport()->update();
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

    const QRectF rect = m_scene->sceneRect();
    const QRect view = viewport()->rect();
    if (rect.width() > view.width() || rect.height() > view.height())
        fitInView(rect, Qt::KeepAspectRatio);

    centerOn(rect.center());
    afterZoom();
}

void ImageView::zoomToActual()
{
    if (!hasImage())
        return;
    m_fitMode = false;
    resetTransform();
    afterZoom();
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
    applyTransformationMode();
    emit scaleChanged(scaleFactor());
}

void ImageView::applyTransformationMode()
{
    m_item->setTransformationMode(scaleFactor() >= CrispThreshold
                                      ? Qt::FastTransformation
                                      : Qt::SmoothTransformation);
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
