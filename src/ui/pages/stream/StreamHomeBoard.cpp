#include "StreamHomeBoard.h"

#include <QVBoxLayout>

#include "ui/pages/stream/StreamContinueStrip.h"

namespace tankostream::stream {

// After the "remove Popular rows" UX change (2026-04-15), this class is
// reduced to a thin wrapper around StreamContinueStrip. The pre-change
// implementation rendered up to 6 featured catalog rows from enabled
// addons (HomeCatalogRow inner class, enumerateCatalogs, chooseFeatured
// Catalogs, CatalogAggregator). All of that is gone — catalog browsing
// is now an explicit user action via StreamPage's Catalog button.

StreamHomeBoard::StreamHomeBoard(CoreBridge* bridge,
                                 StreamLibrary* library,
                                 tankostream::addon::AddonRegistry* registry,
                                 tankostream::stream::MetaAggregator* meta,
                                 QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
    , m_library(library)
    , m_registry(registry)
    , m_meta(meta)
{
    buildUi();
}

void StreamHomeBoard::refresh()
{
    if (m_continueStrip) {
        m_continueStrip->refresh();
    }
}

void StreamHomeBoard::buildUi()
{
    setObjectName(QStringLiteral("StreamHomeBoard"));
    setStyleSheet(QStringLiteral("#StreamHomeBoard { background: transparent; }"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_continueStrip = new StreamContinueStrip(m_bridge, m_library, m_meta, this);
    root->addWidget(m_continueStrip);
}

}
