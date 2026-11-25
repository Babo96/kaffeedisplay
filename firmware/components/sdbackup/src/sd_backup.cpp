#include "kaffee/sd_backup.hpp"

#include <string>

namespace kaffee::sd {
namespace {

constexpr Timestamp kSecondsPerDay = 86400;

// 00:08 local. DST switches at 02:00-03:00, so the wall clock at this time is
// never ambiguous or inexistent.
reports::Civil midnight_plus_eight(const reports::Civil& day) {
    reports::Civil civil = day;
    civil.hour = 0;
    civil.minute = 8;
    civil.second = 0;
    return civil;
}

}  // namespace

Timestamp scheduled_run_at(Timestamp now) {
    if (now <= 0) return 0;

    Timestamp today = reports::from_berlin(midnight_plus_eight(reports::to_berlin(now)));
    if (now >= today) return today;

    // Before 00:08 the last scheduled run was yesterday's. Shifting by one day
    // is safe because the DST transitions happen at 02:00, not near 00:08.
    if (now < kSecondsPerDay) return 0;
    return reports::from_berlin(midnight_plus_eight(reports::to_berlin(now - kSecondsPerDay)));
}

reports::Period current_month(Timestamp now) {
    return reports::month(now);
}

reports::Period previous_month(Timestamp now) {
    return reports::month(reports::month(now).from - 1);
}

std::string db_path() {
    return kDbFileName;
}

std::string csv_path(Timestamp now, bool previous) {
    const reports::Period period = previous ? previous_month(now) : current_month(now);
    return reports::report_filename(reports::ReportKind::Transactions, period);
}

SdPlan plan_for(Timestamp now, const SdRunState& state, TimeSource source, bool card_present) {
    SdPlan plan;
    if (!card_present) return plan;

    // A completely unset clock (now == 0) has no schedule. The DB backup is
    // clock-independent, so write it once; CSV files are never written for
    // 1970. The sentinel 1 stored by execute_plan keeps it from repeating.
    if (now <= 0) {
        plan.write_db = state.last_db_success == 0;
        return plan;
    }

    const Timestamp scheduled = scheduled_run_at(now);
    plan.write_db = state.last_db_success < scheduled;

    const bool trusted = source == TimeSource::Synced;
    if (!trusted) return plan;

    const Timestamp month_start = current_month(now).from;
    // Finalise the previous month when the last CSV success predates the
    // current month. On the 1st at 00:08 this writes last month first, then
    // the new month.
    plan.write_previous_csv = state.last_csv_success < month_start;
    plan.write_current_csv = state.last_csv_success < scheduled;
    return plan;
}

Result<SdRunReport> execute_plan(Timestamp now, const SdPlan& plan, IVolume& volume,
                                 IStateStore& state_store, const SdRunState& initial_state,
                                 const BackupData& data) {
    SdRunReport report;
    if (!plan.due()) return Result<SdRunReport>::ok(report);

    auto mounted = volume.mount();
    if (!mounted) return Result<SdRunReport>::fail(mounted.error->code, mounted.error->message);
    report.mounted = true;

    // Always attempt the unmount, also on every error path below.
    struct UnmountGuard {
        IVolume& volume;
        ~UnmountGuard() {
            volume.unmount();
        }
    } guard{volume};

    SdRunState state = initial_state;
    bool csv_any = false;

    auto write_csv = [&](const reports::Period& period, const std::string& name) -> Result<Unit> {
        if (!data.ledger) {
            return Result<Unit>::fail("sd", "missing ledger snapshot for CSV");
        }
        IVolume::StreamProducer producer = [&](const reports::Sink& sink) {
            return reports::write_csv(*data.ledger, reports::ReportKind::Transactions, period,
                                      reports::CsvFormat::Excel, sink);
        };
        return volume.write_stream(name, producer);
    };

    if (plan.write_previous_csv) {
        auto written = write_csv(previous_month(now), csv_path(now, true));
        if (!written) return Result<SdRunReport>::fail(written.error->code, written.error->message);
        report.previous_csv_written = true;
        csv_any = true;
    }

    if (plan.write_current_csv) {
        auto written = write_csv(current_month(now), csv_path(now, false));
        if (!written) return Result<SdRunReport>::fail(written.error->code, written.error->message);
        report.current_csv_written = true;
        csv_any = true;
    }

    // Persist the CSV marker only when every CSV step of the plan succeeded.
    // A partial run (previous month written, current month failed) is retried
    // from scratch on the next poll and overwrites the same files.
    if (csv_any) {
        state.last_csv_success = now <= 0 ? 1 : now;
        auto saved = state_store.save_last_success(state);
        if (!saved) return Result<SdRunReport>::fail(saved.error->code, saved.error->message);
    }

    if (plan.write_db) {
        auto written = volume.write_file(db_path(), data.db_json);
        if (!written) return Result<SdRunReport>::fail(written.error->code, written.error->message);
        report.db_written = true;
        state.last_db_success = now <= 0 ? 1 : now;
        auto saved = state_store.save_last_success(state);
        if (!saved) return Result<SdRunReport>::fail(saved.error->code, saved.error->message);
    }

    return Result<SdRunReport>::ok(report);
}

}  // namespace kaffee::sd
