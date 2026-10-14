/* SPDX-License-Identifier: GPL-3.0-only; upstream sonicpic/gcores-pocket-player */
#pragma once

#include <cstdint>

namespace gpp {

enum class RecoveryDecision : uint8_t { None, WaitForNetwork, Retry, GiveUp };

// Pure, host-testable policy for deciding when a decoder/network stall should
// become a reconnect. AudioEngine owns the actual network and decoder work.
class RecoveryPolicy {
 public:
  static constexpr uint32_t kNetworkStallMs = 12000;
  static constexpr uint32_t kLocalStallMs = 8000;
  static constexpr uint32_t kStoppedDecoderMs = 1500;
  static constexpr uint32_t kRetryGapMs = 8000;
  static constexpr uint32_t kStablePlaybackResetMs = 30000;
  static constexpr uint8_t kMaxAttempts = 3;

  static RecoveryDecision decide(bool localPlayback, bool networkConnected,
                                 bool decoderRunning, uint32_t noProgressMs,
                                 uint32_t sinceLastAttemptMs, uint8_t attempts) {
    const uint32_t stallThreshold = decoderRunning
                                        ? (localPlayback ? kLocalStallMs : kNetworkStallMs)
                                        : kStoppedDecoderMs;
    if (noProgressMs < stallThreshold) return RecoveryDecision::None;
    if (!localPlayback && !networkConnected) return RecoveryDecision::WaitForNetwork;
    if (attempts > 0 && sinceLastAttemptMs < kRetryGapMs) return RecoveryDecision::None;
    if (attempts >= kMaxAttempts) return RecoveryDecision::GiveUp;
    return RecoveryDecision::Retry;
  }
};

}  // namespace gpp
