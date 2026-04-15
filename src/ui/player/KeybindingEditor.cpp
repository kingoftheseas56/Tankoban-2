#include "ui/player/KeybindingEditor.h"

#include "ui/player/KeyBindings.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {
// Column indices — kept in one place so column insertion doesn't
// misalign the table body, header, and reset-handlers.
constexpr int COL_ACTION  = 0;
constexpr int COL_CURRENT = 1;
constexpr int COL_DEFAULT = 2;
constexpr int COL_RESET   = 3;
constexpr int COL_COUNT   = 4;

QString displayKey(const QKeySequence& seq)
{
    if (seq.isEmpty()) return QObject::tr("(unbound)");
    return seq.toString(QKeySequence::NativeText);
}
}

KeybindingEditor::KeybindingEditor(KeyBindings* bindings, QWidget* parent)
    : QDialog(parent)
    , m_bindings(bindings)
{
    setWindowTitle(tr("Keyboard Shortcuts"));
    setModal(true);
    resize(560, 520);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(10);

    m_hint = new QLabel(tr("Click a shortcut to change it. Press Esc to cancel."), this);
    m_hint->setStyleSheet("color: rgba(255,255,255,140); font-size: 12px;");
    lay->addWidget(m_hint);

    m_table = new QTableWidget(0, COL_COUNT, this);
    m_table->setHorizontalHeaderLabels({ tr("Action"), tr("Shortcut"), tr("Default"), {} });
    m_table->horizontalHeader()->setSectionResizeMode(COL_ACTION,  QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(COL_CURRENT, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(COL_DEFAULT, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(COL_RESET,   QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setFocusPolicy(Qt::NoFocus);  // dialog owns keyPressEvent for capture
    lay->addWidget(m_table, 1);

    // Clicking the Current column enters capture mode for that row.
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int col) {
        if (col == COL_CURRENT)
            beginCapture(row);
    });

    auto* buttons = new QDialogButtonBox(this);
    auto* resetAllBtn = buttons->addButton(tr("Reset All"), QDialogButtonBox::ResetRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(resetAllBtn, &QPushButton::clicked, this, &KeybindingEditor::resetAllBindings);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    lay->addWidget(buttons);

    rebuildTable();
}

void KeybindingEditor::rebuildTable()
{
    if (!m_bindings) return;

    const QStringList actions = m_bindings->allActions();
    m_table->setRowCount(actions.size());
    for (int i = 0; i < actions.size(); ++i) {
        const QString& action = actions.at(i);
        const QString label = KeyBindings::labelForAction(action);
        const QKeySequence cur = m_bindings->keyForAction(action);
        const QKeySequence def = KeyBindings::defaultKeyForAction(action);

        auto* actItem = new QTableWidgetItem(label);
        actItem->setData(Qt::UserRole, action);  // full action id stashed for reset-row lookup
        actItem->setToolTip(action);
        m_table->setItem(i, COL_ACTION, actItem);

        auto* curItem = new QTableWidgetItem(displayKey(cur));
        curItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(i, COL_CURRENT, curItem);

        auto* defItem = new QTableWidgetItem(displayKey(def));
        defItem->setTextAlignment(Qt::AlignCenter);
        defItem->setForeground(QColor(180, 180, 180));
        m_table->setItem(i, COL_DEFAULT, defItem);

        // Reset-row button. Held as a cell widget so per-row click is
        // naturally scoped to the row index at connect time.
        auto* resetBtn = new QPushButton(tr("Reset"));
        resetBtn->setFlat(true);
        resetBtn->setCursor(Qt::PointingHandCursor);
        connect(resetBtn, &QPushButton::clicked, this, [this, i]() { resetRow(i); });
        m_table->setCellWidget(i, COL_RESET, resetBtn);
    }
}

void KeybindingEditor::beginCapture(int row)
{
    m_captureRow = row;
    if (m_hint)
        m_hint->setText(tr("Press the new shortcut for \"%1\" (Esc to cancel)…")
                        .arg(m_table->item(row, COL_ACTION)->text()));
    // Highlight the capturing cell so the user sees the pending state.
    if (auto* item = m_table->item(row, COL_CURRENT))
        item->setText(tr("— press a key —"));
    setFocus(Qt::OtherFocusReason);
}

void KeybindingEditor::keyPressEvent(QKeyEvent* ev)
{
    if (m_captureRow < 0) { QDialog::keyPressEvent(ev); return; }

    const int key = ev->key();
    // Modifier-only presses don't make useful shortcuts — wait for the
    // real key that accompanies them. Same pattern as QMPlay2's
    // KeyBindingsDialog.
    if (key == Qt::Key_Shift || key == Qt::Key_Control
        || key == Qt::Key_Alt   || key == Qt::Key_Meta
        || key == Qt::Key_AltGr || key == Qt::Key_unknown) {
        ev->accept();
        return;
    }

    if (key == Qt::Key_Escape) {
        // Cancel — restore display text without changing the binding.
        const QString action = m_table->item(m_captureRow, COL_ACTION)->data(Qt::UserRole).toString();
        if (auto* item = m_table->item(m_captureRow, COL_CURRENT))
            item->setText(displayKey(m_bindings->keyForAction(action)));
        m_captureRow = -1;
        if (m_hint)
            m_hint->setText(tr("Click a shortcut to change it. Press Esc to cancel."));
        ev->accept();
        return;
    }

    // Build the QKeySequence from key + modifiers (Qt treats the combined
    // int as the first key in the sequence).
    const QKeySequence seq(key | static_cast<int>(ev->modifiers()));
    const int row = m_captureRow;
    m_captureRow = -1;
    if (m_hint)
        m_hint->setText(tr("Click a shortcut to change it. Press Esc to cancel."));
    applyCaptured(row, seq);
    ev->accept();
}

void KeybindingEditor::applyCaptured(int row, const QKeySequence& seq)
{
    const QString action = m_table->item(row, COL_ACTION)->data(Qt::UserRole).toString();
    if (action.isEmpty() || seq.isEmpty() || !m_bindings) { rebuildTable(); return; }

    // Duplicate detection — check if any OTHER action currently holds
    // this sequence. If yes, prompt the user: unbind the other, keep
    // both (creates an ambiguous mapping — first-match wins), or cancel.
    QString conflict;
    for (const QString& a : m_bindings->allActions()) {
        if (a == action) continue;
        if (m_bindings->keyForAction(a) == seq) { conflict = a; break; }
    }

    if (!conflict.isEmpty()) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Shortcut conflict"));
        box.setText(tr("\"%1\" is already assigned to \"%2\".\nUnbind \"%2\" and reassign?")
                    .arg(seq.toString(QKeySequence::NativeText),
                         KeyBindings::labelForAction(conflict)));
        QPushButton* unbindBtn = box.addButton(tr("Unbind && Reassign"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != unbindBtn) { rebuildTable(); return; }
        m_bindings->setBinding(conflict, QKeySequence());  // unbind
    }

    m_bindings->setBinding(action, seq);
    rebuildTable();
}

void KeybindingEditor::resetAllBindings()
{
    if (!m_bindings) return;
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Reset All Shortcuts"));
    box.setText(tr("Restore all keyboard shortcuts to their defaults?"));
    box.setStandardButtons(QMessageBox::Reset | QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Reset) return;
    m_bindings->resetDefaults();
    rebuildTable();
}

void KeybindingEditor::resetRow(int row)
{
    if (!m_bindings) return;
    const QString action = m_table->item(row, COL_ACTION)->data(Qt::UserRole).toString();
    if (action.isEmpty()) return;
    m_bindings->resetAction(action);
    rebuildTable();
}
