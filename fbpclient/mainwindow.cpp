#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QtGui/QFileDialog>
#include <QtGui/QProgressBar>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    fbp_(new FbpClient()),
{
    ui->setupUi(this);

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

void MainWindow::fileAdded( int fileId, const QString &fileName, int startProgress )
{
  addRow( fileId, fileName, startProgress );
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
