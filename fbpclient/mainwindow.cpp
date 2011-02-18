#include "mainwindow.h"
#include "fbpclient.h"
#include "ui_mainwindow.h"

#include <QtGui/QFileDialog>
#include <QtGui/QProgressBar>
#include <QtGui/QMessageBox>
#include <QtCore/QTemporaryFile>

#include "branding.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    fbp_(new FbpClient())
{
    ui->setupUi(this);

#ifndef BRANDING
    ui->branding->hide();
#else
    // Search for BRANDING_LOGO
    QString brandingLogo( BRANDING_LOGO );
    QString prevDir("../");
    for(int i = 0; i < 6; ++i)
    {
      if( QFile::exists(prevDir.repeated(i) + brandingLogo) )
      {
        brandingLogo.prepend(prevDir.repeated(i));
      }
    }
    QPixmap logo(brandingLogo);
    ui->branding->setPixmap(logo);
    #ifdef BRANDING_AUTOACCEPT
        ui->autoDownload->setChecked(true);
    #endif
    #ifdef BRANDING_WINDOW_TITLE
        setWindowTitle(BRANDING_WINDOW_TITLE);
    #endif
#endif

    ui->files->setColumnWidth( 0, 30 );
    ui->files->setColumnWidth( 1, 250 );
    ui->files->setColumnWidth( 2, 250 );

    ui->downloadDir->setText( QDir::homePath() );
    connect( ui->chooseDir, SIGNAL(             clicked()                ),
             this,          SLOT(             chooseDir()                ) );
    connect( fbp_,          SIGNAL(           fileAdded(int,QString,int) ),
             this,          SLOT(             fileAdded(int,QString,int) ) );
    connect( fbp_,          SIGNAL(         fileRemoved(int)             ),
             this,          SLOT(           fileRemoved(int)             ) );
    connect( fbp_,          SIGNAL( fileProgressChanged(int,int)         ),
             this,          SLOT(   fileProgressChanged(int,int)         ) );
    connect( fbp_,          SIGNAL(fileOverwriteWarning(int, QString)    ),
             this,          SLOT(  fileOverwriteWarning(int, QString)    ) );
    connect( fbp_,          SIGNAL(    downloadFinished(int, QString)    ),
             this,          SLOT(      downloadFinished(int, QString)    ) );

    fbp_->startListening();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::addRow( int fileId, const QString &fileName, int progress )
{
  int rowId = ui->files->rowCount();

  ui->files->insertRow( rowId );
  QTableWidgetItem *id = new QTableWidgetItem( QString::number(fileId) );
  QTableWidgetItem *fn = new QTableWidgetItem( fileName );

  QWidget          *wt = new QWidget();
  QSpacerItem      *s1 = new QSpacerItem(1, 1, QSizePolicy::Fixed, QSizePolicy::Expanding );
  QSpacerItem      *s2 = new QSpacerItem(1, 7, QSizePolicy::Fixed, QSizePolicy::Expanding );
  QVBoxLayout      *lt = new QVBoxLayout( wt );
  QProgressBar     *pb = new QProgressBar( wt );
  pb->setObjectName( "PROGRESS_BAR" );
  pb->setParent( this );
  pb->setValue( progress );
  pb->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );

  lt->addSpacerItem( s1 );
  lt->addWidget( pb );
  lt->addSpacerItem( s2 );

  lt->setMargin( 0 );
  wt->setLayout( lt );

  ui->files->setItem( rowId,       0, id );
  ui->files->setItem( rowId,       1, fn );
  ui->files->setCellWidget( rowId, 2, wt );
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

bool MainWindow::checkDirWritable( const QDir &d )
{
  QMessageBox *mb = new QMessageBox;
  if( !d.exists() )
  {
    mb->setText( "The directory you have chosen doesn't exist.");
    mb->show();
    return false;
  }

  QTemporaryFile tf( d.absolutePath() + d.separator() + "fbp_test_XXXXXX" );
  if( !tf.open() )
  {
    mb->setText( "The directory you have chosen does not seem to be writable.");
    mb->show();
    return false;
  }

  tf.remove();
  return true;
}

void MainWindow::downloadFinished( int fileId, const QString &fileName )
{
  Q_ASSERT( fileName == fbp_->fileNameForFile( fileId ) );

  // The bitmask has been removed and the file has been downloaded. Let's show
  // a dialog!
  QMessageBox *mb = new QMessageBox;
  mb->setText( "Download finished!\n" + fileName );
  mb->show();
}

void MainWindow::fileAdded( int fileId, const QString &fileName, int startProgress )
{
  addRow( fileId, fileName, startProgress );

  if( ui->autoDownload->isChecked() )
  {
    // Automatically start the download
    startDownload( fileId );
  }
}

void MainWindow::fileOverwriteWarning(int, const QString &fn)
{
  QMessageBox *mb = new QMessageBox(QMessageBox::Critical, "File already exists",
     "Could not save file: a file with that name already exists. The file was saved as " + fn );
  mb->exec();
  return;
}

void MainWindow::fileProgressChanged( int fileId, int progress )
{
  int rowId = rowForFileId( fileId );
  Q_ASSERT( rowId >= 0 && ui->files->rowCount() > rowId );

  QWidget *w = ui->files->cellWidget( rowId, 2 );
  Q_ASSERT( w != 0 );

  QProgressBar *p = w->findChild<QProgressBar*>( "PROGRESS_BAR" );
  Q_ASSERT( p != 0 );

  p->setValue( progress );
}

void MainWindow::fileRemoved( int fileId )
{
  Q_UNUSED( fileId );
}

void MainWindow::on_pushButton_clicked()
{
  int row = ui->files->currentRow();
  if( row < 0 )
    return;

  int fileId = ui->files->item( row, 0 )->text().toInt();
  Q_ASSERT( rowForFileId( fileId ) == row );

  startDownload( fileId );
}

void MainWindow::on_pushButton_2_clicked()
{
  QCoreApplication::exit(0);
}

int MainWindow::rowForFileId( int fileId ) const
{
  for( int i = 0; i < ui->files->rowCount(); ++i )
  {
    if( ui->files->item( i, 0 )->text() == QString::number( fileId ) )
      return i;
  }

  // not found
  return -1;
}

void MainWindow::startDownload( int fileId )
{
  if( fbp_->isDownloadingFile( fileId ) )
    return;

  QDir d( ui->downloadDir->text() );
  if( checkDirWritable( d ) )
    fbp_->startDownload( fileId, d );
}
