#pragma once

#include "cache_store.h"
#include "market/tora_feed.h"
#include "media_service.h"
#include "news_service.h"
#include "settings.h"

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <cstdint>
#include <map>

namespace newsx {

class AppController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString theme READ theme NOTIFY themeChanged)
    Q_PROPERTY(double opacity READ opacity NOTIFY opacityChanged)
    Q_PROPERTY(bool alwaysOnTop READ always_on_top NOTIFY alwaysOnTopChanged)
    Q_PROPERTY(int hotRefreshMinutes READ hot_refresh_minutes NOTIFY hotRefreshMinutesChanged)
    Q_PROPERTY(int headlineRefreshMinutes READ headline_refresh_minutes NOTIFY headlineRefreshMinutesChanged)
    Q_PROPERTY(bool minimalMode READ minimal_mode NOTIFY minimalModeChanged)
    Q_PROPERTY(QString minimalCard READ minimal_card NOTIFY minimalCardChanged)
    Q_PROPERTY(int initialX READ initial_x CONSTANT)
    Q_PROPERTY(int initialY READ initial_y CONSTANT)
    Q_PROPERTY(int initialWidth READ initial_width CONSTANT)
    Q_PROPERTY(int initialHeight READ initial_height CONSTANT)
    Q_PROPERTY(QVariantList stocks READ stocks NOTIFY stocksChanged)
    Q_PROPERTY(QVariantList hotItems READ hot_items NOTIFY hotItemsChanged)
    Q_PROPERTY(QVariantList headlines READ headlines NOTIFY headlinesChanged)
    Q_PROPERTY(QVariantList posts READ posts NOTIFY postsChanged)
    Q_PROPERTY(QVariantMap commentThreads READ comment_threads NOTIFY commentThreadsChanged)
    Q_PROPERTY(bool postsLoading READ posts_loading NOTIFY postsLoadingChanged)
    Q_PROPERTY(bool postsHasMore READ posts_has_more NOTIFY postsHasMoreChanged)
    Q_PROPERTY(QString articleTitle READ article_title NOTIFY articleTitleChanged)
    Q_PROPERTY(QString articleBrief READ article_brief NOTIFY articleBriefChanged)
    Q_PROPERTY(QString articleSource READ article_source NOTIFY articleSourceChanged)
    Q_PROPERTY(QString articleTime READ article_time NOTIFY articleTimeChanged)
    Q_PROPERTY(QVariantList articleLeadImages READ article_lead_images NOTIFY articleLeadImagesChanged)
    Q_PROPERTY(QVariantList articleBlocks READ article_blocks NOTIFY articleBlocksChanged)
    Q_PROPERTY(QVariantList articleParagraphs READ article_paragraphs NOTIFY articleParagraphsChanged)
    Q_PROPERTY(QVariantList articleImages READ article_images NOTIFY articleImagesChanged)
    Q_PROPERTY(QString detailStatus READ detail_status NOTIFY detailStatusChanged)
    Q_PROPERTY(QString marketStatus READ market_status NOTIFY marketStatusChanged)
    Q_PROPERTY(QString hotStatus READ hot_status NOTIFY hotStatusChanged)
    Q_PROPERTY(QString headlineStatus READ headline_status NOTIFY headlineStatusChanged)
    Q_PROPERTY(QString hotOrigin READ hot_origin NOTIFY hotItemsChanged)
    Q_PROPERTY(QString front READ front NOTIFY frontChanged)
    Q_PROPERTY(QString videoSource READ video_source NOTIFY videoSourceChanged)
    Q_PROPERTY(QString videoStatus READ video_status NOTIFY videoStatusChanged)
    Q_PROPERTY(QString hotTime READ hot_time NOTIFY hotTimeChanged)
    Q_PROPERTY(QString headlineTime READ headline_time NOTIFY headlineTimeChanged)
    Q_PROPERTY(QString marketTime READ market_time NOTIFY marketTimeChanged)
    Q_PROPERTY(QVariantMap imageSources READ image_sources NOTIFY imageSourcesChanged)
