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
    connect(ui->typeBox, SIGNAL(currentIndexChanged(int)), this, SLOT(typeChanged(int)));
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
    }
    else
    {
        stopMining();
    }
}

void MiningPage::startMining()
{
    QStringList args;
    QString url = ui->serverLine->text();
    if (!url.contains("http://"))
        url.prepend("http://");
    QString urlLine = QString("%1:%2").arg(url, ui->portLine->text());
    QString userpassLine = QString("%1:%2").arg(ui->usernameLine->text(), ui->passwordLine->text());
    args << "--algo" << "scrypt";
    args << "--scantime" << ui->scantimeBox->text().toAscii();
    args << "--url" << urlLine.toAscii();
    args << "--userpass" << userpassLine.toAscii();
    args << "--threads" << ui->threadsBox->text().toAscii();

    threadSpeed.clear();

    acceptedShares = 0;
    rejectedShares = 0;

    roundAcceptedShares = 0;
    roundRejectedShares = 0;

    initThreads = ui->threadsBox->value();

#ifdef WIN32
    QString program = "minerd";
#else
    QString program = "./minerd";
#endif

    if (ui->debugCheckBox->isChecked())
        ui->list->addItem(args.join(" ").prepend(" ").prepend(program));

    minerProcess->start(program,args);
    minerProcess->waitForStarted(-1);

    readTimer->start(500);
}

void MiningPage::stopMining()
{
    ui->mineSpeedLabel->setText("Speed: N/A");
    ui->shareCount->setText("Accepted: 0 - Rejected: 0");
    minerProcess->kill();
    readTimer->stop();
}

void MiningPage::saveSettings()
{
    QSettings settings("easyminer.conf", QSettings::IniFormat);

    settings.setValue("threads", ui->threadsBox->text());
    settings.setValue("scantime", ui->scantimeBox->text());
    settings.setValue("url", ui->serverLine->text());
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
        ui->threadsBox->setValue(threads);
    }

    if (settings.value("scantime").isValid())
        ui->scantimeBox->setValue(settings.value("scantime").toInt());
    if (settings.value("url").isValid())
        ui->serverLine->setText(settings.value("url").toString());
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
        QStringList list = outputString.split("\n", QString::SkipEmptyParts);
        int i;
        for (i=0; i<list.size(); i++)
        {
            QString line = list.at(i);

            if (ui->debugCheckBox->isChecked())
                ui->list->addItem(line.trimmed());
            else
            {
                if (line.contains("PROOF OF WORK RESULT: true"))
                    reportToList("Share accepted", SHARE_SUCCESS, getTime(line));
                else if (line.contains("PROOF OF WORK RESULT: false"))
                    reportToList("Share rejected", SHARE_FAIL, getTime(line));
                else if (line.contains("LONGPOLL detected new block"))
                    reportToList("LONGPOLL detected a new block", LONGPOLL, getTime(line));
                else if (line.contains("Supported options:"))
                    reportToList("Miner didn't start properly. Try checking your settings.", ERROR, NULL);
                else if (line.contains("couldn't connect to host"))
                    reportToList("Couldn't connect. Please check pool server and port.", ERROR, NULL);
                else if (line.contains("The requested URL returned error: 403"))
                    reportToList("Couldn't connect. Please check your username and password.", ERROR, NULL);
            }

            if (line.contains("thread ") && line.contains("khash/sec"))
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
    if (error = QProcess::FailedToStart)
    {
        reportToList("Miner failed to start. Make sure you have the minerd executable and libraries in the same directory as Litecoin-QT.", ERROR, NULL);
    }
}

void MiningPage::minerFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    reportToList("Miner exited.", ERROR, NULL);
    minerActive = false;
    resetMiningButton();
}

void MiningPage::minerStarted()
{
    if (!minerActive)
        reportToList("Miner started. It usually takes a minute until the program starts reporting information.", STARTED, NULL);
    minerActive = true;
    resetMiningButton();
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
        ui->mineSpeedLabel->setText(QString("Speed: %1 khash/sec - %2 thread(s)").arg(speedString, threadsString));
    else
        ui->mineSpeedLabel->setText(QString("Speed: ~%1 khash/sec - %2 thread(s)").arg(speedString, threadsString));

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

void MiningPage::enableMiningControls(bool enable)
{
    ui->typeLabel->setEnabled(enable);
    ui->typeBox->setEnabled(enable);
    ui->threadsLabel->setEnabled(enable);
    ui->threadsBox->setEnabled(enable);
    ui->scantimeLabel->setEnabled(enable);
    ui->scantimeBox->setEnabled(enable);
    ui->serverLabel->setEnabled(enable);
    ui->serverLine->setEnabled(enable);
    ui->portLabel->setEnabled(enable);
    ui->portLine->setEnabled(enable);
    ui->usernameLabel->setEnabled(enable);
    ui->usernameLine->setEnabled(enable);
    ui->passwordLabel->setEnabled(enable);
    ui->passwordLine->setEnabled(enable);
}

void MiningPage::enablePoolMiningControls(bool enable)
{
    ui->scantimeLabel->setEnabled(enable);
    ui->scantimeBox->setEnabled(enable);
    ui->serverLabel->setEnabled(enable);
    ui->serverLine->setEnabled(enable);
    ui->portLabel->setEnabled(enable);
    ui->portLine->setEnabled(enable);
    ui->usernameLabel->setEnabled(enable);
    ui->usernameLine->setEnabled(enable);
    ui->passwordLabel->setEnabled(enable);
    ui->passwordLine->setEnabled(enable);
}

void MiningPage::typeChanged(int index)
{
    if (index == 0)  // Solo Mining
    {
        enablePoolMiningControls(false);
    }
    else if (index == 1)  // Pool Minig
    {
        enablePoolMiningControls(true);
    }
}

void MiningPage::resetMiningButton()
{
    ui->startButton->setText(minerActive ? "Stop Mining" : "Start Mining");
    enableMiningControls(!minerActive);
}
