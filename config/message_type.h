#ifndef CMW_CONFIG_MESSAGE_TYPE_H_
#define CMW_CONFIG_MESSAGE_TYPE_H_

#include <cstdint>
#include <string>

namespace hnu {
namespace cmw {
namespace config {

// Applications may specialize this trait with a stable schema name and a
// positive schema version. The default deliberately has no stable identity.
template <typename MessageT>
struct MessageTypeTrait {
  static const char* Name() { return nullptr; }
  static uint32_t Version() { return 0; }
};

namespace message_type_internal {

template <typename MessageT>
inline const char* AbiTypeSignature() {
#if defined(__clang__) || defined(__GNUC__)
  return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
  return __FUNCSIG__;
#else
  return "unknown-message-type";
#endif
}

inline std::string CompilerAbiTag() {
#if defined(__clang__)
  return "clang-" + std::to_string(__clang_major__) + "." +
         std::to_string(__clang_minor__);
#elif defined(__GNUC__)
  return "gcc-" + std::to_string(__GNUC__) + "." +
         std::to_string(__GNUC_MINOR__);
#elif defined(_MSC_VER)
  return "msvc-" + std::to_string(_MSC_VER);
#else
  return "unknown-compiler";
#endif
}

}  // namespace message_type_internal

template <typename MessageT>
inline std::string MessageTypeIdentifier(
    const std::string& caller_provided = std::string()) {
  const char* const stable_name = MessageTypeTrait<MessageT>::Name();
  const uint32_t stable_version = MessageTypeTrait<MessageT>::Version();
  const bool has_stable_name = stable_name != nullptr && stable_name[0] != '\0';
  if (has_stable_name && stable_version != 0) {
    return "cmw.schema/" + std::string(stable_name) + "@" +
           std::to_string(stable_version);
  }
  if (has_stable_name || stable_version != 0) {
    return std::string();
  }
  // Preserve an explicitly supplied legacy identifier. Its stability remains
  // the caller's responsibility because it has no separately declared version.
  if (!caller_provided.empty()) {
    return caller_provided;
  }
  // This fallback is only an ABI/build compatibility guard. It is intentionally
  // compiler-labelled and must not be treated as a portable schema identity.
  return "cmw.abi/" + message_type_internal::CompilerAbiTag() + "/" +
         message_type_internal::AbiTypeSignature<MessageT>();
}

inline bool IsMessageTypeCompatible(const std::string& lhs,
                                    const std::string& rhs) {
  return !lhs.empty() && lhs == rhs;
}

}  // namespace config
}  // namespace cmw
}  // namespace hnu

#endif
