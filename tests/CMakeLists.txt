add_executable(lf_tests
  i18n_test.cpp
  settings_test.cpp
  log_test.cpp
  policy_test.cpp
  tweak_test.cpp
  lang_test.cpp
  util_test.cpp
  preset_test.cpp
  sysinfo_test.cpp
  engine_test.cpp
  registry_integration_test.cpp
)
target_link_libraries(lf_tests PRIVATE lf_core GTest::gtest GTest::gtest_main)
target_compile_definitions(lf_tests PRIVATE LF_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

include(GoogleTest)
# PRE_TEST: テスト一覧の取得を「ビルド時」ではなく ctest 実行時に行う。
# ビルド中にテスト exe を起動しないので、実行がブロックされる環境 (Smart App Control 等) でもビルドは通る。
gtest_discover_tests(lf_tests DISCOVERY_MODE PRE_TEST)
