/**
 * @file tests/integration/test_launch_client_environment.cpp
 * @brief Preserve the launch request client identity in lifecycle hooks.
 */
#include "../tests_common.h"
#include "src/nvhttp.h"
#include "src/process.h"

#include <boost/filesystem.hpp>
#include <fstream>
#include <iterator>

namespace nvhttp {
  // Exercise the production request decoder without starting an HTTP server.
  std::shared_ptr<rtsp_stream::launch_session_t> make_launch_session(bool host_audio, const SimpleWeb::CaseInsensitiveMultimap &args);
}  // namespace nvhttp

namespace {
  auto launch_request(const std::optional<std::string> &client_id) {
    SimpleWeb::CaseInsensitiveMultimap args {
      {"rikey", "00112233445566778899aabbccddeeff"},
      {"rikeyid", "1"},
      {"appid", "1"},
      {"mode", "1920x1080x60"}
    };
    if (client_id) {
      args.emplace("uniqueid", *client_id);
    }
    return nvhttp::make_launch_session(false, args);
  }
}  // namespace

TEST(LaunchClientEnvironmentTest, RequestIdentityIsPreservedAndMissingIdentityStaysEmpty) {
  EXPECT_EQ(launch_request("64e1f15da6a579ad")->unique_id, "64e1f15da6a579ad");
  EXPECT_EQ(launch_request("windows-client")->unique_id, "windows-client");
  EXPECT_TRUE(launch_request(std::nullopt)->unique_id.empty());
  EXPECT_TRUE(launch_request("")->unique_id.empty());
}

#ifdef _WIN32
TEST(LaunchClientEnvironmentTest, PrepAndUndoReceiveEachLaunchOwnerWithoutStaleInheritance) {
  const auto output = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("sunshine-owner-%%%%-%%%%.txt");
  const auto remove_output = util::fail_guard([&]() {
    boost::system::error_code ec;
    boost::filesystem::remove(output, ec);
  });
  boost::process::v1::environment env = boost::this_process::environment();
  env["HYDRA_TEST_OUTPUT"] = output.string();
  env["SUNSHINE_CLIENT_UUID"] = "stale-inherited-owner";
  proc::ctx_t app {};
  app.id = "1";
  app.name = "Identity hook test";
  app.working_dir = output.parent_path().string();
  app.prep_cmds.push_back({
    R"(powershell.exe -NoProfile -NonInteractive -Command "[IO.File]::AppendAllText($env:HYDRA_TEST_OUTPUT, 'do:' + $env:SUNSHINE_CLIENT_UUID + [Environment]::NewLine)")",
    R"(powershell.exe -NoProfile -NonInteractive -Command "[IO.File]::AppendAllText($env:HYDRA_TEST_OUTPUT, 'undo:' + $env:SUNSHINE_CLIENT_UUID + [Environment]::NewLine)")",
    false
  });
  proc::proc_t process {std::move(env), {app}};
  const auto terminate = util::fail_guard([&]() {
    process.terminate();
  });
  for (const auto &id : {"64e1f15da6a579ad", "windows-client", ""}) {
    ASSERT_EQ(process.execute(1, launch_request(id)), 0);
    process.terminate();
  }
  std::ifstream input(output.string());
  ASSERT_TRUE(input.good());
  std::string actual {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  EXPECT_EQ(actual, "do:64e1f15da6a579ad\nundo:64e1f15da6a579ad\ndo:windows-client\nundo:windows-client\ndo:\nundo:\n");
}
#endif
