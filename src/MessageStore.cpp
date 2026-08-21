#include "configuration.h"
#if HAS_MESSAGE_STORE
#include "FSCommon.h"
#include "MessageStore.h"
#include "NodeDB.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "Throttle.h"
#include "UptimeClock.h"
#include "gps/RTC.h"
#include "memory/MemAudit.h"
#include <cstring> // memcpy
#include <utility>
#include <vector>

#if ENABLE_MESSAGE_PERSISTENCE
// Defined further down with the rest of the autosave bookkeeping; needed earlier by
// setAckStatus, which dirties the store like any other mutation.
static inline void markMessageStoreUnsaved();
#endif

#ifndef MESSAGE_TEXT_POOL_SIZE
#define MESSAGE_TEXT_POOL_SIZE (MAX_MESSAGES_SAVED * MAX_MESSAGE_SIZE)
#endif

// Default autosave interval 2 hours, override per device later with -DMESSAGE_AUTOSAVE_INTERVAL_SEC=300 (etc)
#ifndef MESSAGE_AUTOSAVE_INTERVAL_SEC
#define MESSAGE_AUTOSAVE_INTERVAL_SEC (2 * 60 * 60)
#endif

// Global message text pool and state
static char *g_messagePool = nullptr;
static size_t g_poolWritePos = 0;

// Reset pool (called on boot or clear)
static inline void resetMessagePool()
{
    if (!g_messagePool) {
        g_messagePool = static_cast<char *>(malloc(MESSAGE_TEXT_POOL_SIZE));
        if (!g_messagePool) {
            LOG_ERROR("MessageStore: Failed to allocate %d bytes for message pool", MESSAGE_TEXT_POOL_SIZE);
            memaudit::set("msgstore", 0);
            return;
        }
        memaudit::set("msgstore", MESSAGE_TEXT_POOL_SIZE);
    }
    g_poolWritePos = 0;
    memset(g_messagePool, 0, MESSAGE_TEXT_POOL_SIZE);
}

// Offsets are stored in a uint16_t, so a pool larger than 64 KiB would silently truncate them
// and hand back text belonging to a different message.
static_assert(MESSAGE_TEXT_POOL_SIZE <= 65536, "MESSAGE_TEXT_POOL_SIZE must fit StoredMessage::textOffset (uint16_t)");

// Retrieve a const pointer to message text by offset
static inline const char *getTextFromPool(uint16_t offset)
{
    if (!g_messagePool || offset >= MESSAGE_TEXT_POOL_SIZE)
        return "";
    return &g_messagePool[offset];
}

// Bytes one message occupies in the pool, including its terminator.
static inline size_t poolSpanOf(const StoredMessage &m)
{
    return (size_t)m.textLength + 1;
}

static inline bool isIgnoredNodeNum(uint32_t nodeNum)
{
    if (nodeNum == 0 || nodeNum == NODENUM_BROADCAST)
        return false;

    const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeNum);
    return nodeInfoLiteIsIgnored(node);
}

// Helper: assign a timestamp (RTC if available, else boot-relative)
static inline void assignTimestamp(StoredMessage &sm)
{
    uint32_t nowSecs = getValidTime(RTCQuality::RTCQualityDevice, true);
    if (nowSecs) {
        sm.timestamp = nowSecs;
        sm.isBootRelative = false;
    } else {
        // Uptime seconds, not millis()/1000: a stamp taken before the 32-bit wrap otherwise reads as
        // newer than "now" afterwards, and upgradeBootRelativeTimestamps() then declines to heal it.
        sm.timestamp = Time::getUptimeSecs();
        sm.isBootRelative = true;
    }
}

// (The old pushWithLimit helpers are gone: capping now goes through
// MessageStore::evictForInsert(), which also honours the per-thread limit.)

MessageStore::MessageStore(const std::string &label)
{
    filename = "/Messages_" + label + ".msgs";
    resetMessagePool(); // initialize text pool on boot
}

size_t MessageStore::textPoolBytesUsed()
{
    return g_poolWritePos;
}

