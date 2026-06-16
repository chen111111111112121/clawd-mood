idf_component_register(
    SRCS "mood.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES nvs_flash esp_hw_support
)
