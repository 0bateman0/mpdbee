#include "mpdcontroller.h"

#include <mpd/client.h>
#include <mpd/audio_format.h>

#include <QByteArray>
#include <QHash>
#include <QVariantMap>
#include <QVariantList>
#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>

namespace {

QString tag(const struct mpd_song *song,
            enum mpd_tag_type type,
            unsigned index = 0)
{
    const char *value = mpd_song_get_tag(song, type, index);

    return value
        ? QString::fromUtf8(value)
        : QString();
}

int numberBeforeSlash(const QString &value)
{
    bool ok = false;
    const int result =
        value.section('/', 0, 0).toInt(&ok);

    return ok ? result : 0;
}

QVariantMap songMap(const struct mpd_song *song)
{
    QVariantMap item;

    const QString uri =
        QString::fromUtf8(mpd_song_get_uri(song));

    QString title = tag(song, MPD_TAG_TITLE);
    QString artist = tag(song, MPD_TAG_ARTIST);
    QString album = tag(song, MPD_TAG_ALBUM);
    QString albumArtist = tag(song, MPD_TAG_ALBUM_ARTIST);

    if (title.isEmpty())
        title = QFileInfo(uri).completeBaseName();

    if (artist.isEmpty())
        artist = "Unknown artist";

    if (album.isEmpty())
        album = QFileInfo(QFileInfo(uri).path()).fileName();

    if (albumArtist.isEmpty())
        albumArtist = artist;

    item["uri"] = uri;
    item["title"] = title;
    item["artist"] = artist;
    item["album"] = album;
    item["albumArtist"] = albumArtist;
    item["year"] = tag(song, MPD_TAG_DATE);
    item["discNumber"] = numberBeforeSlash(
        tag(song, MPD_TAG_DISC));
    item["trackNumber"] = numberBeforeSlash(
        tag(song, MPD_TAG_TRACK));

    return item;
}

bool trackLess(const QVariant &left, const QVariant &right)
{
    const QVariantMap a = left.toMap();
    const QVariantMap b = right.toMap();

    const int discA = a.value("discNumber").toInt();
    const int discB = b.value("discNumber").toInt();

    if (discA != discB)
        return discA < discB;

    const int trackA = a.value("trackNumber").toInt();
    const int trackB = b.value("trackNumber").toInt();

    if (trackA != trackB && trackA > 0 && trackB > 0)
        return trackA < trackB;

    if (trackA != trackB && trackA == 0)
        return false;

    if (trackA != trackB && trackB == 0)
        return true;

    return QString::compare(
        a.value("title").toString(),
        b.value("title").toString(),
        Qt::CaseInsensitive) < 0;
}

} // namespace

MpdController::MpdController(QObject *parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout,
            this, &MpdController::refresh);

    m_timer.start(1000);

    refresh();
    refreshLibrary();
    refreshQueue();
}

MpdController::~MpdController()
{
    disconnectMpd();
}

mpd_connection *MpdController::connectMpd()
{
    if (m_connection &&
        mpd_connection_get_error(m_connection) == MPD_ERROR_SUCCESS) {
        return m_connection;
    }

    disconnectMpd();

    m_connection = mpd_connection_new(nullptr, 0, 3000);

    if (!m_connection ||
        mpd_connection_get_error(m_connection) != MPD_ERROR_SUCCESS) {
        disconnectMpd();
        m_connectionStatus = "MPD unavailable";
        emit stateChanged();
        return nullptr;
    }

    m_connectionStatus = "Connected";
    return m_connection;
}

void MpdController::disconnectMpd()
{
    if (m_connection) {
        mpd_connection_free(m_connection);
        m_connection = nullptr;
    }
}

QString MpdController::title() const
{
    return m_title;
}

QString MpdController::artist() const
{
    return m_artist;
}

QString MpdController::album() const
{
    return m_album;
}

QString MpdController::state() const
{
    return m_state;
}

QString MpdController::connectionStatus() const
{
    return m_connectionStatus;
}

int MpdController::elapsed() const
{
    return m_elapsed;
}

int MpdController::duration() const
{
    return m_duration;
}

int MpdController::volume() const
{
    return m_volume;
}

QString MpdController::audioFormat() const
{
    return m_audioFormat;
}

