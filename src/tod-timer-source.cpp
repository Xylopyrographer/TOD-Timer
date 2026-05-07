#include "tod-timer-source.hpp"

#include <obs-module.h>
#include <cstdio>
#include <ctime>
#include <string>

// clock_gettime / localtime_r are POSIX (macOS 10.12+, Linux).
// For Windows, swap to timespec_get + localtime_s when adding Win32 support.
#ifndef _WIN32
    #include <time.h>
#endif

// S_* settings keys are defined in tod-timer-source.hpp (shared with settings-dialog).

#ifdef ENABLE_QT
    #include "settings-dialog.hpp"
    #include <obs-frontend-api.h>
    #include <QApplication>
    #include <QWidget>
#endif

// --------------------------------------------------------------------------
// Display-format helper
// Builds the formatted countdown string from total remaining milliseconds,
// honouring the per-segment SegmentFormat rules.
// --------------------------------------------------------------------------
static std::string build_timer_string( long long remaining_ms,
                                       SegmentFormat fmt_h, SegmentFormat fmt_m,
                                       SegmentFormat fmt_s, SegmentFormat fmt_t ) {
    // Extract each unit, or leave its value rolled into the next unit when hidden.
    long long hrs    = 0;
    long long mins   = 0;
    long long secs   = 0;
    long long tenths = 0;

    if ( fmt_h != SegmentFormat::HIDE_ROLLDOWN ) {
        hrs           = remaining_ms / 3600000LL;
        remaining_ms %= 3600000LL;
    }
    if ( fmt_m != SegmentFormat::HIDE_ROLLDOWN ) {
        mins          = remaining_ms / 60000LL;
        remaining_ms %= 60000LL;
    }
    if ( fmt_s != SegmentFormat::HIDE_ROLLDOWN ) {
        secs          = remaining_ms / 1000LL;
        remaining_ms %= 1000LL;
    }
    tenths = remaining_ms / 100LL;

    // Write each visible segment into a small buffer.
    // H, M, S are separated by ":" and tenths by ".".
    char   buf[ 64 ];
    int    pos       = 0;
    bool   any       = false;  // has any segment been written?
    bool   any_nonzero = false; // has any *non-zero* segment been written to the left?
    // ↑ When true, "no leading zero" formats still get a leading zero —
    //   e.g. minutes=1, seconds=9 → "1:09" not "1:9".

    auto write_seg = [ & ]( long long val, SegmentFormat fmt, bool is_tenths ) {
        if ( fmt == SegmentFormat::HIDE_ROLLDOWN ) {
            return;
        }

        const bool conditional = ( fmt == SegmentFormat::IF_NONZERO_NO_LEAD ||
                                   fmt == SegmentFormat::IF_NONZERO_LEAD );
        // Hide only when the segment is zero AND nothing non-zero has been written
        // to its left. If a higher-order unit is non-zero, a zero lower-order unit
        // must still be shown (e.g. 57:00 not just 57).
        if ( conditional && val == 0 && !any_nonzero ) {
            return;
        }

        // Explicit-lead formats always pad; no-lead formats pad only when
        // a non-zero higher-order unit has already been written to the left.
        const bool explicit_lead = ( fmt == SegmentFormat::ALWAYS_LEAD ||
                                     fmt == SegmentFormat::IF_NONZERO_LEAD );
        const bool leading = explicit_lead || any_nonzero;

        if ( any ) {
            buf[ pos++ ] = is_tenths ? '.' : ':';
        }

        if ( is_tenths ) {
            // Tenths is always a single digit 0-9; no padding needed.
            buf[ pos++ ] = '0' + static_cast<char>( val % 10 );
        }
        else if ( leading ) {
            pos += snprintf( buf + pos, sizeof( buf ) - pos, "%02lld", val );
        }
        else {
            pos += snprintf( buf + pos, sizeof( buf ) - pos, "%lld", val );
        }

        any = true;
        if ( val != 0 ) {
            any_nonzero = true;
        }
    };

    write_seg( hrs,    fmt_h, false );
    write_seg( mins,   fmt_m, false );
    write_seg( secs,   fmt_s, false );
    write_seg( tenths, fmt_t, true  );

    buf[ pos ] = '\0';
    // Safety: if every segment was hidden or conditional-zero, show "0".
    if ( pos == 0 ) {
        buf[ 0 ] = '0';
        buf[ 1 ] = '\0';
    }

    return buf;
}

