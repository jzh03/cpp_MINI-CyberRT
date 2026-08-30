// 构建（在 example 目录）：make test_transport_mode_selection
// 运行（在 example 目录）：./build/bin/test_transport_mode_selection

#include <gtest/gtest.h>

#include <cmw/config/RoleAttributes.h>
#include <cmw/config/transport_mode.h>

namespace hnu {
namespace cmw {
namespace config {
namespace {

// 纯元数据单元测试：不启动 SHM 或 FastDDS，只验证 Discovery 属性到模式的映射。
RoleAttributes MakeRoleAttributes(const std::string& host_ip, int32_t process_id) {
  RoleAttributes attr{};
  attr.host_ip = host_ip;
  attr.process_id = process_id;
  return attr;
}

TEST(TransportModeSelectionTest, SameHostAndProcessSelectsIntra) {
  const RoleAttributes local = MakeRoleAttributes("10.0.0.1", 100);
  const RoleAttributes opposite = MakeRoleAttributes("10.0.0.1", 100);
  EXPECT_EQ(OptionalMode::INTRA, SelectMode(local, opposite));
}

TEST(TransportModeSelectionTest, SameHostDifferentProcessSelectsShm) {
  const RoleAttributes local = MakeRoleAttributes("10.0.0.1", 100);
  const RoleAttributes opposite = MakeRoleAttributes("10.0.0.1", 101);
  EXPECT_EQ(OptionalMode::SHM, SelectMode(local, opposite));
}

TEST(TransportModeSelectionTest, DifferentHostSelectsRtpsRegardlessOfProcessId) {
  const RoleAttributes local = MakeRoleAttributes("10.0.0.1", 100);
  const RoleAttributes opposite = MakeRoleAttributes("10.0.0.2", 100);
  EXPECT_EQ(OptionalMode::RTPS, SelectMode(local, opposite));
}

TEST(TransportModeSelectionTest, OnlyHostAndProcessMetadataAffectsSelection) {
  RoleAttributes local = MakeRoleAttributes("10.0.0.1", 100);
  RoleAttributes opposite = MakeRoleAttributes("10.0.0.1", 100);
  local.channel_name = "local_channel";
  local.id = 1;
  opposite.channel_name = "opposite_channel";
  opposite.id = 2;
  EXPECT_EQ(OptionalMode::INTRA, SelectMode(local, opposite));
}

}  // namespace
}  // namespace config
}  // namespace cmw
}  // namespace hnu

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
