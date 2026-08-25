// Tests for the shared message history - src/MessageStore.{h,cpp}.
//
// The three behaviours worth pinning here are the ones that were silently wrong before, and
// which nothing else in the suite covers:
//
//   * the text pool used to wrap to offset 0 when it filled, overwriting bytes that older
//     messages still pointed at, so getText() returned another message's text
//   * eviction was strictly oldest-first across the whole store, so one busy channel emptied
//     every DM thread
//   * delivery status was written onto whatever happened to be the newest message, because
//     nothing recorded which packet a message was sent in
#include "MeshTypes.h" // NODENUM_BROADCAST
#include "MessageStore.h"
#include "TestUtil.h"
#include "mesh/NodeDB.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <unity.h>

// Unity's TEST_ASSERT_NOT_EQUAL compares as int, which would truncate a 64-bit thread key and
// report two distinct threads as equal. Compare the full width instead.
#define ASSERT_THREADS_DIFFER(a, b) TEST_ASSERT_TRUE_MESSAGE((a) != (b), "thread keys collided")

namespace
{

constexpr uint32_t kMe = 0x11111111;
constexpr uint32_t kAlice = 0x22222222;
constexpr uint32_t kBob = 0x33333333;

// Build a message directly, bypassing packet decoding.
StoredMessage makeMessage(uint32_t sender, uint32_t dest, uint8_t channel, const char *text, uint32_t packetId = 0)
{
    StoredMessage m;
    m.sender = sender;
    m.dest = dest;
    m.channelIndex = channel;
    m.type = (dest != 0 && dest != NODENUM_BROADCAST) ? MessageType::DM_TO_US : MessageType::BROADCAST;
    m.packetId = packetId;
    m.timestamp = 1000;
    m.textLength = (uint16_t)strlen(text);
    m.textOffset = messageStore.allocText(text, m.textLength);
    return m;
}

void addMessage(uint32_t sender, uint32_t dest, uint8_t channel, const char *text, uint32_t packetId = 0)
{
    messageStore.addLiveMessage(makeMessage(sender, dest, channel, text, packetId));
}

} // namespace

void setUp(void)
{
    messageStore.clearAllMessages();
}

void tearDown(void) {}

// --------------------------------------------------------------------------------------
// Thread keying
// --------------------------------------------------------------------------------------

static void test_broadcasts_group_by_channel()
{
    StoredMessage a = makeMessage(kAlice, NODENUM_BROADCAST, 0, "hi");
    StoredMessage b = makeMessage(kBob, NODENUM_BROADCAST, 0, "hey");
    StoredMessage c = makeMessage(kAlice, NODENUM_BROADCAST, 3, "other channel");

    TEST_ASSERT_EQUAL_UINT64(threadKeyOf(a, kMe), threadKeyOf(b, kMe));
    ASSERT_THREADS_DIFFER(threadKeyOf(a, kMe), threadKeyOf(c, kMe));
}

static void test_dms_group_by_peer_in_both_directions()
{
    StoredMessage incoming = makeMessage(kAlice, kMe, 0, "you up?");
    StoredMessage outgoing = makeMessage(kMe, kAlice, 0, "yes");

    // The whole point of a thread: my reply belongs with what I am replying to.
    TEST_ASSERT_EQUAL_UINT64(threadKeyOf(incoming, kMe), threadKeyOf(outgoing, kMe));

    StoredMessage other = makeMessage(kBob, kMe, 0, "different person");
    ASSERT_THREADS_DIFFER(threadKeyOf(incoming, kMe), threadKeyOf(other, kMe));
}

static void test_channel_and_node_number_never_collide()
{
    // Channel 3 and node 3 are both small integers; without the tag bit they would land in
    // the same thread and interleave two unrelated conversations.
    StoredMessage broadcast = makeMessage(kAlice, NODENUM_BROADCAST, 3, "channel three");
    StoredMessage dm = makeMessage(3, kMe, 0, "node three");

    ASSERT_THREADS_DIFFER(threadKeyOf(broadcast, kMe), threadKeyOf(dm, kMe));
}

// --------------------------------------------------------------------------------------
// Text pool
// --------------------------------------------------------------------------------------

// The regression test for the wraparound. Fill the pool with messages whose text is
// individually identifiable, then assert every surviving message still reads back as itself.
static void test_pool_never_returns_another_messages_text()
{
    char text[MAX_MESSAGE_SIZE];
    memset(text, 'x', sizeof(text));

    // Enough traffic to cycle the pool several times over.
    const size_t body = 180;
    const size_t iterations = ((size_t)MESSAGE_TEXT_POOL_SIZE / body) * 3 + 16;

    for (size_t i = 0; i < iterations; ++i) {
        snprintf(text, sizeof(text), "%06u-", (unsigned)i);
        memset(text + 7, 'a' + (int)(i % 26), body - 7);
        text[body] = '\0';
        addMessage(kAlice, NODENUM_BROADCAST, 0, text, (uint32_t)(i + 1));
    }

    // Whatever survived, each message's text must still start with its own serial number.
    // Before the fix this failed as soon as the pool wrapped: the oldest survivors read back
    // as the newest messages' text.
    for (const auto &m : messageStore.getLiveMessages()) {
        const char *stored = MessageStore::getText(m);
        TEST_ASSERT_NOT_NULL(stored);

        char expected[16];
        snprintf(expected, sizeof(expected), "%06u-", (unsigned)(m.packetId - 1));
        TEST_ASSERT_EQUAL_STRING_LEN(expected, stored, 7);
    }

    TEST_ASSERT_TRUE(messageStore.getLiveMessages().size() > 0);
}

