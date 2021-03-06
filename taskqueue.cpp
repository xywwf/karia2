﻿// taskqueue.cpp --- 
// 
// Author: liuguangzhao
// Copyright (C) 2007-2013 liuguangzhao@users.sf.net
// URL: 
// Created: 2010-04-03 22:14:39 +0800
// Version: $Id: taskqueue.cpp 200 2013-10-04 07:13:26Z drswinghead $
// 

#include <cassert>

#include "simplelog.h"
#include "taskinfodlg.h"	//for TaskOption class 

#include "taskqueue.h"
#include "sqlitetaskmodel.h"
#include "sqlitesegmentmodel.h"
#include "segmentlogmodel.h"
#include "taskballmapwidget.h"

#include "torrentpeermodel.h"
#include "taskservermodel.h"
#include "seedfilemodel.h"

#include "karia2statcalc.h"
#include "emaria2c.h"
#include "util.h"

Task::Task(int taskId, QString taskUrl)
{
    this->mTaskId = taskId; 
    this->mTaskUrl = taskUrl;
    this->btPeerModel = new TorrentPeerModel();
    this->btTrackerModel = new TorrentTrackerModel();
    this->serverModel = new TaskServerModel();
    this->seedFileModel = new SeedFileModel();
}
Task::~Task()
{
    delete this->btPeerModel; this->btPeerModel = 0;
    delete this->btTrackerModel; this->btTrackerModel = 0;
    delete this->serverModel; this->serverModel = 0;
    qLogx()<<__FUNCTION__<<"here taskId:"<<this->mTaskId;
}

/**
   用于管理一组任务，各种状态的任务都在这其中管理维护

   去掉这些静态函数，太多了。
*/

// static
TaskQueue *TaskQueue::mInstance = NULL;
// static 
TaskQueue *TaskQueue::instance(QObject *parent)
{
    TaskQueue *ins = TaskQueue::mInstance;
    if (ins == NULL) {
        ins = TaskQueue::mInstance = new TaskQueue();
    }
    return ins;
}

bool TaskQueue::removeInstance( int taskId )
{	
    if (this->mTasks.contains(taskId)) {
        this->mTasks.remove(taskId);
    } else {
        assert(1 == 2);
    }
	return false;
}

bool TaskQueue::containsInstance(int taskId) 
{
    return this->mTasks.contains(taskId);
}

TaskQueue::TaskQueue( QObject *parent )
    : QObject(parent)
{
	//assert( option != NULL );
    // this->btPeerModel = NULL;

	this->initDefaultMember();

	this->setObjectName("Class TaskQueue");
}

void TaskQueue::initDefaultMember()
{
	// this->mCreateTime = QDateTime::currentDateTime();
	// this->mStartTime = QDateTime::currentDateTime();
	// this->mEclapsedTime = 0.0 ;
	// this->mLastEclapsedTime = 0.0;	

	// this->mTotalLength = 0 ;
	// this->mGotLength  = 0 ;

	// this->canceled = false ;

}

TaskQueue::~TaskQueue() 
{

}

// void TaskQueue::setParameter( int pTaskId , QString pTaskUrl,long pTotalLength , long pGotLength , 
// 		int pTotalSegmentCount )
// {
// 	this->mTaskId = pTaskId ;
// 	this->mTaskUrl = pTaskUrl ;
// 	this->mTotalLength = pTotalLength ;
// 	this->mGotLength  = pGotLength ;
// 	this->mTotalSegmentCount = pTotalSegmentCount ;
// }

//取状态描述
QString TaskQueue::getStatusString(int status) 
{
	QString ss;	//enum { TS_READY , ............
	switch (status)
	{
	case TaskQueue::TS_READY:
		ss = "ready";
		break ;
	case TaskQueue::TS_WAITING:
		ss = "waiting";
		break;
	case TaskQueue::TS_RUNNING:
		ss = "running";
		break ;
	case TaskQueue::TS_ERROR:
		ss = "error";
		break;
	case TaskQueue::TS_COMPLETE:
		ss = "complete";
		break;
	case TaskQueue::TS_DELETED:
		ss = "deleted";
		break ;
	default: 
		ss = "unknown status";
		break ;
	}
	return ss ;
}

bool TaskQueue::addTaskModel(int taskId , TaskOption *option)
{
	assert(option != 0);
    Task *task = NULL;
    if (!this->mTasks.contains(taskId)) {
        task = new Task(taskId, option->mTaskUrl);
        this->mTasks[taskId] = task;
    } else {
        task = this->mTasks[taskId];
    }

    qLogx()<<__FUNCTION__<<option->mCatId;
	//将任务信息添加到 task list view 中
	QModelIndex index;
    QAbstractItemModel * mdl = SqliteTaskModel::instance(ng::cats::downloading, 0);
    QModelIndexList mil = mdl->match(mdl->index(0, ng::tasks::task_id), Qt::DisplayRole, 
                                     QString("%1").arg(taskId), 1, Qt::MatchExactly | Qt::MatchWrap);
    if (mil.count() == 0) {
        int modelRows = mdl->rowCount();
        modelRows = 0; // put on top
        mdl->insertRows(modelRows, 1);
        index = mdl->index(modelRows, ng::tasks::task_id);
        mdl->setData(index, taskId);
        index = mdl->index(modelRows, ng::tasks::task_status);
        mdl->setData(index, "ready");
        index = mdl->index(modelRows, ng::tasks::save_path);
        mdl->setData(index, option->mSavePath);
        index = mdl->index(modelRows, ng::tasks::file_name);
        mdl->setData(index, TaskQueue::getFileNameByUrl(option->mTaskUrl));
        index = mdl->index(modelRows, ng::tasks::block_activity);
        mdl->setData(index, QString("0/0") );
        index = mdl->index(modelRows, ng::tasks::active_block_count);
        mdl->setData(index, QString("0"));
        index = mdl->index(modelRows, ng::tasks::split_count);
        mdl->setData(index, QString("%1").arg(option->mSplitCount));
        index = mdl->index(modelRows, ng::tasks::real_url);
        mdl->setData(index, option->mTaskUrl);	
        index = mdl->index(modelRows, ng::tasks::org_url);
        mdl->setData(index, option->mTaskUrl);
        index = mdl->index(modelRows, ng::tasks::referer);
        mdl->setData(index, option->mReferer);
        index = mdl->index(modelRows, ng::tasks::user_cat_id );
        // mdl->setData(index ,ng::cats::downloading);
        mdl->setData(index, option->mCatId);
        index = mdl->index(modelRows, ng::tasks::sys_cat_id);
        mdl->setData(index, ng::cats::downloading);
        index = mdl->index(modelRows, ng::tasks::create_time );
        mdl->setData(index, QDateTime::currentDateTime().toString("hh:mm:ss yyyy-MM-dd"));
        index = mdl->index(modelRows, ng::tasks::file_length_abtained);
        mdl->setData(index, QString("false"));
        index = mdl->index(modelRows, ng::tasks::comment);
        mdl->setData(index, option->mCookies);
    } else {
        // model exists, resume task
    }
    // TaskQueue *tq = TaskQueue::instance(task_id);
    // tq->setUrl(option->mTaskUrl);


	return true;
}