// Convenience wrapper used by video_tick — reads atomics into SegmentFormat.
static std::string build_timer_string_from_source( long long remaining_ms,
        const TodTimerSource *d ) {
    return build_timer_string(
               remaining_ms,
               static_cast<SegmentFormat>( d->fmt_hours.load(   std::memory_order_relaxed ) ),
               static_cast<SegmentFormat>( d->fmt_minutes.load( std::memory_order_relaxed ) ),
               static_cast<SegmentFormat>( d->fmt_seconds.load( std::memory_order_relaxed ) ),
               static_cast<SegmentFormat>( d->fmt_tenths.load(  std::memory_order_relaxed ) )
           );
}

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

// Push the current appearance settings from our obs_data onto the inner
// text source. Called from both update() and create().
static void apply_appearance( TodTimerSource *d, obs_data_t *settings ) {
    if ( !d->text_source ) {
        return;
    }

    obs_data_t *ts = obs_data_create();

    // Font — obs_data object with keys: face, style, size, flags.
    // The text source expects exactly this structure under "font".
    obs_data_t *font = obs_data_get_obj( settings, S_FONT );
    if ( font ) {
        obs_data_set_obj( ts, "font", font );
        obs_data_release( font );
    }

    // Color — FT2 uses "color1" (top) and "color2" (bottom) for its gradient.
    // Set both to the same value so the text is a solid colour with no gradient.
    const int64_t col = obs_data_get_int( settings, S_COLOR );
    obs_data_set_int( ts, "color1", col );
    obs_data_set_int( ts, "color2", col );

    // Shadow
    obs_data_set_bool( ts, "drop_shadow",
                       obs_data_get_bool( settings, S_DROP_SHADOW ) );

    // Stroke / outline
    // Note: text_ft2_source_v2 does not support outline_color or outline_size;
    // the outline is always drawn in black at a fixed internal offset.
    obs_data_set_bool( ts, "outline",
                       obs_data_get_bool( settings, S_OUTLINE ) );

    obs_source_update( d->text_source, ts );
    obs_data_release( ts );
}

// --------------------------------------------------------------------------
// Activation / deactivation callbacks (run on the main thread)
// --------------------------------------------------------------------------

static void tod_activate( void *data ) {
    auto *d = static_cast<TodTimerSource *>( data );
    if ( d->auto_start.load( std::memory_order_relaxed ) ) {
        d->reached_zero.store( false, std::memory_order_relaxed );
        d->reset_pending.store( true,  std::memory_order_release );
        d->running.store(       true,  std::memory_order_relaxed );
    }
}

static void tod_deactivate( void *data ) {
    auto *d = static_cast<TodTimerSource *>( data );
    if ( d->auto_stop.load( std::memory_order_relaxed ) ) {
        d->running.store( false, std::memory_order_relaxed );
    }
}

// --------------------------------------------------------------------------
// OBS source callbacks
// --------------------------------------------------------------------------

static const char *tod_get_name( void * ) {
    return obs_module_text( "TODTimerSource" );
}

static void *tod_create( obs_data_t *settings, obs_source_t *source ) {
    auto *d      = new TodTimerSource{};
    d->source    = source;
    d->text_source = obs_source_create_private( TEXT_SOURCE_ID,
                     "tod_timer_inner_text", nullptr );

    d->target_hour.store(  ( int )obs_data_get_int( settings, S_TARGET_HOUR ) );
    d->target_minute.store( ( int )obs_data_get_int( settings, S_TARGET_MINUTE ) );
    d->target_second.store( ( int )obs_data_get_int( settings, S_TARGET_SECOND ) );
    d->target_tenths.store( ( int )obs_data_get_int( settings, S_TARGET_TENTHS ) );

    d->fmt_hours.store(   ( int )obs_data_get_int( settings, S_FMT_HOURS   ) );
    d->fmt_minutes.store( ( int )obs_data_get_int( settings, S_FMT_MINUTES ) );
    d->fmt_seconds.store( ( int )obs_data_get_int( settings, S_FMT_SECONDS ) );
    d->fmt_tenths.store(  ( int )obs_data_get_int( settings, S_FMT_TENTHS  ) );

    d->auto_start.store(   obs_data_get_bool( settings, S_AUTO_START   ) );
    d->auto_stop.store(    obs_data_get_bool( settings, S_AUTO_STOP    ) );
    d->stop_at_zero.store( obs_data_get_bool( settings, S_STOP_AT_ZERO ) );
    d->hide_at_zero.store( obs_data_get_bool( settings, S_HIDE_AT_ZERO ) );

    apply_appearance( d, settings );
    return d;
}

