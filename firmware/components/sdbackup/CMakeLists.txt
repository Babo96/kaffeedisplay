idf_component_register(
    SRCS
        "src/sd_backup.cpp"
        "src/sd_card_esp.cpp"
        "src/nvs_sd_state.cpp"
        "src/sd_backup_task.cpp"
    INCLUDE_DIRS "include"
    REQUIRES domain reports service
    PRIV_REQUIRES driver fatfs nvs_flash esp_timer sdmmc
)
