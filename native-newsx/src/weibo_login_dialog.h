#pragma once
#include <QDialog>
#include <memory>
class QWindow;
class QResizeEvent;
class QShowEvent;
class QHideEvent;
struct ICoreWebView2NewWindowRequestedEventArgs;
namespace newsx {
class WeiboSession;
class WeiboLoginDialog final : public QDialog {
public:
    WeiboLoginDialog(WeiboSession& session, QWindow* owner);
    ~WeiboLoginDialog() override;
    bool initialization_failed() const;
protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* object, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
private:
    struct State;
    std::shared_ptr<State> state_;
    void initialize();
    void collect_cookies();
    void open_popup(ICoreWebView2NewWindowRequestedEventArgs* args);
    void reload_page();
    void shutdown_browser();
    void restart_browser(bool manual = false);
    void navigate_login();
    void alternate_login();
};
}
