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

#include <cstdint>
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
        // A line of text, `name` filled in; `folder` is the OK button's
        // label ("Rename"). poll() gives what was typed ("" if cancelled).
        // A real text field, so a host that keeps keys for itself still
        // lets them reach it (P4: renaming a take in the plugin).
        Text,
    };

    FileDialog() = default;
    ~FileDialog();   // a dialog still open is closed

    FileDialog(const FileDialog&)            = delete;
    FileDialog& operator=(const FileDialog&) = delete;

    // The view the chooser belongs to (the Mac: an NSView). Set, the Mac's
    // panel opens as a sheet on that view's window, so it's in front of it
    // (a plugin's window, which a host may float above a free panel: P4,
    // docs/plugin-version.md). Unset, it's a window of its own, as in the
    // app. Ignored on Linux.
    void setParent(std::uintptr_t nativeView) noexcept { parentView = nativeView; }

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
    std::uintptr_t parentView = 0;
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
