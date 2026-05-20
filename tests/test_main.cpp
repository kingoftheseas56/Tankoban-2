// tankoban_tests entry point — bootstraps QCoreApplication before running gtest.
//
// QSqlDatabase plugin loading (QSQLITE driver) requires a live QCoreApplication
// instance to walk libraryPaths(). gtest_main does not provide one, which makes
// any test that touches QSqlDatabase::addDatabase() segfault with SEH 0xc0000005
// at the first SQL call. We replace the gtest_main link with this main so every
// test in tankoban_tests has Qt event-loop infrastructure available.
//
// Introduced 2026-05-20 (TORRENT_PERSISTENCE_COLLAPSE P0.4) — pre-existing
// non-SQL tests are unaffected (QCoreApplication is benign for them).

#include <gtest/gtest.h>
#include <QCoreApplication>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
