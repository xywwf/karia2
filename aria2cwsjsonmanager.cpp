// aria2cwsjsonmanager.cpp --- 
// 
// Author: liuguangzhao
// Copyright (C) 2007-2013 liuguangzhao@users.sf.net
// URL: 
// Created: 2013-10-14 13:23:16 +0000
// Version: $Id$
// 

#include "simplelog.h"

#include "taskqueue.h"
#include "taskinfodlg.h"
#include "karia2statcalc.h"

#include "aria2rpcserver.h"
#include "aria2cwsjsonmanager.h"

Aria2CWSJsonManager::Aria2CWSJsonManager(bool useSsl, QObject *parent)
    : Aria2RpcManager()
{
    this->mUseSsl = useSsl;
    QObject::connect(&this->mAriaUpdater, &QTimer::timeout, this, &Aria2CWSJsonManager::onAriaUpdaterTimeout);
    QObject::connect(this, &Aria2CWSJsonManager::jaresponse, this, &Aria2CWSJsonManager::onStatArrived);
}

Aria2CWSJsonManager::~Aria2CWSJsonManager()
{
}

int Aria2CWSJsonManager::addTask(int task_id, const QString &url, TaskOption *to)
{
    // Aria2WSJsonRpcClient *jrpc = new Aria2WSJsonRpcClient(this->mRpcServer->getRpcUri(Aria2RpcServer::AST_JSONRPC_WS));
    // mWSJsonRpcs[task_id] = jrpc;

    if (mws == NULL) {
        mws = new QWebSocket();
        QObject::connect(mws, &QWebSocket::connected, this, &Aria2CWSJsonManager::onWSClientConnected);
        QObject::connect(mws, &QWebSocket::disconnected, this, &Aria2CWSJsonManager::onWSClientDisconnected);
        QObject::connect(mws, &QWebSocket::textMessageReceived, this, &Aria2CWSJsonManager::onWSMessageReceived);
        QObject::connect(mws, SIGNAL(error(QAbstractSocket::SocketError)),
                         this, SLOT(onWSClientError(QAbstractSocket::SocketError)));

        QString wsuri = this->mRpcServer->getRpcUri(Aria2RpcServer::AST_JSONRPC_WS);
        mws->open(wsuri);
    }

    Karia2StatCalc* sc = NULL;
    this->m_tasks[task_id] = 0;
    sc = new Karia2StatCalc(task_id, 1000);
    QObject::connect(sc, &Karia2StatCalc::progressStat, this, &Aria2CWSJsonManager::onAllStatArrived);
    statCalcs_.insert(task_id, sc);

    QMap<QString, QVariant> payload;
    QVariantList args;
    QList<QVariant> uris;
    QMap<QString, QVariant> options;
    QString aria2RpcMethod;

    {
        // create task object first
        payload["taskId"] = QString("%1").arg(task_id);
        payload["url"] = url;
    }

    options["gid"] = this->tid2hex(task_id);
    options["max-download-limit"] = "20000";
    options["max-connection-per-server"] = "3";
    options["min-split-size"] = "1M";

    if (url.startsWith("file://") && url.endsWith(".torrent")) {
        aria2RpcMethod = QString("aria2.addTorrent");
        
        QFile torrentFile(url.right(url.length() - 7));
        torrentFile.open(QIODevice::ReadOnly);
        QByteArray torrentConntent = torrentFile.readAll();
        torrentFile.close();

        args.insert(0, torrentConntent);
        args.insert(1, uris);

        // options["split"] = QString("5");
        options["split"] = to->mSplitCount;
        options["dir"] = to->mSavePath;
        args.insert(2, options);
    } else if (url.startsWith("file://") && url.endsWith(".metalink")) {
        aria2RpcMethod = QString("aria2.addMetalink");
        
        QFile metalinkFile(url.right(url.length() - 7));
        metalinkFile.open(QIODevice::ReadOnly);
        QByteArray metalinkContent = metalinkFile.readAll();
        metalinkFile.close();

        args.insert(0, metalinkContent);
        // options["split"] = QString("5");
        options["split"] = to->mSplitCount;
        options["dir"] = to->mSavePath;
        args.insert(1, options);

    } else {
        aria2RpcMethod = QString("aria2.addUri");
        uris << QString(url);
        args.insert(0, uris);
    
        options["split"] = QString("3");
        // options["split"] = to->mSplitCount;
        options["dir"] = to->mSavePath;
        args.insert(1, options);
    }

    this->sendMessage(mws, aria2RpcMethod, args, QVariant(payload));
    /*
    jrpc->call(aria2RpcMethod, args, QVariant(payload),
               this, SLOT(onAriaAddUriResponse(QJsonObject&, QNetworkReply*, QVariant &)),
               this, SLOT(onAriaAddUriFault(int, QString, QNetworkReply*, QVariant &)));
    */
    if (!this->mAriaUpdater.isActive()) {
        this->mAriaUpdater.setInterval(3000);
        // QObject::connect(&this->mAriaUpdater, &QTimer::timeout, this, &Aria2CWSJsonManager::onAriaUpdaterTimeout);
        this->mAriaUpdater.start();
    }

    return 0;
}

int Aria2CWSJsonManager::pauseTask(int task_id)
{
    return 0;
}

