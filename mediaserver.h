#ifndef MEDIASERVER_H
#define MEDIASERVER_H

#include <QObject>
#include <QWidget>
#include <QAbstractVideoSurface>
extern "C" {
#include <libavcodec/avcodec.h>
}

class AbstractVideoFrame:public QObject {
    Q_OBJECT
public:
};

class AbstractVideoEncoder:public QAbstractVideoSurface {
    Q_OBJECT
public:
signals:
    void packetAvailable(AVPacket* packet);

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
    AbstractVideoEncoder* createEncoder();
signals:

public slots:
};

MediaServer* getMediaServer();

#endif // MEDIASERVER_H
