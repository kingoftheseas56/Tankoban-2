/****************************************************************************
** Meta object code from reading C++ file 'BooksPage.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/ui/pages/BooksPage.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>
#include <QtCore/QList>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'BooksPage.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN9BooksPageE_t {};
} // unnamed namespace

template <> constexpr inline auto BooksPage::qt_create_metaobjectdata<qt_meta_tag_ZN9BooksPageE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "BooksPage",
        "openBook",
        "",
        "filePath",
        "onBookSeriesFound",
        "BookSeriesInfo",
        "series",
        "onAudiobookFound",
        "AudiobookInfo",
        "audiobook",
        "onScanFinished",
        "QList<BookSeriesInfo>",
        "allBooks",
        "QList<AudiobookInfo>",
        "allAudiobooks",
        "onTileClicked",
        "seriesPath",
        "seriesName",
        "showGrid"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'openBook'
        QtMocHelpers::SignalData<void(const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Slot 'onBookSeriesFound'
        QtMocHelpers::SlotData<void(const BookSeriesInfo &)>(4, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 5, 6 },
        }}),
        // Slot 'onAudiobookFound'
        QtMocHelpers::SlotData<void(const AudiobookInfo &)>(7, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 8, 9 },
        }}),
        // Slot 'onScanFinished'
        QtMocHelpers::SlotData<void(const QList<BookSeriesInfo> &, const QList<AudiobookInfo> &)>(10, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 11, 12 }, { 0x80000000 | 13, 14 },
        }}),
        // Slot 'onTileClicked'
        QtMocHelpers::SlotData<void(const QString &, const QString &)>(15, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 16 }, { QMetaType::QString, 17 },
        }}),
        // Slot 'showGrid'
        QtMocHelpers::SlotData<void()>(18, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<BooksPage, qt_meta_tag_ZN9BooksPageE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject BooksPage::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9BooksPageE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9BooksPageE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN9BooksPageE_t>.metaTypes,
    nullptr
} };

void BooksPage::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<BooksPage *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->openBook((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->onBookSeriesFound((*reinterpret_cast<std::add_pointer_t<BookSeriesInfo>>(_a[1]))); break;
        case 2: _t->onAudiobookFound((*reinterpret_cast<std::add_pointer_t<AudiobookInfo>>(_a[1]))); break;
        case 3: _t->onScanFinished((*reinterpret_cast<std::add_pointer_t<QList<BookSeriesInfo>>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QList<AudiobookInfo>>>(_a[2]))); break;
        case 4: _t->onTileClicked((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 5: _t->showGrid(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (BooksPage::*)(const QString & )>(_a, &BooksPage::openBook, 0))
            return;
    }
}

const QMetaObject *BooksPage::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *BooksPage::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9BooksPageE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int BooksPage::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 6;
    }
    return _id;
}

// SIGNAL 0
void BooksPage::openBook(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}
QT_WARNING_POP
