#pragma once

// The desktop's own file chooser, so it never blocks the main loop: start()
// it, then poll() from the loop until it reports a result. Main thread; no
// toolkit types.
//
// On Linux it's a separate process (zenity, or kdialog on KDE), copied from
// StageHand's (src/App/FileDialog), as the widgets were (decisions.md,
// "Milestone 4"): DGL has no text entry, and DPF's built-in browser can't
// choose a folder (a session is one, "X.umpire") or suggest a name when
// saving; these tools do both. On macOS it's AppKit's open and save panels,
// shown modeless (FileDialogMac.mm; docs/files.md).

#include <memory>
#include <string>
#include <sys/types.h>

namespace umpire {

class FileDialog {
public:
    enum class Mode {
        OpenFolder,   // choose an existing folder (a .umpire bundle)
        OpenFile,     // choose an existing file; `name` is a pattern to show, "*.midi2"
        Save,         // choose a new path (Linux's tools don't ask about replacing; macOS's panel does)
    };

    FileDialog() = default;
    ~FileDialog();   // a dialog still open is closed

    FileDialog(const FileDialog&)            = delete;
    FileDialog& operator=(const FileDialog&) = delete;

    // Which tool would run ("zenity", "kdialog", "AppKit"), or "" if none is
    // installed.
    static std::string tool();

    // Show the chooser, starting in `folder` (Save: with `name` filled in;
    // OpenFile: showing only files matching `name`, if it's given).
    // False, with `problem` set, if one is already open or no tool is found.
    bool start(Mode mode, const std::string& title, const std::string& folder, const std::string& name,
               std::string& problem);
    bool running() const noexcept;

    // Once the chooser closes: true, with `path` set (empty if cancelled).
    bool poll(std::string& path);

private:
#ifdef __APPLE__
    struct Panel;   // the panel and, once it closes, its answer
    std::shared_ptr<Panel> panel;
#else
    void readOutput();

    pid_t       child  = -1;
    int         output = -1;   // the chooser's stdout
    std::string buffer;
#endif
};

} // namespace umpire
