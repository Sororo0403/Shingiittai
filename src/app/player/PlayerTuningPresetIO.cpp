#include "PlayerTuningPresetIO.h"
#include <fstream>

bool PlayerTuningPresetIO::Save(const std::string &path,
                                const PlayerTuningPreset &p) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        return false;
    }

    ofs << p.maxHp << "\n";
    ofs << p.initialHp << "\n";
    ofs << p.moveSpeed << "\n";
    ofs << p.damageTakenScale << "\n";

    return true;
}

bool PlayerTuningPresetIO::Load(const std::string &path,
                                PlayerTuningPreset &p) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return false;
    }

    ifs >> p.maxHp;
    ifs >> p.initialHp;
    ifs >> p.moveSpeed;
    ifs >> p.damageTakenScale;

    return !ifs.fail();
}