QVariantList MpdController::albums() const
{
    return m_albums;
}

QVariantList MpdController::queue() const
{
    return m_queue;
}

void MpdController::refresh()
{
    auto *conn = connectMpd();

    if (!conn)
        return;

    struct mpd_status *status = mpd_run_status(conn);

    if (!status) {
        disconnectMpd();
        m_connectionStatus = "MPD unavailable";
        emit stateChanged();
        return;
    }

    switch (mpd_status_get_state(status)) {
    case MPD_STATE_PLAY:
        m_state = "play";
        break;
    case MPD_STATE_PAUSE:
        m_state = "pause";
        break;
    default:
        m_state = "stop";
        break;
    }

    m_elapsed = static_cast<int>(
        mpd_status_get_elapsed_time(status));

    m_duration = static_cast<int>(
        mpd_status_get_total_time(status));

    const int currentVolume = mpd_status_get_volume(status);
    m_volume = currentVolume < 0 ? 0 : currentVolume;

    const struct mpd_audio_format *audio =
        mpd_status_get_audio_format(status);

    if (audio && audio->sample_rate > 0 && audio->bits > 0) {
        const double rate = audio->sample_rate / 1000.0;
        const QString rateText =
            QString::number(rate, 'f',
                            audio->sample_rate % 1000 == 0 ? 0 : 1);

        m_audioFormat = QString("%1 kHz · %2-bit")
                            .arg(rateText)
                            .arg(audio->bits);
    } else {
        m_audioFormat = "Format unavailable";
    }

    mpd_status_free(status);

    struct mpd_song *song = mpd_run_current_song(conn);

    if (song) {
        const QVariantMap current = songMap(song);

        m_title = current.value("title").toString();
        m_artist = current.value("artist").toString();
        m_album = current.value("album").toString();

        mpd_song_free(song);
    } else {
        m_title = "Nothing playing";
        m_artist.clear();
        m_album.clear();
    }

    m_connectionStatus = "Connected";
    emit stateChanged();
}

void MpdController::refreshLibrary()
{
    auto *conn = connectMpd();

    if (!conn)
        return;

    if (!mpd_send_list_all_meta(conn, "")) {
        disconnectMpd();
        return;
    }

    QVariantList result;
    QHash<QString, int> albumIndexes;

    struct mpd_song *song = nullptr;

    while ((song = mpd_recv_song(conn)) != nullptr) {
        const QVariantMap track = songMap(song);

        const QString albumTitle =
            track.value("album").toString();

        const QString albumArtist =
            track.value("albumArtist").toString();

        const QString year =
            track.value("year").toString();

        // Keep different artists and differently dated releases apart.
        const QString key =
            albumArtist.toCaseFolded() + QChar(0x1f) +
            albumTitle.toCaseFolded() + QChar(0x1f) +
            year;

        int index = albumIndexes.value(key, -1);

        if (index < 0) {
            QVariantMap album;

            album["title"] = albumTitle;
            album["artist"] = albumArtist;
            album["year"] = year;
            album["uri"] = track.value("uri");
            album["tracks"] = QVariantList{};
            album["trackCount"] = 0;

            index = result.size();
            albumIndexes.insert(key, index);
            result.append(album);
        }

        QVariantMap album = result[index].toMap();
        QVariantList tracks = album.value("tracks").toList();

        tracks.append(track);

        album["tracks"] = tracks;
        album["trackCount"] = tracks.size();

        result[index] = album;

        mpd_song_free(song);
    }

    if (!mpd_response_finish(conn)) {
        disconnectMpd();
        return;
    }

    // Sort each album's tracks into disc and track order.
    for (int i = 0; i < result.size(); ++i) {
        QVariantMap album = result[i].toMap();
        QVariantList tracks = album.value("tracks").toList();

        std::stable_sort(
            tracks.begin(), tracks.end(), trackLess);

        album["tracks"] = tracks;
        album["trackCount"] = tracks.size();

        result[i] = album;
    }

    // Sort the catalogue alphabetically by album artist and title.
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);

    std::stable_sort(
        result.begin(), result.end(),
        [&collator](const QVariant &left, const QVariant &right) {
            const QVariantMap a = left.toMap();
            const QVariantMap b = right.toMap();

            const int artistOrder = collator.compare(
                a.value("artist").toString(),
                b.value("artist").toString());

            if (artistOrder != 0)
                return artistOrder < 0;

            return collator.compare(
                a.value("title").toString(),
                b.value("title").toString()) < 0;
        });

    m_albums = result;
    emit albumsChanged();
}

