#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_PAGERUI

#include "MessageStore.h"
#include "Observer.h"
#include "mesh/MeshTypes.h"

#include <atomic>
#include <vector>

namespace PagerUI
{

/// Conversation-level state on top of MessageStore.
///
/// MessageStore holds the messages; this holds what a chat UI additionally needs - which
/// conversations exist, which have been read, and how to send. It also owns message ingest,
/// because the usual path (TextMessageModule's IF_SCREEN block) compiles away in a build that
/// replaces BaseUI.
///
/// Lives on the main loop, like the rest of the mesh stack. The UI task reads a snapshot; it
/// never calls in here directly.
class ChatStore
{
  public:
    struct Thread {
        ThreadKey key;
        uint32_t lastReadTimestamp;
        uint16_t unread;
    };

    /// Subscribe to incoming text messages. Call after setupModules().
    void begin();

    /// Send `text` to `dest` on `channel`. Returns the mesh packet id, or 0 if the send could
    /// not be started. The message is added to the store and handed to the ack tracker.
    ///
    /// Main loop only. The composer runs on the UI task and must hand the request across
    /// rather than calling this directly - it allocates from the packet pool and touches the
    /// router, neither of which is safe from another task.
    uint32_t send(NodeNum dest, ChannelIndex channel, const char *text);

    /// Unread count for one thread. Main loop only - it walks a container the main loop
    /// mutates.
    uint16_t unreadFor(ThreadKey key) const;

    /// Total unread across all conversations. Safe to read from the UI task: it is a scalar
    /// mirror maintained by the main loop, precisely so the status bar does not have to walk
    /// a vector that can reallocate underneath it.
    uint16_t unreadTotal() const { return totalUnread.load(std::memory_order_relaxed); }

    /// Mark a thread read up to the newest message it holds.
    void markRead(ThreadKey key);

    /// Threads that currently have messages, newest activity first. Main loop only - it walks
    /// the message store. The UI receives a copy of the result, it does not call this.
    std::vector<Thread> threads() const;

    /// Bumped whenever anything the UI renders has changed, so the UI can notice without
    /// walking the store every frame. Safe to read from the UI task.
    uint32_t revision() const { return rev.load(std::memory_order_relaxed); }

  private:
    int onTextMessage(const meshtastic_MeshPacket *packet);

    CallbackObserver<ChatStore, const meshtastic_MeshPacket *> textObserver =
        CallbackObserver<ChatStore, const meshtastic_MeshPacket *>(this, &ChatStore::onTextMessage);

    std::vector<Thread> threadState;

    // The two values the UI is allowed to read directly. Everything else in here is main-loop
    // only, because a vector that can reallocate is not something another task may walk.
    std::atomic<uint16_t> totalUnread{0};
    std::atomic<uint32_t> rev{0};

    Thread &stateFor(ThreadKey key);
    void recount(); // refresh totalUnread and bump rev; main loop only
};

extern ChatStore chatStore;

} // namespace PagerUI

#endif // MESHTASTIC_INCLUDE_PAGERUI
