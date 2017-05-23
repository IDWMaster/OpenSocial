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
#include <condition_variable>

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
    std::condition_variable evt;
    std::thread* dispatcher;
    std::thread* netThread;
    std::shared_ptr<System::MessageQueue> dispatcherQueue;
    std::shared_ptr<System::Net::UDPSocket> client;
    System::Net::IPEndpoint ep;
    uint16_t pid; //Packet id -- NOT pid
    std::queue<AVPacket*> pendingPackets;
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
        netThread = new std::thread([=](){
           while(1) {
               std::unique_lock<std::mutex> l(mtx);
               std::queue<AVPacket*> packets;
               int64_t pts = -1;
               int64_t duration;
               int64_t total = 0;
               velociraptor:
               while(pendingPackets.empty()) {
                   evt.wait(l);
               }
               if(pts == -1) {
                   AVPacket* p = pendingPackets.front();
                   packets.push(p);
                   pts = p->pts;
                   total+=p->size;
                   pendingPackets.pop();
                   goto velociraptor;
               }
               AVPacket* p = pendingPackets.front();
               printf("%i\n",(int)p->pts);
               total+=p->size;
               packets.push(p);
               pendingPackets.pop();
               if(p->pts == pts) {
                   goto velociraptor;
               }
               duration = p->pts-pts;
               l.unlock();
               duration = (int64_t)((((double)duration)/60.0)*1000.0*1000.0);



               unsigned char segsize = 10; //1024 (1 << 10)
               size_t maxlen = 1 << (size_t)segsize;
               size_t numSegments = total/maxlen;

               if(numSegments == 0) {
                   numSegments = 1;
               }
                duration/=numSegments;

               while(packets.size()) {

               AVPacket* packet = packets.front();
               packets.pop();
                   size_t toSend = packet->size;
               uint16_t segid = 0;
               size_t offset = 0;
               while(toSend) {
                   size_t plen = toSend > maxlen ? maxlen : toSend;
                   unsigned char* mander = new unsigned char[2+4+2+1+plen];
                   unsigned char* ptr = mander;
                   memcpy(ptr,&pid,2);
                   ptr+=2;
                   memcpy(ptr,&packet->size,4);
                   ptr+=4;
                   memcpy(ptr,&segid,2);
                   ptr+=2;
                   *ptr = segsize;
                   ptr++;
                   memcpy(ptr,packet->data+offset,plen);
                   offset+=plen;
                   client->Send(mander,2+4+2+1+plen,ep);
                   toSend-=plen;
                   segid++;
                   delete[] mander;
               }
               pid++;
               av_packet_free(&packet);
               }

           }

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
        std::unique_lock<std::mutex> l(net.mtx);
        net.pendingPackets.push(packet);
        net.evt.notify_all();
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
