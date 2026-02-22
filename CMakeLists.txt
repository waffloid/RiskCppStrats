cmake_minimum_required(VERSION 3.20)
project(RiskCppStrats LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Engine library (no RayLib dependency) ---
add_library(crisky_engine STATIC
    src/engine/graph.cpp
    src/engine/graph_builder.cpp
    src/engine/edge_lanes.cpp
    src/systems/combat/combat_resolver.cpp
    src/engine/production.cpp
    src/engine/routing.cpp
    src/engine/game.cpp
    src/engine/benchmark.cpp
    src/ai/players/distribution_ai_player.cpp
    src/ai/sub_agents/economy_agent.cpp
    src/ai/sub_agents/expansion_agent.cpp
    src/ai/utils/war_utilities.cpp
    src/ai/sub_agents/knapsack_war_agent.cpp
    src/ai/sub_agents/direct_war_agent.cpp
    src/ai/models/model_registry.cpp
    src/ai/models/v0_expansion.cpp
    src/ai/models/v1_knapsack.cpp
    src/ai/models/v1_knapsack_hybrid.cpp
    src/ai/models/v2_knapsack.cpp
    src/ai/models/v3.cpp
    src/ai/models/v4.cpp
    src/ai/sub_agents/conditional_expansion_agent.cpp
    src/ai/distribution.cpp
    src/systems/economy/economy_solvers.cpp
    src/systems/transport/transport_solvers.cpp
    src/systems/transport/potential_solvers.cpp
    src/systems/transport/ot_solver.cpp
    src/systems/ordering/ordering_solvers.cpp
    src/observability/metrics_collector.cpp
)
target_include_directories(crisky_engine PUBLIC src/)

# --- Headless binary ---
add_executable(crisky_headless src/apps/headless/main.cpp)
target_link_libraries(crisky_headless PRIVATE crisky_engine crisky_graph_algo)

# --- RayLib (FetchContent) ---
include(FetchContent)
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
)
FetchContent_MakeAvailable(raylib)

# --- Dear ImGui + ImPlot + rlImGui (for debug viz) ---
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
)
FetchContent_Declare(implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG master
)
FetchContent_Declare(rlimgui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG main
)

# --- Eigen (header-only, for spectral/SDP solvers) ---
# Populated manually to avoid target conflicts with Eigen's CMakeLists.txt.
FetchContent_Declare(eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG 3.4.0
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(eigen)
if(NOT eigen_POPULATED)
    FetchContent_Populate(eigen)
endif()
add_library(Eigen3::Eigen INTERFACE IMPORTED)
target_include_directories(Eigen3::Eigen INTERFACE ${eigen_SOURCE_DIR})

FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()
FetchContent_GetProperties(implot)
if(NOT implot_POPULATED)
    FetchContent_Populate(implot)
endif()
FetchContent_GetProperties(rlimgui)
if(NOT rlimgui_POPULATED)
    FetchContent_Populate(rlimgui)
endif()

# Build imgui as a static library (no CMakeLists.txt shipped)
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${implot_SOURCE_DIR}
    ${rlimgui_SOURCE_DIR}
)
target_link_libraries(imgui_lib PUBLIC raylib)

# --- Viz library (panels, overlays, ring_buffer, renderer, camera, viz_app — links imgui + engine) ---
add_library(crisky_viz STATIC
    src/viz/panel_host.cpp
    src/viz/panels/time_series_chart.cpp
    src/viz/panels/bar_chart.cpp
    src/viz/panels/graph_heatmap.cpp
    src/viz/panels/stats_table.cpp
    src/viz/panels/playback_controls.cpp
    src/viz/overlays/node_heatmap.cpp
    src/viz/overlays/gradient_arrows.cpp
    src/viz/viz_app.cpp
    src/renderer/renderer.cpp
    src/renderer/camera.cpp
)
target_include_directories(crisky_viz PUBLIC src/)
target_link_libraries(crisky_viz PUBLIC imgui_lib crisky_engine)

# --- Interactive game binary ---
add_executable(crisky_game
    src/apps/game/main.cpp
    src/ui/human_player.cpp
)
target_link_libraries(crisky_game PRIVATE crisky_viz crisky_graph_algo)

# --- Screenshot tool ---
add_executable(crisky_screenshots
    src/apps/screenshots/main.cpp
)
target_link_libraries(crisky_screenshots PRIVATE crisky_viz)

# --- GA tuning binary ---
add_executable(ga_tune src/apps/ga_tune/main.cpp)
target_link_libraries(ga_tune PRIVATE crisky_engine)

# --- Tests ---
add_executable(test_graph tests/test_graph.cpp)
target_link_libraries(test_graph PRIVATE crisky_engine)

add_executable(test_edge_lanes tests/test_edge_lanes.cpp)
target_link_libraries(test_edge_lanes PRIVATE crisky_engine)

add_executable(test_combat tests/test_combat.cpp)
target_link_libraries(test_combat PRIVATE crisky_engine)

add_executable(test_game tests/test_game.cpp)
target_link_libraries(test_game PRIVATE crisky_engine)

add_executable(test_gameplay tests/test_gameplay.cpp)
target_link_libraries(test_gameplay PRIVATE crisky_engine)

add_executable(test_collision_invariant tests/test_collision_invariant.cpp)
target_link_libraries(test_collision_invariant PRIVATE crisky_engine)

add_executable(test_scenarios tests/test_scenarios.cpp)
target_link_libraries(test_scenarios PRIVATE crisky_engine)

