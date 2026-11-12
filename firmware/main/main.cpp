#include <algorithm>
#include <memory>
#include <utility>

#include "api_controller.hpp"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kaffee/api.hpp"
#include "kaffee/board_esp.hpp"
#include "kaffee/littlefs_store.hpp"
#include "kaffee/net_manager.hpp"
#include "kaffee/nvs_settings_store.hpp"
#include "kaffee/service.hpp"
#include "kaffee/ui.hpp"
#include "kaffee/ui_board_adapter.hpp"
#include "kaffee/ui_control.hpp"
#include "kaffee/web_assets.hpp"
#include "lvgl.h"
#include "nvs_flash.h"
#include "platform_adapter.hpp"
#include "serial_console.hpp"
#include "storage_boot.hpp"

namespace {

constexpr const char* kTag = "kaffee";
constexpr const char* kMountPoint = "/littlefs";

struct BootState {
    bool nvs = false;
    bool storage = false;
    bool display = false;
    bool lvgl = false;
    bool service = false;
    bool ui = false;
    bool network = false;
    bool api = false;
};

// Boot health gate for the dual-OTA rollback window. The image is confirmed
// only after storage, service, display/LVGL, the three-page UI, the network
// manager and the API controller/server have all come up. A failed component
// leaves rollback armed.
bool boot_health_check(const BootState& state) {
    const bool healthy = state.nvs && state.storage && state.display && state.lvgl &&
                         state.service && state.ui && state.network && state.api;
    ESP_LOGI(kTag,
             "boot health: nvs=%d storage=%d display=%d lvgl=%d service=%d ui=%d network=%d "
             "api=%d => %s",
             state.nvs, state.storage, state.display, state.lvgl, state.service, state.ui,
             state.network, state.api, healthy ? "ok" : "failed");
    return healthy;
}

void log_failure(const char* what, const kaffee::Error& error) {
    ESP_LOGE(kTag, "%s failed: %s (%s)", what, error.code.c_str(), error.message.c_str());
}

// True only when every byte is erased (0xFF). Used once after a failed mount:
// a new/blank partition may be formatted, but an existing corrupt filesystem
// must not be silently wiped because it may contain the ledger.
bool partition_is_blank(const char* label) {
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, label);
    if (partition == nullptr) return false;

    static uint8_t buffer[4096];
    for (size_t offset = 0; offset < partition->size; offset += sizeof(buffer)) {
        const size_t length = std::min(static_cast<size_t>(sizeof(buffer)),
                                       static_cast<size_t>(partition->size - offset));
        if (esp_partition_read(partition, offset, buffer, length) != ESP_OK) return false;
        for (size_t i = 0; i < length; ++i) {
            if (buffer[i] != 0xFF) return false;
        }
    }
    return true;
}

bool mount_storage() {
    esp_vfs_littlefs_conf_t fs_conf = {};
    fs_conf.base_path = kMountPoint;
    fs_conf.partition_label = "storage";
    fs_conf.format_if_mount_failed = false;
    fs_conf.dont_mount = false;

    esp_err_t err = esp_vfs_littlefs_register(&fs_conf);
    const bool blank = err != ESP_OK && partition_is_blank("storage");
    const kaffee::boot::StorageAction action = kaffee::boot::storage_action(err == ESP_OK, blank);
    if (action == kaffee::boot::StorageAction::FormatBlankThenMount) {
        ESP_LOGW(kTag, "LittleFS partition is blank; formatting once during first boot");
        err = esp_littlefs_format("storage");
        if (err == ESP_OK) {
            err = esp_vfs_littlefs_register(&fs_conf);
        } else {
            ESP_LOGE(kTag, "LittleFS format of blank partition failed: %s", esp_err_to_name(err));
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(kTag,
                 "LittleFS mount failed: %s. Refusing to format existing data "
                 "(ledger preserved); OTA rollback stays armed.",
                 esp_err_to_name(err));
        return false;
    }

    size_t total = 0;
    size_t used = 0;
    if (esp_littlefs_info("storage", &total, &used) == ESP_OK) {
        ESP_LOGI(kTag, "storage mounted at %s: %u/%u bytes used", kMountPoint,
                 static_cast<unsigned>(used), static_cast<unsigned>(total));
    } else {
        ESP_LOGI(kTag, "storage mounted at %s", kMountPoint);
    }
    return true;
}

}  // namespace

