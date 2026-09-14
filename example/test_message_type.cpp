#include <cmw/config/message_type.h>
#include <cmw/common/log.h>

#include <gtest/gtest.h>

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <iostream>

namespace {
struct StableMessageV1 {};
struct StableMessageV2 {};
struct BuildLocalMessage {};
struct LegacyNamedMessage {};
struct OtherBuildLocalMessage {};

std::string ReadChildIdentifier(const char* kind) {
  int pipe_fd[2];
  if (pipe(pipe_fd) != 0) return std::string();
  const pid_t pid = fork();
  if (pid == 0) {
    close(pipe_fd[0]);
    dup2(pipe_fd[1], STDOUT_FILENO);
    close(pipe_fd[1]);
    execl("/proc/self/exe", "/proc/self/exe", "--print-message-type", kind,
          static_cast<char*>(nullptr));
    _exit(127);
  }
  close(pipe_fd[1]);
  std::string result;
  std::array<char, 256> buffer{};
  ssize_t count = 0;
  while ((count = read(pipe_fd[0], buffer.data(), buffer.size())) > 0) {
    result.append(buffer.data(), static_cast<std::size_t>(count));
  }
  close(pipe_fd[0]);
  int status = 0;
  if (pid < 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
      WEXITSTATUS(status) != 0) {
    return std::string();
  }
  return result;
}
}  // namespace

namespace hnu {
namespace cmw {
namespace config {
template <>
struct MessageTypeTrait<StableMessageV1> {
  static const char* Name() { return "example.telemetry"; }
  static uint32_t Version() { return 1; }
};
template <>
struct MessageTypeTrait<StableMessageV2> {
  static const char* Name() { return "example.telemetry"; }
  static uint32_t Version() { return 2; }
};
}  // namespace config
}  // namespace cmw
}  // namespace hnu

TEST(MessageTypeTest, ExplicitSchemaIncludesVersion) {
  using hnu::cmw::config::MessageTypeIdentifier;
  EXPECT_EQ("cmw.schema/example.telemetry@1",
            MessageTypeIdentifier<StableMessageV1>());
  EXPECT_EQ("cmw.schema/example.telemetry@2",
            MessageTypeIdentifier<StableMessageV2>());
  EXPECT_NE(MessageTypeIdentifier<StableMessageV1>(),
            MessageTypeIdentifier<StableMessageV2>());
}

TEST(MessageTypeTest, FallbackIsNonEmptyAndCompilerScoped) {
  const std::string first =
      hnu::cmw::config::MessageTypeIdentifier<BuildLocalMessage>();
  const std::string second =
      hnu::cmw::config::MessageTypeIdentifier<BuildLocalMessage>();
  EXPECT_EQ(first, second);
  EXPECT_EQ(0u, first.find("cmw.abi/"));
}

TEST(MessageTypeTest, LegacyExplicitNameIsPreserved) {
  EXPECT_EQ("legacy.explicit.name",
            hnu::cmw::config::MessageTypeIdentifier<LegacyNamedMessage>(
                "legacy.explicit.name"));
}

TEST(MessageTypeTest, CompatibilityRejectsUnknownAndVersionMismatch) {
  using hnu::cmw::config::IsMessageTypeCompatible;
  EXPECT_FALSE(IsMessageTypeCompatible("", ""));
  EXPECT_FALSE(IsMessageTypeCompatible("type-a", ""));
  EXPECT_FALSE(IsMessageTypeCompatible("type-a", "type-b"));
  EXPECT_TRUE(IsMessageTypeCompatible("type-a", "type-a"));
}

TEST(MessageTypeTest, AbiFallbackIsConsistentAcrossExecAndDistinguishesTypes) {
  const std::string local =
      hnu::cmw::config::MessageTypeIdentifier<BuildLocalMessage>();
  EXPECT_EQ(local, ReadChildIdentifier("same"));
  EXPECT_NE(local, ReadChildIdentifier("other"));
}

int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "--print-message-type") {
    if (std::string(argv[2]) == "same") {
      std::cout <<
          hnu::cmw::config::MessageTypeIdentifier<BuildLocalMessage>();
      return 0;
    }
    if (std::string(argv[2]) == "other") {
      std::cout <<
          hnu::cmw::config::MessageTypeIdentifier<OtherBuildLocalMessage>();
      return 0;
    }
    return 2;
  }
  Logger_Init("MessageTypeTest");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
