#pragma once

#include <memory>

#include "kaffee/sd_backup.hpp"

namespace kaffee {
class KaffeeService;
}

namespace kaffee::sd {

// NVS-backed success markers in the private `kaffee_sd` namespace.
class NvsSdStateStore final : public IStateStore {
  public:
    Result<SdRunState> load_last_success() override;
    Result<Unit> save_last_success(const SdRunState& state) override;
};

// FatFs volume on SPI2. Call once per task, not per poll; the handle keeps no
// mounted state between runs.
std::unique_ptr<IVolume> make_sd_volume();

// Starts the low-priority backup task. Call after the boot health check has
// confirmed the OTA image, so a slow or missing card cannot delay boot. The
// monitor is owned by main and also read by PlatformAdapter::stats().
bool start_sd_backup_task(KaffeeService& service, SdStatusMonitor& status);

}  // namespace kaffee::sd
