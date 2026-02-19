#pragma once

namespace ContainerRegistryTest {

    /**
     * ContainerRegistry Integration Test Suite
     *
     * == TEST INTEGRITY POLICY ==
     *
     * 1. NEVER alter a test to make it pass. If a test fails, the implementation
     *    is wrong, not the test.
     *
     * 2. Test changes MUST happen BEFORE implementing new functionality.
     *    Write the test first, watch it fail, then implement.
     *
     * 3. When adding a new container source, add its contract tests FIRST:
     *    - Does it claim the right FormIDs?
     *    - Does Resolve() return valid data?
     *    - Do all PickerEntries pass OwnsContainer()?
     *
     * 4. If a test seems wrong, discuss before changing. The test likely
     *    encodes an invariant that protects against subtle bugs.
     *
     * == USAGE ==
     *
     * Run all integration tests for ContainerRegistry.
     * Returns true if all tests pass, false if any fail.
     * Logs detailed results to SLID.log.
     *
     * Safe to call at any time after sources are registered (kDataLoaded).
     * Registers test sources with fake FormIDs (0xCAFEBABE, 0xABBAABBA, etc.)
     * that won't interfere with real gameplay.
     */
    bool RunTests();

}