//static 
QString TaskQueue::getFileNameByUrl(QString text)
{
	QUrl url(text);
	QFileInfo fi(url.path());
	QString fname = url.hasQuery() ?
        fi.fileName().left(fi.fileName().lastIndexOf('?')) 
        : fi.fileName();
	if (fname.isEmpty()) fname = "index.html";

    return fname;
}

// void TaskQueue::setUrl(QString url)
// {
//     this->mTaskUrl = url;
//     if (this->mTaskUrl.endsWith(".torrent")) {
//         if (this->btPeerModel == NULL) {
//             this->btPeerModel = new TorrentPeerModel();
//         }
//     }
// }
TorrentPeerModel *TaskQueue::torrentPeerModel(int taskId)
{
    TorrentPeerModel *pm = NULL;
    if (this->mTasks.contains(taskId)) {
        pm = this->mTasks[taskId]->btPeerModel;
    }
    return pm;
}

TorrentTrackerModel *TaskQueue::torrentTrackerModel(int taskId)
{
    TorrentTrackerModel *pm = NULL;
    if (this->mTasks.contains(taskId)) {
        pm = this->mTasks[taskId]->btTrackerModel;
    }
    return pm;
}

TaskServerModel *TaskQueue::taskServerModel(int taskId)
{
    TaskServerModel *sm = NULL;

    if (this->mTasks.contains(taskId)) {
        sm = this->mTasks[taskId]->serverModel;
    }
    return sm;
}

SeedFileModel *TaskQueue::taskSeedFileModel(int taskId)
{
    SeedFileModel *sm = NULL;

    if (this->mTasks.contains(taskId)) {
        sm = this->mTasks[taskId]->seedFileModel;
    }
    return sm;
}

bool TaskQueue::isTorrentTask(int taskId)
{
    // if (this->mTaskUrl.endsWith(".torrent")) {
    //     return true;
    // }
    if (this->mTasks.contains(taskId)) {
        if (this->mTasks[taskId]->mTaskUrl.endsWith(".torrent")) {
            return true;
        }
    }
    return false;
}

bool TaskQueue::isMagnetTask(int taskId) 
{
    // if (this->mTaskUrl.startsWith("magnet:?")) {
    //     return true;
    // }
    if (this->mTasks.contains(taskId)) {
        if (this->mTasks[taskId]->mTaskUrl.startsWith("magnet:?")) {
            return true;
        }
    }
    return false;
}

bool TaskQueue::isMetalinkTask(int taskId)
{
    // if (this->mTaskUrl.endsWith(".metalink")) {
    //     return true;
    // }
    if (this->mTasks.contains(taskId)) {
        if (this->mTasks[taskId]->mTaskUrl.endsWith(".metalink")) {
            return true;
        }
    }
    return false;
}

bool TaskQueue::setPeers(int taskId, QVariantList &peers)
{
    // bool rv = this->btPeerModel->setData(peers);
    // return rv;
    if (this->mTasks.contains(taskId)) {
        this->mTasks[taskId]->btPeerModel->setData(peers);
    }
    return true;
}

bool TaskQueue::setTrackers(int taskId, QVariantList &trackers)
{
    if (this->mTasks.contains(taskId)) {
        this->mTasks[taskId]->btTrackerModel->setData(trackers);
    }
    return true;
}

bool TaskQueue::setSeedFiles(int taskId, QVariantList &files)
{
    if (this->mTasks.contains(taskId)) {
        // this->mTasks[taskId]->seedFileModel->setData(files, true);
        this->mTasks[taskId]->seedFileModel->setData(files, false);
    }
    return true;
}

bool TaskQueue::updateSelectFile(int taskId, QString selected)
{
    if (this->mTasks.contains(taskId)) {
        this->mTasks[taskId]->seedFileModel->updateSelectFile(selected);
    }
    return true;
}

// void TaskQueue::onOneSegmentFinished(int taskId, int segId , int finishStatus ) 
// {
// 	qLogx() << __FUNCTION__<<taskId<< " "<< segId  ;

// 	long startOffset ;
// 	long totalLength ;
// 	long gotLength ;
// 	long totalEmitLength ;
// 	int  segCount ;
// 	long splitLength , splitOffset , splitLeftLength ;
// 	TaskQueue * tq = 0 ;
// 	// BaseRetriver * br = 0 , * brn = 0 , * nhs = 0 ;
// 	bool doneTask = false ;	//標識是否执行了完成任务的移动工作。
// 	SqliteSegmentModel * mdl = SqliteSegmentModel::instance(taskId,this);

// }
	
// void TaskQueue::onFirstSegmentReady(int pTaskId , long totalLength, bool supportBrokenRetrive)
// {
// 	qLogx() << __FUNCTION__ ;

// 	qLogx()<<  __FUNCTION__ <<" return for nothing done " ;
// 	return ;
// 	//
// 	TaskQueue * tq = 0 ;
// 	//tq =  this->findTaskById(pTaskId );
// 	tq = this ;
	
// 	//cacl per len and start offset 
// 	long perLen;// = totalLength/tq->mTotalSegmentCount ;
// 	long startOffset = perLen ;

// 	QModelIndex index ;
// 	int modelRows ;

// 	if (tq == 0) {
// 		qLogx()<< "find task faild : "<<pTaskId ;
// 		// tq->mTaskStatus = TaskQueue::TS_ERROR;
// 		this->onTaskListCellNeedChange( pTaskId , 1 , tq->getStatusString(TaskQueue::TS_ERROR));
// 		return;
// 	}

// 	QUrl url;// (tq->mTaskUrl);

// 	//update task list view model cell
// 	this->onTaskListCellNeedChange( pTaskId , ng::tasks::file_size , QString("%1").arg( totalLength ));
// 	// tq->mTotalLength = totalLength ;
// 	//通知画任务状态图。先去掉了，看什么时候能加进去。
// 	//TaskBallMapWidget::instance()->onRunTaskCompleteState( tq );

