// Copyright (c) 2009-2017 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Developers
// Copyright (c) 2016-2017 Duality Blockchain Solutions Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "clientmodel.h"

#include "bantablemodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "peertablemodel.h"

#include "alert.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "clientversion.h"
#include "net.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "util.h"

#include <stdint.h>

#include <QDebug>
#include <QTimer>

static const int64_t nClientStartupTime = GetTime();
static int64_t nLastHeaderTipUpdateNotification = 0;
static int64_t nLastBlockTipUpdateNotification = 0;

ClientModel::ClientModel(OptionsModel *optionsModel, QObject *parent) :
    QObject(parent),
    optionsModel(optionsModel),
    peerTableModel(0),
    banTableModel(0),
    cachedNumBlocks(0),
    cachedReindexing(0), 
    cachedImporting(0),
    numBlocksAtStartup(-1),
    pollTimer(0)
{
    peerTableModel = new PeerTableModel(this);
    banTableModel = new BanTableModel(this);
    pollTimer = new QTimer(this);
    connect(pollTimer, SIGNAL(timeout()), this, SLOT(updateTimer()));
    pollTimer->start(MODEL_UPDATE_DELAY);

    subscribeToCoreSignals();
}

ClientModel::~ClientModel()
{
    unsubscribeFromCoreSignals();
}

int ClientModel::getNumConnections(unsigned int flags) const
{
    LOCK(cs_vNodes);
    if (flags == CONNECTIONS_ALL) // Shortcut if we want total
        return vNodes.size();

    int nNum = 0;
    Q_FOREACH(CNode* pnode, vNodes)
    if (flags & (pnode->fInbound ? CONNECTIONS_IN : CONNECTIONS_OUT))
        nNum++;

    return nNum;
}

int ClientModel::getNumBlocks() const
{
    LOCK(cs_main);
    return chainActive.Height();
}

int ClientModel::getNumBlocksAtStartup()
{
    if (numBlocksAtStartup == -1) numBlocksAtStartup = getNumBlocks();
    return numBlocksAtStartup;
}

int ClientModel::getHeaderTipHeight() const
{
    LOCK(cs_main);
    if (!pindexBestHeader)
        return 0;
    return pindexBestHeader->nHeight;
}

int64_t ClientModel::getHeaderTipTime() const
{
    LOCK(cs_main);
    if (!pindexBestHeader)
        return 0;
    return pindexBestHeader->GetBlockTime();
}

quint64 ClientModel::getTotalBytesRecv() const
{
    return CNode::GetTotalBytesRecv();
}

quint64 ClientModel::getTotalBytesSent() const
{
    return CNode::GetTotalBytesSent();
}

QDateTime ClientModel::getLastBlockDate() const
{
    LOCK(cs_main);
    if (chainActive.Tip())
        return QDateTime::fromTime_t(chainActive.Tip()->GetBlockTime());
    else
        return QDateTime::fromTime_t(Params().GenesisBlock().GetBlockTime()); // Genesis block's time of current network
}

double ClientModel::getVerificationProgress(const CBlockIndex *tipIn) const
{
    CBlockIndex *tip = const_cast<CBlockIndex *>(tipIn);
    if (!tip)
    {
        LOCK(cs_main);
        tip = chainActive.Tip();
    }
    return Checkpoints::GuessVerificationProgress(Params().Checkpoints(), tip);
}

long ClientModel::getMempoolSize() const
{
    return mempool.size();
}

size_t ClientModel::getMempoolDynamicUsage() const
{
    return mempool.DynamicMemoryUsage();
}

void ClientModel::updateTimer()
{
    // no locking required at this point
    // the following calls will aquire the required lock
    Q_EMIT mempoolSizeChanged(getMempoolSize(), getMempoolDynamicUsage());
    Q_EMIT bytesChanged(getTotalBytesRecv(), getTotalBytesSent());
}

void ClientModel::updateNumConnections(int numConnections)
{
    Q_EMIT numConnectionsChanged(numConnections);
}

void ClientModel::updateAlert(const QString &hash, int status)
{
    // Show error message notification for new alert
    if(status == CT_NEW)
    {
        uint256 hash_256;
        hash_256.SetHex(hash.toStdString());
        CAlert alert = CAlert::getAlertByHash(hash_256);
        if(!alert.IsNull())
        {
            Q_EMIT message(tr("Network Alert"), QString::fromStdString(alert.strStatusBar), CClientUIInterface::ICON_ERROR);
        }
    }

    Q_EMIT alertsChanged(getStatusBarWarnings());
}

bool ClientModel::inInitialBlockDownload() const
{
    return IsInitialBlockDownload();
}

enum BlockSource ClientModel::getBlockSource() const
{
    if (fReindex)
        return BLOCK_SOURCE_REINDEX;
    else if (fImporting)
        return BLOCK_SOURCE_DISK;
    else if (getNumConnections() > 0)
        return BLOCK_SOURCE_NETWORK;

    return BLOCK_SOURCE_NONE;
}

QString ClientModel::getStatusBarWarnings() const
{
    return QString::fromStdString(GetWarnings("statusbar"));
}