void MessageStore::compactTextPool()
{
    if (!g_messagePool)
        return;

    // Text is allocated strictly in deque order and never wraps, so each message's offset is
    // greater than or equal to the compacted position of everything before it. That makes a
    // single forward pass safe in place - the destination never runs ahead of the source.
    size_t write = 0;
    for (auto &m : liveMessages) {
        const size_t span = poolSpanOf(m);

        // Defensive: a record from a firmware whose pool wrapped, or a corrupt load, could
        // carry an offset that no longer addresses its own text. Drop the text rather than
        // read past the pool or hand back someone else's message.
        if ((size_t)m.textOffset + span > MESSAGE_TEXT_POOL_SIZE) {
            m.textOffset = 0;
            m.textLength = 0;
            continue;
        }

        if (write != m.textOffset)
            memmove(&g_messagePool[write], &g_messagePool[m.textOffset], span);
        m.textOffset = (uint16_t)write;
        write += span;
    }

    g_poolWritePos = write;
}

uint16_t MessageStore::allocText(const char *src, size_t len)
{
    // Pool allocation can fail at boot; getTextFromPool() already maps offset 0 to "" in that case
    if (!g_messagePool)
        return 0;

    if (len >= MAX_MESSAGE_SIZE)
        len = MAX_MESSAGE_SIZE - 1;
    const size_t need = len + 1;

    if (g_poolWritePos + need > MESSAGE_TEXT_POOL_SIZE) {
        // Reclaim the gaps left by deleted messages first; that alone is often enough.
        compactTextPool();

        if (g_poolWritePos + need > MESSAGE_TEXT_POOL_SIZE) {
            // Still short, so the pool genuinely holds more live text than it has room for.
            // Drop whole messages oldest-first until it fits. This is the one place where the
            // pool, rather than the message cap, decides how much history survives.
            size_t live = g_poolWritePos;
            while (!liveMessages.empty() && live + need > MESSAGE_TEXT_POOL_SIZE) {
                live -= poolSpanOf(liveMessages.front());
                liveMessages.pop_front();
            }
            compactTextPool();
        }

        if (g_poolWritePos + need > MESSAGE_TEXT_POOL_SIZE) {
            // The pool cannot hold even a single message. Misconfiguration, not a runtime
            // condition - refuse rather than loop or scribble past the end.
            LOG_ERROR("MessageStore: text pool (%d B) too small for a %u B message", MESSAGE_TEXT_POOL_SIZE, (unsigned)need);
            return 0;
        }
    }

    const uint16_t offset = (uint16_t)g_poolWritePos;
    memcpy(&g_messagePool[offset], src, len);
    g_messagePool[offset + len] = '\0';
    g_poolWritePos += need;
    return offset;
}

ThreadKey MessageStore::threadKeyOf(const StoredMessage &msg) const
{
    return ::threadKeyOf(msg, nodeDB->getNodeNum());
}

size_t MessageStore::countInThread(ThreadKey key) const
{
    size_t n = 0;
    for (const auto &m : liveMessages) {
        if (threadKeyOf(m) == key)
            n++;
    }
    return n;
}

void MessageStore::evictForInsert()
{
    if (liveMessages.size() < MAX_MESSAGES_SAVED)
        return;

    // Prefer to take the hit from whichever conversation is hogging the store, so a busy
    // channel cannot quietly evict every DM. One pass to tally, resolving our own node number
    // once rather than per comparison.
    const uint32_t localNode = nodeDB->getNodeNum();

    std::vector<std::pair<ThreadKey, uint32_t>> tally;
    tally.reserve(8);
    for (const auto &m : liveMessages) {
        const ThreadKey key = ::threadKeyOf(m, localNode);
        bool found = false;
        for (auto &entry : tally) {
            if (entry.first == key) {
                entry.second++;
                found = true;
                break;
            }
        }
        if (!found)
            tally.emplace_back(key, 1u);
    }

    ThreadKey biggestKey = 0;
    uint32_t biggestCount = 0;
    for (const auto &entry : tally) {
        if (entry.second > biggestCount) {
            biggestCount = entry.second;
            biggestKey = entry.first;
        }
    }

    // Only worth targeting if that thread has more than one message: evicting a thread's last
    // message would make the conversation disappear from the list entirely, which is a worse
    // outcome than trimming a long thread by one.
    if (biggestCount > 1) {
        for (auto it = liveMessages.begin(); it != liveMessages.end(); ++it) {
            if (::threadKeyOf(*it, localNode) == biggestKey) {
                liveMessages.erase(it);
                return;
            }
        }
    }

    liveMessages.pop_front();
}

