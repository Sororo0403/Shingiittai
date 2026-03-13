#pragma once
#include "EnemyTuningPreset.h"
#include <string>

namespace EnemyTuningPresetIO {

bool Save(const std::string &path, const EnemyTuningPreset &preset);
bool Load(const std::string &path, EnemyTuningPreset &preset);

} // namespace EnemyTuningPresetIO