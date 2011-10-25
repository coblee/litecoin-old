#include "miningpage.h"
#include "ui_miningpage.h"

MiningPage::MiningPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MiningPage)
{
    ui->setupUi(this);

    setFixedSize(400, 420);

    checkSettings();

    minerActive = false;

    minerProcess = new QProcess(this);
    minerProcess->setProcessChannelMode(QProcess::MergedChannels);

    readTimer = new QTimer(this);

    acceptedShares = 0;
    rejectedShares = 0;

    roundAcceptedShares = 0;
    roundRejectedShares = 0;

    initThreads = 0;

    connect(readTimer, SIGNAL(timeout()), this, SLOT(readProcessOutput()));

    connect(ui->startButton, SIGNAL(pressed()), this, SLOT(startPressed()));
    connect(minerProcess, SIGNAL(started()), this, SLOT(minerStarted()));
    connect(minerProcess, SIGNAL(error(QProcess::ProcessError)), this, SLOT(minerError(QProcess::ProcessError)));
    connect(minerProcess, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(minerFinished(int, QProcess::ExitStatus)));
    connect(minerProcess, SIGNAL(readyReadStandardOutput()), this, SLOT(readProcessOutput()));
}

MiningPage::~MiningPage()
{
    minerProcess->kill();

    saveSettings();

    delete ui;
}

void MiningPage::startPressed()
{
    if (minerActive == false)
    {
        startMining();
        ui->startButton->setText("Stop Mining");
        minerActive = true;
    }
    else
    {
        stopMining();
        ui->startButton->setText("Start Mining");
        minerActive = false;
    }
}

void MiningPage::startMining()
{
    QStringList args;
    QString url = ui->rpcServerLine->text();
    if (!url.contains("http://"))
        url.prepend("http://");
    QString urlLine = QString("%1:%2").arg(url, ui->portLine->text());
    QString userpassLine = QString("%1:%2").arg(ui->usernameLine->text(), ui->passwordLine->text());
    args << "--algo" << "scrypt";
    args << "--s" << ui->scantimeLine->text().toAscii();
    args << "--url" << urlLine.toAscii();
    args << "--userpass" << userpassLine.toAscii();
    args << "--threads" << ui->threadsBox->currentText().toAscii();
    args << "-P";

    threadSpeed.clear();

    acceptedShares = 0;
    rejectedShares = 0;

    roundAcceptedShares = 0;
    roundRejectedShares = 0;

    initThreads = ui->threadsBox->currentText().toInt();

#ifdef WIN32
    QString program = "minerd";
#else
    QString program = "./minerd";
#endif

    minerProcess->start(program,args);
    minerProcess->waitForStarted(-1);

    readTimer->start(500);
}

void MiningPage::stopMining()
{
    reportToList("Miner stopped", 0, NULL);
    ui->mineSpeedLabel->setText("N/A");
    ui->shareCount->setText("Accepted: 0 - Rejected: 0");
    minerProcess->kill();
    readTimer->stop();
}

void MiningPage::saveSettings()
{
    QSettings settings("easyminer.conf", QSettings::IniFormat);

    settings.setValue("threads", ui->threadsBox->currentText());
    settings.setValue("scantime", ui->scantimeLine->text());
    settings.setValue("url", ui->rpcServerLine->text());
    settings.setValue("username", ui->usernameLine->text());
    settings.setValue("password", ui->passwordLine->text());
    settings.setValue("port", ui->portLine->text());

    settings.sync();
}

void MiningPage::checkSettings()
{
    QSettings settings("easyminer.conf", QSettings::IniFormat);
    if (settings.value("threads").isValid())
    {
        int threads = settings.value("threads").toInt();
        threads--;
        ui->threadsBox->setCurrentIndex(threads);
    }

    if (settings.value("scantime").isValid())
        ui->scantimeLine->setText(settings.value("scantime").toString());
    if (settings.value("url").isValid())
        ui->rpcServerLine->setText(settings.value("url").toString());
    if (settings.value("username").isValid())
        ui->usernameLine->setText(settings.value("username").toString());
    if (settings.value("password").isValid())
        ui->passwordLine->setText(settings.value("password").toString());
    if (settings.value("port").isValid())
        ui->portLine->setText(settings.value("port").toString());
}