// Live message handling (RAM only)
void MessageStore::addLiveMessage(StoredMessage &&msg)
{
    evictForInsert();
    liveMessages.emplace_back(std::move(msg));
}
void MessageStore::addLiveMessage(const StoredMessage &msg)
{
    evictForInsert();
    liveMessages.push_back(msg);
}

const StoredMessage *MessageStore::findByPacketId(uint32_t packetId) const
{
    if (packetId == 0)
        return nullptr;

    // Newest first: an ACK almost always refers to something sent moments ago.
    for (auto it = liveMessages.rbegin(); it != liveMessages.rend(); ++it) {
        if (it->packetId == packetId)
            return &(*it);
    }
    return nullptr;
}

bool MessageStore::setAckStatus(uint32_t packetId, AckStatus status)
{
    if (packetId == 0)
        return false;

    for (auto it = liveMessages.rbegin(); it != liveMessages.rend(); ++it) {
        if (it->packetId == packetId) {
            it->ackStatus = status;
#if ENABLE_MESSAGE_PERSISTENCE
            markMessageStoreUnsaved();
#endif
            return true;
        }
    }
    return false;
}

#if ENABLE_MESSAGE_PERSISTENCE
static bool g_messageStoreHasUnsavedChanges = false;
static uint32_t g_lastAutoSaveMs = 0; // last time we actually saved

static inline uint32_t autosaveIntervalMs()
{
    uint32_t sec = (uint32_t)MESSAGE_AUTOSAVE_INTERVAL_SEC;
    if (sec < 60)
        sec = 60;
    return sec * 1000UL;
}

// Mark new messages in RAM that need to be saved later
static inline void markMessageStoreUnsaved()
{
    g_messageStoreHasUnsavedChanges = true;

    if (g_lastAutoSaveMs == 0) {
        g_lastAutoSaveMs = Time::getMillis();
    }
}

// Called periodically from the main loop in main.cpp
static inline void autosaveTick(MessageStore *store)
{
    if (!store)
        return;

    uint32_t now = Time::getMillis();

    if (g_lastAutoSaveMs == 0) {
        g_lastAutoSaveMs = now;
        return;
    }

    if (Throttle::isWithinTimespanMs(g_lastAutoSaveMs, autosaveIntervalMs()))
        return;

    // Autosave interval reached, only save if there are unsaved messages.
    if (g_messageStoreHasUnsavedChanges) {
        LOG_INFO("Autosaving MessageStore to flash");
        store->saveToFlash();
    } else {
        LOG_INFO("Autosave skipped, no changes to save");
        g_lastAutoSaveMs = now;
    }
}
#endif

bool MessageStore::shouldStorePacket(const meshtastic_MeshPacket &packet) const
{
    const uint32_t localNode = nodeDB->getNodeNum();
    const bool isDM = packet.to != 0 && packet.to != NODENUM_BROADCAST;
    if (isDM) {
        const bool outgoing = packet.from == 0 || packet.from == localNode;
        const uint32_t peer = outgoing ? packet.to : packet.from;
        return !isIgnoredNodeNum(peer);
    }

    if (packet.from != 0 && packet.from != localNode)
        return !isIgnoredNodeNum(packet.from);

    return true;
}

bool MessageStore::isMessageVisible(const StoredMessage &msg) const
{
    const uint32_t localNode = nodeDB->getNodeNum();
    if (msg.type == MessageType::DM_TO_US) {
        const uint32_t peer = (msg.sender == localNode) ? msg.dest : msg.sender;
        return !isIgnoredNodeNum(peer);
    }

    if (msg.sender != 0 && msg.sender != localNode)
        return !isIgnoredNodeNum(msg.sender);

    return true;
}

