#pragma once

#include <QTreeWidget>

class LibraryListView : public QTreeWidget {
    Q_OBJECT
public:
    explicit LibraryListView(QWidget* parent = nullptr);

    struct ItemData {
        QString name;
        QString path;
        int itemCount = 0;
        qint64 lastModifiedMs = 0;
    };

    void clear();
    void addItem(const ItemData& data);
    void setRootFilter(const QString& rootPath);
    void setTextFilter(const QString& query);

signals:
    void itemActivated(const QString& path);
    void itemRightClicked(const QString& path, const QPoint& globalPos);

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void applyFilters();

    struct Row {
        ItemData data;
        QTreeWidgetItem* item = nullptr;
    };
    QList<Row> m_rows;
    QString m_rootFilter;
    QString m_textFilter;
};
