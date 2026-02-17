#include "APIMessaging.h"
#include "NetworkManager.h"

namespace APIMessaging {

    void Initialize() {
        logger::info("SLID API messaging initialized");
    }

    void HandleMessage(SKSE::MessagingInterface::Message* a_msg) {
        if (!a_msg) return;

        const char* sender = a_msg->sender ? a_msg->sender : "unknown";

        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) {
            logger::error("APIMessaging: SKSE messaging interface unavailable");
            return;
        }

        auto type = static_cast<MessageType>(a_msg->type);

        switch (type) {
            case MessageType::kRequestNetworkList: {
                logger::info("APIMessaging: received network list request from {}", sender);

                auto* mgr = NetworkManager::GetSingleton();
                auto names = mgr->GetNetworkNames();

                // Build response: count + null-terminated strings
                std::vector<char> buffer;

                // Count
                std::uint32_t count = static_cast<std::uint32_t>(names.size());
                buffer.resize(sizeof(count));
                std::memcpy(buffer.data(), &count, sizeof(count));

                // Append each name as null-terminated string
                for (const auto& name : names) {
                    buffer.insert(buffer.end(), name.begin(), name.end());
                    buffer.push_back('\0');
                }

                // Dispatch response back to the requesting plugin
                messaging->Dispatch(
                    static_cast<std::uint32_t>(MessageType::kResponseNetworkList),
                    buffer.data(),
                    static_cast<std::uint32_t>(buffer.size()),
                    sender
                );

                logger::info("APIMessaging: dispatched {} network names to {}", count, sender);
                break;
            }

            case MessageType::kRequestNetworkContainers: {
                if (!a_msg->data || a_msg->dataLen < sizeof(NetworkContainersRequest)) {
                    logger::warn("APIMessaging: invalid network containers request from {}", sender);
                    break;
                }

                auto* request = static_cast<NetworkContainersRequest*>(a_msg->data);
                std::string networkName(request->networkName);

                logger::info("APIMessaging: received container request for '{}' from {}", networkName, sender);

                auto* mgr = NetworkManager::GetSingleton();
                auto* network = mgr->FindNetwork(networkName);

                if (!network) {
                    // Send empty response with network name for correlation
                    NetworkContainersResponse response{};
                    strncpy_s(response.networkName, networkName.c_str(), sizeof(response.networkName) - 1);
                    response.masterFormID = 0;
                    response.catchAllFormID = 0;
                    response.filterCount = 0;

                    messaging->Dispatch(
                        static_cast<std::uint32_t>(MessageType::kResponseNetworkContainers),
                        &response,
                        sizeof(response),
                        sender
                    );

                    logger::info("APIMessaging: network '{}' not found, dispatched empty response to {}", networkName, sender);
                    break;
                }

                // Collect unique filter-bound container FormIDs
                std::vector<RE::FormID> filterContainers;
                for (const auto& stage : network->filters) {
                    if (stage.containerFormID != 0 &&
                        stage.containerFormID != network->masterFormID &&
                        stage.containerFormID != network->catchAllFormID)
                    {
                        // Check for duplicates
                        bool found = false;
                        for (auto id : filterContainers) {
                            if (id == stage.containerFormID) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            filterContainers.push_back(stage.containerFormID);
                        }
                    }
                }

                // Build response buffer: header + FormID array
                std::vector<char> buffer(sizeof(NetworkContainersResponse) +
                                         filterContainers.size() * sizeof(RE::FormID));

                auto* response = reinterpret_cast<NetworkContainersResponse*>(buffer.data());
                strncpy_s(response->networkName, networkName.c_str(), sizeof(response->networkName) - 1);
                response->masterFormID = network->masterFormID;
                response->catchAllFormID = network->catchAllFormID;
                response->filterCount = static_cast<std::uint32_t>(filterContainers.size());

                // Copy filter container FormIDs
                if (!filterContainers.empty()) {
                    auto* filterData = reinterpret_cast<RE::FormID*>(buffer.data() + sizeof(NetworkContainersResponse));
                    std::memcpy(filterData, filterContainers.data(), filterContainers.size() * sizeof(RE::FormID));
                }

                messaging->Dispatch(
                    static_cast<std::uint32_t>(MessageType::kResponseNetworkContainers),
                    buffer.data(),
                    static_cast<std::uint32_t>(buffer.size()),
                    sender
                );

                logger::info("APIMessaging: dispatched network '{}' containers (master={:08X}, catchAll={:08X}, {} filters) to {}",
                    networkName, network->masterFormID, network->catchAllFormID,
                    filterContainers.size(), sender);
                break;
            }

            default:
                // Not our message type, ignore
                break;
        }
    }

}  // namespace APIMessaging
