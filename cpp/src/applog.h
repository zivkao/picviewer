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

/// Flush and close. Called on the way out so the last lines are not lost.
void shutdown();

} // namespace applog
