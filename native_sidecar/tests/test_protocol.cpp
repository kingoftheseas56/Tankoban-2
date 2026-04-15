#include <gtest/gtest.h>
#include "protocol.h"

TEST(Protocol, ParseValidCommand) {
    auto cmd = parse_command(R"({"type":"cmd","seq":1,"sessionId":"s1","name":"ping","payload":{}})");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->type, "cmd");
    EXPECT_EQ(cmd->name, "ping");
    EXPECT_EQ(cmd->sessionId, "s1");
    EXPECT_EQ(cmd->seq, 1);
}

TEST(Protocol, ParseRejectsNonCmd) {
    auto cmd = parse_command(R"({"type":"evt","name":"ready"})");
    EXPECT_FALSE(cmd.has_value());
}

TEST(Protocol, ParseRejectsGarbage) {
    auto cmd = parse_command("not json at all");
    EXPECT_FALSE(cmd.has_value());
}

TEST(Protocol, ParseRejectsEmpty) {
    auto cmd = parse_command("");
    EXPECT_FALSE(cmd.has_value());
}

TEST(Protocol, ParseDefaultPayload) {
    auto cmd = parse_command(R"({"type":"cmd","seq":5,"sessionId":"","name":"shutdown"})");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->name, "shutdown");
    EXPECT_TRUE(cmd->payload.is_object());
    EXPECT_TRUE(cmd->payload.empty());
}
