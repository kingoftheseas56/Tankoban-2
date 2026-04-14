#include "FadingStackedWidget.h"

#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>

FadingStackedWidget::FadingStackedWidget(QWidget* parent)
    : QStackedWidget(parent)
{
}

void FadingStackedWidget::setCurrentIndexAnimated(int index)
{
    if (index == currentIndex() || index < 0 || index >= count())
        return;

    QWidget* outgoing = currentWidget();
    QWidget* incoming = widget(index);

    // Fade out the outgoing page
    auto* outEffect = new QGraphicsOpacityEffect(outgoing);
    outgoing->setGraphicsEffect(outEffect);

    auto* fadeOut = new QPropertyAnimation(outEffect, "opacity", this);
    fadeOut->setDuration(m_duration / 2);
    fadeOut->setStartValue(1.0);
    fadeOut->setEndValue(0.0);

    connect(fadeOut, &QPropertyAnimation::finished, this, [this, index, outgoing, incoming]() {
        // Switch page at midpoint
        setCurrentIndex(index);

        // Remove effect from outgoing
        outgoing->setGraphicsEffect(nullptr);

        // Fade in the incoming page
        auto* inEffect = new QGraphicsOpacityEffect(incoming);
        incoming->setGraphicsEffect(inEffect);

        auto* fadeIn = new QPropertyAnimation(inEffect, "opacity", this);
        fadeIn->setDuration(m_duration / 2);
        fadeIn->setStartValue(0.0);
        fadeIn->setEndValue(1.0);

        connect(fadeIn, &QPropertyAnimation::finished, this, [incoming]() {
            incoming->setGraphicsEffect(nullptr);
        });

        fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
    });

    fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
}
