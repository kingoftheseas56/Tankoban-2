#include "core/manga/TrustedUploaders.h"

#include <gtest/gtest.h>

using tankoban::manga::TrustedUploaders;

TEST(TrustedUploadersTest, ExactMatchReturnsTrue)
{
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("1r0n")));
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("hox")));
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("viz digital")));
}

TEST(TrustedUploadersTest, CaseInsensitiveMatchReturnsTrue)
{
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("Hox")));
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("HOX")));
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("VIZ Digital")));
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("VIZ DIGITAL")));
}

TEST(TrustedUploadersTest, UnknownUploaderReturnsFalse)
{
    EXPECT_FALSE(TrustedUploaders::isTrusted(QStringLiteral("randomuploader")));
    EXPECT_FALSE(TrustedUploaders::isTrusted(QStringLiteral("vi")));          // substring of canonical
    EXPECT_FALSE(TrustedUploaders::isTrusted(QStringLiteral("viz digital 2"))); // canonical + suffix
    EXPECT_FALSE(TrustedUploaders::isTrusted(QString()));                       // empty
}

TEST(TrustedUploadersTest, WhitespaceTrimmedBeforeMatch)
{
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("  hox")));
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("hox  ")));
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("  hox  ")));
    EXPECT_TRUE(TrustedUploaders::isTrusted(QStringLiteral("\t1r0n\n")));
    EXPECT_FALSE(TrustedUploaders::isTrusted(QStringLiteral("   ")));   // whitespace-only normalises to empty
}
