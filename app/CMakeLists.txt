find_package(imgui CONFIG REQUIRED)

if(LF_REQUIRE_ADMIN)
  set(LF_EXEC_LEVEL "requireAdministrator")
else()
  set(LF_EXEC_LEVEL "asInvoker")
endif()
set(LF_VERSION_COMMA "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
set(LF_VERSION_DOT "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.0")

configure_file(res/app.manifest.in ${CMAKE_CURRENT_BINARY_DIR}/app.manifest @ONLY)
configure_file(res/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)

add_executable(latencyforge WIN32
  src/main.cpp
  src/dx11.cpp
  src/ui.cpp
  src/theme.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/app.rc
)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/app.rc PROPERTIES
  OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/app.manifest;${CMAKE_CURRENT_SOURCE_DIR}/res/app.ico")
target_include_directories(latencyforge PRIVATE src)
target_compile_definitions(latencyforge PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
target_link_libraries(latencyforge PRIVATE lf_core imgui::imgui d3d11 dxgi dwmapi)
# マニフェストは .rc で埋め込むためリンカー生成を無効化。
target_link_options(latencyforge PRIVATE /MANIFEST:NO)

# 実行時データ (言語 JSON, tweak 定義) を exe 隣の data/ へ配置 (ポータブル)。
add_custom_command(TARGET latencyforge POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:latencyforge>/data)