// Add from incoming/outgoing packet
const StoredMessage *MessageStore::tryAddFromPacket(const meshtastic_MeshPacket &packet)
{
    if (!shouldStorePacket(packet)) {
        LOG_DEBUG("Drop store 0x%08x", packet.from);
        return nullptr;
    }

    StoredMessage sm;
    assignTimestamp(sm);
    sm.channelIndex = packet.channel;

    const char *payload = reinterpret_cast<const char *>(packet.decoded.payload.bytes);
    // payload.bytes is not NUL-terminated, so bound by the received size too: a shorter message
    // stored after a longer one would otherwise pick up the previous occupant's trailing bytes.
    size_t avail = packet.decoded.payload.size;
    if (avail > MAX_MESSAGE_SIZE - 1)
        avail = MAX_MESSAGE_SIZE - 1;
    size_t len = strnlen(payload, avail);
    sm.textOffset = allocText(payload, len);
    sm.textLength = len;
    sm.packetId = packet.id;

    // Determine sender
    uint32_t localNode = nodeDB->getNodeNum();
    sm.sender = (packet.from == 0) ? localNode : packet.from;

    sm.dest = packet.to;

    bool isDM = (sm.dest != 0 && sm.dest != NODENUM_BROADCAST);

    sm.type = isDM ? MessageType::DM_TO_US : MessageType::BROADCAST;

    // Delivery status only describes messages *we* sent, and nothing renders it for anything
    // else (BaseUI gates the glyphs on isMine). Stamping received traffic ACKED was
    // meaningless, and actively wrong for a DM the phone originated with `from` already set to
    // our own node number - that arrives here unacknowledged but was being marked delivered.
    sm.ackStatus = AckStatus::NONE;

#if !(MESHTASTIC_EXCLUDE_PKI_KEYGEN || MESHTASTIC_EXCLUDE_PKI)
    sm.xeddsaSigned = packet.xeddsa_signed;
#endif

    addLiveMessage(sm);

#if ENABLE_MESSAGE_PERSISTENCE
    markMessageStoreUnsaved();
#endif

    return &liveMessages.back();
}

#if ENABLE_MESSAGE_PERSISTENCE

// ---------------------------------------------------------------------------------------
// On-flash format
//
// v1 was a bare `uint8_t count` followed by that many fixed-size records, with no magic and
// no version. It had to change for two independent reasons:
//
//   * the uint8_t count caps persistence at 255 messages. Above that the count wraps while
//     the writer still emits every record, so the file silently disagrees with itself.
//   * every record carried a fixed char[MAX_MESSAGE_SIZE], so a 20-character message cost
//     220 bytes of flash. That is tolerable for 20 messages and wasteful for several hundred.
//
// v2 puts a magic and a version up front, widens the count, and stores text inline at its
// real length. loadFromFlash() still reads v1 and rewrites it as v2 on the first boot.
//
// Downgrade behaviour is worth stating: firmware that only knows v1 reads our magic's first
// byte as a count, then fails the fixed-size record read almost immediately and loads zero
// messages. Degraded, not corrupt.
// ---------------------------------------------------------------------------------------

static constexpr uint32_t kMessageFileMagic = 0x4753454D; // 'MESG' little-endian
static constexpr uint16_t kMessageFileVersion = 2;

struct __attribute__((packed)) MessageFileHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t recordSize; // size of the fixed part of each record, for forward tolerance
    uint32_t count;
};

// Fixed part of a v2 record; `textLength` bytes of text follow it immediately, unterminated.
struct __attribute__((packed)) StoredMessageRecordV2 {
    uint32_t timestamp;
    uint32_t sender;
    uint32_t dest;
    uint32_t packetId;
    uint8_t channelIndex;
    uint8_t isBootRelative;
    uint8_t ackStatus; // static_cast<uint8_t>(AckStatus)
    uint8_t type;      // static_cast<uint8_t>(MessageType)
    uint8_t xeddsaSigned;
    uint8_t reserved; // keeps the fixed part even-sized and leaves room to grow
    uint16_t textLength;
};

// The v1 record, retained only so an existing file can be migrated.
struct __attribute__((packed)) StoredMessageRecordV1 {
    uint32_t timestamp;
    uint32_t sender;
    uint8_t channelIndex;
    uint32_t dest;
    uint8_t isBootRelative;
    uint8_t ackStatus;
    uint8_t type;
    uint8_t xeddsaSigned;
    uint16_t textLength;
    char text[MAX_MESSAGE_SIZE];
};

// Serialize one StoredMessage to flash
static inline void writeMessageRecord(SafeFile &f, const StoredMessage &m)
{
    const char *txt = getTextFromPool(m.textOffset);
    size_t len = strnlen(txt, MAX_MESSAGE_SIZE - 1);

    StoredMessageRecordV2 rec = {};
    rec.timestamp = m.timestamp;
    rec.sender = m.sender;
    rec.dest = m.dest;
    rec.packetId = m.packetId;
    rec.channelIndex = m.channelIndex;
    rec.isBootRelative = m.isBootRelative;
    rec.ackStatus = static_cast<uint8_t>(m.ackStatus);
    rec.type = static_cast<uint8_t>(m.type);
    rec.xeddsaSigned = m.xeddsaSigned ? 1 : 0;
    rec.textLength = (uint16_t)len;

    f.write(reinterpret_cast<const uint8_t *>(&rec), sizeof(rec));
    if (len)
        f.write(reinterpret_cast<const uint8_t *>(txt), len);
}

