#include "media_service.h"

#include <QBuffer>
#include <QDir>
#include <QImage>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QSet>
#include <QStringList>
#include <QUrl>

#include <algorithm>

namespace newsx {
namespace {

bool is_supported_image_host(const QUrl& url) {
    const auto host = url.host().toLower();
    return host == QStringLiteral("sinaimg.cn") || host.endsWith(QStringLiteral(".sinaimg.cn")) ||
           host == QStringLiteral("cls.cn") || host.endsWith(QStringLiteral(".cls.cn"));
}

QString referer_for(const QUrl& url) {
    const auto host = url.host().toLower();
    if (host == QStringLiteral("cls.cn") || host.endsWith(QStringLiteral(".cls.cn")))
        return QStringLiteral("https://www.cls.cn/");
    return QStringLiteral("https://weibo.com/");
}

// Weibo serves avatars as squares; crop them to a circle here so the QML side can
// draw a plain Image instead of masking it with a layer effect per row.
QByteArray circular_avatar(const QByteArray& bytes) {
    QImage image;
    if (!image.loadFromData(bytes)) return {};
    const int side = std::min(image.width(), image.height());
    if (side < 2) return {};
    QImage square = image.copy((image.width() - side) / 2, (image.height() - side) / 2, side, side)
                        .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (square.isNull()) return {};
    // DestinationIn only touches painted pixels, so the mask has to cover the whole
    // square (transparent corners, opaque disc) instead of just drawing an ellipse.
    QImage mask(side, side, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    QPainter mask_painter(&mask);
    mask_painter.setRenderHint(QPainter::Antialiasing, true);
    mask_painter.setPen(Qt::NoPen);
    mask_painter.setBrush(Qt::white);
    mask_painter.drawEllipse(QRect(0, 0, side, side));
    mask_painter.end();
    QPainter painter(&square);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.drawImage(0, 0, mask);
    painter.end();
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !square.save(&buffer, "PNG")) return {};
    return png;
}

} // namespace

MediaService::MediaService(QObject* parent) : QObject(parent) {}
MediaService::~MediaService() { cancel(); cancel_images(); }

void MediaService::cancel_images() {
    ++image_token_;
    pending_images_.clear();
    scheduled_images_.clear();
    auto replies = std::move(image_replies_);
    image_replies_.clear();
    for (auto* reply : replies) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    image_files_.clear();
}

void MediaService::prepare_images(const QJsonArray& posts) {
    // Avatars come first: they are tiny yet every row needs one, while a single
    // post can eat the whole budget with its pictures. A missing avatar is far
    // more visible than a missing picture.
    QSet<QString> avatars;
    QStringList queue;
    QSet<QString> queued;
    for (const auto& post_value : posts) {
        const auto avatar = post_value.toObject().value(QStringLiteral("avatar")).toString();
        if (!avatar.isEmpty()) avatars.insert(avatar);
    }
    for (const auto& avatar : avatars) {
        if (queued.contains(avatar)) continue;
        queued.insert(avatar);
        queue.append(avatar);
    }
    for (const auto& post_value : posts) {
        const auto post = post_value.toObject();
        QStringList rest;
        for (const auto& image : post.value(QStringLiteral("images")).toArray())
            rest.append(image.toString());
        const auto poster = post.value(QStringLiteral("video_poster")).toString();
        if (!poster.isEmpty()) rest.append(poster);
        for (const auto& source : rest) {
            if (source.isEmpty() || queued.contains(source)) continue;
            queued.insert(source);
            queue.append(source);
        }
    }

    for (const auto& source : queue) {
        const QUrl url(source);
        if ((url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http")) ||
            !is_supported_image_host(url) || scheduled_images_.contains(source) ||
            image_files_.contains(source)) continue;
        scheduled_images_.insert(source);
        pending_images_.append({source, avatars.contains(source), 0});
    }
    start_images();
}

void MediaService::start_images() {
    // Keep transfers bounded without dropping later posts or exhausting a shared byte budget.
    while (image_replies_.size() < 4 && !pending_images_.isEmpty()) {
        const auto job = pending_images_.takeFirst();
        QUrl url(job.source);
        url.setScheme(QStringLiteral("https"));
        auto file = std::make_shared<QTemporaryFile>(
            QDir(QDir::tempPath()).filePath(QStringLiteral("newsx-image-XXXXXX.img")));
        if (!file->open()) { scheduled_images_.remove(job.source); continue; }
        const auto request_token = image_token_;
        QNetworkRequest request(url);
        request.setTransferTimeout(20000);
        request.setRawHeader("User-Agent", "Mozilla/5.0");
        request.setRawHeader("Referer", referer_for(url).toUtf8());
        auto* reply = network_.get(request);
        reply->setReadBufferSize(512 * 1024);
        image_replies_.push_back(reply);
        auto bytes = std::make_shared<qint64>(0);
        const auto drain = [reply, file, bytes] {
            const auto chunk = reply->readAll();
            *bytes += chunk.size();
            if (*bytes > 20'000'000 || file->write(chunk) != chunk.size()) reply->abort();
        };
        connect(reply, &QNetworkReply::readyRead, this, drain);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, file, bytes, job, request_token, drain] {
            std::erase(image_replies_, reply);
            if (request_token != image_token_) { reply->deleteLater(); return; }
            drain();
            const bool downloaded = reply->error() == QNetworkReply::NoError &&
                *bytes > 0 && *bytes <= 20'000'000;
            reply->deleteLater();
            file->flush();
            file->seek(0);
            const auto data = downloaded ? file->readAll() : QByteArray{};
            QImage decoded;
            const bool valid = !data.isEmpty() && decoded.loadFromData(data);
            if (valid) {
                if (job.avatar) {
                    const auto round = circular_avatar(data);
                    if (!round.isEmpty()) {
                        file->resize(0);
                        file->seek(0);
                        file->write(round);
                        file->flush();
                    }
                }
                image_files_.emplace(job.source, file);
                emit image_ready(job.source, QUrl::fromLocalFile(file->fileName()).toString());
            } else if (job.attempt < 2) {
                pending_images_.append({job.source, job.avatar, job.attempt + 1});
            } else {
                scheduled_images_.remove(job.source);
            }
            start_images();
        });
    }
}

