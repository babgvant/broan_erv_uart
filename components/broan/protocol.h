#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace esphome {
namespace broan {

inline uint8_t packet_checksum(uint8_t sender, uint8_t target, const std::vector<uint8_t> &payload) {
  uint8_t sum = static_cast<uint8_t>(1 + target + sender + 1 + payload.size());
  for (auto byte : payload) sum += byte;
  return static_cast<uint8_t>(1 - sum);
}

// Bounded incremental framing shared by active and passive transports.
class PacketFramer {
 public:
  uint32_t packets{0}, valid{0}, invalid{0}, checksum_failures{0};

  template<typename Callback> void feed(uint8_t byte, uint32_t now, Callback callback) {
    expire(now, callback);
    buffer_.push_back(byte);
    last_byte_ = now;
    drain(callback);
  }

  template<typename Callback> void expire(uint32_t now, Callback callback) {
    if (!buffer_.empty() && static_cast<uint32_t>(now - last_byte_) >= 250) {
      // A truncated frame must not hide later complete frames indefinitely.
      ++packets;
      ++invalid;
      while (!buffer_.empty()) {
        buffer_.erase(buffer_.begin());
        drain(callback);
      }
    }
  }

 private:
  std::vector<uint8_t> buffer_;
  uint32_t last_byte_{0};

  template<typename Callback> void drain(Callback callback) {
    while (!buffer_.empty()) {
      if (buffer_[0] != 1) { buffer_.erase(buffer_.begin()); continue; }
      if (buffer_.size() < 5) return;
      if (buffer_[3] != 1 || buffer_[4] == 0) {
        ++packets; ++invalid;
        buffer_.erase(buffer_.begin());
        continue;
      }
      const size_t size = static_cast<size_t>(buffer_[4]) + 7;
      if (buffer_.size() < size) return;
      ++packets;
      std::vector<uint8_t> payload(buffer_.begin() + 5, buffer_.begin() + size - 2);
      if (buffer_[size - 2] != packet_checksum(buffer_[2], buffer_[1], payload)) {
        ++invalid; ++checksum_failures;
        buffer_.erase(buffer_.begin());
        continue;
      }
      if (buffer_[size - 1] != 4) {
        ++invalid;
        buffer_.erase(buffer_.begin());
        continue;
      }
      ++valid;
      callback(buffer_[2], buffer_[1], payload);
      buffer_.erase(buffer_.begin(), buffer_.begin() + size);
    }
  }
};

inline bool valid_register_payload(const std::vector<uint8_t> &payload) {
  size_t offset = 1;
  while (offset < payload.size()) {
    if (payload.size() - offset < 3) return false;
    size_t length = payload[offset + 2];
    offset += 3;
    if (length > payload.size() - offset) return false;
    offset += length;
  }
  return !payload.empty();
}

}  // namespace broan
}  // namespace esphome
