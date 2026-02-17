#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>

#include <spdlog/sinks/basic_file_sink.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <vector>

using namespace std::literals;

namespace logger = SKSE::log;

namespace stl {
    using namespace SKSE::stl;
}

// Skip items that GetInventory() returns but the game UI doesn't display:
// leveled lists, nameless engine objects, and non-playable items (e.g. Hearthfire
// construction materials like Sawn Log that have the kNonPlayable record flag).
// These should never be counted, moved, or sold.
inline bool IsPhantomItem(RE::TESBoundObject* a_item)
{
    if (!a_item) return true;
    if (a_item->GetFormType() == RE::FormType::LeveledItem) return true;
    const char* name = a_item->GetName();
    if (!name || name[0] == '\0') return true;
    if (!a_item->GetPlayable()) return true;
    return false;
}
