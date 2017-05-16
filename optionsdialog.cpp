#include "optionsdialog.h"
#include "ui_optionsdialog.h"
#include <QCamera>
#include <QCameraInfo>
#include "videosink.h"
#include <QList>
#include <QVideoWidget>
#include "mediaserver.h"
#include "cppext/cppext.h"
#include <thread>
#include <unistd.h>
#include <queue>
#include <mutex>

//Tester for network streaming

class DispatcherCallback:public System::Message {
public:
    std::shared_ptr<System::Event> evt;

    DispatcherCallback(const std::shared_ptr<System::Event>& evt):evt(evt) {

    }
};

class TestNetworkSender {
public:
    std::mutex mtx;
    std::thread* dispatcher;
    std::shared_ptr<System::MessageQueue> dispatcherQueue;
    std::shared_ptr<System::Net::UDPSocket> client;
    System::Net::IPEndpoint ep;

    uint16_t pid; //Packet id -- NOT pid
    TestNetworkSender() {
        pid = 0;
        client = System::Net::CreateUDPSocket();
        ep.ip = "::ffff:192.168.0.4";
        //ep.ip = "fe80::26df:6aff:fe00:201";
        ep.port = 9090;
        dispatcher = new std::thread([=](){
            dispatcherQueue = System::MakeQueue([=](const std::shared_ptr<System::Message>& msg){
                ((DispatcherCallback*)msg.get())->evt->Process();
            });
            System::Enter();
        });
    }
    template<typename T>
    void runOnDispatcher(const T& functor) {
        dispatcherQueue->Post(std::make_shared<DispatcherCallback>(System::F2E(functor)));
    }

    ~TestNetworkSender() {
        runOnDispatcher([=](){
            dispatcherQueue = nullptr;
        });
        dispatcher->join();
        delete dispatcher; //dispatch the dispatcher

    }
};
static TestNetworkSender net;


OptionsDialog::OptionsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::OptionsDialog)
{
    ui->setupUi(this);
    AbstractVideoEncoder* surface = getMediaServer()->createEncoder();
    AbstractMediaPlayer* player = getMediaServer()->createMediaPlayer();
    connect(surface,&AbstractVideoEncoder::packetAvailable,[=](AVPacket* packet){
        //printf("Got packet?\n");
        //Packet ID (short) -- Total length (int) -- Segment ID (short) -- Segment size (byte)
        size_t toSend = packet->size;
        uint16_t segid = 0;
        unsigned char segsize = 10; //1024 (1 << 10)
        size_t offset = 0;
        size_t maxlen = 1 << (size_t)segsize;
        while(toSend) {
            size_t plen = toSend > maxlen ? maxlen : toSend;
            unsigned char* mander = new unsigned char[2+4+2+1+plen];
            unsigned char* ptr = mander;
            memcpy(ptr,&net.pid,2);
            ptr+=2;
            memcpy(ptr,&packet->size,4);
            ptr+=4;
            memcpy(ptr,&segid,2);
            ptr+=2;
            *ptr = segsize;
            ptr++;
            memcpy(ptr,packet->data+offset,plen);
            offset+=plen;
            net.client->Send(mander,2+4+2+1+plen,net.ep);
            toSend-=plen;
            segid++;
            delete[] mander;
        }
        net.pid++;
        av_packet_free(&packet);
    });
    //player->attachEncoder(surface);
   // ui->videoView->addWidget(player);

    QList<QCameraInfo> cameras = QCameraInfo::availableCameras();
    for(auto bot = cameras.begin();bot != cameras.end();bot++) {
        ui->comboBox->addItem(bot->description(),bot->deviceName().toUtf8());
    }

    QCamera* camera = new QCamera(this);
    camera->setViewfinder(surface);
    camera->start();
    connect(this,&QDialog::finished,[=](){
        deleteLater();
    });
}

OptionsDialog::~OptionsDialog()
{
    delete ui;
}
