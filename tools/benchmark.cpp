// Standalone CLI: measures per-message latency (p50/p99/p99.9) and overall
// throughput of OrderBook, replaying a real LOBSTER message file. Not part
// of the test suite — needs the full-day sample file (data/lobster/, not
// checked in, see lobster_replay.cpp) and benchmark numbers from a shared CI
// runner would be meaningless noise anyway. Run manually, in a Release
// build. See SPEC.md §7 for the methodology this follows.
//
// Usage: benchmark <message_file.csv>

#include "orderbook/book.hpp"
#include "orderbook/lobster.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

// Messages processed before timing starts, to let the allocator/cache reach
// a steady state — discarded from every reported statistic (§7: "warm up
// before measuring; discard the first N messages").
constexpr std::size_t kWarmupMessages = 10'000;

double percentile(const std::vector<std::int64_t>& sorted_ns, double p) {
    if (sorted_ns.empty()) {
        return 0.0;
    }
    const auto idx = static_cast<std::size_t>(p * static_cast<double>(sorted_ns.size() - 1));
    return static_cast<double>(sorted_ns[idx]);
}

void report(const std::string& label, std::vector<std::int64_t> ns) {
    if (ns.empty()) {
        return;
    }
    std::sort(ns.begin(), ns.end());
    std::cout << std::left << std::setw(16) << label << " n=" << std::setw(8) << ns.size()
               << " p50=" << std::setw(8) << percentile(ns, 0.50) << "ns"
               << " p99=" << std::setw(8) << percentile(ns, 0.99) << "ns"
               << " p99.9=" << std::setw(8) << percentile(ns, 0.999) << "ns\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: benchmark <message_file.csv>\n";
        return EXIT_FAILURE;
    }

#ifndef NDEBUG
    std::cerr << "warning: built without NDEBUG (not a Release build) — numbers will not reflect "
                 "optimized performance. Configure with -DCMAKE_BUILD_TYPE=Release.\n";
#endif

    // Pre-parse into memory so the timed loop measures OrderBook cost alone,
    // not file I/O or CSV parsing (§7: fixed, isolated input).
    std::vector<orderbook::lobster::Message> messages;
    orderbook::lobster::for_each_message(argv[1],
                                          [&](const orderbook::lobster::Message& m) { messages.push_back(m); });

    if (messages.size() <= kWarmupMessages) {
        std::cerr << "not enough messages (" << messages.size() << ") to warm up and measure\n";
        return EXIT_FAILURE;
    }

    orderbook::OrderBook book;
    for (std::size_t i = 0; i < kWarmupMessages; ++i) {
        orderbook::lobster::apply(book, messages[i]);
    }

    const std::size_t measured_count = messages.size() - kWarmupMessages;
    std::vector<std::int64_t> all_ns;
    std::vector<std::int64_t> new_ns;
    std::vector<std::int64_t> cancel_ns;
    std::vector<std::int64_t> delete_ns;
    std::vector<std::int64_t> execute_ns;
    all_ns.reserve(measured_count);

    const auto wall_start = Clock::now();
    for (std::size_t i = kWarmupMessages; i < messages.size(); ++i) {
        const orderbook::lobster::Message& message = messages[i];

        const auto op_start = Clock::now();
        orderbook::lobster::apply(book, message);
        const auto op_end = Clock::now();

        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(op_end - op_start).count();
        all_ns.push_back(ns);
        switch (message.type) {
            case orderbook::lobster::EventType::New:
                new_ns.push_back(ns);
                break;
            case orderbook::lobster::EventType::PartialCancel:
                cancel_ns.push_back(ns);
                break;
            case orderbook::lobster::EventType::Delete:
                delete_ns.push_back(ns);
                break;
            case orderbook::lobster::EventType::ExecuteVisible:
                execute_ns.push_back(ns);
                break;
            case orderbook::lobster::EventType::ExecuteHidden:
            case orderbook::lobster::EventType::Cross:
            case orderbook::lobster::EventType::Halt:
                break;
        }
    }
    const auto wall_end = Clock::now();

    const double seconds = std::chrono::duration<double>(wall_end - wall_start).count();
    const double throughput = static_cast<double>(measured_count) / seconds;

    std::cout << messages.size() << " messages (" << kWarmupMessages << " warmup, " << measured_count
               << " measured)\n";
    std::cout << "wall time: " << seconds << "s, throughput: " << static_cast<std::int64_t>(throughput)
               << " msg/s\n\n";

    // Overall per-message latency (§3 Phase 5: "per-message latency"), plus
    // a breakdown by event type — later Phase 6 optimizations target
    // specific operations (e.g. O(1) cancel), so knowing which op's latency
    // moved is what makes attribution possible (see CLAUDE.md).
    report("overall", all_ns);
    report("New", new_ns);
    report("PartialCancel", cancel_ns);
    report("Delete", delete_ns);
    report("ExecuteVisible", execute_ns);

    return EXIT_SUCCESS;
}
