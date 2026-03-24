/****************************************************************************
** Meta object code from reading C++ file 'FfmpegDecoder.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/ui/player/FfmpegDecoder.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'FfmpegDecoder.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN13FfmpegDecoderE_t {};
} // unnamed namespace

template <> constexpr inline auto FfmpegDecoder::qt_create_metaobjectdata<qt_meta_tag_ZN13FfmpegDecoderE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "FfmpegDecoder",
        "frameReady",
        "",
        "QImage",
        "frame",
        "ptsMs",
        "positionChanged",
        "playbackFinished",
        "errorOccurred",
        "msg",
        "play",
        "pause",
        "togglePause",
        "stop",
        "seek",
        "ms"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'frameReady'
        QtMocHelpers::SignalData<void(const QImage &, qint64)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 }, { QMetaType::LongLong, 5 },
        }}),
        // Signal 'positionChanged'
        QtMocHelpers::SignalData<void(qint64)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::LongLong, 5 },
        }}),
        // Signal 'playbackFinished'
        QtMocHelpers::SignalData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'errorOccurred'
        QtMocHelpers::SignalData<void(const QString &)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 },
        }}),
        // Slot 'play'
        QtMocHelpers::SlotData<void()>(10, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'pause'
        QtMocHelpers::SlotData<void()>(11, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'togglePause'
        QtMocHelpers::SlotData<void()>(12, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'stop'
        QtMocHelpers::SlotData<void()>(13, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'seek'
        QtMocHelpers::SlotData<void(qint64)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::LongLong, 15 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<FfmpegDecoder, qt_meta_tag_ZN13FfmpegDecoderE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject FfmpegDecoder::staticMetaObject = { {
    QMetaObject::SuperData::link<QThread::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13FfmpegDecoderE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13FfmpegDecoderE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN13FfmpegDecoderE_t>.metaTypes,
    nullptr
} };

void FfmpegDecoder::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<FfmpegDecoder *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->frameReady((*reinterpret_cast<std::add_pointer_t<QImage>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2]))); break;
        case 1: _t->positionChanged((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1]))); break;
        case 2: _t->playbackFinished(); break;
        case 3: _t->errorOccurred((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: _t->play(); break;
        case 5: _t->pause(); break;
        case 6: _t->togglePause(); break;
        case 7: _t->stop(); break;
        case 8: _t->seek((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (FfmpegDecoder::*)(const QImage & , qint64 )>(_a, &FfmpegDecoder::frameReady, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (FfmpegDecoder::*)(qint64 )>(_a, &FfmpegDecoder::positionChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (FfmpegDecoder::*)()>(_a, &FfmpegDecoder::playbackFinished, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (FfmpegDecoder::*)(const QString & )>(_a, &FfmpegDecoder::errorOccurred, 3))
            return;
    }
}

const QMetaObject *FfmpegDecoder::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *FfmpegDecoder::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13FfmpegDecoderE_t>.strings))
        return static_cast<void*>(this);
    return QThread::qt_metacast(_clname);
}

int FfmpegDecoder::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QThread::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 9)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 9;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 9)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 9;
    }
    return _id;
}

// SIGNAL 0
void FfmpegDecoder::frameReady(const QImage & _t1, qint64 _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}

// SIGNAL 1
void FfmpegDecoder::positionChanged(qint64 _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void FfmpegDecoder::playbackFinished()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void FfmpegDecoder::errorOccurred(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}
QT_WARNING_POP
