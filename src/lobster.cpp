#include "orderbook/lobster.hpp"

#include <array>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace orderbook::lobster {

namespace {

// Splits a 6-field CSV line into field views without allocating. LOBSTER
// message lines have no quoting/escaping to worry about.
std::array<std::string_view, 6> split_fields(const std::string& line) {
    std::array<std::string_view, 6> fields;
    std::size_t start = 0;
    for (std::size_t field_idx = 0; field_idx < 6; ++field_idx) {
        const std::size_t comma = (field_idx + 1 < 6) ? line.find(',', start) : line.size();
        if (comma == std::string::npos) {
            throw std::invalid_argument("lobster: malformed message line: " + line);
        }
        fields[field_idx] = std::string_view(line).substr(start, comma - start);
        start = comma + 1;
    }
    return fields;
}

// LOBSTER prices are dollars * 10000; this project's Price is cents, i.e.
// dollars * 100. The exchange's minimum tick is a cent, so this division is
// always exact for real data.
Price to_cents(long long lobster_price) {
    return static_cast<Price>(lobster_price / 100);
}

Side to_side(int direction) {
    if (direction == 1) {
        return Side::Buy;
    }
    if (direction == -1) {
        return Side::Sell;
    }
    throw std::invalid_argument("lobster: unrecognized direction " + std::to_string(direction));
}

} // namespace

Message parse_message_line(const std::string& line) {
    const std::array<std::string_view, 6> fields = split_fields(line);

    Message message;
    message.time_seconds = std::stod(std::string(fields[0]));
    message.type = static_cast<EventType>(std::stoi(std::string(fields[1])));
    message.order_id = std::stoll(std::string(fields[2]));
    message.size = std::stoll(std::string(fields[3]));
    message.price = to_cents(std::stoll(std::string(fields[4])));
    message.side = to_side(std::stoi(std::string(fields[5])));
    return message;
}

void for_each_message(const std::string& path, const std::function<void(const Message&)>& callback) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("lobster: could not open message file: " + path);
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        callback(parse_message_line(line));
    }
}

ApplyEffect apply(OrderBook& book, const Message& message) {
    ApplyEffect effect;
    const bool is_buy = message.side == Side::Buy;

    switch (message.type) {
        case EventType::New:
            book.add(Order{message.order_id, message.side, message.price, message.size});
            (is_buy ? effect.added_buy : effect.added_sell) = message.size;
            return effect;
        case EventType::PartialCancel:
            if (book.reduce(message.order_id, message.size)) {
                (is_buy ? effect.cancelled_buy : effect.cancelled_sell) = message.size;
            }
            return effect;
        case EventType::ExecuteVisible:
            if (book.reduce(message.order_id, message.size)) {
                (is_buy ? effect.filled_buy : effect.filled_sell) = message.size;
            }
            return effect;
        case EventType::Delete:
            if (auto removed = book.cancel(message.order_id)) {
                (is_buy ? effect.cancelled_buy : effect.cancelled_sell) = removed->qty;
            }
            return effect;
        case EventType::ExecuteHidden:
        case EventType::Cross:
        case EventType::Halt:
            return effect;
    }
    return effect;
}

} // namespace orderbook::lobster
