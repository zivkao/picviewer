// Entry point.

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QLoggingCategory>

#include <cstdio>

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

    // Not addVersionOption(): it claims -v, which collides with -v for verbose
    // and made `picviewer -v file.jpg` print the version and exit -- producing
    // no log at all, in exactly the situation someone asks for one. The Python
    // build uses -v for verbose, so this one does too.
    const QCommandLineOption versionOption(QStringLiteral("version"),
                                           QStringLiteral("show the version and exit"));
    parser.addOption(versionOption);
    const QCommandLineOption verbose({QStringLiteral("v"), QStringLiteral("verbose")},
                                     QStringLiteral("debug logging"));
    parser.addOption(verbose);
    parser.addPositionalArgument(QStringLiteral("path"),
                                 QStringLiteral("image file or folder to open"));
    parser.process(app);

    if (parser.isSet(versionOption)) {
        std::printf("Pic Viewer %s (C++, Qt %s)\n", Version, qVersion());
        return 0;
    }

    applog::install(parser.isSet(verbose));
    applog::installCrashHandler();
    // The pid separates runs: the log is appended to, and two copies open at
    // once interleave their lines indistinguishably without it.
    qCInfo(logApp).noquote() << "--- Pic Viewer" << Version << "(C++) starting, pid"
                             << QCoreApplication::applicationPid() << "---";
    applog::logEnvironment();

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
