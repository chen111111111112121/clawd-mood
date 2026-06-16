idf_component_register(
    SRCS "eyes_rig.cpp" "eyes_render.cpp"
    INCLUDE_DIRS "."
    REQUIRES display              # 公开头 eyes_pose.hpp 不依赖 display；但实现用 display::gfx()
    PRIV_REQUIRES esp_hw_support  # esp_random()
)
