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
  src/sysinfo.cpp
  src/sample_systems.cpp
  src/win_probe.cpp
  src/affinity_plan.cpp
  src/affinity_safety.cpp
  src/affinity_profile.cpp
  src/affinity_manager.cpp
  src/win_process_api.cpp
  src/power_registry.cpp
  src/win_power_api.cpp
)
target_include_directories(lf_core PUBLIC include)
target_compile_features(lf_core PUBLIC cxx_std_20)
target_link_libraries(lf_core PUBLIC nlohmann_json::nlohmann_json advapi32 dxgi powrprof)
