#include <ob/itch/parser.hpp>
#include <ob/itch/replay.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: itch_replay <itch_file> <symbol> [snapshot_interval]\n"
                  << "  e.g.: itch_replay data/itch/08302019.NASDAQ_ITCH50 MSFT 100000\n";
        return 1;
    }

    std::string itch_path = argv[1];
    std::string symbol = argv[2];
    std::size_t interval = (argc >= 4) ? std::stoull(argv[3]) : 100000;

    std::cout << "Replaying " << symbol << " from " << itch_path << " ...\n";

    ob::itch::ReplayHandler handler(symbol);
    handler.set_snapshot_callback(
        [&](std::uint64_t ts_ns, const ob::OrderBook& book) {
            auto bid = book.best_bid();
            auto ask = book.best_ask();
            // Format timestamp as HH:MM:SS.mmm
            auto ms = ts_ns / 1000000;
            auto sec = ms / 1000; ms %= 1000;
            auto min = sec / 60; sec %= 60;
            auto hr = min / 60; min %= 60;

            std::cout << "[" << hr << ":"
                      << (min < 10 ? "0" : "") << min << ":"
                      << (sec < 10 ? "0" : "") << sec << "."
                      << (ms < 100 ? "0" : "") << (ms < 10 ? "0" : "") << ms << "] "
                      << "events=" << handler.events_applied()
                      << " orders=" << book.order_count()
                      << " bid=";
            if (bid) std::cout << *bid; else std::cout << "---";
            std::cout << " ask=";
            if (ask) std::cout << *ask; else std::cout << "---";
            std::cout << "\n";
        },
        interval
    );

    auto total = ob::itch::parse_file(itch_path, handler);

    std::cout << "\nDone. " << total << " messages parsed, "
              << handler.events_applied() << " events applied to " << symbol << ".\n";

    auto bid = handler.book().best_bid();
    auto ask = handler.book().best_ask();
    std::cout << "Final: bid=";
    if (bid) std::cout << *bid; else std::cout << "---";
    std::cout << " ask=";
    if (ask) std::cout << *ask; else std::cout << "---";
    std::cout << " orders=" << handler.book().order_count() << "\n";

    return 0;
}
