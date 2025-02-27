#include "file_sink.hpp"

#include <gtest/gtest.h>

#include <userver/engine/async.hpp>
#include <userver/engine/io/pipe.hpp>
#include <userver/fs/blocking/read.hpp>
#include <userver/fs/blocking/temp_directory.hpp>
#include <userver/utest/utest.hpp>
#include <userver/utils/rand.hpp>
#include <userver/utils/text.hpp>

#include "sink_helper_test.hpp"

USERVER_NAMESPACE_BEGIN

UTEST(FileSink, TestCreateFile) {
  const auto temp_root = fs::blocking::TempDirectory::Create();
  const std::string filename =
      temp_root.GetPath() + "/temp_file_" + std::to_string(utils::Rand());
  auto sink = logging::impl::FileSink(filename);
  ASSERT_TRUE(boost::filesystem::exists(filename));
}

UTEST(FileSink, TestCreateFileMultiDir) {
  const auto temp_root = fs::blocking::TempDirectory::Create();
  const std::string filename = temp_root.GetPath() +
                               "/dir1/dir2/dir3/temp_file_" +
                               std::to_string(utils::Rand());
  auto sink = logging::impl::FileSink(filename);
  ASSERT_TRUE(boost::filesystem::exists(filename));
}

UTEST(FileSink, TestWriteInFile) {
  const auto temp_root = fs::blocking::TempDirectory::Create();
  const std::string filename =
      temp_root.GetPath() + "/temp_file_" + std::to_string(utils::Rand());
  auto sink = logging::impl::FileSink(filename);
  ASSERT_TRUE(boost::filesystem::exists(filename));
  EXPECT_NO_THROW(sink.Log({"default", spdlog::level::critical, "message"}));
}

UTEST(FileSink, CheckPermissionsFile) {
  const auto temp_root = fs::blocking::TempDirectory::Create();
  const std::string filename =
      temp_root.GetPath() + "/temp_file_" + std::to_string(utils::Rand());
  auto sink = logging::impl::FileSink(filename);
  ASSERT_TRUE(boost::filesystem::exists(filename));
  boost::filesystem::file_status stat = boost::filesystem::status(filename);
  ASSERT_TRUE(stat.permissions() & boost::filesystem::owner_read);
  ASSERT_TRUE(stat.permissions() & boost::filesystem::owner_write);
  ASSERT_FALSE(stat.permissions() & boost::filesystem::owner_exe);
  ASSERT_TRUE(stat.permissions() & boost::filesystem::group_read);
  ASSERT_FALSE(stat.permissions() & boost::filesystem::group_write);
  ASSERT_FALSE(stat.permissions() & boost::filesystem::group_exe);
  ASSERT_TRUE(stat.permissions() & boost::filesystem::others_read);
  ASSERT_FALSE(stat.permissions() & boost::filesystem::others_write);
  ASSERT_FALSE(stat.permissions() & boost::filesystem::others_exe);
}

UTEST(FileSink, CheckPermissionsDir) {
  const auto temp_root = fs::blocking::TempDirectory::Create();
  const std::string filename =
      temp_root.GetPath() + "/dir/temp_file_" + std::to_string(utils::Rand());
  auto sink = logging::impl::FileSink(filename);
  ASSERT_TRUE(boost::filesystem::exists(filename));
  auto path = boost::filesystem::path(filename).parent_path();
  boost::filesystem::file_status stat = boost::filesystem::status(path);
  ASSERT_TRUE(stat.permissions() & boost::filesystem::owner_read);
  ASSERT_TRUE(stat.permissions() & boost::filesystem::owner_write);
  ASSERT_TRUE(stat.permissions() & boost::filesystem::owner_exe);
  ASSERT_TRUE(stat.permissions() & boost::filesystem::group_read);
  ASSERT_FALSE(stat.permissions() & boost::filesystem::group_write);
  ASSERT_TRUE(stat.permissions() & boost::filesystem::group_exe);
  ASSERT_TRUE(stat.permissions() & boost::filesystem::others_read);
  ASSERT_FALSE(stat.permissions() & boost::filesystem::others_write);
  ASSERT_TRUE(stat.permissions() & boost::filesystem::others_exe);
}

UTEST(FileSink, TestValidWriteInFile) {
  const auto temp_root = fs::blocking::TempDirectory::Create();
  const std::string filename =
      temp_root.GetPath() + "/temp_file_" + std::to_string(utils::Rand());
  auto sink = logging::impl::FileSink(filename);
  ASSERT_TRUE(boost::filesystem::exists(filename));
  EXPECT_NO_THROW(sink.Log({"default", spdlog::level::warn, "message"}));

  auto content = fs::blocking::ReadFileContents(filename);

  const auto result = test::NormalizeLogs(content);

  EXPECT_EQ(result.size(), 1);
  EXPECT_EQ(result.front(), "[datetime] [default] [warning] message");
}

