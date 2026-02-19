#include "ContainerRegistry.h"

namespace ContainerRegistryTest {

    // Test source that claims specific fake FormIDs
    class TestSource : public IContainerSource {
        RE::FormID m_testID;
        int m_priority;

    public:
        TestSource(RE::FormID a_id, int a_priority = 999)
            : m_testID(a_id), m_priority(a_priority) {}

        const char* GetSourceID() const override { return "test"; }
        int GetPriority() const override { return m_priority; }

        bool OwnsContainer(RE::FormID a_formID) const override {
            return a_formID == m_testID;
        }

        ContainerDisplay Resolve(RE::FormID) const override {
            return ContainerDisplay{
                .name = "Test Container",
                .location = "Test Location",
                .color = 0xFF0000,
                .available = true,
                .group = 99
            };
        }

        std::vector<PickerEntry> GetPickerEntries(RE::FormID) const override {
            return {PickerEntry{
                .name = "Test Entry",
                .location = "Test Location",
                .formID = m_testID,
                .isTagged = false,
                .color = 0xFF0000,
                .group = 99,
                .enabled = true
            }};
        }
    };

    // Higher priority test source for priority testing
    class HighPriorityTestSource : public IContainerSource {
        RE::FormID m_testID;

    public:
        explicit HighPriorityTestSource(RE::FormID a_id) : m_testID(a_id) {}

        const char* GetSourceID() const override { return "test_high_priority"; }
        int GetPriority() const override { return 1; }  // Very high priority

        bool OwnsContainer(RE::FormID a_formID) const override {
            return a_formID == m_testID;
        }

        ContainerDisplay Resolve(RE::FormID) const override {
            return ContainerDisplay{
                .name = "High Priority Container",
                .location = "Priority Location",
                .color = 0x00FF00,
                .available = true,
                .group = 98
            };
        }

        std::vector<PickerEntry> GetPickerEntries(RE::FormID) const override {
            return {PickerEntry{
                .name = "High Priority Entry",
                .location = "Priority Location",
                .formID = m_testID,
                .isTagged = false,
                .color = 0x00FF00,
                .group = 98,
                .enabled = true
            }};
        }
    };

    // Test FormIDs - obviously fake values that won't appear in real gameplay
    constexpr RE::FormID kUnclaimedID = 0xDEADBEEF;
    constexpr RE::FormID kTestSourceID = 0xCAFEBABE;
    constexpr RE::FormID kPriorityTestID = 0xBAADF00D;
    constexpr RE::FormID kFallbackTestID = 0xABBAABBA;

    bool RunTests() {
        auto* registry = ContainerRegistry::GetSingleton();
        int passed = 0;
        int failed = 0;

        logger::info("=== ContainerRegistry Integration Tests ===");

        // TEST 1: Unclaimed FormID returns fallback
        {
            auto display = registry->Resolve(kUnclaimedID);
            if (display.group == 255 && !display.name.empty()) {
                logger::info("TEST 1 PASS: Unclaimed FormID returns fallback (name='{}', group={})",
                             display.name, display.group);
                passed++;
            } else {
                logger::error("TEST 1 FAIL: Unclaimed FormID should return fallback with group=255, got group={}",
                              display.group);
                failed++;
            }
        }

        // TEST 2: Register test source, verify it claims its FormID
        {
            registry->Register(std::make_unique<TestSource>(kTestSourceID));

            auto display = registry->Resolve(kTestSourceID);
            if (display.name == "Test Container" && display.color == 0xFF0000) {
                logger::info("TEST 2 PASS: Test source claims its FormID (name='{}', color={:06X})",
                             display.name, display.color);
                passed++;
            } else {
                logger::error("TEST 2 FAIL: Test source should claim FormID {:08X}, got name='{}'",
                              kTestSourceID, display.name);
                failed++;
            }
        }

        // TEST 3: BuildPickerList includes test source
        {
            auto list = registry->BuildPickerList(0);
            bool foundTest = false;
            for (const auto& entry : list) {
                if (entry.name == "Test Entry" && entry.formID == kTestSourceID) {
                    foundTest = true;
                    break;
                }
            }
            if (foundTest) {
                logger::info("TEST 3 PASS: BuildPickerList includes test source entry");
                passed++;
            } else {
                logger::error("TEST 3 FAIL: BuildPickerList should include test source entry");
                failed++;
            }
        }

        // TEST 4: All PickerEntry FormIDs pass OwnsContainer for some source
        {
            auto list = registry->BuildPickerList(0);
            bool allOwned = true;
            RE::FormID failedID = 0;

            for (const auto& entry : list) {
                // Skip Pass entry (formID=0) and disabled entries
                if (entry.formID == 0) continue;
                if (!entry.enabled) continue;

                bool owned = false;
                for (const auto& source : registry->GetSources()) {
                    if (source->OwnsContainer(entry.formID)) {
                        owned = true;
                        break;
                    }
                }
                if (!owned) {
                    allOwned = false;
                    failedID = entry.formID;
                    break;
                }
            }

            if (allOwned) {
                logger::info("TEST 4 PASS: All PickerEntry FormIDs are owned by some source");
                passed++;
            } else {
                logger::error("TEST 4 FAIL: PickerEntry FormID {:08X} not owned by any source", failedID);
                failed++;
            }
        }

        // TEST 5: Priority order respected - higher priority source wins
        {
            // Register a high-priority source that claims the same ID as a potential low-priority source
            registry->Register(std::make_unique<HighPriorityTestSource>(kPriorityTestID));
            // Also register a low-priority source for the same ID
            registry->Register(std::make_unique<TestSource>(kPriorityTestID, 999));

            auto display = registry->Resolve(kPriorityTestID);
            if (display.name == "High Priority Container") {
                logger::info("TEST 5 PASS: Higher priority source wins (name='{}')", display.name);
                passed++;
            } else {
                logger::error("TEST 5 FAIL: Higher priority source should win, got name='{}'", display.name);
                failed++;
            }
        }

        // TEST 6: Resolve returns valid data for all claimed FormIDs
        {
            bool allValid = true;
            std::vector<RE::FormID> testIDs = {kTestSourceID, kPriorityTestID};

            for (auto id : testIDs) {
                auto display = registry->Resolve(id);
                if (display.name.empty()) {
                    logger::error("TEST 6 FAIL: Resolve({:08X}) returned empty name", id);
                    allValid = false;
                    break;
                }
            }

            if (allValid) {
                logger::info("TEST 6 PASS: Resolve returns valid data for all claimed FormIDs");
                passed++;
            } else {
                failed++;
            }
        }

        // TEST 7: Fallback display has expected properties
        {
            auto display = registry->Resolve(kFallbackTestID);  // Another unclaimed ID
            if (display.available == false && display.group == 255 && display.color == 0x555555) {
                logger::info("TEST 7 PASS: Fallback has correct properties (available=false, group=255, color=gray)");
                passed++;
            } else {
                logger::error("TEST 7 FAIL: Fallback properties incorrect (available={}, group={}, color={:06X})",
                              display.available, display.group, display.color);
                failed++;
            }
        }

        // Summary
        logger::info("=== Test Results: {} passed, {} failed ===", passed, failed);

        if (failed == 0) {
            logger::info("ContainerRegistry: All integration tests PASSED");
        } else {
            logger::error("ContainerRegistry: {} integration tests FAILED", failed);
        }

        return failed == 0;
    }

}  // namespace ContainerRegistryTest