static void tod_destroy( void *data ) {
    auto *d = static_cast<TodTimerSource *>( data );
    obs_source_release( d->text_source );
    delete d;
}

static void tod_update( void *data, obs_data_t *settings ) {
    auto *d = static_cast<TodTimerSource *>( data );

    d->target_hour.store(  ( int )obs_data_get_int( settings, S_TARGET_HOUR ) );
    d->target_minute.store( ( int )obs_data_get_int( settings, S_TARGET_MINUTE ) );
    d->target_second.store( ( int )obs_data_get_int( settings, S_TARGET_SECOND ) );
    d->target_tenths.store( ( int )obs_data_get_int( settings, S_TARGET_TENTHS ) );

    d->fmt_hours.store(   ( int )obs_data_get_int( settings, S_FMT_HOURS   ) );
    d->fmt_minutes.store( ( int )obs_data_get_int( settings, S_FMT_MINUTES ) );
    d->fmt_seconds.store( ( int )obs_data_get_int( settings, S_FMT_SECONDS ) );
    d->fmt_tenths.store(  ( int )obs_data_get_int( settings, S_FMT_TENTHS  ) );

    d->auto_start.store(   obs_data_get_bool( settings, S_AUTO_START   ) );
    d->auto_stop.store(    obs_data_get_bool( settings, S_AUTO_STOP    ) );
    d->stop_at_zero.store( obs_data_get_bool( settings, S_STOP_AT_ZERO ) );
    d->hide_at_zero.store( obs_data_get_bool( settings, S_HIDE_AT_ZERO ) );

    // When settings change (e.g. new target time), un-freeze any zero-hold.
    d->reached_zero.store( false, std::memory_order_relaxed );
    d->reset_pending.store( true, std::memory_order_release );

    apply_appearance( d, settings );
}

static void tod_get_defaults( obs_data_t *settings ) {
    // Default font: Arial 72pt, plain
    obs_data_t *font = obs_data_create();
    obs_data_set_default_string( font, "face",  "Arial" );
    obs_data_set_default_string( font, "style", "" );
    obs_data_set_default_int( font,    "size",  72 );
    obs_data_set_default_int( font,    "flags", 0 );
    obs_data_set_default_obj( settings, S_FONT, font );
    obs_data_release( font );

    obs_data_set_default_int( settings,  S_COLOR,       0xFFFFFFFF ); // white, full alpha
    obs_data_set_default_bool( settings, S_DROP_SHADOW, false );
    obs_data_set_default_bool( settings, S_OUTLINE,     false );

    obs_data_set_default_int( settings, S_TARGET_HOUR,   12 );
    obs_data_set_default_int( settings, S_TARGET_MINUTE,  0 );
    obs_data_set_default_int( settings, S_TARGET_SECOND,  0 );
    obs_data_set_default_int( settings, S_TARGET_TENTHS,  0 );

    // Default format: hh:mm:ss.t
    obs_data_set_default_int( settings, S_FMT_HOURS,   ( int )SegmentFormat::ALWAYS_LEAD );
    obs_data_set_default_int( settings, S_FMT_MINUTES, ( int )SegmentFormat::ALWAYS_LEAD );
    obs_data_set_default_int( settings, S_FMT_SECONDS, ( int )SegmentFormat::ALWAYS_LEAD );
    obs_data_set_default_int( settings, S_FMT_TENTHS,  ( int )SegmentFormat::ALWAYS_NO_LEAD );

    obs_data_set_default_bool( settings, S_AUTO_START,   true  );
    obs_data_set_default_bool( settings, S_AUTO_STOP,    true  );
    obs_data_set_default_bool( settings, S_STOP_AT_ZERO, true  );
    obs_data_set_default_bool( settings, S_HIDE_AT_ZERO, false );
}

#ifdef ENABLE_QT
// Open the Qt settings dialog when the "Configure…" button is clicked.
// 'data' is TodTimerSource* (passed via obs_properties_create_param).
static bool tod_on_configure( obs_properties_t *, obs_property_t *, void *data ) {
    auto *d = static_cast<TodTimerSource *>( data );
    obs_data_t *settings = obs_source_get_settings( d->source );

    SettingsDialog dlg( settings,
                        static_cast<QWidget *>( obs_frontend_get_main_window() ) );
    if ( dlg.exec() == QDialog::Accepted ) {
        dlg.applyToSettings( settings );

        // Apply changes to the source immediately (timer starts using new values).
        obs_source_update( d->source, settings );

        // Persist the updated settings to the scene collection JSON right now.
        obs_frontend_save();

        // Close the OBS Properties panel cleanly so quitting OBS doesn't trigger
        // its "unsaved changes" prompt.  OBSBasicProperties does not override
        // accept(), so QDialog::accept() hides it without the closeEvent revert.
        for ( QWidget *w : QApplication::topLevelWidgets() ) {
            if ( strcmp( w->metaObject()->className(), "OBSBasicProperties" ) == 0 ) {
                QMetaObject::invokeMethod( w, "accept", Qt::QueuedConnection );
                break;
            }
        }
    }

    obs_data_release( settings );
    return false;
}
#endif