//////// statqueue members
// TODO 线程需要一直运行处理等待状态，否则不断启动线程会用掉太多的资源。
void Aria2CWSJsonManager::run()
{
    int stkey;
    Aria2StatCollector *sclt;
    QPair<int, Aria2StatCollector*> elem;
    int tid = -1;

    while (!this->stkeys.empty()) {
        this->stkLocker.lock();
        elem = this->stkeys.dequeue();
        this->stkLocker.unlock();

        stkey = elem.first;
        sclt = elem.second;
        tid = sclt->tid;

        if (this->belongsTos.contains(sclt->strGid)) {
            sclt->strBelongsTo = QString("%1").arg(tid);
        }
        qLogx()<<"dispatching stat event:"<<stkey<<tid;

        if (stkey < 0) {
            // 任务完成事件
            this->confirmBackendFinished(tid, NULL);
        } else {
            this->checkAndDispatchStat(sclt);
            this->checkAndDispatchServerStat(sclt);
        }

        // 子任务关系
        if (sclt->strFollowedBy.size() > 0) {
            for (int i = 0; i < sclt->strFollowedBy.size(); i++) {
                if (!this->belongsTos.contains(sclt->strFollowedBy.at(i))) {
                    this->belongsTos[sclt->strFollowedBy.at(i)] = tid;
                }
            }
            this->getAria2ChildStatus(sclt->strFollowedBy, tid);
        }

        delete sclt;
    }
    
}

bool Aria2CWSJsonManager::checkAndDispatchStat(Aria2StatCollector *sclt)
{
    QMap<int, QVariant> stats, stats2; // QVariant可能是整数，小数，或者字符串
    // qLogx()<<"";
    // emit this->taskStatChanged(sclt->tid, sclt->totalLength, sclt->completedLength,
    //                            sclt->totalLength == 0 ? 0: (sclt->completedLength*100/ sclt->totalLength),
    //                            sclt->downloadSpeed, sclt->uploadSpeed);

    stats[ng::stat2::task_id] = sclt->tid;
    stats[ng::stat2::belongs_to] = sclt->strBelongsTo;
    stats[ng::stat2::total_length] = (qulonglong)sclt->totalLength;
    stats[ng::stat2::completed_length] = (qulonglong)sclt->completedLength;
    stats[ng::stat2::completed_percent] = (int)(sclt->totalLength == 0 ? 0: (sclt->completedLength*100/ sclt->totalLength));
    stats[ng::stat2::download_speed] = sclt->downloadSpeed;
    stats[ng::stat2::upload_speed] = sclt->uploadSpeed;
    stats[ng::stat2::gid] = (qulonglong)sclt->gid;
    stats[ng::stat2::num_connections] = sclt->connections;
    stats[ng::stat2::hex_bitfield] = QString(sclt->bitfield.c_str());
    stats[ng::stat2::num_pieces] = sclt->numPieces;
    stats[ng::stat2::piece_length] = sclt->pieceLength;
    stats[ng::stat2::eta] = sclt->eta;
    stats[ng::stat2::str_eta] = ""; //QString::fromStdString(aria2::util::secfmt(sclt->eta));
    stats[ng::stat2::error_code] = sclt->errorCode;
    stats[ng::stat2::status] = sclt->state == aria2::DOWNLOAD_ACTIVE ? "active" : "waiting"; 
    // ready, active, waiting, complete, removed, error, pause
    
    emit this->taskStatChanged(sclt->tid, stats);

    stats2[ng::stat2::download_speed] = sclt->globalStat2.downloadSpeed;
    stats2[ng::stat2::upload_speed] = sclt->globalStat2.uploadSpeed;
    emit this->globalStatChanged(stats2);

    return true;
}

bool Aria2CWSJsonManager::checkAndDispatchServerStat(Aria2StatCollector *sclt)
{
    QList<QMap<QString, QString> > servers;
    QMap<QString, QString> server;

    for (int i = 0; i < sclt->server_stats.servers.size(); i++) {
        server["index"] = QString("%1,%2").arg(i).arg(sclt->server_stats.servers.at(i).state);
        server["currentUri"] = sclt->server_stats.servers.at(i).uri.c_str();
        server["downloadSpeed"] = QString("%1").arg(sclt->server_stats.servers.at(i).downloadSpeed);

        servers.append(server);
        server.clear();
    }

    if (servers.size() > 0) {
        emit this->taskServerStatChanged(sclt->tid, servers);
    }

    return true;
}

bool Aria2CWSJsonManager::confirmBackendFinished(int tid, void *eaw)
{
    QMap<int, QVariant> stats; // QVariant可能是整数，小数，或者字符串

    // aria2::GroupId::clear();

    /*
    switch(eaw->exit_status) {
    case aria2::error_code::FINISHED:
        emit this->taskFinished(eaw->m_tid, eaw->exit_status);

        this->m_tasks.remove(eaw->m_tid);
        this->m_rtasks.remove(eaw);
        eaw->deleteLater();    
        break;
    case aria2::error_code::IN_PROGRESS:
        stats[ng::stat2::error_code] = eaw->exit_status;
        stats[ng::stat2::error_string] = "";// QString::fromStdString(aria2::fmt(MSG_DOWNLOAD_NOT_COMPLETE, tid, ""));
        stats[ng::stat2::status] = "pause";
        emit this->taskStatChanged(eaw->m_tid, stats);
        this->m_tasks.remove(eaw->m_tid);
        this->m_rtasks.remove(eaw);
        eaw->deleteLater();
        break;
    case aria2::error_code::REMOVED:
        break;
    case aria2::error_code::RESOURCE_NOT_FOUND:
        stats[ng::stat2::error_code] = eaw->exit_status;
        stats[ng::stat2::error_string] = "";// QString(MSG_RESOURCE_NOT_FOUND);
        stats[ng::stat2::status] = "error";
        emit this->taskStatChanged(eaw->m_tid, stats);
        this->m_tasks.remove(eaw->m_tid);
        this->m_rtasks.remove(eaw);
        eaw->deleteLater();
        break;
    default:
        break;
    }
    */
    return true;
}