// Deserialize one v2 record; returns false on short read
static inline bool readMessageRecordV2(File &f, StoredMessage &m, MessageStore &store)
{
    StoredMessageRecordV2 rec = {};
    if (f.readBytes(reinterpret_cast<char *>(&rec), sizeof(rec)) != sizeof(rec))
        return false;

    m.timestamp = rec.timestamp;
    m.sender = rec.sender;
    m.dest = rec.dest;
    m.packetId = rec.packetId;
    m.channelIndex = rec.channelIndex;
    m.isBootRelative = rec.isBootRelative;
    m.ackStatus = static_cast<AckStatus>(rec.ackStatus);
    m.type = static_cast<MessageType>(rec.type);
    m.xeddsaSigned = rec.xeddsaSigned != 0;

    // A well-formed record never exceeds this - the writer clamps. Anything longer means the
    // file is corrupt or from a future format, and we cannot know where the next record
    // starts, so stop rather than resynchronise on a guess.
    if (rec.textLength > MAX_MESSAGE_SIZE - 1) {
        LOG_WARN("MessageStore: record claims %u B of text; stopping load", (unsigned)rec.textLength);
        return false;
    }

    char text[MAX_MESSAGE_SIZE] = {};
    if (rec.textLength && f.readBytes(text, rec.textLength) != (int)rec.textLength)
        return false;

    m.textLength = (uint16_t)strnlen(text, rec.textLength);
    m.textOffset = store.allocText(text, m.textLength);
    return true;
}

// Deserialize one v1 record; returns false on short read
static inline bool readMessageRecordV1(File &f, StoredMessage &m, MessageStore &store)
{
    StoredMessageRecordV1 rec = {};
    if (f.readBytes(reinterpret_cast<char *>(&rec), sizeof(rec)) != sizeof(rec))
        return false;

    m.timestamp = rec.timestamp;
    m.sender = rec.sender;
    m.channelIndex = rec.channelIndex;
    m.dest = rec.dest;
    m.isBootRelative = rec.isBootRelative;
    m.ackStatus = static_cast<AckStatus>(rec.ackStatus);
    m.type = static_cast<MessageType>(rec.type);
    m.xeddsaSigned = rec.xeddsaSigned != 0;
    m.packetId = 0; // v1 predates the field

    m.textLength = strnlen(rec.text, MAX_MESSAGE_SIZE - 1);
    m.textOffset = store.allocText(rec.text, m.textLength);

    return true;
}

void MessageStore::saveToFlash()
{
#ifdef FSCom
    // Ensure root exists
    spiLock->lock();
    FSCom.mkdir("/");
    spiLock->unlock();

    SafeFile f(filename.c_str(), false);

    spiLock->lock();
    size_t count = liveMessages.size();
    if (count > MAX_MESSAGES_SAVED)
        count = MAX_MESSAGES_SAVED;

    MessageFileHeader hdr = {};
    hdr.magic = kMessageFileMagic;
    hdr.version = kMessageFileVersion;
    hdr.recordSize = (uint16_t)sizeof(StoredMessageRecordV2);
    hdr.count = (uint32_t)count;
    f.write(reinterpret_cast<const uint8_t *>(&hdr), sizeof(hdr));

    for (size_t i = 0; i < count; ++i) {
        writeMessageRecord(f, liveMessages[i]);
    }
    spiLock->unlock();

    f.close();
#endif

    // Reset autosave state after any save
    g_messageStoreHasUnsavedChanges = false;
    g_lastAutoSaveMs = Time::getMillis();
}

