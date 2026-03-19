#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace Lifecycle {

    enum class State : uint8_t {
        kUninitialized,  // Before kDataLoaded
        kDataLoaded,     // ESP/ESM forms available, no game session
        kGameLoading,    // kPostLoadGame or kNewGame fired, world not ready
        kWorldReady      // First TESCellFullyLoadedEvent, REFRs resolvable
    };

    /// Lock-free read of current lifecycle state.
    State GetState();

    /// Transition to a new state. Logs the transition.
    /// kWorldReady: drains the deferred callback queue.
    /// kGameLoading: clears any stale callbacks from a previous session.
    void TransitionTo(State a_newState);

    /// Queue a callback to run when the world is ready.
    /// If already kWorldReady, runs immediately.
    void DeferUntilWorldReady(std::function<void()> a_callback, const char* a_debugName = nullptr);

    /// Convenience: true if state is kWorldReady.
    bool IsWorldReady();

}  // namespace Lifecycle
