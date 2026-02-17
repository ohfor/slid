#include "SalesProcessor.h"
#include "Distributor.h"
#include "NetworkManager.h"
#include "Settings.h"
#include "TranslationService.h"

#include <chrono>

namespace SalesProcessor {

    void TryProcessSales() {
            if (!Settings::bModEnabled) return;

            auto* mgr = NetworkManager::GetSingleton();
            if (!mgr->HasSellContainer()) return;

            bool anySold = false;

            // General vendor sales (anonymous 10%/24h)
            auto result = Distributor::ProcessSales();
            if (result.itemsSold > 0) {
                mgr->AppendTransactions(result.transactions);

                std::string msg = TF("$SLID_NotifySold",
                                     std::to_string(result.itemsSold),
                                     std::to_string(result.goldEarned));
                RE::DebugNotification(msg.c_str());
                logger::info("SalesProcessor: {}", msg);
                anySold = true;
            }

            // Registered vendor sales (per-vendor buy lists, independent timers)
            auto vendorResult = Distributor::ProcessVendorSales();
            if (vendorResult.totalItemsSold > 0) {
                mgr->AppendTransactions(vendorResult.transactions);

                // Use vendor count as the vendor "name" for the notification
                std::string vendorCount = std::to_string(vendorResult.vendorsVisited) + " vendor(s)";
                std::string vmsg = TF("$SLID_NotifyVendorSold",
                                      vendorCount,
                                      std::to_string(vendorResult.totalItemsSold),
                                      std::to_string(vendorResult.totalGoldEarned));
                RE::DebugNotification(vmsg.c_str());
                logger::info("SalesProcessor: {}", vmsg);
                anySold = true;
            }

            if (anySold) {
                RE::PlaySound("ITMGoldUp");
            }
        }

    namespace {
        class SleepStopListener : public RE::BSTEventSink<RE::TESSleepStopEvent> {
        public:
            static SleepStopListener* GetSingleton() {
                static SleepStopListener singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::TESSleepStopEvent*,
                RE::BSTEventSource<RE::TESSleepStopEvent>*) override {

                logger::debug("SleepStopListener: sleep ended, checking sales");
                TryProcessSales();
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            SleepStopListener() = default;
        };

        class WaitStopListener : public RE::BSTEventSink<RE::TESWaitStopEvent> {
        public:
            static WaitStopListener* GetSingleton() {
                static WaitStopListener singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::TESWaitStopEvent*,
                RE::BSTEventSource<RE::TESWaitStopEvent>*) override {

                logger::debug("WaitStopListener: wait ended, checking sales");
                TryProcessSales();
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            WaitStopListener() = default;
        };

        class CellChangeListener : public RE::BSTEventSink<RE::TESCellAttachDetachEvent> {
        public:
            static CellChangeListener* GetSingleton() {
                static CellChangeListener singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::TESCellAttachDetachEvent* a_event,
                RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override {

                if (!a_event || !a_event->attached) return RE::BSEventNotifyControl::kContinue;

                // Debounce — multiple cells attach at once during load/fast travel
                auto now = std::chrono::steady_clock::now();
                if (now - m_lastCheck < std::chrono::seconds(10)) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                m_lastCheck = now;

                logger::debug("CellChangeListener: cell attached, checking sales");
                TryProcessSales();
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            CellChangeListener() = default;
            std::chrono::steady_clock::time_point m_lastCheck;
        };
    }

    void RegisterEventSinks() {
        auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
        if (!holder) {
            logger::error("SalesProcessor: ScriptEventSourceHolder not available");
            return;
        }

        holder->AddEventSink(SleepStopListener::GetSingleton());
        holder->AddEventSink(WaitStopListener::GetSingleton());
        holder->AddEventSink(CellChangeListener::GetSingleton());
        logger::info("SalesProcessor: registered sleep/wait/cell-change event sinks");
    }
}
