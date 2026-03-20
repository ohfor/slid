# SLID API Reference

Inter-plugin messaging API for native SKSE plugin authors. Allows other plugins to query SLID's storage network configuration at runtime.

This API is used by [SCIE](https://www.nexusmods.com/skyrimspecialedition/mods/170497) to discover SLID containers for crafting integration.

---

## Registration

Register as a listener for SLID messages during your plugin load:

```cpp
SKSE::GetMessagingInterface()->RegisterListener("SLID", MyMessageHandler);
```

**Important:** If your plugin also has a `nullptr` listener (to receive messages from all plugins), SKSE may route SLID's responses to that handler instead of your "SLID"-specific listener. In that case, route messages to your SLID handler from your general handler:

```cpp
void GeneralMessageHandler(SKSE::MessagingInterface::Message* a_msg) {
    // Handle other messages...

    // Also route to SLID handler for SLID responses
    MySLIDHandler(a_msg);
}
```

---

## Message Types

```cpp
enum class MessageType : std::uint32_t {
    // Requests (send TO SLID)
    kRequestNetworkList       = 'SLNL',  // Request list of network names
    kRequestNetworkContainers = 'SLNC',  // Request containers for a network

    // Responses (receive FROM SLID)
    kResponseNetworkList       = 'SLRL',  // Network name list
    kResponseNetworkContainers = 'SLRC'   // Container FormID array
};
```

---

## Request/Response Structures

```cpp
// Network containers request
struct NetworkContainersRequest {
    char networkName[64];              // Null-terminated network name
};

// Network containers response
struct NetworkContainersResponse {
    char networkName[64];              // Echo of requested network name (for correlation)
    RE::FormID masterFormID;           // Master container (0 = network not found)
    RE::FormID catchAllFormID;         // Catch-all (0 = same as master)
    uint32_t filterCount;              // Number of filter-bound containers
    // Followed by: filterCount RE::FormID values
};
```

---

## Querying Network List

Send a `kRequestNetworkList` message with no payload. SLID responds with a `kResponseNetworkList` containing a count followed by null-terminated UTF-8 network name strings.

```cpp
void RequestNetworkList() {
    auto* messaging = SKSE::GetMessagingInterface();
    messaging->Dispatch(
        static_cast<uint32_t>(MessageType::kRequestNetworkList),
        nullptr,
        0,
        "SLID"
    );
}

void MyMessageHandler(SKSE::MessagingInterface::Message* msg) {
    if (msg->type == static_cast<uint32_t>(MessageType::kResponseNetworkList)) {
        auto* data = static_cast<const char*>(msg->data);
        uint32_t count = *reinterpret_cast<const uint32_t*>(data);
        data += sizeof(uint32_t);

        logger::info("SLID has {} networks:", count);
        for (uint32_t i = 0; i < count; ++i) {
            logger::info("  - {}", data);
            data += strlen(data) + 1;  // Skip past null terminator
        }
    }
}
```

---

## Querying Network Containers

Send a `kRequestNetworkContainers` message with a `NetworkContainersRequest` payload. SLID responds with a `kResponseNetworkContainers` containing the master, catch-all, and filter-bound container FormIDs. If the network is not found, `masterFormID` is 0.

The response excludes duplicate FormIDs — if a container serves multiple filters, or if the catch-all is the master, it appears only once.

```cpp
void RequestNetworkContainers(const std::string& networkName) {
    NetworkContainersRequest request{};
    strncpy_s(request.networkName, networkName.c_str(), sizeof(request.networkName) - 1);

    auto* messaging = SKSE::GetMessagingInterface();
    messaging->Dispatch(
        static_cast<uint32_t>(MessageType::kRequestNetworkContainers),
        &request,
        sizeof(request),
        "SLID"
    );
}

void MyMessageHandler(SKSE::MessagingInterface::Message* msg) {
    if (msg->type == static_cast<uint32_t>(MessageType::kResponseNetworkContainers)) {
        auto* response = static_cast<const NetworkContainersResponse*>(msg->data);

        logger::info("Network '{}': master={:08X}, catchAll={:08X}, {} filters",
            response->networkName, response->masterFormID,
            response->catchAllFormID, response->filterCount);

        auto* filterIDs = reinterpret_cast<const RE::FormID*>(
            static_cast<const char*>(msg->data) + sizeof(NetworkContainersResponse)
        );
        for (uint32_t i = 0; i < response->filterCount; ++i) {
            logger::info("  Filter[{}]: {:08X}", i, filterIDs[i]);
        }
    }
}
```

---

## Header Reference

The structs and message types are defined in [`include/APIMessaging.h`](../include/APIMessaging.h). You can copy the enum and structs into your own project — no compile-time dependency on SLID is needed.
