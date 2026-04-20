#include "EnemyTuningPresetIO.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace {

bool EndsWithInsensitive(const std::string &value, const std::string &suffix) {
    if (value.size() < suffix.size()) {
        return false;
    }

    const size_t offset = value.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        const unsigned char lhs = static_cast<unsigned char>(value[offset + i]);
        const unsigned char rhs = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(lhs) != std::tolower(rhs)) {
            return false;
        }
    }
    return true;
}

std::string Trim(const std::string &value) {
    const auto first =
        std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
            return std::isspace(ch) != 0;
        });
    if (first == value.end()) {
        return {};
    }

    const auto last =
        std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
            return std::isspace(ch) != 0;
        }).base();
    return std::string(first, last);
}

std::string StripUtf8Bom(std::string value) {
    if (value.size() >= 3 && static_cast<unsigned char>(value[0]) == 0xEF &&
        static_cast<unsigned char>(value[1]) == 0xBB &&
        static_cast<unsigned char>(value[2]) == 0xBF) {
        value.erase(0, 3);
    }
    return value;
}

std::vector<std::string> SplitCsvLine(const std::string &line) {
    std::vector<std::string> cells;
    std::string current;
    bool inQuotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
                current.push_back('"');
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
            continue;
        }

        if (ch == ',' && !inQuotes) {
            cells.push_back(Trim(current));
            current.clear();
            continue;
        }

        current.push_back(ch);
    }

    cells.push_back(Trim(current));
    return cells;
}

bool ParseFloat3(const std::string &value, DirectX::XMFLOAT3 &out) {
    std::istringstream iss(value);
    return static_cast<bool>(iss >> out.x >> out.y >> out.z);
}

bool ParseFloat3Cells(const std::vector<std::string> &cells,
                      DirectX::XMFLOAT3 &outValue) {
    if (cells.size() >= 5) {
        std::istringstream x(cells[2]);
        std::istringstream y(cells[3]);
        std::istringstream z(cells[4]);
        if ((x >> outValue.x) && (y >> outValue.y) && (z >> outValue.z)) {
            return true;
        }
    }
    if (cells.size() >= 3) {
        return ParseFloat3(cells[2], outValue);
    }
    return false;
}

template <typename T> bool ParseCell(const std::vector<std::string> &cells,
                                     size_t index, T &outValue) {
    if (index >= cells.size()) {
        return false;
    }
    std::istringstream iss(cells[index]);
    T parsed{};
    if (!(iss >> parsed)) {
        return false;
    }
    outValue = parsed;
    return true;
}

bool LoadLegacyText(const std::string &path, EnemyTuningPreset &p) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return false;
    }

    auto readOptional = [&ifs](auto &value) {
        using ValueType = std::decay_t<decltype(value)>;
        ValueType temp{};
        if (ifs >> temp) {
            value = temp;
            return;
        }
        ifs.clear();
    };

    ifs >> p.nearAttackDistance;
    ifs >> p.farAttackDistance;

    ifs >> p.smash.damage >> p.smash.knockback >> p.smash.hitBoxSize.x >>
        p.smash.hitBoxSize.y >> p.smash.hitBoxSize.z;
    ifs >> p.smashChargeTime >> p.smashAttackForwardOffset >>
        p.smashAttackHeightOffset;
    ifs >> p.smashTiming.totalTime >> p.smashTiming.trackingEndTime >>
        p.smashTiming.activeStartTime >> p.smashTiming.activeEndTime >>
        p.smashTiming.recoveryStartTime;

    ifs >> p.sweep.damage >> p.sweep.knockback >> p.sweep.hitBoxSize.x >>
        p.sweep.hitBoxSize.y >> p.sweep.hitBoxSize.z;
    ifs >> p.sweepChargeTime >> p.sweepAttackSideOffset >>
        p.sweepAttackHeightOffset;
    ifs >> p.sweepTiming.totalTime >> p.sweepTiming.trackingEndTime >>
        p.sweepTiming.activeStartTime >> p.sweepTiming.activeEndTime >>
        p.sweepTiming.recoveryStartTime;

    ifs >> p.bullet.damage >> p.bullet.knockback >> p.bullet.hitBoxSize.x >>
        p.bullet.hitBoxSize.y >> p.bullet.hitBoxSize.z;
    ifs >> p.shotChargeTime >> p.shotRecoveryTime >> p.shotInterval >>
        p.shotMinCount >> p.shotMaxCount >> p.bulletSpeed >> p.bulletLifeTime >>
        p.bulletSpawnHeightOffset;

    ifs >> p.wave.damage >> p.wave.knockback >> p.wave.hitBoxSize.x >>
        p.wave.hitBoxSize.y >> p.wave.hitBoxSize.z;
    ifs >> p.waveChargeTime >> p.waveRecoveryTime >> p.waveSpeed >>
        p.waveMaxDistance >> p.waveSpawnForwardOffset >>
        p.waveSpawnHeightOffset;

    readOptional(p.warpApproachChainMaxSteps);
    readOptional(p.warpEscapeChainMaxSteps);
    readOptional(p.approachChainContinueDistance);
    readOptional(p.escapeChainContinueDistance);

    readOptional(p.sweepWarpSmashMaxDistance);
    readOptional(p.sweepWarpSmashChance);
    readOptional(p.waveWarpSmashMinDistance);
    readOptional(p.waveWarpSmashChance);

    readOptional(p.enemyMaxHp);
    readOptional(p.phase2HealthRatioThreshold);

    readOptional(p.warpStartTime);
    readOptional(p.warpMoveTime);
    readOptional(p.warpEndTime);

    return true;
}

