#pragma once
#include <QByteArray>
#include <QJsonArray>

namespace newsx {
struct DesktopWeiboPage {
    QJsonArray posts;
    bool recognized = false;
    bool has_more = false;
    bool login_required = false;
};
DesktopWeiboPage parse_desktop_weibo(const QByteArray& html);
}
