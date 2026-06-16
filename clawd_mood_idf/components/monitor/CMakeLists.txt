idf_component_register(
    SRCS "monitor.cpp"
    INCLUDE_DIRS "."
    REQUIRES eyes mood
    PRIV_REQUIRES esp_hw_support
)