bool LoadNumericCsv(const std::string &path, EnemyTuningPreset &p) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return false;
    }

    std::vector<std::vector<std::string>> rows;
    std::string line;
    while (std::getline(ifs, line)) {
        line = StripUtf8Bom(line);
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        rows.push_back(SplitCsvLine(trimmed));
    }

    if (rows.size() < 16) {
        return false;
    }

    ParseCell(rows[0], 0, p.nearAttackDistance);
    ParseCell(rows[1], 0, p.farAttackDistance);

    ParseCell(rows[2], 0, p.smash.damage);
    ParseCell(rows[2], 1, p.smash.knockback);
    ParseFloat3Cells(rows[2], p.smash.hitBoxSize);
    ParseCell(rows[3], 0, p.smashChargeTime);
    ParseCell(rows[3], 1, p.smashAttackForwardOffset);
    ParseCell(rows[3], 2, p.smashAttackHeightOffset);
    ParseCell(rows[4], 0, p.smashTiming.totalTime);
    ParseCell(rows[4], 1, p.smashTiming.trackingEndTime);
    ParseCell(rows[4], 2, p.smashTiming.activeStartTime);
    ParseCell(rows[4], 3, p.smashTiming.activeEndTime);
    ParseCell(rows[4], 4, p.smashTiming.recoveryStartTime);

    ParseCell(rows[5], 0, p.sweep.damage);
    ParseCell(rows[5], 1, p.sweep.knockback);
    ParseFloat3Cells(rows[5], p.sweep.hitBoxSize);
    ParseCell(rows[6], 0, p.sweepChargeTime);
    ParseCell(rows[6], 1, p.sweepAttackSideOffset);
    ParseCell(rows[6], 2, p.sweepAttackHeightOffset);
    ParseCell(rows[7], 0, p.sweepTiming.totalTime);
    ParseCell(rows[7], 1, p.sweepTiming.trackingEndTime);
    ParseCell(rows[7], 2, p.sweepTiming.activeStartTime);
    ParseCell(rows[7], 3, p.sweepTiming.activeEndTime);
    ParseCell(rows[7], 4, p.sweepTiming.recoveryStartTime);

    ParseCell(rows[8], 0, p.bullet.damage);
    ParseCell(rows[8], 1, p.bullet.knockback);
    ParseFloat3Cells(rows[8], p.bullet.hitBoxSize);
    ParseCell(rows[9], 0, p.shotChargeTime);
    ParseCell(rows[9], 1, p.shotRecoveryTime);
    ParseCell(rows[9], 2, p.shotInterval);
    ParseCell(rows[9], 3, p.shotMinCount);
    ParseCell(rows[9], 4, p.shotMaxCount);
    ParseCell(rows[9], 5, p.bulletSpeed);
    ParseCell(rows[9], 6, p.bulletLifeTime);
    ParseCell(rows[9], 7, p.bulletSpawnHeightOffset);

    ParseCell(rows[10], 0, p.wave.damage);
    ParseCell(rows[10], 1, p.wave.knockback);
    ParseFloat3Cells(rows[10], p.wave.hitBoxSize);
    ParseCell(rows[11], 0, p.waveChargeTime);
    ParseCell(rows[11], 1, p.waveRecoveryTime);
    ParseCell(rows[11], 2, p.waveSpeed);
    ParseCell(rows[11], 3, p.waveMaxDistance);
    ParseCell(rows[11], 4, p.waveSpawnForwardOffset);
    ParseCell(rows[11], 5, p.waveSpawnHeightOffset);

    ParseCell(rows[12], 0, p.warpApproachChainMaxSteps);
    ParseCell(rows[12], 1, p.warpEscapeChainMaxSteps);
    ParseCell(rows[12], 2, p.approachChainContinueDistance);
    ParseCell(rows[12], 3, p.escapeChainContinueDistance);

    ParseCell(rows[13], 0, p.sweepWarpSmashMaxDistance);
    ParseCell(rows[13], 1, p.sweepWarpSmashChance);
    ParseCell(rows[13], 2, p.waveWarpSmashMinDistance);
    ParseCell(rows[13], 3, p.waveWarpSmashChance);

    ParseCell(rows[14], 0, p.enemyMaxHp);
    ParseCell(rows[14], 1, p.phase2HealthRatioThreshold);

    ParseCell(rows[15], 0, p.warpStartTime);
    ParseCell(rows[15], 1, p.warpMoveTime);
    ParseCell(rows[15], 2, p.warpEndTime);
    return true;
}

} // namespace

