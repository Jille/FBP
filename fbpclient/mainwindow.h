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
    int rowForFileId( int ) const;

private slots:
    void chooseDir();
    void on_pushButton_2_clicked();
    void on_autoDownload_toggled(bool checked);
    void fileAdded(int, const QString &fn, int startProgress);
    void fileRemoved(int);
    void fileProgressChanged( int, int );
    void addRow( int id, const QString &fn, int progress );
};

#endif // MAINWINDOW_H