// 	//set the first segment totalLength element
// 	// br = tq->mSegmentThread[0];

// 	//br->mTotalLength = perLen ;
// 	QAbstractItemModel * mdl = 0 ;
// }

/**
 * 除了修改这个控制参数外还应该做点什么呢。
 * 这个可以由子线程发出多次，但如果第二次发应该是错误的，这并没有关系，我们只在这里判断就行了。
 */
// void TaskQueue::onAbtainedFileLength( int pTaskId , long totalLength , bool supportBrokenRetrive)
// {
// 	qLogx() << __FUNCTION__ << "taskId: "<< pTaskId << "totalLength :" << totalLength ;
// 	//if( this->mFileLengthAbtained == false )
// 	if( this->getFileAbtained(pTaskId,ng::cats::downloading) == false )
// 	{
// 		//this->mFileLengthAbtained = true ;	//这个参数非常重要
// 		this->mTotalLength = totalLength ;
// 		this->onTaskListCellNeedChange( pTaskId , (int)ng::tasks::file_length_abtained , QString("true") );
// 		this->onTaskListCellNeedChange( pTaskId , (int)ng::tasks::file_size , QString("%1").arg(totalLength) );
// 		//emit this->attemperTaskStatus(this ,(int) TaskQueue::TS_RUNNING );
// 		//SqliteSegmentModel::instance(pTaskId,this)->setCapacity(totalLength );
// 		this->onStartTask(pTaskId);
// 	}
// 	else
// 	{
// 		assert( 1== 2 );
// 	}
// }

// void TaskQueue::onFirstSegmentFaild( int taskId, int errorNo )
// {
// 	qLogx() << __FUNCTION__ << "taskId: "<< taskId << "errorNo :" << errorNo ;
// 	TaskQueue * tq = 0 ;
// 	//tq = this->findTaskById(taskId);
// 	tq = this ;
// 	if( tq == 0 ) 
// 	{
// 		return ;
// 	}
// 	tq->mTaskStatus = TaskQueue::TS_ERROR;
// 	this->onTaskListCellNeedChange( taskId , ng::tasks::task_status , tq->getStatusString(TaskQueue::TS_ERROR));

// }

// void TaskQueue::onStartSegment(int pTaskId,int pSegId)
// {
// 	qLogx()<<__FUNCTION__<<pTaskId<<pSegId;
// 	int row =0 ;
// 	// BaseRetriver *br = 0 ;
// 	QAbstractItemModel * logmdl = 0 ;
// 	SqliteSegmentModel * segmdl = 0 ;

// 	logmdl = SegmentLogModel::instance( pTaskId , pSegId , this );
// 	segmdl = SqliteSegmentModel::instance(pTaskId , this);
// }

// void TaskQueue::onPauseSegment(int pTaskId,int pSegId)
// {

// }

void TaskQueue::onLogSegment(int taskId, int segId, QString log, int type) 
{
	//qLogx() << __FUNCTION__ << taskId <<" "<< segId ;

	int nowRows ;
	QModelIndex index ;
	QAbstractItemModel * mdl = 0 ;	
	
	mdl = SegmentLogModel::instance(taskId, segId ,this);

	nowRows = mdl->rowCount();
	mdl->insertRows(nowRows,1);
	index = mdl->index(nowRows,ng::logs::log_type);
	mdl->setData(index,type);
	index = mdl->index(nowRows,ng::logs::add_time);
	mdl->setData(index,
		QTime::currentTime().toString("hh:mm:ss ") + QDate::currentDate().toString("yyyy-MM-dd") );
	index = mdl->index(nowRows,ng::logs::log_content);
	mdl->setData(index,log);
}

