#pragma once

// All user-visible device strings live here. German only; identifiers and
// comments stay English. Keep the list free of printf placeholders so callers
// compose values with the helpers in format.hpp.

namespace kaffee::ui::strings {

inline constexpr const char* APP_TITLE = "Kaffeekasse";
inline constexpr const char* APP_SUBTITLE = "Kaffee, Tee & mehr";

// Page 1 — Buchen
inline constexpr const char* BOOK_TITLE = "Buchen";
inline constexpr const char* BOOK_QUESTION = "Wer trinkt?";
inline constexpr const char* BOOK_PICK_DRINK = "Getränk wählen";
inline constexpr const char* BOOK_GUEST = "Gast";
inline constexpr const char* BOOK_GUEST_HINT = "Bar";
inline constexpr const char* BOOK_CHANGE_USER = "Wechseln";
inline constexpr const char* BOOK_EMPTY = "Keine Benutzer – Admin: Seite 3 → WLAN + Passwort";
inline constexpr const char* BOOK_NO_PRODUCTS = "Keine Getränke hinterlegt";
inline constexpr const char* BOOK_FAILED = "Buchung fehlgeschlagen";
inline constexpr const char* BOOK_GUEST_DISABLED = "Gastverkäufe sind deaktiviert";
inline constexpr const char* BOOK_UNDO = "Rückgängig";
inline constexpr const char* BOOK_UNDONE = "Buchung storniert";
inline constexpr const char* BOOK_UNDO_FAILED = "Stornierung fehlgeschlagen";
inline constexpr const char* BOOK_TOAST_SEPARATOR = " · ";
inline constexpr const char* BOOK_CREDIT = "Guthaben";
inline constexpr const char* BOOK_DEBT = "Offen";
inline constexpr const char* BOOK_BALANCE = "Saldo";

// Page 2 — Statistik
inline constexpr const char* STATS_TITLE = "Statistik";
inline constexpr const char* STATS_EMPTY = "Keine Buchungen in diesem Monat";
inline constexpr const char* STATS_GUEST = "Gastverkäufe";
inline constexpr const char* STATS_TIME_WARNING =
    "Uhrzeit nicht synchron – Monat evtl. unvollständig";

// Page 3 — System
inline constexpr const char* SYSTEM_TITLE = "System";
inline constexpr const char* SYSTEM_SETUP_TITLE = "Erste Einrichtung";
inline constexpr const char* SYSTEM_SETUP_PASSWORD = "Admin-Passwort";
inline constexpr const char* SYSTEM_SETUP_SSID = "WLAN-Netzwerk";
inline constexpr const char* SYSTEM_SETUP_URL = "Admin-Adresse";
inline constexpr const char* SYSTEM_SETUP_HINT =
    "Passwort in der Admin-Weboberfläche unter Einstellungen ändern.";
inline constexpr const char* SYSTEM_SETUP_AP_HINT =
    "WLAN starten, dann die Admin-Adresse im Browser öffnen.";
inline constexpr const char* SYSTEM_WLAN = "WLAN";
inline constexpr const char* SYSTEM_WLAN_ACTIVE = "Aktiv";
inline constexpr const char* SYSTEM_WLAN_INACTIVE = "Aus";
inline constexpr const char* SYSTEM_WLAN_STARTING = "Startet …";
inline constexpr const char* SYSTEM_WLAN_HELP =
    "Hotspot nur bei Bedarf. Zum Starten 3 Sekunden halten.";
inline constexpr const char* SYSTEM_WLAN_START = "WLAN starten";
inline constexpr const char* SYSTEM_WLAN_HOLD = "3 s halten …";
inline constexpr const char* SYSTEM_WLAN_EXTEND = "Verlängern";
inline constexpr const char* SYSTEM_WLAN_STOP = "Stoppen";
inline constexpr const char* SYSTEM_WLAN_SSID = "Netzwerk";
inline constexpr const char* SYSTEM_WLAN_PASSWORD = "Passwort";
inline constexpr const char* SYSTEM_WLAN_ADDRESS = "Adresse";
inline constexpr const char* SYSTEM_WLAN_REMAINING = "Noch";
inline constexpr const char* SYSTEM_WLAN_JOIN_QR = "WLAN beitreten";
inline constexpr const char* SYSTEM_WLAN_URL_QR = "Admin öffnen";
inline constexpr const char* SYSTEM_WLAN_LAN_QR = "Admin im LAN öffnen";
inline constexpr const char* SYSTEM_WLAN_LAN = "LAN";
inline constexpr const char* SYSTEM_WLAN_AP_STARTED = "WLAN gestartet";
inline constexpr const char* SYSTEM_WLAN_STOPPED = "WLAN gestoppt";
inline constexpr const char* SYSTEM_WLAN_FAILED = "WLAN-Aktion fehlgeschlagen";
inline constexpr const char* SYSTEM_TIME = "Zeit";
inline constexpr const char* SYSTEM_TIME_SOURCE = "Quelle";
inline constexpr const char* SYSTEM_TIME_LAST_SYNC = "Letzte Sync";
inline constexpr const char* SYSTEM_TIME_QUALITY = "Qualität";
inline constexpr const char* SYSTEM_TIME_TRUSTED = "Synchron";
inline constexpr const char* SYSTEM_TIME_UNTRUSTED = "Nicht synchron";
inline constexpr const char* SYSTEM_TIME_NEVER = "Noch nie";
inline constexpr const char* SYSTEM_TIME_WARNING =
    "Zeit nicht synchron – Buchungen erhalten eine ungeprüfte Zeit.";
inline constexpr const char* SYSTEM_TIME_SYNC = "Zeit synchronisieren";
inline constexpr const char* SYSTEM_TIME_SYNC_HINT =
    "WLAN geöffnet – bitte Admin-Seite im Browser öffnen.";
inline constexpr const char* SYSTEM_TIME_SYNC_ALREADY =
    "Zeit wird beim Login in der Admin-Seite gesetzt.";
inline constexpr const char* SYSTEM_TIME_UNKNOWN = "Unbekannt";
inline constexpr const char* SYSTEM_TIME_MANUAL = "Manuell";
inline constexpr const char* SYSTEM_TIME_BROWSER = "Browser";
inline constexpr const char* SYSTEM_TIME_SNTP = "SNTP";
inline constexpr const char* SYSTEM_TIME_RESTORED = "Wiederhergestellt";
inline constexpr const char* SYSTEM_TIME_UNSET = "Nicht gesetzt";
inline constexpr const char* SYSTEM_DEVICE = "Gerät";
inline constexpr const char* SYSTEM_TEMPERATURE = "Chip-Temperatur";
inline constexpr const char* SYSTEM_UPTIME = "Laufzeit";
inline constexpr const char* SYSTEM_HEAP = "Freier Heap";
inline constexpr const char* SYSTEM_PSRAM = "Freier PSRAM";
inline constexpr const char* SYSTEM_STORAGE = "Speicher";
inline constexpr const char* SYSTEM_FIRMWARE = "Firmware";
inline constexpr const char* SYSTEM_MAC = "MAC";
inline constexpr const char* SYSTEM_IP = "IP / RSSI";
inline constexpr const char* SYSTEM_DRINKS_TODAY = "Getränke heute";
inline constexpr const char* SYSTEM_DISPLAY = "Anzeige";
inline constexpr const char* SYSTEM_DISPLAY_READONLY =
    "Änderungen erfolgen in der Admin-Weboberfläche.";
inline constexpr const char* SYSTEM_ROTATION = "Rotation";
inline constexpr const char* SYSTEM_BRIGHTNESS = "Helligkeit";
inline constexpr const char* SYSTEM_DIM_TIMEOUT = "Dimmen nach";
inline constexpr const char* SYSTEM_OFF_TIMEOUT = "Bildschirm aus nach";
inline constexpr const char* SYSTEM_NEVER = "Nie";
inline constexpr const char* SYSTEM_NONE = "–";

// Power-save illustration
inline constexpr const char* STANDBY_TITLE = "Kaffeepause";
inline constexpr const char* STANDBY_TOUCH_HINT = "Zum Aufwecken berühren";

// Shared
inline constexpr const char* COMMON_ON = "Ein";
inline constexpr const char* COMMON_OFF = "Aus";
inline constexpr const char* COMMON_DEGREE = "°";
inline constexpr const char* COMMON_CELSIUS = " °C";
inline constexpr const char* COMMON_PERCENT = " %";
inline constexpr const char* COMMON_EURO = " €";
inline constexpr const char* COMMON_TIMES = "×";
inline constexpr const char* COMMON_SEPARATOR = " · ";
inline constexpr const char* COMMON_DASH = "–";
inline constexpr const char* COMMON_AGO = "vor";
inline constexpr const char* COMMON_JUST_NOW = "gerade eben";
inline constexpr const char* UNIT_DAYS = "T";
inline constexpr const char* UNIT_HOURS = "Std.";
inline constexpr const char* UNIT_MINUTES = "Min.";
inline constexpr const char* UNIT_SECONDS = "s";
inline constexpr const char* UNIT_KILOBYTES = "kB";
inline constexpr const char* UNIT_MEGABYTES = "MB";
inline constexpr const char* UNIT_GIGABYTES = "GB";

// Test pattern
inline constexpr const char* TEST_TITLE = "Anzeige-Test";
inline constexpr const char* TEST_HINT = "Rotation prüfen, dann tippen";
inline constexpr const char* TEST_ROTATION = "Rotation";
inline constexpr const char* TEST_TOUCH = "Touch";

// German month names, January first.
inline constexpr const char* MONTHS[] = {"Januar",    "Februar", "März",     "April",
                                         "Mai",       "Juni",    "Juli",     "August",
                                         "September", "Oktober", "November", "Dezember"};

}  // namespace kaffee::ui::strings
