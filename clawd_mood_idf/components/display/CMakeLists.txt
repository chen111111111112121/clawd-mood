idf_component_register(
    SRCS "display.cpp"
    INCLUDE_DIRS "."
    REQUIRES LovyanGFX   # 本地 vendored 组件 components/LovyanGFX；display.hpp 公开包含 <LovyanGFX.hpp>，需 public REQUIRES
)
