#include "SCIEIntegration.h"

namespace SCIEIntegration {

    namespace {
        constexpr auto kSCIEPluginName = "CraftingInventoryExtender"sv;

        std::vector<RE::FormID> g_cachedContainers;
        bool g_hasCache = false;

        void HandleSCIEMessage(SKSE::MessagingInterface::Message* a_msg) {
            if (!a_msg) return;

            logger::info("SCIEIntegration: received message type={:08X}, dataLen={}",
                         a_msg->type, a_msg->dataLen);

            if (a_msg->type == static_cast<uint32_t>(MessageType::kResponseContainers)) {
                // Response contains array of FormIDs
                if (a_msg->data && a_msg->dataLen > 0) {
                    size_t count = a_msg->dataLen / sizeof(RE::FormID);
                    auto* formIDs = static_cast<RE::FormID*>(a_msg->data);

                    g_cachedContainers.clear();
                    g_cachedContainers.reserve(count);

                    for (size_t i = 0; i < count; ++i) {
                        if (formIDs[i] != 0) {
                            g_cachedContainers.push_back(formIDs[i]);
                        }
                    }

                    g_hasCache = true;
                    logger::info("SCIEIntegration: received {} containers from SCIE", g_cachedContainers.size());
                } else {
                    g_cachedContainers.clear();
                    g_hasCache = true;
                    logger::info("SCIEIntegration: received empty container list from SCIE");
                }
            }
        }
    }

    bool IsInstalled() {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        return dataHandler && dataHandler->LookupModByName("CraftingInventoryExtender.esp");
    }

    void RegisterListener() {
        auto* messaging = SKSE::GetMessagingInterface();
        if (messaging) {
            bool installed = IsInstalled();
            logger::info("SCIEIntegration: SCIE ESP installed = {}", installed);
            if (messaging->RegisterListener(kSCIEPluginName.data(), HandleSCIEMessage)) {
                logger::info("SCIEIntegration: registered as SCIE message listener");
            } else {
                logger::info("SCIEIntegration: failed to register listener (SCIE DLL not loaded?)");
            }
        }
    }

    void RequestContainers() {
        logger::info("SCIEIntegration::RequestContainers called");

        if (!IsInstalled()) {
            logger::info("SCIEIntegration: SCIE ESP not installed, skipping request");
            return;
        }

        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) {
            logger::error("SCIEIntegration: no messaging interface");
            return;
        }

        // Clear existing cache before requesting fresh data
        g_cachedContainers.clear();
        g_hasCache = false;

        // Send request to SCIE
        logger::info("SCIEIntegration: dispatching kRequestContainers to '{}'", kSCIEPluginName);
        bool sent = messaging->Dispatch(
            static_cast<uint32_t>(MessageType::kRequestContainers),
            nullptr,
            0,
            kSCIEPluginName.data()
        );

        if (sent) {
            logger::info("SCIEIntegration: dispatch returned true");
        } else {
            logger::warn("SCIEIntegration: dispatch returned false");
        }
    }

    const std::vector<RE::FormID>& GetCachedContainers() {
        return g_cachedContainers;
    }

    void ClearCache() {
        g_cachedContainers.clear();
        g_hasCache = false;
        logger::debug("SCIEIntegration: cache cleared");
    }

    bool HasCache() {
        return g_hasCache;
    }

    void HandleMessage(SKSE::MessagingInterface::Message* a_msg) {
        // Route to internal handler
        HandleSCIEMessage(a_msg);
    }
}
