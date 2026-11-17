#pragma once

#include <array>
#include <optional>
#include <string>

namespace newsx::market {

struct Level {
    std::optional<double> price;
    std::optional<double> volume;
    bool operator==(const Level&) const = default;
};

struct Quote {
    std::string symbol;
    std::string name;
    std::string trading_day;
    std::optional<double> price;
    std::optional<double> change_percent;
    std::optional<double> turnover;
    std::optional<double> volume;
    std::optional<double> open_price;
    std::optional<double> high_price;
    std::optional<double> low_price;
    std::optional<double> close_price;
    std::optional<double> upper_limit;
    std::optional<double> lower_limit;
    std::array<Level, 5> bids{};
    std::array<Level, 5> asks{};
    bool operator==(const Quote&) const = default;
};

} // namespace newsx::market
