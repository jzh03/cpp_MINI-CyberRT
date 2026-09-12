#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <iterator>
#include <string>
#include <gtest/gtest.h>
#include <cmw/common/log.h>

namespace {

std::string Read(const std::string& path) {
  std::ifstream stream(path);
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

TEST(LoggerPaths, RootSelectionNamesAppendRotationAndFailures) {
  auto logger = Logger::Instance();
  logger->console(false);
  const char* previous = getenv("CMW_PATH");
  const bool had_root = previous != nullptr;
  const std::string saved_root = previous == nullptr ? "" : previous;
  char* cwd = getcwd(nullptr, 0);
  ASSERT_NE(nullptr, cwd);
  const std::string saved_cwd(cwd);
  free(cwd);
  const std::string root = CMW_PROJECT_ROOT;
  const std::string name = "logger_paths_" + std::to_string(getpid());
  const std::string path = root + "/log/" + name + ".log";

  // All scratch artifacts stay inside this project's log directory.
  std::string pattern = root + "/log/" + name + "_XXXXXX";
  ASSERT_NE(nullptr, mkdtemp(&pattern[0]));
  struct Cleanup {
    std::string cwd, root, scratch, path;
    bool had_root;
    ~Cleanup() {
      Logger::Instance()->close();
      Logger::Instance()->max(0);
      chdir(cwd.c_str());
      if (had_root) { setenv("CMW_PATH", root.c_str(), 1); }
      else { unsetenv("CMW_PATH"); }
      unlink(path.c_str());
      const std::string logs = scratch + "/log";
      DIR* dir = opendir(logs.c_str());
      if (dir != nullptr) {
        while (auto entry = readdir(dir)) {
          const std::string name = entry->d_name;
          if (name != "." && name != "..") { unlink((logs + "/" + name).c_str()); }
        }
        closedir(dir);
      }
      rmdir(logs.c_str());
      rmdir(scratch.c_str());
    }
  } cleanup{saved_cwd, saved_root, pattern, path, had_root};

  ASSERT_EQ(0, chdir(pattern.c_str()));
  ASSERT_EQ(0, unsetenv("CMW_PATH"));
  Logger_Init(name + ".log");  // Build-time root works from an unrelated cwd.
  log_info("first-record");
  Logger_Init("../../" + name + ".log");  // Reopen appends; path cannot escape.
  log_info("second-record");
  logger->close();
  EXPECT_NE(std::string::npos, Read(path).find("first-record"));
  EXPECT_NE(std::string::npos, Read(path).find("second-record"));
  EXPECT_EQ(-1, access((pattern + "/" + name + ".log").c_str(), F_OK));

  ASSERT_EQ(0, setenv("CMW_PATH", pattern.c_str(), 1));
  Logger_Init("/tmp/ignored/rotation");  // mkdir log, basename and .log suffix.
  logger->max(1);
  log_info("rotated-record");
  logger->max(0);
  log_info("after-rotation");
  logger->close();
  EXPECT_NE(std::string::npos, Read(pattern + "/log/rotation.log").find("after-rotation"));
  DIR* dir = opendir((pattern + "/log").c_str());
  ASSERT_NE(nullptr, dir);
  bool rotated = false;
  while (auto entry = readdir(dir)) {
    const std::string entry_name = entry->d_name;
    if (entry_name.find("rotation.log.") == 0) {
      rotated = Read(pattern + "/log/" + entry_name).find("rotated-record") != std::string::npos;
    }
  }
  closedir(dir);
  EXPECT_TRUE(rotated);
  EXPECT_THROW(logger->open("../"), std::logic_error);
  ASSERT_EQ(0, symlink("../outside.log", (pattern + "/log/link.log").c_str()));
  EXPECT_THROW(logger->open("link.log"), std::logic_error);
  EXPECT_EQ(-1, access((pattern + "/outside.log").c_str(), F_OK));
  ASSERT_EQ(0, setenv("CMW_PATH", (pattern + "/missing").c_str(), 1));
  EXPECT_THROW(logger->open("failure.log"), std::logic_error);
  EXPECT_EQ(-1, access((pattern + "/failure.log").c_str(), F_OK));
}

}  // namespace

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
