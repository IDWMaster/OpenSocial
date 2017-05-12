#include "mediaserver.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavcodec/vaapi.h>
}
#include <queue>
#include <mutex>
#include <QAbstractVideoSurface>
#include <QList>
#include <QPainter>
#include <condition_variable>
#include <thread>
#include <va/va.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <iostream>

// FFMPEG config
// ./configure --enable-libx264 --enable-shared --enable-gpl --enable-ffplay --enable-sdl --enable-vaapi


class VideoFrame:public AbstractVideoFrame {
    Q_OBJECT
public:
    unsigned char* data;
    size_t width;
    size_t height;
    VideoFrame(unsigned char* data, size_t width, size_t height) {
        this->data = data;
        this->width = width;
        this->height = height;
    }
    ~VideoFrame() {
        delete[] data;
    }
};

class MediaEncoder:public AbstractVideoEncoder {
    Q_OBJECT
public:
    AVCodecContext* encodeCtx = nullptr;
    int64_t pts = 0;
    SwsContext* scaler;
    std::queue<QVideoFrame> pendingFrames;
    std::condition_variable evt;
    std::mutex mtx;
    std::thread* encoder;
    bool running = true;


    MediaEncoder() {
        encoder = new std::thread([=](){
            while(running) {
                std::unique_lock<std::mutex> l(mtx);
                evt.wait(l);
                std::queue<QVideoFrame> frames = pendingFrames;
                pendingFrames = std::queue<QVideoFrame>();
                l.unlock();
                while(frames.size()) {
                    QVideoFrame frame = frames.front();
                    frames.pop();
                    AVPixelFormat fmt = AV_PIX_FMT_YUV420P;
                    format = fmt;
                    if(encodeCtx == nullptr) {
                        codec_init(frame.width(),frame.height());
                    }
                    if(!(frame.width() == encodeCtx->width && frame.height() == encodeCtx->height)) {
                        codec_init(frame.width(),frame.height());
                    }
                    AVFrame* aframe = av_frame_alloc();
                    AVFrame* hwframe = 0;
                    aframe->pts = pts;
                    aframe->width = frame.width();
                    aframe->height = frame.height();
                    aframe->format = format;
                    av_image_fill_linesizes(aframe->linesize,(AVPixelFormat)aframe->format,aframe->width);
                    if(encodeCtx->pix_fmt != format) {
                        //Hardware encoder; special allocation
                        hwframe = av_frame_alloc();
                        hwframe->format = aframe->format;
                        if(av_hwframe_get_buffer(encodeCtx->hw_frames_ctx,hwframe,0)) {
                            printf("failed to get buffer\n");
                        }
                        AVPixelFormat* formats;


                        av_hwframe_map(aframe,hwframe,AV_HWFRAME_MAP_WRITE);
                    }else {
                       av_frame_get_buffer(aframe,0);
                    }
                    frame.map(QAbstractVideoBuffer::MapMode::ReadOnly);
                    const unsigned char* bits = frame.bits();
                    int stride = 4*frame.width();
                    sws_scale(scaler,&bits,&stride,0,frame.height(),aframe->data,aframe->linesize);
                    frame.unmap();


                    int status = 0;
                    if(hwframe) {
                        av_frame_unref(aframe);
                        avcodec_send_frame(encodeCtx,hwframe);
                        av_frame_free(&hwframe);
                    }else {
                        avcodec_send_frame(encodeCtx,aframe);
                    }
                    av_frame_free(&aframe);
                    AVPacket* packet = av_packet_alloc();
                    if(!avcodec_receive_packet(encodeCtx,packet)) {
                        //Got packet?
                        emit packetAvailable(packet);
                    }else {
                        av_packet_free(&packet);
                    }
                    pts+=60;
                }
            }
            avcodec_free_context(&encodeCtx);
            sws_freeContext(scaler);
        });
    }

    AVPixelFormat format;
    void codec_init(int width, int height) {
        if(encodeCtx) {
            avcodec_free_context(&encodeCtx);
            sws_freeContext(scaler);
        }
        scaler = sws_getContext(width,height,AV_PIX_FMT_BGR32,width,height,format,SWS_BICUBIC,0,0,0);
        AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);

        AVHWAccel* accel = 0;
        while(accel = av_hwaccel_next(accel)) {
            if(accel->id == AV_CODEC_ID_H264 && accel->pix_fmt == AV_PIX_FMT_VAAPI_VLD) {
                codec = avcodec_find_encoder_by_name("h264_vaapi");
                break;
            }
        }