extern "C" void app_main(void) {
    // Keep the embedded admin SPA linked. The API serves the same table through
    // kaffee_web_asset_lookup(); this reference also proves the table is part of
    // the image even before the HTTP server starts.
    (void)kaffee::web::assets();

    BootState state;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    state.nvs = true;

    ESP_LOGI(kTag, "Kaffeekasse booting");

    // Mount the LittleFS data partition declared in partitions.csv. A failed
    // mount must not silently wipe a corrupted ledger; only a blank partition is
    // formatted once (see storage_boot.hpp).
    if (!mount_storage()) {
        // Leave OTA rollback armed: a bad image/partition must not be confirmed.
        return;
    }
    state.storage = true;

    static kaffee::NvsSettingsStore settings_store;
    static kaffee::LittleFsEventStore event_store(kMountPoint);

    auto created_display = kaffee::EspDisplay::create();
    if (!created_display) {
        log_failure("display creation", *created_display.error);
        return;
    }
    static std::unique_ptr<kaffee::EspDisplay> display = std::move(*created_display.value);

    kaffee::Result<kaffee::Unit> result = display->init();
    if (!result) {
        log_failure("board init", *result.error);
        return;
    }
    state.display = true;

#if CONFIG_BOARD_DISPLAY_SELFTEST
    result = display->run_scanout_selftest();
    if (!result) {
        log_failure("scanout self-test", *result.error);
        return;
    }
    esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(kTag,
             "diagnostic build: colour bars left on the panel; set "
             "CONFIG_BOARD_DISPLAY_SELFTEST=n and reflash for the normal LVGL boot");
    return;
#endif

    result = display->init_lvgl();
    if (!result) {
        log_failure("LVGL init", *result.error);
        return;
    }
    state.lvgl = true;

    // NetworkManager is cheap to construct; init() happens after the service so
    // the settings provider can see generated AP credentials.
    static kaffee::NetworkManager network;
    network.set_settings_provider([]() {
        auto loaded = settings_store.load();
        return loaded ? *loaded.value : kaffee::Settings{};
    });

    static kaffee::PlatformAdapter platform(*display, network);

    auto created_service = kaffee::KaffeeService::create(event_store, settings_store, platform);
    if (!created_service) {
        log_failure("service creation", *created_service.error);
        return;
    }
    static std::unique_ptr<kaffee::KaffeeService> service = std::move(*created_service.value);
    state.service = true;
    state.storage = event_store.info().mounted;

    // Apply the persisted display settings before the UI builds its pages.
    kaffee::Settings boot_settings;
    auto loaded_settings = settings_store.load();
    if (loaded_settings) {
        boot_settings = *loaded_settings.value;
    } else {
        log_failure("settings load", *loaded_settings.error);
    }
    if (auto applied = display->apply_settings(boot_settings.display); !applied) {
        log_failure("display settings apply", *applied.error);
    }

    // Bring the network up before the UI and console stack allocations. The DNS
    // task needs a contiguous internal-RAM stack; the 12 KiB console and the
    // LVGL page objects must not fragment it first.
    if (auto initialized = network.init(); !initialized) {
        log_failure("network init", *initialized.error);
    } else {
        state.network = true;
        if (auto applied = network.apply_settings(boot_settings.network); !applied) {
            log_failure("network settings apply", *applied.error);
        }
        if (auto applied = network.apply_time_settings(boot_settings.time); !applied) {
            log_failure("time settings apply", *applied.error);
        }
    }

    // Start the recovery console after the network is up but before the UI
    // builds its page objects. Its 12 KiB internal stack must not be fragmented
    // by LVGL allocations; commands remain valid once boot completes.
    if (start_serial_console(*service, platform, *display, nullptr)) {
        ESP_LOGI(kTag, "serial console started");
    }

    // The UI is created and started under the LVGL lock; all subsequent LVGL
    // calls happen on the esp_lvgl_port task through its timers.
    static kaffee::ui::BoardDisplayPort display_port(
        *display, display->lv_display(),
        [](bool stay_awake) { display->set_stay_awake(stay_awake); });
    kaffee::ui::attach_display_port(&display_port);

    static std::unique_ptr<kaffee::ui::UiApp> ui_app;
    if (display->lock(2000)) {
        auto created_ui = kaffee::ui::UiApp::create(*service);
        if (created_ui) {
            ui_app = std::move(*created_ui.value);
            ui_app->start();
            state.ui = true;
            ESP_LOGI(kTag, "UI started on page 1");
        } else {
            log_failure("UI creation", *created_ui.error);
        }
        display->unlock();
    } else {
        ESP_LOGE(kTag, "cannot lock LVGL to start the UI");
    }

    auto created_api = kaffee::ApiServer::create(*service, platform);
    if (!created_api) {
        log_failure("API creation", *created_api.error);
    }
    static std::unique_ptr<kaffee::ApiServer> api_server =
        created_api ? std::move(*created_api.value) : nullptr;
    static kaffee::ApiController* api_controller = nullptr;
    bool api_controller_started = false;
    if (api_server) {
        static kaffee::ApiController controller(*api_server, *service, platform);
        if (!controller.start()) {
            ESP_LOGE(kTag, "API controller task creation failed");
        } else {
            api_controller = &controller;
            api_controller_started = true;
        }
    }
    state.api = api_server != nullptr && api_controller_started;

    if (state.network) {
        // Set the listener after init so the network timer task delivers the
        // initial availability notification to the API worker.
        if (api_controller != nullptr) network.set_listener(api_controller);

        // Provisioning convenience: a device with no users starts the AP for
        // the configured window. Otherwise the AP stays off until page 3 or the
        // admin API asks for it.
        if (service->users().empty()) {
            ESP_LOGI(
                kTag, "no users yet, starting provisioning AP for %ds",
                boot_settings.network.ap_window_s > 0 ? boot_settings.network.ap_window_s : 900);
            if (auto started = service->start_ap(); !started) {
                log_failure("provisioning AP start", *started.error);
            }
        }
    }

    // Wait briefly for the API controller to react to the AP/STA notification.
    // If an interface is up, a missing HTTP server is a health failure; the
    // controller also retries on its own every few seconds.
    if (state.network) {
        bool interface_expected = false;
        for (int attempt = 0; attempt < 30; ++attempt) {
            const kaffee::NetworkStatus status = network.status();
            interface_expected = status.ap_active || status.sta_connected || status.ap_starting;
            if (interface_expected && api_server != nullptr && api_server->running()) break;
            if (!interface_expected && attempt >= 5) break;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        const kaffee::NetworkStatus status = network.status();
        interface_expected = status.ap_active || status.sta_connected;
        if (interface_expected && api_server != nullptr && !api_server->running()) {
            ESP_LOGE(kTag, "interface is up but the HTTP server did not start");
            state.api = false;
        }
    }

    // The serial console is the recovery/HIL interface. Start it after the
    // network so `status`/`ap` reflect the real interface state. It never calls
    // LVGL; all commands go through the service/platform facades.
    kaffee::ui::UiControl* ui_control = kaffee::ui::control_for(ui_app.get());
    kaffee::set_serial_console_ui(ui_control);

    const kaffee::BoardStats stats = display->stats();
    ESP_LOGI(kTag, "uptime %llu s, heap %u B (min %u B), PSRAM %u/%u B, touch %s",
             static_cast<unsigned long long>(stats.uptime_s), stats.free_heap, stats.min_free_heap,
             stats.free_psram, stats.total_psram, stats.touch_present ? "ready" : "missing");
    if (stats.temperature_valid) {
        ESP_LOGI(kTag, "chip temperature %.1f C", stats.chip_temperature_c);
    } else {
        ESP_LOGI(kTag, "chip temperature unavailable");
    }

    if (boot_health_check(state)) {
        ESP_LOGI(kTag, "health check passed, confirming OTA image");
        esp_ota_mark_app_valid_cancel_rollback();
    } else {
        ESP_LOGE(kTag, "health check failed, leaving OTA rollback armed");
    }
    ESP_LOGI(kTag, "boot complete");
}
