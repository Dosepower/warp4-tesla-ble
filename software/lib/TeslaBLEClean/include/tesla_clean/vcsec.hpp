// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <tesla_clean/identity.hpp>
namespace tesla_clean {
struct VcsecPacket { std::array<uint8_t,128> bytes{}; size_t length=0; };
enum class SleepState : uint8_t { Unknown, Awake, Asleep };
enum class VcsecKind { Vehicle, Pairing };
enum class VcsecOperation { Success, Waiting, Error };
struct VcsecStatus {
    VcsecKind kind=VcsecKind::Vehicle;
    SleepState sleep=SleepState::Unknown;
    VcsecOperation operation=VcsecOperation::Error;
    uint32_t information=0,outer_operation=0,whitelist_operation=0;
    bool whitelist=false;
};
// Official pinned vcsec.proto:29-36. Form factor is metadata, not permission.
enum class KeyFormFactor : uint8_t { Unknown=0, NfcCard=1, IosDevice=6, AndroidDevice=7, CloudKey=9 };
bool pair_charging_manager(const IdentityLease &identity,VcsecPacket &output,KeyFormFactor form_factor=KeyFormFactor::CloudKey);
Bytes status_payload();
bool wake_payload(bool explicitly_authorized,VcsecPacket &output);
bool decode_vcsec_status(Bytes input,VcsecStatus &output);
// Status only, not an authentication decoder. Routed replies require the exact
// current address and VCSEC source domain; never relax session/HMAC parsing.
bool decode_pairing_reply(Bytes input,Bytes address,VcsecStatus &output);
}