/////
bool Aria2CWSJsonManager::onAllStatArrived(int stkey)
{
    Aria2StatCollector *sclt = static_cast<Karia2StatCalc*>(sender())->getNextStat(stkey);
    QPair<int, Aria2StatCollector*> stval(stkey, sclt);

    this->stkLocker.lock();
    this->stkeys.enqueue(stval);
    this->stkLocker.unlock();

    if (!this->isRunning()) {
        this->start();
    }
    
    return true;
}

////////
void Aria2CWSJsonManager::onAriaAddUriResponse(QJsonObject &response, QNetworkReply *reply, QVariant &payload)
{
    // qLogx()<<response<<reply<<payload;
}

void Aria2CWSJsonManager::onAriaAddUriFault(int errorCode, QString errorString, QNetworkReply *reply, QVariant &payload)
{
    qLogx()<<errorCode<<reply<<payload;
}

void Aria2CWSJsonManager::onAriaUpdaterTimeout()
{
    // qLogx()<<"timer out update";
    QVariantList args;
    int taskId;
    QString ariaGid;
    Karia2StatCalc *sc = NULL;
    Aria2WSJsonRpcClient *jrpc = NULL;

    qLogx()<<m_tasks.size()<<mWSJsonRpcs.size()<<QDateTime::currentDateTime();
    QHashIterator<int, void*> hit(this->m_tasks);
    
    while (hit.hasNext()) {
        hit.next();
        taskId = hit.key();
        ariaGid = this->tid2hex(taskId);
        sc = statCalcs_[taskId];
        jrpc = mWSJsonRpcs[taskId];

        QVariantMap  tellStatusMethod;
        QVariantMap getServersMethod;
        QVariantMap getGlobalStatMethod;
        QVariantList gargs;
        QVariantList args;
        QVariantMap options;
        QVariantList loptions;

        options[0] = ariaGid;
        loptions << ariaGid;

        tellStatusMethod["methodName"] = QString("aria2.tellStatus");
        tellStatusMethod["params"] = loptions;
        getServersMethod["methodName"] = QString("aria2.getServers");
        getServersMethod["params"] = loptions;
        getGlobalStatMethod["methodName"] = QString("aria2.getGlobalStat");
        getGlobalStatMethod["params"] = QVariant(""); // Failed to marshal unknown variant type:  QVariant::Invalid


        args << tellStatusMethod;
        args << getServersMethod;
        args << getGlobalStatMethod;

        gargs.insert(0, args);

        this->sendMessage(mws, QString("system.multicall"), gargs, QVariant(taskId));
        /*
        jrpc->call(QString("system.multicall"), gargs, QVariant(taskId),
                   sc, SLOT(calculateStat(QJsonObject&, QNetworkReply*, QVariant&)),
                   this, SLOT(onAriaGetStatusFault(int, QString, QNetworkReply*, QVariant &)));
        */
    }

}
void Aria2CWSJsonManager::onAriaGetStatusFault(int code, QString reason, QNetworkReply *reply, QVariant &payload)
{
    qLogx()<<code<<reason<<payload;
}

void Aria2CWSJsonManager::getAria2ChildStatus(QStringList childs, int tid)
{
    QVariantList args;
    int taskId;
    QString ariaGid;
    Karia2StatCalc *sc = NULL;
    // Aria2WSJsonRpcClient *jrpc = NULL;

    taskId = tid;
    sc = statCalcs_[tid];
    // jrpc = mWSJsonRpcs[tid];
    // qLogx()<<jrpc<<tid<<mWSJsonRpcs.size()<<childs<<m_tasks.size()<<sender()<<QDateTime::currentDateTime();

    if (true) {
        QVariantMap  tellStatusMethod;
        QVariantMap getServersMethod;
        QVariantMap getGlobalStatMethod;
        QVariantList gargs;
        QVariantList args;
        QVariantMap options;
        QVariantList loptions;

        for (int i = 0; i < childs.count() ; i ++) {
            ariaGid = childs.at(i);
            loptions << ariaGid;
        }

        tellStatusMethod["methodName"] = QString("aria2.tellStatus");
        tellStatusMethod["params"] = loptions;
        getServersMethod["methodName"] = QString("aria2.getServers");
        getServersMethod["params"] = loptions;
        getGlobalStatMethod["methodName"] = QString("aria2.getGlobalStat");
        getGlobalStatMethod["params"] = QVariant(""); // Failed to marshal unknown variant type:  QVariant::Invalid


        args << tellStatusMethod;
        args << getServersMethod;
        args << getGlobalStatMethod;

        gargs.insert(0, args);

        this->sendMessage(mws, QString("system.multicall"), gargs, QVariant(taskId));
        /*
        jrpc->call(QString("system.multicall"), gargs, QVariant(taskId),
                   sc, SLOT(calculateStat(QJsonObject&, QNetworkReply*, QVariant&)),
                   this, SLOT(onGetAria2ChildStatusFault(int, QString, QNetworkReply*, QVariant &)));
        */
    }
}

void Aria2CWSJsonManager::onGetAria2ChildStatusFault(int code, QString reason, QNetworkReply *reply, QVariant &payload)
{
    qLogx()<<code<<reason<<payload;
}

void Aria2CWSJsonManager::onWSClientError(QAbstractSocket::SocketError error)
{
    qLogx()<<error;
}

void Aria2CWSJsonManager::onWSClientConnected()
{
    qLogx()<<"";
    QString call_msg;
    qint64 slen;

    while (!this->mWaitConnectQueue.empty()) {
        call_msg = this->mWaitConnectQueue.dequeue();
        slen = this->mws->sendTextMessage(call_msg);
    }
}

void Aria2CWSJsonManager::onWSClientDisconnected()
{
    qLogx()<<"";
}