static obs_properties_t *tod_get_properties( void *data ) {
    #ifdef ENABLE_QT
    obs_properties_t *props = obs_properties_create_param( data, nullptr );
    obs_properties_add_button( props, "configure",
                               obs_module_text( "Configure" ), tod_on_configure );
    return props;
    #else
    ( void )data;
    obs_properties_t *props = obs_properties_create();
    obs_properties_add_int( props, S_TARGET_HOUR,
                            obs_module_text( "TargetHour" ),   0, 23, 1 );
    obs_properties_add_int( props, S_TARGET_MINUTE,
                            obs_module_text( "TargetMinute" ), 0, 59, 1 );
    obs_properties_add_int( props, S_TARGET_SECOND,
                            obs_module_text( "TargetSecond" ), 0, 59, 1 );
    obs_properties_add_int( props, S_TARGET_TENTHS,
                            obs_module_text( "TargetTenths" ), 0,  9, 1 );
    obs_properties_add_bool( props, S_AUTO_START,   obs_module_text( "AutoStart" ) );
    obs_properties_add_bool( props, S_AUTO_STOP,    obs_module_text( "AutoStop" ) );
    obs_properties_add_bool( props, S_STOP_AT_ZERO, obs_module_text( "StopAtZero" ) );
    obs_properties_add_bool( props, S_HIDE_AT_ZERO, obs_module_text( "HideAtZero" ) );
    obs_properties_add_font( props, S_FONT,  obs_module_text( "Font" ) );
    obs_properties_add_color_alpha( props, S_COLOR, obs_module_text( "Color" ) );
    obs_properties_add_bool( props, S_DROP_SHADOW, obs_module_text( "Shadow" ) );
    obs_properties_add_bool( props, S_OUTLINE,     obs_module_text( "Stroke" ) );
    auto add_fmt = [ & ]( const char *key, const char *label ) {
        obs_property_t *p = obs_properties_add_list( props, key, label,
                            OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT );
        obs_property_list_add_int( p, obs_module_text( "FmtAlwaysNoLead"    ),
                                   ( int )SegmentFormat::ALWAYS_NO_LEAD     );
        obs_property_list_add_int( p, obs_module_text( "FmtAlwaysLead"      ),
                                   ( int )SegmentFormat::ALWAYS_LEAD        );
        obs_property_list_add_int( p, obs_module_text( "FmtIfNonzeroNoLead" ),
                                   ( int )SegmentFormat::IF_NONZERO_NO_LEAD );
        obs_property_list_add_int( p, obs_module_text( "FmtIfNonzeroLead"   ),
                                   ( int )SegmentFormat::IF_NONZERO_LEAD    );
        obs_property_list_add_int( p, obs_module_text( "FmtHideRolldown"    ),
                                   ( int )SegmentFormat::HIDE_ROLLDOWN      );
    };
    add_fmt( S_FMT_HOURS,   obs_module_text( "FmtHoursLabel"   ) );
    add_fmt( S_FMT_MINUTES, obs_module_text( "FmtMinutesLabel" ) );
    add_fmt( S_FMT_SECONDS, obs_module_text( "FmtSecondsLabel" ) );
    add_fmt( S_FMT_TENTHS,  obs_module_text( "FmtTenthsLabel"  ) );
    return props;
    #endif
}

// --------------------------------------------------------------------------
// Render-thread callbacks
// --------------------------------------------------------------------------