void TaskQueue::onTaskStatusNeedUpdate2(int taskId, QMap<int, QVariant> stats)
{
    int totalLength, completedLength, completedPercent,
        downloadSpeed, uploadSpeed;


    totalLength = stats.value(ng::stat::total_length).toInt();
    completedLength = stats.value(ng::stat::completed_length).toInt();
    completedPercent = stats.value(ng::stat::completed_percent).toInt();
    downloadSpeed = stats.value(ng::stat::download_speed).toInt();
    uploadSpeed = stats.value(ng::stat::upload_speed).toInt();

    int numConnections = stats.value(ng::stat::num_connections).toInt();
    int numPieces = stats.value(ng::stat::num_pieces).toInt();
    int pieceLength = stats.value(ng::stat::piece_length).toInt();
    QString str_bitfield = stats.value(ng::stat::hex_bitfield).toString();
    quint64 gid = stats.value(ng::stat::gid).toULongLong();
    QString status = stats.value(ng::stat::status).toString();
    int eta = stats.value(ng::stat::eta).toInt();
    QString str_eta = stats.value(ng::stat::str_eta).toString();
    QString error_string = stats.value(ng::stat::error_string).toString();

    qLogx()<<taskId << totalLength << completedLength<< completedPercent<< downloadSpeed<< uploadSpeed << status;

	QModelIndex idx , idx2,idx3;
	quint64 fsize , abtained ;
	double  percent ;
    // bool found = false;
    unsigned short bitfield = 0;
    QStringList bitList;
    QString bitString;
    // QVariantList files = sts["files"].toList();

	//maybe should use mCatID , but we cant know the value here , so use default downloading cat
	QAbstractItemModel *mdl = SqliteTaskModel::instance(ng::cats::downloading, this);

	//QDateTime bTime , eTime ; 
	//bTime = QDateTime::currentDateTime();
    QModelIndexList mil = mdl->match(mdl->index(0, ng::tasks::task_id), Qt::DisplayRole,
                                     QVariant(QString("%1").arg(taskId)), 1, Qt::MatchExactly | Qt::MatchWrap);

    if (mil.count() == 1) {
        idx = mil.at(0);
        if (completedLength == 0 && totalLength == 0) {
            mdl->setData(mdl->index(idx.row(), ng::tasks::task_status), status);
        } else if (completedLength == totalLength && completedLength > 0) {
            // mdl->setData(mdl->index(idx.row(), ng::tasks::finish_time), QDateTime::currentDateTime().toString());
            // // fix no need download data case, the file is already downloaded, user readd it.
            // if (files.count() > 0 
            //     && files.at(0).toMap().value("length").toLongLong() > 
            //     mdl->index(idx.row(), ng::tasks::file_size).data().toLongLong()) {
            //     mdl->setData(mdl->index(idx.row(), ng::tasks::file_size), files.at(0).toMap().value("length"));
            // }
            mdl->setData(mdl->index(idx.row(), ng::tasks::file_size), totalLength);
            mdl->setData(mdl->index(idx.row(), ng::tasks::abtained_length), completedLength);
            mdl->setData(mdl->index(idx.row(), ng::tasks::task_status), "complete");
            mdl->setData(mdl->index(idx.row(), ng::tasks::comment), error_string);
            mdl->setData(mdl->index(idx.row(), ng::tasks::left_length), QVariant("0"));
            mdl->setData(mdl->index(idx.row(), ng::tasks::abtained_percent), QString("%1").arg(100));
            mdl->setData(mdl->index(idx.row(), ng::tasks::finish_time),
                         QDateTime::currentDateTime().toString("hh:mm:ss yyyy-MM-dd"));
            mdl->setData(mdl->index(idx.row(), ng::tasks::left_timestamp), QString::number(0));
        } else {
            mdl->setData(mdl->index(idx.row(), ng::tasks::file_size), totalLength);
            mdl->setData(mdl->index(idx.row(), ng::tasks::current_speed), downloadSpeed);
            mdl->setData(mdl->index(idx.row(), ng::tasks::abtained_length), completedLength);
            mdl->setData(mdl->index(idx.row(), ng::tasks::task_status), status);
            mdl->setData(mdl->index(idx.row(), ng::tasks::active_block_count), numConnections);
            mdl->setData(mdl->index(idx.row(), ng::tasks::left_length), totalLength - completedLength);
            mdl->setData(mdl->index(idx.row(), ng::tasks::total_block_count), numPieces);
            mdl->setData(mdl->index(idx.row(), ng::tasks::block_activity), 
                         QString("%1/%2").arg(numConnections).arg(numPieces));
            mdl->setData(mdl->index(idx.row(), ng::tasks::abtained_percent), completedPercent);
            mdl->setData(mdl->index(idx.row(), ng::tasks::aria_gid), gid);
            mdl->setData(mdl->index(idx.row(), ng::tasks::total_packet), str_bitfield);
            mdl->setData(mdl->index(idx.row(), ng::tasks::left_timestamp), str_eta);

        }

        // 计算完成的pieces
        // if (sts["status"].toString() == "complete") {
        //     int blockCount = mdl->data(mdl->index(idx.row(), ng::tasks::total_block_count)).toInt();
        //     QStringList bitList;
        //     for (int i = 0 ; i < blockCount; i ++) bitList << "1";
        //     mdl->setData(mdl->index(idx.row(), ng::tasks::total_packet), bitList.join(","));
        // } else {
        //     mdl->setData(mdl->index(idx.row(), ng::tasks::total_packet), 
        //                  this->fromBitArray(this->fromHexBitString(sts["bitfield"].toString())));
        // }

        // fix case that can not resovle file from url, must conntect to server 
        // if (!sts.contains("bittorrent")) {
        //     QVariantMap  fileMap0;
        //     QString fileName;
        //     QString fileSize;
        //     if (files.count() > 0) {
        //         fileMap0 = files.at(0).toMap();
        //         fileName = fileMap0.value("path").toString();
        //         if (sts.contains("dir")) {
        //             fileName = fileName.right(fileName.length() - sts["dir"].toString().length() - 1);                    
        //         } else {
        //             fileName = QFileInfo(fileName).fileName();
        //         }
        //         mdl->setData(mdl->index(idx.row(), ng::tasks::file_name), fileName);
        //     }
        // }

        // 处理bt信息
        // if (sts.contains("bittorrent")) {
        //     QVariantMap btSts = sts["bittorrent"].toMap();
        //     qLogx()<<"announceList:"<<btSts["announceList"]
        //             <<"comment:"<<btSts["comment"]
        //             <<"createDate:"<<btSts["createDate"]
        //             <<"mode:"<<btSts["mode"]
        //             <<"info:"<<btSts["info"];
        // }
    } else {
        qLogx()<<__FUNCTION__<<"Can not found update model" << mil.count();
    }

}

void TaskQueue::onTaskStatusNeedUpdate(int taskId, QVariantMap &sts)
{
	//qLogx() << __FUNCTION__ ;

	QModelIndex idx , idx2,idx3;
	quint64 fsize , abtained ;
	double  percent ;
    // bool found = false;
    unsigned short bitfield = 0;
    int numPieces = 0;
    QStringList bitList;
    QString bitString;
    QVariantList files = sts["files"].toList();

	//maybe should use mCatID , but we cant know the value here , so use default downloading cat
	QAbstractItemModel *mdl = SqliteTaskModel::instance(ng::cats::downloading, this);

	//QDateTime bTime , eTime ; 
	//bTime = QDateTime::currentDateTime();
    QModelIndexList mil = mdl->match(mdl->index(0, ng::tasks::task_id), Qt::DisplayRole,
                                     QVariant(QString("%1").arg(taskId)), 1, Qt::MatchExactly | Qt::MatchWrap);

    if (mil.count() == 1) {
        idx = mil.at(0);
        if (sts["status"].toString() == "complete") {
            mdl->setData(mdl->index(idx.row(), ng::tasks::finish_time), QDateTime::currentDateTime().toString());
            // fix no need download data case, the file is already downloaded, user readd it.
            if (files.count() > 0 
                && files.at(0).toMap().value("length").toLongLong() > 
                mdl->index(idx.row(), ng::tasks::file_size).data().toLongLong()) {
                mdl->setData(mdl->index(idx.row(), ng::tasks::file_size), files.at(0).toMap().value("length"));
            }
            mdl->setData(mdl->index(idx.row(), ng::tasks::abtained_length), 
                         mdl->data(mdl->index(idx.row(), ng::tasks::file_size), Qt::EditRole));
            mdl->setData(mdl->index(idx.row(), ng::tasks::task_status), sts["status"]);
            mdl->setData(mdl->index(idx.row(), ng::tasks::left_length), QVariant("0"));
            mdl->setData(mdl->index(idx.row(), ng::tasks::abtained_percent), QString("%1").arg(100));
            mdl->setData(mdl->index(idx.row(), ng::tasks::finish_time),
                         QDateTime::currentDateTime().toString("hh:mm:ss yyyy-MM-dd"));
        } else {
            mdl->setData(mdl->index(idx.row(), ng::tasks::file_size), sts["totalLength"]);
            mdl->setData(mdl->index(idx.row(), ng::tasks::current_speed), sts["downloadSpeed"]);
            mdl->setData(mdl->index(idx.row(), ng::tasks::abtained_length), sts["completedLength"]);
            mdl->setData(mdl->index(idx.row(), ng::tasks::task_status), sts["status"]);
            mdl->setData(mdl->index(idx.row(), ng::tasks::active_block_count), sts["connections"]);
            mdl->setData(mdl->index(idx.row(), ng::tasks::left_length), 
                         (sts["totalLength"].toLongLong() - sts["completedLength"].toLongLong()));
            mdl->setData(mdl->index(idx.row(), ng::tasks::total_block_count), sts["numPieces"]);
            mdl->setData(mdl->index(idx.row(), ng::tasks::block_activity), 
                         QString("%1/%2").arg(sts["connections"].toString()).arg(sts["numPieces"].toString()));
            mdl->setData(mdl->index(idx.row(), ng::tasks::abtained_percent)
                         , QString("%1").arg((sts["totalLength"].toLongLong() == 0) ? 0 
                                               : (sts["completedLength"].toLongLong()*100.0 / sts["totalLength"].toLongLong()*1.0)));
        }

        // 计算完成的pieces
        if (sts["status"].toString() == "complete") {
            int blockCount = mdl->data(mdl->index(idx.row(), ng::tasks::total_block_count)).toInt();
            QStringList bitList;
            for (int i = 0 ; i < blockCount; i ++) bitList << "1";
            mdl->setData(mdl->index(idx.row(), ng::tasks::total_packet), bitList.join(","));
        } else {
            mdl->setData(mdl->index(idx.row(), ng::tasks::total_packet), 
                         this->fromBitArray(this->fromHexBitString(sts["bitfield"].toString())));
        }

        // fix case that can not resovle file from url, must conntect to server 
        if (!sts.contains("bittorrent")) {
            QVariantMap  fileMap0;
            QString fileName;
            QString fileSize;
            if (files.count() > 0) {
                fileMap0 = files.at(0).toMap();
                fileName = fileMap0.value("path").toString();
                if (sts.contains("dir")) {
                    fileName = fileName.right(fileName.length() - sts["dir"].toString().length() - 1);                    
                } else {
                    fileName = QFileInfo(fileName).fileName();
                }
                mdl->setData(mdl->index(idx.row(), ng::tasks::file_name), fileName);
            }
        }

        // 处理bt信息
        // if (sts.contains("bittorrent")) {
        //     QVariantMap btSts = sts["bittorrent"].toMap();
        //     qLogx()<<"announceList:"<<btSts["announceList"]
        //             <<"comment:"<<btSts["comment"]
        //             <<"createDate:"<<btSts["createDate"]
        //             <<"mode:"<<btSts["mode"]
        //             <<"info:"<<btSts["info"];
        // }
    } else {
        qLogx()<<__FUNCTION__<<"Can not found update model";
    }

    // qLogx()<<"found status:"<<found;
}

