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
    src/engine/combat.cpp
    src/engine/production.cpp
    src/engine/routing.cpp
    src/engine/game.cpp
    src/engine/benchmark.cpp
    src/player/players/attention_ai_player.cpp
    src/player/sub_agents/economy_agent.cpp
    src/player/sub_agents/expansion_agent.cpp
    src/player/utils/war_utilities.cpp
    src/player/sub_agents/knapsack_war_agent.cpp
    src/player/sub_agents/direct_war_agent.cpp
    src/player/sub_agents/bootstrap_economy_agent.cpp
    src/player/models.cpp
    src/systems/economy/economy_solvers.cpp
    src/systems/transport/transport_solvers.cpp
    src/systems/ordering/ordering_solvers.cpp
)
target_include_directories(crisky_engine PUBLIC src/)

# --- Headless binary ---
add_executable(crisky_headless src/main_headless.cpp)
target_link_libraries(crisky_headless PRIVATE crisky_engine)

# --- RayLib (FetchContent) ---
include(FetchContent)
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
)
FetchContent_MakeAvailable(raylib)

# --- Interactive game binary ---
add_executable(crisky_game
    src/main_game.cpp
    src/player/players/human_player.cpp
    src/renderer/renderer.cpp
    src/renderer/camera.cpp
)
target_link_libraries(crisky_game PRIVATE crisky_engine raylib)

# --- Screenshot tool ---
add_executable(crisky_screenshots
    src/screenshot_themes.cpp
    src/renderer/renderer.cpp
    src/renderer/camera.cpp
)
target_link_libraries(crisky_screenshots PRIVATE crisky_engine raylib)

# --- GA tuning binary ---
add_executable(ga_tune src/ga_tune.cpp)
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
target_link_libraries(crisky_gyms PUBLIC crisky_systems)
target_include_directories(crisky_gyms PUBLIC src/)

# --- Gym headless binaries ---
add_executable(gym_economy src/gym_economy.cpp)
target_link_libraries(gym_economy PRIVATE crisky_gyms)

add_executable(gym_combat src/gym_combat.cpp)
target_link_libraries(gym_combat PRIVATE crisky_gyms)

add_executable(gym_transport src/gym_transport.cpp)
target_link_libraries(gym_transport PRIVATE crisky_gyms)

add_executable(gym_ordering src/gym_ordering.cpp)
target_link_libraries(gym_ordering PRIVATE crisky_gyms)

add_executable(gym_composed src/gym_composed.cpp)
target_link_libraries(gym_composed PRIVATE crisky_gyms)

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
