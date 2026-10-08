add_library(lf_core STATIC
  src/i18n.cpp
  src/settings.cpp
  src/util.cpp
  src/log.cpp
  src/registry.cpp
  src/win_registry.cpp
  src/policy.cpp
  src/tweak.cpp
  src/state.cpp
  src/history.cpp
  src/engine.cpp
  src/system.cpp
  src/preset.cpp
)
target_include_directories(lf_core PUBLIC include)
target_compile_features(lf_core PUBLIC cxx_std_20)
target_link_libraries(lf_core PUBLIC nlohmann_json::nlohmann_json advapi32)
