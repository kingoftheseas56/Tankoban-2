#pragma once

#include <QWidget>
#include <QGroupBox>
#include <QLabel>

class CoreBridge;
class StreamLibrary;
class TileStrip;

class StreamContinueStrip : public QWidget
{
    Q_OBJECT

public:
    explicit StreamContinueStrip(CoreBridge* bridge, StreamLibrary* library,
                                 QWidget* parent = nullptr);

    void refresh();

signals:
    void playRequested(const QString& imdbId, int season, int episode);

private:
    void buildUI();

    CoreBridge*    m_bridge;
    StreamLibrary* m_library;

    QGroupBox*  m_group = nullptr;
    TileStrip*  m_strip = nullptr;

    static constexpr int MAX_ITEMS = 20;
    static constexpr double MIN_POSITION_SEC = 10.0;
};
