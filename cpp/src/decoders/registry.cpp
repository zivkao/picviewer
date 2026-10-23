#include "registry.h"

#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

#include <algorithm>

#include "heif_decoder.h"
#include "qt_decoder.h"
#include "raw_decoder.h"

Q_LOGGING_CATEGORY(logRegistry, "picviewer.registry")

namespace decoders {

Registry::Registry()
{
    m_decoders.push_back(std::make_unique<QtDecoder>());
    m_decoders.push_back(std::make_unique<HeifDecoder>());
    m_decoders.push_back(std::make_unique<RawDecoder>());
}

Registry &Registry::instance()
{
    static Registry registry;
    return registry;
}

std::vector<const Decoder *> Registry::candidates(
    const QString &path, std::optional<sniff::Container> container) const
{
    const sniff::Container resolved = container.value_or(sniff::of(path));

    std::vector<std::pair<int, const Decoder *>> ranked;
    ranked.reserve(m_decoders.size());
    for (const auto &decoder : m_decoders) {
        const int score = decoder->score(path, resolved);
        if (score > 0)
            ranked.emplace_back(score, decoder.get());
    }

    // Descending by score. stable_sort keeps registration order for genuine
    // ties, but the scoring is designed not to produce them for the
    // ambiguous cases -- a tie there was how a TIFF-based RAW ended up at the
    // Qt decoder, which "succeeded" by returning the embedded preview.
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto &a, const auto &b) { return a.first > b.first; });

    std::vector<const Decoder *> result;
    result.reserve(ranked.size());
    for (const auto &[score, decoder] : ranked)
        result.push_back(decoder);
    return result;
}

std::optional<DecodedImage> Registry::loadPreview(const QString &path, QSize maxSize) const
{
    const sniff::Container container = sniff::of(path);
    for (const Decoder *decoder : candidates(path, container)) {
        if (auto preview = decoder->loadPreview(path, container, maxSize))
            return preview;
    }
    return std::nullopt;
}

std::optional<DecodedImage> Registry::loadFull(const QString &path,
                                               QSize maxSize,
                                               DecodeError *error) const
{
    if (auto unreadable = checkReadable(path)) {
        if (error)
            *error = *unreadable;
        return std::nullopt;
    }

    const sniff::Container container = sniff::of(path);
    const std::vector<const Decoder *> ranked = candidates(path, container);

    // Everything needed to reason about a failure before it happens: what the
    // file looked like on disk, what it was taken for, and who was going to
    // be asked. Logged at debug so a normal session stays readable, but -v
    // turns it on and that is what a bug report should carry.
    if (logRegistry().isDebugEnabled()) {
        QStringList names;
        for (const Decoder *decoder : ranked)
            names << QStringLiteral("%1(%2)").arg(decoder->name()).arg(
                decoder->score(path, container));
        qCDebug(logRegistry).noquote().nospace()
            << "dispatch " << QFileInfo(path).fileName()
            << ": " << QFileInfo(path).size() << " bytes, sniffed "
            << sniff::name(container) << ", candidates " << names.join(QLatin1Char(' '));
    }

    if (ranked.empty()) {
        if (error)
            *error = {DecodeError::Kind::Unsupported,
                      QStringLiteral("No decoder claims %1 (%2)")
                          .arg(QFileInfo(path).fileName(), sniff::name(container))};
        return std::nullopt;
    }

    QStringList failures;
    for (const Decoder *decoder : ranked) {
        QString why;
        if (auto result = decoder->loadFull(path, container, maxSize, &why)) {
            if (decoder != ranked.front()) {
                // The preferred decoder lost. Worth a line of its own: it
                // means the ranking disagreed with reality for this file.
                qCInfo(logRegistry).noquote()
                    << "fell through to" << decoder->name() << "for"
                    << QFileInfo(path).fileName()
                    << "after" << ranked.front()->name() << "declined";
            }
            // Which decoder won matters when diagnosing a wrong-looking image:
            // a TIFF-based RAW handled by "qt" means the small embedded
            // preview got through instead of the photograph.
            result->metadata.insert(QStringLiteral("decoder"), decoder->name());
            return result;
        }
        qCWarning(logRegistry).noquote() << decoder->name() << "failed on"
                               << QFileInfo(path).fileName() << ":" << why;
        failures << QStringLiteral("%1: %2").arg(decoder->name(), why);
    }

    if (error)
        *error = {DecodeError::Kind::Unsupported,
                  QStringLiteral("Could not decode %1 (%2)\n%3")
                      .arg(QFileInfo(path).fileName(), sniff::name(container),
                           failures.join(QLatin1Char('\n')))};
    return std::nullopt;
}

QStringList Registry::decoderNames() const
{
    QStringList names;
    for (const auto &decoder : m_decoders)
        names << decoder->name();
    return names;
}

QSet<QString> Registry::supportedExtensions() const
{
    QSet<QString> extensions = QtDecoder::extensions();
    extensions.unite(HeifDecoder::extensions());
    extensions.unite(RawDecoder::extensions());
    return extensions;
}

std::optional<DecodeError> checkReadable(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return DecodeError{DecodeError::Kind::Unreadable,
                           QStringLiteral("The file no longer exists.")};

    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        // Opening can succeed on a cloud placeholder and fail on the first
        // read, which is where the real refusal surfaces.
        if (file.read(1).isEmpty() && file.error() != QFileDevice::NoError) {
            return DecodeError{
                DecodeError::Kind::Unreadable,
                QStringLiteral("Windows refused to read this file.\n\n%1")
                    .arg(file.errorString())};
        }
        return std::nullopt;
    }

    return DecodeError{
        DecodeError::Kind::Unreadable,
        QStringLiteral(
            "Windows refused access to this file.\n\n"
            "It is usually one of:\n"
            "  - a OneDrive or other cloud placeholder the sync engine "
            "cannot fetch (try 'Always keep on this device')\n"
            "  - the file is open exclusively in another program\n"
            "  - the file permissions do not allow reading")};
}

} // namespace decoders