void Aria2CWSJsonManager::onWSMessageReceived(QString jsmessage)
{
    qLogx()<<""<<jsmessage.length()<<jsmessage;
    QJsonDocument jdoc = QJsonDocument::fromJson(jsmessage.toLatin1());
    QJsonObject jmessage = jdoc.object();
    int taskId;
    QVariant payload;

    if (jmessage.contains("id") &&jmessage.contains("result")
        && jmessage.value("result").isArray()) {

        taskId = mCbMeta.value(jmessage.value("id").toInt()).toInt();
        payload = QVariant(taskId);
        emit this->jaresponse(jmessage, 0, payload);
        mCbMeta.remove(jmessage.value("id").toInt());
    } else {
        // notify event
    }

    /*
    QJsonRpcMessage message(jmessage);
    QVariant result = message.result();
    int errorCode = message.errorCode();
    QString errorMessage = message.errorMessage();

    if (errorCode == 0) {
        // emit this->jaresponse(jmessage, 0, meta->payload);
    } else {
        // emit this->fault(errorCode, errorMessage, 0, meta->payload);
    } 
    */  
}

bool Aria2CWSJsonManager::sendMessage(QWebSocket *ws, QString method, QVariantList arguments, QVariant payload)
{
    QJsonObject *jobj = new QJsonObject;
    jobj->insert("jsonrpc", QLatin1String("2.0"));
    jobj->insert("method", method);
    if (!arguments.isEmpty()) {
        jobj->insert("params", QJsonArray::fromVariantList(arguments));
    }

    static int json_id = 0;
    jobj->insert("id", ++ json_id);
    mCbMeta[json_id] = payload;

    QJsonDocument jdoc = QJsonDocument(*jobj);
    jdoc.toJson(QJsonDocument::Compact);
    QString call_msg = jdoc.toJson(QJsonDocument::Compact);

    if (ws->isValid()) {
        qint64 ret = ws->sendTextMessage(call_msg);
    } else {
        this->mWaitConnectQueue.enqueue(call_msg);
    }

    return true;
}

void Aria2CWSJsonManager::onStatArrived(QJsonObject &response, QNetworkReply *reply, QVariant &payload)
{
    int taskId;
    Karia2StatCalc *sch = NULL;

    taskId = payload.toInt();
    if (!statCalcs_.contains(taskId)) {
        Q_ASSERT(1 == 2);
        return;
    }

    sch = statCalcs_.value(taskId);
    sch->calculateStat(response, reply, payload);
}


//////////////////////////
// websocket协议是一种长连接协议，这个客户端封装应该能够支持长连接
//////////////////////////
Aria2WSJsonRpcClient::Aria2WSJsonRpcClient(QString url, QObject *parent)
    : QObject(parent)
{
    this->mUrl = url;

    // QObject::connect(this, &Aria2WSJsonRpcClient::disconnectConnection, this, &Aria2WSJsonRpcClient::onDisconnectConnection);
}

Aria2WSJsonRpcClient::~Aria2WSJsonRpcClient()
{

}

bool Aria2WSJsonRpcClient::call(QString method, QVariantList arguments, QVariant payload, 
                                QObject* responseObject, const char* responseSlot,
                                QObject* faultObject, const char* faultSlot)
{
    // qLogx()<<this<<this->mUrl;
    QUrl uo(this->mUrl);
    // mLws->connectToHost(uo.host(), uo.port());

    QLibwebsockets *mLws = new QLibwebsockets(uo.host(), uo.port());
    QWebSocket *ws = new QWebSocket();
    CallbackMeta *meta = new CallbackMeta();
    meta->mLws = mLws;
    meta->mws = ws;
    meta->method = method;
    meta->arguments = arguments;
    meta->payload = payload;
    meta->responseObject = responseObject;
    meta->responseSlot = responseSlot;
    meta->faultObject = faultObject;
    meta->faultSlot = faultSlot;

    QObject::connect(this, SIGNAL(jaresponse(QJsonObject &, QNetworkReply *, QVariant &)), 
                     meta->responseObject, meta->responseSlot);
    QObject::connect(this, SIGNAL(fault(int, const QString &, QNetworkReply *, QVariant &)), 
                     meta->faultObject, meta->faultSlot);

    qLogx()<<""<<mCbMeta.size()<<mCbAddCounter.size()<<mCbDelCounter.size()<<ws<<payload<<QDateTime::currentDateTime();
    this->mCbMeta[ws] = meta;
    this->mCbAddCounter[ws] = 1;

    // QObject::connect(mLws, &QLibwebsockets::connected, this, &Aria2WSJsonRpcClient::onRawSocketConnected);
    // QObject::connect(mLws, &QLibwebsockets::messageReceived, this, &Aria2WSJsonRpcClient::onMessageReceived);
    // QObject::connect(mLws, &QLibwebsockets::websocketError, this, &Aria2WSJsonRpcClient::onWebsocketError);

    // mLws->start();

    QObject::connect(ws, &QWebSocket::connected, this, &Aria2WSJsonRpcClient::onRawSocketConnected);
    QObject::connect(ws, &QWebSocket::textMessageReceived, this, &Aria2WSJsonRpcClient::onMessageReceived);
    QObject::connect(ws, SIGNAL(error(QAbstractSocket::SocketError)),
                     this, SLOT(onRawSocketConnectError(QAbstractSocket::SocketError)));

    ws->open(uo);

    // qLogx()<<"connecting...";
    return true;
}

bool Aria2WSJsonRpcClient::onRawSocketConnectError(QAbstractSocket::SocketError socketError)
{
    qLogx()<<socketError;

    QWebSocket *ws = (QWebSocket*)(sender());
    CallbackMeta *meta = mCbMeta.contains(ws) ? this->mCbMeta.value(ws) : NULL;

    if (meta == NULL) {
        Q_ASSERT(meta != NULL);
    } else {
        emit this->disconnectConnection(this->mCbMeta.value(ws));
    }

    return true;

    ///// 
    /*
    QLibwebsockets *mLws = (QLibwebsockets*)(sender());
    CallbackMeta *meta = mCbMeta.contains(mLws) ? this->mCbMeta.value(mLws) : NULL;

    if (meta == NULL) {
        Q_ASSERT(meta != NULL);
    } else {
        emit this->disconnectConnection(this->mCbMeta.value(mLws));
    }
    */
    return true;
}

