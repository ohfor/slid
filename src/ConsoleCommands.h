#pragma once

namespace ConsoleCommands {
    bool RegisterFunctions(RE::BSScript::IVirtualMachine* a_vm);

    /// Register TESSpellCastEvent sink to capture crosshair at spell-cast time (main thread).
    /// Must be called after kDataLoaded so SLID.esp FormIDs are resolvable.
    void RegisterEventSink();
}