        if(!accel) {
            printf("WARNING: Your hardware or software configuration does not support accelerated video encoding.\n");
        }


        encodeCtx = avcodec_alloc_context3(codec);
        encodeCtx->hwaccel = accel;
        encodeCtx->time_base.den = 60;
        encodeCtx->time_base.num = 1;
        encodeCtx->pix_fmt = format;
        if(accel) {
            encodeCtx->pix_fmt = accel->pix_fmt;
        }
        encodeCtx->width = width;
        encodeCtx->height = height;

        if(accel) {
        if(av_hwdevice_ctx_create(&encodeCtx->hw_device_ctx,AV_HWDEVICE_TYPE_VAAPI,0,0,0)) {
            printf("Unable to open hardware context\n");
        }else {
            encodeCtx->hw_frames_ctx = av_hwframe_ctx_alloc(encodeCtx->hw_device_ctx);
            AVHWFramesContext* framectx = (AVHWFramesContext*)encodeCtx->hw_frames_ctx->data; //Why isn't this better documented? It's not really a byte array....
            format = AV_PIX_FMT_NV12; //Why is this the only format that seems to work? (at least on Intel processors)?
            //TODO: Will this break on non-Intel CPUs?
            framectx->format = AV_PIX_FMT_VAAPI_VLD;
            framectx->width = encodeCtx->width;
            framectx->height = encodeCtx->height;
            framectx->sw_format = format;

            if(av_hwframe_ctx_init(encodeCtx->hw_frames_ctx)) {
                printf("Failed to start encoder\n");
            }

        }
        }
        avcodec_open2(encodeCtx,codec,0);
        emit initialized();
    }

    QList<QVideoFrame::PixelFormat> supportedPixelFormats(
                QAbstractVideoBuffer::HandleType handleType) const {
        QList<QVideoFrame::PixelFormat> retval;
        retval.append(QVideoFrame::PixelFormat::Format_RGB32);

        return retval;
    }

    bool present(const QVideoFrame & _frame) {
        if(!(_frame.width() > 0 && _frame.height() > 0)) {
            return false;
        }
        QVideoFrame frame = _frame;
        std::unique_lock<std::mutex> l(mtx);
        pendingFrames.push(frame);
        evt.notify_one();
        return true;
    }
    ~MediaEncoder() {
        running = false;
        evt.notify_one();
        encoder->join();
        delete encoder;
    }
signals:
    void initialized();
};


