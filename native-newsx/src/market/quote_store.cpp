#include "quote_store.h"

namespace newsx::market {

bool QuoteStore::set_trading_day(std::string day) {
    if (day.size() != 8 || day <= trading_day_) return false;
    for (char c : day) if (c < '0' || c > '9') return false;
    trading_day_ = std::move(day);
    for (auto& [_, quote] : quotes_) {
        const auto symbol = quote.symbol;
        const auto name = quote.name;
        quote = Quote{};
        quote.symbol = symbol;
        quote.name = name;
        quote.trading_day = trading_day_;
    }
    return true;
}

bool QuoteStore::update(Quote quote) {
    if (quote.symbol.empty()) return false;
    if (!quote.trading_day.empty() && !trading_day_.empty() && quote.trading_day < trading_day_) return false;
    if (!quote.trading_day.empty()) (void)set_trading_day(quote.trading_day);
    auto existing = quotes_.find(quote.symbol);
    if (existing != quotes_.end() && existing->second.trading_day == quote.trading_day &&
        existing->second.price && !quote.price) return false;
    if (quote.trading_day.empty()) quote.trading_day = trading_day_;
    if (existing != quotes_.end() && existing->second == quote) return false;
    quotes_[quote.symbol] = std::move(quote);
    return true;
}

void QuoteStore::erase(const std::string& symbol) { quotes_.erase(symbol); }

std::vector<Quote> QuoteStore::ordered(const std::vector<std::string>& symbols) const {
    std::vector<Quote> result;
    result.reserve(symbols.size());
    for (const auto& symbol : symbols) {
        if (auto it = quotes_.find(symbol); it != quotes_.end()) result.push_back(it->second);
    }
    return result;
}

} // namespace newsx::market
