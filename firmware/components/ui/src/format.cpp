#include "format.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "strings.hpp"

namespace kaffee::ui {
namespace {

std::string decimal_comma(double value, int precision) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, value);
    std::string out(buf);
    for (char& c : out) {
        if (c == '.') c = ',';
    }
    return out;
}

void append_first_utf8(std::string& out, const std::string& text) {
    if (text.empty()) return;
    unsigned char lead = static_cast<unsigned char>(text[0]);
    size_t len = 1;
    if ((lead & 0xE0) == 0xC0) {
        len = 2;
    } else if ((lead & 0xF0) == 0xE0) {
        len = 3;
    } else if ((lead & 0xF8) == 0xF0) {
        len = 4;
    }
    if (len > text.size()) len = 1;
    out.append(text, 0, len);
}

std::tm local_tm(Timestamp ts) {
    std::tm tm{};
    std::time_t t = static_cast<std::time_t>(ts);
    if (ts <= 0 || localtime_r(&t, &tm) == nullptr) {
        tm.tm_year = 70;
        tm.tm_mon = 0;
        tm.tm_mday = 1;
    }
    return tm;
}

}  // namespace

std::string format_money(Cents cents, bool with_symbol) {
    bool negative = cents < 0;
    uint64_t abs =
        negative ? static_cast<uint64_t>(-(cents + 1)) + 1 : static_cast<uint64_t>(cents);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s%llu,%02llu", negative ? "-" : "",
                  static_cast<unsigned long long>(abs / 100),
                  static_cast<unsigned long long>(abs % 100));
    std::string out(buf);
    if (with_symbol) out += strings::COMMON_EURO;
    return out;
}

std::string format_balance(Cents cents) {
    if (cents < 0) {
        return std::string(strings::BOOK_DEBT) + " " + format_money(-cents);
    }
    return std::string(strings::BOOK_CREDIT) + " " + format_money(cents);
}

std::string format_clock(Timestamp ts) {
    if (ts <= 0) return "--:--";
    std::tm tm = local_tm(ts);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    return buf;
}

std::string format_date(Timestamp ts) {
    if (ts <= 0) return strings::COMMON_DASH;
    std::tm tm = local_tm(ts);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%02d.%02d.%04d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
    return buf;
}

std::string format_month(Timestamp now, int month_offset) {
    std::tm tm = local_tm(now);
    int total = (tm.tm_year + 1900) * 12 + tm.tm_mon + month_offset;
    int year = total / 12;
    int month = total % 12;
    if (month < 0) {
        month += 12;
        year -= 1;
    }
    return std::string(strings::MONTHS[month]) + " " + std::to_string(year);
}

void month_bounds(Timestamp now, int month_offset, Timestamp* from, Timestamp* to) {
    std::tm base = local_tm(now);
    std::tm start{};
    start.tm_year = base.tm_year;
    start.tm_mon = base.tm_mon + month_offset;
    start.tm_mday = 1;
    start.tm_hour = 0;
    start.tm_min = 0;
    start.tm_sec = 0;
    start.tm_isdst = -1;
    // Normalise out-of-range months (including negative offsets).
    std::time_t start_t = std::mktime(&start);
    std::tm next = start;
    next.tm_mon += 1;
    next.tm_isdst = -1;
    std::time_t next_t = std::mktime(&next);
    if (from) *from = static_cast<Timestamp>(start_t);
    if (to) *to = static_cast<Timestamp>(next_t);
}

std::string format_uptime(uint64_t seconds) {
    uint64_t days = seconds / 86400;
    uint64_t hours = (seconds % 86400) / 3600;
    uint64_t minutes = (seconds % 3600) / 60;
    if (days > 0) {
        return std::to_string(days) + " " + strings::UNIT_DAYS + " " + std::to_string(hours) + " " +
               strings::UNIT_HOURS;
    }
    if (hours > 0) {
        return std::to_string(hours) + " " + strings::UNIT_HOURS + " " + std::to_string(minutes) +
               " " + strings::UNIT_MINUTES;
    }
    if (minutes > 0) {
        return std::to_string(minutes) + " " + strings::UNIT_MINUTES;
    }
    return std::to_string(seconds) + " " + strings::UNIT_SECONDS;
}

std::string format_countdown(int seconds) {
    if (seconds < 0) seconds = 0;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", seconds / 60, seconds % 60);
    return buf;
}