bool Aria2WSJsonRpcClient::onRawSocketConnected()
{
    // qLogx()<<"sending..."<<(sender());
    QWebSocket *ws = (QWebSocket*)(sender());
    CallbackMeta *meta2 = mCbMeta.contains(ws) ? this->mCbMeta.value(ws) : NULL;

    if (meta2 == NULL) {
        Q_ASSERT(meta2 != NULL);
    } else {
        this->sendMessage(ws, meta2->method, meta2->arguments);
    }

    return true;

    /////
    /*
    QLibwebsockets *mLws = (QLibwebsockets*)(sender());
    CallbackMeta *meta = mCbMeta.contains(mLws) ? this->mCbMeta.value(mLws) : NULL;

    // QJsonRpcMessage request = QJsonRpcMessage::createRequest(meta->method, meta->arguments);
    // mLws->sendMessage(request);
    if (meta == NULL) {
        Q_ASSERT(meta != NULL);
    } else {
        mLws->sendMessage(meta->method, meta->arguments);
    }

    */

    /*
    QTcpSocket *mRawSock = (QTcpSocket*)(sender());
    CallbackMeta *meta = this->mCbMeta[mRawSock];

    QJsonRpcSocket *mJsonRpcSock = new QJsonRpcSocket(mRawSock);
    QObject::connect(mJsonRpcSock, &QJsonRpcSocket::messageReceived, this, &Aria2WSJsonRpcClient::onMessageReceived);
    QObject::connect(this, SIGNAL(aresponse(QVariant &, QNetworkReply *, QVariant &)), 
                     meta->responseObject, meta->responseSlot);
    QObject::connect(this, SIGNAL(fault(int, const QString &, QNetworkReply *, QVariant &)), 
                     meta->faultObject, meta->faultSlot);

    this->mCbMeta2[mJsonRpcSock] = meta;
    meta->mJsonRpcSock = mJsonRpcSock;

    QJsonRpcMessage request = QJsonRpcMessage::createRequest(meta->method, meta->arguments);
    QJsonRpcServiceReply *reply = mJsonRpcSock->sendMessage(request);

    // QJsonRpcMessage response = this->mJsonRpcSock->sendMessageBlocking(request, 5000);
    // qLogx()<<response.result();
    */

    return true;
}

void Aria2WSJsonRpcClient::onWebsocketError()
{
    qLogx()<<"errrrrrrrrr";
    /*
    QLibwebsockets *mLws = (QLibwebsockets*)(sender());
    CallbackMeta *meta = mCbMeta.contains(mLws) ? this->mCbMeta.value(mLws) : NULL;

    if (meta == NULL) {
        Q_ASSERT(meta != NULL);
    } else {
        this->onDisconnectConnection(meta);
    }
    */
}

void Aria2WSJsonRpcClient::onMessageReceived(QString rjmessage)
{
    QWebSocket *ws = (QWebSocket*)(sender());
    CallbackMeta *meta = mCbMeta.contains(ws) ? this->mCbMeta.value(ws) : NULL;
    qLogx()<<ws<<meta<<rjmessage.length()<<rjmessage;

    if (meta == NULL) {
        qLogx()<<"meta object not exists"<<ws<<mCbMeta.contains(ws)
               <<mCbAddCounter.contains(ws) <<mCbDelCounter.contains(ws)
               <<rjmessage.length();
        return;
    }

    this->mCbDelCounter[ws] = 1;

    QJsonDocument jdoc = QJsonDocument::fromJson(rjmessage.toLatin1());
    QJsonObject jmessage = jdoc.object();

    QJsonRpcMessage message(jmessage);
    QVariant result = message.result();
    int errorCode = message.errorCode();
    QString errorMessage = message.errorMessage();

    if (errorCode == 0) {
        // emit this->aresponse(result, 0, meta->payload);
        emit this->jaresponse(jmessage, 0, meta->payload);
    } else {
        emit this->fault(errorCode, errorMessage, 0, meta->payload);
    }   

    this->mCbMeta.remove(ws);
    emit this->disconnectConnection(meta);
    
}

void Aria2WSJsonRpcClient::onMessageReceived2(QJsonObject jmessage)
{
    // qLogx()<<jmessage<<sender();

    QWebSocket *ws = (QWebSocket*)(sender());
    CallbackMeta *meta = mCbMeta.contains(ws) ? this->mCbMeta.value(ws) : NULL;

    if (meta == NULL) {
        qLogx()<<"meta object not exists"<<ws<<mCbMeta.contains(ws)
               <<mCbAddCounter.contains(ws) <<mCbDelCounter.contains(ws);
        return;
    }

    this->mCbDelCounter[ws] = 1;

    QJsonRpcMessage message(jmessage);
    QVariant result = message.result();
    int errorCode = message.errorCode();
    QString errorMessage = message.errorMessage();

    if (errorCode == 0) {
        // emit this->aresponse(result, 0, meta->payload);
        emit this->jaresponse(jmessage, 0, meta->payload);
    } else {
        emit this->fault(errorCode, errorMessage, 0, meta->payload);
    }   

    this->mCbMeta.remove(ws);
    emit this->disconnectConnection(meta);


    /*
    QLibwebsockets *mLws = (QLibwebsockets*)(sender());
    CallbackMeta *meta = mCbMeta.contains(mLws) ? this->mCbMeta.value(mLws) : NULL;

    if (meta == NULL) {
        qLogx()<<"meta object not exists"<<mLws<<mCbMeta.contains(mLws)
               <<mCbAddCounter.contains(mLws) <<mCbDelCounter.contains(mLws);
        return;
    }

    this->mCbDelCounter[mLws] = 1;

    QJsonRpcMessage message(jmessage);
    QVariant result = message.result();
    int errorCode = message.errorCode();
    QString errorMessage = message.errorMessage();

    if (errorCode == 0) {
        // emit this->aresponse(result, 0, meta->payload);
        emit this->jaresponse(jmessage, 0, meta->payload);
    } else {
        emit this->fault(errorCode, errorMessage, 0, meta->payload);
    }   

    this->mCbMeta.remove(mLws);
    emit this->disconnectConnection(meta);
    */
}

