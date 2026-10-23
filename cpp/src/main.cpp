// Entry point.

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QLoggingCategory>

#include "applog.h"
#include "decoders/decoder.h"
#include "window.h"

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <shobjidl.h>
#endif

Q_LOGGING_CATEGORY(logApp, "picviewer")

namespace {

constexpr auto Version = "0.1.0";

/// Windows groups taskbar buttons by this id and takes the icon from whatever
/// owns it. Left unset, the app inherits a generic identity and the taskbar
/// shows the wrong icon however the window is decorated.
void claimTaskbarIdentity()
{
#ifdef Q_OS_WIN
    SetCurrentProcessExplicitAppUserModelID(L"zivkao.picviewer.cpp");
#endif
}

} // namespace

int main(int argc, char **argv)
{
    claimTaskbarIdentity();

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Pic Viewer"));
    app.setApplicationVersion(QString::fromLatin1(Version));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Image viewer"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption verbose({QStringLiteral("v"), QStringLiteral("verbose")},
                                     QStringLiteral("debug logging"));
    parser.addOption(verbose);
    parser.addPositionalArgument(QStringLiteral("path"),
                                 QStringLiteral("image file or folder to open"));
    parser.process(app);

    applog::install(parser.isSet(verbose));
    qCInfo(logApp) << "--- Pic Viewer" << Version << "(C++) starting, logging to"
                   << applog::filePath() << "---";

    // Queued signals carry decoded images from the worker pool to the GUI
    // thread, and an unregistered type makes that connection fail silently.
    qRegisterMetaType<decoders::DecodedImage>("decoders::DecodedImage");

    app.setWindowIcon(QIcon(QStringLiteral(":/icon.ico")));

    MainWindow window;
    window.show();

    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty())
        window.openPath(positional.first());

    const int code = app.exec();
    qCInfo(logApp) << "--- exiting with code" << code << "---";
    applog::shutdown();
    return code;
}
