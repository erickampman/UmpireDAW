#pragma once

// The desktop's own file chooser, run as a separate process (zenity, or
// kdialog on KDE), so it never blocks the main loop: start() it, then poll()
// from the loop until it reports a result. Copied from StageHand's
// (src/App/FileDialog), as the widgets were (decisions.md, "Milestone 4"):
// DGL has no text entry, and DPF's built-in browser can't choose a folder (a
// session is one, "X.umpire") or suggest a name when saving; these tools do
// both. On macOS neither tool is there, so start() says so (docs/files.md,
// "Not in it"). Main thread; no toolkit types.

#include <string>
#include <sys/types.h>

namespace umpire {

class FileDialog {
public:
    enum class Mode {
        OpenFolder,   // choose an existing folder (a .umpire bundle)
        OpenFile,     // choose an existing file; `name` is a pattern to show, "*.midi2"
        Save,         // choose a new path; the tool doesn't ask about replacing
    };

    FileDialog() = default;
    ~FileDialog();   // a dialog still open is closed

    FileDialog(const FileDialog&)            = delete;
    FileDialog& operator=(const FileDialog&) = delete;

    // Which tool would run ("zenity", "kdialog"), or "" if neither is installed.
    static std::string tool();

    // Show the chooser, starting in `folder` (Save: with `name` filled in;
    // OpenFile: showing only files matching `name`, if it's given).
    // False, with `problem` set, if one is already open or no tool is found.
    bool start(Mode mode, const std::string& title, const std::string& folder, const std::string& name,
               std::string& problem);
    bool running() const noexcept { return child > 0; }

    // Once the chooser closes: true, with `path` set (empty if cancelled).
    bool poll(std::string& path);

private:
    void readOutput();

    pid_t       child  = -1;
    int         output = -1;   // the chooser's stdout
    std::string buffer;
};

} // namespace umpire
