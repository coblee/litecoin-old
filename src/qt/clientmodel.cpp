#include "clientmodel.h"
#include "guiconstants.h"
#include "optionsmodel.h"
#include "addresstablemodel.h"
#include "transactiontablemodel.h"

#include "headers.h"

#include <QTimer>
#include <QDateTime>

ClientModel::ClientModel(OptionsModel *optionsModel, QObject *parent) :
    QObject(parent), optionsModel(optionsModel),
    cachedNumConnections(0), cachedNumBlocks(0), cachedHashrate(0)
{
    // Until signal notifications is built into the bitcoin core,
    //  simply update everything after polling using a timer.
    QTimer *timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(update()));
    timer->start(MODEL_UPDATE_DELAY);

    numBlocksAtStartup = -1;

    // Read our specific settings from the wallet db
    /*
    CWalletDB walletdb(optionsModel->getWallet()->strWalletFile);
    walletdb.ReadSetting("miningDebug", miningDebug);
    walletdb.ReadSetting("miningScanTime", miningScanTime);
    std::string str;
    walletdb.ReadSetting("miningServer", str);
    miningServer = QString::fromStdString(str);
    walletdb.ReadSetting("miningPort", str);
    miningPort = QString::fromStdString(str);
    walletdb.ReadSetting("miningUsername", str);
    miningUsername = QString::fromStdString(str);
    walletdb.ReadSetting("miningPassword", str);
    miningPassword = QString::fromStdString(str);
    */
//    if (fGenerateBitcoins)
//    {
        miningType = SoloMining;
        miningStarted = true;
//    }
//    else
//    {
//        miningType = PoolMining;
//        walletdb.ReadSetting("miningStarted", miningStarted);
//    }
//    miningThreads = nLimitProcessors;
}

int ClientModel::getNumConnections() const
{
    return vNodes.size();
}

int ClientModel::getNumBlocks() const
{
    return nBestHeight;
}

int ClientModel::getNumBlocksAtStartup()
{
    if (numBlocksAtStartup == -1) numBlocksAtStartup = getNumBlocks();
    return numBlocksAtStartup;
}

ClientModel::MiningType ClientModel::getMiningType() const
{
    return miningType;
}

int ClientModel::getMiningThreads() const
{
    return miningThreads;
}

bool ClientModel::getMiningStarted() const
{
    return miningStarted;
}

bool ClientModel::getMiningDebug() const
{
    return miningDebug;
}

void ClientModel::setMiningDebug(bool debug)
{
    miningDebug = debug;
//    WriteSetting("miningDebug", miningDebug);
}

int ClientModel::getMiningScanTime() const
{
    return miningScanTime;
}

void ClientModel::setMiningScanTime(int scantime)
{
    miningScanTime = scantime;
//    WriteSetting("miningScanTime", miningScanTime);
}

QString ClientModel::getMiningServer() const
{
    return miningServer;
}

void ClientModel::setMiningServer(QString server)
{
    miningServer = server;
//    WriteSetting("miningServer", miningServer.toStdString());
}

QString ClientModel::getMiningPort() const
{
    return miningPort;
}

void ClientModel::setMiningPort(QString port)
{
    miningPort = port;
//    WriteSetting("miningPort", miningPort.toStdString());
}

QString ClientModel::getMiningUsername() const
{
    return miningUsername;
}

void ClientModel::setMiningUsername(QString username)
{
    miningUsername = username;
//    WriteSetting("miningUsername", miningUsername.toStdString());
}

QString ClientModel::getMiningPassword() const
{
    return miningPassword;
}

void ClientModel::setMiningPassword(QString password)
{
    miningPassword = password;
//    WriteSetting("miningPassword", miningPassword.toStdString());
}

int ClientModel::getHashrate() const
{
    if (GetTimeMillis() - nHPSTimerStart > 8000)
        return (boost::int64_t)0;
    return (boost::int64_t)dHashesPerSec;
}

// Litecoin: copied from bitcoinrpc.cpp.
double ClientModel::GetDifficulty() const
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.

    if (pindexBest == NULL)
        return 1.0;
    int nShift = (pindexBest->nBits >> 24) & 0xff;

    double dDiff =
        (double)0x0000ffff / (double)(pindexBest->nBits & 0x00ffffff);

    while (nShift < 29)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

QDateTime ClientModel::getLastBlockDate() const
{
    return QDateTime::fromTime_t(pindexBest->GetBlockTime());
}

void ClientModel::update()
{
    int newNumConnections = getNumConnections();
    int newNumBlocks = getNumBlocks();

    if(cachedNumConnections != newNumConnections)
        emit numConnectionsChanged(newNumConnections);
    if(cachedNumBlocks != newNumBlocks)
        emit numBlocksChanged(newNumBlocks);

    cachedNumConnections = newNumConnections;
    cachedNumBlocks = newNumBlocks;

    // Only need to update if solo mining. When pool mining, stats are pushed.
    if (miningType == SoloMining)
    {
        int newHashrate = getHashrate();
        if (cachedHashrate != newHashrate)
            emit miningChanged(miningStarted, newHashrate);
        cachedHashrate = newHashrate;
    }
}

bool ClientModel::isTestNet() const
{
    return fTestNet;
}

bool ClientModel::inInitialBlockDownload() const
{
    return IsInitialBlockDownload();
}

int ClientModel::getNumBlocksOfPeers() const
{
    return GetNumBlocksOfPeers();
}

void ClientModel::setMining(MiningType type, bool mining, int threads, int hashrate)
{
    if (type == SoloMining && mining != miningStarted)
    {
//            GenerateBitcoins(mining ? 1 : 0, getOptionsModel()->getWallet());
    }
    miningType = type;
    miningStarted = mining;
//    WriteSetting("miningStarted", mining);
//    WriteSetting("fLimitProcessors", 1);
//    WriteSetting("nLimitProcessors", threads);
    emit miningChanged(mining, hashrate);
}

QString ClientModel::getStatusBarWarnings() const
{
    return QString::fromStdString(GetWarnings("statusbar"));
}

OptionsModel *ClientModel::getOptionsModel()
{
    return optionsModel;
}

QString ClientModel::formatFullVersion() const
{
    return QString::fromStdString(FormatFullVersion());
}
