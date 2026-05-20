// src/core/manga/fandom/WikiManifest.cpp

#include "WikiManifest.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace tankoban::manga::fandom {

namespace {

PageModel parsePageModel(const QString& s)
{
    if (s == QStringLiteral("hierarchy"))       return PageModel::Hierarchy;
    if (s == QStringLiteral("chapter-branded")) return PageModel::ChapterBranded;
    return PageModel::Monolith;
}

ExtractorType parseExtractorType(const QString& s)
{
    if (s == QStringLiteral("infobox")) return ExtractorType::Infobox;
    if (s == QStringLiteral("mixed"))   return ExtractorType::Mixed;
    return ExtractorType::Table;
}

FieldExpectation parseFieldExpectation(const QString& s)
{
    if (s == QStringLiteral("required")) return FieldExpectation::Required;
    if (s == QStringLiteral("expected")) return FieldExpectation::Expected;
    if (s == QStringLiteral("absent"))   return FieldExpectation::Absent;
    return FieldExpectation::Optional;
}

PaginationModel parsePaginationModel(const QString& s)
{
    if (s == QStringLiteral("range-pages"))  return PaginationModel::RangePages;
    if (s == QStringLiteral("next-link"))    return PaginationModel::NextLink;
    if (s == QStringLiteral("detail-links")) return PaginationModel::DetailLinks;
    return PaginationModel::None;
}

} // anonymous

WikiManifest WikiManifest::fromJson(const QJsonObject& obj)
{
    WikiManifest m;
    m.seriesId          = obj.value(QStringLiteral("seriesId")).toString();
    m.wikidataQid       = obj.value(QStringLiteral("wikidataQid")).toString();
    m.fandomWikiId      = obj.value(QStringLiteral("fandomWikiId")).toString();
    m.volumePagePath    = obj.value(QStringLiteral("volumePagePath")).toString();
    m.pageModel         = parsePageModel(obj.value(QStringLiteral("pageModel")).toString());
    m.extractorType     = parseExtractorType(obj.value(QStringLiteral("extractorType")).toString());
    m.chapterKeyword    = obj.value(QStringLiteral("chapterKeyword")).toString(QStringLiteral("Chapter"));
    m.groupingSemantics = obj.value(QStringLiteral("groupingSemantics")).toString();
    m.notes             = obj.value(QStringLiteral("notes")).toString();

    const auto editionFilters = obj.value(QStringLiteral("editionFilters")).toArray();
    for (const auto& v : editionFilters)
        m.editionFilters.append(v.toString());

    const auto unitHierarchy = obj.value(QStringLiteral("unitHierarchy")).toArray();
    for (const auto& v : unitHierarchy)
        m.unitHierarchy.append(v.toString());

    const QJsonObject expected = obj.value(QStringLiteral("expectedFields")).toObject();
    for (auto it = expected.constBegin(); it != expected.constEnd(); ++it)
        m.expectedFields.insert(it.key(), parseFieldExpectation(it.value().toString()));

    // Pagination (Codex §5 expansion). All fields optional; defaults preserve
    // None-paginated single-page behavior.
    m.paginationModel  = parsePaginationModel(obj.value(QStringLiteral("paginationModel")).toString());
    m.pagePathPattern  = obj.value(QStringLiteral("pagePathPattern")).toString();
    m.pageRangeSize    = obj.value(QStringLiteral("pageRangeSize")).toInt(0);
    m.maxVolumeProbe   = obj.value(QStringLiteral("maxVolumeProbe")).toInt(0);
    m.nextLinkSelector = obj.value(QStringLiteral("nextLinkSelector")).toString();

    return m;
}

} // namespace tankoban::manga::fandom
