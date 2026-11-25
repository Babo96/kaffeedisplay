#include <doctest.h>

#include <string>

#include "format.hpp"
#include "strings.hpp"

using namespace kaffee;

TEST_CASE("money formatting is German and uses cents") {
    CHECK(ui::format_money(0) == "0,00 €");
    CHECK(ui::format_money(50) == "0,50 €");
    CHECK(ui::format_money(350) == "3,50 €");
    CHECK(ui::format_money(1234567) == "12345,67 €");
    CHECK(ui::format_money(-1250) == "-12,50 €");
    CHECK(ui::format_money(50, false) == "0,50");
}

TEST_CASE("balance labels credit and debt") {
    CHECK(ui::format_balance(350) == "Guthaben 3,50 €");
    CHECK(ui::format_balance(-1250) == "Offen 12,50 €");
}

TEST_CASE("initials handle one and two word names") {
    CHECK(ui::initials("Anna") == "A");
    CHECK(ui::initials("Anna Maria") == "AM");
    CHECK(ui::initials("Jürgen") == "J");
}

TEST_CASE("durations and byte sizes are compact") {
    CHECK(ui::format_uptime(274860) == "3 T 4 Std.");
    CHECK(ui::format_uptime(3660) == "1 Std. 1 Min.");
    CHECK(ui::format_uptime(120) == "2 Min.");
    CHECK(ui::format_bytes(148 * 1024) == "148 kB");
    CHECK(ui::format_bytes(7 * 1024 * 1024) == "7,0 MB");
}

TEST_CASE("first-setup strings are German and guide the admin") {
    const std::string empty = ui::strings::BOOK_EMPTY;
    CHECK(empty.find("Keine Benutzer") != std::string::npos);
    CHECK(empty.find("Seite 3") != std::string::npos);
    CHECK(empty.find("WLAN") != std::string::npos);
    CHECK(empty.find("Passwort") != std::string::npos);
    CHECK(std::string(ui::strings::SYSTEM_SETUP_TITLE) == "Erste Einrichtung");
    CHECK(std::string(ui::strings::SYSTEM_SETUP_PASSWORD) == "Admin-Passwort");
    CHECK(std::string(ui::strings::SYSTEM_SETUP_HINT).find("Einstellungen") != std::string::npos);
}

TEST_CASE("month navigation produces stable bounds") {
    const Timestamp now = 1'794'386'460;  // 2026-11-11 09:41 Europe/Berlin
    CHECK(ui::format_month(now, 0) == "November 2026");
    CHECK(ui::format_month(now, -1) == "Oktober 2026");

    Timestamp from = 0;
    Timestamp to = 0;
    ui::month_bounds(now, 0, &from, &to);
    CHECK(from > 0);
    CHECK(to > from);
    CHECK(to - from >= 28 * 86400);
    CHECK(to - from <= 31 * 86400);
}

TEST_CASE("microSD backup status maps to one short German header state") {
    const Timestamp saved = 1'794'386'460;  // 2026-11-11 09:41 Europe/Berlin

    // No card or mount failure: muted word, no time.
    const ui::SdBackupStatus missing = ui::sd_backup_status(false, false, 0, false);
    CHECK(missing.state == ui::SdBackupState::NotDetected);
    CHECK(missing.value == "SD fehlt");

    // A probe that lost the card wins over an older success or error.
    CHECK(ui::sd_backup_status(false, true, saved, false).value == "SD fehlt");

    // Card seen but nothing has succeeded yet.
    const ui::SdBackupStatus detected = ui::sd_backup_status(true, false, 0, false);
    CHECK(detected.state == ui::SdBackupState::Present);
    CHECK(detected.value == "SD erkannt");

    // Failed write: short German state, never the raw English error text.
    const ui::SdBackupStatus failed =
        ui::sd_backup_status(true, false, saved, true /* has_error */);
    CHECK(failed.state == ui::SdBackupState::Error);
    CHECK(failed.value == "SD Fehler");

    // A successful run is just "ok": no timestamp, no second clock in the header.
    const ui::SdBackupStatus done = ui::sd_backup_status(true, true, saved, false);
    CHECK(done.state == ui::SdBackupState::Saved);
    CHECK(done.value == "SD ok");
    CHECK(done.value.find(':') == std::string::npos);

    // A stale error string next to a successful run still shows the good run.
    CHECK(ui::sd_backup_status(true, true, saved, true).value == "SD ok");

    // Unset clock: still "SD ok", never 1970 and never a time.
    const ui::SdBackupStatus no_clock = ui::sd_backup_status(true, true, 1, false);
    CHECK(no_clock.state == ui::SdBackupState::SavedWithoutClock);
    CHECK(no_clock.value == "SD ok");
    CHECK(no_clock.value.find("1970") == std::string::npos);
    CHECK(ui::sd_backup_status(true, true, 0, false).value == "SD ok");
}
