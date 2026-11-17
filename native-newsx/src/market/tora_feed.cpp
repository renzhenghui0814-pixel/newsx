#include "tora_feed.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>

namespace newsx::market {
namespace {

std::optional<double> positive(double number) {
    if (std::isfinite(number) && number > 0 && number < 1e15) return number;
    return std::nullopt;
}

std::string now_time() {
    const auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_s(&local, &time);
    std::ostringstream result;
    result << std::put_time(&local, "%H:%M:%S");
    return result.str();
}

std::string trim_c_string(const char* data) {
    if (!data) return {};
    std::string result(data);
    while (!result.empty() && (result.back() == ' ' || result.back() == '\0')) result.pop_back();
    return result;
}

Quote from_data(const TORALEV1API::CTORATstpMarketDataField& data, const Symbol& symbol) {
    Quote quote;
    quote.symbol = symbol.full();
    quote.name = trim_c_string(data.SecurityName);
    if (quote.name.empty()) quote.name = quote.symbol;
    quote.trading_day = trim_c_string(data.TradingDay);
    quote.price = positive(data.LastPrice);
    if (quote.price && positive(data.PreClosePrice) && data.PreClosePrice < 1e10)
        quote.change_percent = (*quote.price / data.PreClosePrice - 1.0) * 100.0;
    quote.turnover = positive(data.Turnover);
    quote.volume = positive(static_cast<double>(data.Volume));
    quote.open_price = positive(data.OpenPrice);
    quote.high_price = positive(data.HighestPrice);
    quote.low_price = positive(data.LowestPrice);
    quote.close_price = positive(data.PreClosePrice);
    quote.upper_limit = positive(data.UpperLimitPrice);
    quote.lower_limit = positive(data.LowerLimitPrice);
    const std::array<double, 5> bid_prices{data.BidPrice1, data.BidPrice2, data.BidPrice3,
                                            data.BidPrice4, data.BidPrice5};
    const std::array<double, 5> ask_prices{data.AskPrice1, data.AskPrice2, data.AskPrice3,
                                            data.AskPrice4, data.AskPrice5};
    const std::array<double, 5> bid_volumes{static_cast<double>(data.BidVolume1),
        static_cast<double>(data.BidVolume2), static_cast<double>(data.BidVolume3),
        static_cast<double>(data.BidVolume4), static_cast<double>(data.BidVolume5)};
    const std::array<double, 5> ask_volumes{static_cast<double>(data.AskVolume1),
        static_cast<double>(data.AskVolume2), static_cast<double>(data.AskVolume3),
        static_cast<double>(data.AskVolume4), static_cast<double>(data.AskVolume5)};
    for (std::size_t i = 0; i < 5; ++i) {
        quote.bids[i] = {positive(bid_prices[i]), positive(bid_volumes[i])};
        quote.asks[i] = {positive(ask_prices[i]), positive(ask_volumes[i])};
    }
    return quote;
}

} // namespace

ToraFeed::ToraFeed(StatusHandler on_status) : on_status_(std::move(on_status)) {}
ToraFeed::~ToraFeed() { stop(); }

bool ToraFeed::start(const std::string& fronts, std::vector<std::string> symbols) {
    if (active_ || !valid_fronts(fronts)) return false;
    std::vector<Symbol> parsed;
    for (const auto& value : symbols) {
        auto symbol = parse_symbol(value);
        if (!symbol) return false;
        if (std::find(parsed.begin(), parsed.end(), *symbol) == parsed.end()) parsed.push_back(*symbol);
    }
    front_storage_.clear();
    std::size_t begin = 0;
    while (begin < fronts.size()) {
        const auto end = fronts.find(',', begin);
        std::string front = fronts.substr(begin, end == std::string::npos ? end : end - begin);
        const auto first = front.find_first_not_of(" \t");
        const auto last = front.find_last_not_of(" \t");
        front_storage_.push_back(front.substr(first, last - first + 1));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    {
        std::lock_guard lock(mutex_);
        watchlist_ = std::move(parsed);
        logged_in_ = false;
    }
    api_ = TORALEV1API::CTORATstpXMdApi::CreateTstpXMdApi();
    if (!api_) return false;
    active_ = true;
    api_->RegisterSpi(this);
    for (auto& front : front_storage_) api_->RegisterFront(front.data());
    report("正在连接行情前置");
    api_->Init();
    return true;
}

void ToraFeed::stop() {
    if (!active_.exchange(false)) return;
    if (api_) {
        api_->RegisterSpi(nullptr);
        api_->Release();
        api_ = nullptr;
    }
    std::lock_guard lock(mutex_);
    logged_in_ = false;
}

void ToraFeed::report(std::string message) const {
    if (on_status_) on_status_(std::move(message));
}

void ToraFeed::OnFrontConnected() {
    if (!active_) return;
    report("已连接，正在登录");
    TORALEV1API::CTORATstpReqUserLoginField login{};
    const int result = api_->ReqUserLogin(&login, 1);
    if (result != 0) report("登录请求失败：" + std::to_string(result));
}

void ToraFeed::OnFrontDisconnected(int reason) {
    {
        std::lock_guard lock(mutex_);
        logged_in_ = false;
    }
    if (active_) report("连接断开：" + std::to_string(reason) + "，等待重连");
}

void ToraFeed::OnRspUserLogin(TORALEV1API::CTORATstpRspUserLoginField* login,
                              TORALEV1API::CTORATstpRspInfoField* info, int) {
    if (!active_) return;
    if (info && info->ErrorID != 0) {
        report("登录失败：" + std::to_string(info->ErrorID));
        return;
    }
    std::vector<Symbol> symbols;
    {
        std::lock_guard lock(mutex_);
        logged_in_ = true;
        if (login && quotes_.set_trading_day(trim_c_string(login->TradingDay))) ++version_;
        symbols = watchlist_;
    }
    report("已登录，正在订阅");
    for (const auto& symbol : symbols) {
        if (const int result = change_subscription(symbol, true); result != 0)
            report(symbol.exchange + " 订阅失败：" + std::to_string(result));
    }
    report("实时行情已订阅");
}

void ToraFeed::OnRspSubMarketData(TORALEV1API::CTORATstpSpecificSecurityField*,
                                  TORALEV1API::CTORATstpRspInfoField* info) {
    if (active_ && info && info->ErrorID != 0)
        report("订阅失败：" + std::to_string(info->ErrorID));
}

void ToraFeed::OnRtnMarketData(TORALEV1API::CTORATstpMarketDataField* data) {
    if (!active_ || !data) return;
    const auto code = trim_c_string(data->SecurityID);
    std::lock_guard lock(mutex_);
    auto it = std::find_if(watchlist_.begin(), watchlist_.end(), [&](const Symbol& symbol) {
        return symbol.code == code && symbol.tora_exchange() == data->ExchangeID;
    });
    if (it == watchlist_.end()) return;
    if (quotes_.update(from_data(*data, *it))) {
        ++version_;
        updated_at_ = now_time();
    }
}

int ToraFeed::change_subscription(const Symbol& symbol, bool subscribe) {
    char code[7]{};
    std::copy_n(symbol.code.c_str(), 6, code);
    char* codes[] = {code};
    return subscribe ? api_->SubscribeMarketData(codes, 1, symbol.tora_exchange())
                     : api_->UnSubscribeMarketData(codes, 1, symbol.tora_exchange());
}

bool ToraFeed::add_symbol(const std::string& value) {
    auto symbol = parse_symbol(value);
    if (!symbol) return false;
    bool subscribe = false;
    {
        std::lock_guard lock(mutex_);
        if (std::find(watchlist_.begin(), watchlist_.end(), *symbol) != watchlist_.end()) return false;
        watchlist_.push_back(*symbol);
        subscribe = logged_in_;
    }
    if (subscribe && change_subscription(*symbol, true) != 0) {
        std::lock_guard lock(mutex_);
        std::erase(watchlist_, *symbol);
        return false;
    }
    return true;
}

bool ToraFeed::remove_symbol(const std::string& value) {
    auto symbol = parse_symbol(value);
    if (!symbol) return false;
    bool unsubscribe = false;
    {
        std::lock_guard lock(mutex_);
        if (std::find(watchlist_.begin(), watchlist_.end(), *symbol) == watchlist_.end()) return false;
        unsubscribe = logged_in_;
    }
    if (unsubscribe && change_subscription(*symbol, false) != 0) return false;
    {
        std::lock_guard lock(mutex_);
        std::erase(watchlist_, *symbol);
        quotes_.erase(symbol->full());
        ++version_;
    }
    return true;
}

Snapshot ToraFeed::snapshot() const {
    std::lock_guard lock(mutex_);
    Snapshot result;
    result.version = version_;
    std::vector<std::string> symbols;
    symbols.reserve(watchlist_.size());
    for (const auto& symbol : watchlist_) symbols.push_back(symbol.full());
    result.quotes = quotes_.ordered(symbols);
    result.trading_day = quotes_.trading_day();
    result.updated_at = updated_at_;
    return result;
}

std::uint64_t ToraFeed::version() const {
    std::lock_guard lock(mutex_);
    return version_;
}

} // namespace newsx::market
