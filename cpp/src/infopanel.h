// The side panel: what the camera recorded, and what the file is.

#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QWidget>

#include "decoders/decoder.h"

class QLabel;

class InfoPanel : public QWidget
{
    Q_OBJECT

public:
    explicit InfoPanel(QWidget *parent = nullptr);

    /// Show the camera's record and the file's details for *image*, decoded
    /// from *path*. Called again when a RAW's full decode replaces its
    /// preview, which carries the same record.
    void showImage(const QString &path, const decoders::DecodedImage &image);

    /// A file that would not decode: its name, size and date, nothing more.
    void showFailure(const QString &path);

    /// Nothing open.
    void clear();

    /// Everything shown, as "label: value" lines under section headings --
    /// what the tests read, rather than the rich text drawn on screen.
    QString text() const { return m_plain; }

private:
    using Rows = QList<QPair<QString, QString>>;

    void present(const Rows &camera, const QString &cameraNote, const Rows &file);

    QLabel *m_label;
    QString m_plain;
};
