#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_PAGERUI

#include "AckTracker.h"

#include "DebugConfiguration.h"
#include "NodeDB.h"
#include "mesh/NextHopRouter.h"
#include "mesh/Router.h"
#include "mesh/Throttle.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

namespace PagerUI
{

AckTracker *ackTracker = nullptr;

// Slack on top of the router's own retry window, to cover the responder's turnaround and a
// little scheduling jitter. Only reached when nothing at all comes back.
static constexpr uint32_t kTimeoutSlackMs = 2000;

AckTracker::AckTracker() : SinglePortModule("chatack", meshtastic_PortNum_ROUTING_APP), concurrency::OSThread("chatack") {}

void AckTracker::track(uint32_t packetId, NodeNum dest, const meshtastic_MeshPacket *packet)
{
    if (packetId == 0)
        return;

    // Arm the timeout past the point where the router itself gives up, so a message is never
    // shown as failed while a retransmission is still in flight.
    uint32_t window = 5000;
    if (packet && router)
        window = NextHopRouter::NUM_RELIABLE_UNICAST_ATTEMPTS * router->getRetransmissionMsec(packet);

    if (pendingUsed == kMaxPending) {
        // Oldest first: it is the one closest to timing out anyway, and dropping it only
        // means its status stays NONE rather than becoming wrong.
        LOG_DEBUG("AckTracker: pending table full, forgetting 0x%08x", pending[0].packetId);
        forget(0);
    }

    pending[pendingUsed++] = Pending{packetId, dest, millis() + window + kTimeoutSlackMs, false};
}

size_t AckTracker::pendingCount() const
{
    return pendingUsed;
}

void AckTracker::forget(size_t index)
{
    if (index >= pendingUsed)
        return;
    for (size_t i = index; i + 1 < pendingUsed; ++i)
        pending[i] = pending[i + 1];
    pendingUsed--;
}

ProcessMessage AckTracker::handleReceived(const meshtastic_MeshPacket &mp)
{
    // MeshModule::callModules only delivers decoded packets addressed to us, so there is no
    // need for the "am I expecting anything" latch the old implementation carried.
    const uint32_t requestId = mp.decoded.request_id;
    if (requestId == 0)
        return ProcessMessage::CONTINUE;

    for (size_t i = 0; i < pendingUsed; ++i) {
        if (pending[i].packetId != requestId)
            continue;

        meshtastic_Routing decoded = meshtastic_Routing_init_default;
        pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, meshtastic_Routing_fields, &decoded);

        const bool isAck = (decoded.error_reason == meshtastic_Routing_Error_NONE);
        const bool wasBroadcast = (pending[i].dest == NODENUM_BROADCAST);
        const bool fromDest = (mp.from == pending[i].dest);

        if (!isAck) {
            messageStore.setAckStatus(requestId, AckStatus::NACKED);
            LOG_DEBUG("AckTracker: 0x%08x failed (reason %d)", requestId, (int)decoded.error_reason);
            forget(i);
        } else if (wasBroadcast || fromDest) {
            messageStore.setAckStatus(requestId, AckStatus::ACKED);
            forget(i);
        } else {
            // Someone relayed it onward. That is real progress and worth showing, but it is
            // not delivery - so unlike the old implementation we keep waiting, which is what
            // lets a relayed message still reach ACKED when the destination answers.
            pending[i].relayed = true;
            messageStore.setAckStatus(requestId, AckStatus::RELAYED);
        }

        return ProcessMessage::CONTINUE;
    }

    return ProcessMessage::CONTINUE;
}

int32_t AckTracker::runOnce()
{
    // One "now" for the whole sweep, which is what deadlinePassedAt is for. Throttle rather
    // than a bare millis() comparison so this stays correct across the 32-bit wrap; none of
    // these deadlines can be the 0 or UINT32_MAX sentinel, since every one is built as
    // millis() + a non-zero window.
    const uint32_t now = millis();

    for (size_t i = 0; i < pendingUsed;) {
        if (Throttle::deadlinePassedAt(now, pending[i].deadlineMs)) {
            // A relay ACK already told us it got somewhere; downgrading that to TIMEOUT would
            // lose information, so only a message that was never seen at all times out.
            if (!pending[i].relayed) {
                messageStore.setAckStatus(pending[i].packetId, AckStatus::TIMEOUT);
                LOG_DEBUG("AckTracker: 0x%08x timed out", pending[i].packetId);
            }
            forget(i);
        } else {
            i++;
        }
    }

    return 1000;
}

} // namespace PagerUI

#endif // MESHTASTIC_INCLUDE_PAGERUI
