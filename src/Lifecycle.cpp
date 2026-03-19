#include "Lifecycle.h"

namespace Lifecycle {

    namespace {
        struct DeferredCallback {
            std::function<void()> callback;
            std::string debugName;
        };

        static std::atomic<State> s_state{State::kUninitialized};
        static std::mutex s_queueMutex;
        static std::vector<DeferredCallback> s_deferredQueue;

        const char* StateName(State a_state) {
            switch (a_state) {
                case State::kUninitialized: return "kUninitialized";
                case State::kDataLoaded:    return "kDataLoaded";
                case State::kGameLoading:   return "kGameLoading";
                case State::kWorldReady:    return "kWorldReady";
                default:                    return "Unknown";
            }
        }
    }

    State GetState() {
        return s_state.load(std::memory_order_acquire);
    }

    bool IsWorldReady() {
        return GetState() == State::kWorldReady;
    }

    void TransitionTo(State a_newState) {
        auto oldState = s_state.load(std::memory_order_acquire);
        logger::info("Lifecycle: {} -> {}", StateName(oldState), StateName(a_newState));
        s_state.store(a_newState, std::memory_order_release);

        if (a_newState == State::kGameLoading) {
            // Clear stale callbacks from a previous session
            std::lock_guard lock(s_queueMutex);
            if (!s_deferredQueue.empty()) {
                logger::debug("Lifecycle: clearing {} stale deferred callback(s)", s_deferredQueue.size());
                s_deferredQueue.clear();
            }
        }

        if (a_newState == State::kWorldReady) {
            // Drain the deferred callback queue
            std::vector<DeferredCallback> toRun;
            {
                std::lock_guard lock(s_queueMutex);
                toRun.swap(s_deferredQueue);
            }
            for (auto& entry : toRun) {
                logger::info("Lifecycle: running deferred callback '{}'", entry.debugName);
                entry.callback();
            }
        }
    }

    void DeferUntilWorldReady(std::function<void()> a_callback, const char* a_debugName) {
        std::string name = a_debugName ? a_debugName : "(unnamed)";

        if (GetState() == State::kWorldReady) {
            // Already ready — run immediately
            logger::debug("Lifecycle: running '{}' immediately (already kWorldReady)", name);
            a_callback();
            return;
        }

        std::lock_guard lock(s_queueMutex);
        s_deferredQueue.push_back({std::move(a_callback), std::move(name)});
    }

}  // namespace Lifecycle
