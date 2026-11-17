#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QTemporaryFile>
#include <QJsonArray>

#include <map>
#include <memory>
#include <vector>

class QNetworkReply;

namespace newsx {

class MediaService final : public QObject {
    Q_OBJECT
public:
    explicit MediaService(QObject* parent = nullptr);
    ~MediaService() override;
    void prepare_video(const QString& source);
    void cancel();
    void prepare_images(const QJsonArray& posts);
    void cancel_images();

signals:
    void video_ready(QString source);
    void video_failed(QString message);
    void image_ready(QString original, QString local_source);

private:
    QNetworkAccessManager network_;
    QNetworkReply* reply_ = nullptr;
    std::unique_ptr<QTemporaryFile> file_;
    QString requested_url_;
    qint64 bytes_written_ = 0;
    quint64 token_ = 0;
    quint64 image_token_ = 0;
    qint64 image_bytes_ = 0;
    std::map<QString, std::shared_ptr<QTemporaryFile>> image_files_;
    std::vector<QNetworkReply*> image_replies_;
};

} // namespace newsx
