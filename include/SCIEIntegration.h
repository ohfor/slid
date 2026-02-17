#pragma once

#include <vector>

namespace SCIEIntegration {

    // SCIE API message types (from SCIE docs)
    enum class MessageType : std::uint32_t {
        // Requests (send TO SCIE)
        kRequestContainers     = 'SCRC',
        kRequestContainerState = 'SCRS',
        kRequestInventory      = 'SCRI',

        // Responses (receive FROM SCIE)
        kResponseContainers     = 'SCPC',
        kResponseContainerState = 'SCPS',
        kResponseInventory      = 'SCPI'
    };

    // Check if SCIE plugin is loaded (ESP check)
    bool IsInstalled();

    // Register as SCIE message listener. Call once at plugin load.
    void RegisterListener();

    // Request container list from SCIE. Response arrives async via message handler.
    void RequestContainers();

    // Get cached SCIE container FormIDs. Returns empty if no response received yet.
    const std::vector<RE::FormID>& GetCachedContainers();

    // Clear the container cache. Call when config menu closes.
    void ClearCache();

    // Check if we have a valid cached response
    bool HasCache();

    // Handle incoming SCIE messages (called from main APIMessageHandler)
    void HandleMessage(SKSE::MessagingInterface::Message* a_msg);
}
