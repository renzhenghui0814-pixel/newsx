#pragma once

#include "quote.h"

#include <map>
#include <string>
#include <vector>

namespace newsx::market {

// All mutation happens under ToraFeed's lock. Kept Qt-free for deterministic tests.
class QuoteStore {
public:
    [[nodiscard]] bool set_trading_day(std::string day);
    [[nodiscard]] bool update(Quote quote);
    void erase(const std::string& symbol);
    [[nodiscard]] std::vector<Quote> ordered(const std::vector<std::string>& symbols) const;
    [[nodiscard]] const std::string& trading_day() const { return trading_day_; }

private:
    std::string trading_day_;
    std::map<std::string, Quote> quotes_;
};

} // namespace newsx::market
