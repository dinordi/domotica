#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , serial(new QSerialPort(this))
{
    ui->setupUi(this);


    connect(serial, &QSerialPort::readyRead, this, &MainWindow::handleReadyRead);
    connect(ui->ComButton, &QPushButton::clicked, this, &MainWindow::connectCom);
    connect(ui->LedButton, &QPushButton::clicked, this, &MainWindow::sendData);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::handleReadyRead()
{
    QByteArray data = serial->readAll();
    ui->textBrowser->append(data);
    int value = data.toInt();
    qDebug() << value;
    if (value == 3) {
        ui->LedLabel->setStyleSheet("background-color: yellow;");
    } else if (value == 4) {
        ui->LedLabel->setStyleSheet("background-color: black;");
    }
}

void MainWindow::connectCom()
{
    int x = ui->ComSpinBox->value();

    // Convert int to QString and concatenate with the port name prefix
    QString portName = "COM" + QString::number(x);
    qDebug() << portName;
    serial->setPortName(portName);
    serial->setBaudRate(QSerialPort::Baud9600);
    serial->setDataBits(QSerialPort::Data8);
    serial->setParity(QSerialPort::NoParity);
    serial->setStopBits(QSerialPort::OneStop);
    serial->setFlowControl(QSerialPort::NoFlowControl);
    if (!serial->open(QIODevice::ReadWrite)) {
        // Handle error
    }
}

void MainWindow::sendData()
{
    QString Char;

    if(toggle == 0)
    {
        Char = "1\r\n";
        toggle = 1;
    }
    else
    {
        Char = "2\r\n";
        toggle = 0;
    }


    QByteArray byte = Char.toUtf8();

    serial->write(byte);
    qDebug() << Char;
}

