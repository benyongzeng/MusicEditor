/**************************************************************************
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**************************************************************************/

#include "ffmpeg.h"
#include "mainwindow.h"
#include "meaudiodecoder.h"
#include "meauidoencoder.h"
#include "asynchronous_decode.h"
#include <assert.h>
#include "QWave2/Waveform.h"
#include "QWave2/WaveformVRuler.h"
#include <QWave2/SndFile.h>
#include <QWave2/Waveform.h>
#include <QWave2/WaveformScrollBar.h>
#include <QWave2/WaveformRuler.h>
#include <QWave2/WaveformCursorProxy.h>
#include <QWave2/WaveformSelectionProxy.h>
#include <QWave2/TimeLabel.h>
#include "meunity.h"



QFutureWatcher< QVector<short> > *MainWindow::decoderWatcher=NULL;
QFutureWatcher<void> *MainWindow::encoderWatcher=NULL;
//![0]
MainWindow::MainWindow()
{
    audioOutput = new Phonon::AudioOutput(Phonon::MusicCategory, this);
    mediaObject = new Phonon::MediaObject(this);
    metaInformationResolver = new Phonon::MediaObject(this);

    mediaObject->setTickInterval(1000);
//![0]
//![2]
    connect(mediaObject, SIGNAL(tick(qint64)), this, SLOT(tick(qint64)));
    connect(mediaObject, SIGNAL(stateChanged(Phonon::State,Phonon::State)),
            this, SLOT(stateChanged(Phonon::State,Phonon::State)));
    connect(metaInformationResolver, SIGNAL(stateChanged(Phonon::State,Phonon::State)),
            this, SLOT(metaStateChanged(Phonon::State,Phonon::State)));
    connect(mediaObject, SIGNAL(currentSourceChanged(Phonon::MediaSource)),
            this, SLOT(sourceChanged(Phonon::MediaSource)));
    connect(mediaObject, SIGNAL(aboutToFinish()), this, SLOT(aboutToFinish()));
//![2]

//![1]
    Phonon::createPath(mediaObject, audioOutput);
//![1]
//    decoder=new MEAudioDecoder();

    setupActions();
    setupMenus();
    setupUi();
    decoderWatcher=new QFutureWatcher< QVector< short > >(this);
    encoderWatcher=new QFutureWatcher<void>(this);
    connect(decoderWatcher,SIGNAL(resultReadyAt(int)),this,SLOT(showCurve(int)));
    previousRow=-1;
    justPaintRow=-1;
    timeLcd->display("00:00");

}

MainWindow::~MainWindow()
{
    if(decoderWatcher)
        delete decoderWatcher;
    if(encoderWatcher)
        delete encoderWatcher;
    decoderWatcher=0;
    encoderWatcher=0;
//    delete decoder;
    QMap<int,MEAudioDecoder*>::Iterator it;
    for( it = decoders.begin();  it != decoders.end();  ++it)
    {
        MEAudioDecoder* decoder=(MEAudioDecoder*)it.value();
        decoder->release();
    }


}

//![6]
void MainWindow::addFiles()
{
    QStringList files = QFileDialog::getOpenFileNames(this, tr("Select Music Files"),
        QDesktopServices::storageLocation(QDesktopServices::MusicLocation));

    if (files.isEmpty())
        return;

    int index = sources.size();
    QString file;
    foreach (QString string, files) {
            Phonon::MediaSource source(string);
            sources.append(source);
            file=string;
            break;
    }
    if (!sources.isEmpty())
        metaInformationResolver->setCurrentSource(sources.at(index));
}
//![6]

void MainWindow::about()
{
    QMessageBox::information(this, tr("About Music Editor"),
        tr("The Music Player example shows how to use Phonon - the multimedia"
           " framework that comes with Qt - to create a simple music player."));
}

void MainWindow::translateMusicFormat()
{
    QString file = QFileDialog::getSaveFileName(this,tr("save file"),QDesktopServices::storageLocation(QDesktopServices::MusicLocation),
                                                "Music (*.mp3 *.wav *.wma)");


    int64_t seekTime=mediaObject->currentTime();

    if(file.length())
    {
        if(!QFile::exists(file))
        {
            QFile musicFile(file);
            musicFile.open(QIODevice::Truncate);
            char a='\0';
            musicFile.write(&a);
            musicFile.close();
        }
        MEAudioDecoder* decoder=decoders[musicTable->currentRow()<0?0:musicTable->currentRow()];
        encoderWatcher->setFuture(QtConcurrent::run(AsynchronousEncoder,file,decoder,seekTime));
    }


}

