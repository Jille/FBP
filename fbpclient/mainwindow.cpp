#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        ui->retranslateUi(this);
        break;
    default:
        break;
    }
}

void MainWindow::on_autoDownload_toggled( bool checked )
{
  if( checked )
    ui->autoBuffer->setChecked( true );

  ui->autoBuffer->setEnabled( !checked );
}

void MainWindow::on_pushButton_2_clicked()
{
  QCoreApplication::exit(0);
}
