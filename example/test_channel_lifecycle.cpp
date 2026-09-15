#include <cmw/common/global_data.h>
#include <cmw/common/log.h>
#include <cmw/config/RoleAttributes.h>
#include <cmw/discovery/specific_manager/channel_manager.h>
#include <cmw/transport/common/identity.h>
#include <cmw/transport/message/loaned_message.h>
#include <cmw/transport/receiver/hybrid_receiver.h>
#include <cmw/transport/transmitter/hybrid_transmitter.h>
#include <cmw/transport/transmitter/intra_transmitter.h>

#include <gtest/gtest.h>

namespace {

using hnu::cmw::common::GlobalData;
using hnu::cmw::config::RoleAttributes;
using hnu::cmw::config::RoleType;
using hnu::cmw::discovery::ChannelManager;

RoleAttributes MakeEndpoint(const std::string& node_name,
                            const std::string& channel_name,
                            const std::string& message_type) {
  RoleAttributes attr{};
  attr.host_name = GlobalData::Instance()->HostName();
  attr.host_ip = GlobalData::Instance()->HostIp();
  attr.process_id = GlobalData::Instance()->ProcessId();
  attr.node_name = node_name;
  attr.node_id = GlobalData::RegisterNode(node_name);
  attr.channel_name = channel_name;
  attr.channel_id = GlobalData::RegisterChannel(channel_name);
  attr.id = hnu::cmw::transport::Identity().HashValue();
  attr.message_type = message_type;
  return attr;
}

TEST(ChannelLifecycleTest, ReaderLeaveRemovesNodeAndChannelIndexes) {
  ChannelManager manager;
  const RoleAttributes reader =
      MakeEndpoint("reader-node", "reader-channel", "schema@1");
  ASSERT_TRUE(manager.Join(reader, RoleType::ROLE_READER, false));

  std::vector<RoleAttributes> found;
  manager.GetReadersOfNode(reader.node_name, &found);
  ASSERT_EQ(1u, found.size());
  found.clear();
  manager.GetReadersOfChannel(reader.channel_name, &found);
  ASSERT_EQ(1u, found.size());

  // Discovery is intentionally not started; Leave may fail to broadcast, but
  // it must still remove the local indexes before attempting the write.
  EXPECT_FALSE(manager.Leave(reader, RoleType::ROLE_READER));
  found.clear();
  manager.GetReadersOfNode(reader.node_name, &found);
  EXPECT_TRUE(found.empty());
  found.clear();
  manager.GetReadersOfChannel(reader.channel_name, &found);
  EXPECT_TRUE(found.empty());
}

TEST(ChannelLifecycleTest, ConflictingMessageTypeIsNotInserted) {
  ChannelManager manager;
  const RoleAttributes writer =
      MakeEndpoint("writer-node", "typed-channel", "schema@1");
  const RoleAttributes reader =
      MakeEndpoint("reader-node", "typed-channel", "different-schema@1");
  ASSERT_TRUE(manager.Join(writer, RoleType::ROLE_WRITER, false));
  EXPECT_FALSE(manager.Join(reader, RoleType::ROLE_READER, false));

  std::vector<RoleAttributes> found;
  manager.GetReadersOfChannel(reader.channel_name, &found);
  EXPECT_TRUE(found.empty());
  EXPECT_FALSE(manager.HasCompatibleMessageType(reader.channel_name,
                                                reader.message_type));
  EXPECT_TRUE(manager.HasCompatibleMessageType(writer.channel_name,
                                               writer.message_type));
}

TEST(ChannelLifecycleTest, HybridEndpointsRejectMismatchedPeerType) {
  RoleAttributes writer =
      MakeEndpoint("hybrid-writer", "hybrid-type-channel", "schema-a@1");
  RoleAttributes reader =
      MakeEndpoint("hybrid-reader", "hybrid-type-channel", "schema-b@1");

  hnu::cmw::transport::HybridTransmitter<
      hnu::cmw::transport::LoanedMessage> hybrid_writer(writer, nullptr);
  hybrid_writer.Enable(reader);
  EXPECT_EQ(nullptr, hybrid_writer.AcquireLoanedMessage(32));

  int received = 0;
  hnu::cmw::transport::HybridReceiver<hnu::cmw::transport::LoanedMessage>
      hybrid_reader(reader,
          [&received](const std::shared_ptr<hnu::cmw::transport::LoanedMessage>&,
                      const hnu::cmw::transport::MessageInfo&,
                      const RoleAttributes&) { ++received; });
  hybrid_reader.Enable(writer);

  hnu::cmw::transport::IntraTransmitter<hnu::cmw::transport::LoanedMessage>
      direct_writer(writer);
  direct_writer.Enable();
  auto message = direct_writer.AcquireLoanedMessage(32);
  ASSERT_NE(nullptr, message);
  message->set_size(1);
  EXPECT_TRUE(direct_writer.TransmitLoanedMessage(std::move(message)));
  EXPECT_EQ(0, received);
}

}  // namespace

int main(int argc, char** argv) {
  Logger_Init("ChannelLifecycleTest");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
