#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <functional>

class QWidget;

class ReleaseUpdate final : public QObject {
    Q_OBJECT
public:
    explicit ReleaseUpdate(QWidget* window);
    void check(std::function<void(const QString&, bool)> finished);

signals:
    void downloadProgress(qint64 received, qint64 total);

private:
    QWidget* window;
    QNetworkAccessManager network;
    void download(const QString& tag, const QString& asset, const QString& target,
                  qint64 size, const QByteArray& digest, bool appImage,
                  std::function<void(const QString&, bool)> finished);
};
