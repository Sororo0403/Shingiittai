#pragma once
#include "PlayerTuningPreset.h"
#include <string>

namespace PlayerTuningPresetIO {

bool Save(const std::string &path, const PlayerTuningPreset &preset);
bool Load(const std::string &path, PlayerTuningPreset &preset);

} // namespace PlayerTuningPresetIO
