#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "fbpclient.h"

namespace Ui {
    class MainWindow;
}

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = 0);
    ~MainWindow();

protected:
    void changeEvent(QEvent *e);

private:
    Ui::MainWindow *ui;
    FbpClient      *fbp_;

private slots:
    void chooseDir();
    void on_pushButton_2_clicked();
    void on_autoDownload_toggled(bool checked);
};

#endif // MAINWINDOW_H
