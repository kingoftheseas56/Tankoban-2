#pragma once

#include <QObject>
#include <QRunnable>
#include <QPixmap>
#include <QString>

class DecodeTaskSignals : public QObject {
    Q_OBJECT
signals:
    void decoded(int pageIndex, const QPixmap& pixmap, int width, int height, int volumeId);
    void dimensionsReady(int pageIndex, int w, int h);
    void failed(int pageIndex);
};

class DecodeTask : public QRunnable {
public:
    DecodeTask(int pageIndex, const QString& cbzPath, const QString& pageName, int volumeId);

    void run() override;

    DecodeTaskSignals notifier;

private:
    int m_pageIndex;
    QString m_cbzPath;
    QString m_pageName;
    int m_volumeId;
};