static void test_compaction_reclaims_space_from_deleted_messages()
{
    std::string filler(200, 'z');
    for (int i = 0; i < 8; ++i)
        addMessage(kAlice, NODENUM_BROADCAST, 0, filler.c_str());

    const size_t usedBefore = MessageStore::textPoolBytesUsed();
    TEST_ASSERT_TRUE(usedBefore >= 8 * 201);

    messageStore.deleteAllMessagesInChannel(0);
    messageStore.compactTextPool();

    // Nothing is left, so the pool should be back to empty rather than permanently consumed.
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)MessageStore::textPoolBytesUsed());
}

static void test_surviving_text_stays_readable_after_compaction()
{
    addMessage(kAlice, NODENUM_BROADCAST, 0, "keep me");
    addMessage(kBob, NODENUM_BROADCAST, 0, "delete me");
    addMessage(kAlice, NODENUM_BROADCAST, 0, "keep me too");

    messageStore.deleteAllMessagesFromNode(kBob);
    messageStore.compactTextPool();

    // Compaction rewrites offsets in place; the survivors must move with their text.
    const auto &msgs = messageStore.getLiveMessages();
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)msgs.size());
    TEST_ASSERT_EQUAL_STRING("keep me", MessageStore::getText(msgs[0]));
    TEST_ASSERT_EQUAL_STRING("keep me too", MessageStore::getText(msgs[1]));
}

// --------------------------------------------------------------------------------------
// Eviction
// --------------------------------------------------------------------------------------

static void test_busy_channel_does_not_evict_a_quiet_dm_thread()
{
    // One DM, then enough channel traffic to overrun the store several times. Oldest-first
    // eviction would have discarded the DM almost immediately.
    addMessage(kAlice, kMe, 0, "the only DM", 4242);

    for (size_t i = 0; i < (size_t)MAX_MESSAGES_SAVED * 2; ++i)
        addMessage(kBob, NODENUM_BROADCAST, 0, "chatter");

    TEST_ASSERT_NOT_NULL(messageStore.findByPacketId(4242));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)messageStore.countInThread(threadKeyForPeer(kAlice)));
}

static void test_store_still_respects_the_global_cap()
{
    for (size_t i = 0; i < (size_t)MAX_MESSAGES_SAVED * 2; ++i)
        addMessage(kBob, NODENUM_BROADCAST, 0, "chatter");

    TEST_ASSERT_TRUE(messageStore.getLiveMessages().size() <= (size_t)MAX_MESSAGES_SAVED);
}

// --------------------------------------------------------------------------------------
// Delivery status
// --------------------------------------------------------------------------------------

static void test_ack_lands_on_the_message_that_was_sent()
{
    addMessage(kMe, kAlice, 0, "first", 1001);
    addMessage(kMe, kBob, 0, "second", 1002);
    // A message arriving between the send and its ACK is exactly what used to misattribute
    // the status, because the old code wrote to whatever was newest.
    addMessage(kAlice, kMe, 0, "unrelated incoming", 1003);

    TEST_ASSERT_TRUE(messageStore.setAckStatus(1001, AckStatus::ACKED));

    const StoredMessage *first = messageStore.findByPacketId(1001);
    const StoredMessage *second = messageStore.findByPacketId(1002);
    const StoredMessage *incoming = messageStore.findByPacketId(1003);

    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL(AckStatus::ACKED, first->ackStatus);
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_EQUAL(AckStatus::NONE, second->ackStatus);
    TEST_ASSERT_NOT_NULL(incoming);
    TEST_ASSERT_EQUAL(AckStatus::NONE, incoming->ackStatus);
}

static void test_packet_id_zero_never_matches()
{
    // Records written before packetId existed carry 0. Treating that as a real id would let
    // one stray ACK mark an arbitrary migrated message as delivered.
    addMessage(kMe, kAlice, 0, "no id recorded", 0);

    TEST_ASSERT_NULL(messageStore.findByPacketId(0));
    TEST_ASSERT_FALSE(messageStore.setAckStatus(0, AckStatus::ACKED));
}

static void test_ack_for_unknown_packet_is_reported_not_applied()
{
    addMessage(kMe, kAlice, 0, "sent", 5001);

    // Normal once a send has aged out of history; must not silently hit something else.
    TEST_ASSERT_FALSE(messageStore.setAckStatus(9999, AckStatus::NACKED));
    TEST_ASSERT_EQUAL(AckStatus::NONE, messageStore.findByPacketId(5001)->ackStatus);
}

void setup()
{
    initializeTestEnvironment();

    // threadKeyOf() and the visibility filters resolve against our own node number.
    if (!nodeDB)
        nodeDB = new NodeDB();

    UNITY_BEGIN();

    // Thread keying
    RUN_TEST(test_broadcasts_group_by_channel);
    RUN_TEST(test_dms_group_by_peer_in_both_directions);
    RUN_TEST(test_channel_and_node_number_never_collide);

    // Text pool
    RUN_TEST(test_pool_never_returns_another_messages_text);
    RUN_TEST(test_compaction_reclaims_space_from_deleted_messages);
    RUN_TEST(test_surviving_text_stays_readable_after_compaction);

    // Eviction
    RUN_TEST(test_busy_channel_does_not_evict_a_quiet_dm_thread);
    RUN_TEST(test_store_still_respects_the_global_cap);

    // Delivery status
    RUN_TEST(test_ack_lands_on_the_message_that_was_sent);
    RUN_TEST(test_packet_id_zero_never_matches);
    RUN_TEST(test_ack_for_unknown_packet_is_reported_not_applied);

    exit(UNITY_END());
}

void loop() {}