public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    [[nodiscard]] QString theme() const { return settings_.theme; }
    [[nodiscard]] double opacity() const { return settings_.opacity; }
    [[nodiscard]] bool always_on_top() const { return settings_.always_on_top; }
    [[nodiscard]] int hot_refresh_minutes() const { return settings_.hot_refresh_minutes; }
    [[nodiscard]] int headline_refresh_minutes() const { return settings_.headline_refresh_minutes; }
    [[nodiscard]] bool minimal_mode() const { return settings_.minimal_mode; }
    [[nodiscard]] QString minimal_card() const { return settings_.minimal_card; }
    [[nodiscard]] int initial_x() const { return settings_.x; }
    [[nodiscard]] int initial_y() const { return settings_.y; }
    [[nodiscard]] int initial_width() const { return settings_.width; }
    [[nodiscard]] int initial_height() const { return settings_.height; }
    [[nodiscard]] QVariantList stocks() const { return stocks_; }
    [[nodiscard]] QVariantList hot_items() const { return hot_items_; }
    [[nodiscard]] QVariantList headlines() const { return headlines_; }
    [[nodiscard]] QVariantList posts() const { return posts_; }
    [[nodiscard]] QVariantMap comment_threads() const { return comment_threads_; }
    [[nodiscard]] bool posts_loading() const { return posts_loading_; }
    [[nodiscard]] bool posts_has_more() const { return posts_has_more_; }
    [[nodiscard]] QString article_title() const { return article_title_; }
    [[nodiscard]] QString article_brief() const { return article_brief_; }
    [[nodiscard]] QString article_source() const { return article_source_; }
    [[nodiscard]] QString article_time() const { return article_time_; }
    [[nodiscard]] QVariantList article_lead_images() const { return article_lead_images_; }
    [[nodiscard]] QVariantList article_blocks() const { return article_blocks_; }
    [[nodiscard]] QVariantList article_paragraphs() const { return article_paragraphs_; }
    [[nodiscard]] QVariantList article_images() const { return article_images_; }
    [[nodiscard]] QString detail_status() const { return detail_status_; }
    [[nodiscard]] QString market_status() const { return market_status_; }
    [[nodiscard]] QString hot_status() const { return hot_status_; }
    [[nodiscard]] QString headline_status() const { return headline_status_; }
    [[nodiscard]] QString hot_origin() const { return hot_origin_; }
    [[nodiscard]] QString front() const { return settings_.tora_front; }
    [[nodiscard]] QString video_source() const { return video_source_; }
    [[nodiscard]] QString video_status() const { return video_status_; }
    [[nodiscard]] QString hot_time() const { return hot_time_; }
    [[nodiscard]] QString headline_time() const { return headline_time_; }
    [[nodiscard]] QString market_time() const { return market_time_; }
    [[nodiscard]] QVariantMap image_sources() const { return image_sources_; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshHot();
    Q_INVOKABLE void refreshHeadlines();
    Q_INVOKABLE void setHotRefreshMinutes(int value);
    Q_INVOKABLE void setHeadlineRefreshMinutes(int value);
    Q_INVOKABLE void toggleTheme();
    Q_INVOKABLE void setOpacity(double value);
    Q_INVOKABLE void setAlwaysOnTop(bool value);
    Q_INVOKABLE void setMinimalMode(bool value);
    Q_INVOKABLE void setMinimalCard(const QString& value);
    Q_INVOKABLE void saveWindow(int x, int y, int width, int height);
    Q_INVOKABLE bool addStock(const QString& symbol);
    Q_INVOKABLE void removeStock(const QString& symbol);
    Q_INVOKABLE void toggleFollow(const QString& symbol);
    Q_INVOKABLE bool setFront(const QString& value);
    Q_INVOKABLE void openWeibo(const QString& keyword, const QString& search_url = {});
    Q_INVOKABLE void loadMoreWeibo();
    Q_INVOKABLE void loadComments(const QString& post_id);
    Q_INVOKABLE void openArticle(const QString& url);
    Q_INVOKABLE void openExternal(const QString& url);
    Q_INVOKABLE void prepareVideo(const QString& url);
    Q_INVOKABLE void cancelVideo();
    Q_INVOKABLE void cancelMedia();
    Q_INVOKABLE void requestIdleCleanup() { emit idleCleanupRequested(); }
    Q_INVOKABLE QVariantMap stock(const QString& symbol) const;

signals:
    void themeChanged();
    void opacityChanged();
    void alwaysOnTopChanged();
    void hotRefreshMinutesChanged();
    void headlineRefreshMinutesChanged();
    void minimalModeChanged();
    void minimalCardChanged();
    void stocksChanged();
    void hotItemsChanged();
    void headlinesChanged();
    void postsChanged();
    void commentThreadsChanged();
    void postsLoadingChanged();
    void postsHasMoreChanged();
    void articleTitleChanged();
    void articleBriefChanged();
    void articleSourceChanged();
    void articleTimeChanged();
    void articleLeadImagesChanged();
    void articleBlocksChanged();
    void articleParagraphsChanged();
    void articleImagesChanged();
    void detailStatusChanged();
    void marketStatusChanged();
    void hotStatusChanged();
    void headlineStatusChanged();
    void frontChanged();
    void videoSourceChanged();
    void videoStatusChanged();
    void hotTimeChanged();
    void headlineTimeChanged();
    void marketTimeChanged();
    void imageSourcesChanged();
    void idleCleanupRequested();

private:
    void rebuild_stocks();
    void poll_market();
    void save_market_cache();
    void set_detail_status(QString value);
    static QVariantList from_json(const QJsonArray& rows);

    Settings settings_;
    CacheStore cache_;
    NewsService news_;
    MediaService media_;
    market::ToraFeed feed_;
    QTimer market_timer_;
    QTimer hot_timer_;
    QTimer headline_timer_;
    QVariantList stocks_;
    QVariantList hot_items_;
    QVariantList headlines_;
    QVariantList posts_;
    QVariantMap comment_threads_;
    QString post_keyword_;
    QString post_search_url_;
    int post_page_ = 0;
    int post_count_before_page_ = 0;
    bool posts_loading_ = false;
    bool posts_has_more_ = false;
    QString article_title_;
    QString article_brief_;
    QString article_source_;
    QString article_time_;
    QVariantList article_lead_images_;
    QVariantList article_blocks_;
    QVariantList article_paragraphs_;
    QVariantList article_images_;
    QString detail_status_;
    QString market_status_;
    QString hot_status_;
    QString headline_status_;
    QString hot_origin_;
    QString video_source_;
    QString video_status_;
    QString hot_time_;
    QString headline_time_;
    QString market_time_;
    QString market_cache_stamp_;
    QVariantMap image_sources_;
    bool market_dirty_ = false;
    std::map<std::string, market::Quote> quotes_;
    std::string market_day_;
    std::uint64_t feed_version_ = 0;
    int detail_token_ = 0;
    qint64 last_market_save_ = 0;
};

} // namespace newsx