QBitArray TaskQueue::fromHexBitString(QString fields)
{
    QBitArray ba;
    int hLen = fields.length();
    unsigned char ch = 0;

    ba.resize(hLen * 4);
    for (int i = 0; i < hLen; i ++) {
        ch = QString(fields.at(i)).toUShort(NULL, 16);
        for (int j =  3 ; j >= 0; j --) {
            ba.setBit(i * 4 + 3 - j, (ch >> j) & 0x01);
        }
    }
    
    // qLogx()<<fields<<ba<<ba.size();
    return ba;
}

QString TaskQueue::fromBitArray(QBitArray ba)
{
    QStringList bitList;
    for (int i = 0 ; i < ba.size(); i ++) {
        bitList << (ba.testBit(i) ? "1" : "0");
    }
    return bitList.join(",");
}

void TaskQueue::onTaskListCellNeedChange(int taskId, int cellId, QString value)
{
	//qLogx() << __FUNCTION__ ;

	QModelIndex idx, idx2, idx3;
	quint64 fsize, abtained;
	double  percent;
    // bool found = false;
    int row;

	//maybe should use mCatID , but we cant know the value here , so use default downloading cat
	QAbstractItemModel *mdl = SqliteTaskModel::instance(ng::cats::downloading, this);

    QModelIndexList mil = mdl->match(mdl->index(0, ng::tasks::task_id), Qt::DisplayRole, 
                                     QString("%1").arg(taskId), 1, Qt::MatchExactly | Qt::MatchWrap);

    // qLogx()<<"match found cell change:"<<mil<<taskId<<value;
    if (mil.count() == 1) {
        row = mil.at(0).row();
        idx = mdl->index(row, cellId);
        //change the value 
        qLogx()<<mdl->data(idx);
        mdl->setData(idx, value);
        if(cellId == ng::tasks::abtained_length) {
            idx = mdl->index(row, ng::tasks::file_size);
            idx2 = mdl->index(row, ng::tasks::left_length) ;
            idx3 = mdl->index(row, ng::tasks::abtained_percent) ;
            abtained = value.toULongLong() ;
            fsize = mdl->data(idx).toULongLong() ;
            percent = 100.0*abtained/ fsize ;
            //change the value 
            mdl->setData(idx2, fsize - abtained);
            mdl->setData(idx3, percent);

            //通知DropZone
            emit this->taskCompletionChanged(taskId, (int)percent, 
                                             this->getRealUrlModel(taskId, ng::cats::downloading));
            emit this->taskCompletionChanged(this, false) ;
        }
    }

}

void TaskQueue::onTaskServerStatusNeedUpdate(int taskId, QList<QMap<QString, QString> > stats)
{
    TaskServerModel *model;

    if (this->mTasks.contains(taskId)) {
        model = this->mTasks[taskId]->serverModel;
        model->setData(stats);
    }
}

//void TaskQueue::onSegmentGotLengthNeedUpdate ( int taskId , int segId , long delta , QString opt )
void TaskQueue::onSegmentGotLengthNeedUpdate ( int taskId , int segId , long delta , int optType )
{
	//qLogx() << __FUNCTION__ << delta << optType ;
	TaskQueue * tq = 0 ;
	SqliteSegmentModel * mdl = 0 ;
	
	QDateTime currTime = QDateTime::currentDateTime();

	//mAtomMutex.lock();

	int nowRows ;
	QModelIndex index ;	
	
	int segCount ;
	// QVector<BaseRetriver*> sq ;
	// BaseRetriver* psq ;
	quint64 task_abtained = 0 ;
	
	tq = this ;	


	return;	
}
/**
 * 用于修改线程模型的数据。
 */
