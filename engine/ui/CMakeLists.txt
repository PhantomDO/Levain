add_library(levain_ui STATIC src/context.cpp src/ui_pass.cpp)
add_library(levain::ui ALIAS levain_ui)

target_include_directories(levain_ui PUBLIC include)

# ImGui est dans notre API (ImDrawData, ImGuiIO), comme NVRHI ; SDL, lui, ne traverse pas platform
# (ADR-0032, contrôlé par deps.imgui-visibility).
find_package(imgui CONFIG REQUIRED)
target_link_libraries(levain_ui PUBLIC imgui::imgui levain::core levain::platform levain::render)
