#include "App/FileDialog.h"

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

// FileDialog on macOS (FileDialog.h): NSOpenPanel and NSSavePanel, begun
// modeless, so the main loop (MainLoopMac.mm, which hands AppKit every event)
// keeps ticking while one is up. The completion handler keeps the answer for
// poll().

namespace umpire {

struct FileDialog::Panel {
    NSSavePanel* panel = nil;   // NSOpenPanel is one
    bool         done  = false;
    std::string  path;          // "" if cancelled
};

FileDialog::~FileDialog() {
    if (panel && !panel->done) [panel->panel cancel:nil];
}

std::string FileDialog::tool() { return "AppKit"; }

bool FileDialog::running() const noexcept { return panel != nullptr; }

bool FileDialog::start(Mode mode, const std::string& title, const std::string& folder, const std::string& name,
                       std::string& problem) {
    if (running()) {
        problem = "a file dialog is already open";
        return false;
    }
    @autoreleasepool {
        NSSavePanel* chooser = nil;
        if (mode == Mode::Save) {
            chooser                       = [NSSavePanel savePanel];
            chooser.nameFieldStringValue  = [NSString stringWithUTF8String:name.c_str()];
            chooser.canCreateDirectories  = YES;
            chooser.extensionHidden       = NO;
        } else {
            NSOpenPanel* const open      = [NSOpenPanel openPanel];
            open.canChooseDirectories    = mode == Mode::OpenFolder;
            open.canChooseFiles          = mode == Mode::OpenFile;
            open.allowsMultipleSelection = NO;
            // "*.midi2": only those files.
            if (mode == Mode::OpenFile && name.rfind("*.", 0) == 0) {
                if (UTType* const type = [UTType typeWithFilenameExtension:[NSString stringWithUTF8String:name.c_str() + 2]])
                    open.allowedContentTypes = @[type];
            }
            chooser = open;
        }
        // An open panel shows the message, not the title; a save panel, both.
        NSString* const text = [NSString stringWithUTF8String:title.c_str()];
        chooser.title        = text;
        chooser.message      = text;
        chooser.directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:folder.c_str()] isDirectory:YES];

        auto shown   = std::make_shared<Panel>();
        shown->panel = chooser;
        panel        = shown;
        [chooser beginWithCompletionHandler:^(NSModalResponse response) {
            if (response == NSModalResponseOK && shown->panel.URL != nil)
                shown->path = shown->panel.URL.path.fileSystemRepresentation;
            shown->done  = true;
            shown->panel = nil;   // no cycle through this handler
        }];
    }
    return true;
}

bool FileDialog::poll(std::string& path) {
    if (!panel || !panel->done) return false;
    path = std::move(panel->path);
    panel.reset();
    return true;
}

} // namespace umpire
