#pragma once

#include "quote_store.h"
#include "symbol.h"

#include <TORATstpXMdApi.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace newsx::market {

struct Snapshot {
    std::uint64_t version = 0;
    std::vector<Quote> quotes;
    std::string trading_day;
    std::string updated_at;
};

class ToraFeed final : private TORALEV1API::CTORATstpXMdSpi {
public:
    using StatusHandler = std::function<void(std::string)>;

    explicit ToraFeed(StatusHandler on_status);
    ~ToraFeed();
    ToraFeed(const ToraFeed&) = delete;
    ToraFeed& operator=(const ToraFeed&) = delete;

    bool start(const std::string& fronts, std::vector<std::string> symbols);
    void stop();
    bool add_symbol(const std::string& symbol);
    bool remove_symbol(const std::string& symbol);
    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] std::uint64_t version() const;

private:
    void OnFrontConnected() override;
    void OnFrontDisconnected(int reason) override;
    void OnRspUserLogin(TORALEV1API::CTORATstpRspUserLoginField* login,
                        TORALEV1API::CTORATstpRspInfoField* info, int request_id) override;
    void OnRspSubMarketData(TORALEV1API::CTORATstpSpecificSecurityField* security,
                            TORALEV1API::CTORATstpRspInfoField* info) override;
    void OnRtnMarketData(TORALEV1API::CTORATstpMarketDataField* data) override;

    int change_subscription(const Symbol& symbol, bool subscribe);
    void report(std::string message) const;

    StatusHandler on_status_;
    TORALEV1API::CTORATstpXMdApi* api_ = nullptr;
    std::atomic<bool> active_{false};
    mutable std::mutex mutex_;
    bool logged_in_ = false;
    std::vector<Symbol> watchlist_;
    std::vector<std::string> front_storage_;
    QuoteStore quotes_;
    std::uint64_t version_ = 0;
    std::string updated_at_;
};

} // namespace newsx::market
