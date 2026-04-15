#include "EnemyTuningPresetIO.h"
#include <fstream>
#include <type_traits>

bool EnemyTuningPresetIO::Save(const std::string &path,
                               const EnemyTuningPreset &p) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        return false;
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
  /*  ofs << p.smashChargeTime << " " << p.smashAttackTime << " "
        << p.smashRecoveryTime << " " << p.smashAttackForwardOffset << " "
        << p.smashAttackHeightOffset << "\n";
    ofs << p.smashTiming.totalTime << " " << p.smashTiming.activeStartTime
        << " " << p.smashTiming.activeEndTime << " "
        << p.smashTiming.recoveryStartTime << "\n";*/

    ofs << p.sweep.damage << " " << p.sweep.knockback << " "
        << p.sweep.hitBoxSize.x << " " << p.sweep.hitBoxSize.y << " "
        << p.sweep.hitBoxSize.z << "\n";
    ofs << p.sweepChargeTime << " " << p.sweepAttackSideOffset << " "
        << p.sweepAttackHeightOffset << "\n";
    ofs << p.sweepTiming.totalTime << " " << p.sweepTiming.trackingEndTime
        << " " << p.sweepTiming.activeStartTime << " "
        << p.sweepTiming.activeEndTime << " " << p.sweepTiming.recoveryStartTime
        << "\n";
 /*   ofs << p.sweepChargeTime << " " << p.sweepAttackTime << " "
        << p.sweepRecoveryTime << " " << p.sweepAttackSideOffset << " "
        << p.sweepAttackHeightOffset << "\n";
    ofs << p.sweepTiming.totalTime << " " << p.sweepTiming.activeStartTime
        << " " << p.sweepTiming.activeEndTime << " "
        << p.sweepTiming.recoveryStartTime << "\n";*/

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