void TaskQueue::onSegmentCellNeedChange( int taskId , int segId ,  int cellId , QString value ) 
{
	//qLogx() << __FUNCTION__ ;

	QModelIndex idx , segidx ;

	//maybe should use mCatID , but we cant know the value here , so use default downloading cat
	QAbstractItemModel * mdl = SqliteSegmentModel::instance( taskId ,  this) ;

	int taskCount = mdl->rowCount();
	for( int i = 0 ; i < taskCount ; i ++ )
	{	
		//find the cell index
		idx = mdl->index(i,ng::segs::task_id );
		segidx = mdl->index(i,ng::segs::seg_id );

		if( idx.data().toInt() == (taskId) && segidx.data().toInt() == segId  )
		{			
		//	qLogx()<<"found index of cell" ;

			idx = mdl->index(i,cellId);
			//change the value 
			mdl->setData(idx,value);

			break;
		}
	}
}

//void TaskQueue::onProgressState(int tid, quint32 gid, quint64 total_length,
//               quint64 curr_length, quint32 down_speed, quint32 up_speed,
//               quint32 num_conns, quint32 eta)
//{
//    QModelIndex idx , idx2,idx3;
//    quint64 fsize , abtained ;
//    double  percent ;
//    // bool found = false;
//    unsigned short bitfield = 0;
//    int numPieces = 0;
//    QStringList bitList;
//    QString bitString;
//    // QVariantList files = sts["files"].toList();

//    //maybe should use mCatID , but we cant know the value here , so use default downloading cat
//    QAbstractItemModel *mdl = SqliteTaskModel::instance(ng::cats::downloading, this);

//    //QDateTime bTime , eTime ;
//    //bTime = QDateTime::currentDateTime();
//    QModelIndexList mil = mdl->match(mdl->index(0, ng::tasks::task_id), Qt::DisplayRole,
//                                     QVariant(QString("%1").arg(tid)), 1, Qt::MatchExactly | Qt::MatchWrap);

//    if (mil.count() == 1) {
//        idx = mil.at(0);
//        // if (sts["status"].toString() == "complete") {
//        if (total_length == curr_length) {
//            mdl->setData(mdl->index(idx.row(), ng::tasks::finish_time), QDateTime::currentDateTime().toString());
//            // fix no need download data case, the file is already downloaded, user readd it.
////            if (files.count() > 0
////                && files.at(0).toMap().value("length").toLongLong() >
////                mdl->index(idx.row(), ng::tasks::file_size).data().toLongLong()) {
////                mdl->setData(mdl->index(idx.row(), ng::tasks::file_size), files.at(0).toMap().value("length"));
////            }
//            mdl->setData(mdl->index(idx.row(), ng::tasks::abtained_length),
//                         mdl->data(mdl->index(idx.row(), ng::tasks::file_size), Qt::EditRole));
//            mdl->setData(mdl->index(idx.row(), ng::tasks::task_status), QString("Done"));
//            mdl->setData(mdl->index(idx.row(), ng::tasks::left_length), QVariant("0"));
//            mdl->setData(mdl->index(idx.row(), ng::tasks::abtained_percent), QString("%1").arg(100));
//            mdl->setData(mdl->index(idx.row(), ng::tasks::finish_time),
//                         QDateTime::currentDateTime().toString("hh:mm:ss yyyy-MM-dd"));
//        } else {
//            mdl->setData(mdl->index(idx.row(), ng::tasks::file_size), QString::number(total_length));
//            mdl->setData(mdl->index(idx.row(), ng::tasks::current_speed), QString::number(down_speed));
//            mdl->setData(mdl->index(idx.row(), ng::tasks::abtained_length), QString::number(curr_length));
//            mdl->setData(mdl->index(idx.row(), ng::tasks::task_status), QString("active"));
//            mdl->setData(mdl->index(idx.row(), ng::tasks::active_block_count), QString::number(num_conns));
//            mdl->setData(mdl->index(idx.row(), ng::tasks::left_length),
//                         QString::number(total_length - curr_length));
//            mdl->setData(mdl->index(idx.row(), ng::tasks::total_block_count), QString::number(num_conns));
//            mdl->setData(mdl->index(idx.row(), ng::tasks::block_activity),
//                         QString("%1/%2").arg(num_conns).arg(num_conns));
//            mdl->setData(mdl->index(idx.row(), ng::tasks::abtained_percent)
//                         , QString("%1").arg((total_length == 0) ? 0
//                                               : (curr_length*100.0 / total_length*1.0)));
//            mdl->setData(mdl->index(idx.row(), ng::tasks::left_timestamp),
//                         QString::fromStdString(aria2::util::secfmt(eta)));
//            mdl->setData(mdl->index(idx.row(), ng::tasks::aria_gid),
//                         QString::number(gid));
//        }

//        // 计算完成的pieces
//        // if (sts["status"].toString() == "complete") {
//        if (total_length == curr_length) {
//            int blockCount = mdl->data(mdl->index(idx.row(), ng::tasks::total_block_count)).toInt();
//            QStringList bitList;
//            for (int i = 0 ; i < blockCount; i ++) bitList << "1";
//            mdl->setData(mdl->index(idx.row(), ng::tasks::total_packet), bitList.join(","));
//        } else {
////            mdl->setData(mdl->index(idx.row(), ng::tasks::total_packet),
////                         this->fromBitArray(this->fromHexBitString(sts["bitfield"].toString())));
//        }

//        // fix case that can not resovle file from url, must conntect to server
////        if (!sts.contains("bittorrent")) {
////            QVariantMap  fileMap0;
////            QString fileName;
////            QString fileSize;
////            if (files.count() > 0) {
////                fileMap0 = files.at(0).toMap();
////                fileName = fileMap0.value("path").toString();
////                if (sts.contains("dir")) {
////                    fileName = fileName.right(fileName.length() - sts["dir"].toString().length() - 1);
////                } else {
////                    fileName = QFileInfo(fileName).fileName();
////                }
////                mdl->setData(mdl->index(idx.row(), ng::tasks::file_name), fileName);
////            }
////        }

//        // 处理bt信息
//        // if (sts.contains("bittorrent")) {
//        //     QVariantMap btSts = sts["bittorrent"].toMap();
//        //     qLogx()<<"announceList:"<<btSts["announceList"]
//        //             <<"comment:"<<btSts["comment"]
//        //             <<"createDate:"<<btSts["createDate"]
//        //             <<"mode:"<<btSts["mode"]
//        //             <<"info:"<<btSts["info"];
//        // }
//    } else {
//        qLogx()<<__FUNCTION__<<"Can not found update model";
//    }
//}

