#include "artworkprovider.h"

#include <mpd/client.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QBuffer>
#include <QColor>
#include <QSize>
#include <QPainter>
#include <QStandardPaths>
#include <QMutexLocker>
#include <QImageReader>

namespace
{
QString decodedUri(const QString &id)
{
    const QByteArray encoded = id.toLatin1();

    return QString::fromUtf8(
        QByteArray::fromBase64(
            encoded,
            QByteArray::Base64UrlEncoding |
            QByteArray::AbortOnBase64DecodingErrors));
}

QString artworkCacheDirectory()
{
    const QString base =
        QStandardPaths::writableLocation(
            QStandardPaths::CacheLocation);

    const QString path = base + "/artwork";

    QDir().mkpath(path);

    return path;
}
}

ArtworkProvider::ArtworkProvider()
    : QQuickImageProvider(QQuickImageProvider::Image),
      m_memoryCache(64 * 1024)
{
}

QString ArtworkProvider::cacheFilePath(
    const QString &uri,
    const QSize &requestedSize) const
{
    const QByteArray key =
        uri.toUtf8() + ':' +
        QByteArray::number(requestedSize.width()) + 'x' +
        QByteArray::number(requestedSize.height());

    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(
            key, QCryptographicHash::Sha256).toHex());

    return artworkCacheDirectory() + '/' + hash + ".jpg";
}

QImage ArtworkProvider::placeholder(const QSize &requested) const
{
    const QSize size = requested.isValid()
        ? requested : QSize(180, 180);

    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor("#20232b"));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.setPen(QColor("#555b6b"));

    const int side = qMin(size.width(), size.height());
    const QRectF disc(
        (size.width() - side * 0.46) / 2.0,
        (size.height() - side * 0.46) / 2.0,
        side * 0.46,
        side * 0.46);

    painter.drawEllipse(disc);

    painter.setBrush(QColor("#555b6b"));
    painter.drawEllipse(disc.center(), side * 0.075, side * 0.075);

    return image;
}

QImage ArtworkProvider::requestImage(
    const QString &id,
    QSize *size,
    const QSize &requestedSize)
{
    const QString uri = decodedUri(id);

    QSize target = requestedSize;

    if (!target.isValid())
        target = QSize(200, 200);

    target.setWidth(qBound(64, target.width(), 600));
    target.setHeight(qBound(64, target.height(), 600));

    if (size)
        *size = target;

    if (uri.isEmpty())
        return placeholder(target);

    const QString cacheKey =
        uri + ':' +
        QString::number(target.width()) + 'x' +
        QString::number(target.height());

    {
        QMutexLocker locker(&m_mutex);

        if (QImage *cached = m_memoryCache.object(cacheKey))
            return *cached;
    }

    const QString path = cacheFilePath(uri, target);

    QImage image;

    if (QFileInfo::exists(path)) {
        QImageReader reader(path);
        reader.setScaledSize(target);
        image = reader.read();
    }

    if (image.isNull())
        image = loadArtwork(uri, target);

    if (image.isNull())
        image = placeholder(target);

    {
        QMutexLocker locker(&m_mutex);

        // Cost is approximately the image's memory usage in KiB.
        const int cost = qMax(
            1, static_cast<int>(image.sizeInBytes() / 1024));

        m_memoryCache.insert(
            cacheKey, new QImage(image), cost);
    }

    return image;
}

QImage ArtworkProvider::loadArtwork(
    const QString &uri,
    const QSize &requestedSize)
{
    QImage image;

    // MPD connections are serialized to avoid concurrent use of
    // the same libmpdclient connection.
    QMutexLocker locker(&m_mutex);

    mpd_connection *connection =
        mpd_connection_new(nullptr, 0, 3000);

    if (!connection ||
        mpd_connection_get_error(connection) != MPD_ERROR_SUCCESS) {
        if (connection)
            mpd_connection_free(connection);

        return image;
    }

    const QByteArray encodedUri = uri.toUtf8();
    QByteArray bytes;
    unsigned offset = 0;

    // Read artwork in chunks. A reasonable upper bound prevents
    // accidentally downloading an enormous file.
    constexpr int maxArtworkBytes = 32 * 1024 * 1024;

    while (bytes.size() < maxArtworkBytes) {
        if (!mpd_send_albumart(
                connection, encodedUri.constData(), offset)) {
            break;
        }

        QByteArray chunk(8192, '\0');

        const int received = mpd_recv_albumart(
            connection, chunk.data(),
            static_cast<size_t>(chunk.size()));

        const bool finished = mpd_response_finish(connection);

        if (received < 0 || !finished)
            break;

        if (received == 0)
            break;

        bytes.append(chunk.constData(), received);
        offset += static_cast<unsigned>(received);

        if (static_cast<int>(received) < chunk.size())
            break;
    }

    mpd_connection_free(connection);

    if (bytes.isEmpty())
        return image;

    QImageReader reader;
    QBuffer buffer(&bytes);

    if (!buffer.open(QIODevice::ReadOnly))
        return image;

    reader.setDevice(&buffer);
    reader.setAutoTransform(true);

    const QSize original = reader.size();

    if (original.isValid()) {
        QSize scaled = original;
        scaled.scale(requestedSize, Qt::KeepAspectRatio);
        reader.setScaledSize(scaled);
    }

    image = reader.read();

    if (image.isNull())
        return image;

    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    const QString path = cacheFilePath(uri, requestedSize);
    const QString temporaryPath = path + ".tmp";

    if (image.save(temporaryPath, "JPEG", 88)) {
        QFile::remove(path);
        QFile::rename(temporaryPath, path);
    }

    return image;
}