void MpdController::refreshQueue()
{
    auto *conn = connectMpd();

    if (!conn)
        return;

    if (!mpd_send_list_queue_meta(conn)) {
        disconnectMpd();
        return;
    }

    QVariantList result;
    struct mpd_entity *entity = nullptr;

    while ((entity = mpd_recv_entity(conn)) != nullptr) {
        if (mpd_entity_get_type(entity) == MPD_ENTITY_TYPE_SONG) {
            const struct mpd_song *song =
                mpd_entity_get_song(entity);

            if (song) {
                QVariantMap item = songMap(song);

                item["position"] = mpd_song_get_pos(song);
                result.append(item);
            }
        }

        mpd_entity_free(entity);
    }

    if (!mpd_response_finish(conn)) {
        disconnectMpd();
        return;
    }

    m_queue = result;
    emit queueChanged();
}

void MpdController::play()
{
    auto *conn = connectMpd();

    if (conn)
        mpd_run_play(conn);

    refresh();
}

void MpdController::pause()
{
    auto *conn = connectMpd();

    if (!conn)
        return;

    mpd_run_pause(conn, m_state == "play");

    refresh();
}

void MpdController::next()
{
    auto *conn = connectMpd();

    if (conn)
        mpd_run_next(conn);

    refresh();
    refreshQueue();
}

void MpdController::previous()
{
    auto *conn = connectMpd();

    if (conn)
        mpd_run_previous(conn);

    refresh();
}

void MpdController::seek(int seconds)
{
    auto *conn = connectMpd();

    if (conn) {
        mpd_run_seek_current(
            conn, static_cast<float>(qMax(0, seconds)), false);
    }

    refresh();
}

void MpdController::setVolume(int value)
{
    auto *conn = connectMpd();

    if (conn)
        mpd_run_set_volume(conn, qBound(0, value, 100));

    refresh();
}

void MpdController::playUri(const QString &uri)
{
    auto *conn = connectMpd();

    if (!conn || uri.isEmpty())
        return;

    struct mpd_status *status = mpd_run_status(conn);

    const unsigned position = status
        ? mpd_status_get_queue_length(status) : 0;

    if (status)
        mpd_status_free(status);

    const QByteArray encoded = uri.toUtf8();

    if (mpd_run_add(conn, encoded.constData()))
        mpd_run_play_pos(conn, position);

    refresh();
    refreshQueue();
}

void MpdController::addUri(const QString &uri)
{
    auto *conn = connectMpd();

    if (!conn || uri.isEmpty())
        return;

    const QByteArray encoded = uri.toUtf8();

    if (mpd_run_add(conn, encoded.constData()))
        refreshQueue();
}

void MpdController::playAlbum(const QVariantList &uris)
{
    auto *conn = connectMpd();

    if (!conn || uris.isEmpty())
        return;

    if (!mpd_run_clear(conn))
        return;

    for (const QVariant &value : uris) {
        const QByteArray uri = value.toString().toUtf8();

        if (!uri.isEmpty())
            mpd_run_add(conn, uri.constData());
    }

    mpd_run_play_pos(conn, 0);

    refresh();
    refreshQueue();
}

void MpdController::removeQueueItem(int position)
{
    auto *conn = connectMpd();

    if (conn && position >= 0)
        mpd_run_delete(conn, static_cast<unsigned>(position));

    refreshQueue();
    refresh();
}

void MpdController::clearQueue()
{
    auto *conn = connectMpd();

    if (conn)
        mpd_run_clear(conn);

    refreshQueue();
    refresh();
}

QString MpdController::artworkUrlForUri(
    const QString &uri) const
{
    const QByteArray encoded = uri.toUtf8().toBase64(
        QByteArray::Base64UrlEncoding |
        QByteArray::OmitTrailingEquals);

    return QStringLiteral("image://covers/") +
           QString::fromLatin1(encoded);
}
