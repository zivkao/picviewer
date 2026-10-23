// The image canvas: zoom, pan, fit.

#pragma once

#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>

class QMimeData;

class ImageView : public QGraphicsView
{
    Q_OBJECT

public:
    static constexpr qreal MinScale = 0.02;
    static constexpr qreal MaxScale = 40.0;
    static constexpr qreal ZoomStep = 1.25;
    // Past this magnification, smoothing just blurs the pixels the user
    // zoomed in to inspect, so switch to nearest-neighbour.
    static constexpr qreal CrispThreshold = 4.0;

    explicit ImageView(QWidget *parent = nullptr);

    /// Show *image*. With keepView, preserve the current zoom and pan, which
    /// matters when a full-quality decode replaces the preview already on
    /// screen: the picture should sharpen in place, not jump back to fit.
    void setImage(const QImage &image, bool keepView = false);
    void clearImage();
    bool hasImage() const;

    /// Text for the empty canvas. Failures are shown here rather than in a
    /// dialog: stepping through a folder of unreadable files would otherwise
    /// mean dismissing one modal per image.
    void setMessage(const QString &text);

    qreal scaleFactor() const { return transform().m11(); }
    const QPixmap pixmap() const { return m_item->pixmap(); }

public slots:
    void fit();
    void zoomToActual();
    void toggleFit();
    void zoomBy(qreal factor);

signals:
    void scaleChanged(qreal scale);
    void filesDropped(const QStringList &paths);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;

    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void afterZoom();
    void applyTransformationMode();
    void setHover(bool hovering);

    QGraphicsScene *m_scene;
    QGraphicsPixmapItem *m_item;
    QPixmap m_checker;
    QString m_message;
    bool m_fitMode = true;
    bool m_hasAlpha = false;
    bool m_dragHover = false;
};

/// Local filesystem paths carried by a drag, ignoring remote URLs.
QStringList droppedPaths(const QMimeData *mime);