/*
void TaskQueue::onProgressState(Aria2StatCollector *stats)
{
    QModelIndex idx , idx2,idx3;
    quint64 fsize , abtained ;
    double  percent ;
    // bool found = false;
    unsigned short bitfield = 0;
    int numPieces = 0;
    QStringList bitList;
    QString bitString;
    // QVariantList files = sts["files"].toList();

    //maybe should use mCatID , but we cant know the value here , so use default downloading cat
    QAbstractItemModel *mdl = SqliteTaskModel::instance(ng::cats::downloading, this);

    //QDateTime bTime , eTime ;
    //bTime = QDateTime::currentDateTime();
    QModelIndexList mil = mdl->match(mdl->index(0, ng::tasks::task_id), Qt::DisplayRole,
                                     QVariant(QString("%1").arg(stats->tid)), 1, Qt::MatchExactly | Qt::MatchWrap);

    if (mil.count() == 1) {
        idx = mil.at(0);
        // if (sts["status"].toString() == "complete") {
        if (stats->totalLength == stats->completedLength) {
            mdl->setData(mdl->index(idx.row(), ng::tasks::finish_time), QDateTime::currentDateTime().toString());
            // fix no need download data case, the file is already downloaded, user readd it.
//            if (files.count() > 0
//                && files.at(0).toMap().value("length").toLongLong() >
//                mdl->index(idx.row(), ng::tasks::file_size).data().toLongLong()) {
//                mdl->setData(mdl->index(idx.row(), ng::tasks::file_size), files.at(0).toMap().value("length"));
//            }
            mdl->setData(mdl->index(idx.row(), ng::tasks::abtained_length),
                         mdl->data(mdl->index(idx.row(), ng::tasks::file_size), Qt::EditRole));
            mdl->setData(mdl->index(idx.row(), ng::tasks::task_status), QString("Done"));
            mdl->setData(mdl->index(idx.row(), ng::tasks::left_length), QVariant("0"));
            mdl->setData(mdl->index(idx.row(), ng::tasks::abtained_percent), QString("%1").arg(100));
            mdl->setData(mdl->index(idx.row(), ng::tasks::finish_time),
                         QDateTime::currentDateTime().toString("hh:mm:ss yyyy-MM-dd"));
        } else {
            mdl->setData(mdl->index(idx.row(), ng::tasks::file_size), QString::number(stats->totalLength));
            mdl->setData(mdl->index(idx.row(), ng::tasks::current_speed), QString::number(stats->downloadSpeed));
            mdl->setData(mdl->index(idx.row(), ng::tasks::average_speed), QString::number(stats->downloadSpeed));
            mdl->setData(mdl->index(idx.row(), ng::tasks::abtained_length), QString::number(stats->completedLength));
            mdl->setData(mdl->index(idx.row(), ng::tasks::task_status), QString("active"));
            mdl->setData(mdl->index(idx.row(), ng::tasks::active_block_count), QString::number(stats->connections));
            mdl->setData(mdl->index(idx.row(), ng::tasks::left_length),
                         QString::number(stats->totalLength - stats->completedLength));
            mdl->setData(mdl->index(idx.row(), ng::tasks::total_block_count), QString::number(stats->numPieces));
            mdl->setData(mdl->index(idx.row(), ng::tasks::block_activity),
                         QString("%1/%2").arg(stats->connections).arg(stats->numPieces));
            mdl->setData(mdl->index(idx.row(), ng::tasks::abtained_percent)
                         , QString("%1").arg((stats->totalLength == 0) ? 0
                                               : (stats->completedLength*100.0 / stats->totalLength*1.0)));
            mdl->setData(mdl->index(idx.row(), ng::tasks::left_timestamp),
                         QString::fromStdString(aria2::util::secfmt(stats->eta)));
            mdl->setData(mdl->index(idx.row(), ng::tasks::aria_gid),
                         QString::number(stats->gid));
        }

        // 计算完成的pieces
        // if (sts["status"].toString() == "complete") {
        if (stats->totalLength == stats->completedLength) {
            int blockCount = mdl->data(mdl->index(idx.row(), ng::tasks::total_block_count)).toInt();
            QStringList bitList;
            for (int i = 0 ; i < blockCount; i ++) bitList << "1";
            mdl->setData(mdl->index(idx.row(), ng::tasks::total_packet), bitList.join(","));
        } else {
            mdl->setData(mdl->index(idx.row(), ng::tasks::total_packet),
                         this->fromBitArray(this->fromHexBitString(QString::fromStdString(stats->bitfield))));
        }

        // fix case that can not resovle file from url, must conntect to server
//        if (!sts.contains("bittorrent")) {
//            QVariantMap  fileMap0;
//            QString fileName;
//            QString fileSize;
//            if (files.count() > 0) {
//                fileMap0 = files.at(0).toMap();
//                fileName = fileMap0.value("path").toString();
//                if (sts.contains("dir")) {
//                    fileName = fileName.right(fileName.length() - sts["dir"].toString().length() - 1);
//                } else {
//                    fileName = QFileInfo(fileName).fileName();
//                }
//                mdl->setData(mdl->index(idx.row(), ng::tasks::file_name), fileName);
//            }
//        }

        // 处理bt信息
        // if (sts.contains("bittorrent")) {
        //     QVariantMap btSts = sts["bittorrent"].toMap();
        //     qLogx()<<"announceList:"<<btSts["announceList"]
        //             <<"comment:"<<btSts["comment"]
        //             <<"createDate:"<<btSts["createDate"]
        //             <<"mode:"<<btSts["mode"]
        //             <<"info:"<<btSts["info"];
        // }
    } else {
        qLogx()<<__FUNCTION__<<"Can not found update model";
    }

    delete stats;
}
*/


//void TaskQueue::onTaskDone(int pTaskId)	//
//{
//	qLogx() << __FUNCTION__ ;
//
//}

	// more from nullget.h

void TaskQueue::onMemoryOverLoad()
{
	qLogx()<<__FUNCTION__<<__LINE__ ;
	// emit this->onTaskDone( this->mTaskId );
}

