#pragma once

#include <obs-module.h>
#include <atomic>
#include <string>

// --------------------------------------------------------------------------
// Settings keys (shared between tod-timer-source.cpp and settings-dialog.cpp)
// --------------------------------------------------------------------------
#define S_TARGET_HOUR    "target_hour"
#define S_TARGET_MINUTE  "target_minute"
#define S_TARGET_SECOND  "target_second"
#define S_TARGET_TENTHS  "target_tenths"
#define S_FONT           "font"
#define S_COLOR          "color"
#define S_DROP_SHADOW    "drop_shadow"
#define S_OUTLINE        "outline"
#define S_FMT_HOURS      "fmt_hours"
#define S_FMT_MINUTES    "fmt_minutes"
#define S_FMT_SECONDS    "fmt_seconds"
#define S_FMT_TENTHS     "fmt_tenths"
#define S_AUTO_START     "auto_start"
#define S_AUTO_STOP      "auto_stop"
#define S_STOP_AT_ZERO   "stop_at_zero"
#define S_HIDE_AT_ZERO   "hide_at_zero"

// --------------------------------------------------------------------------
// Platform text source ID
// OBS ships text_ft2_source_v2 on macOS/Linux, text_gdiplus_v2 on Windows.
// --------------------------------------------------------------------------
#if defined(_WIN32)
    static constexpr const char *TEXT_SOURCE_ID = "text_gdiplus_v2";
#else
    static constexpr const char *TEXT_SOURCE_ID = "text_ft2_source_v2";
#endif

// --------------------------------------------------------------------------
// Per-segment display format — matches the 5 options from ProPresenter / RV.
//   ALWAYS_NO_LEAD    : always show, no leading zero          (h  / m  / s  / t)
//   ALWAYS_LEAD       : always show, with leading zero        (hh / mm / ss / tt)
//   IF_NONZERO_NO_LEAD: show only when value > 0, no leading  (h? / m? / s? / t?)
//   IF_NONZERO_LEAD   : show only when value > 0, leading     (hh?/ mm?/ ss?/ tt?)
//   HIDE_ROLLDOWN     : hide this unit; its value rolls into   (--)
//                       the next lower unit (e.g. hide hours →
//                       minutes can exceed 59)
// --------------------------------------------------------------------------
enum class SegmentFormat : int {
    ALWAYS_NO_LEAD     = 0,
    ALWAYS_LEAD        = 1,
    IF_NONZERO_NO_LEAD = 2,
    IF_NONZERO_LEAD    = 3,
    HIDE_ROLLDOWN      = 4,
};

// --------------------------------------------------------------------------
// Per-instance data
// target_* and running are written from the UI thread (properties callbacks)
// and read from the render thread (video_tick / video_render), so they use
// std::atomic. last_display is only touched by the render thread.
// --------------------------------------------------------------------------
struct TodTimerSource {
    obs_source_t *source{nullptr};
    obs_source_t *text_source{nullptr};

    // Runtime state — written from both the main thread (activate/deactivate/
    // update) and the render thread (video_tick), so all atomics.
    std::atomic<bool> running{false};
    std::atomic<bool> reached_zero{false};   // frozen after hitting 00:00:00.0
    std::atomic<bool> reset_pending{false};  // tells render thread to clear last_display

    // Behaviour checkboxes
    std::atomic<bool> auto_start{true};
    std::atomic<bool> auto_stop{true};
    std::atomic<bool> stop_at_zero{true};
    std::atomic<bool> hide_at_zero{false};

    std::atomic<int> target_hour{12};
    std::atomic<int> target_minute{0};
    std::atomic<int> target_second{0};
    std::atomic<int> target_tenths{0};

    // Per-segment display format (stored as int; cast to SegmentFormat when read).
    std::atomic<int> fmt_hours{( int )SegmentFormat::ALWAYS_LEAD};
    std::atomic<int> fmt_minutes{( int )SegmentFormat::ALWAYS_LEAD};
    std::atomic<int> fmt_seconds{( int )SegmentFormat::ALWAYS_LEAD};
    std::atomic<int> fmt_tenths{( int )SegmentFormat::ALWAYS_NO_LEAD};

    // Render-thread-only state (never touched from other threads).
    std::string last_display;
    bool counting_started{false}; // true once we've seen remaining > 0 while running;
    // prevents "freeze at zero" when target already passed.
};

// Registers the obs_source_info with OBS. Called from obs_module_load().
void register_tod_timer_source();
