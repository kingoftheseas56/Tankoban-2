#pragma once

#include <QWidget>
#include <QListWidget>
#include <QCheckBox>

class PlaylistDrawer : public QWidget {
    Q_OBJECT

public:
    explicit PlaylistDrawer(QWidget* parent = nullptr);

    void populate(const QStringList& paths, int currentIndex);
    void toggle();
    bool isOpen() const { return isVisible(); }
    bool isAutoAdvance() const;

signals:
    void episodeSelected(int index);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void dismiss();
    void installClickFilter();
    void removeClickFilter();

    QListWidget* m_list                 = nullptr;
    QCheckBox*   m_autoAdvance          = nullptr;
    bool         m_clickFilterInstalled = false;
};
