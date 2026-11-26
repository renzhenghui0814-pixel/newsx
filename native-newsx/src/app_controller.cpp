#include "app_controller.h"
#include "weibo_diagnostics.h"
#include "weibo_session.h"
#include "weibo_login_dialog.h"
#include <QPointer>

#include <QDateTime>
#include <QDesktopServices>
#include <QJsonObject>
#include <QMetaObject>
#include <QSet>
#include <QUrl>

#include <algorithm>

namespace newsx {
namespace {

QString price(std::optional<double> value) {
    return value ? QString::number(*value, 'f', 2) : QString{};
}

QString number(std::optional<double> value) {
    if (!value) return QStringLiteral("—");
    if (*value >= 1e8) return QString::number(*value / 1e8, 'f', 2) + QStringLiteral("亿");
    if (*value >= 1e4) return QString::number(*value / 1e4, 'f', 1) + QStringLiteral("万");
    return QString::number(*value, 'f', 0);
}

QString cache_time(const QJsonObject& record) {
    const auto raw = record.value(QStringLiteral("time")).toString();
    const auto parsed = QDateTime::fromString(raw, QStringLiteral("yyyyMMdd-HH:mm:ss"));
    return parsed.isValid() ? parsed.toString(QStringLiteral("MM-dd HH:mm")) : QString{};
}

QString format_article_time(const QVariant& value) {
    bool numeric = false;
    auto seconds = value.toString().toLongLong(&numeric);
    if (numeric && seconds > 0) {
        if (seconds > 10'000'000'000LL) seconds /= 1000;
        return QDateTime::fromSecsSinceEpoch(seconds).toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    }
    const auto parsed = QDateTime::fromString(value.toString(), Qt::ISODate);
    return parsed.isValid() ? parsed.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString{};
}

QVariantList sorted_headlines(QVariantList rows) {
    std::stable_sort(rows.begin(), rows.end(), [](const QVariant& left, const QVariant& right) {
        return left.toMap().value(QStringLiteral("published_at")).toLongLong()
            > right.toMap().value(QStringLiteral("published_at")).toLongLong();
    });
    return rows;
}

QVariantMap level(const market::Level& value) {
    return {{QStringLiteral("price"), price(value.price)},
            {QStringLiteral("volume"), number(value.volume)},
            {QStringLiteral("volumeValue"), value.volume.value_or(0)}};
}

QVariantMap quote_map(const market::Quote& quote, bool followed) {
    QVariantList bids, asks;
    for (const auto& entry : quote.bids) bids.append(level(entry));
    for (const auto& entry : quote.asks) asks.append(level(entry));
    const auto name = QString::fromLocal8Bit(quote.name.c_str());
    return {{QStringLiteral("symbol"), QString::fromStdString(quote.symbol)},
            {QStringLiteral("name"), name.isEmpty() ? QString::fromStdString(quote.symbol) : name},
            {QStringLiteral("price"), price(quote.price)},
            {QStringLiteral("change"), quote.change_percent
                ? (*quote.change_percent >= 0 ? QStringLiteral("+") : QString{})
                  + QString::number(*quote.change_percent, 'f', 2) + QStringLiteral("%") : QString{}},
            {QStringLiteral("changeValue"), quote.change_percent.value_or(0)},
            {QStringLiteral("followed"), followed},
            {QStringLiteral("volume"), number(quote.volume)},
            {QStringLiteral("turnover"), number(quote.turnover)},
            {QStringLiteral("open"), price(quote.open_price)},
            {QStringLiteral("close"), price(quote.close_price)},
            {QStringLiteral("high"), price(quote.high_price)},
            {QStringLiteral("low"), price(quote.low_price)},
            {QStringLiteral("upper"), price(quote.upper_limit)},
            {QStringLiteral("lower"), price(quote.lower_limit)},
            {QStringLiteral("bids"), bids}, {QStringLiteral("asks"), asks}};
}

std::optional<double> json_number(const QJsonValue& value) {
    if (value.isDouble()) return value.toDouble();
    return std::nullopt;
}

market::Quote cached_quote(const QJsonObject& row) {
    market::Quote quote;
    quote.symbol = row.value(QStringLiteral("symbol")).toString().toStdString();
    quote.name = row.value(QStringLiteral("name")).toString().toLocal8Bit().toStdString();
    quote.trading_day = row.value(QStringLiteral("trading_day")).toString().toStdString();
    bool ok = false;
    const auto cached_price = row.value(QStringLiteral("price")).toString().toDouble(&ok);
    if (ok) quote.price = cached_price;
    const auto change = row.value(QStringLiteral("change_percent")).toString().toDouble(&ok);
    if (ok) quote.change_percent = change;
    quote.volume = json_number(row.value(QStringLiteral("volume")));
    quote.turnover = json_number(row.value(QStringLiteral("turnover")));
    quote.open_price = json_number(row.value(QStringLiteral("open_price")));
    quote.high_price = json_number(row.value(QStringLiteral("high_price")));
    quote.low_price = json_number(row.value(QStringLiteral("low_price")));
    quote.close_price = json_number(row.value(QStringLiteral("close_price")));
    quote.upper_limit = json_number(row.value(QStringLiteral("upper_limit")));
    quote.lower_limit = json_number(row.value(QStringLiteral("lower_limit")));
    const auto bids = row.value(QStringLiteral("bid_levels")).toArray();
    const auto asks = row.value(QStringLiteral("ask_levels")).toArray();
    for (int i = 0; i < 5; ++i) {
        if (i < bids.size()) {
            const auto values = bids[i].toArray();
            if (values.size() >= 2) quote.bids[i] = {json_number(values[0]), json_number(values[1])};
        }
        if (i < asks.size()) {
            const auto values = asks[i].toArray();
            if (values.size() >= 2) quote.asks[i] = {json_number(values[0]), json_number(values[1])};
        }
    }
    return quote;
}

QJsonObject quote_json(const market::Quote& quote) {
    QJsonObject row{{QStringLiteral("symbol"), QString::fromStdString(quote.symbol)},
                    {QStringLiteral("name"), QString::fromLocal8Bit(quote.name.c_str())},
                    {QStringLiteral("price"), price(quote.price)},
                    {QStringLiteral("change_percent"), quote.change_percent
                        ? QString::number(*quote.change_percent, 'f', 2) : QString{}},
                    {QStringLiteral("trading_day"), QString::fromStdString(quote.trading_day)}};
    const auto add = [&row](const char* key, const std::optional<double>& value) {
        row.insert(QString::fromLatin1(key), value ? QJsonValue(*value) : QJsonValue::Null);
    };
    add("volume", quote.volume);
    add("turnover", quote.turnover);
    add("open_price", quote.open_price);
    add("high_price", quote.high_price);
    add("low_price", quote.low_price);
    add("close_price", quote.close_price);
    add("upper_limit", quote.upper_limit);
    add("lower_limit", quote.lower_limit);
    QJsonArray bids, asks;
    for (int i = 0; i < 5; ++i) {
        bids.append(QJsonArray{quote.bids[i].price ? QJsonValue(*quote.bids[i].price) : QJsonValue::Null,
                               quote.bids[i].volume ? QJsonValue(*quote.bids[i].volume) : QJsonValue::Null});
        asks.append(QJsonArray{quote.asks[i].price ? QJsonValue(*quote.asks[i].price) : QJsonValue::Null,
                               quote.asks[i].volume ? QJsonValue(*quote.asks[i].volume) : QJsonValue::Null});
    }
    row.insert(QStringLiteral("bid_levels"), bids);
    row.insert(QStringLiteral("ask_levels"), asks);
    return row;
}

} // namespace

QVariantList AppController::from_json(const QJsonArray& rows) {
    QVariantList result;
    result.reserve(rows.size());
    for (const auto& row : rows) result.append(row.toObject().toVariantMap());
    return result;
}

AppController::AppController(QObject* parent)
    : QObject(parent), settings_(load_settings()), news_(this), media_(this),
      feed_([this](std::string value) {
          QMetaObject::invokeMethod(this, [this, value = std::move(value)] {
              market_status_ = QString::fromUtf8(value);
              emit marketStatusChanged();
          }, Qt::QueuedConnection);
      }) {
    const auto stock_cache = cache_.load(QStringLiteral("stocks"));
    market_time_ = cache_time(stock_cache);
    market_cache_stamp_ = stock_cache.value(QStringLiteral("time")).toString();
    market_day_ = stock_cache.value(QStringLiteral("trading_day")).toString().toStdString();
    for (const auto& item : stock_cache.value(QStringLiteral("items")).toArray()) {
        auto quote = cached_quote(item.toObject());
        if (!quote.symbol.empty() && (!quote.price || quote.trading_day == market_day_))
            quotes_[quote.symbol] = std::move(quote);
    }
    const auto hot_cache = cache_.load(QStringLiteral("weibo"));
    const auto headline_cache = cache_.load(QStringLiteral("cls"));
    hot_items_ = from_json(hot_cache.value(QStringLiteral("items")).toArray());
    headlines_ = sorted_headlines(from_json(headline_cache.value(QStringLiteral("items")).toArray()));
    hot_origin_ = hot_cache.value(QStringLiteral("origin")).toString();
    hot_time_ = cache_time(hot_cache);
    headline_time_ = cache_time(headline_cache);
    rebuild_stocks();

    connect(&news_, &NewsService::hot_ready, this, [this](const QJsonArray& rows, const QString& origin) {
        hot_items_ = from_json(rows);
        hot_origin_ = origin;
        hot_time_ = QDateTime::currentDateTime().toString(QStringLiteral("MM-dd HH:mm"));
        hot_status_ = QStringLiteral("刷新完成");
        emit hotItemsChanged();
        emit hotTimeChanged();
        emit hotStatusChanged();
        cache_.save(QStringLiteral("weibo"), rows, {}, {}, origin);
    });
    connect(&news_, &NewsService::hot_failed, this, [this](const QString& error) {
        hot_status_ = hot_items_.isEmpty() ? error : QStringLiteral("刷新失败 · 已显示缓存");
        emit hotStatusChanged();
    });
    connect(&news_, &NewsService::headlines_ready, this, [this](const QJsonArray& rows) {
        headlines_ = sorted_headlines(from_json(rows));
        headline_time_ = QDateTime::currentDateTime().toString(QStringLiteral("MM-dd HH:mm"));
        headline_status_ = QStringLiteral("刷新完成");
        emit headlinesChanged();
        emit headlineTimeChanged();
        emit headlineStatusChanged();
        cache_.save(QStringLiteral("cls"), rows);
    });
    connect(&news_, &NewsService::headlines_failed, this, [this](const QString& error) {
        headline_status_ = headlines_.isEmpty() ? error : QStringLiteral("刷新失败 · 已显示缓存");
        emit headlineStatusChanged();
    });
    connect(&news_, &NewsService::posts_ready, this,
            [this](int token, int page, const QJsonArray& rows, bool final, bool has_more, const QString& status) {
        if (token != detail_token_ || page != post_page_ + 1 || !final) return;
        bool added = false;
        int new_count = 0;
        for (const auto& value : from_json(rows)) {
            const auto url = value.toMap().value(QStringLiteral("url")).toString();
            const auto id = value.toMap().value(QStringLiteral("id")).toString();
            const auto same_post = [&id, &url](const QVariant& existing) {
                const auto row = existing.toMap();
                return !id.isEmpty() ? row.value(QStringLiteral("id")).toString() == id
                    : row.value(QStringLiteral("url")).toString() == url;
            };
            bool found = false;
            for (auto& existing : posts_) {
                if (same_post(existing)) {
                    if (!value.toMap().contains(QStringLiteral("search_source"))) existing = value;
                    found = true;
                    break;
                }
            }
            if (!found) {
                for (auto& existing : pending_posts_) {
                    if (same_post(existing)) {
                        if (!value.toMap().contains(QStringLiteral("search_source"))) existing = value;
                        found = true;
                        break;
                    }
                }
            }
            if (!found) { pending_posts_.append(value); added = true; ++new_count; }
        }
        log_weibo_page(QStringLiteral("page_merged"), {
            {QStringLiteral("token"), token}, {QStringLiteral("page"), page},
            {QStringLiteral("received"), rows.size()}, {QStringLiteral("new_count"), new_count},
            {QStringLiteral("displayed"), posts_.size()}, {QStringLiteral("pending"), pending_posts_.size()},
            {QStringLiteral("stop_reason"), added ? QString{} : rows.isEmpty()
                ? QStringLiteral("parsed_page_empty") : QStringLiteral("page_all_duplicates")}});
        post_page_ = page;
        post_source_has_more_ = added && has_more;
        post_completion_status_ = status;
        posts_loading_ = false;
        set_detail_status(added ? QString{} : QStringLiteral("本次未返回更多微博"));
        append_post_batch();
        emit postsLoadingChanged();
    });
    connect(&news_, &NewsService::posts_failed, this, [this](int token, int page, const QString& error) {
        if (token != detail_token_ || page != post_page_ + 1) return;
        // A failed request is retryable; keep the current page and existing posts.
        posts_loading_ = false;
        emit postsLoadingChanged();
        log_weibo_page(QStringLiteral("page_failed"), {
            {QStringLiteral("token"), token}, {QStringLiteral("page"), page},
            {QStringLiteral("error"), error}, {QStringLiteral("displayed"), posts_.size()}});
        set_detail_status(error);
    });
    connect(&news_, &NewsService::comments_ready, this,
            [this](int token, const QString& post_id, int page,
                   const QJsonArray& rows, bool has_more) {
        if (token != detail_token_) return;
        auto thread = comment_threads_.value(post_id).toMap();
        auto items = page == 1 ? QVariantList{} : thread.value(QStringLiteral("items")).toList();
        QSet<QString> seen;
        for (const auto& item : items)
            seen.insert(item.toMap().value(QStringLiteral("id")).toString());
        for (const auto& item : from_json(rows)) {
            const auto id = item.toMap().value(QStringLiteral("id")).toString();
            if (id.isEmpty() || seen.contains(id)) continue;
            seen.insert(id);
            items.append(item);
        }
        thread.insert(QStringLiteral("loading"), false);
        thread.insert(QStringLiteral("items"), items);
        thread.insert(QStringLiteral("page"), page);
        thread.insert(QStringLiteral("hasMore"), has_more && (page == 1 || !rows.isEmpty()));
        thread.insert(QStringLiteral("error"), QString{});
        comment_threads_.insert(post_id, thread);
        media_.prepare_images(rows);
        emit commentThreadsChanged();
    });
    connect(&news_, &NewsService::comments_failed, this,
            [this](int token, const QString& post_id, int page, const QString& error) {
        if (token != detail_token_) return;
        auto thread = comment_threads_.value(post_id).toMap();
        thread.insert(QStringLiteral("loading"), false);
        thread.insert(QStringLiteral("hasMore"), false);
        thread.insert(QStringLiteral("error"), error);
        if (page == 1) thread.insert(QStringLiteral("items"), QVariantList{});
        comment_threads_.insert(post_id, thread);
        emit commentThreadsChanged();
    });
    connect(&news_, &NewsService::article_ready, this, [this](int token, const QJsonObject& article) {
        if (token != detail_token_) return;
        article_title_ = article.value(QStringLiteral("title")).toString();
        article_brief_ = article.value(QStringLiteral("brief")).toString();
        const auto detail_source = article.value(QStringLiteral("source")).toString().trimmed();
        if (!detail_source.isEmpty()) article_source_ = detail_source;
        const auto detail_time = format_article_time(article.value(QStringLiteral("published_at")).toVariant());
        if (!detail_time.isEmpty()) article_time_ = detail_time;
        QVariantList lead_images;
        for (const auto& value : article.value(QStringLiteral("lead_images")).toArray())
            lead_images.append(value.toString());
        article_lead_images_ = lead_images;
        QVariantList blocks;
        for (const auto& value : article.value(QStringLiteral("blocks")).toArray())
            blocks.append(value.toObject().toVariantMap());
        article_blocks_ = blocks;
        QVariantList paragraphs;
        for (const auto& value : article.value(QStringLiteral("paragraphs")).toArray())
            paragraphs.append(value.toString());
        article_paragraphs_ = paragraphs;
        QVariantList images;
        QJsonArray image_rows;
        for (const auto& value : article.value(QStringLiteral("images")).toArray()) {
            const auto url = value.toString();
            if (url.isEmpty()) continue;
            images.append(url);
            image_rows.append(QJsonObject{{QStringLiteral("images"), QJsonArray{url}}});
        }
        article_images_ = images;
        emit articleTitleChanged();
        emit articleBriefChanged();
        emit articleSourceChanged();
        emit articleTimeChanged();
        emit articleLeadImagesChanged();
        emit articleBlocksChanged();
        emit articleParagraphsChanged();
        emit articleImagesChanged();
        if (!image_rows.isEmpty()) media_.prepare_images(image_rows);
        set_detail_status({});
    });
    connect(&news_, &NewsService::article_failed, this, [this](int token, const QString& error) {
        if (token == detail_token_) set_detail_status(error);
    });
    connect(&media_, &MediaService::video_ready, this, [this](const QString& source) {
        video_source_ = source;
        video_status_.clear();
        emit videoSourceChanged();
        emit videoStatusChanged();
    });
    connect(&media_, &MediaService::video_failed, this, [this](const QString& message) {
        video_status_ = message;
        emit videoStatusChanged();
    });
    connect(&media_, &MediaService::image_ready, this, [this](const QString& original,
                                                              const QString& local_source) {
        image_sources_.insert(original, local_source);
        emit imageSourcesChanged();
    });

    market_status_ = settings_.tora_front.isEmpty() ? QStringLiteral("站点未配置") : QStringLiteral("站点连接中");
    if (!settings_.tora_front.isEmpty()) {
        std::vector<std::string> symbols;
        for (const auto& item : settings_.symbols) symbols.push_back(item.symbol.toStdString());
        feed_.start(settings_.tora_front.toStdString(), std::move(symbols));
    }
    market_timer_.setInterval(200);
    connect(&market_timer_, &QTimer::timeout, this, &AppController::poll_market);
    market_timer_.start();
    hot_timer_.setInterval(settings_.hot_refresh_minutes * 60 * 1000);
    connect(&hot_timer_, &QTimer::timeout, this, &AppController::refreshHot);
    hot_timer_.start();
    headline_timer_.setInterval(settings_.headline_refresh_minutes * 60 * 1000);
    connect(&headline_timer_, &QTimer::timeout, this, &AppController::refreshHeadlines);
    headline_timer_.start();
    connect(news_.session(), &WeiboSession::statusChanged, this, &AppController::weiboLoginStatusChanged);
    connect(news_.session(), &WeiboSession::userNameChanged, this, &AppController::weiboUserNameChanged);
    connect(news_.session(), &WeiboSession::changed, this, [this] {
        emit weiboSessionChanged();
        refreshHot();
        if (!post_keyword_.isEmpty()) {
            const auto keyword = post_keyword_;
            const auto url = post_search_url_;
            openWeibo(keyword, url);
        }
    });
    QTimer::singleShot(250, this, &AppController::refresh);
}

bool AppController::weibo_logged_in() const { return news_.session()->logged_in(); }
QObject* AppController::weibo_session() const { return news_.session(); }
QString AppController::weibo_user_name() const { return news_.session()->user_name(); }
void AppController::logoutWeibo() { news_.session()->logout(); }
QString AppController::weibo_login_status() const { return news_.session()->status(); }
void AppController::openWeiboLogin(QWindow* owner) {
    // Create/activate native UI after the QML mouse handler has returned.
    const QPointer<QWindow> guarded_owner(owner);
    QTimer::singleShot(0, this, [this, guarded_owner] {
        // A hidden Qt native host may have destroyed its HWND. Never reuse a
        // controller belonging to a closed window; teardown runs on this queued turn.
        if (!weibo_login_dialog_ || !weibo_login_dialog_->isVisible() || weibo_login_dialog_->initialization_failed())
            weibo_login_dialog_ = std::make_unique<WeiboLoginDialog>(*news_.session(), guarded_owner.data());
        weibo_login_dialog_->show();
        weibo_login_dialog_->raise();
        weibo_login_dialog_->activateWindow();
    });
}

AppController::~AppController() {
    disconnect(&news_, nullptr, this, nullptr);
    disconnect(&media_, nullptr, this, nullptr);
    feed_.stop();
    if (market_dirty_) save_market_cache();
}

void AppController::rebuild_stocks() {
    stocks_.clear();
    for (const auto& item : settings_.symbols) {
        const auto symbol = item.symbol.toStdString();
        auto it = quotes_.find(symbol);
        market::Quote empty;
        empty.symbol = symbol;
        empty.name = symbol;
        stocks_.append(quote_map(it == quotes_.end() ? empty : it->second, item.followed));
    }
    emit stocksChanged();
}

void AppController::poll_market() {
    if (feed_.version() == feed_version_) return;
    const auto snapshot = feed_.snapshot();
    if (snapshot.version == feed_version_) return;
    feed_version_ = snapshot.version;
    if (!snapshot.trading_day.empty() && snapshot.trading_day > market_day_) {
        market_day_ = snapshot.trading_day;
        market_cache_stamp_ = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HH:mm:ss"));
        market_dirty_ = true;
        for (auto& [_, quote] : quotes_) {
            const auto symbol = quote.symbol;
            const auto name = quote.name;
            quote = market::Quote{};
            quote.symbol = symbol;
            quote.name = name;
            quote.trading_day = market_day_;
        }
        save_market_cache();
    }
    for (const auto& quote : snapshot.quotes) {
        if (quote.trading_day.empty() || quote.trading_day >= market_day_) {
            quotes_[quote.symbol] = quote;
            market_cache_stamp_ = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HH:mm:ss"));
            market_dirty_ = true;
        }
    }
    rebuild_stocks();
    if (!snapshot.updated_at.empty()) {
        market_time_ = QString::fromStdString(snapshot.updated_at);
        emit marketTimeChanged();
    }
    const auto now = QDateTime::currentSecsSinceEpoch();
    if (market_dirty_ && now - last_market_save_ >= 10) save_market_cache();
}

void AppController::save_market_cache() {
    QJsonArray rows;
    for (const auto& item : settings_.symbols) {
        const auto symbol = item.symbol.toStdString();
        if (const auto it = quotes_.find(symbol); it != quotes_.end()) rows.append(quote_json(it->second));
    }
    cache_.save(QStringLiteral("stocks"), rows, QString::fromStdString(market_day_),
                {}, {}, market_cache_stamp_);
    last_market_save_ = QDateTime::currentSecsSinceEpoch();
    market_dirty_ = false;
}

void AppController::refresh() {
    refreshHot();
    refreshHeadlines();
}

void AppController::refreshHot() {
    hot_status_ = QStringLiteral("正在刷新");
    emit hotStatusChanged();
    news_.refresh_hot(settings_.hot_url);
}

void AppController::refreshHeadlines() {
    headline_status_ = QStringLiteral("正在刷新");
    emit headlineStatusChanged();
    news_.refresh_headlines();
}

void AppController::setHotRefreshMinutes(int value) {
    const auto minutes = std::clamp(value, 1, 1440);
    if (settings_.hot_refresh_minutes == minutes) return;
    settings_.hot_refresh_minutes = minutes;
    hot_timer_.setInterval(minutes * 60 * 1000);
    save_settings(settings_);
    emit hotRefreshMinutesChanged();
}

void AppController::setHeadlineRefreshMinutes(int value) {
    const auto minutes = std::clamp(value, 1, 1440);
    if (settings_.headline_refresh_minutes == minutes) return;
    settings_.headline_refresh_minutes = minutes;
    headline_timer_.setInterval(minutes * 60 * 1000);
    save_settings(settings_);
    emit headlineRefreshMinutesChanged();
}

void AppController::toggleTheme() {
    settings_.theme = settings_.theme == QStringLiteral("dark") ? QStringLiteral("light") : QStringLiteral("dark");
    save_settings(settings_);
    emit themeChanged();
}

void AppController::setOpacity(double value) {
    const auto clamped = std::clamp(value, 0.45, 1.0);
    if (qFuzzyCompare(clamped, settings_.opacity)) return;
    settings_.opacity = clamped;
    save_settings(settings_);
    emit opacityChanged();
}

void AppController::setAlwaysOnTop(bool value) {
    if (settings_.always_on_top == value) return;
    settings_.always_on_top = value;
    save_settings(settings_);
    emit alwaysOnTopChanged();
}

void AppController::setMinimalMode(bool value) {
    setWindowMode(value ? QStringLiteral("minimal") : QStringLiteral("normal"));
}

void AppController::setWindowMode(const QString& value) {
    if (value != QStringLiteral("normal") && value != QStringLiteral("minimal") && value != QStringLiteral("hidden")) return;
    if (settings_.window_mode == value) return;
    const bool old_minimal = settings_.minimal_mode;
    settings_.window_mode = value;
    settings_.minimal_mode = value == QStringLiteral("minimal");
    save_settings(settings_);
    emit windowModeChanged();
    if (old_minimal != settings_.minimal_mode) emit minimalModeChanged();
}

void AppController::setMinimalCard(const QString& value) {
    if (value != QStringLiteral("stocks") && value != QStringLiteral("weibo") &&
        value != QStringLiteral("cls")) return;
    if (settings_.minimal_card == value) return;
    settings_.minimal_card = value;
    save_settings(settings_);
    emit minimalCardChanged();
}

void AppController::saveWindow(int x, int y, int width, int height) {
    settings_.x = x;
    settings_.y = y;
    settings_.width = std::clamp(width, 332, 900);
    settings_.height = std::clamp(height, 390, 1040);
    save_settings(settings_);
}

bool AppController::addStock(const QString& raw) {
    const auto symbol = market::parse_symbol(raw.toStdString());
    if (!symbol) return false;
    const auto value = QString::fromStdString(symbol->full());
    for (const auto& item : settings_.symbols) if (item.symbol == value) return false;
    if (!settings_.tora_front.isEmpty() && !feed_.add_symbol(symbol->full())) return false;
    settings_.symbols.push_back({value, true});
    save_settings(settings_);
    rebuild_stocks();
    return true;
}

void AppController::removeStock(const QString& symbol) {
    if (!settings_.tora_front.isEmpty() && !feed_.remove_symbol(symbol.toStdString())) return;
    settings_.symbols.erase(std::remove_if(settings_.symbols.begin(), settings_.symbols.end(),
        [&](const WatchItem& item) { return item.symbol == symbol; }), settings_.symbols.end());
    quotes_.erase(symbol.toStdString());
    save_settings(settings_);
    save_market_cache();
    rebuild_stocks();
}

void AppController::toggleFollow(const QString& symbol) {
    for (auto& item : settings_.symbols) {
        if (item.symbol == symbol) { item.followed = !item.followed; break; }
    }
    save_settings(settings_);
    rebuild_stocks();
}

bool AppController::setFront(const QString& value) {
    if (!value.isEmpty() && !market::valid_fronts(value.toStdString())) return false;
    feed_.stop();
    settings_.tora_front = value;
    save_settings(settings_);
    emit frontChanged();
    if (value.isEmpty()) {
        market_status_ = QStringLiteral("站点未配置");
        emit marketStatusChanged();
        return true;
    }
    std::vector<std::string> symbols;
    for (const auto& item : settings_.symbols) symbols.push_back(item.symbol.toStdString());
    return feed_.start(value.toStdString(), std::move(symbols));
}

void AppController::set_detail_status(QString value) {
    detail_status_ = std::move(value);
    emit detailStatusChanged();
}

void AppController::openWeibo(const QString& keyword, const QString& search_url) {
    ++detail_token_;
    post_keyword_ = keyword;
    post_search_url_ = search_url;
    post_page_ = 0;
    pending_posts_.clear();
    post_source_has_more_ = true;
    post_completion_status_.clear();
    posts_loading_ = true;
    posts_has_more_ = true;
    emit postsLoadingChanged();
    emit postsHasMoreChanged();
    media_.cancel_images();
    image_sources_.clear();
    emit imageSourcesChanged();
    posts_.clear();
    emit postsChanged();
    comment_threads_.clear();
    emit commentThreadsChanged();
    set_detail_status(QStringLiteral("正在获取微博…"));
    news_.load_weibo_posts(keyword, post_search_url_, detail_token_);
}

void AppController::append_post_batch() {
    QJsonArray images;
    const auto count = std::min<qsizetype>(10, pending_posts_.size());
    for (qsizetype i = 0; i < count; ++i) {
        const auto value = pending_posts_.takeFirst();
        posts_.append(value);
        images.append(QJsonObject::fromVariantMap(value.toMap()));
    }
    posts_has_more_ = !pending_posts_.isEmpty() || post_source_has_more_;
    if (!posts_has_more_ && !post_completion_status_.isEmpty()) set_detail_status(post_completion_status_);
    log_weibo_page(QStringLiteral("batch_displayed"), {
        {QStringLiteral("token"), detail_token_}, {QStringLiteral("page"), post_page_},
        {QStringLiteral("added"), count}, {QStringLiteral("displayed"), posts_.size()},
        {QStringLiteral("pending"), pending_posts_.size()}, {QStringLiteral("has_more"), posts_has_more_}});
    media_.prepare_images(images);
    emit postsChanged();
    emit postsHasMoreChanged();
}

void AppController::loadMoreWeibo() {
    if (post_keyword_.isEmpty() || posts_loading_ || !posts_has_more_) return;
    set_detail_status({});
    if (!pending_posts_.isEmpty()) {
        append_post_batch();
        return;
    }
    posts_loading_ = true;
    emit postsLoadingChanged();
    news_.load_more_weibo_posts(post_keyword_, post_search_url_, detail_token_, post_page_ + 1);
}

void AppController::loadComments(const QString& post_id) {
    if (post_id.isEmpty()) return;
    auto thread = comment_threads_.value(post_id).toMap();
    if (thread.value(QStringLiteral("loading")).toBool() ||
        (comment_threads_.contains(post_id) && !thread.value(QStringLiteral("hasMore")).toBool())) return;
    const int page = thread.value(QStringLiteral("page")).toInt() + 1;
    thread.insert(QStringLiteral("loading"), true);
    thread.insert(QStringLiteral("error"), QString{});
    if (page == 1) thread.insert(QStringLiteral("items"), QVariantList{});
    comment_threads_.insert(post_id, thread);
    emit commentThreadsChanged();
    news_.load_comments(post_id, detail_token_, page);
}

void AppController::openArticle(const QString& url) {
    ++detail_token_;
    post_keyword_.clear();
    post_search_url_.clear();
    article_source_.clear();
    article_time_.clear();
    for (const auto& value : headlines_) {
        const auto row = value.toMap();
        if (row.value(QStringLiteral("url")).toString() != url) continue;
        article_source_ = row.value(QStringLiteral("source")).toString().trimmed();
        article_time_ = format_article_time(row.value(QStringLiteral("published_at")));
        break;
    }
    if (article_source_.isEmpty()) article_source_ = QStringLiteral("财联社");
    article_title_.clear();
    article_brief_.clear();
    article_lead_images_.clear();
    article_blocks_.clear();
    article_paragraphs_.clear();
    article_images_.clear();
    emit articleTitleChanged();
    emit articleBriefChanged();
    emit articleSourceChanged();
    emit articleTimeChanged();
    emit articleLeadImagesChanged();
    emit articleBlocksChanged();
    emit articleParagraphsChanged();
    emit articleImagesChanged();
    set_detail_status(QStringLiteral("正在获取正文…"));
    news_.load_article(url, detail_token_);
}

void AppController::openExternal(const QString& url) {
    const QUrl target(url);
    if (target.scheme() == QStringLiteral("https") || target.scheme() == QStringLiteral("http"))
        QDesktopServices::openUrl(target);
}

void AppController::prepareVideo(const QString& url) {
    video_source_.clear();
    video_status_ = QStringLiteral("正在加载视频…");
    emit videoSourceChanged();
    emit videoStatusChanged();
    media_.prepare_video(url);
}

void AppController::cancelVideo() {
    media_.cancel();
    video_source_.clear();
    video_status_.clear();
    emit videoSourceChanged();
    emit videoStatusChanged();
}

void AppController::cancelMedia() {
    ++detail_token_;
    cancelVideo();
    media_.cancel_images();
    image_sources_.clear();
    emit imageSourcesChanged();
    if (!posts_.isEmpty()) {
        posts_.clear();
        emit postsChanged();
    }
    if (!comment_threads_.isEmpty()) {
        comment_threads_.clear();
        emit commentThreadsChanged();
    }
    if (!article_title_.isEmpty() || !article_brief_.isEmpty() || !article_blocks_.isEmpty()
            || !article_lead_images_.isEmpty()
            || !article_source_.isEmpty() || !article_time_.isEmpty()) {
        article_title_.clear();
        article_brief_.clear();
        article_source_.clear();
        article_time_.clear();
        article_lead_images_.clear();
        article_blocks_.clear();
        article_paragraphs_.clear();
        article_images_.clear();
        emit articleTitleChanged();
        emit articleBriefChanged();
        emit articleSourceChanged();
        emit articleTimeChanged();
        emit articleLeadImagesChanged();
        emit articleBlocksChanged();
        emit articleParagraphsChanged();
        emit articleImagesChanged();
    }
    post_keyword_.clear();
    post_search_url_.clear();
    post_page_ = 0;
    if (posts_loading_) { posts_loading_ = false; emit postsLoadingChanged(); }
    if (posts_has_more_) { posts_has_more_ = false; emit postsHasMoreChanged(); }
}

QVariantMap AppController::stock(const QString& symbol) const {
    for (const auto& row : stocks_) {
        const auto object = row.toMap();
        if (object.value(QStringLiteral("symbol")).toString() == symbol) return object;
    }
    return {};
}

} // namespace newsx
