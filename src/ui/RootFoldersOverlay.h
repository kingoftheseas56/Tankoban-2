#pragma once

#include <QWidget>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

class CoreBridge;

class RootFoldersOverlay : public QWidget {
    Q_OBJECT
public:
    explicit RootFoldersOverlay(CoreBridge* bridge, QWidget* parent = nullptr);

    void refresh(const QString& domain);

signals:
    void closeRequested();
    void foldersChanged();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void clearBody();
    QWidget* folderRow(const QString& path);
    void onAdd();
    void onRemove(const QString& path);

    CoreBridge*  m_bridge;
    QString      m_domain;
    QFrame*      m_card      = nullptr;
    QLabel*      m_title     = nullptr;
    QVBoxLayout* m_bodyLayout = nullptr;
    QPushButton* m_addBtn    = nullptr;
};