UTEST(FileSink, TestReopenWithTruncate) {
  const auto temp_root = fs::blocking::TempDirectory::Create();
  const std::string filename =
      temp_root.GetPath() + "/temp_file_" + std::to_string(utils::Rand());
  auto sink = logging::impl::FileSink(filename);
  ASSERT_TRUE(boost::filesystem::exists(filename));
  EXPECT_NO_THROW(sink.Log({"default", spdlog::level::warn, "message"}));

  auto content = fs::blocking::ReadFileContents(filename);

  const auto result = test::NormalizeLogs(content);

  EXPECT_EQ(result.size(), 1);
  EXPECT_EQ(result.front(), "[datetime] [default] [warning] message");

  EXPECT_NO_THROW(sink.Reopen(logging::impl::ReopenMode::kTruncate));
  content = fs::blocking::ReadFileContents(filename);
  ASSERT_TRUE(content.empty());
}

UTEST(FileSink, TestReopenWithTruncateWrite) {
  const auto temp_root = fs::blocking::TempDirectory::Create();
  const std::string filename =
      temp_root.GetPath() + "/temp_file_" + std::to_string(utils::Rand());
  auto sink = logging::impl::FileSink(filename);
  ASSERT_TRUE(boost::filesystem::exists(filename));
  EXPECT_NO_THROW(sink.Log({"default", spdlog::level::warn, "message"}));

  auto content = fs::blocking::ReadFileContents(filename);

  auto result = test::NormalizeLogs(content);

  EXPECT_EQ(result.size(), 1);
  EXPECT_EQ(result.front(), "[datetime] [default] [warning] message");

  EXPECT_NO_THROW(sink.Reopen(logging::impl::ReopenMode::kTruncate));
  content = fs::blocking::ReadFileContents(filename);
  ASSERT_TRUE(content.empty());

  EXPECT_NO_THROW(sink.Log({"default", spdlog::level::info, "message 2"}));

  content = fs::blocking::ReadFileContents(filename);

  result = test::NormalizeLogs(content);

  EXPECT_EQ(result.size(), 1);
  EXPECT_EQ(result.front(), "[datetime] [default] [info] message 2");
}

UTEST(FileSink, TestReopenWithoutTruncate) {
  const auto temp_root = fs::blocking::TempDirectory::Create();
  const std::string filename =
      temp_root.GetPath() + "/temp_file_" + std::to_string(utils::Rand());
  auto sink = logging::impl::FileSink(filename);
  ASSERT_TRUE(boost::filesystem::exists(filename));
  EXPECT_NO_THROW(sink.Log({"default", spdlog::level::warn, "message"}));

  auto content = fs::blocking::ReadFileContents(filename);

  const auto result = test::NormalizeLogs(content);

  EXPECT_EQ(result.size(), 1);
  EXPECT_EQ(result.front(), "[datetime] [default] [warning] message");

  EXPECT_NO_THROW(sink.Reopen(logging::impl::ReopenMode::kAppend));
  content = fs::blocking::ReadFileContents(filename);

  const auto result_reopen = test::NormalizeLogs(content);

  EXPECT_EQ(result_reopen.size(), 1);
  EXPECT_EQ(result_reopen.front(), "[datetime] [default] [warning] message");
}

UTEST(FileSink, TestReopenBeforeRemove) {
  const auto temp_root = fs::blocking::TempDirectory::Create();
  const std::string filename =
      temp_root.GetPath() + "/temp_file_" + std::to_string(utils::Rand());
  auto sink = logging::impl::FileSink(filename);
  ASSERT_TRUE(boost::filesystem::exists(filename));
  EXPECT_NO_THROW(sink.Log({"default", spdlog::level::warn, "message"}));

  auto content = fs::blocking::ReadFileContents(filename);

  const auto result = test::NormalizeLogs(content);

  EXPECT_EQ(result.size(), 1);
  EXPECT_EQ(result.front(), "[datetime] [default] [warning] message");

  boost::filesystem::remove(filename);
  EXPECT_NO_THROW(sink.Reopen(logging::impl::ReopenMode::kAppend));
  content = fs::blocking::ReadFileContents(filename);
  ASSERT_TRUE(content.empty());
}

UTEST(FileSink, TestReopenBeforeRemoveCreate) {
  const auto temp_root = fs::blocking::TempDirectory::Create();
  const std::string filename =
      temp_root.GetPath() + "/temp_file_" + std::to_string(utils::Rand());
  auto sink = logging::impl::FileSink(filename);
  ASSERT_TRUE(boost::filesystem::exists(filename));
  EXPECT_NO_THROW(sink.Log({"default", spdlog::level::warn, "message"}));

  auto content = fs::blocking::ReadFileContents(filename);

  auto result = test::NormalizeLogs(content);

  EXPECT_EQ(result.size(), 1);
  EXPECT_EQ(result.front(), "[datetime] [default] [warning] message");
  namespace b_fs = boost::filesystem;

  b_fs::remove(filename);

  // Create file manually
  auto fd = ::open(filename.c_str(), O_CLOEXEC | O_CREAT | O_WRONLY | O_APPEND,
                   b_fs::owner_write | b_fs::owner_read | b_fs::group_read |
                       b_fs::others_read);
  ASSERT_TRUE(fd != -1);
  ::close(fd);

  EXPECT_NO_THROW(sink.Reopen(logging::impl::ReopenMode::kAppend));
  content = fs::blocking::ReadFileContents(filename);
  ASSERT_TRUE(content.empty());

  EXPECT_NO_THROW(sink.Log({"default", spdlog::level::warn, "message"}));

  content = fs::blocking::ReadFileContents(filename);

  result = test::NormalizeLogs(content);

  EXPECT_EQ(result.size(), 1);
  EXPECT_EQ(result.front(), "[datetime] [default] [warning] message");
}