void MessageStore::loadFromFlash()
{
    std::deque<StoredMessage>().swap(liveMessages);
    resetMessagePool(); // reset pool when loading

#ifdef FSCom
    bool migratedFromV1 = false;
    {
        concurrency::LockGuard guard(spiLock);

        if (!FSCom.exists(filename.c_str()))
            return;

        auto f = FSCom.open(filename.c_str(), FILE_O_READ);
        if (!f)
            return;

        // Peek the header. A v1 file starts with a message count, which can never look like
        // our magic, so the first four bytes tell the two formats apart without a flag file.
        MessageFileHeader hdr = {};
        const bool haveHeader = f.readBytes(reinterpret_cast<char *>(&hdr), sizeof(hdr)) == sizeof(hdr);

        if (haveHeader && hdr.magic == kMessageFileMagic && hdr.version == kMessageFileVersion &&
            hdr.recordSize == sizeof(StoredMessageRecordV2)) {
            uint32_t count = hdr.count;
            if (count > MAX_MESSAGES_SAVED)
                count = MAX_MESSAGES_SAVED;

            for (uint32_t i = 0; i < count; ++i) {
                StoredMessage m;
                if (!readMessageRecordV2(f, m, *this))
                    break;
                liveMessages.push_back(m);
            }
        } else {
            // v1: rewind and read the legacy layout.
            f.seek(0);
            uint8_t count = 0;
            f.readBytes(reinterpret_cast<char *>(&count), 1);
            if (count > MAX_MESSAGES_SAVED)
                count = MAX_MESSAGES_SAVED;

            for (uint8_t i = 0; i < count; ++i) {
                StoredMessage m;
                if (!readMessageRecordV1(f, m, *this))
                    break;
                liveMessages.push_back(m);
            }
            migratedFromV1 = true;
            LOG_INFO("MessageStore: migrating %u messages from v1 format", (unsigned)liveMessages.size());
        }

        f.close();
    }

    // Rewrite in the new format immediately, so the migration happens once rather than on
    // every boot. saveToFlash takes spiLock itself, hence being outside the guard above.
    if (pruneHiddenMessages() || migratedFromV1)
        saveToFlash();
#endif
    // Loading messages does not trigger an autosave
    g_messageStoreHasUnsavedChanges = false;
    g_lastAutoSaveMs = Time::getMillis();
}

#else
// If persistence is disabled, these functions become no-ops
void MessageStore::saveToFlash() {}
void MessageStore::loadFromFlash() {}
#endif

// Clear all messages (RAM + persisted queue)
void MessageStore::clearAllMessages()
{
    std::deque<StoredMessage>().swap(liveMessages);
    resetMessagePool();

#if defined(FSCom) && ENABLE_MESSAGE_PERSISTENCE
    SafeFile f(filename.c_str(), false);

    // A valid, empty file rather than a deleted one, so the next load takes the normal path.
    MessageFileHeader hdr = {};
    hdr.magic = kMessageFileMagic;
    hdr.version = kMessageFileVersion;
    hdr.recordSize = (uint16_t)sizeof(StoredMessageRecordV2);
    hdr.count = 0;

    // SafeFile already does its own spiLock in its constructor and close().
    // Avoid nesting spiLocks, as this will hang until watchdog reset!
    {
        concurrency::LockGuard guard(spiLock);
        f.write(reinterpret_cast<const uint8_t *>(&hdr), sizeof(hdr));
    }

    f.close();
#endif

#if ENABLE_MESSAGE_PERSISTENCE
    g_messageStoreHasUnsavedChanges = false;
    g_lastAutoSaveMs = Time::getMillis();
#endif
}

// Internal helpers for targeted erasure.
template <typename Predicate> static bool eraseFirstMatch(std::deque<StoredMessage> &deque, Predicate pred)
{
    for (auto it = deque.begin(); it != deque.end(); ++it) {
        if (pred(*it)) {
            deque.erase(it);
            return true;
        }
    }
    return false;
}

template <typename Predicate> static void eraseAllMatches(std::deque<StoredMessage> &deque, Predicate pred)
{
    for (auto it = deque.begin(); it != deque.end();) {
        if (pred(*it)) {
            it = deque.erase(it);
        } else {
            ++it;
        }
    }
}

bool MessageStore::pruneHiddenMessages()
{
    const size_t before = liveMessages.size();
    eraseAllMatches(liveMessages, [&](const StoredMessage &m) { return !isMessageVisible(m); });
    return liveMessages.size() != before;
}

// Delete oldest message (RAM + persisted queue)
void MessageStore::deleteOldestMessage()
{
    if (!liveMessages.empty()) {
        liveMessages.pop_front();
    }
    saveToFlash();
}

