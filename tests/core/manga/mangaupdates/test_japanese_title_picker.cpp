#include "core/manga/mangaupdates/JapaneseTitlePicker.h"

#include <gtest/gtest.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

using tankoban::manga::mangaupdates::JapaneseTitlePicker;

namespace {

QString loadFixtureRaw(const QString& relPath)
{
    QFile f(QStringLiteral(TANKOBAN_TEST_FIXTURE_DIR) + QStringLiteral("/") + relPath);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}

QStringList altTitlesFromFixture(const QString& relPath)
{
    const QString raw = loadFixtureRaw(relPath);
    if (raw.isEmpty()) return {};
    const auto obj = QJsonDocument::fromJson(raw.toUtf8()).object();
    QStringList out;
    for (const auto& v : obj.value(QStringLiteral("associated")).toArray()) {
        const QString t = v.toObject().value(QStringLiteral("title")).toString().trimmed();
        if (!t.isEmpty()) out.append(t);
    }
    return out;
}

} // namespace

TEST(JapaneseTitlePicker, PicksKatakanaFromBerserkFixture) {
    auto titles = altTitlesFromFixture(QStringLiteral("mangaupdates/berserk_series_51239621230.json"));
    ASSERT_FALSE(titles.isEmpty()) << "Fixture missing or empty";

    const QString japanese = JapaneseTitlePicker::pickFirstJapanese(titles);
    EXPECT_EQ(japanese, QString::fromUtf8("ベルセルク"));
}

TEST(JapaneseTitlePicker, PicksHiraganaWhenPresent) {
    QStringList titles = {
        QStringLiteral("Berserk"),
        QString::fromUtf8("べるせるく"),   // hiragana
        QString::fromUtf8("Берсерк"),
    };
    EXPECT_EQ(JapaneseTitlePicker::pickFirstJapanese(titles),
              QString::fromUtf8("べるせるく"));
}

TEST(JapaneseTitlePicker, PicksCjkUnifiedIdeographs) {
    QStringList titles = {
        QStringLiteral("Kingdom"),
        QString::fromUtf8("キングダム"),
    };
    EXPECT_EQ(JapaneseTitlePicker::pickFirstJapanese(titles),
              QString::fromUtf8("キングダム"));
}

TEST(JapaneseTitlePicker, ReturnsEmptyWhenNoJapaneseTitle) {
    QStringList titles = {
        QStringLiteral("Death Note"),
        QStringLiteral("DEATH NOTE"),
        QStringLiteral("DN"),
    };
    EXPECT_TRUE(JapaneseTitlePicker::pickFirstJapanese(titles).isEmpty());
}

TEST(JapaneseTitlePicker, ReturnsEmptyOnEmptyInput) {
    EXPECT_TRUE(JapaneseTitlePicker::pickFirstJapanese(QStringList{}).isEmpty());
}

TEST(JapaneseTitlePicker, SkipsLatinScriptEntriesBeforeFirstCjk) {
    QStringList titles = {
        QStringLiteral("Berserk"),
        QStringLiteral("Berserker"),
        QString::fromUtf8("ベルセルク"),    // 3rd entry, should still win
        QString::fromUtf8("Берсерク"),
    };
    EXPECT_EQ(JapaneseTitlePicker::pickFirstJapanese(titles),
              QString::fromUtf8("ベルセルク"));
}
