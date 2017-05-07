#include "optionsdialog.h"
#include "ui_optionsdialog.h"
#include <QCamera>
#include <QCameraInfo>
#include "videosink.h"
#include <QList>
#include <QVideoWidget>
#include "mediaserver.h"

OptionsDialog::OptionsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::OptionsDialog)
{
    ui->setupUi(this);
    QAbstractVideoSurface* surface = getMediaServer()->createEncoder();
    AbstractMediaPlayer* player = getMediaServer()->createMediaPlayer();
    player->attachEncoder(surface);
    ui->videoView->addWidget(player);

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