static void tod_video_tick( void *data, float /*seconds*/ ) {
    auto *d = static_cast<TodTimerSource *>( data );
    if ( !d->text_source ) {
        return;
    }

    // Honour any pending display-reset requested by the main thread
    // (e.g. source activated, settings changed).
    if ( d->reset_pending.exchange( false, std::memory_order_acq_rel ) ) {
        d->last_display.clear();
        d->counting_started = false; // re-evaluate on next tick
    }

    // If the timer already hit zero and is frozen, nothing to do.
    if ( d->reached_zero.load( std::memory_order_relaxed ) ) {
        return;
    }

    std::string text;

    if ( !d->running.load( std::memory_order_relaxed ) ) {
        // Stopped: freeze at the last rendered string.
        // On the very first tick, build a placeholder from the target time.
        if ( d->last_display.empty() ) {
            const long long placeholder_ms =
                ( long long )d->target_hour.load()   * 3600000LL
                + ( long long )d->target_minute.load() *   60000LL
                + ( long long )d->target_second.load() *    1000LL
                + ( long long )d->target_tenths.load() *     100LL;
            text = build_timer_string_from_source( placeholder_ms, d );
        }
        else {
            return; // Nothing to update.
        }
    }
    else {
        // Running: compute remaining time using POSIX (macOS 12+ / Linux).
        struct timespec ts;
        clock_gettime( CLOCK_REALTIME, &ts );
        struct tm lt;
        localtime_r( &ts.tv_sec, &lt );

        // Target time expressed as milliseconds since midnight.
        const long long target_ms =
            ( long long )d->target_hour.load()   * 3600000LL
            + ( long long )d->target_minute.load() *   60000LL
            + ( long long )d->target_second.load() *    1000LL
            + ( long long )d->target_tenths.load() *     100LL;

        // Current time expressed as milliseconds since midnight.
        const long long current_ms =
            ( ( long long )lt.tm_hour * 3600LL
              + ( long long )lt.tm_min  *   60LL
              + ( long long )lt.tm_sec ) * 1000LL
            + ts.tv_nsec / 1000000LL;

        long long remaining = target_ms - current_ms;

        if ( remaining > 0 ) {
            // We're actively counting down — record that fact.
            d->counting_started = true;
        }
        else {
            // remaining <= 0
            const bool should_stop = d->stop_at_zero.load( std::memory_order_relaxed );
            const bool should_hide = d->hide_at_zero.load( std::memory_order_relaxed );

            if ( d->counting_started && ( should_stop || should_hide ) ) {
                // The timer actually ran down to zero — freeze it.
                d->reached_zero.store( true,  std::memory_order_relaxed );
                d->running.store(      false, std::memory_order_relaxed );

                if ( should_hide ) {
                    // Empty text makes the text source render nothing (0×0).
                    if ( !d->last_display.empty() ) {
                        d->last_display.clear();
                        obs_data_t *td = obs_data_create();
                        obs_data_set_string( td, "text", "" );
                        obs_source_update( d->text_source, td );
                        obs_data_release( td );
                    }
                    return;
                }

                // stop_at_zero only: show the display frozen at 00:00:00.0.
                remaining = 0;
            }
            else {
                // Target already passed when we started (or neither flag set) —
                // count down to the next occurrence (same time tomorrow).
                remaining += 24LL * 3600000LL;
            }
        }

        text = build_timer_string_from_source( remaining, d );
    }

    if ( text.empty() || text == d->last_display ) {
        return;
    }

    d->last_display = text;

    obs_data_t *td = obs_data_create();
    obs_data_set_string( td, "text", text.c_str() );
    obs_source_update( d->text_source, td );
    obs_data_release( td );
}

static void tod_video_render( void *data, gs_effect_t * /*effect*/ ) {
    auto *d = static_cast<TodTimerSource *>( data );
    if ( d->text_source ) {
        obs_source_video_render( d->text_source );
    }
}

static uint32_t tod_get_width( void *data ) {
    auto *d = static_cast<TodTimerSource *>( data );
    return d->text_source ? obs_source_get_width( d->text_source ) : 0;
}

static uint32_t tod_get_height( void *data ) {
    auto *d = static_cast<TodTimerSource *>( data );
    return d->text_source ? obs_source_get_height( d->text_source ) : 0;
}

// --------------------------------------------------------------------------
// Registration
// --------------------------------------------------------------------------

void register_tod_timer_source() {
    static obs_source_info info{};

    info.id           = "tod_countdown_timer";
    info.type         = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;

    info.get_name       = tod_get_name;
    info.create         = tod_create;
    info.destroy        = tod_destroy;
    info.update         = tod_update;
    info.get_properties = tod_get_properties;
    info.get_defaults   = tod_get_defaults;
    info.video_render   = tod_video_render;
    info.video_tick     = tod_video_tick;
    info.get_width      = tod_get_width;
    info.get_height     = tod_get_height;
    info.activate       = tod_activate;
    info.deactivate     = tod_deactivate;

    obs_register_source( &info );
}
