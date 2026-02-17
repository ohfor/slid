#pragma once

namespace SalesProcessor {
    // Register sleep/wait/cell-change event sinks. Call from kDataLoaded.
    void RegisterEventSinks();

    // Run sales check (general + vendor). Called by event sinks and overview menu.
    void TryProcessSales();
}
