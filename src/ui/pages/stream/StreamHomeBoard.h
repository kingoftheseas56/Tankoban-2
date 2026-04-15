#pragma once

#include <QWidget>

class CoreBridge;
class StreamLibrary;
class StreamContinueStrip;

class QVBoxLayout;

namespace tankostream::addon {
class AddonRegistry;
}

namespace tankostream::stream {

class MetaAggregator;

// After the "remove Popular rows" UX change (2026-04-15), StreamHomeBoard is
// a thin container around the Continue Watching strip. The featured
// catalog rows + HomeCatalogRow inner class were deleted; users now reach
// the catalog browser via the Catalog button in StreamPage's search bar.
// The widget is kept (rather than inlined into StreamPage) so the existing
// scroll-layer composition + construction/refresh signal wiring stays
// undisturbed. Future inline refactor optional.
class StreamHomeBoard : public QWidget
{
    Q_OBJECT

public:
    explicit StreamHomeBoard(CoreBridge* bridge,
                             StreamLibrary* library,
                             tankostream::addon::AddonRegistry* registry,
                             tankostream::stream::MetaAggregator* meta,
                             QWidget* parent = nullptr);

    void refresh();

    StreamContinueStrip* continueStrip() const { return m_continueStrip; }

private:
    void buildUi();

    CoreBridge* m_bridge = nullptr;
    StreamLibrary* m_library = nullptr;
    tankostream::addon::AddonRegistry* m_registry = nullptr;
    tankostream::stream::MetaAggregator* m_meta = nullptr;

    StreamContinueStrip* m_continueStrip = nullptr;
};

}
