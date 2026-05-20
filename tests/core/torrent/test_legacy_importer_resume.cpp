// tankoban_tests — LegacyImporter::loadResumeBlob tests (P1.2)
//
// Covers the silent-fail-on-missing contract + .toLower() case-insensitive
// boundary at the resume-blob reader. The reader's job is dumbest-possible:
// "give me bytes if the file is there, empty if anything is wrong." Every
// failure mode collapses to an empty QByteArray return — no exceptions, no
// warnings, no errors. importInto's summary tallies non-empty returns as
// `resumeBlobsAttached`.
//
// See docs/superpowers/plans/2026-05-19-torrent-persistence-collapse.md
// Task 1.2 and src/core/torrent/TorrentEngine.cpp:154 for the filename
// convention (<hashToHex(handle)>.fastresume, hashToHex always lowercase).
//
// Phase 1 of TORRENT_PERSISTENCE_COLLAPSE.

#include <gtest/gtest.h>

#include "core/torrent/LegacyImporter.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QTemporaryDir>

namespace {

using tankoban::torrent::LegacyImporter;

class LegacyImporterResumeTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
    }

    QString resumeDir() const { return m_tmpDir.path(); }

    void writeFastresume(const QString& lowercaseHash, const QByteArray& bytes) {
        const QString path = QDir(resumeDir())
                                 .filePath(lowercaseHash + QStringLiteral(".fastresume"));
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        if (!bytes.isEmpty()) {
            ASSERT_EQ(f.write(bytes), bytes.size());
        }
        f.close();
    }

    QTemporaryDir m_tmpDir;
    LegacyImporter m_importer;
};

}  // namespace

TEST_F(LegacyImporterResumeTest, AbsentReturnsEmpty) {
    const QByteArray blob = m_importer.loadResumeBlob(
        resumeDir(),
        QStringLiteral("0000000000000000000000000000000000000000"));
    EXPECT_TRUE(blob.isEmpty());
}

TEST_F(LegacyImporterResumeTest, PresentReturnsExactBytes) {
    const QString hash = QStringLiteral("cafebabe1234567890abcdef1234567890abcdef");
    const QByteArray expected = QByteArray::fromHex("d12345e0deadbeefcafebabe1337c0de");
    writeFastresume(hash, expected);

    const QByteArray actual = m_importer.loadResumeBlob(resumeDir(), hash);
    EXPECT_EQ(actual, expected);
}

TEST_F(LegacyImporterResumeTest, CaseInsensitiveHashLookup) {
    const QString lowerHash = QStringLiteral("abcdef1234567890abcdef1234567890abcdef12");
    const QString upperHash = QStringLiteral("ABCDEF1234567890ABCDEF1234567890ABCDEF12");
    const QByteArray expected = QByteArray::fromHex("0badf00ddeadbeef");
    writeFastresume(lowerHash, expected);

    const QByteArray actual = m_importer.loadResumeBlob(resumeDir(), upperHash);
    EXPECT_EQ(actual, expected);
}

TEST_F(LegacyImporterResumeTest, EmptyFileReturnsEmptyByteArray) {
    const QString hash = QStringLiteral("1111111111111111111111111111111111111111");
    writeFastresume(hash, QByteArray{});

    const QByteArray actual = m_importer.loadResumeBlob(resumeDir(), hash);
    EXPECT_TRUE(actual.isEmpty());
}

TEST_F(LegacyImporterResumeTest, NonExistentDirectoryReturnsEmpty) {
    const QString bogusDir = resumeDir() + QStringLiteral("/does/not/exist");
    const QByteArray blob = m_importer.loadResumeBlob(
        bogusDir,
        QStringLiteral("2222222222222222222222222222222222222222"));
    EXPECT_TRUE(blob.isEmpty());
}
