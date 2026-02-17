# SLID Public API Reference

This document describes the public API for Skyrim Linked Item Distribution (SLID), allowing external mod authors to query SLID's storage network configuration.

## Overview

SLID exposes its functionality through:
1. **Papyrus Native Functions** - For script-based mods
2. **SKSE Messaging** - For native plugin authors (advanced)

The API provides access to:
- Storage network discovery
- Network container configuration
- Tagged container names

---

## Papyrus API

### Script: `SLID_API`

All API functions are global native functions on the `SLID_API` script.

---

### Network Discovery

#### `GetNetworkNames`

```papyrus
string[] Function GetNetworkNames() global native
```

Returns an array of all storage network names configured in SLID.

**Returns:** Array of network name strings.

**Example:**
```papyrus
string[] networks = SLID_API.GetNetworkNames()
Debug.Trace("SLID has " + networks.Length + " storage networks")
int i = 0
while i < networks.Length
    Debug.Trace("  - " + networks[i])
    i += 1
endwhile
```

---

#### `GetNetworkContainerCount`

```papyrus
int Function GetNetworkContainerCount(string asNetworkName) global native
```

Gets the number of containers linked in a network.

**Parameters:**
- `asNetworkName` - The network name to query

**Returns:** Number of containers (master + filter-bound), or `-1` if network not found.

---

### Network Containers

#### `GetNetworkMaster`

```papyrus
ObjectReference Function GetNetworkMaster(string asNetworkName) global native
```

Gets the master container for a network.

**Parameters:**
- `asNetworkName` - The network name to query

**Returns:** The master container reference, or `None` if network not found.

**Example:**
```papyrus
ObjectReference master = SLID_API.GetNetworkMaster("Breezehome")
if master
    Debug.Trace("Master container: " + master.GetDisplayName())
endif
```

---

#### `GetNetworkContainers`

```papyrus
ObjectReference[] Function GetNetworkContainers(string asNetworkName) global native
```

Gets all containers linked in a network (master + filter-bound + catch-all).

**Parameters:**
- `asNetworkName` - The network name to query

**Returns:** Array of container references, or empty array if network not found.

**Note:** The first element is always the master container. Subsequent elements are filter-bound containers in pipeline order. Duplicates are removed (if catch-all == master, it's not repeated).

**Example:**
```papyrus
ObjectReference[] containers = SLID_API.GetNetworkContainers("Breezehome")
Debug.Trace("Network has " + containers.Length + " containers")
```

---

### Container Info

#### `GetContainerName`

```papyrus
string Function GetContainerName(ObjectReference akContainer) global native
```

Gets the display name for a tagged container.

**Parameters:**
- `akContainer` - The container to query

**Returns:** The custom display name if tagged, or empty string if not tagged.

**Example:**
```papyrus
string name = SLID_API.GetContainerName(someChest)
if name != ""
    Debug.Trace("Tagged as: " + name)
else
    Debug.Trace("Not tagged")
endif
```

---

#### `IsContainerInNetwork`

```papyrus
bool Function IsContainerInNetwork(ObjectReference akContainer, string asNetworkName) global native
```

Checks if a container is linked in a specific network.

**Parameters:**
- `akContainer` - The container to check
- `asNetworkName` - The network name to check

**Returns:** `true` if the container is the master, catch-all, or filter-bound in the network.

---

### Version Info

#### `GetAPIVersion`

```papyrus
int Function GetAPIVersion() global native
```

Gets the SLID API version number.

**Returns:** Version as `major * 100 + minor` (e.g., `100` = v1.0.0)

---

## SKSE Messaging (Native Plugins)

For native SKSE plugin authors, SLID provides a messaging interface for C++ integration.

### Registration

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

### Message Types

```cpp
namespace SLID::API {
    enum class MessageType : std::uint32_t {
        // Requests (send TO SLID)
        kRequestNetworkList       = 'SLNL',  // Request list of network names
        kRequestNetworkContainers = 'SLNC',  // Request containers for a network

        // Responses (receive FROM SLID)
        kResponseNetworkList       = 'SLRL',  // Network name list
        kResponseNetworkContainers = 'SLRC'   // Container FormID array
    };
}
```

### Request/Response Structures

```cpp
// Network list request - no payload needed
// Send: Dispatch(kRequestNetworkList, nullptr, 0, "SLID")

// Network list response
struct NetworkListResponse {
    uint32_t count;                    // Number of networks
    // Followed by: count null-terminated UTF-8 strings
};

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

### Example: Query Network List

```cpp
void RequestNetworkList() {
    auto* messaging = SKSE::GetMessagingInterface();
    messaging->Dispatch(
        static_cast<uint32_t>(SLID::API::MessageType::kRequestNetworkList),
        nullptr,
        0,
        "SLID"
    );
}

void MyMessageHandler(SKSE::MessagingInterface::Message* msg) {
    if (msg->type == static_cast<uint32_t>(SLID::API::MessageType::kResponseNetworkList)) {
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

### Example: Query Network Containers

```cpp
void RequestNetworkContainers(const std::string& networkName) {
    SLID::API::NetworkContainersRequest request{};
    strncpy_s(request.networkName, networkName.c_str(), sizeof(request.networkName) - 1);

    auto* messaging = SKSE::GetMessagingInterface();
    messaging->Dispatch(
        static_cast<uint32_t>(SLID::API::MessageType::kRequestNetworkContainers),
        &request,
        sizeof(request),
        "SLID"
    );
}

void MyMessageHandler(SKSE::MessagingInterface::Message* msg) {
    if (msg->type == static_cast<uint32_t>(SLID::API::MessageType::kResponseNetworkContainers)) {
        auto* response = static_cast<const SLID::API::NetworkContainersResponse*>(msg->data);

        // Use networkName to correlate response with pending request
        logger::info("Response for network '{}':", response->networkName);
        logger::info("  Master: {:08X}", response->masterFormID);
        logger::info("  CatchAll: {:08X}", response->catchAllFormID);
        logger::info("  Filter containers: {}", response->filterCount);

        auto* filterIDs = reinterpret_cast<const RE::FormID*>(
            static_cast<const char*>(msg->data) + sizeof(SLID::API::NetworkContainersResponse)
        );
        for (uint32_t i = 0; i < response->filterCount; ++i) {
            logger::info("    Filter[{}]: {:08X}", i, filterIDs[i]);
        }
    }
}
```

---

## Use Cases

### SCIE Integration

SCIE can query SLID to discover which containers are part of storage networks:

```cpp
// On crafting session start, ask SLID for network containers
void OnCraftingStart() {
    // Get network list
    RequestNetworkList();

    // Later, after receiving response, query each network
    for (const auto& name : cachedNetworkNames) {
        RequestNetworkContainers(name);
    }

    // Use the container FormIDs to:
    // - Exclude SLID-managed containers from SCIE's material scanning
    // - Show network info in SCIE's container list
    // - Coordinate container state between mods
}
```

### Custom Mod Integration

A player home mod can check if its containers are configured in SLID:

```papyrus
Scriptname MyHomeIntegration extends Quest

Function CheckSLIDSetup()
    string[] networks = SLID_API.GetNetworkNames()

    int i = 0
    while i < networks.Length
        if StringUtil.Find(networks[i], "MyHome") >= 0
            Debug.Notification("MyHome is configured in SLID!")
            return
        endif
        i += 1
    endwhile

    Debug.Notification("Tip: Set up MyHome containers in SLID for auto-sorting!")
EndFunction
```

---

## Version History

| Version | Changes |
|---------|---------|
| 1.0.0 | Initial public API release |
