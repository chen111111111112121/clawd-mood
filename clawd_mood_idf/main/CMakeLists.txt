idf_component_register(
    SRCS "app_main.cpp" "render_task.cpp"
    INCLUDE_DIRS "."
    REQUIRES display eyes monitor mood netsvc
)