UTEST(FileSink, TestReopenBeforeRemoveAndWrite) {
  const auto temp_root = fs::blocking::TempDirectory::Create();
  const std::string filename =
      temp_root.GetPath() + "/temp_file_" + std::to_string(utils::Rand());
  auto sink = logging::impl::FileSink(filename);
  ASSERT_TRUE(boost::filesystem::exists(filename));
  EXPECT_NO_THROW(sink.Log({"default", spdlog::level::warn, "message"}));

  auto content = fs::blocking::ReadFileContents(filename);

  auto result = test::NormalizeLogs(content);

  EXPECT_EQ(result.size(), 1);
  EXPECT_EQ(result.front(), "[datetime] [default] [warning] message");

  boost::filesystem::remove(filename);
  EXPECT_NO_THROW(sink.Reopen(logging::impl::ReopenMode::kAppend));
  content = fs::blocking::ReadFileContents(filename);
  ASSERT_TRUE(content.empty());

  EXPECT_NO_THROW(sink.Log({"default", spdlog::level::warn, "message"}));

  content = fs::blocking::ReadFileContents(filename);

  result = test::NormalizeLogs(content);

  EXPECT_EQ(result.size(), 1);
  EXPECT_EQ(result.front(), "[datetime] [default] [warning] message");
}

UTEST(FileSink, TestReopenMoveFile) {
  const auto temp_root = fs::blocking::TempDirectory::Create();
  const std::string filename =
      temp_root.GetPath() + "/temp_file_" + std::to_string(utils::Rand());

  const std::string filename_2 =
      temp_root.GetPath() + "/temp_file_" + std::to_string(utils::Rand());

  ASSERT_TRUE(filename != filename_2);

  auto sink = logging::impl::FileSink(filename);
  ASSERT_TRUE(boost::filesystem::exists(filename));
  EXPECT_NO_THROW(sink.Log({"default", spdlog::level::warn, "message"}));

  auto content = fs::blocking::ReadFileContents(filename);

  auto result = test::NormalizeLogs(content);

  EXPECT_EQ(result.size(), 1);
  EXPECT_EQ(result.front(), "[datetime] [default] [warning] message");

  ::rename(filename.c_str(), filename_2.c_str());

  EXPECT_NO_THROW(sink.Log({"default", spdlog::level::info, "message 2"}));

  content = fs::blocking::ReadFileContents(filename_2);
  ASSERT_FALSE(content.empty());
  result = test::NormalizeLogs(content);

  EXPECT_EQ(result.size(), 2);
  EXPECT_EQ(result[0], "[datetime] [default] [warning] message");
  EXPECT_EQ(result[1], "[datetime] [default] [info] message 2");

  EXPECT_NO_THROW(sink.Reopen(logging::impl::ReopenMode::kAppend));

  content = fs::blocking::ReadFileContents(filename);
  ASSERT_TRUE(content.empty());

  EXPECT_NO_THROW(sink.Log({"default", spdlog::level::warn, "message"}));

  content = fs::blocking::ReadFileContents(filename);

  result = test::NormalizeLogs(content);

  EXPECT_EQ(result.size(), 1);
  EXPECT_EQ(result.front(), "[datetime] [default] [warning] message");
}

UTEST(FileSink, TestValidWriteMultiInFile) {
  const auto temp_root = fs::blocking::TempDirectory::Create();
  const std::string filename =
      temp_root.GetPath() + "/temp_file_" + std::to_string(utils::Rand());
  auto sink = logging::impl::FileSink(filename);
  ASSERT_TRUE(boost::filesystem::exists(filename));
  EXPECT_NO_THROW(sink.Log({"default", spdlog::level::warn, "message"}));
  EXPECT_NO_THROW(sink.Log({"basic", spdlog::level::info, "message 2"}));
  EXPECT_NO_THROW(sink.Log({"current", spdlog::level::critical, "message 3"}));

  auto content = fs::blocking::ReadFileContents(filename);

  const auto result = test::NormalizeLogs(content);

  EXPECT_EQ(result.size(), 3);
  EXPECT_EQ(result[0], "[datetime] [default] [warning] message");
  EXPECT_EQ(result[1], "[datetime] [basic] [info] message 2");
  EXPECT_EQ(result[2], "[datetime] [current] [critical] message 3");
}

USERVER_NAMESPACE_END