void MediaService::cancel() {
    ++token_;
    if (reply_) {
        disconnect(reply_, nullptr, this, nullptr);
        reply_->abort();
        reply_->deleteLater();
        reply_ = nullptr;
    }
    file_.reset();
    requested_url_.clear();
    bytes_written_ = 0;
}

void MediaService::prepare_video(const QString& source) {
    const QUrl url(source);
    if ((url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http")) ||
        url.host().isEmpty()) {
        emit video_failed(QStringLiteral("视频链接无效"));
        return;
    }
    if (requested_url_ == source && file_ && !reply_ && file_->size() > 0) {
        emit video_ready(QUrl::fromLocalFile(file_->fileName()).toString());
        return;
    }
    cancel();
    requested_url_ = source;
    file_ = std::make_unique<QTemporaryFile>(
        QDir(QDir::tempPath()).filePath(QStringLiteral("newsx-video-XXXXXX.mp4")));
    if (!file_->open()) {
        emit video_failed(QStringLiteral("无法创建视频临时文件"));
        return;
    }
    const auto request_token = token_;
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setRawHeader("User-Agent", "Mozilla/5.0");
    request.setRawHeader("Referer", "https://m.weibo.cn/");
    reply_ = network_.get(request);
    reply_->setReadBufferSize(1024 * 1024);
    auto* current = reply_;
    connect(current, &QNetworkReply::readyRead, this, [this, current, request_token] {
        if (request_token != token_ || current != reply_ || !file_) return;
        const auto bytes = current->readAll();
        bytes_written_ += bytes.size();
        if (bytes_written_ > 80'000'000 || file_->write(bytes) != bytes.size()) {
            current->abort();
        }
    });
    connect(current, &QNetworkReply::finished, this, [this, current, request_token] {
        if (request_token != token_ || current != reply_) { current->deleteLater(); return; }
        const auto error = current->error();
        const auto type = current->header(QNetworkRequest::ContentTypeHeader).toString();
        current->deleteLater();
        reply_ = nullptr;
        if (error != QNetworkReply::NoError || !file_ || bytes_written_ == 0 ||
            bytes_written_ > 80'000'000 || !type.startsWith(QStringLiteral("video/"))) {
            file_.reset();
            requested_url_.clear();
            emit video_failed(QStringLiteral("视频下载失败"));
            return;
        }
        file_->flush();
        emit video_ready(QUrl::fromLocalFile(file_->fileName()).toString());
    });
}

} // namespace newsx
