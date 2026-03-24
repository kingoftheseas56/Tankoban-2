#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>

class BookSeriesView : public QWidget {
    Q_OBJECT
public:
    explicit BookSeriesView(QWidget* parent = nullptr);

    void showSeries(const QString& seriesPath, const QString& seriesName);

signals:
    void backRequested();
    void bookSelected(const QString& filePath);

private:
    QLabel*      m_titleLabel = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    QString      m_seriesPath;
};
