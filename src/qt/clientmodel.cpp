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
    int newHashrate = getHashrate();

    if(cachedNumConnections != newNumConnections)
        emit numConnectionsChanged(newNumConnections);
    if(cachedNumBlocks != newNumBlocks)
        emit numBlocksChanged(newNumBlocks);
    if(cachedHashrate != newHashrate)
        emit hashrateChanged(newHashrate);

    cachedNumConnections = newNumConnections;
    cachedNumBlocks = newNumBlocks;
    cachedHashrate = newHashrate;
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

OptionsModel *ClientModel::getOptionsModel()
{
    return optionsModel;
}

QString ClientModel::formatFullVersion() const
{
    return QString::fromStdString(FormatFullVersion());
}