void Aria2WSJsonRpcClient::onDisconnectConnection(void *cbmeta)
{
    // qLogx()<<cbmeta<<sender();

    CallbackMeta *meta = (CallbackMeta*)cbmeta;

    QObject::disconnect(this, SIGNAL(jaresponse(QJsonObject &, QNetworkReply *, QVariant &)), 
                        meta->responseObject, meta->responseSlot);
    QObject::disconnect(this, SIGNAL(fault(int, const QString &, QNetworkReply *, QVariant &)), 
                        meta->faultObject, meta->faultSlot);
    
    QLibwebsockets *mLws = meta->mLws;
    mLws->close();
    // mLws->deleteLater();

    /*
    CallbackMeta *meta = (CallbackMeta*)cbmeta;

    QObject::disconnect(this, SIGNAL(aresponse(QVariant &, QNetworkReply *, QVariant &)), 
                        meta->responseObject, meta->responseSlot);
    QObject::disconnect(this, SIGNAL(fault(int, const QString &, QNetworkReply *, QVariant &)), 
                     meta->faultObject, meta->faultSlot);

    QTcpSocket *rsock = meta->mRawSock;
    QJsonRpcSocket *jsock = meta->mJsonRpcSock;
    this->mCbMeta.remove(rsock);
    if (jsock) this->mCbMeta2.remove(jsock);
    delete meta;
    rsock->deleteLater();
    if (jsock) jsock->deleteLater();
    */
}

bool Aria2WSJsonRpcClient::sendMessage(QWebSocket *ws, QString method, QVariantList arguments)
{
    QJsonObject *jobj = new QJsonObject;
    jobj->insert("jsonrpc", QLatin1String("2.0"));
    jobj->insert("method", method);
    if (!arguments.isEmpty()) {
        jobj->insert("params", QJsonArray::fromVariantList(arguments));
    }

    static int json_id = 0;
    jobj->insert("id", (++json_id));

    QJsonDocument jdoc = QJsonDocument(*jobj);
    jdoc.toJson(QJsonDocument::Compact);
    QString call_msg = jdoc.toJson(QJsonDocument::Compact);

    qint64 ret = ws->sendTextMessage(call_msg);

    return true;
}



///////////////////////
//////////////////////

QLibwebsockets::QLibwebsockets(QString host, int port, QObject *parent)
    : QThread(0)
{
    // test code
    if (0) {
        lws_get_library_version();
        libwebsocket_create_context(0);
        libwebsocket_client_connect(lws_ctx, 0, 0, 0, 0, 0, 0, 0, 0);
    }
    
    this->m_wshost = host;
    this->m_wsport = port;

    QObject::connect(this, &QLibwebsockets::finished, this, &QLibwebsockets::onSelfFinished);
    // QTimer::singleShot(100, this, SLOT(start()));
    // this->start();
}


QLibwebsockets::~QLibwebsockets()
{
    // qLogx()<<"here"<<this->del_ctx;
    // libwebsocket_context_destroy(this->del_ctx);
    // destory 本身没有问题，出在SSL_free上？为什么呢？
}

void QLibwebsockets::run()
{
    // go on
    this->m_loop_timer = new QTimer();
    // QObject::connect(this->m_loop_timer, &QTimer::timeout, this, &QLibwebsockets::onLoopCycle);
    // QObject::connect(this, &QLibwebsockets::destroyContext, this, &QLibwebsockets::onDestroyContext);

    // qLogx()<<"connect lws ...";
    this->connectToHost(m_wshost, m_wsport);

    // qLogx()<<"entry lws loop ...";
    while (true && !this->m_closed) {
        QDateTime btime = QDateTime::currentDateTime();

        int rv = libwebsocket_service(lws_ctx, 50); // 立即返回

        QDateTime etime = QDateTime::currentDateTime();
        int dtime = btime.msecsTo(etime);
        qLogx()<<dtime<<rv;

        if (rv != 0) {
            qLogx()<<"Any error for lws loop cycle: "<<rv; 
            break;
        }
    }

    if (!this->m_closed) {
        emit websocketError();
        while (true && !this->m_closed) {
            usleep(1000 * 10);
        }
    }

    // qLogx()<<"close lws...";
    this->onDestroyContext(lws_ctx);

    // this->onSelfFinished();
    return;

    // qLogx()<<"entry loop...";
    this->exec();
}