//![9]
void MainWindow::stateChanged(Phonon::State newState, Phonon::State /* oldState */)
{
    switch (newState) {
        case Phonon::ErrorState:
            if (mediaObject->errorType() == Phonon::FatalError) {
                QMessageBox::warning(this, tr("Fatal Error"),
                mediaObject->errorString());
            } else {
                QMessageBox::warning(this, tr("Error"),
                mediaObject->errorString());
            }
            break;
//![9]
//![10]
        case Phonon::PlayingState:
                playAction->setEnabled(false);
                pauseAction->setEnabled(true);
                stopAction->setEnabled(true);
                addFilesAction->setEnabled(false);
                translateAction->setEnabled(false);
                break;
        case Phonon::StoppedState:
                stopAction->setEnabled(false);
                playAction->setEnabled(true);
                pauseAction->setEnabled(false);
                addFilesAction->setEnabled(true);
                translateAction->setEnabled(true);
                timeLcd->display("00:00");
                break;
        case Phonon::PausedState:
                pauseAction->setEnabled(false);
                stopAction->setEnabled(true);
                playAction->setEnabled(true);
                addFilesAction->setEnabled(true);
                translateAction->setEnabled(true);
                break;
//![10]
        case Phonon::BufferingState:
                break;
        default:

            ;
    }
    if(musicTable->columnCount())
    {
        nextAction->setEnabled(true);
        previousAction->setEnabled(true);
    }
    else
    {
        nextAction->setEnabled(false);
        previousAction->setEnabled(false);
    }
}

//![11]
void MainWindow::tick(qint64 time)
{
    QTime displayTime(0, (time / 60000) % 60, (time / 1000) % 60);

    timeLcd->display(displayTime.toString("mm:ss"));
    if(time>this->dur||time<this->beg)
        this->mediaObject->pause();
}
//![11]

//![12]
void MainWindow::tableClicked(int row, int /* column */)
{
    if(previousRow==row)
        return;
    else
        previousRow=row;

    bool wasPlaying = mediaObject->state() == Phonon::PlayingState;

    mediaObject->stop();
    mediaObject->clearQueue();

    if (row >= sources.size())
        return;

    mediaObject->setCurrentSource(sources[row]);

    MEAudioDecoder* decoder=decoders[row];
//    decoder->dealloc();
//    decoder->OpenFile(sources[row].fileName());

    if (wasPlaying)
        mediaObject->play();
    else
        mediaObject->stop();
}


void MainWindow::sourceChanged(const Phonon::MediaSource &source)
{
    musicTable->selectRow(sources.indexOf(source));
    timeLcd->display("00:00");
}


void MainWindow::metaStateChanged(Phonon::State newState, Phonon::State /* oldState */)
{
    if (newState == Phonon::ErrorState) {
        QMessageBox::warning(this, tr("Error opening files"),
            metaInformationResolver->errorString());
        while (!sources.isEmpty() &&
               !(sources.takeLast() == metaInformationResolver->currentSource())) {} /* loop */;
        return;
    }

    if (newState != Phonon::StoppedState && newState != Phonon::PausedState)
        return;

    if (metaInformationResolver->currentSource().type() == Phonon::MediaSource::Invalid)
            return;

    QMap<QString, QString> metaData = metaInformationResolver->metaData();

    QString title = metaData.value(tr("TITLE"));
    QTableWidgetItem *titleItem = NULL;
    titleItem=new QTableWidgetItem(metaInformationResolver->currentSource().fileName());

    QWidget *titleWidget=new QWidget(musicTable);

    titleItem->setFlags(titleItem->flags() ^ Qt::ItemIsEditable);

    int currentRow = musicTable->rowCount();
    musicTable->insertRow(currentRow);
    justPaintRow=currentRow;
//    musicTable->setItem(currentRow, 0, titleItem);
    musicTable->setCellWidget(currentRow,0,titleWidget);
    QString file=metaInformationResolver->currentSource().fileName();
    MEAudioDecoder* decoder=new MEAudioDecoder();
    decoders.insert(currentRow,static_cast<MEAudioDecoder*>(decoder));
    decoderWatcher->setFuture(QtConcurrent::run(AsynchronousDecoder,file,decoder));
    QWidget *waveFormWidget=new QWidget(musicTable);
    waveFormWidget->resize(1000,300);
//    Plot* plot=new Plot(musicTable);
    musicTable->setCellWidget(currentRow,1,waveFormWidget);

    if (musicTable->selectedItems().isEmpty()) {
        musicTable->selectRow(0);
        mediaObject->setCurrentSource(metaInformationResolver->currentSource());
    }

    Phonon::MediaSource source = metaInformationResolver->currentSource();
    int index = sources.indexOf(metaInformationResolver->currentSource()) + 1;
    if (sources.size() > index) {
        metaInformationResolver->setCurrentSource(sources.at(index));
    }
    else {
        musicTable->resizeColumnsToContents();
        musicTable->setColumnWidth(1, 1000);
         musicTable->setColumnWidth(0, 200);
         musicTable->setRowHeight(currentRow,200);
    }
}
//![15]

