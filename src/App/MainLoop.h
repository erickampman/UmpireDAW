#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>

#include "uw/plugin/HostServices.h"

namespace umpire {

using uw::TimerId;
using uw::kInvalidTimer;
namespace FdEvent = uw::FdEvent;

// The app's main-thread event loop: it runs timers and watched file
// descriptors until asked to quit. Copied from StageHand's (src/App/MainLoop.h,
// 2026-10-23), which serves plugin editors (CLAP timer-support,
// posix-fd-support) and its own housekeeping. Umpire's main window is driven
// from one of its timers. Plugin editors come with milestone 10, when this
// may move into uw-core with the editor windows (decisions.md, "Milestone 4").
//
// Linux (MainLoop.cpp): a poll() loop, the editor windows' X11 connections
// among its fds. macOS (MainLoopMac.mm): AppKit's event loop, the timers as
// CFRunLoopTimers and the fds as CFFileDescriptors in the run loop's common
// modes, so they keep running while a window is resized or a menu is tracked
// (StageHand's docs/macos.md, "The app").
//
// Everything runs on the thread that calls run(), except requestQuit(), which is
// async-signal-safe so a SIGINT handler can call it.
class MainLoop {
public:
    MainLoop();    // creates the wake-up pipe
    ~MainLoop();

    MainLoop(const MainLoop&)            = delete;
    MainLoop& operator=(const MainLoop&) = delete;

    // A periodic callback. A timer that falls behind is rescheduled from now, not
    // fired back-to-back to catch up.
    TimerId addTimer(std::uint32_t periodMs, std::function<void()> callback);
    void    removeTimer(TimerId id);   // safe from inside any callback

    // Call `callback` with the FdEvent bits that fired whenever `fd` is ready for
    // `events`. Returns false if `fd` is already watched.
    bool watchFd(int fd, std::uint32_t events, std::function<void(std::uint32_t)> callback);
    bool modifyFd(int fd, std::uint32_t events);
    void unwatchFd(int fd);            // safe from inside any callback

    // Run until requestQuit().
    void run();

    // Make run() return soon. Async-signal-safe: an atomic store plus a write()
    // to the wake-up pipe.
    void requestQuit() noexcept;

    // True while the system runs a loop of its own on this thread: on macOS,
    // AppKit tracking a window resize, a title-bar drag or a menu (the run
    // loop is in a mode other than the default one). Timers still fire then,
    // but nothing may take events off the queue: those events belong to
    // AppKit's loop, and dispatching them ourselves can start a second resize
    // inside the first, which crashes AppKit. Always false on Linux. From
    // StageHand (cb90fbb, 2026-11-09).
    static bool systemTracking() noexcept;

private:
#ifdef __APPLE__
    struct Timer {
        void*                                  timer = nullptr;   // CFRunLoopTimerRef
        std::shared_ptr<std::function<void()>> callback;
    };
    struct Watch {
        void*                                               descriptor = nullptr;   // CFFileDescriptorRef
        void*                                               source     = nullptr;   // CFRunLoopSourceRef
        std::uint32_t                                       events     = 0;
        std::shared_ptr<std::function<void(std::uint32_t)>> callback;
    };
    void enable(int fd);   // CFFileDescriptor callbacks are one-shot

public:
    void dispatchFd(int fd, std::uint32_t fired);   // from the CFFileDescriptor callback; not for callers

private:
    std::map<TimerId, Timer> timers;
    std::map<int, Watch>     watches;
    TimerId                  nextTimerId = 1;
    int                      wakeRead    = -1;   // self-pipe: requestQuit() from a signal handler
    int                      wakeWrite   = -1;
    std::atomic<bool>        quit{false};
#else
    using Clock = std::chrono::steady_clock;

    // Callbacks are shared so a dispatch keeps its callback alive even if the
    // callback removes its own timer or watch.
    struct Timer {
        std::chrono::milliseconds              period;
        Clock::time_point                      due;
        std::shared_ptr<std::function<void()>> callback;
    };
    struct Watch {
        std::uint32_t                                       events;
        std::shared_ptr<std::function<void(std::uint32_t)>> callback;
    };

    std::map<TimerId, Timer> timers;
    std::map<int, Watch>     watches;
    TimerId                  nextTimerId = 1;
    int                      wakeRead    = -1;   // self-pipe (portable; eventfd is Linux's)
    int                      wakeWrite   = -1;
    std::atomic<bool>        quit{false};
#endif
};

} // namespace umpire
