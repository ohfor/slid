#pragma once

#include <string>

namespace SummonChest {
    // Summon sequence: spawn chest, apply shader
    void Summon(const std::string& a_networkName);

    // Disable + delete the spawned chest, clear all state
    void Despawn();

    // Is there an active summoned chest?
    bool IsActive();

    // Check if a given FormID is the currently active summoned chest
    bool IsSummonedChest(RE::FormID a_id);

    // Which network the summoned chest is for
    std::string GetNetworkName();

    // Reset all state (called on game load/revert)
    void Clear();
}
