/**
 * @file tests/integration/test_launch_client_environment.cpp
 * @brief Preserve the launch request client identity in lifecycle hooks.
 */
#include "../tests_common.h"
#include "src/nvhttp.h"
#include "src/process.h"

#include <optional>

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

TEST(LaunchClientEnvironmentTest, EachLaunchReplacesTheHookEnvironmentOwner) {
  boost::process::v1::environment env = boost::this_process::environment();
  env["SUNSHINE_CLIENT_UUID"] = "stale-inherited-owner";
  proc::ctx_t app {};
  app.id = "1";
  app.name = "Desktop";
  proc::proc_t process {std::move(env), {app}};
  for (const auto &id : {"android-client", "windows-client", ""}) {
    ASSERT_EQ(process.execute(1, launch_request(id)), 0);
    EXPECT_EQ(process.get_env().at("SUNSHINE_CLIENT_UUID").to_string(), id);
    process.terminate();
  }
}
