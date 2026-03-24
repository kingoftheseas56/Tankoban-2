#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>

class SeriesView : public QWidget {
    Q_OBJECT
public:
    explicit SeriesView(QWidget* parent = nullptr);

    void showSeries(const QString& seriesPath, const QString& seriesName);

signals:
    void backRequested();
    void issueSelected(const QString& cbzPath);

private:
    QLabel*      m_titleLabel = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    QString      m_seriesPath;
};
