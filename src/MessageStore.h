#pragma once

// HAS_MESSAGE_STORE is derived in configuration.h from the UI selection. Include it here
// rather than relying on every includer having pulled it in first: an undefined macro is
// silently 0 to the preprocessor, so getting this wrong would compile the store away
// without a diagnostic.
#include "configuration.h"

#if HAS_MESSAGE_STORE

// Disable debug logging entirely on release builds of HELTEC_MESH_SOLAR for space constraints
#if defined(HELTEC_MESH_SOLAR)
#define LOG_DEBUG(...)
#endif

// Enable or disable message persistence (flash storage)
// Define -DENABLE_MESSAGE_PERSISTENCE=0 in build_flags to disable it entirely
#ifndef ENABLE_MESSAGE_PERSISTENCE
#define ENABLE_MESSAGE_PERSISTENCE 1
#endif

#include "mesh/generated/meshtastic/mesh.pb.h"
#include <cstdint>
#include <deque>
#include <string>

// How many messages are stored (RAM + flash).
// Define -DMESSAGE_HISTORY_LIMIT=N in build_flags to control memory usage.
#ifndef MESSAGE_HISTORY_LIMIT
#if (defined(ARCH_ESP32) &&                                                                                                      \
     !(defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S2))) ||       \
    defined(NRF52840_XXAA)
// Baseline ESP32 (non-PSRAM variants) and nRF52840 (~115 KB heap arena shared with SoftDevice +
// FreeRTOS stacks; 2.8.0 field reports hit 99% use) have limited heap; reduce message history on
// resource-constrained builds. Override with -DMESSAGE_HISTORY_LIMIT=N if needed.
#define MESSAGE_HISTORY_LIMIT 10
#else
#define MESSAGE_HISTORY_LIMIT 20
#endif
#endif

// Internal alias used everywhere in code - do NOT redefine elsewhere.
#define MAX_MESSAGES_SAVED MESSAGE_HISTORY_LIMIT

// There is deliberately no per-thread cap. Eviction picks its victim from whichever thread
// currently holds the most messages (see MessageStore::evictForInsert), which is what stops a
// busy channel from emptying your DMs. A fixed per-thread limit on top of that would only add
// a downside: it would cap a single active conversation well below the space actually free.

// Maximum text payload size per message in bytes.
// This still defines the max message length, but we no longer reserve this space per message.
#define MAX_MESSAGE_SIZE 220

// Total shared text pool size for all messages combined.
// The text pool is RAM-only. Text is re-stored from flash into the pool on boot.
#ifndef MESSAGE_TEXT_POOL_SIZE
#define MESSAGE_TEXT_POOL_SIZE (MAX_MESSAGES_SAVED * MAX_MESSAGE_SIZE)
#endif

// Explicit message classification
enum class MessageType : uint8_t {
    BROADCAST = 0, // broadcast message
    DM_TO_US = 1   // direct message addressed to this node
};

// Delivery status for messages we sent
enum class AckStatus : uint8_t {
    NONE = 0,    // just sent, waiting (no symbol shown)
    ACKED = 1,   // got a valid ACK from destination
    NACKED = 2,  // explicitly failed
    TIMEOUT = 3, // no ACK after retry window
    RELAYED = 4  // got an ACK from relay, not destination
};

struct StoredMessage {
    uint32_t timestamp;   // When message was created (secs since boot or RTC)
    uint32_t sender;      // NodeNum of sender
    uint8_t channelIndex; // Channel index used
    uint32_t dest;        // Destination node (broadcast or direct)
    MessageType type;     // Derived from dest (explicit classification)
    bool isBootRelative;  // true = Time::getUptimeSecs() fallback; false = epoch/RTC absolute
    AckStatus ackStatus;  // Delivery status (only meaningful for our own sent messages)

    // Mesh packet id this message was carried in; 0 when unknown (records written before the
    // field existed, and anything not sourced from a packet). Needed to correlate a routing
    // ACK back to the message that caused it, to recognise our own broadcasts arriving back
    // via Router::sendLocal, and to give the UI a stable identity for a message.
    uint32_t packetId;

    // Text storage metadata - rebuilt from flash at boot
    uint16_t textOffset; // Offset into global text pool (valid only after loadFromFlash())
    uint16_t textLength; // Length of text in bytes

    bool xeddsaSigned; // true if packet carried a verified XEdDSA signature

    // Default constructor initializes all fields safely
    StoredMessage()
        : timestamp(0), sender(0), channelIndex(0), dest(0xffffffff), type(MessageType::BROADCAST), isBootRelative(false),
          ackStatus(AckStatus::NONE), packetId(0), textOffset(0), textLength(0), xeddsaSigned(false)
    {
    }
};

