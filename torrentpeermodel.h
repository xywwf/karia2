// torrentpeermodel.h --- 
// 
// Author: liuguangzhao
// Copyright (C) 2007-2010 liuguangzhao@users.sf.net
// URL: 
// Created: 2010-04-07 10:55:37 +0800
// Version: $Id: torrentpeermodel.h 80 2010-04-07 07:18:09Z liuguangzhao $
// 

#ifndef _TORRENTPEERMODEL_H_
#define _TORRENTPEERMODEL_H_


#include <QtCore>
#include <QtGui>

class TorrentPeerModel : public QAbstractTableModel
{
    Q_OBJECT;
public:
    TorrentPeerModel(QObject *parent = 0);
    ~TorrentPeerModel();

    bool setData(QVariantList &peers);

    int columnCount(const QModelIndex &parent) const;
    int rowCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
    virtual QVariant 	headerData ( int section, Qt::Orientation orientation, int role = Qt::DisplayRole ) const;
    bool insertRows ( int row, int count, const QModelIndex & parent = QModelIndex() );
    bool removeRows ( int row, int count, const QModelIndex & parent = QModelIndex() );

private:
    QVariantList mPeers;
    char *columnHeaders;
};

#endif /* _TORRENTPEERMODEL_H_ */
