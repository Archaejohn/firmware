#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_PAGERUI

#include "MessageStore.h"
#include "concurrency/OSThread.h"
#include "mesh/SinglePortModule.h"

namespace PagerUI
{

/// Tracks delivery of messages we sent, and writes the outcome back onto the message that
/// caused it.
///
/// This replaces the single-outstanding-request scheme in CannedMessageModule, which held one
/// `lastRequestId` and applied the result to whatever message happened to be newest. That is
/// correct only while nothing arrives between a send and its ACK.
///
/// Runs on the main loop (OSThread), never on the UI task - it mutates the message store, and
/// the UI only reads it.
class AckTracker : public SinglePortModule, private concurrency::OSThread
{
  public:
    AckTracker();

    /// Start watching for the outcome of `packetId`, sent to `dest`. `packet` is used only to
    /// size the timeout against the airtime this send will actually take, and is not retained.
    void track(uint32_t packetId, NodeNum dest, const meshtastic_MeshPacket *packet);

    /// How many sends are still outstanding.
    size_t pendingCount() const;

  protected:
    bool wantPacket(const meshtastic_MeshPacket *p) override { return p->decoded.portnum == meshtastic_PortNum_ROUTING_APP; }
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    int32_t runOnce() override;

  private:
    // A handful is plenty: these are messages the user typed, not mesh traffic.
    static constexpr size_t kMaxPending = 8;

    struct Pending {
        uint32_t packetId;
        NodeNum dest;
        uint32_t deadlineMs;
        bool relayed; // saw a relay ACK; still hoping for the destination
    };

    Pending pending[kMaxPending] = {};
    size_t pendingUsed = 0;

    void forget(size_t index);
};

extern AckTracker *ackTracker;

} // namespace PagerUI

#endif // MESHTASTIC_INCLUDE_PAGERUI