//![16]
void MainWindow::aboutToFinish()
{
    int index = sources.indexOf(mediaObject->currentSource()) + 1;
    if (sources.size() > index) {
        mediaObject->enqueue(sources.at(index));
    }
}
//![16]

void MainWindow::setupActions()
{
    playAction = new QAction(style()->standardIcon(QStyle::SP_MediaPlay), tr("Play"), this);
    playAction->setShortcut(tr("Ctrl+P"));
    playAction->setDisabled(true);
    pauseAction = new QAction(style()->standardIcon(QStyle::SP_MediaPause), tr("Pause"), this);
    pauseAction->setShortcut(tr("Ctrl+A"));
    pauseAction->setDisabled(true);
    stopAction = new QAction(style()->standardIcon(QStyle::SP_MediaStop), tr("Stop"), this);
    stopAction->setShortcut(tr("Ctrl+S"));
    stopAction->setDisabled(true);
    nextAction = new QAction(style()->standardIcon(QStyle::SP_MediaSkipForward), tr("Next"), this);
    nextAction->setShortcut(tr("Ctrl+N"));
    nextAction->setDisabled(true);
    previousAction = new QAction(style()->standardIcon(QStyle::SP_MediaSkipBackward), tr("Previous"), this);
    previousAction->setShortcut(tr("Ctrl+R"));
    previousAction->setDisabled(true);
    addFilesAction = new QAction(tr("Import &Files"), this);
    addFilesAction->setShortcut(tr("Ctrl+F"));
    exitAction = new QAction(tr("E&xit"), this);
    exitAction->setShortcuts(QKeySequence::Quit);
    aboutAction = new QAction(tr("A&bout"), this);
    aboutAction->setShortcut(tr("Ctrl+B"));
    aboutQtAction = new QAction(tr("About &Qt"), this);
    aboutQtAction->setShortcut(tr("Ctrl+Q"));
    translateAction = new QAction(style()->standardIcon(QStyle::SP_DirClosedIcon),tr("&Export File"),this);

//![5]
    connect(playAction, SIGNAL(triggered()), mediaObject, SLOT(play()));
    connect(pauseAction, SIGNAL(triggered()), mediaObject, SLOT(pause()) );
    connect(stopAction, SIGNAL(triggered()), mediaObject, SLOT(stop()));
//![5]
    connect(addFilesAction, SIGNAL(triggered()), this, SLOT(addFiles()));
    connect(exitAction, SIGNAL(triggered()), this, SLOT(close()));
    connect(aboutAction, SIGNAL(triggered()), this, SLOT(about()));
    connect(aboutQtAction, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
    connect(translateAction, SIGNAL(triggered()), this, SLOT(translateMusicFormat()));
}

void MainWindow::setupMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(addFilesAction);
    fileMenu->addSeparator();
    fileMenu->addAction(translateAction);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction);

    QMenu *aboutMenu = menuBar()->addMenu(tr("&Help"));
    aboutMenu->addAction(aboutAction);
    aboutMenu->addAction(aboutQtAction);
}

