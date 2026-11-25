#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "kaffee/domain.hpp"
#include "kaffee/reports.hpp"

// Backup-only microSD policy and orchestration.
//
// The card is never live storage: a run mounts the volume, writes at most one
// JSON database backup and one or two monthly CSV files, then unmounts. Nothing
// in this header depends on ESP-IDF or FatFs, so the plan truth table and the
// write ordering are covered by host tests with in-memory fakes.
//
// The daily run happens at 00:08 Europe/Berlin. The plan never writes a CSV
// while the clock is not `TimeSource::Synced`; the database backup does not
// depend on the clock. A failed write aborts the run and leaves the persisted
// success marker untouched, so the next poll retries.
namespace kaffee::sd {

inline constexpr const char* kDbFileName = "kaffeekasse-db.json";

// Success markers persisted in NVS (namespace `kaffee_sd`). Keeping the
// database and CSV markers separate makes a DB-only run (untrusted clock)
// repeatable when the clock later becomes trustworthy.
struct SdRunState {
    Timestamp last_db_success = 0;
    Timestamp last_csv_success = 0;
};

// Stored instead of `now` when a clock-independent DB run succeeds while the
// wall clock is unset. It is never shown as a real timestamp.
inline constexpr Timestamp kNoClockSuccess = 1;

// Presence probe cadence when no backup run is due. The card has no detect
// pin, so the only way to refresh presence is a mount/unmount pair.
inline constexpr Timestamp kProbeIntervalSeconds = 5 * 60;

// True when an idle probe should run. `now` and `last_probe` are monotonic
// seconds, not wall time, so a browser clock jump cannot trigger extra mounts.
bool probe_due(Timestamp now, Timestamp last_probe);

// Thread-safe snapshot consumed by `PlatformAdapter::stats()`. The UI/LVGL
// task and the serial console both read it, while the backup task writes it.
// Defaults mean "no card detected / no successful run yet".
struct SdStatus {
    bool card_present = false;
    bool backup_ok = false;
    Timestamp last_success = 0;
    std::string last_error;
};

class SdStatusMonitor {
  public:
    SdStatus snapshot() const;
    // Only advances; ignores 0 and the unset-clock sentinel.
    void set_last_success(Timestamp ts);
    void record_success(Timestamp now);
    void record_error(std::string message, bool card_present);
    // Idle presence probes update only `card_present`; `backup_ok`,
    // `last_success` and `last_error` keep describing the last real backup.
    void record_probe_success();
    void record_probe_failure();

  private:
    mutable std::mutex mutex_;
    SdStatus status_;
};

struct SdPlan {
    bool write_db = false;
    bool write_previous_csv = false;
    bool write_current_csv = false;

    bool due() const {
        return write_db || write_previous_csv || write_current_csv;
    }
};

// Most recent scheduled run (00:08 local) at or before `now`.
Timestamp scheduled_run_at(Timestamp now);

// Current calendar month in Europe/Berlin and the month before it.
reports::Period current_month(Timestamp now);
reports::Period previous_month(Timestamp now);

// File name of the DB backup and of the current/previous month's CSV.
std::string db_path();
std::string csv_path(Timestamp now, bool previous_month);

// Decides what a run must write. `card_present` is false when the caller
// already knows no compatible card is mounted; the ESP task only calls this
// after a successful mount instead, because mounting is the detection.
SdPlan plan_for(Timestamp now, const SdRunState& state, TimeSource source,
                bool card_present = true);

class IVolume {
  public:
    virtual ~IVolume() = default;

    virtual Result<Unit> mount() = 0;
    // Must be safe to call after a failed mount and after a write failure.
    virtual Result<Unit> unmount() = 0;
    // Replaces `name` with `data`.
    virtual Result<Unit> write_file(const std::string& name, std::string_view data) = 0;

    // Streams a file through the report sink without buffering the document.
    // The producer returns the first error; the volume must still close (and
    // usually remove) the partial file.
    using StreamProducer = std::function<Result<Unit>(const reports::Sink&)>;
    virtual Result<Unit> write_stream(const std::string& name, const StreamProducer& produce) = 0;
};

class IStateStore {
  public:
    virtual ~IStateStore() = default;
    virtual Result<SdRunState> load_last_success() = 0;
    virtual Result<Unit> save_last_success(const SdRunState& state) = 0;
};

// Everything a due run needs. The caller builds `db_json` with
// `backup_to_json()` outside the service lock and only supplies a ledger
// snapshot when a CSV is due.
struct BackupData {
    std::string db_json;
    std::shared_ptr<const Ledger> ledger;
};

struct SdRunReport {
    bool mounted = false;
    bool db_written = false;
    bool previous_csv_written = false;
    bool current_csv_written = false;
};

// Executes a plan. Order: previous month's CSV, current month's CSV, database
// backup. Any failure aborts the remaining writes, attempts an unmount and
// returns the error without advancing that step's success marker.
Result<SdRunReport> execute_plan(Timestamp now, const SdPlan& plan, IVolume& volume,
                                 IStateStore& state_store, const SdRunState& initial_state,
                                 const BackupData& data);

}  // namespace kaffee::sd
