#include "ui/player/OpenUrlDialog.h"

#include "ui/player/PlayerUtils.h"

#include <QClipboard>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

OpenUrlDialog::OpenUrlDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Open Stream URL"));
    setModal(true);
    setFixedSize(450, 120);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    auto* label = new QLabel(tr("Enter a stream URL (http, https, rtsp, rtmp):"), this);
    layout->addWidget(label);

    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText(QStringLiteral("https://example.com/stream.m3u8"));
    layout->addWidget(m_edit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Open"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        // Reject on accept if the text is empty or obviously malformed.
        // Sidecar surfaces protocol-level errors downstream for anything
        // that makes it past this gate.
        if (!player_utils::looksLikeUrl(m_edit->text())) {
            m_edit->selectAll();
            m_edit->setFocus();
            return;  // stays open; user can correct or cancel
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    populateFromClipboard();
    m_edit->setFocus();
    m_edit->selectAll();
}

void OpenUrlDialog::populateFromClipboard()
{
    const QClipboard* cb = QGuiApplication::clipboard();
    if (!cb) return;
    const QString text = cb->text().trimmed();
    if (player_utils::looksLikeUrl(text))
        m_edit->setText(text);
}

QString OpenUrlDialog::url() const
{
    return m_edit ? m_edit->text().trimmed() : QString();
}
