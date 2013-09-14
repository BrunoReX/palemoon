/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set sw=2 ts=8 et tw=80 :
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_ChannelEventQueue_h
#define mozilla_net_ChannelEventQueue_h

#include <nsTArray.h>
#include <nsAutoPtr.h>
#include <nsThreadUtils.h>

class nsISupports;

namespace mozilla {
namespace net {

class ChannelEvent
{
 public:
  ChannelEvent() { MOZ_COUNT_CTOR(ChannelEvent); }
  virtual ~ChannelEvent() { MOZ_COUNT_DTOR(ChannelEvent); }
  virtual void Run() = 0;
};

// Workaround for Necko re-entrancy dangers. We buffer IPDL messages in a
// queue if still dispatching previous one(s) to listeners/observers.
// Otherwise synchronous XMLHttpRequests and/or other code that spins the
// event loop (ex: IPDL rpc) could cause listener->OnDataAvailable (for
// instance) to be dispatched and called before mListener->OnStartRequest has
// completed.

class AutoEventEnqueuerBase;

class ChannelEventQueue
{
  NS_INLINE_DECL_REFCOUNTING(ChannelEventQueue)

 public:
  ChannelEventQueue(nsISupports *owner)
    : mSuspendCount(0)
    , mSuspended(false)
    , mForced(false)
    , mFlushing(false)
    , mOwner(owner) {}

  ~ChannelEventQueue() {}

  // Checks to determine if an IPDL-generated channel event can be processed
  // immediately, or needs to be queued using Enqueue().
  inline bool ShouldEnqueue();

  // Puts IPDL-generated channel event into queue, to be run later
  // automatically when EndForcedQueueing and/or Resume is called.
  inline void Enqueue(ChannelEvent* callback);

  // After StartForcedQueueing is called, ShouldEnqueue() will return true and
  // no events will be run/flushed until EndForcedQueueing is called.
  // - Note: queueing may still be required after EndForcedQueueing() (if the
  //   queue is suspended, etc):  always call ShouldEnqueue() to determine
  //   whether queueing is needed.
  inline void StartForcedQueueing();
  inline void EndForcedQueueing();

  // Suspend/resume event queue.  ShouldEnqueue() will return true and no events
  // will be run/flushed until resume is called.  These should be called when
  // the channel owning the event queue is suspended/resumed.
  inline void Suspend();
  // Resume flushes the queue asynchronously, i.e. items in queue will be
  // dispatched in a new event on the current thread.
  inline void Resume();

 private:
  inline void MaybeFlushQueue();
  void FlushQueue();
  inline void CompleteResume();

  nsTArray<nsAutoPtr<ChannelEvent> > mEventQueue;

  uint32_t mSuspendCount;
  bool     mSuspended;
  bool mForced;
  bool mFlushing;

  // Keep ptr to avoid refcount cycle: only grab ref during flushing.
  nsISupports *mOwner;

  friend class AutoEventEnqueuer;
};

inline bool
ChannelEventQueue::ShouldEnqueue()
{
  bool answer =  mForced || mSuspended || mFlushing;

  NS_ABORT_IF_FALSE(answer == true || mEventQueue.IsEmpty(),
                    "Should always enqueue if ChannelEventQueue not empty");

  return answer;
}

inline void
ChannelEventQueue::Enqueue(ChannelEvent* callback)
{
  mEventQueue.AppendElement(callback);
}

inline void
ChannelEventQueue::StartForcedQueueing()
{
  mForced = true;
}

inline void
ChannelEventQueue::EndForcedQueueing()
{
  mForced = false;
  MaybeFlushQueue();
}

inline void
ChannelEventQueue::Suspend()
{
  mSuspended = true;
  mSuspendCount++;
}

inline void
ChannelEventQueue::CompleteResume()
{
  // channel may have been suspended again since Resume fired event to call this.
  if (!mSuspendCount) {
    // we need to remain logically suspended (for purposes of queuing incoming
    // messages) until this point, else new incoming messages could run before
    // queued ones.
    mSuspended = false;
    MaybeFlushQueue();
  }
}

inline void
ChannelEventQueue::Resume()
{
  // Resuming w/o suspend: error in debug mode, ignore in build
  MOZ_ASSERT(mSuspendCount > 0);
  if (mSuspendCount <= 0) {
    return;
  }

  if (!--mSuspendCount) {
    nsRefPtr<nsRunnableMethod<ChannelEventQueue> > event =
      NS_NewRunnableMethod(this, &ChannelEventQueue::CompleteResume);
    NS_DispatchToCurrentThread(event);
  }
}

inline void
ChannelEventQueue::MaybeFlushQueue()
{
  // Don't flush if forced queuing on, we're already being flushed, or
  // suspended, or there's nothing to flush
  if (!mForced && !mFlushing && !mSuspended && !mEventQueue.IsEmpty())
    FlushQueue();
}

// Ensures that ShouldEnqueue() will be true during its lifetime (letting
// caller know incoming IPDL msgs should be queued). Flushes the queue when it
// goes out of scope.
class AutoEventEnqueuer
{
 public:
  AutoEventEnqueuer(ChannelEventQueue *queue) : mEventQueue(queue) {
    mEventQueue->StartForcedQueueing();
  }
  ~AutoEventEnqueuer() {
    mEventQueue->EndForcedQueueing();
  }
 private:
  ChannelEventQueue* mEventQueue;
};

}
}

#endif
