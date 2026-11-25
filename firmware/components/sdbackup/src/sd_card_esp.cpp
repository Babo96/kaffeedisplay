#include "kaffee/sd_backup_esp.hpp"

#ifdef ESP_PLATFORM

#include <cerrno>
#include <cstdio>
#include <string>
#include <utility>

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "unistd.h"

namespace kaffee::sd {
namespace {

constexpr const char* kTag = "kaffee.sd";
constexpr const char* kMountPoint = "/sdcard";

// Guition ESP32-4848S040 TF slot, SPI mode. SCK/MOSI are shared with the
// ST7701 3-wire init bus, which is only used during panel init.
constexpr gpio_num_t kPinSck = GPIO_NUM_48;
constexpr gpio_num_t kPinMosi = GPIO_NUM_47;
constexpr gpio_num_t kPinMiso = GPIO_NUM_41;
constexpr gpio_num_t kPinCs = GPIO_NUM_42;

std::string full_path(const std::string& name) {
    if (!name.empty() && name.front() == '/') return std::string(kMountPoint) + name;
    return std::string(kMountPoint) + "/" + name;
}

// Owns one output file. `finish()` must be called; the destructor only closes
// a file that was never finished (for example after an early return).
class FileWriter {
  public:
    explicit FileWriter(std::string path) : path_(std::move(path)) {
        file_ = std::fopen(path_.c_str(), "wb");
    }
    ~FileWriter() {
        if (file_ != nullptr) std::fclose(file_);
    }

    bool valid() const {
        return file_ != nullptr;
    }

    bool write(std::string_view chunk) {
        if (file_ == nullptr) return false;
        if (!chunk.empty() && std::fwrite(chunk.data(), 1, chunk.size(), file_) != chunk.size()) {
            failed_ = true;
            return false;
        }
        return true;
    }

    Result<Unit> finish() {
        if (file_ == nullptr) {
            return Result<Unit>::fail("sd", "cannot open " + path_);
        }
        bool ok = !failed_;
        if (ok && std::fflush(file_) != 0) ok = false;
        if (ok && ::fsync(::fileno(file_)) != 0) ok = false;
        if (std::fclose(file_) != 0) ok = false;
        file_ = nullptr;
        if (!ok) {
            std::remove(path_.c_str());
            return Result<Unit>::fail("sd", "write failed: " + path_);
        }
        return Result<Unit>::ok(Unit{});
    }

  private:
    std::string path_;
    std::FILE* file_ = nullptr;
    bool failed_ = false;
};

class EspSdVolume final : public IVolume {
  public:
    ~EspSdVolume() override {
        unmount();
    }

    Result<Unit> mount() override {
        if (mounted_) return Result<Unit>::ok(Unit{});

        // Internal pull-ups on CS and MISO before the driver claims the pins.
        // The board may already have external pull-ups; this is defensive and
        // documented as unverified in the SD backup ADR.
        gpio_config_t pull = {};
        pull.pin_bit_mask = (1ULL << kPinCs) | (1ULL << kPinMiso);
        pull.mode = GPIO_MODE_INPUT;
        pull.pull_up_en = GPIO_PULLUP_ENABLE;
        pull.pull_down_en = GPIO_PULLDOWN_DISABLE;
        pull.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&pull);

        spi_bus_config_t bus = {};
        bus.mosi_io_num = kPinMosi;
        bus.miso_io_num = kPinMiso;
        bus.sclk_io_num = kPinSck;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.max_transfer_sz = 4096;

        esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SDSPI_DEFAULT_DMA);
        if (err != ESP_OK) {
            return Result<Unit>::fail("sd",
                                      std::string("spi_bus_initialize: ") + esp_err_to_name(err));
        }
        bus_initialized_ = true;

        host_ = SDSPI_HOST_DEFAULT();
        host_.slot = SPI2_HOST;

        sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot.gpio_cs = kPinCs;
        slot.host_id = SPI2_HOST;

        esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
        // Never format a user card, not even when the mount fails.
        mount_config.format_if_mount_failed = false;
        mount_config.max_files = 4;
        mount_config.allocation_unit_size = 16 * 1024;
        mount_config.disk_status_check_enable = true;

        err = esp_vfs_fat_sdspi_mount(kMountPoint, &host_, &slot, &mount_config, &card_);
        if (err != ESP_OK) {
            card_ = nullptr;
            spi_bus_free(SPI2_HOST);
            bus_initialized_ = false;
            return Result<Unit>::fail("sd", std::string("mount: ") + esp_err_to_name(err));
        }

        mounted_ = true;
        ESP_LOGD(kTag, "backup card mounted at %s", kMountPoint);
        return Result<Unit>::ok(Unit{});
    }

    Result<Unit> unmount() override {
        Result<Unit> result = Result<Unit>::ok(Unit{});
        if (card_ != nullptr) {
            esp_err_t err = esp_vfs_fat_sdcard_unmount(kMountPoint, card_);
            if (err != ESP_OK) {
                result = Result<Unit>::fail("sd", std::string("unmount: ") + esp_err_to_name(err));
            }
            card_ = nullptr;
        }
        if (bus_initialized_) {
            esp_err_t err = spi_bus_free(SPI2_HOST);
            if (err != ESP_OK && result) {
                result =
                    Result<Unit>::fail("sd", std::string("spi_bus_free: ") + esp_err_to_name(err));
            }
            bus_initialized_ = false;
        }
        mounted_ = false;
        return result;
    }

    Result<Unit> write_file(const std::string& name, std::string_view data) override {
        FileWriter writer(full_path(name));
        if (!writer.valid()) return writer.finish();
        writer.write(data);
        return writer.finish();
    }

    Result<Unit> write_stream(const std::string& name, const StreamProducer& produce) override {
        FileWriter writer(full_path(name));
        if (!writer.valid()) return writer.finish();

        reports::Sink sink{[&writer](std::string_view chunk) { return writer.write(chunk); }};
        auto produced = produce(sink);
        if (!produced) {
            (void)writer.finish();
            return produced;
        }
        return writer.finish();
    }

  private:
    sdmmc_card_t* card_ = nullptr;
    sdmmc_host_t host_{};
    bool mounted_ = false;
    bool bus_initialized_ = false;
};

}  // namespace

std::unique_ptr<IVolume> make_sd_volume() {
    return std::make_unique<EspSdVolume>();
}

}  // namespace kaffee::sd

#endif  // ESP_PLATFORM
