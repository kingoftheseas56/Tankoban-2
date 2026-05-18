#pragma once

#include <QWidget>

class QLabel;

namespace tankoban::ui::widgets {

class ComicsSeriesViewLoadingOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit ComicsSeriesViewLoadingOverlay(QWidget* parent = nullptr);
    ~ComicsSeriesViewLoadingOverlay() override;

    void setMessage(const QString& text);

protected:
    void paintEvent(QPaintEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;

private:
    QLabel* m_label = nullptr;
};

} // namespace tankoban::ui::widgets