int QLibwebsockets::wsLoopCallback(struct libwebsocket_context *ctx,
                                   struct libwebsocket *wsi,
                                   enum libwebsocket_callback_reasons reason,
                                   void *user, void *in, size_t len)
{
    // qLogx()<<this<<ctx<<wsi<<reason<<user<<in<<len;

    char *buff;
    char *rdata = "{\"id\": 1,\"jsonrpc\": \"2.0\",\"method\": \"aria2.getVersion\",\"params\": [\"a\"]}";
    char *pname = "default";
    int rv;
    // QJsonRpcMessage request;
    QJsonDocument jdoc;
    QByteArray tmpba;

    char m_wsin[8192];

	switch (reason) {
	case LWS_CALLBACK_CLIENT_ESTABLISHED:
		// qLogx()<<"callback_dumb_increment: LWS_CALLBACK_CLIENT_ESTABLISHED";
        emit this->connected();

        /*
        buff = (char*)calloc(1, 1024);
        memset(buff, 0, 1024);
        strcpy(buff, rdata);
        
        request = QJsonRpcMessage::createRequest(QString("aria2.getVersion"), QVariantList());
        jdoc = QJsonDocument(request.toObject());
        jdoc.toJson(QJsonDocument::Compact);
        
        strcpy(buff, jdoc.toJson(QJsonDocument::Compact).data());
        qLogx()<<"sending "<<buff<<strlen(buff);
        rv = libwebsocket_write(wsi, (unsigned char*)buff, strlen(buff), LWS_WRITE_TEXT);
        qLogx()<<"sending "<<rv<<buff;
        */

		break;

	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
		qLogx()<<"LWS_CALLBACK_CLIENT_CONNECTION_ERROR";
		// was_closed = 1;
		break;

	case LWS_CALLBACK_CLOSED:
		// qLogx()<<"LWS_CALLBACK_CLOSED";
		// was_closed = 1;
		break;

	case LWS_CALLBACK_CLIENT_RECEIVE:
        memset(m_wsin, 0, sizeof(m_wsin));
        if (in) {
            memcpy(m_wsin, in, len);
        }
        // qLogx()<<"rx %d '%s'"<<m_wsin;
        jdoc = QJsonDocument::fromJson(QByteArray(m_wsin, len));
        // request = QJsonRpcMessage(jdoc.object());
        emit messageReceived(jdoc.object());
        // this->m_rdq.enqueue(QByteArray((char*)in, len));

		break;

    case LWS_CALLBACK_CLIENT_WRITEABLE:
        // qLogx()<<"client_writable callback.";
        if (this->m_wrq.isEmpty()) break;
        tmpba = this->m_wrq.dequeue();
        buff = (char *)calloc(1, tmpba.length() + 200 + LWS_SEND_BUFFER_PRE_PADDING + LWS_SEND_BUFFER_POST_PADDING);
        memset(buff, 0, tmpba.length() + 200 + LWS_SEND_BUFFER_PRE_PADDING + LWS_SEND_BUFFER_POST_PADDING);
        strncpy(buff + LWS_SEND_BUFFER_PRE_PADDING, tmpba.data(), tmpba.length());
        rv = libwebsocket_write(wsi, (unsigned char*)buff + LWS_SEND_BUFFER_PRE_PADDING, tmpba.length(), LWS_WRITE_TEXT);
        // qLogx()<<"lws write rv:"<<rv;
        free(buff); buff = NULL;
        break;

    case LWS_CALLBACK_PROTOCOL_DESTROY:
        qLogx()<<"LWS_CALLBACK_PROTOCOL_DESTROY ed";
        break;

	/* because we are protocols[0] ... */
	case LWS_CALLBACK_CLIENT_CONFIRM_EXTENSION_SUPPORTED:
        qLogx()<<"not support.";
        return 1;
        /*
		if ((strcmp(in, "deflate-stream") == 0)) {
			qLogx()<<"denied deflate-stream extension";
			return 1;
		}
		if ((strcmp(in, "deflate-frame") == 0)) {
			qLogx()<<"denied deflate-frame extension";
			return 1;
		}
		if ((strcmp(in, "x-google-mux") == 0)) {
			qLogx()<<"denied x-google-mux extension";
			return 1;
		}
        */
		break;
	default:
        // qLogx()<<"what reason:"<<reason;
		break;
    }

    return 0;
}

static int QLibwebsockets_loop_callback(struct libwebsocket_context *cthis,
                                        struct libwebsocket *wsi,
                                        enum libwebsocket_callback_reasons reason,
                                        void *user, void *in, size_t len)
{
    QLibwebsockets *qws = (QLibwebsockets*)(user);
    // qLogx()<<"user void"<<user<<qws;
    if (user == NULL) {
        return 0;
    }

    return qws->wsLoopCallback(cthis, wsi, reason, user, in, len);
}

static struct libwebsocket_protocols lws_protocols[] = {
	{
		"default",
		QLibwebsockets_loop_callback, // callback_dumb_increment,
		0,
		2*1024*1024,  // 2M
	},
	{ NULL, NULL, 0, 0 } /* end */
};

void QLibwebsockets::onLoopCycle()
{
    /*
    if (this->m_rdq.size() > 0) {
        QByteArray ba;
        QJsonDocument jdoc;
        while (this->m_rdq.size() > 0) {
            ba = this->m_rdq.dequeue();
            jdoc = QJsonDocument::fromJson(ba);
            emit this->messageReceived(jdoc.object());
        }
    }
    */

    if (this->lws_ctx != NULL && this->m_loop_timer->isActive()) {
        qLogx()<<this->lws_ctx << this->m_loop_timer->isActive()<<this->m_closed;
        if (this->m_closed) {
            this->m_loop_timer->stop();
            this->del_ctx = this->lws_ctx;
            libwebsocket_context_destroy(this->lws_ctx);
            this->lws_ctx = NULL;
            this->h_lws = NULL;
            qLogx()<<"destroy done.";
            QObject::connect(this, SIGNAL(finished()), this, SLOT(onSelfFinished()));
            this->quit();
            // this->deleteLater();
            return;
        }
        int rv = libwebsocket_service(lws_ctx, 0); // 立即返回
        if (rv != 0) {
            qLogx()<<"Any error for lws loop cycle: "<<rv; 
        }
    }

    /*
    if (this->m_rdq.size() > 0) {
        QByteArray ba;
        QJsonDocument jdoc;
        while (this->m_rdq.size() > 0) {
            ba = this->m_rdq.dequeue();
            jdoc = QJsonDocument::fromJson(ba);
            emit this->messageReceived(jdoc.object());
        }
    }
    */
}

