#pragma once

namespace Diagnostics {

    /// Run all startup self-checks and log results.
    /// Call after kWorldReady when all forms and REFRs are available.
    /// Never crashes — all checks are log-only.
    void ValidateState();

}  // namespace Diagnostics
