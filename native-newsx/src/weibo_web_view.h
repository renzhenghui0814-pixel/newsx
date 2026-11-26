#pragma once
#include <QQuickItem>
#include <QPointer>
#include <QUrl>
#include <QColor>
#include <QtQml/qqmlregistration.h>
#include <memory>

class QFocusEvent;
struct ICoreWebView2NewWindowRequestedEventArgs;
namespace newsx {
class WeiboSession;
class WeiboWebView : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    // Supported sites share the native host, but keep separate browser profiles.
    Q_PROPERTY(QString site READ site WRITE set_site NOTIFY siteChanged)
    Q_PROPERTY(QUrl url READ url WRITE set_url NOTIFY urlChanged)
    Q_PROPERTY(QUrl currentUrl READ current_url NOTIFY currentUrlChanged)
    Q_PROPERTY(QObject* session READ session WRITE set_session NOTIFY sessionChanged)
    Q_PROPERTY(qreal zoomFactor READ zoom_factor WRITE set_zoom_factor NOTIFY zoomFactorChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool contentReady READ content_ready NOTIFY contentReadyChanged)
    Q_PROPERTY(bool loginRequired READ login_required NOTIFY loginRequiredChanged)
    Q_PROPERTY(bool canGoBack READ can_go_back NOTIFY canGoBackChanged)
    Q_PROPERTY(QString errorString READ error_string NOTIFY errorStringChanged)
    Q_PROPERTY(bool darkTheme READ dark_theme WRITE set_dark_theme NOTIFY appearanceChanged)
    Q_PROPERTY(bool hideSiteBack READ hide_site_back WRITE set_hide_site_back NOTIFY appearanceChanged)
    Q_PROPERTY(QColor backgroundColor READ background_color WRITE set_background_color NOTIFY appearanceChanged)
    Q_PROPERTY(bool loginOnly READ login_only WRITE set_login_only NOTIFY loginOnlyChanged)
public:
    explicit WeiboWebView(QQuickItem* parent = nullptr);
    ~WeiboWebView() override;
    QString site() const { return site_; }
    void set_site(const QString& site);
    QUrl url() const { return url_; }
    QUrl current_url() const { return current_url_; }
    QObject* session() const;
    qreal zoom_factor() const { return zoom_; }
    bool loading() const { return loading_; }
    bool content_ready() const { return content_ready_; }
    bool login_required() const { return login_required_; }
    bool can_go_back() const { return can_go_back_; }
    QString error_string() const { return error_; }
    bool dark_theme() const { return dark_; }
    bool hide_site_back() const { return hide_back_; }
    QColor background_color() const { return background_; }
    bool login_only() const { return login_only_; }
    void set_dark_theme(bool value);
    void set_hide_site_back(bool value);
    void set_background_color(const QColor& color);
    void set_login_only(bool value);
    void set_url(const QUrl& url);
    void set_session(QObject* session);
    void set_zoom_factor(qreal factor);
    Q_INVOKABLE bool isPointerOverWindow() const;
    Q_INVOKABLE void navigateTo(const QUrl& url);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void goBack();
signals:
    void siteChanged();
    void urlChanged();
    void currentUrlChanged();
    void sessionChanged();
    void zoomFactorChanged();
    void loadingChanged();
    void contentReadyChanged();
    void loginRequiredChanged();
    void canGoBackChanged();
    void errorStringChanged();
    void closeRequested();
    void appearanceChanged();
    void loginOnlyChanged();
protected:
    void componentComplete() override;
    void geometryChange(const QRectF& geometry, const QRectF& old) override;
    bool eventFilter(QObject* object, QEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
private:
    struct State;
    std::shared_ptr<State> state_;
    QPointer<WeiboSession> session_;
    QPointer<QQuickWindow> attached_window_;
    QString site_ = QStringLiteral("weibo");
    QUrl url_, current_url_;
    qreal zoom_ = 1;
    bool loading_ = false, can_go_back_ = false, rebuild_pending_ = false;
    bool content_ready_ = false, login_required_ = false;
    QString error_;
    bool dark_ = false, hide_back_ = false, login_only_ = false;
    QColor background_ = QColor(QStringLiteral("#eff3f6"));
    void attach_window();
    void schedule_open();
    void open();
    void shutdown();
    void update_geometry();
    void fail(const QString& message);
    void set_loading(bool value);
    void set_content_ready(bool value);
    void set_login_required(bool value);
    void show_login_required();
    void reveal_document(quint64 generation, int attempts);
    void finish_navigation();
    void navigate();
    void collect_cookies();
    void open_popup(ICoreWebView2NewWindowRequestedEventArgs* args);
    QString appearance_script() const;
    void apply_appearance();
};
}
