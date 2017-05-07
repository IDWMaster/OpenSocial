#ifndef VIDEOSINK_H
#define VIDEOSINK_H

#include <QWidget>
#include <QFrame>

///@summary A VideoSink serves as an output from a video renderer.
/// A VideoSink maintains a Surface, which can be locked by a producer for drawing into.
/// The Surface is by default unallocated. A Producer should request a frame by calling requestFrame
/// and release the frame by calling releaseFrame.
class VideoSink : public QWidget
{
    Q_OBJECT
public:
    explicit VideoSink(QWidget *parent = 0);
    void requestFrame();
signals:

public slots:
};

#endif // VIDEOSINK_H