class MediaDecoder:public QObject {
    Q_OBJECT
public:
    AVCodecContext* context;
    SwsContext* scaler;
    std::thread* decoderThread;
    std::queue<AVPacket*> pendingPackets;
    std::mutex mtx;
    std::condition_variable evt;
    bool running = true;
    MediaDecoder(AVPixelFormat pixelFormat, int width, int height, QObject* owner = nullptr):QObject(owner) {
        pixelFormat = AV_PIX_FMT_YUV420P;
        decoderThread = new std::thread([=](){

            AVHWAccel* accel = 0;

            AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);

            while(accel = av_hwaccel_next(accel)) {
                if(accel->id == AV_CODEC_ID_H264 && accel->pix_fmt == AV_PIX_FMT_VAAPI_VLD) {
                    break;
                }

            }
            if(!accel) {
                printf("No HW-accelerated decoder found.\n");
            }


            context = avcodec_alloc_context3(codec);

            context->time_base.num = 1;
            context->time_base.den = 60;
            context->pix_fmt = pixelFormat;
            context->width = width;
            context->height = height;

            if(accel) {
                //context->hwaccel = accel;
               // av_hwdevice_ctx_create(&context->hw_device_ctx,AV_HWDEVICE_TYPE_VAAPI,0,0,0);
                //context->hwaccel_context = ((AVHWDeviceContext*)context->hw_device_ctx->data)->hwctx;
                context->pix_fmt = AV_PIX_FMT_QSV;
               // context->hwaccel = accel;
                //TODO find hwaccel  (name) == h264_vaapi
                /*context->hw_frames_ctx = av_hwframe_ctx_alloc(context->hw_device_ctx);
                AVHWFramesContext* framectx = (AVHWFramesContext*)context->hw_frames_ctx->data; //Why isn't this better documented? It's not really a byte array....
                //TODO: Will this break on non-Intel CPUs?
                framectx->format = AV_PIX_FMT_VAAPI_VLD;
                framectx->width = context->width;
                framectx->height = context->height;
                framectx->sw_format = pixelFormat;

                if(av_hwframe_ctx_init(context->hw_frames_ctx)) {
                    printf("Failed to start decoder\n");
                }*/

            }
            avcodec_open2(context,context->codec,0);

            printf("Decode: Using hwaccel %p\n",context->hwaccel);
            scaler = sws_getContext(width,height,pixelFormat,width,height,AV_PIX_FMT_RGBA,SWS_BICUBIC,0,0,0);


            while(running) {
                std::unique_lock<std::mutex> l(mtx);
                while(!pendingPackets.size() && running) {
                    evt.wait(l);
                }
                std::queue<AVPacket*> packets = pendingPackets;
                pendingPackets = std::queue<AVPacket*>();
                l.unlock();

                while(packets.size()) {
                AVPacket* packet = packets.front();
                packets.pop();
                    avcodec_send_packet(context,packet);
                AVFrame* frame = av_frame_alloc();
                if(!avcodec_receive_frame(context,frame)) {
                    unsigned char* mander = new unsigned char[frame->width*frame->height*4];
                    int stride = 4*frame->width;
                    sws_scale(scaler,frame->data,frame->linesize,0,frame->height,&mander,&stride);
                    emit frameAvailable(new VideoFrame(mander,frame->width,frame->height));
                    av_frame_free(&frame);
                }else {
                    av_frame_free(&frame);
                }
                av_packet_free(&packet);
                }
            }

            std::unique_lock<std::mutex> l(mtx);
            while(pendingPackets.size()) {
                AVPacket* packet = pendingPackets.front();
                pendingPackets.pop();
                av_packet_free(&packet);
                sws_freeContext(scaler);
                avcodec_free_context(&context);
            }
        });

    }
    ~MediaDecoder() {
        running = false;
        evt.notify_one();
        decoderThread->join();
        delete decoderThread;
    }

public slots:
    void injectPacket(AVPacket* packet) {
        std::unique_lock<std::mutex> l(mtx);
        pendingPackets.push(packet);
        evt.notify_one();
    }
signals:
    void frameAvailable(AbstractVideoFrame* frame);
};

class MediaPlayer:public AbstractMediaPlayer {
    Q_OBJECT
public:
    MediaDecoder* decoder = nullptr;
    MediaEncoder* encoder = nullptr;
    VideoFrame* frame = nullptr;

    void attachEncoder(QAbstractVideoSurface * _encoder) {
        encoder = (MediaEncoder*)_encoder;
        connect(encoder,SIGNAL(initialized()),this,SLOT(encoderInitialized()));
    }
public slots:
    void encoderInitialized() {
        this->decoder = new MediaDecoder(encoder->format,encoder->encodeCtx->width,encoder->encodeCtx->height,this);
        connect(encoder,SIGNAL(packetAvailable(AVPacket*)),decoder,SLOT(injectPacket(AVPacket*)));
        connect(decoder,SIGNAL(frameAvailable(AbstractVideoFrame*)),this,SLOT(notifyFrameAvailable(AbstractVideoFrame*)));
    }

    void paintEvent(QPaintEvent*) {
         if(frame) {
        QPainter painter(this);
        painter.drawImage(0,0,QImage(frame->data,frame->width,frame->height,QImage::Format_RGB32));
        delete frame;
        frame = nullptr;
         }
    }

    void notifyFrameAvailable(AbstractVideoFrame* _frame) {
        VideoFrame* frame = (VideoFrame*)_frame;
        if(this->frame) {
            delete this->frame;
            this->frame = nullptr;
        }
        this->frame = frame;
        repaint(0,0,width(),height());
    }
};

AbstractVideoEncoder* MediaServer::createEncoder() {
    return new MediaEncoder();
}


AbstractMediaPlayer* MediaServer::createMediaPlayer() {
    return new MediaPlayer();
}

MediaServer::MediaServer(QObject *parent) : QObject(parent)
{
    av_register_all();
    //av_log_set_level(AV_LOG_DEBUG);
}

MediaServer* server = nullptr;

MediaServer* getMediaServer() {
    if(!server) {
        server = new MediaServer(nullptr);
    }
    return server;
}


#include "mediaserver.moc"
