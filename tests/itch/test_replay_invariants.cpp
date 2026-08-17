#include <gtest/gtest.h>
#include <ob/itch/parser.hpp>
#include <ob/itch/replay.hpp>
#include <ob/invariant_checker.hpp>

#include <filesystem>
#include <iostream>

using namespace ob::itch;

namespace {

const char* ITCH_PATH = "data/itch/08302019.NASDAQ_ITCH50";

} // namespace

TEST(ItchReplay, InvariantCoherence) {
    if (!std::filesystem::exists(ITCH_PATH)) {
        std::cout << "SKIPPED: " << ITCH_PATH << " not found\n";
        GTEST_SKIP() << "ITCH sample absent";
    }

    // Replay MSFT and check invariants after every event
    // ITCH prices are 1e-4 scale. US equity tick = $0.01 = 100 in ITCH units.
    ReplayHandler handler("MSFT", 100);
    std::size_t violations = 0;
    std::string first_violation;

    handler.set_snapshot_callback(
        [&](std::uint64_t /*ts*/, const ob::OrderBook& book) {
            auto result = ob::check_invariants(book);
            if (!result.ok && violations == 0) {
                first_violation = result.message +
                    " (after event " + std::to_string(handler.events_applied()) + ")";
            }
            if (!result.ok) ++violations;
        },
        100  // Check every 100 events (>15K checks across 1.5M events)
    );

    auto total = parse_file(ITCH_PATH, handler);

    std::cout << "MSFT replay: " << total << " messages, "
              << handler.events_applied() << " events applied, "
              << violations << " invariant violations\n";

    EXPECT_EQ(violations, 0u) << first_violation;
    EXPECT_GT(handler.events_applied(), 100000u);
}
