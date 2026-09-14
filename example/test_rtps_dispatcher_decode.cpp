#include <gtest/gtest.h>
#include <cmw/transport/dispatcher/rtps_dispatcher.h>

namespace hnu {
namespace cmw {
namespace transport {
// Exercise the production adapters and dispatcher routing without DDS sockets.
class RtpsDispatcherTestAccess {
 public:
  static std::unique_ptr<RtpsDispatcher> Make() {
    return std::unique_ptr<RtpsDispatcher>(new RtpsDispatcher());
  }
  template <typename T>
  static void Attach(RtpsDispatcher* dispatcher, const RoleAttributes& self,
                     const RoleAttributes& peer, const MessageListener<T>& callback,
                     bool filtered) {
    if (filtered) dispatcher->AddListenerImpl(self, peer, callback, std::false_type{});
    else dispatcher->AddListenerImpl(self, callback, std::false_type{});
  }
  static void Deliver(RtpsDispatcher* dispatcher, uint64_t channel,
                      const std::shared_ptr<std::string>& payload,
                      const MessageInfo& info) {
    dispatcher->OnMessage(channel, payload, info);
  }
};
namespace {
using serialize::DataStream;
struct Message : serialize::Serializable {
  uint64_t sequence = 0;
  std::string body;
  SERIALIZE(sequence, body)
};

TEST(RtpsDispatcherDecode, BothAdaptersDropMalformedMessagesAndRecover) {
  for (bool filtered : {false, true}) {
    SCOPED_TRACE(filtered ? "peer-specific" : "channel-wide");
    auto dispatcher = RtpsDispatcherTestAccess::Make();
    RoleAttributes self{}, peer{};
    self.channel_name = "decode_regression";
    self.channel_id = common::GlobalData::RegisterChannel(self.channel_name);
    self.id = 17;
    Identity sender;
    peer.id = sender.HashValue();
    MessageInfo info(sender, 91);
    unsigned callbacks = 0;
    MessageListener<Message> listener = [&](const std::shared_ptr<Message>& msg,
                                            const MessageInfo& actual_info) {
      ++callbacks;
      EXPECT_EQ(msg->sequence, 42u);
      EXPECT_EQ(msg->body, "complete payload");
      EXPECT_EQ(actual_info.seq_num(), 91u);
    };
    RtpsDispatcherTestAccess::Attach(dispatcher.get(), self, peer, listener, filtered);
    auto deliver = [&](const std::shared_ptr<std::string>& payload) {
      RtpsDispatcherTestAccess::Deliver(dispatcher.get(), self.channel_id, payload, info);
    };
    Message message;
    message.sequence = 42;
    message.body = "complete payload";
    DataStream valid;
    valid.write(message);
    const std::string encoded(valid.data(), valid.size());
    deliver(nullptr);
    // Every truncation, including after successfully decoding the first field,
    // must leave the user callback untouched.
    for (size_t size = 0; size < encoded.size(); ++size)
      deliver(std::make_shared<std::string>(encoded.substr(0, size)));
    DataStream wrong_type;
    wrong_type.write(uint32_t{42});
    deliver(std::make_shared<std::string>(wrong_type.data(), wrong_type.size()));
    std::string damaged = encoded;
    damaged[0] = static_cast<char>(DataStream::VECTOR);
    deliver(std::make_shared<std::string>(damaged));
    EXPECT_EQ(callbacks, 0u);
    deliver(std::make_shared<std::string>(encoded));
    // RTPS payload alignment padding is compatible with the ordinary decoder.
    deliver(std::make_shared<std::string>(encoded + std::string(3, '\0')));
    EXPECT_EQ(callbacks, 2u);
  }
}
}
}
}
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  Logger_Init("rtps_dispatcher_decode");
  Logger::Instance()->console(false);
  return RUN_ALL_TESTS();
}
