#pragma once
// Tesla-owned checked string update (T1r2). Replaces the former core member
// Config::updateStringChecked so config.cpp/config.h stay official.
// Uses only public Config members: asString() (type-checked, aborts on wrong
// type exactly like Config::get), Config::value (public; upstream modules such
// as charge_tracker/api set value.updated directly), the public ConfVariant
// union member val.s and the public Config::ConfString::getVal().
//
// Semantics (identical to the T1 core helper):
//   nullptr            -> false, nothing touched
//   unchanged          -> true, update flags untouched
//   changed            -> true, value replaced, value.updated = 0xFF
//   OOM on candidate   -> false, prior valid string and update flags retained
// The only allocation is the candidate construction; String move-assignment
// (ESP32 WString::move) either copies into existing capacity or transfers
// ownership and never allocates. Deliberately NOT update_value<String>/
// updateString(), which copy by value first and invalidate on OOM.
#include "config.h"
#include <utility>

inline bool tesla_update_string_checked(Config &cfg, const char *val)
{
    if (val == nullptr) {
        return false;
    }
    const CoolString &current = cfg.asString(); // type check (abort) as Config::get<ConfString>
    if (current == val) {
        return true;
    }
    String candidate(val);
    if (!candidate) {
        return false;
    }
    CoolString *target = cfg.value.val.s.getVal(); // tag verified STRING by asString() above
    static_cast<String &>(*target) = std::move(candidate);
    cfg.value.updated = 0xFF;
    return true;
}

// Callers pass Config::get(...) results. The official Config::Wrap exposes only
// operator->() and an explicit operator Config*() (no operator*), so convert via
// that supported explicit conversion. A null wrapper (get_or_null) is refused.
inline bool tesla_update_string_checked(Config::Wrap cfg, const char *val)
{
    Config *target = static_cast<Config *>(cfg);
    if (target == nullptr) {
        return false;
    }
    return tesla_update_string_checked(*target, val);
}