void MiningPage::readProcessOutput()
{
    QByteArray output;

    minerProcess->reset();

    output = minerProcess->readAll();

    QString outputString(output);

    if (!outputString.isEmpty())
    {
        QStringList list = outputString.split("\n");
        int i;
        for (i=0; i<list.size(); i++)
        {
            QString line = list.at(i);
            if (line.contains("PROOF OF WORK RESULT: true"))
                reportToList("Share accepted", SHARE_SUCCESS, getTime(line));

            else if (line.contains("PROOF OF WORK RESULT: false"))
                reportToList("Share rejected", SHARE_FAIL, getTime(line));

            else if (line.contains("LONGPOLL detected new block"))
                reportToList("LONGPOLL detected a new block", LONGPOLL, getTime(line));

            else if (line.contains("Supported options:"))
                reportToList("Miner didn't start properly. Try checking your settings.", FATAL_ERROR, NULL);

            else if (line.contains("The requested URL returned error: 403"))
                reportToList("Couldn't connect. Try checking your username and password.", ERROR, NULL);
            else if (line.contains("thread ") && line.contains("khash/sec"))
            {
                QString threadIDstr = line.at(line.indexOf("thread ")+7);
                int threadID = threadIDstr.toInt();

                int threadSpeedindx = line.indexOf(",");
                QString threadSpeedstr = line.mid(threadSpeedindx);
                threadSpeedstr.chop(10);
                threadSpeedstr.remove(", ");
                threadSpeedstr.remove(" ");
                threadSpeedstr.remove('\n');
                double speed=0;
                speed = threadSpeedstr.toDouble();

                threadSpeed[threadID] = speed;

                updateSpeed();
            }
        }
    }
}

void MiningPage::minerError(QProcess::ProcessError error)
{
    QString errorMessage;
    switch(error)
    {
        case QProcess::Crashed:
            errorMessage = "Miner killed";
            break;

        case QProcess::FailedToStart:
            errorMessage = "Miner failed to start. Make sure you have the minerd executable and libraries in the same directory as Litecoin-QT.";
            break;

        default:
            errorMessage = "Unknown error";
            break;
    }

    reportToList(errorMessage, ERROR, NULL);

}

void MiningPage::minerFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    QString exitMessage;
    switch(exitStatus)
    {
        case QProcess::NormalExit:
            exitMessage = "Miner exited normally";

        case QProcess::CrashExit:
            exitMessage = "Miner exited.";

        default:
            exitMessage = "Miner exited abnormally.";
    }
}

void MiningPage::minerStarted()
{
    reportToList("Miner started. It usually takes a minute until the program starts reporting information.", STARTED, NULL);
}

void MiningPage::updateSpeed()
{
    double totalSpeed=0;
    int totalThreads=0;

    QMapIterator<int, double> iter(threadSpeed);
    while(iter.hasNext())
    {
        iter.next();
        totalSpeed += iter.value();
        totalThreads++;
    }

    // If all threads haven't reported the hash speed yet, make an assumption
    if (totalThreads != initThreads)
    {
        totalSpeed = (totalSpeed/totalThreads)*initThreads;
    }

    QString speedString = QString("%1").arg(totalSpeed);
    QString threadsString = QString("%1").arg(initThreads);

    QString acceptedString = QString("%1").arg(acceptedShares);
    QString rejectedString = QString("%1").arg(rejectedShares);

    QString roundAcceptedString = QString("%1").arg(roundAcceptedShares);
    QString roundRejectedString = QString("%1").arg(roundRejectedShares);

    if (totalThreads == initThreads)
        ui->mineSpeedLabel->setText(QString("%1 khash/sec - %2 thread(s)").arg(speedString, threadsString));
    else
        ui->mineSpeedLabel->setText(QString("~%1 khash/sec - %2 thread(s)").arg(speedString, threadsString));

    ui->shareCount->setText(QString("Accepted: %1 (%3) - Rejected: %2 (%4)").arg(acceptedString, rejectedString, roundAcceptedString, roundRejectedString));
}

void MiningPage::reportToList(QString msg, int type, QString time)
{
    QString message;
    if (time == NULL)
        message = QString("[%1] - %2").arg(QTime::currentTime().toString(), msg);
    else
        message = QString("[%1] - %2").arg(time, msg);

    switch(type)
    {
        case SHARE_SUCCESS:
            acceptedShares++;
            roundAcceptedShares++;
            updateSpeed();
            break;

        case SHARE_FAIL:
            rejectedShares++;
            roundRejectedShares++;
            updateSpeed();
            break;

        case FATAL_ERROR:
            startPressed();
            break;

        case LONGPOLL:
            roundAcceptedShares = 0;
            roundRejectedShares = 0;
            break;

        default:
            break;
    }

    ui->list->addItem(message);
    ui->list->scrollToBottom();
}

// Function for fetching the time
QString MiningPage::getTime(QString time)
{
    if (time.contains("["))
    {
        time.resize(21);
        time.remove("[");
        time.remove("]");
        time.remove(0,11);

        return time;
    }
    else
        return NULL;
}
