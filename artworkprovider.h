#pragma once

#include <QQuickImageProvider>
#include <QMutex>
#include <QImage>
#include <QCache>
#include <QString>

class ArtworkProvider : public QQuickImageProvider
{
public:
    ArtworkProvider();

    QImage requestImage(
        const QString &id,
        QSize *size,
        const QSize &requestedSize) override;

private:
    QImage loadArtwork(const QString &uri,
                       const QSize &requestedSize);

    QImage placeholder(const QSize &size) const;

    QString cacheFilePath(const QString &uri,
                          const QSize &requestedSize) const;

    QMutex m_mutex;
    QCache<QString, QImage> m_memoryCache;
};
