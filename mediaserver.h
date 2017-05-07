#ifndef MEDIASERVER_H
#define MEDIASERVER_H

#include <QObject>
#include <QWidget>
#include <QAbstractVideoSurface>

class AbstractVideoFrame:public QObject {
    Q_OBJECT
public:
};

class AbstractMediaPlayer:public QWidget {
Q_OBJECT
public:
    virtual void attachEncoder(QAbstractVideoSurface* encoder) = 0;
    virtual ~AbstractMediaPlayer(){}
public slots:
    virtual void notifyFrameAvailable(AbstractVideoFrame* frame) = 0;
};

class MediaServer : public QObject
{
    Q_OBJECT
public:
    explicit MediaServer(QObject *parent = 0);
    AbstractMediaPlayer* createMediaPlayer();
    QAbstractVideoSurface* createEncoder();
signals:

public slots:
};

MediaServer* getMediaServer();

#endif // MEDIASERVER_H
