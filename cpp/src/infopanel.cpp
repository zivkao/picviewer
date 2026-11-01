#include "infopanel.h"

#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>

namespace {

QString fileSize(qint64 bytes)
{
    // Base 1024 with KB/MB, the way Explorer shows it, so the two agree.
    return QLocale::c().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

QString when(const QDateTime &time)
{
    return time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

} // namespace

InfoPanel::InfoPanel(QWidget *parent)
    : QWidget(parent)
    , m_label(new QLabel(this))
{
    m_label->setTextFormat(Qt::RichText);
    m_label->setWordWrap(true);
    m_label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    // Selectable, so a lens name or a date can be copied out.
    m_label->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->addWidget(m_label);
    layout->addStretch(1);

    setMinimumWidth(220);
    clear();
}

void InfoPanel::showImage(const QString &path, const decoders::DecodedImage &image)
{
    const QFileInfo file(path);
    const QSize size = image.fullSize.isValid() ? image.fullSize : image.image.size();

    Rows fileRows = {{QStringLiteral("Name"), file.fileName()}};
    fileRows << qMakePair(QStringLiteral("Dimensions"),
                          QStringLiteral("%1 x %2 (%3 MP)")
                              .arg(size.width())
                              .arg(size.height())
                              .arg(QString::number(qint64(size.width()) * size.height() / 1e6,
                                                   'f', 1)));
    fileRows << qMakePair(QStringLiteral("File size"), fileSize(file.size()));
    if (const QString format = image.metadata.value(QStringLiteral("format")); !format.isEmpty())
        fileRows << qMakePair(QStringLiteral("Format"), format);
    if (const QString decoder = image.metadata.value(QStringLiteral("decoder")); !decoder.isEmpty())
        fileRows << qMakePair(QStringLiteral("Decoder"), decoder);
    if (image.metadata.contains(QStringLiteral("icc")))
        fileRows << qMakePair(QStringLiteral("Colour"), QStringLiteral("embedded profile, shown as sRGB"));
    fileRows << qMakePair(QStringLiteral("Modified"), when(file.lastModified()));

    const Rows camera = photoinfo::rows(image.info);
    present(camera, camera.isEmpty() ? QStringLiteral("No camera information in this file")
                                     : QString(),
            fileRows);
}

void InfoPanel::showFailure(const QString &path)
{
    const QFileInfo file(path);
    Rows fileRows = {{QStringLiteral("Name"), file.fileName()}};
    if (file.exists()) {
        fileRows << qMakePair(QStringLiteral("File size"), fileSize(file.size()));
        fileRows << qMakePair(QStringLiteral("Modified"), when(file.lastModified()));
    }
    present({}, QStringLiteral("Could not be decoded"), fileRows);
}

void InfoPanel::clear()
{
    m_plain = QStringLiteral("No image");
    m_label->setText(QStringLiteral("<p style='color:gray'>No image</p>"));
}

void InfoPanel::present(const Rows &camera, const QString &cameraNote, const Rows &file)
{
    QString html;
    QStringList plain;

    const auto section = [&](const QString &title, const Rows &rows, const QString &note) {
        html += QStringLiteral("<p style='margin-top:6px'><b>%1</b></p>").arg(title);
        plain << title;
        if (!note.isEmpty()) {
            html += QStringLiteral("<p style='color:gray'>%1</p>").arg(note.toHtmlEscaped());
            plain << QStringLiteral("  ") + note;
        }
        if (rows.isEmpty())
            return;
        html += QStringLiteral("<table cellspacing='0' cellpadding='2'>");
        for (const auto &[label, value] : rows) {
            html += QStringLiteral("<tr><td style='color:gray; padding-right:10px'>%1</td>"
                                   "<td>%2</td></tr>")
                        .arg(label.toHtmlEscaped(), value.toHtmlEscaped());
            plain << QStringLiteral("  %1: %2").arg(label, value);
        }
        html += QStringLiteral("</table>");
    };

    section(QStringLiteral("Camera"), camera, cameraNote);
    section(QStringLiteral("File"), file, {});

    m_label->setText(html);
    m_plain = plain.join(QLatin1Char('\n'));
}
