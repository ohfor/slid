#pragma once

namespace ActivationHook {
    // Install the ActivateRef hook. Call from SKSEPluginLoad.
    bool Install();

    // Set bypass for one activation (consumed on use).
    // Used by "Open" menu action to avoid recursion.
    void SetBypass(RE::FormID a_formID);

    // Return the actor FormID of the last vendor NPC we prepared dialogue for.
    // Used by OnVendorDialogueAccept to identify the vendor without alias dependency.
    RE::FormID GetLastVendorActorID();
}