std::string format_bytes(uint64_t bytes) {
    constexpr double kKiB = 1024.0;
    if (bytes >= static_cast<uint64_t>(1024) * 1024 * 1024) {
        return decimal_comma(static_cast<double>(bytes) / (kKiB * 1024 * 1024), 1) + " " +
               strings::UNIT_GIGABYTES;
    }
    if (bytes >= 1024 * 1024) {
        return decimal_comma(static_cast<double>(bytes) / (kKiB * 1024), 1) + " " +
               strings::UNIT_MEGABYTES;
    }
    return std::to_string(bytes / 1024) + " " + strings::UNIT_KILOBYTES;
}

std::string format_last_sync(Timestamp now, Timestamp last_sync) {
    if (last_sync <= 0) return strings::SYSTEM_TIME_NEVER;
    Timestamp delta = now > last_sync ? now - last_sync : 0;
    if (delta < 60) return strings::COMMON_JUST_NOW;
    return std::string(strings::COMMON_AGO) + " " + format_uptime(static_cast<uint64_t>(delta));
}

std::string format_temperature(float celsius) {
    return decimal_comma(static_cast<double>(celsius), 1) + strings::COMMON_CELSIUS;
}

std::string initials(const std::string& name) {
    std::string out;
    size_t pos = 0;
    bool first = true;
    while (pos < name.size()) {
        while (pos < name.size() && std::isspace(static_cast<unsigned char>(name[pos]))) {
            ++pos;
        }
        if (pos >= name.size()) break;
        size_t start = pos;
        while (pos < name.size() && !std::isspace(static_cast<unsigned char>(name[pos]))) {
            ++pos;
        }
        std::string word = name.substr(start, pos - start);
        if (first) {
            append_first_utf8(out, word);
            first = false;
        }
    }
    // Also take the last word's initial for multi-word names.
    size_t last_end = name.find_last_not_of(" \t\r\n");
    if (last_end != std::string::npos) {
        size_t last_start = name.find_last_of(" \t\r\n", last_end);
        last_start = last_start == std::string::npos ? 0 : last_start + 1;
        std::string last_word = name.substr(last_start, last_end - last_start + 1);
        if (last_word != name && !last_word.empty()) {
            out.clear();
            append_first_utf8(out, name);
            append_first_utf8(out, last_word);
        }
    }
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

std::string time_source_label(TimeSource source, const std::string& last_source) {
    if (last_source == "browser") return strings::SYSTEM_TIME_BROWSER;
    if (last_source == "sntp") return strings::SYSTEM_TIME_SNTP;
    if (last_source == "manual") return strings::SYSTEM_TIME_MANUAL;
    if (last_source == "restored") return strings::SYSTEM_TIME_RESTORED;
    switch (source) {
        case TimeSource::Synced:
            return strings::SYSTEM_TIME_BROWSER;
        case TimeSource::Restored:
            return strings::SYSTEM_TIME_RESTORED;
        case TimeSource::Unset:
        default:
            return strings::SYSTEM_TIME_UNSET;
    }
}

std::string rotation_label(int degrees) {
    return std::to_string(degrees) + strings::COMMON_DEGREE;
}

SdBackupStatus sd_backup_status(bool card_present, bool backup_ok, Timestamp last_success,
                                bool has_error) {
    // Mirrors sd::kNoClockSuccess: a run succeeded while the wall clock was
    // unset. The header never shows a time, so no 1970 can leak out.
    constexpr Timestamp kNoClockSuccess = 1;

    auto named = [](SdBackupState state, const char* word) {
        SdBackupStatus status;
        status.state = state;
        status.value = std::string(strings::SYSTEM_SD) + " " + word;
        return status;
    };

    if (!card_present) return named(SdBackupState::NotDetected, strings::SYSTEM_SD_NOT_DETECTED);
    if (backup_ok) {
        return named(last_success <= kNoClockSuccess ? SdBackupState::SavedWithoutClock
                                                     : SdBackupState::Saved,
                     strings::SYSTEM_SD_OK);
    }
    if (has_error) return named(SdBackupState::Error, strings::SYSTEM_SD_ERROR);
    return named(SdBackupState::Present, strings::SYSTEM_SD_DETECTED);
}

}  // namespace kaffee::ui
