// Logging to a rotating file.
//
// The app is built WIN32, so it has no console: anything written to stderr
// disappears. The file is not a nicety, it is the only record that exists --
// without it a decode that silently returns the wrong image leaves no trace,
// and a crash before the window appears leaves nothing at all.

#pragma once

#include <QString>

namespace applog {

/// %LOCALAPPDATA%\PicViewer\logs -- per user, outside the install, so it
/// survives a reinstall and needs no write access to Program Files.
QString directory();
QString filePath();

/// Install the message handler. Call before anything that might log.
void install(bool verbose);

/// Record what this is running on and against.
///
/// Decode bugs are routinely specific to a library version, and a report
/// without these cannot be reproduced. Called once the application object
/// exists, since the screen details need it.
void logEnvironment();

/// Catch what would otherwise be a silent disappearance.
///
/// The decoding happens inside LibRaw and libheif, C libraries handed
/// arbitrary files, so an access violation is the realistic failure -- and
/// with no console and no handler it leaves nothing behind at all. The Python
/// version has sys.excepthook for the same reason.
void installCrashHandler();

/// Flush and close. Called on the way out so the last lines are not lost.
void shutdown();

} // namespace applog
