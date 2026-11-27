file(GLOB_RECURSE app_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "."
    PRIV_REQUIRES mbedtls esp_system esp_hw_support heap esp_timer esp_wifi esp_netif nvs_flash spi_flash esp_partition app_update mdns
)