OptionsModel *ClientModel::getOptionsModel()
{
    return optionsModel;
}

PeerTableModel *ClientModel::getPeerTableModel()
{
    return peerTableModel;
}

BanTableModel *ClientModel::getBanTableModel()
{
    return banTableModel;
}

QString ClientModel::formatFullVersion() const
{
    return QString::fromStdString(FormatFullVersion());
}

QString ClientModel::formatSubVersion() const
{
    return QString::fromStdString(strSubVersion);
}

QString ClientModel::formatBuildDate() const
{
    return QString::fromStdString(CLIENT_DATE);
}

bool ClientModel::isReleaseVersion() const
{
    return CLIENT_VERSION_IS_RELEASE;
}

QString ClientModel::clientName() const
{
    return QString::fromStdString(CLIENT_NAME);
}

QString ClientModel::formatClientStartupTime() const
{
    return QDateTime::fromTime_t(nClientStartupTime).toString();
}

void ClientModel::updateBanlist()
{
    banTableModel->refresh();
}

QString ClientModel::dataDir() const
{
    return GUIUtil::boostPathToQString(GetDataDir());
}

// Handlers for core signals
static void ShowProgress(ClientModel *clientmodel, const std::string &title, int nProgress)
{
    // emits signal "showProgress"
    QMetaObject::invokeMethod(clientmodel, "showProgress", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(title)),
                              Q_ARG(int, nProgress));
}

static void NotifyNumConnectionsChanged(ClientModel *clientmodel, int newNumConnections)
{
    // Too noisy: qDebug() << "NotifyNumConnectionsChanged : " + QString::number(newNumConnections);
    QMetaObject::invokeMethod(clientmodel, "updateNumConnections", Qt::QueuedConnection,
                              Q_ARG(int, newNumConnections));
}

static void NotifyAlertChanged(ClientModel *clientmodel, const uint256 &hash, ChangeType status)
{
    qDebug() << "NotifyAlertChanged : " + QString::fromStdString(hash.GetHex()) + " status=" + QString::number(status);
    QMetaObject::invokeMethod(clientmodel, "updateAlert", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(hash.GetHex())),
                              Q_ARG(int, status));
}

static void BannedListChanged(ClientModel *clientmodel)
{
    qDebug() << QString("%1: Requesting update for peer banlist").arg(__func__);
    QMetaObject::invokeMethod(clientmodel, "updateBanlist", Qt::QueuedConnection);
}

static void BlockTipChanged(ClientModel *clientmodel, bool initialSync, const CBlockIndex *pIndex, bool fHeader)
{
    // lock free async UI updates in case we have a new block tip
    // during initial sync, only update the UI if the last update
    // was > 250ms (MODEL_UPDATE_DELAY) ago
    int64_t now = 0;
    if (initialSync)
        now = GetTimeMillis();

    int64_t& nLastUpdateNotification = fHeader ? nLastHeaderTipUpdateNotification : nLastBlockTipUpdateNotification;

    // if we are in-sync, update the UI regardless of last update time
    if (!initialSync || now - nLastUpdateNotification > MODEL_UPDATE_DELAY) {
        //pass a async signal to the UI thread
        QMetaObject::invokeMethod(clientmodel, "numBlocksChanged", Qt::QueuedConnection,
                                  Q_ARG(int, pIndex->nHeight),
                                  Q_ARG(QDateTime, QDateTime::fromTime_t(pIndex->GetBlockTime())),
                                  Q_ARG(double, clientmodel->getVerificationProgress(pIndex)),
                                  Q_ARG(bool, fHeader));
        nLastUpdateNotification = now;
        nLastBlockTipUpdateNotification = now;
    }
}

void ClientModel::subscribeToCoreSignals()
{
    // Connect signals to client
    uiInterface.ShowProgress.connect(boost::bind(ShowProgress, this, _1, _2));
    uiInterface.NotifyNumConnectionsChanged.connect(boost::bind(NotifyNumConnectionsChanged, this, _1));
    uiInterface.NotifyAlertChanged.connect(boost::bind(NotifyAlertChanged, this, _1, _2));
    uiInterface.BannedListChanged.connect(boost::bind(BannedListChanged, this));
    uiInterface.NotifyBlockTip.connect(boost::bind(BlockTipChanged, this, _1, _2, false));
    uiInterface.NotifyHeaderTip.connect(boost::bind(BlockTipChanged, this, _1, _2, true));
}

void ClientModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    uiInterface.ShowProgress.disconnect(boost::bind(ShowProgress, this, _1, _2));
    uiInterface.NotifyNumConnectionsChanged.disconnect(boost::bind(NotifyNumConnectionsChanged, this, _1));
    uiInterface.NotifyAlertChanged.disconnect(boost::bind(NotifyAlertChanged, this, _1, _2));
    uiInterface.BannedListChanged.disconnect(boost::bind(BannedListChanged, this));
    uiInterface.NotifyBlockTip.disconnect(boost::bind(BlockTipChanged, this, _1, _2, false));
    uiInterface.NotifyHeaderTip.disconnect(boost::bind(BlockTipChanged, this, _1, _2, true));
}
