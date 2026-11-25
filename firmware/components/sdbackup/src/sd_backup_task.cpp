#include "kaffee/sd_backup_esp.hpp"

#ifdef ESP_PLATFORM

#include <cstdint>
#include <memory>
#include <string>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kaffee/backup.hpp"
#include "kaffee/service.hpp"

namespace kaffee::sd {
namespace {

constexpr const char* kTag = "kaffee.sd";
// Well below the UI/HTTP/network tasks; a slow card must never compete with
// the kiosk. The boot health check has already confirmed the OTA image before
// this task exists.
constexpr UBaseType_t kTaskPriority = 2;
constexpr uint32_t kTaskStackBytes = 5120;
constexpr uint32_t kPollIntervalMs = 45000;
// There is no card-detect pin. When no run is due, a mount/unmount pair every
// five minutes refreshes presence without writing anything. A due run's own
// mount counts as the probe.
constexpr uint32_t kProbeIntervalMs = 5 * 60 * 1000;
static_assert(kProbeIntervalMs == static_cast<uint32_t>(kProbeIntervalSeconds) * 1000,
              "probe interval must match the pure planner constant");
// A missing card is normal. Log it once per probe interval, not every 45 s poll.
constexpr int64_t kMountLogIntervalUs = static_cast<int64_t>(kProbeIntervalMs) * 1000;

struct TaskContext {
    KaffeeService* service = nullptr;
    std::unique_ptr<IVolume> volume;
    std::unique_ptr<IStateStore> state_store;
    SdStatusMonitor* status = nullptr;
};

bool starts_with(const std::string& text, const char* prefix) {
    return text.rfind(prefix, 0) == 0;
}

void log_run(const SdPlan& plan, const SdRunReport& report) {
    ESP_LOGI(kTag, "backup done: db=%d previous_csv=%d current_csv=%d", report.db_written,
             report.previous_csv_written, report.current_csv_written);
    (void)plan;
}

Timestamp monotonic_seconds() {
    return static_cast<Timestamp>(esp_timer_get_time() / 1000000);
}

void log_missing_card(const std::string& message, int64_t& last_mount_log_us) {
    const int64_t now_us = esp_timer_get_time();
    if (now_us - last_mount_log_us < kMountLogIntervalUs) return;
    last_mount_log_us = now_us;
    ESP_LOGW(kTag, "no compatible backup card: %s", message.c_str());
}

// Mount/unmount without writing. Presence only: the monitor must keep the
// result of the last real backup run.
void probe_presence(TaskContext& context, int64_t& last_mount_log_us) {
    auto mounted = context.volume->mount();
    if (!mounted) {
        context.status->record_probe_failure();
        log_missing_card(mounted.error->message, last_mount_log_us);
        return;
    }
    context.status->record_probe_success();
    auto unmounted = context.volume->unmount();
    if (!unmounted) {
        ESP_LOGW(kTag, "card probe unmount failed: %s", unmounted.error->message.c_str());
    }
}

void run_once(TaskContext& context, Timestamp mono_now, Timestamp& last_probe,
              int64_t& last_mount_log_us) {
    const TimeInfo time = context.service->time_info();

    SdRunState state;
    auto loaded = context.state_store->load_last_success();
    if (!loaded) {
        ESP_LOGW(kTag, "cannot load backup state: %s", loaded.error->message.c_str());
    } else {
        state = *loaded.value;
        const Timestamp latest = state.last_db_success > state.last_csv_success
                                     ? state.last_db_success
                                     : state.last_csv_success;
        context.status->set_last_success(latest);
    }

    const SdPlan plan = plan_for(time.now, state, time.source);
    if (!plan.due()) {
        // Done for today: refresh presence occasionally, never write.
        if (probe_due(mono_now, last_probe)) {
            last_probe = mono_now;
            probe_presence(context, last_mount_log_us);
        }
        return;
    }

    // A due run's mount is the probe, so it shares the same interval. A
    // failing run therefore retries every probe interval, never every 45 s.
    if (!probe_due(mono_now, last_probe)) return;

    std::shared_ptr<const Ledger> ledger;
    if (plan.write_previous_csv || plan.write_current_csv || plan.write_db) {
        ledger = context.service->ledger_snapshot();
    }

    BackupData data;
    data.ledger = ledger;
    if (plan.write_db) {
        data.db_json = backup_to_json(*ledger, context.service->settings(), time.now);
    }

    // The run's mount is the probe for this interval, success or failure.
    last_probe = mono_now;
    auto ran = execute_plan(time.now, plan, *context.volume, *context.state_store, state, data);
    if (ran) {
        context.status->record_success(time.now);
        log_run(plan, *ran.value);
        return;
    }

    const std::string& message = ran.error->message;
    const bool mount_problem =
        starts_with(message, "mount") || starts_with(message, "spi_bus_initialize");
    context.status->record_error(message, !mount_problem);
    if (mount_problem) {
        log_missing_card(message, last_mount_log_us);
    } else {
        ESP_LOGW(kTag, "backup failed: %s", message.c_str());
    }
}

void task_entry(void* arg) {
    auto* context = static_cast<TaskContext*>(arg);
    Timestamp last_probe = 0;
    int64_t last_mount_log_us = 0;
    for (;;) {
        run_once(*context, monotonic_seconds(), last_probe, last_mount_log_us);
        vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
    }
}

}  // namespace

bool start_sd_backup_task(KaffeeService& service, SdStatusMonitor& status) {
    auto* context = new TaskContext;
    context->service = &service;
    context->volume = make_sd_volume();
    context->state_store = std::make_unique<NvsSdStateStore>();
    context->status = &status;

    if (xTaskCreate(&task_entry, "sd_backup", kTaskStackBytes, context, kTaskPriority, nullptr) !=
        pdPASS) {
        delete context;
        ESP_LOGE(kTag, "could not create the microSD backup task");
        return false;
    }
    ESP_LOGI(kTag, "microSD backup task started (poll every %u ms)",
             static_cast<unsigned>(kPollIntervalMs));
    return true;
}

}  // namespace kaffee::sd

#endif  // ESP_PLATFORM
