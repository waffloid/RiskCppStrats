#ifndef CRISKY_MODELS_HPP
#define CRISKY_MODELS_HPP

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/player_interface.hpp"

using ModelFactory = std::function<std::unique_ptr<PlayerInterface>(int player_id)>;

// Get a model factory by name. Returns nullptr if not found.
const ModelFactory* get_model(const std::string& name);

// List all registered model names.
std::vector<std::string> list_models();

#endif
