#pragma once

#include <cstdint>
#include <string>

#include "kaffee/domain.hpp"
#include "kaffee/settings.hpp"

namespace kaffee::ui {

// Presentation helpers shared by the three pages. Pure C++ so the host tests
// can cover them without LVGL.

std::string format_money(Cents cents, bool with_symbol = true);
std::string format_balance(Cents cents);
std::string format_clock(Timestamp ts);
std::string format_date(Timestamp ts);
std::string format_month(Timestamp now, int month_offset);
void month_bounds(Timestamp now, int month_offset, Timestamp* from, Timestamp* to);
std::string format_uptime(uint64_t seconds);
std::string format_countdown(int seconds);
std::string format_bytes(uint64_t bytes);
std::string format_last_sync(Timestamp now, Timestamp last_sync);
std::string format_temperature(float celsius);
std::string initials(const std::string& name);
std::string time_source_label(TimeSource source, const std::string& last_source);
std::string rotation_label(int degrees);

// State of the microSD backup chip in the page-3 header. The raw backend error
// text is English and technical, so it never reaches the panel; only this
// mapping does. Every value is short enough to sit beside the clock.
enum class SdBackupState {
    NotDetected,        // no compatible card, or the mount failed
    Present,            // card seen, no run has succeeded yet
    Error,              // card seen, the last run failed
    Saved,              // card seen, last run wrote successfully
    SavedWithoutClock,  // success while the clock was unset (never a 1970 date)
};

struct SdBackupStatus {
    SdBackupState state = SdBackupState::NotDetected;
    std::string value;  // `SD` + short German state word, ready to render
};

SdBackupStatus sd_backup_status(bool card_present, bool backup_ok, Timestamp last_success,
                                bool has_error);

}  // namespace kaffee::ui
