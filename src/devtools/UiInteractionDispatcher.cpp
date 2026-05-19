#include "devtools/UiInteractionDispatcher.h"

#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMetaEnum>
#include <QMetaObject>
#include <QModelIndex>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPoint>
#include <QRect>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTextEdit>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>

namespace {

constexpr int kListDefaultLimit  = 100;
constexpr int kWaitDefaultMs     = 5000;
constexpr int kWaitCapMs         = 30000;
constexpr int kWaitPollMs        = 50;

QObject* findByName(QWidget* root, const QString& name)
{
    if (!root || name.isEmpty()) return nullptr;
    if (root->objectName() == name) return root;
    return root->findChild<QObject*>(name, Qt::FindChildrenRecursively);
}

QString widgetText(QObject* obj)
{
    if (auto* b = qobject_cast<QAbstractButton*>(obj)) return b->text();
    if (auto* le = qobject_cast<QLineEdit*>(obj))      return le->text();
    if (auto* cb = qobject_cast<QComboBox*>(obj))      return cb->currentText();
    if (auto* te = qobject_cast<QTextEdit*>(obj))      return te->toPlainText();
    if (auto* pe = qobject_cast<QPlainTextEdit*>(obj)) return pe->toPlainText();
    // Generic property fallback — covers QLabel + anything else exposing text().
    const QVariant v = obj->property("text");
    return v.isValid() ? v.toString() : QString();
}

QJsonObject geometryObject(QWidget* w)
{
    if (!w) return {};
    const QRect r = w->geometry();
    return QJsonObject{
        {"x",      r.x()},
        {"y",      r.y()},
        {"width",  r.width()},
        {"height", r.height()},
    };
}

QJsonObject snapshotObject(QObject* obj)
{
    QJsonObject o;
    o["objectName"] = obj ? obj->objectName() : QString();
    o["className"]  = obj && obj->metaObject() ? QString::fromLatin1(obj->metaObject()->className()) : QString();
    auto* w = qobject_cast<QWidget*>(obj);
    o["isWidget"]   = (w != nullptr);
    o["visible"]    = w ? w->isVisible() : false;
    o["enabled"]    = w ? w->isEnabled() : false;
    o["geometry"]   = geometryObject(w);
    o["text"]       = widgetText(obj);
    return o;
}

bool globToRegex(const QString& glob, QRegularExpression* out)
{
    // Translate a simple glob (* and ?) to a full-match regex.
    QString pat;
    pat.reserve(glob.size() * 2);
    pat.append('^');
    for (QChar c : glob) {
        if (c == '*')      pat.append(".*");
        else if (c == '?') pat.append('.');
        else if (QString("\\^$.|+(){}[]").contains(c)) {
            pat.append('\\');
            pat.append(c);
        } else {
            pat.append(c);
        }
    }
    pat.append('$');
    QRegularExpression rx(pat);
    if (!rx.isValid()) return false;
    *out = rx;
    return true;
}

// Parse a Qt::Key code from a string. Accepts the canonical forms agents
// produce: "Qt.Key_Down", "Qt::Key_Down", "Key_Down", "Down", or a numeric
// string (the int value of the enum). Returns Qt::Key_unknown on failure.
Qt::Key parseQtKey(const QString& raw, bool* ok)
{
    if (ok) *ok = false;
    QString s = raw.trimmed();
    if (s.isEmpty()) return Qt::Key_unknown;

    bool num = false;
    const int direct = s.toInt(&num);
    if (num) {
        if (ok) *ok = true;
        return static_cast<Qt::Key>(direct);
    }

    if (s.startsWith(QStringLiteral("Qt.")))  s.remove(0, 3);
    if (s.startsWith(QStringLiteral("Qt::"))) s.remove(0, 4);
    if (!s.startsWith(QStringLiteral("Key_"))) s.prepend(QStringLiteral("Key_"));

    const QMetaEnum en = QMetaEnum::fromType<Qt::Key>();
    if (!en.isValid()) return Qt::Key_unknown;
    bool enumOk = false;
    const int val = en.keyToValue(s.toUtf8().constData(), &enumOk);
    if (!enumOk) return Qt::Key_unknown;
    if (ok) *ok = true;
    return static_cast<Qt::Key>(val);
}

QPoint widgetCenter(QWidget* w)
{
    if (!w) return QPoint();
    return QPoint(w->width() / 2, w->height() / 2);
}

QJsonObject makeError(const char* code, const QString& msg)
{
    return QJsonObject{
        {"type",    QStringLiteral("error")},
        {"code",    QString::fromLatin1(code)},
        {"message", msg},
    };
}

void mergeReply(QJsonObject& reply, const QJsonObject& src)
{
    for (auto it = src.constBegin(); it != src.constEnd(); ++it) {
        // Preserve dispatcher framing keys ("type","seq") unless src is an
        // error envelope (then overwrite "type" to propagate).
        if (it.key() == QLatin1String("seq")) continue;
        reply.insert(it.key(), it.value());
    }
}

void listVisitor(QObject* node,
                 const QRegularExpression& rx,
                 int limit,
                 QJsonArray& out)
{
    if (!node) return;
    if (out.size() >= limit) return;
    const QString name = node->objectName();
    if (!name.isEmpty() && rx.match(name).hasMatch())
        out.append(snapshotObject(node));
    const auto kids = node->children();
    for (QObject* c : kids) {
        if (out.size() >= limit) return;
        listVisitor(c, rx, limit, out);
    }
}

QJsonArray walkLayerChain(QObject* leaf)
{
    QJsonArray chain;
    QObject* cur = leaf;
    QSet<QObject*> seen;
    while (cur && !seen.contains(cur)) {
        seen.insert(cur);
        QJsonObject o;
        o["objectName"] = cur->objectName();
        o["className"]  = cur->metaObject()
                          ? QString::fromLatin1(cur->metaObject()->className())
                          : QString();
        chain.append(o);
        cur = cur->parent();
    }
    return chain;
}

}  // namespace