bool EnemyTuningPresetIO::Save(const std::string &path,
                               const EnemyTuningPreset &p) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        return false;
    }

    if (EndsWithInsensitive(path, ".csv")) {
        ofs << p.nearAttackDistance << "\n";
        ofs << p.farAttackDistance << "\n";
        ofs << p.smash.damage << "," << p.smash.knockback << ","
            << p.smash.hitBoxSize.x << "," << p.smash.hitBoxSize.y << ","
            << p.smash.hitBoxSize.z << "\n";
        ofs << p.smashChargeTime << "," << p.smashAttackForwardOffset << ","
            << p.smashAttackHeightOffset << "\n";
        ofs << p.smashTiming.totalTime << "," << p.smashTiming.trackingEndTime
            << "," << p.smashTiming.activeStartTime << ","
            << p.smashTiming.activeEndTime << ","
            << p.smashTiming.recoveryStartTime << "\n";
        ofs << p.sweep.damage << "," << p.sweep.knockback << ","
            << p.sweep.hitBoxSize.x << "," << p.sweep.hitBoxSize.y << ","
            << p.sweep.hitBoxSize.z << "\n";
        ofs << p.sweepChargeTime << "," << p.sweepAttackSideOffset << ","
            << p.sweepAttackHeightOffset << "\n";
        ofs << p.sweepTiming.totalTime << "," << p.sweepTiming.trackingEndTime
            << "," << p.sweepTiming.activeStartTime << ","
            << p.sweepTiming.activeEndTime << ","
            << p.sweepTiming.recoveryStartTime << "\n";
        ofs << p.bullet.damage << "," << p.bullet.knockback << ","
            << p.bullet.hitBoxSize.x << "," << p.bullet.hitBoxSize.y << ","
            << p.bullet.hitBoxSize.z << "\n";
        ofs << p.shotChargeTime << "," << p.shotRecoveryTime << ","
            << p.shotInterval << "," << p.shotMinCount << "," << p.shotMaxCount
            << "," << p.bulletSpeed << "," << p.bulletLifeTime << ","
            << p.bulletSpawnHeightOffset << "\n";
        ofs << p.wave.damage << "," << p.wave.knockback << ","
            << p.wave.hitBoxSize.x << "," << p.wave.hitBoxSize.y << ","
            << p.wave.hitBoxSize.z << "\n";
        ofs << p.waveChargeTime << "," << p.waveRecoveryTime << ","
            << p.waveSpeed << "," << p.waveMaxDistance << ","
            << p.waveSpawnForwardOffset << "," << p.waveSpawnHeightOffset
            << "\n";
        ofs << p.warpApproachChainMaxSteps << "," << p.warpEscapeChainMaxSteps
            << "," << p.approachChainContinueDistance << ","
            << p.escapeChainContinueDistance << "\n";
        ofs << p.sweepWarpSmashMaxDistance << "," << p.sweepWarpSmashChance
            << "," << p.waveWarpSmashMinDistance << "," << p.waveWarpSmashChance
            << "\n";
        ofs << p.enemyMaxHp << "," << p.phase2HealthRatioThreshold << "\n";
        ofs << p.warpStartTime << "," << p.warpMoveTime << "," << p.warpEndTime
            << "\n";
        return true;
    }

    ofs << p.nearAttackDistance << "\n";
    ofs << p.farAttackDistance << "\n";

    ofs << p.smash.damage << " " << p.smash.knockback << " "
        << p.smash.hitBoxSize.x << " " << p.smash.hitBoxSize.y << " "
        << p.smash.hitBoxSize.z << "\n";
    ofs << p.smashChargeTime << " " << p.smashAttackForwardOffset << " "
        << p.smashAttackHeightOffset << "\n";
    ofs << p.smashTiming.totalTime << " " << p.smashTiming.trackingEndTime
        << " " << p.smashTiming.activeStartTime << " "
        << p.smashTiming.activeEndTime << " " << p.smashTiming.recoveryStartTime
        << "\n";

    ofs << p.sweep.damage << " " << p.sweep.knockback << " "
        << p.sweep.hitBoxSize.x << " " << p.sweep.hitBoxSize.y << " "
        << p.sweep.hitBoxSize.z << "\n";
    ofs << p.sweepChargeTime << " " << p.sweepAttackSideOffset << " "
        << p.sweepAttackHeightOffset << "\n";
    ofs << p.sweepTiming.totalTime << " " << p.sweepTiming.trackingEndTime
        << " " << p.sweepTiming.activeStartTime << " "
        << p.sweepTiming.activeEndTime << " " << p.sweepTiming.recoveryStartTime
        << "\n";

    ofs << p.bullet.damage << " " << p.bullet.knockback << " "
        << p.bullet.hitBoxSize.x << " " << p.bullet.hitBoxSize.y << " "
        << p.bullet.hitBoxSize.z << "\n";
    ofs << p.shotChargeTime << " " << p.shotRecoveryTime << " "
        << p.shotInterval << " " << p.shotMinCount << " " << p.shotMaxCount
        << " " << p.bulletSpeed << " " << p.bulletLifeTime << " "
        << p.bulletSpawnHeightOffset << "\n";

    ofs << p.wave.damage << " " << p.wave.knockback << " "
        << p.wave.hitBoxSize.x << " " << p.wave.hitBoxSize.y << " "
        << p.wave.hitBoxSize.z << "\n";
    ofs << p.waveChargeTime << " " << p.waveRecoveryTime << " " << p.waveSpeed
        << " " << p.waveMaxDistance << " " << p.waveSpawnForwardOffset << " "
        << p.waveSpawnHeightOffset << "\n";

    ofs << p.warpApproachChainMaxSteps << " " << p.warpEscapeChainMaxSteps << " "
        << p.approachChainContinueDistance << " "
        << p.escapeChainContinueDistance << "\n";
    ofs << p.sweepWarpSmashMaxDistance << " " << p.sweepWarpSmashChance << " "
        << p.waveWarpSmashMinDistance << " " << p.waveWarpSmashChance << "\n";
    ofs << p.enemyMaxHp << " " << p.phase2HealthRatioThreshold << "\n";
    ofs << p.warpStartTime << " " << p.warpMoveTime << " " << p.warpEndTime
        << "\n";

    return true;
}

bool EnemyTuningPresetIO::Load(const std::string &path, EnemyTuningPreset &p) {
    if (EndsWithInsensitive(path, ".csv")) {
        return LoadNumericCsv(path, p);
    }
    return LoadLegacyText(path, p);
}
