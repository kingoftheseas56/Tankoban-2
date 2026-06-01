// src/core/manga/WesternSeriesParse.cpp
#include "WesternSeriesParse.h"

#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>

namespace tankoban::manga::western {

namespace {
// Ported verbatim from edition_classify.py. Non-letter lookarounds (NOT \b):
// RCO prefixes collected editions with '_' which is a word char, so \b would
// not fire before it; [a-z] lookarounds (case-insensitive) match regardless.
struct Rule { QRegularExpression rx; int tier; };

const QList<Rule>& rules() {
    static const QList<Rule> r = {
        { QRegularExpression(R"((?<![a-z])compendium(?![a-z]))",
              QRegularExpression::CaseInsensitiveOption), 0 },
        { QRegularExpression(R"((?<![a-z])omnibus(?![a-z]))",
              QRegularExpression::CaseInsensitiveOption), 1 },
        { QRegularExpression(R"((?<![a-z])(?:tpb|trade paperback|complete collection)(?![a-z]))",
              QRegularExpression::CaseInsensitiveOption), 2 },
        { QRegularExpression(R"((?<![a-z])(?:deluxe|absolute|library edition)(?![a-z]))",
              QRegularExpression::CaseInsensitiveOption), 3 },
        { QRegularExpression(R"((?<![a-z])(?:vol\.?|volume)(?![a-z]))",
              QRegularExpression::CaseInsensitiveOption), 4 },
    };
    return r;
}

const QRegularExpression& issueRe() {
    static const QRegularExpression re(R"((?<![a-z])issue(?![a-z])|#\s*\d)",
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

QString unescapeEntities(QString s) {
    s.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">")
     .replace("&quot;", "\"").replace("&#39;", "'").replace("&nbsp;", " ");
    return s;
}
} // namespace

int editionTier(const QString& label) {
    for (const auto& r : rules())
        if (r.rx.match(label).hasMatch())
            return r.tier;
    return 99;
}

bool isCollected(const QString& label) {
    const int t = editionTier(label);
    if (t <= 3) return true;
    if (t == 4 && !issueRe().match(label).hasMatch()) return true;
    return false;
}

QString slugToLabel(const QString& href) {
    QString s = href;
    while (s.endsWith('/')) s.chop(1);
    const QString seg = s.section('/', -1);
    static const QRegularExpression sep(R"([\s \-\x{2010}-\x{2015}]+)");
    return seg.split(sep, Qt::SkipEmptyParts).join(' ').trimmed();
}

QList<SeriesItem> parseSeriesItems(const QString& html) {
    // /Comic/<Series>/<Item> with optional ?query (query excluded from capture).
    // Custom raw-string delimiter avoids the )" termination clash with the regex.
    static const QRegularExpression itemRe(R"rx(href="(/Comic/[^"/]+/[^"?]+)(?:\?[^"]*)?")rx");
    QList<SeriesItem> items;
    QSet<QString> seen;
    auto it = itemRe.globalMatch(html);
    while (it.hasNext()) {
        const QString href = it.next().captured(1);
        if (seen.contains(href)) continue;
        seen.insert(href);
        items.push_back({ slugToLabel(href), href });
    }
    return items;
}

QString parseSeriesCover(const QString& html) {
    // Custom raw-string delimiter avoids the )" termination clash with the regex.
    static const QRegularExpression coverRe(
        R"rx(<link\s+rel="image_src"\s+href="([^"]+)")rx",
        QRegularExpression::CaseInsensitiveOption);
    const auto m = coverRe.match(html);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString parseSeriesSummary(const QString& html) {
    static const QRegularExpression sumRe(
        R"(<span class="info">\s*Summary:\s*</span>\s*<p>(.*?)</p>)",
        QRegularExpression::DotMatchesEverythingOption
            | QRegularExpression::CaseInsensitiveOption);
    const auto m = sumRe.match(html);
    if (!m.hasMatch()) return QString();
    static const QRegularExpression tagRe(R"(<[^>]+>)");
    static const QRegularExpression wsRe(R"(\s+)");
    QString text = m.captured(1);
    text.replace(tagRe, " ");
    text = unescapeEntities(text);
    return text.replace(wsRe, " ").trimmed();
}

bool needsSummaryFallback(const QString& summary) {
    return summary.trimmed().size() < 120;
}

QJsonArray buildEditions(const QList<SeriesItem>& items) {
    QList<SeriesItem> collected;
    for (const auto& it : items)
        if (isCollected(it.label))
            collected.push_back(it);
    std::stable_sort(collected.begin(), collected.end(),
                     [](const SeriesItem& a, const SeriesItem& b) {
                         return editionTier(a.label) < editionTier(b.label);
                     });
    QJsonArray out;
    for (const auto& it : collected) {
        QJsonObject e;
        e["label"] = it.label;
        e["href"] = it.href;
        e["formatTier"] = editionTier(it.label);
        out.push_back(e);
    }
    return out;
}

} // namespace tankoban::manga::western
