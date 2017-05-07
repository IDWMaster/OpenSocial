#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "optionsdialog.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    connect(ui->action_Options,&QAction::triggered,[=](){
        OptionsDialog* dlg = new OptionsDialog(this);
        dlg->show();

    });
}

MainWindow::~MainWindow()
{
    delete ui;
}
