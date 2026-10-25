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

    /// Zoom as the user means it: screen pixels per image pixel, so 1.0 is a
    /// true 1:1 on any display.
    ///
    /// Not transform().m11() alone. The view's transform maps the scene to
    /// *logical* pixels, and on a display scaled to 150% each logical pixel is
    /// 1.5 physical ones -- so an untouched transform shows the image at 150%
    /// while calling it 100%, and upsamples it on the way.
    qreal scaleFactor() const { return transform().m11() * pixelRatio(); }
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
    bool event(QEvent *event) override;
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
    /// Physical pixels per logical pixel on the screen the canvas is on.
    qreal pixelRatio() const { return viewport()->devicePixelRatio(); }

    /// Set the transform for a zoom in screen pixels per image pixel,
    /// keeping whatever scene point was at the centre of the view there.
    void applyZoom(qreal zoom);

    /// The transform that shows one image pixel per screen pixel.
    QTransform oneToOne() const;

    bool m_fitMode = true;
    /// The ratio the current transform was computed for. Kept so a move to a
    /// screen with different scaling can preserve the zoom the user chose.
    qreal m_ratioAtZoom = 1.0;
    bool m_hasAlpha = false;
    bool m_dragHover = false;
};

/// Local filesystem paths carried by a drag, ignoring remote URLs.
QStringList droppedPaths(const QMimeData *mime);
