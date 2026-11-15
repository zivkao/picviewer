// Entry point.

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>
#include <QLoggingCategory>

#include <cstdio>
#include <optional>

#include "applog.h"
#include "decoders/decoder.h"
#include "instance.h"
#include "window.h"

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <shellapi.h>
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

/// The paths this launch was given, made absolute -- or nothing if it was
/// given any option. --version, or -v for a verbose log, makes a launch of
/// its own rather than one to hand over to the window already open.
///
/// Read straight from the command line: this runs before there is a
/// QApplication, so that a launch handing over its files costs no more than
/// starting a process.
std::optional<QStringList> plainLaunchPaths()
{
#ifdef Q_OS_WIN
    int count = 0;
    wchar_t **args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args)
        return std::nullopt;
    QStringList paths;
    bool plain = true;
    for (int i = 1; i < count && plain; ++i) {
        const QString arg = QString::fromWCharArray(args[i]);
        if (arg.startsWith(QLatin1Char('-')))
            plain = false;
        else
            paths << QFileInfo(arg).absoluteFilePath();
    }
    LocalFree(args);
    if (!plain)
        return std::nullopt;
    return paths;
#else
    return std::nullopt;
#endif
}

} // namespace

int main(int argc, char **argv)
{
    claimTaskbarIdentity();

    // One window per session: a later launch hands its files to the first
    // and leaves. If the first does not answer, this one opens a window of
    // its own rather than lose the file it was asked to show.
    SingleInstance instance(SingleInstance::defaultKey());
    const std::optional<QStringList> handover = plainLaunchPaths();
    bool unanswered = false;
    if (!instance.isPrimary() && handover) {
        if (instance.forward(*handover))
            return 0;
        unanswered = true;
    }

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
    if (unanswered)
        qCWarning(logApp) << "another launch holds the window but did not answer; opening our own";

    // Queued signals carry decoded images from the worker pool to the GUI
    // thread, and an unregistered type makes that connection fail silently.
    qRegisterMetaType<decoders::DecodedImage>("decoders::DecodedImage");

    app.setWindowIcon(QIcon(QStringLiteral(":/icon.ico")));

    MainWindow window;
    window.show();

    QStringList positional;
    for (const QString &path : parser.positionalArguments())
        positional << QFileInfo(path).absoluteFilePath();
    window.openPaths(positional);

    window.acceptLaunches(&instance, positional);
    if (instance.isPrimary())
        instance.listen();
    // Closing: a launch from now on opens its own window instead of handing
    // its files to this one on its way out.
    QObject::connect(&app, &QGuiApplication::lastWindowClosed, &instance, &SingleInstance::stop);

    const int code = app.exec();
    qCInfo(logApp) << "--- exiting with code" << code << "---";
    applog::shutdown();
    return code;
}
