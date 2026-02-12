#ifndef CRISKY_MODEL_REGISTRY_HPP
#define CRISKY_MODEL_REGISTRY_HPP

#include "ai/models.hpp"

// Register a model factory by name. Called by per-model .cpp files.
void register_model(const char* name, ModelFactory factory);

#endif
