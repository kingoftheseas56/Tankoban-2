#include "core/manga/TrustedUploaders.h"

#include <gtest/gtest.h>

using tankoban::manga::TrustedUploaders;

TEST(TrustedUploadersTest, ExactMatchReturnsTrue)
{
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("antiherogold")));
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("1r0n")));
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("danke-empire")));
}

TEST(TrustedUploadersTest, CaseInsensitiveMatchReturnsTrue)
{
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("AntiHeroGold")));
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("ANTIHEROGOLD")));
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("Danke-Empire")));
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("DANKE-EMPIRE")));
}

TEST(TrustedUploadersTest, UnknownUploaderReturnsFalse)
{
    EXPECT_FALSE(TrustedUploaders::isTrusted(QStringLiteral("randomuploader")));
    EXPECT_FALSE(TrustedUploaders::isTrusted(QStringLiteral("antihero")));     // substring of canonical
    EXPECT_FALSE(TrustedUploaders::isTrusted(QStringLiteral("antiherogold2"))); // canonical + suffix
    EXPECT_FALSE(TrustedUploaders::isTrusted(QString()));                       // empty
}

TEST(TrustedUploadersTest, WhitespaceTrimmedBeforeMatch)
{
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("  antiherogold")));
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("antiherogold  ")));
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("  antiherogold  ")));
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("\t1r0n\n")));
    EXPECT_FALSE(TrustedUploaders::isTrusted(QStringLiteral("   ")));   // whitespace-only normalises to empty
}
