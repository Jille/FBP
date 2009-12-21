#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QtGui/QFileDialog>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    fbp_(new FbpClient())
{
    ui->setupUi(this);

    ui->downloadDir->setText( QDir::homePath() );
    connect( ui->chooseDir, SIGNAL(  clicked() ),
             this,          SLOT(  chooseDir() ) );
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

void MainWindow::chooseDir()
{
  QString s = QFileDialog::getExistingDirectory( this,
                 "Choose a download directory", ui->downloadDir->text() );
  ui->downloadDir->setText( s );
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