# --- Graph algorithms library (max-cut / QUBO solvers — uses Eigen, no RayLib) ---
add_library(crisky_graph_algo STATIC
    src/systems/graph_algo/maxcut_solvers.cpp
    src/systems/graph_algo/qubo_objectives.cpp
    src/ai/models/v5_qubo.cpp
    src/ai/models/v7.cpp
    src/ai/models/v8.cpp
    src/ai/models/v9.cpp
    src/ai/models/v10.cpp
    src/ai/models/v11.cpp
)
target_include_directories(crisky_graph_algo PUBLIC src/)
target_link_libraries(crisky_graph_algo PUBLIC crisky_engine Eigen3::Eigen)

# --- Systems library (solvers, types, data sink — no RayLib) ---
# Solver .cpp files are compiled into crisky_engine (Option A) to avoid
# circular deps with player code.  data_sink + loss_functions live here.
add_library(crisky_systems STATIC
    src/systems/common/data_sink.cpp
)
target_link_libraries(crisky_systems PUBLIC crisky_engine)
target_include_directories(crisky_systems PUBLIC src/)

# --- Gyms library (experiment harnesses — no RayLib) ---
add_library(crisky_gyms STATIC
    src/gyms/economy_gym.cpp
    src/gyms/combat_benchmarks.cpp
    src/gyms/combat_gym.cpp
    src/gyms/transport_gym.cpp
    src/gyms/ordering_gym.cpp
    src/gyms/projections.cpp
)
target_link_libraries(crisky_gyms PUBLIC crisky_systems crisky_graph_algo)
target_include_directories(crisky_gyms PUBLIC src/)

# --- Gym headless binaries ---
add_executable(gym_economy src/apps/gyms/gym_economy.cpp)
target_link_libraries(gym_economy PRIVATE crisky_gyms)

add_executable(gym_combat src/apps/gyms/gym_combat.cpp)
target_link_libraries(gym_combat PRIVATE crisky_gyms)

add_executable(gym_transport src/apps/gyms/gym_transport.cpp)
target_link_libraries(gym_transport PRIVATE crisky_gyms)

add_executable(gym_transport_diag src/apps/gyms/gym_transport_diag.cpp)
target_link_libraries(gym_transport_diag PRIVATE crisky_gyms)

add_executable(gym_ordering src/apps/gyms/gym_ordering.cpp)
target_link_libraries(gym_ordering PRIVATE crisky_gyms)

add_executable(gym_composed src/apps/gyms/gym_composed.cpp)
target_link_libraries(gym_composed PRIVATE crisky_gyms)

# --- Combat gym viz (head mode with ImGui panels) ---
add_executable(gym_combat_viz
    src/apps/gyms/gym_combat_viz.cpp
)
target_link_libraries(gym_combat_viz PRIVATE crisky_gyms crisky_viz)

# --- Transport gym viz (head mode with ImGui panels) ---
add_executable(gym_transport_viz
    src/apps/gyms/gym_transport_viz.cpp
)
target_link_libraries(gym_transport_viz PRIVATE crisky_gyms crisky_viz)

# --- Economy gym viz (head mode with ImGui panels + MCMC trace) ---
add_executable(gym_economy_viz
    src/apps/gyms/gym_economy_viz.cpp
)
target_link_libraries(gym_economy_viz PRIVATE crisky_gyms crisky_viz)

# --- Infrastructure tests ---
add_executable(test_data_sink tests/test_data_sink.cpp)
target_link_libraries(test_data_sink PRIVATE crisky_systems)

# --- Gym tests ---
add_executable(test_economy_solvers tests/test_economy_solvers.cpp)
target_link_libraries(test_economy_solvers PRIVATE crisky_gyms)

add_executable(test_combat_gym tests/test_combat_gym.cpp)
target_link_libraries(test_combat_gym PRIVATE crisky_gyms)

add_executable(test_transport_gym tests/test_transport_gym.cpp)
target_link_libraries(test_transport_gym PRIVATE crisky_gyms)

add_executable(test_ordering_gym tests/test_ordering_gym.cpp)
target_link_libraries(test_ordering_gym PRIVATE crisky_gyms)

add_executable(test_distribution tests/test_distribution.cpp)
target_link_libraries(test_distribution PRIVATE crisky_engine)

# --- Max-cut gym headless ---
add_executable(gym_maxcut src/apps/gyms/gym_maxcut.cpp)
target_link_libraries(gym_maxcut PRIVATE crisky_gyms)

# --- Max-cut gym viz ---
add_executable(gym_maxcut_viz src/apps/gyms/gym_maxcut_viz.cpp)
target_link_libraries(gym_maxcut_viz PRIVATE crisky_gyms crisky_viz)

# --- Max-cut tests ---
add_executable(test_maxcut tests/test_maxcut.cpp)
target_link_libraries(test_maxcut PRIVATE crisky_gyms)

# --- OT solver tests ---
add_executable(test_ot_solver tests/test_ot_solver.cpp)
target_link_libraries(test_ot_solver PRIVATE crisky_gyms)

add_executable(test_ot_plan_evolution tests/test_ot_plan_evolution.cpp)
target_link_libraries(test_ot_plan_evolution PRIVATE crisky_gyms)

# --- Viz test binary ---
add_executable(viz_test src/apps/viz_test/main.cpp)
target_link_libraries(viz_test PRIVATE crisky_viz)