// Delete oldest message in a specific channel
void MessageStore::deleteOldestMessageInChannel(uint8_t channel)
{
    auto pred = [channel](const StoredMessage &m) { return m.type == MessageType::BROADCAST && m.channelIndex == channel; };
    eraseFirstMatch(liveMessages, pred);
    saveToFlash();
}

void MessageStore::deleteAllMessagesInChannel(uint8_t channel)
{
    auto pred = [channel](const StoredMessage &m) { return m.type == MessageType::BROADCAST && m.channelIndex == channel; };
    eraseAllMatches(liveMessages, pred);
    saveToFlash();
}

void MessageStore::deleteAllMessagesWithPeer(uint32_t peer)
{
    uint32_t local = nodeDB->getNodeNum();
    auto pred = [&](const StoredMessage &m) {
        if (m.type != MessageType::DM_TO_US)
            return false;
        uint32_t other = (m.sender == local) ? m.dest : m.sender;
        return other == peer;
    };
    eraseAllMatches(liveMessages, pred);
    saveToFlash();
}

void MessageStore::deleteAllMessagesFromNode(uint32_t nodeNum)
{
    const uint32_t local = nodeDB->getNodeNum();
    auto pred = [&](const StoredMessage &m) {
        if (m.sender == nodeNum)
            return true;
        if (m.type != MessageType::DM_TO_US)
            return false;
        return m.sender == local ? m.dest == nodeNum : m.sender == nodeNum;
    };
    eraseAllMatches(liveMessages, pred);
    saveToFlash();
}

// Delete oldest message in a direct chat with a node
void MessageStore::deleteOldestMessageWithPeer(uint32_t peer)
{
    auto pred = [peer](const StoredMessage &m) {
        if (m.type != MessageType::DM_TO_US)
            return false;
        uint32_t other = (m.sender == nodeDB->getNodeNum()) ? m.dest : m.sender;
        return other == peer;
    };
    eraseFirstMatch(liveMessages, pred);
    saveToFlash();
}

std::deque<StoredMessage> MessageStore::getChannelMessages(uint8_t channel) const
{
    std::deque<StoredMessage> result;
    for (const auto &m : liveMessages) {
        if (isMessageVisible(m) && m.type == MessageType::BROADCAST && m.channelIndex == channel) {
            result.push_back(m);
        }
    }
    return result;
}

std::deque<StoredMessage> MessageStore::getDirectMessages() const
{
    std::deque<StoredMessage> result;
    for (const auto &m : liveMessages) {
        if (isMessageVisible(m) && m.type == MessageType::DM_TO_US) {
            result.push_back(m);
        }
    }
    return result;
}

bool MessageStore::hasVisibleMessages() const
{
    for (const auto &m : liveMessages) {
        if (isMessageVisible(m))
            return true;
    }
    return false;
}

// Upgrade boot-relative timestamps once RTC is valid
// Only same-boot boot-relative messages are healed.
// Persisted boot-relative messages from old boots stay ??? forever.
void MessageStore::upgradeBootRelativeTimestamps()
{
    uint32_t nowSecs = getValidTime(RTCQuality::RTCQualityDevice, true);
    if (nowSecs == 0)
        return; // Still no valid RTC

    uint32_t bootNow = Time::getUptimeSecs();

    auto fix = [&](std::deque<StoredMessage> &dq) {
        for (auto &m : dq) {
            if (m.isBootRelative && m.timestamp <= bootNow) {
                uint32_t bootOffset = nowSecs - bootNow;
                m.timestamp += bootOffset;
                m.isBootRelative = false;
            }
        }
    };
    fix(liveMessages);
}

const char *MessageStore::getText(const StoredMessage &msg)
{
    // Wrapper around the internal helper
    return getTextFromPool(msg.textOffset);
}

uint16_t MessageStore::storeText(const char *src, size_t len)
{
    // Historically static, and called that way from outside. There is one pool and one store,
    // so route it through the global instance - allocText() needs the message deque in order
    // to reclaim space.
    return messageStore.allocText(src, len);
}

#if ENABLE_MESSAGE_PERSISTENCE
void messageStoreAutosaveTick()
{
    // Called from the main loop to check autosave timing
    autosaveTick(&messageStore);
}
#endif

// Global definition
MessageStore messageStore("default");
#endif
