#pragma once

namespace APIMessaging {

    // Message types for SLID API (matches docs/API.md)
    enum class MessageType : std::uint32_t {
        // Requests (received from other plugins)
        kRequestNetworkList       = 'SLNL',  // Request list of network names
        kRequestNetworkContainers = 'SLNC',  // Request containers for a network

        // Responses (sent to requesting plugin)
        kResponseNetworkList       = 'SLRL',  // Network name list
        kResponseNetworkContainers = 'SLRC'   // Container FormID array
    };

    // Request structure for kRequestNetworkContainers
    struct NetworkContainersRequest {
        char networkName[64];  // Null-terminated network name
    };

    // Response structure for kResponseNetworkContainers
    struct NetworkContainersResponse {
        char networkName[64];       // Echo of requested network name (for correlation)
        RE::FormID masterFormID;    // Master container (0 = network not found)
        RE::FormID catchAllFormID;  // Catch-all (0 = same as master)
        std::uint32_t filterCount;  // Number of filter-bound containers
        // Followed by: filterCount RE::FormID values
    };

    // Initialize API messaging
    void Initialize();

    // Handle incoming API messages from other plugins
    void HandleMessage(SKSE::MessagingInterface::Message* a_msg);

}  // namespace APIMessaging
