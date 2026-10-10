#pragma once

#include <QObject>
#include <QVariantList>
#include <QTimer>
#include <QString>

struct mpd_connection;

class MpdController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString title READ title NOTIFY stateChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY stateChanged)
    Q_PROPERTY(QString album READ album NOTIFY stateChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)

    Q_PROPERTY(QString connectionStatus
               READ connectionStatus NOTIFY stateChanged)

    Q_PROPERTY(int elapsed READ elapsed NOTIFY stateChanged)
    Q_PROPERTY(int duration READ duration NOTIFY stateChanged)
    Q_PROPERTY(int volume READ volume NOTIFY stateChanged)
    Q_PROPERTY(QString audioFormat READ audioFormat NOTIFY stateChanged)

    Q_PROPERTY(QVariantList albums
               READ albums NOTIFY albumsChanged)

    Q_PROPERTY(QVariantList queue
               READ queue NOTIFY queueChanged)

public:
    explicit MpdController(QObject *parent = nullptr);
    ~MpdController() override;

    QString title() const;
    QString artist() const;
    QString album() const;
    QString state() const;
    QString connectionStatus() const;

    int elapsed() const;
    int duration() const;
    int volume() const;
    QString audioFormat() const;

    QVariantList albums() const;
    QVariantList queue() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshLibrary();
    Q_INVOKABLE void refreshQueue();

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(int seconds);
    Q_INVOKABLE void setVolume(int value);

    Q_INVOKABLE void playUri(const QString &uri);
    Q_INVOKABLE void addUri(const QString &uri);

    Q_INVOKABLE void playAlbum(const QVariantList &uris);
    Q_INVOKABLE void removeQueueItem(int position);
    Q_INVOKABLE void clearQueue();

    Q_INVOKABLE QString artworkUrlForUri(
        const QString &uri) const;

signals:
    void stateChanged();
    void albumsChanged();
    void queueChanged();

private:
    mpd_connection *connectMpd();
    void disconnectMpd();

    QString m_title = "Nothing playing";
    QString m_artist;
    QString m_album;
    QString m_state = "stop";
    QString m_connectionStatus = "Connecting...";

    int m_elapsed = 0;
    int m_duration = 0;
    int m_volume = 0;
    QString m_audioFormat = "Format unavailable";

    QVariantList m_albums;
    QVariantList m_queue;

    QTimer m_timer;
    mpd_connection *m_connection = nullptr;
};
