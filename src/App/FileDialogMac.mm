#include "App/FileDialog.h"

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

// FileDialog on macOS (FileDialog.h): NSOpenPanel and NSSavePanel, begun
// modeless (or as a sheet on the parent view's window, if one is given), so
// the main loop (MainLoopMac.mm, which hands AppKit every event) keeps
// ticking while one is up. The completion handler keeps the answer for
// poll().

namespace umpire {

struct FileDialog::Panel {
    NSSavePanel* panel = nil;   // NSOpenPanel is one
    NSAlert*     alert = nil;   // Mode::Text: an alert with a text field
    bool         done  = false;
    std::string  path;          // "" if cancelled
};

FileDialog::~FileDialog() {
    if (!panel || panel->done) return;
    if (panel->panel != nil) [panel->panel cancel:nil];
    if (panel->alert != nil) {
        NSWindow* const sheet = panel->alert.window;
        if (sheet.sheetParent != nil) [sheet.sheetParent endSheet:sheet returnCode:NSAlertSecondButtonReturn];
    }
}

std::string FileDialog::tool() { return "AppKit"; }

bool FileDialog::running() const noexcept { return panel != nullptr; }

bool FileDialog::start(Mode mode, const std::string& title, const std::string& folder, const std::string& name,
                       std::string& problem) {
    if (running()) {
        problem = "a file dialog is already open";
        return false;
    }
    NSWindow* const owner = parentView != 0 ? [(__bridge NSView*)reinterpret_cast<void*>(parentView) window] : nil;
    if (mode == Mode::Text) {
        @autoreleasepool {
            NSAlert* const alert = [[NSAlert alloc] init];
            alert.messageText    = [NSString stringWithUTF8String:title.c_str()];
            [alert addButtonWithTitle:[NSString stringWithUTF8String:folder.empty() ? "OK" : folder.c_str()]];
            [alert addButtonWithTitle:@"Cancel"];
            NSTextField* const field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 280, 24)];
            field.stringValue        = [NSString stringWithUTF8String:name.c_str()];
            alert.accessoryView      = field;
            [alert layout];
            alert.window.initialFirstResponder = field;
            auto shown   = std::make_shared<Panel>();
            shown->alert = alert;
            panel        = shown;
            const auto finished = ^(NSModalResponse response) {
                if (response == NSAlertFirstButtonReturn) shown->path = field.stringValue.UTF8String;
                shown->done  = true;
                shown->alert = nil;
            };
            if (owner != nil) {
                [alert beginSheetModalForWindow:owner completionHandler:finished];
                [alert.window makeFirstResponder:field];
            } else {
                finished([alert runModal]);   // the app has no use for it yet
            }
        }
        return true;
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
        void (^finished)(NSModalResponse) = ^(NSModalResponse response) {
            if (response == NSModalResponseOK && shown->panel.URL != nil)
                shown->path = shown->panel.URL.path.fileSystemRepresentation;
            shown->done  = true;
            shown->panel = nil;   // no cycle through this handler
        };
        // On a parent's window, a sheet: in front of it, and still modeless
        // for the rest of the app (the host goes on).
        if (owner != nil) [chooser beginSheetModalForWindow:owner completionHandler:finished];
        else [chooser beginWithCompletionHandler:finished];
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