/// Identifier for the conversation a message belongs to.
///
/// Broadcasts group by channel, DMs group by the other party regardless of direction, and the
/// two spaces are kept apart by the top bit so channel 3 and node 3 can never collide.
using ThreadKey = uint64_t;

static constexpr ThreadKey kThreadKeyDmFlag = (ThreadKey)1 << 63;

constexpr ThreadKey threadKeyForChannel(uint8_t channel)
{
    return (ThreadKey)channel;
}

constexpr ThreadKey threadKeyForPeer(uint32_t peer)
{
    return kThreadKeyDmFlag | (ThreadKey)peer;
}

/// Thread a message belongs to, as seen from `localNode`.
/// Free function and header-only so the native tests can exercise it without a NodeDB.
inline ThreadKey threadKeyOf(const StoredMessage &msg, uint32_t localNode)
{
    if (msg.type != MessageType::DM_TO_US)
        return threadKeyForChannel(msg.channelIndex);

    const uint32_t peer = (msg.sender == localNode) ? msg.dest : msg.sender;
    return threadKeyForPeer(peer);
}

class MessageStore
{
  public:
    explicit MessageStore(const std::string &label);

    // Live RAM methods (always current, used by UI and runtime)
    void addLiveMessage(StoredMessage &&msg);
    void addLiveMessage(const StoredMessage &msg); // convenience overload
    const std::deque<StoredMessage> &getLiveMessages() const { return liveMessages; }
    // Add new messages from packets. Returns nullptr if the packet is filtered out.
    const StoredMessage *tryAddFromPacket(const meshtastic_MeshPacket &mp); // Incoming/outgoing -> RAM only

    // Persistence methods (used only on boot/shutdown)
    void saveToFlash();   // Save messages to flash
    void loadFromFlash(); // Load messages from flash

    // Clear all messages (RAM + persisted queue + text pool)
    void clearAllMessages();

    // Delete helpers
    void deleteOldestMessage(); // remove oldest from RAM (and flash on save)
    void deleteOldestMessageInChannel(uint8_t channel);
    void deleteOldestMessageWithPeer(uint32_t peer);
    void deleteAllMessagesInChannel(uint8_t channel);
    void deleteAllMessagesWithPeer(uint32_t peer);
    void deleteAllMessagesFromNode(uint32_t nodeNum);
    // Unified accessor (for UI code, defaults to RAM buffer)
    const std::deque<StoredMessage> &getMessages() const { return liveMessages; }
    bool hasVisibleMessages() const;

    // Helper filters for future use
    std::deque<StoredMessage> getChannelMessages(uint8_t channel) const; // Only broadcast messages on a channel
    std::deque<StoredMessage> getDirectMessages() const;                 // Only direct messages
    bool shouldStorePacket(const meshtastic_MeshPacket &mp) const;
    bool isMessageVisible(const StoredMessage &msg) const;

    // Upgrade boot-relative timestamps once RTC is valid
    void upgradeBootRelativeTimestamps();

    // Retrieve the C-string text for a stored message
    static const char *getText(const StoredMessage &msg);

    // Copy text into this store's pool, reclaiming or evicting as needed, and return its
    // offset. Note this can evict old messages, so take the offset before pushing the
    // message that owns it.
    uint16_t allocText(const char *src, size_t len);

    // Allocate text into pool (used by sender-side code). Kept static for existing callers;
    // routes to the global instance.
    static uint16_t storeText(const char *src, size_t len);

    // Find a message by the mesh packet id it was carried in. Returns nullptr if unknown, and
    // never matches packetId 0 (which means "not recorded").
    const StoredMessage *findByPacketId(uint32_t packetId) const;

    // Update delivery status for the message carried by `packetId`. Returns false when no such
    // message is held, which is normal once a send has aged out of history.
    bool setAckStatus(uint32_t packetId, AckStatus status);

    // Thread a message belongs to, resolved against this node's own number.
    ThreadKey threadKeyOf(const StoredMessage &msg) const;

    // Number of messages currently held for a thread.
    size_t countInThread(ThreadKey key) const;

    // Reclaim text-pool space left behind by deleted messages, rewriting the offsets of the
    // messages that remain. Called automatically when the pool fills; public so tests (and a
    // UI that has just bulk-deleted a thread) can force it.
    void compactTextPool();

    // Bytes of the text pool currently in use.
    static size_t textPoolBytesUsed();

  private:
    bool pruneHiddenMessages();

    // Make room for one more message, respecting the per-thread cap.
    void evictForInsert();

    std::deque<StoredMessage> liveMessages; // Single in-RAM message buffer (also used for persistence)
    std::string filename;                   // Flash filename for persistence
};

#if ENABLE_MESSAGE_PERSISTENCE
// Called periodically from main loop to trigger time based autosave
void messageStoreAutosaveTick();
#endif

// Global instance (defined in MessageStore.cpp)
extern MessageStore messageStore;

#endif
