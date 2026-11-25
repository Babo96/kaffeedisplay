#include "kaffee/sd_backup_esp.hpp"

#ifdef ESP_PLATFORM

#include <cstdint>
#include <string>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"

namespace kaffee::sd {
namespace {

constexpr const char* kTag = "kaffee.sd";
constexpr const char* kNamespace = "kaffee_sd";
constexpr const char* kLastDbKey = "last_db";
constexpr const char* kLastCsvKey = "last_csv";

Result<Unit> nvs_fail(const std::string& what, esp_err_t err) {
    return Result<Unit>::fail("sd", what + ": " + esp_err_to_name(err));
}

}  // namespace

Result<SdRunState> NvsSdStateStore::load_last_success() {
    SdRunState state;
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return Result<SdRunState>::ok(state);
    if (err != ESP_OK) return Result<SdRunState>::fail("sd", esp_err_to_name(err));

    uint64_t value = 0;
    err = nvs_get_u64(handle, kLastDbKey, &value);
    if (err == ESP_OK) {
        state.last_db_success = static_cast<Timestamp>(value);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return Result<SdRunState>::fail("sd", esp_err_to_name(err));
    }

    value = 0;
    err = nvs_get_u64(handle, kLastCsvKey, &value);
    if (err == ESP_OK) {
        state.last_csv_success = static_cast<Timestamp>(value);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return Result<SdRunState>::fail("sd", esp_err_to_name(err));
    }

    nvs_close(handle);
    return Result<SdRunState>::ok(state);
}

Result<Unit> NvsSdStateStore::save_last_success(const SdRunState& state) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) return nvs_fail("open namespace", err);

    err = nvs_set_u64(handle, kLastDbKey, static_cast<uint64_t>(state.last_db_success));
    if (err == ESP_OK) {
        err = nvs_set_u64(handle, kLastCsvKey, static_cast<uint64_t>(state.last_csv_success));
    }
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "could not persist backup state: %s", esp_err_to_name(err));
        return nvs_fail("save state", err);
    }
    return Result<Unit>::ok(Unit{});
}

}  // namespace kaffee::sd

#endif  // ESP_PLATFORM
