#include "AddFromUrlDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QClipboard>

AddFromUrlDialog::AddFromUrlDialog(QWidget* parent, const QString& seedText)
    : QDialog(parent)
{
    setWindowTitle("Add from URL");
    setMinimumSize(560, 400);
    resize(620, 440);
    setStyleSheet(QStringLiteral(
        "AddFromUrlDialog { background: rgba(12,12,12,0.98); border: 1px solid rgba(255,255,255,0.08); border-radius: 12px; }"
    ));

    buildUI();

    if (!seedText.isEmpty())
        m_edit->setPlainText(seedText);
    else
        preloadFromClipboardIfApplicable();

    onTextChanged();
}

void AddFromUrlDialog::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(10);

    auto* title = new QLabel("Add torrents from URL");
    title->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold; color: #eee;"));
    root->addWidget(title);

    auto* hint = new QLabel(
        "Paste one magnet link per line. Only magnet: URIs are supported in this build.");
    hint->setStyleSheet(QStringLiteral("font-size: 11px; color: #888;"));
    hint->setWordWrap(true);
    root->addWidget(hint);

    m_edit = new QTextEdit;
    m_edit->setAcceptRichText(false);
    m_edit->setPlaceholderText(QStringLiteral(
        "magnet:?xt=urn:btih:...\nmagnet:?xt=urn:btih:...\n"));
    m_edit->setStyleSheet(QStringLiteral(
        "QTextEdit { background: rgba(0,0,0,0.35); border: 1px solid rgba(255,255,255,0.1); "
        "border-radius: 6px; color: #eee; padding: 6px; font-family: Consolas, monospace; font-size: 11px; }"
    ));
    connect(m_edit, &QTextEdit::textChanged, this, &AddFromUrlDialog::onTextChanged);
    root->addWidget(m_edit, 1);

    auto* optionsRow = new QHBoxLayout;

    auto* catLabel = new QLabel("Category");
    catLabel->setStyleSheet(QStringLiteral("color: #ccc; font-size: 12px;"));
    optionsRow->addWidget(catLabel);

    m_categoryCombo = new QComboBox;
    m_categoryCombo->addItem("Videos",     "videos");
    m_categoryCombo->addItem("Books",      "books");
    m_categoryCombo->addItem("Audiobooks", "audiobooks");
    m_categoryCombo->addItem("Comics",     "comics");
    m_categoryCombo->setCurrentIndex(0);
    optionsRow->addWidget(m_categoryCombo);

    m_startImmediate = new QCheckBox("Start each immediately");
    m_startImmediate->setChecked(true);
    m_startImmediate->setStyleSheet(QStringLiteral("color: #ccc; font-size: 12px;"));
    optionsRow->addWidget(m_startImmediate);

    optionsRow->addStretch();

    root->addLayout(optionsRow);

    m_status = new QLabel;
    m_status->setStyleSheet(QStringLiteral("font-size: 11px; color: #888;"));
    root->addWidget(m_status);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_acceptBtn = buttons->button(QDialogButtonBox::Ok);
    m_acceptBtn->setText("Add");
    m_acceptBtn->setCursor(Qt::PointingHandCursor);
    m_acceptBtn->setEnabled(false);
    buttons->button(QDialogButtonBox::Cancel)->setCursor(Qt::PointingHandCursor);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

void AddFromUrlDialog::preloadFromClipboardIfApplicable()
{
    const QString clip = QGuiApplication::clipboard()->text().trimmed();
    if (clip.isEmpty())
        return;
    if (clip.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive))
        m_edit->setPlainText(clip);
    // .torrent URL preload: detected but intentionally not auto-filled since
    // this build doesn't support the add-torrent-file path yet — leaving the
    // textedit empty keeps the user from thinking it will "just work."
}

void AddFromUrlDialog::onTextChanged()
{
    m_parsedMagnets.clear();

    int skipped = 0;
    const auto lines = m_edit->toPlainText().split('\n', Qt::SkipEmptyParts);
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive))
            m_parsedMagnets.append(line);
        else
            ++skipped;
    }

    if (m_parsedMagnets.isEmpty() && skipped == 0)
        m_status->setText(QString());
    else if (m_parsedMagnets.isEmpty())
        m_status->setText(QStringLiteral("%1 line(s) skipped (only magnet: links supported).").arg(skipped));
    else if (skipped == 0)
        m_status->setText(QStringLiteral("%1 magnet link(s) ready.").arg(m_parsedMagnets.size()));
    else
        m_status->setText(QStringLiteral("%1 magnet(s) ready, %2 line(s) skipped.")
                              .arg(m_parsedMagnets.size()).arg(skipped));

    m_acceptBtn->setEnabled(!m_parsedMagnets.isEmpty());
}

QStringList AddFromUrlDialog::magnets() const
{
    return m_parsedMagnets;
}

QString AddFromUrlDialog::category() const
{
    return m_categoryCombo->currentData().toString();
}

bool AddFromUrlDialog::startImmediately() const
{
    return m_startImmediate->isChecked();
}
