#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_PAGERUI

#include "ChatStore.h"

#include "AckTracker.h"
#include "DebugConfiguration.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "UptimeClock.h"
#include "gps/RTC.h"
#include "mesh/Router.h"
#include "modules/TextMessageModule.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace PagerUI
{

ChatStore chatStore;

void ChatStore::begin()
{
    if (!textMessageModule) {
        LOG_ERROR("PagerUI: no TextMessageModule - incoming messages will not be stored");
        return;
    }

    // TextMessageModule normally writes to messageStore itself, but that call sits inside
    // IF_SCREEN and compiles away at HAS_SCREEN 0. Its notifyObservers() runs unconditionally
    // though, so ingest happens here instead - the same approach InkHUD takes.
    textObserver.observe(textMessageModule);

    // Seed thread state from whatever was restored from flash, treating it all as read: the
    // alternative is a device that boots claiming everything is new.
    for (const auto &m : messageStore.getLiveMessages()) {
        Thread &t = stateFor(messageStore.threadKeyOf(m));
        if (m.timestamp > t.lastReadTimestamp)
            t.lastReadTimestamp = m.timestamp;
    }
    recount();
}

int ChatStore::onTextMessage(const meshtastic_MeshPacket *packet)
{
    if (!packet)
        return 0;

    // Router::sendLocal delivers our own broadcasts back to us, so an outgoing broadcast
    // arrives here after we already stored it at send time. Without the id we could not tell
    // that apart from a genuine incoming message and every broadcast would appear twice.
    if (packet->id && messageStore.findByPacketId(packet->id)) {
        LOG_DEBUG("PagerUI: ignoring loopback of our own 0x%08x", packet->id);
        return 0;
    }

    const StoredMessage *stored = messageStore.tryAddFromPacket(*packet);
    if (stored) {
        Thread &t = stateFor(messageStore.threadKeyOf(*stored));
        if (t.unread < UINT16_MAX)
            t.unread++;
        recount();
    }

    // Must return 0: Observable::notifyObservers aborts the chain on the first non-zero
    // return, and everything else observing text messages is downstream of us.
    return 0;
}

uint32_t ChatStore::send(NodeNum dest, ChannelIndex channel, const char *text)
{
    if (!text || !*text || !router || !service)
        return 0;

    meshtastic_MeshPacket *p = router->allocForSending();
    if (!p) {
        LOG_ERROR("PagerUI: no packet available to send");
        return 0;
    }

    p->to = dest;
    p->channel = channel;
    p->want_ack = true;
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p->decoded.dest = dest;

    // PKI upgrade, matching CannedMessageModule::sendText. Skipping this is not a small
    // difference: without it a DM to a peer whose key we hold goes out under channel
    // encryption, readable by everyone else holding that channel's PSK.
    const NodeNum me = nodeDB->getNodeNum();
    if (dest != NODENUM_BROADCAST && dest != me) {
        const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(dest);
        if (node && nodeInfoLiteHasUser(node) && node->public_key.size == 32) {
            p->pki_encrypted = true;
            p->channel = 0;
        }
    }

    size_t len = strnlen(text, MAX_MESSAGE_SIZE - 1);
    if (len > meshtastic_Constants_DATA_PAYLOAD_LEN)
        len = meshtastic_Constants_DATA_PAYLOAD_LEN;
    p->decoded.payload.size = len;
    memcpy(p->decoded.payload.bytes, text, len);

    // Read the id before handing the packet over: sendToMesh returns it to the pool, and the
    // memory may be reused before the next statement runs.
    const uint32_t packetId = p->id;

    StoredMessage sm;
    sm.sender = me;
    sm.dest = dest;
    sm.channelIndex = channel;
    sm.type = (dest != 0 && dest != NODENUM_BROADCAST) ? MessageType::DM_TO_US : MessageType::BROADCAST;
    sm.packetId = packetId;
    sm.ackStatus = AckStatus::NONE;
    sm.textLength = (uint16_t)len;
    sm.textOffset = messageStore.allocText(text, len);

    const uint32_t nowSecs = getValidTime(RTCQuality::RTCQualityDevice, true);
    sm.timestamp = nowSecs ? nowSecs : Time::getUptimeSecs();
    sm.isBootRelative = (nowSecs == 0);

    messageStore.addLiveMessage(std::move(sm));

    // Our own message counts as read; otherwise sending would light up an unread badge on the
    // conversation we are looking at.
    Thread &t = stateFor(dest == NODENUM_BROADCAST ? threadKeyForChannel(channel) : threadKeyForPeer(dest));
    if (t.lastReadTimestamp < sm.timestamp)
        t.lastReadTimestamp = sm.timestamp;

    if (ackTracker)
        ackTracker->track(packetId, dest, p);

    service->sendToMesh(p, RX_SRC_LOCAL, true);
    recount();

    LOG_INFO("PagerUI: sent 0x%08x to 0x%08x (%u B)", packetId, dest, (unsigned)len);
    return packetId;
}

ChatStore::Thread &ChatStore::stateFor(ThreadKey key)
{
    for (auto &t : threadState) {
        if (t.key == key)
            return t;
    }
    threadState.push_back(Thread{key, 0, 0});
    return threadState.back();
}

uint16_t ChatStore::unreadFor(ThreadKey key) const
{
    for (const auto &t : threadState) {
        if (t.key == key)
            return t.unread;
    }
    return 0;
}

void ChatStore::recount()
{
    uint32_t total = 0;
    for (const auto &t : threadState)
        total += t.unread;

    totalUnread.store((uint16_t)(total > UINT16_MAX ? UINT16_MAX : total), std::memory_order_relaxed);
    rev.fetch_add(1, std::memory_order_relaxed);
}

void ChatStore::markRead(ThreadKey key)
{
    Thread &t = stateFor(key);
    if (t.unread == 0)
        return;

    t.unread = 0;
    for (const auto &m : messageStore.getLiveMessages()) {
        if (messageStore.threadKeyOf(m) == key && m.timestamp > t.lastReadTimestamp)
            t.lastReadTimestamp = m.timestamp;
    }
    recount();
}

std::vector<ChatStore::Thread> ChatStore::threads() const
{
    // Derive the list from the messages actually held, rather than from threadState: a thread
    // whose last message has been evicted should disappear from the conversation list, not
    // linger as an empty row.
    std::vector<Thread> out;
    std::vector<uint32_t> newest;

    for (const auto &m : messageStore.getLiveMessages()) {
        if (!messageStore.isMessageVisible(m))
            continue;

        const ThreadKey key = messageStore.threadKeyOf(m);
        size_t at = out.size();
        for (size_t i = 0; i < out.size(); ++i) {
            if (out[i].key == key) {
                at = i;
                break;
            }
        }
        if (at == out.size()) {
            uint32_t lastRead = 0;
            for (const auto &s : threadState) {
                if (s.key == key) {
                    lastRead = s.lastReadTimestamp;
                    break;
                }
            }
            out.push_back(Thread{key, lastRead, unreadFor(key)});
            newest.push_back(m.timestamp);
        } else if (m.timestamp > newest[at]) {
            newest[at] = m.timestamp;
        }
    }

    // Most recent activity first - the order a chat list is expected to be in.
    std::vector<size_t> order(out.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return newest[a] > newest[b]; });

    std::vector<Thread> sorted;
    sorted.reserve(out.size());
    for (size_t i : order)
        sorted.push_back(out[i]);
    return sorted;
}

} // namespace PagerUI

#endif // MESHTASTIC_INCLUDE_PAGERUI