bool TaskQueue::onStartTask(int pTaskId) 
{
	////假设任务的运行句柄即TaskQueue实例不存在,创建任务实例，并启动它。
	//assert( TaskQueue::containsInstance(pTaskId) == false ) ;
	
	if( TaskQueue::containsInstance(pTaskId) == false ) 
	{
		TaskQueue * taskQueue = NULL;//TaskQueue::instance(pTaskId,0);
		// if (taskQueue->canceled == true) {
		// 	qLogx()<<__FUNCTION__<<__LINE__<<" task caceled";
		// 	return false;
		// }
		if ( taskQueue->getFileAbtained(pTaskId,ng::cats::downloading) ) {
			int segCount = taskQueue->getMaxSegCount(pTaskId,ng::cats::downloading) ;
			for (int i = 0 ; i < segCount ; i ++ ) {
				// taskQueue->onStartSegment(pTaskId, i);
			}
		} else {
			// taskQueue->onStartSegment(pTaskId,0);
		}
    } else {
		TaskQueue * taskQueue = NULL;// TaskQueue::instance(pTaskId,0);
		// if( taskQueue->canceled == true ) 
		// {
		// 	qLogx()<<__FUNCTION__<<__LINE__<<" task caceled";
		// 	return false ;
		// }		
		// if ( taskQueue->getFileAbtained(pTaskId,ng::cats::downloading) )
		// {
		// 	int segCount = taskQueue->getMaxSegCount(pTaskId,ng::cats::downloading) ;
		// 	for( int i = 0 ; i < segCount ; i ++ )
		// 	{
		// 		taskQueue->onStartSegment(pTaskId,i);
		// 	}
		// }
		// else
		// {
		// 	taskQueue->onStartSegment(pTaskId,0);
		// }
	}

	return false ;
}

void TaskQueue::onPauseTask(int pTaskId ) 
{
	if (this->mTasks.contains(pTaskId)) {
		// assert(1 == 2);		
        Task *task = this->mTasks[pTaskId];
        this->mTasks.remove(pTaskId);
        delete task;
	} else {
		// TaskQueue * taskQueue = TaskQueue::instance(pTaskId,0);
		// taskQueue->canceled = true ;

		// TaskQueue::removeInstance(pTaskId);
		//delete taskQueue;taskQueue = 0 ;		
		//delete 操作在　onOneSegmentFinished　成员函数中
        qLogx()<<__FUNCTION__<<"No task meta object found:"<<pTaskId;
	}	
}


int TaskQueue::getMaxSegCount(int pTaskId,int cat_id)
{
	int msc = -1 ;
	SqliteTaskModel * tm = SqliteTaskModel::instance(cat_id,0);
	QModelIndex mix ;

	for( int i = 0 ; i < tm->rowCount() ; i ++ )
	{
		mix = tm->index(i,ng::tasks::task_id);
		int currTaskId = tm->data(mix).toInt();
		if( currTaskId == pTaskId )
		{
			msc = tm->data(tm->index(i,ng::tasks::total_block_count)).toInt() ;

			break ;
		}
	}

	assert( msc > 0 );
	return msc ;
}

bool TaskQueue::getFileAbtained( int pTaskId , int cat_id)
{
	bool msc = false ;
	SqliteTaskModel * tm = SqliteTaskModel::instance(cat_id,0);
	QModelIndex mix ;

	for( int i = 0 ; i < tm->rowCount() ; i ++ )
	{
		mix = tm->index(i,ng::tasks::task_id);
		int currTaskId = tm->data(mix).toInt();
		if( currTaskId == pTaskId )
		{
			QString vv = tm->data(tm->index(i,ng::tasks::file_length_abtained)).toString() ;
			msc = vv == "true" ? true : false ;
			break ;
		}
	}
	
	return msc ;
}

QString TaskQueue::getRealUrlModel(int pTaskId,int cat_id)
{
	QString msc  ;
	SqliteTaskModel * tm = SqliteTaskModel::instance(cat_id,0);
	QModelIndex mix ;

	for( int i = 0 ; i < tm->rowCount() ; i ++ )
	{
		mix = tm->index(i,ng::tasks::task_id);
		int currTaskId = tm->data(mix).toInt();
		if( currTaskId == pTaskId )
		{
			msc = tm->data(tm->index(i,ng::tasks::real_url)).toString() ;
			break ;
		}
	}	
	return msc ;
}

int TaskQueue::getActiveSegCount(int pTaskId, int cat_id)
{
	int msc = -1 ;
	SqliteTaskModel *tm = SqliteTaskModel::instance(cat_id,0);
	QModelIndex mix;

	return msc;
}

// void TaskQueue::setTaskId(int task_id)
// {
// 	// this->mTaskId = task_id ;
// }

QBitArray TaskQueue::getCompletionBitArray(int taskId, QString &bitStr, QString &hexBitStr)
{
	QModelIndex idx;
    QString bitString;
    int numPieces = 0;

	//maybe should use mCatID , but we cant know the value here , so use default downloading cat
    QAbstractItemModel *mdl = SqliteTaskModel::instance(ng::cats::downloading, this);

    // i known it must be only one match
    QModelIndexList mil = mdl->match(mdl->index(0, ng::tasks::task_id), Qt::DisplayRole, 
                                     QVariant(QString("%1").arg(taskId)), 1, Qt::MatchExactly | Qt::MatchWrap);
    // qLogx()<<"found match model:"<<mil;

    if (mil.count() == 1) {
        idx = mil.at(0);
        bitString = mdl->data(mdl->index(idx.row(), ng::tasks::total_packet)).toString();
        numPieces = mdl->data(mdl->index(idx.row(), ng::tasks::total_block_count)).toInt();
        // qLogx()<<__FUNCTION__<<numPieces<<bitString;

        QBitArray ba;
        // ba.resize(qMin(bitList.length(), numPieces));
        ba = this->fromHexBitString(bitString);
        bitStr = this->fromBitArray(ba);
        ba.resize(numPieces);
        // qLogx()<<"string is "<<bitString<<" BS:"<<ba.size()<<ba;
        // dumpBitArray(ba);
        // return bitString;
        return ba;
    } else {
        // qLogx()<<__FUNCTION__<<"can not found bit model";
        return QBitArray();
    }
}

// void TaskQueue::onTaskLogArrived(QString cuid, QString itime, QString log)
// {
//     int taskId;
//     Task *task = NULL;
    
//     // qLogx()<<"LOG-PART:"<<cuid<<itime<<log;
//     return; // use so much memory, omit it now

//     if (this->mTaskCUIDs.contains(cuid)) {
//         taskId = this->mTaskCUIDs.value(cuid);
//         task = this->mTasks.value(taskId);

//         task->mLogs.append(QPair<QString, QString>(itime, log));
//     } else {
//         // unattached log
//         if (this->mUnattachedLogs.contains(cuid)) {
            
//             this->mUnattachedLogs[cuid].append(QPair<QString, QString>(itime, log));
//         } else {
//             QVector<QPair<QString, QString> > vlog;
//             vlog.append(QPair<QString, QString>(itime, log));
//             this->mUnattachedLogs.insert(cuid, vlog);
//         }
//     }
// }

void TaskQueue::deleteLater () 
{
	qLogx()<<__FUNCTION__<<__LINE__;
	QObject::deleteLater();
}