bool QLibwebsockets::connectToHost(QString host, unsigned short port)
{
    // lws_set_log_level(LLL_DEBUG | LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_CLIENT, NULL);
    lws_set_log_level(LLL_ERR, NULL);

    struct lws_context_creation_info *lws_ctx_ci = (struct lws_context_creation_info*)calloc(1, sizeof(struct lws_context_creation_info));
    memset(lws_ctx_ci, 0, sizeof(struct lws_context_creation_info));
    lws_ctx_ci->protocols = lws_protocols;
    lws_ctx_ci->ssl_cert_filepath = "rpckey/rpc.pub";
    lws_ctx_ci->ssl_private_key_filepath = "rpckey/rpc.pri";
    lws_ctx_ci->ssl_ca_filepath = "rpckey/rpc.pub";
    lws_ctx_ci->gid = lws_ctx_ci->uid = -1;


    // QUrl uo(this->mUrl);
    int use_ssl = 0; // 2 or 1 or 0 are all ok
    char h[32] = {0};
    strncpy(h, host.toLatin1().data(), host.length());
    unsigned short p = port;
    // char *path = uo.path();
    this->lws_ctx = libwebsocket_create_context(lws_ctx_ci);

    // qLogx()<<h<<p<<host<<port;
    this->h_lws = libwebsocket_client_connect_extended(lws_ctx, h, p, use_ssl,
                                              "/jsonrpc", h, h,
                                              lws_protocols[0].name, -1,
                                              this);
    /*
    this->h_lws = libwebsocket_client_connect_extended(lws_ctx, "127.0.0.1", 6800, use_ssl,
                                              "/jsonrpc", "127.0.0.1", "127.0.0.1",
                                              lws_protocols[0].name, -1,
                                              this);
    */

    if (!this->m_loop_timer->isActive()) {
        this->m_loop_timer->setInterval(300);
        this->m_loop_timer->start();
    }

    while (false) {
        libwebsocket_service(lws_ctx, 30000);
    }

    // qLogx()<<h_lws;

    return true;
}

bool QLibwebsockets::close()
{
    struct libwebsocket_context *ctx = this->lws_ctx;
    // this->lws_ctx = NULL;
    // this->h_lws = NULL;

    /*
    if (this->loop_timer.isActive()) {
        this->loop_timer.stop();
    }
    QObject::disconnect(&this->loop_timer, &QTimer::timeout, this, &QLibwebsockets::onLoopCycle);
    qLogx()<<this->loop_timer.isActive();
    int rv = libwebsocket_service(ctx, 0);
    */
    this->m_closed = true;

    // emit destroyContext(ctx);

    return true;
}

void QLibwebsockets::onDestroyContext(void *ctx)
{
    // qLogx()<<"destroy context ..."<<ctx;

    struct libwebsocket_context *actx = (struct libwebsocket_context *)ctx;
    libwebsocket_context_destroy(actx);    
}

void QLibwebsockets::onSelfFinished()
{
    qLogx()<<"";
    this->deleteLater();
}

bool QLibwebsockets::sendMessage(QJsonRpcMessage message)
{
    // qLogx()<<message;

    QJsonDocument jdoc = QJsonDocument(message.toObject());
    jdoc.toJson(QJsonDocument::Compact);
    this->m_wrq.enqueue(jdoc.toJson(QJsonDocument::Compact));

    libwebsocket *wsi = this->h_lws;
    int rv = libwebsocket_callback_on_writable(this->lws_ctx, wsi);
    // qLogx()<<"vvv="<<rv;

    return true;
}
bool QLibwebsockets::sendMessage(QString method, QVariantList arguments)
{
    /*
    QJsonRpcMessage request;
    request.d->object = new QJsonObject;
    request.d->object->insert("jsonrpc", QLatin1String("2.0"));
    request.d->object->insert("method", method);
    if (!params.isEmpty())
        request.d->object->insert("params", QJsonArray::fromVariantList(params));
    return request;

    QJsonRpcMessage request = QJsonRpcMessagePrivate::createBasicRequest(method, params);
    request.d->type = QJsonRpcMessage::Request;
    QJsonRpcMessagePrivate::uniqueRequestCounter++;
    request.d->object->insert("id", QJsonRpcMessagePrivate::uniqueRequestCounter);
    return request;
    */

    QJsonObject *jobj = new QJsonObject;
    jobj->insert("jsonrpc", QLatin1String("2.0"));
    jobj->insert("method", method);
    if (!arguments.isEmpty()) {
        jobj->insert("params", QJsonArray::fromVariantList(arguments));
    }

    static int json_id = 0;
    jobj->insert("id", (++json_id));

    QJsonDocument jdoc = QJsonDocument(*jobj);
    jdoc.toJson(QJsonDocument::Compact);
    this->m_wrq.enqueue(jdoc.toJson(QJsonDocument::Compact));
        
    libwebsocket *wsi = this->h_lws;
    // int rv = libwebsocket_write(wsi, (unsigned char*)buff, strlen(buff)+1, LWS_WRITE_TEXT);
    int rv = libwebsocket_callback_on_writable(lws_ctx, wsi);
    // qLogx()<<"vvv="<<rv;

    // QJsonRpcMessage request = QJsonRpcMessage::createRequest(method, arguments);
    // return this->sendMessage(request);
    return true;
}

// 像是qjsonrpc的实现有内存问题
// 还有libwebsocket库的线程安全问题。

/*
  libwebsockets的函数前缀，
  lws开始的是比较简单的函数
  libwebsockets开始的是server端函数
  libwebsocket是客户端函数
 */