UiInteractionDispatcher::UiInteractionDispatcher(QWidget* root, QObject* parent)
    : QObject(parent), m_root(root)
{
}

QStringList UiInteractionDispatcher::commandList()
{
    return {
        // Read-only — TANKOBAN_DEV_UI_SIM not required.
        "ui_query_widget",
        "ui_query_focus",
        "ui_active_layer",
        "ui_list_widgets",
        "ui_dry_run",
        // Write-capable — require TANKOBAN_DEV_UI_SIM=1.
        "ui_click",
        "ui_keypress",
        "ui_text_input",
        "ui_simulate_scroll",
        "ui_simulate_mouse",
        "ui_wait_for",
        "ui_set_checkbox",
        "ui_set_combo",
        "ui_select_table_row",
    };
}

bool UiInteractionDispatcher::isWriteCapable(const QString& cmd)
{
    static const QSet<QString> kWrite{
        "ui_click",
        "ui_keypress",
        "ui_text_input",
        "ui_simulate_scroll",
        "ui_simulate_mouse",
        "ui_wait_for",
        "ui_set_checkbox",
        "ui_set_combo",
        "ui_select_table_row",
    };
    return kWrite.contains(cmd);
}

bool UiInteractionDispatcher::dispatch(const QString& cmd,
                                      const QJsonObject& payload,
                                      QJsonObject& reply)
{
    if (!commandList().contains(cmd))
        return false;
    if (!m_root) {
        mergeReply(reply, makeError("INTERNAL", QStringLiteral("UiInteractionDispatcher root widget is null")));
        return true;
    }

    // ── Read-only ───────────────────────────────────────────────────────────

    if (cmd == QLatin1String("ui_query_widget")) {
        const QString name = payload.value("objectName").toString();
        if (name.isEmpty()) {
            mergeReply(reply, makeError("BAD_REQUEST", QStringLiteral("payload.objectName required")));
            return true;
        }
        QObject* obj = findByName(m_root, name);
        if (!obj) {
            mergeReply(reply, makeError("WIDGET_NOT_FOUND",
                QStringLiteral("no QObject with objectName '%1'").arg(name)));
            return true;
        }
        mergeReply(reply, QJsonObject{{"widget", snapshotObject(obj)}});
        return true;
    }

    if (cmd == QLatin1String("ui_query_focus")) {
        QWidget* fw = QApplication::focusWidget();
        QJsonObject info;
        info["objectName"] = fw ? fw->objectName() : QString();
        info["className"]  = (fw && fw->metaObject())
            ? QString::fromLatin1(fw->metaObject()->className())
            : QString();
        info["hasFocus"]   = (fw != nullptr);
        if (fw) info["geometry"] = geometryObject(fw);
        mergeReply(reply, QJsonObject{{"focus", info}});
        return true;
    }

    if (cmd == QLatin1String("ui_active_layer")) {
        // Build a structural layer snapshot the agents can read without
        // pixel-snooping: focused widget + its ancestor chain + visible
        // top-level widgets. Lightweight read-only.
        QWidget* fw = QApplication::focusWidget();
        QJsonObject layer;
        layer["focusObjectName"] = fw ? fw->objectName() : QString();
        layer["focusClassName"]  = (fw && fw->metaObject())
            ? QString::fromLatin1(fw->metaObject()->className())
            : QString();
        layer["focusChain"]      = walkLayerChain(fw);

        QJsonArray topLevels;
        const auto tls = QApplication::topLevelWidgets();
        for (QWidget* w : tls) {
            if (!w->isVisible()) continue;
            QJsonObject o;
            o["objectName"] = w->objectName();
            o["className"]  = w->metaObject() ? QString::fromLatin1(w->metaObject()->className()) : QString();
            o["geometry"]   = geometryObject(w);
            topLevels.append(o);
        }
        layer["visibleTopLevels"] = topLevels;
        mergeReply(reply, QJsonObject{{"layer", layer}});
        return true;
    }

    if (cmd == QLatin1String("ui_list_widgets")) {
        const QString filter = payload.value("filter").toString(QStringLiteral("*"));
        const int limit = qBound(1, payload.value("limit").toInt(kListDefaultLimit), 5000);
        QRegularExpression rx;
        if (!globToRegex(filter, &rx)) {
            mergeReply(reply, makeError("BAD_REQUEST",
                QStringLiteral("filter glob '%1' did not compile").arg(filter)));
            return true;
        }
        QJsonArray results;
        listVisitor(m_root, rx, limit, results);
        mergeReply(reply, QJsonObject{
            {"filter",  filter},
            {"limit",   limit},
            {"count",   results.size()},
            {"widgets", results},
        });
        return true;
    }

    if (cmd == QLatin1String("ui_dry_run")) {
        const QString innerCmd = payload.value("innerCmd").toString();
        const QJsonObject innerPayload = payload.value("innerPayload").toObject();
        if (!commandList().contains(innerCmd)) {
            mergeReply(reply, makeError("BAD_REQUEST",
                QStringLiteral("innerCmd '%1' is not a ui_* command").arg(innerCmd)));
            return true;
        }
        if (innerCmd == QLatin1String("ui_dry_run")) {
            mergeReply(reply, makeError("BAD_REQUEST",
                QStringLiteral("ui_dry_run cannot wrap itself")));
            return true;
        }
        const QString name = innerPayload.value("objectName").toString();
        QObject* obj = name.isEmpty() ? nullptr : findByName(m_root, name);
        QJsonObject out;
        out["innerCmd"] = innerCmd;
        out["fired"]    = false;
        out["resolved"] = (obj != nullptr);
        out["target"]   = obj ? snapshotObject(obj) : QJsonObject{};
        out["plannedEvent"] = innerCmd;  // human-readable label
        mergeReply(reply, out);
        return true;
    }

    // ── Write-capable ───────────────────────────────────────────────────────
    // (TANKOBAN_DEV_UI_SIM gate enforced by MainWindow before forwarding.)

    if (cmd == QLatin1String("ui_click")) {
        const QString name = payload.value("objectName").toString();
        QObject* obj = findByName(m_root, name);
        if (!obj) {
            mergeReply(reply, makeError("WIDGET_NOT_FOUND",
                QStringLiteral("no QObject with objectName '%1'").arg(name)));
            return true;
        }
        QString path;
        if (auto* btn = qobject_cast<QAbstractButton*>(obj)) {
            btn->animateClick();
            path = QStringLiteral("animateClick");
        } else {
            bool invoked = QMetaObject::invokeMethod(obj, "click", Qt::DirectConnection);
            if (invoked) {
                path = QStringLiteral("invokeMethod(click)");
            } else if (auto* w = qobject_cast<QWidget*>(obj)) {
                const QPoint c = widgetCenter(w);
                auto* press = new QMouseEvent(QEvent::MouseButtonPress, c, w->mapToGlobal(c),
                                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                auto* rel   = new QMouseEvent(QEvent::MouseButtonRelease, c, w->mapToGlobal(c),
                                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::postEvent(w, press);
                QApplication::postEvent(w, rel);
                path = QStringLiteral("postEvent(MouseButtonPress+Release@center)");
            } else {
                mergeReply(reply, makeError("UNSUPPORTED",
                    QStringLiteral("object '%1' is not a QAbstractButton and has no click() slot").arg(name)));
                return true;
            }
        }
        mergeReply(reply, QJsonObject{
            {"fired", true},
            {"path",  path},
            {"target", snapshotObject(obj)},
        });
        return true;
    }

    if (cmd == QLatin1String("ui_keypress")) {
        const QString name = payload.value("objectName").toString();
        const QString keyRaw = payload.value("key").toString();
        QObject* obj = findByName(m_root, name);
        if (!obj) {
            mergeReply(reply, makeError("WIDGET_NOT_FOUND",
                QStringLiteral("no QObject with objectName '%1'").arg(name)));
            return true;
        }
        bool keyOk = false;
        const Qt::Key key = parseQtKey(keyRaw, &keyOk);
        if (!keyOk) {
            mergeReply(reply, makeError("BAD_REQUEST",
                QStringLiteral("key '%1' did not resolve to Qt::Key (try 'Qt.Key_Down' or a numeric value)").arg(keyRaw)));
            return true;
        }
        auto* w = qobject_cast<QWidget*>(obj);
        if (!w) {
            mergeReply(reply, makeError("UNSUPPORTED",
                QStringLiteral("object '%1' is not a QWidget — cannot receive QKeyEvent").arg(name)));
            return true;
        }
        auto* press = new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier);
        auto* rel   = new QKeyEvent(QEvent::KeyRelease, key, Qt::NoModifier);
        QApplication::postEvent(w, press);
        QApplication::postEvent(w, rel);
        mergeReply(reply, QJsonObject{
            {"fired", true},
            {"key",   keyRaw},
            {"keyValue", static_cast<int>(key)},
            {"target", snapshotObject(obj)},
        });
        return true;
    }

    if (cmd == QLatin1String("ui_text_input")) {
        const QString name = payload.value("objectName").toString();
        const QString text = payload.value("text").toString();
        QObject* obj = findByName(m_root, name);
        if (!obj) {
            mergeReply(reply, makeError("WIDGET_NOT_FOUND",
                QStringLiteral("no QObject with objectName '%1'").arg(name)));
            return true;
        }
        QString path;
        if (auto* le = qobject_cast<QLineEdit*>(obj)) {
            le->setText(text);
            path = QStringLiteral("QLineEdit::setText");
        } else if (auto* te = qobject_cast<QTextEdit*>(obj)) {
            te->setPlainText(text);
            path = QStringLiteral("QTextEdit::setPlainText");
        } else if (auto* pe = qobject_cast<QPlainTextEdit*>(obj)) {
            pe->setPlainText(text);
            path = QStringLiteral("QPlainTextEdit::setPlainText");
        } else if (auto* cb = qobject_cast<QComboBox*>(obj)) {
            if (cb->isEditable()) {
                cb->setEditText(text);
                path = QStringLiteral("QComboBox::setEditText");
            } else {
                mergeReply(reply, makeError("UNSUPPORTED",
                    QStringLiteral("QComboBox '%1' is not editable — use ui_set_combo").arg(name)));
                return true;
            }
        } else {
            bool invoked = QMetaObject::invokeMethod(
                obj, "setText", Qt::DirectConnection, Q_ARG(QString, text));
            if (!invoked) {
                mergeReply(reply, makeError("UNSUPPORTED",
                    QStringLiteral("object '%1' has no setText slot and is not a known text widget").arg(name)));
                return true;
            }
            path = QStringLiteral("invokeMethod(setText)");
        }
        mergeReply(reply, QJsonObject{
            {"fired", true},
            {"path",  path},
            {"text",  text},
            {"target", snapshotObject(obj)},
        });
        return true;
    }

    if (cmd == QLatin1String("ui_simulate_scroll")) {
        const QString name = payload.value("objectName").toString();
        const int delta = payload.value("delta").toInt();
        QObject* obj = findByName(m_root, name);
        if (!obj) {
            mergeReply(reply, makeError("WIDGET_NOT_FOUND",
                QStringLiteral("no QObject with objectName '%1'").arg(name)));
            return true;
        }
        auto* w = qobject_cast<QWidget*>(obj);
        if (!w) {
            mergeReply(reply, makeError("UNSUPPORTED",
                QStringLiteral("object '%1' is not a QWidget — cannot receive QWheelEvent").arg(name)));
            return true;
        }
        const QPoint c = widgetCenter(w);
        // angleDelta y > 0 means scroll up (towards user); negative scrolls
        // content down. Match the QWheelEvent contract; agents pass the
        // sign they want.
        auto* wheel = new QWheelEvent(c, w->mapToGlobal(c),
                                       QPoint(0, delta),
                                       QPoint(0, delta),
                                       Qt::NoButton,
                                       Qt::NoModifier,
                                       Qt::NoScrollPhase,
                                       false);
        QApplication::postEvent(w, wheel);
        mergeReply(reply, QJsonObject{
            {"fired", true},
            {"delta", delta},
            {"target", snapshotObject(obj)},
        });
        return true;
    }

    if (cmd == QLatin1String("ui_simulate_mouse")) {
        const QString name = payload.value("objectName").toString();
        const QString evType = payload.value("eventType").toString();
        QObject* obj = findByName(m_root, name);
        if (!obj) {
            mergeReply(reply, makeError("WIDGET_NOT_FOUND",
                QStringLiteral("no QObject with objectName '%1'").arg(name)));
            return true;
        }
        auto* w = qobject_cast<QWidget*>(obj);
        if (!w) {
            mergeReply(reply, makeError("UNSUPPORTED",
                QStringLiteral("object '%1' is not a QWidget").arg(name)));
            return true;
        }
        QEvent::Type qet;
        if      (evType == QLatin1String("press"))        qet = QEvent::MouseButtonPress;
        else if (evType == QLatin1String("release"))      qet = QEvent::MouseButtonRelease;
        else if (evType == QLatin1String("move"))         qet = QEvent::MouseMove;
        else if (evType == QLatin1String("double-click")) qet = QEvent::MouseButtonDblClick;
        else {
            mergeReply(reply, makeError("BAD_REQUEST",
                QStringLiteral("eventType '%1' not in [press,release,move,double-click]").arg(evType)));
            return true;
        }
        const QPoint center = widgetCenter(w);
        QPoint p = center;
        if (payload.contains(QStringLiteral("x")))
            p.setX(payload.value("x").toInt(center.x()));
        if (payload.contains(QStringLiteral("y")))
            p.setY(payload.value("y").toInt(center.y()));
        const Qt::MouseButton button = (qet == QEvent::MouseMove) ? Qt::NoButton : Qt::LeftButton;
        auto* ev = new QMouseEvent(qet, p, w->mapToGlobal(p),
                                    button, button, Qt::NoModifier);
        QApplication::postEvent(w, ev);
        mergeReply(reply, QJsonObject{
            {"fired",     true},
            {"eventType", evType},
            {"point",     QJsonObject{{"x", p.x()}, {"y", p.y()}}},
            {"target",    snapshotObject(obj)},
        });
        return true;
    }

    if (cmd == QLatin1String("ui_set_checkbox")) {
        const QString name = payload.value("objectName").toString();
        const bool checked = payload.value("checked").toBool();
        QObject* obj = findByName(m_root, name);
        if (!obj) {
            mergeReply(reply, makeError("WIDGET_NOT_FOUND",
                QStringLiteral("no QObject with objectName '%1'").arg(name)));
            return true;
        }
        auto* btn = qobject_cast<QAbstractButton*>(obj);
        if (!btn || !btn->isCheckable()) {
            mergeReply(reply, makeError("UNSUPPORTED",
                QStringLiteral("object '%1' is not a checkable QAbstractButton").arg(name)));
            return true;
        }
        btn->setChecked(checked);
        mergeReply(reply, QJsonObject{
            {"fired",   true},
            {"checked", btn->isChecked()},
            {"target",  snapshotObject(obj)},
        });
        return true;
    }

    if (cmd == QLatin1String("ui_set_combo")) {
        const QString name = payload.value("objectName").toString();
        const QString value = payload.value("value").toString();
        QObject* obj = findByName(m_root, name);
        if (!obj) {
            mergeReply(reply, makeError("WIDGET_NOT_FOUND",
                QStringLiteral("no QObject with objectName '%1'").arg(name)));
            return true;
        }
        auto* cb = qobject_cast<QComboBox*>(obj);
        if (!cb) {
            mergeReply(reply, makeError("UNSUPPORTED",
                QStringLiteral("object '%1' is not a QComboBox").arg(name)));
            return true;
        }
        // Try index-by-text first; fall back to setCurrentText (works for
        // editable combos and best-effort for non-editable when the text
        // matches a known entry).
        const int idx = cb->findText(value);
        QString path;
        if (idx >= 0) {
            cb->setCurrentIndex(idx);
            path = QStringLiteral("setCurrentIndex(findText)");
        } else {
            cb->setCurrentText(value);
            path = QStringLiteral("setCurrentText");
        }
        mergeReply(reply, QJsonObject{
            {"fired",        true},
            {"path",         path},
            {"requested",    value},
            {"currentText",  cb->currentText()},
            {"currentIndex", cb->currentIndex()},
            {"target",       snapshotObject(obj)},
        });
        return true;
    }

    if (cmd == QLatin1String("ui_select_table_row")) {
        const QString name = payload.value("objectName").toString();
        const int row = payload.value("row").toInt(-1);
        QObject* obj = findByName(m_root, name);
        if (!obj) {
            mergeReply(reply, makeError("WIDGET_NOT_FOUND",
                QStringLiteral("no QObject with objectName '%1'").arg(name)));
            return true;
        }
        auto* view = qobject_cast<QAbstractItemView*>(obj);
        if (!view) {
            mergeReply(reply, makeError("UNSUPPORTED",
                QStringLiteral("object '%1' is not a QAbstractItemView").arg(name)));
            return true;
        }
        if (row < 0) {
            mergeReply(reply, makeError("BAD_REQUEST",
                QStringLiteral("row '%1' must be >= 0").arg(row)));
            return true;
        }
        auto* model = view->model();
        if (!model) {
            mergeReply(reply, makeError("INTERNAL",
                QStringLiteral("QAbstractItemView '%1' has no model").arg(name)));
            return true;
        }
        if (row >= model->rowCount()) {
            mergeReply(reply, makeError("OUT_OF_RANGE",
                QStringLiteral("row %1 exceeds model rowCount %2").arg(row).arg(model->rowCount())));
            return true;
        }
        const QModelIndex idx = model->index(row, 0);
        view->setCurrentIndex(idx);
        view->selectionModel()->select(idx,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        view->scrollTo(idx);
        mergeReply(reply, QJsonObject{
            {"fired",    true},
            {"row",      row},
            {"rowCount", model->rowCount()},
            {"target",   snapshotObject(obj)},
        });
        return true;
    }

    if (cmd == QLatin1String("ui_wait_for")) {
        const QString condition = payload.value("condition").toString();
        const int timeoutMs = qBound(0, payload.value("timeoutMs").toInt(kWaitDefaultMs), kWaitCapMs);
        if (condition.isEmpty()) {
            mergeReply(reply, makeError("BAD_REQUEST",
                QStringLiteral("payload.condition required ('<name>' or '<name>:visible'/':enabled'/':text-matches:<regex>')")));
            return true;
        }
        // Parse condition: <name>[:<predicate>[:<arg>]]
        QString objectName = condition;
        QString predicate  = QStringLiteral("visible");
        QString predArg;
        const int firstColon = condition.indexOf(':');
        if (firstColon >= 0) {
            objectName = condition.left(firstColon);
            const QString rest = condition.mid(firstColon + 1);
            const int second = rest.indexOf(':');
            if (second >= 0) {
                predicate = rest.left(second);
                predArg   = rest.mid(second + 1);
            } else {
                predicate = rest;
            }
        }
        QRegularExpression textRx;
        if (predicate == QLatin1String("text-matches")) {
            if (predArg.isEmpty()) {
                mergeReply(reply, makeError("BAD_REQUEST",
                    QStringLiteral("text-matches predicate requires a regex argument after second colon")));
                return true;
            }
            textRx = QRegularExpression(predArg);
            if (!textRx.isValid()) {
                mergeReply(reply, makeError("BAD_REQUEST",
                    QStringLiteral("text-matches regex '%1' did not compile").arg(predArg)));
                return true;
            }
        } else if (predicate != QLatin1String("visible") && predicate != QLatin1String("enabled")) {
            mergeReply(reply, makeError("BAD_REQUEST",
                QStringLiteral("predicate '%1' not in [visible,enabled,text-matches]").arg(predicate)));
            return true;
        }

        auto checkOnce = [&]() -> bool {
            QObject* obj = findByName(m_root, objectName);
            if (!obj) return false;
            auto* w = qobject_cast<QWidget*>(obj);
            if (predicate == QLatin1String("visible"))      return w && w->isVisible();
            if (predicate == QLatin1String("enabled"))      return w && w->isEnabled();
            if (predicate == QLatin1String("text-matches")) {
                return textRx.match(widgetText(obj)).hasMatch();
            }
            return false;
        };

        QElapsedTimer timer;
        timer.start();
        QEventLoop loop;
        QTimer poll;
        poll.setInterval(kWaitPollMs);
        bool met = false;
        QObject::connect(&poll, &QTimer::timeout, [&]() {
            if (checkOnce()) {
                met = true;
                loop.quit();
            } else if (timer.elapsed() >= timeoutMs) {
                loop.quit();
            }
        });
        if (checkOnce()) {
            met = true;
        } else {
            poll.start();
            loop.exec();
            poll.stop();
        }

        QObject* obj = findByName(m_root, objectName);
        QJsonObject out{
            {"met",        met},
            {"elapsedMs",  static_cast<int>(timer.elapsed())},
            {"timeoutMs",  timeoutMs},
            {"condition",  condition},
            {"resolved",   obj != nullptr},
            {"target",     obj ? snapshotObject(obj) : QJsonObject{}},
        };
        if (!met) {
            // Surface as error so callers can branch on type without sniffing
            // the "met" flag.
            mergeReply(reply, makeError("TIMEOUT",
                QStringLiteral("condition '%1' not met within %2ms").arg(condition).arg(timeoutMs)));
            for (auto it = out.constBegin(); it != out.constEnd(); ++it)
                reply.insert(it.key(), it.value());
        } else {
            mergeReply(reply, out);
        }
        return true;
    }

    // Should be unreachable — commandList() membership is enforced above.
    mergeReply(reply, makeError("INTERNAL",
        QStringLiteral("ui_* command '%1' recognised but not implemented").arg(cmd)));
    return true;
}
