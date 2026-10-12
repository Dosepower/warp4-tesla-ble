// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <tesla_clean/identity.hpp>
namespace tesla_clean {
struct VcsecPacket { std::array<uint8_t,128> bytes{}; size_t length=0; };
enum class SleepState : uint8_t { Unknown, Awake, Asleep };
enum class VcsecKind { Vehicle, Pairing };
enum class VcsecOperation { Success, Waiting, Error };
struct VcsecStatus { VcsecKind kind=VcsecKind::Vehicle; SleepState sleep=SleepState::Unknown; VcsecOperation operation=VcsecOperation::Error; uint32_t information=0; };
bool pair_charging_manager(const IdentityLease &identity,VcsecPacket &output);
Bytes status_payload();
bool wake_payload(bool explicitly_authorized,VcsecPacket &output);
bool decode_vcsec_status(Bytes input,VcsecStatus &output);
}
