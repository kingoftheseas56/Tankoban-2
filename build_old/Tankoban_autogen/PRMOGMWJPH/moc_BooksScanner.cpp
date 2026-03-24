/****************************************************************************
** Meta object code from reading C++ file 'BooksScanner.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/core/BooksScanner.h"
#include <QtCore/qmetatype.h>
#include <QtCore/QList>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'BooksScanner.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN12BooksScannerE_t {};
} // unnamed namespace

template <> constexpr inline auto BooksScanner::qt_create_metaobjectdata<qt_meta_tag_ZN12BooksScannerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "BooksScanner",
        "bookSeriesFound",
        "",
        "BookSeriesInfo",
        "series",
        "audiobookFound",
        "AudiobookInfo",
        "audiobook",
        "scanFinished",
        "QList<BookSeriesInfo>",
        "allBooks",
        "QList<AudiobookInfo>",
        "allAudiobooks",
        "scan",
        "bookRoots",
        "audiobookRoots"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'bookSeriesFound'
        QtMocHelpers::SignalData<void(const BookSeriesInfo &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
        // Signal 'audiobookFound'
        QtMocHelpers::SignalData<void(const AudiobookInfo &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 6, 7 },
        }}),
        // Signal 'scanFinished'
        QtMocHelpers::SignalData<void(const QList<BookSeriesInfo> &, const QList<AudiobookInfo> &)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 9, 10 }, { 0x80000000 | 11, 12 },
        }}),
        // Slot 'scan'
        QtMocHelpers::SlotData<void(const QStringList &, const QStringList &)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QStringList, 14 }, { QMetaType::QStringList, 15 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<BooksScanner, qt_meta_tag_ZN12BooksScannerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject BooksScanner::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12BooksScannerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12BooksScannerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN12BooksScannerE_t>.metaTypes,
    nullptr
} };

void BooksScanner::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<BooksScanner *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->bookSeriesFound((*reinterpret_cast<std::add_pointer_t<BookSeriesInfo>>(_a[1]))); break;
        case 1: _t->audiobookFound((*reinterpret_cast<std::add_pointer_t<AudiobookInfo>>(_a[1]))); break;
        case 2: _t->scanFinished((*reinterpret_cast<std::add_pointer_t<QList<BookSeriesInfo>>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QList<AudiobookInfo>>>(_a[2]))); break;
        case 3: _t->scan((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[2]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 0:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< BookSeriesInfo >(); break;
            }
            break;
        case 1:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< AudiobookInfo >(); break;
            }
            break;
        case 2:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<AudiobookInfo> >(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<BookSeriesInfo> >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (BooksScanner::*)(const BookSeriesInfo & )>(_a, &BooksScanner::bookSeriesFound, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (BooksScanner::*)(const AudiobookInfo & )>(_a, &BooksScanner::audiobookFound, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (BooksScanner::*)(const QList<BookSeriesInfo> & , const QList<AudiobookInfo> & )>(_a, &BooksScanner::scanFinished, 2))
            return;
    }
}

const QMetaObject *BooksScanner::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *BooksScanner::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12BooksScannerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int BooksScanner::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 4)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 4)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    }
    return _id;
}

// SIGNAL 0
void BooksScanner::bookSeriesFound(const BookSeriesInfo & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void BooksScanner::audiobookFound(const AudiobookInfo & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void BooksScanner::scanFinished(const QList<BookSeriesInfo> & _t1, const QList<AudiobookInfo> & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1, _t2);
}
QT_WARNING_POP