//![3]
void MainWindow::setupUi()
{
//![3]
    QToolBar *bar = new QToolBar;
    bar->addAction(previousAction);
    bar->addAction(playAction);
    bar->addAction(pauseAction);
    bar->addAction(stopAction);   
    bar->addAction(nextAction);
//    bar->addAction(translateAction);

//![4]
    seekSlider = new Phonon::SeekSlider(this);
    seekSlider->setMediaObject(mediaObject);

    volumeSlider = new Phonon::VolumeSlider(this);
    volumeSlider->setAudioOutput(audioOutput);
//![4]
    volumeSlider->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);

    QLabel *volumeLabel = new QLabel;
    volumeLabel->setPixmap(QPixmap("images/volume.png"));

    QPalette palette;
    palette.setBrush(QPalette::Light, Qt::darkGray);

    timeLcd = new QLCDNumber;
    timeLcd->setPalette(palette);

    QStringList headers;
//    headers << tr("Title") << tr("Artist") << tr("Album") << tr("Year");
    headers<<tr("File name")<<tr("Waveform");
    musicTable = new QTableWidget(0, 2);
    musicTable->setHorizontalHeaderLabels(headers);
    musicTable->setSelectionMode(QAbstractItemView::SingleSelection);
    musicTable->setSelectionBehavior(QAbstractItemView::SelectItems);
    musicTable->setEditTriggers ( QAbstractItemView::NoEditTriggers );
    connect(musicTable, SIGNAL(cellPressed(int,int)),
            this, SLOT(tableClicked(int,int)));
    musicTable->setColumnWidth(1,1200);
    QHBoxLayout *seekerLayout = new QHBoxLayout;
    seekerLayout->addWidget(seekSlider);
    seekerLayout->addWidget(timeLcd);

    QHBoxLayout *playbackLayout = new QHBoxLayout;
    playbackLayout->addWidget(bar);
    playbackLayout->addStretch();
    playbackLayout->addWidget(volumeLabel);
    playbackLayout->addWidget(volumeSlider);

    QVBoxLayout *mainLayout = new QVBoxLayout;



    mainLayout->addLayout(seekerLayout);
    mainLayout->addLayout(playbackLayout);
    mainLayout->addWidget(musicTable);

    QWidget *widget = new QWidget;
    widget->setLayout(mainLayout);

    setCentralWidget(widget);
    setWindowTitle("Music Editor");
}

void MainWindow::showCurve(int num)
{
    int currentRow=this->justPaintRow;
    currentRow=currentRow<0?0:currentRow;
    QWidget* waveFromWidget=dynamic_cast<QWidget*>(musicTable->cellWidget(currentRow,1));

    MEAudioDecoder* decoder=decoders[num];
    QWave2::SndFile* sndFile=new QWave2::SndFile(static_cast<MEAudioDecoder*>(decoder));
    sndFile->data=decoderWatcher->resultAt(num);
    QWidget *titleWidget =dynamic_cast<QWidget*>(musicTable->cellWidget(currentRow,0));
//    QVBoxLayout *titleLayout =dynamic_cast<QVBoxLayout*>(titleWidget->layout());
    qDebug()<<"Total time:"<<mediaObject->totalTime();
    QGridLayout* grid=MEUnity::unity()->creatWaveFromPanel(sndFile,this,mediaObject->totalTime()/1000);
    QVBoxLayout* titleLayout=MEUnity::unity()->creatTitlePanel(decoder);

    if(sndFile->data.count())
    {
        titleWidget->setLayout(titleLayout);

        waveFromWidget->setLayout(grid);
    }
    else
    {
        musicTable->removeRow(currentRow);
        sources.removeAt(currentRow);
        metaInformationResolver->clearQueue();
        mediaObject->clearQueue();
        decoders[currentRow]->release();
        this->decoders.remove(currentRow);
        QMessageBox::information(this, tr("Alert"),
            tr("Fail to open the file"));
    }
    justPaintRow=-1;
}

void MainWindow::changeSelection(double beg, double dur, Waveform*)
{
    this->beg=beg*1000;
    this->dur=dur*1000+this->beg;
    mediaObject->seek((qint64)beg*1000);
    mediaObject->play();
}

void
MainWindow::setTime(Waveform*,double t)
{

}

