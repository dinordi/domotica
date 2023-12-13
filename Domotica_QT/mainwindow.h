#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSerialPort>
#include <QSerialPortInfo>


QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void handleReadyRead();
    void connectCom();
    void sendData();

private:
    Ui::MainWindow *ui;
    QSerialPort *serial;
    int toggle = 0;
};
#endif // MAINWINDOW_H
