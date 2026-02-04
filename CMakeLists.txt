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
    src/player/attention_ai.cpp
    src/player/economy_agent.cpp
    src/player/expansion_agent.cpp
    src/player/war_utilities.cpp
    src/player/knapsack_war_agent.cpp
    src/player/direct_war_agent.cpp
    src/player/bootstrap_economy_agent.cpp
    src/player/models.cpp
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
    src/player/human_player.cpp
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
