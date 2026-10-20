#pragma once

enum class TeslaBleForgetResult { deleted, failed, cancelled };

// Worker-only transaction. Transport/protocol are detached before touching
// persisted material; reconstruction is a separate, cancellation-checked stage.
template <typename Cancel, typename Disconnect, typename Discard, typename Erase, typename Rebuild>
TeslaBleForgetResult tesla_ble_forget(Cancel cancel, Disconnect disconnect, Discard discard, Erase erase, Rebuild rebuild)
{
    if (cancel()) { return TeslaBleForgetResult::cancelled; }
    disconnect();
    if (cancel()) { return TeslaBleForgetResult::cancelled; }
    discard();
    if (cancel()) { return TeslaBleForgetResult::cancelled; }
    const bool removed = erase();
    if (!removed) { return TeslaBleForgetResult::failed; }
    if (cancel()) { return TeslaBleForgetResult::cancelled; }
    rebuild();
    return TeslaBleForgetResult::deleted;
}
