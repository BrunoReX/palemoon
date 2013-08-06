/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: sw=4 ts=4 et :
 */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Plugin App.
 *
 * The Initial Developer of the Original Code is
 *   Chris Jones <jones.chris.g@gmail.com>
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#ifndef ipc_glue_AsyncChannel_h
#define ipc_glue_AsyncChannel_h 1

#include "base/basictypes.h"
#include "base/message_loop.h"

#include "mozilla/Monitor.h"
#include "mozilla/ipc/Transport.h"

//-----------------------------------------------------------------------------

namespace mozilla {
namespace ipc {

struct HasResultCodes
{
    enum Result {
        MsgProcessed,
        MsgDropped,
        MsgNotKnown,
        MsgNotAllowed,
        MsgPayloadError,
        MsgProcessingError,
        MsgRouteError,
        MsgValueError,
    };
};

class AsyncChannel : public Transport::Listener, protected HasResultCodes
{
protected:
    typedef mozilla::Monitor Monitor;

    enum ChannelState {
        ChannelClosed,
        ChannelOpening,
        ChannelConnected,
        ChannelTimeout,
        ChannelClosing,
        ChannelError
    };

public:
    typedef IPC::Message Message;
    typedef mozilla::ipc::Transport Transport;

    class /*NS_INTERFACE_CLASS*/ AsyncListener: protected HasResultCodes
    {
    public:
        virtual ~AsyncListener() { }

        virtual void OnChannelClose() = 0;
        virtual void OnChannelError() = 0;
        virtual Result OnMessageReceived(const Message& aMessage) = 0;
        virtual void OnProcessingError(Result aError) = 0;
        virtual void OnChannelConnected(int32 peer_pid) {};
    };

    enum Side { Parent, Child, Unknown };

public:
    //
    // These methods are called on the "worker" thread
    //
    AsyncChannel(AsyncListener* aListener);
    virtual ~AsyncChannel();

    // "Open" from the perspective of the transport layer; the underlying
    // socketpair/pipe should already be created.
    //
    // Returns true iff the transport layer was successfully connected,
    // i.e., mChannelState == ChannelConnected.
    bool Open(Transport* aTransport, MessageLoop* aIOLoop=0, Side aSide=Unknown);
    
    // Close the underlying transport channel.
    void Close();

    // Asynchronously send a message to the other side of the channel
    virtual bool Send(Message* msg);

    // Asynchronously deliver a message back to this side of the
    // channel
    virtual bool Echo(Message* msg);

    // Send OnChannelConnected notification to listeners.
    void DispatchOnChannelConnected(int32 peer_pid);

    //
    // These methods are called on the "IO" thread
    //

    // Implement the Transport::Listener interface
    NS_OVERRIDE virtual void OnMessageReceived(const Message& msg);
    NS_OVERRIDE virtual void OnChannelConnected(int32 peer_pid);
    NS_OVERRIDE virtual void OnChannelError();

protected:
    // Can be run on either thread
    void AssertWorkerThread() const
    {
        NS_ABORT_IF_FALSE(mWorkerLoop == MessageLoop::current(),
                          "not on worker thread!");
    }

    void AssertIOThread() const
    {
        NS_ABORT_IF_FALSE(mIOLoop == MessageLoop::current(),
                          "not on IO thread!");
    }

    bool Connected() const {
        mMonitor.AssertCurrentThreadOwns();
        return ChannelConnected == mChannelState;
    }

    // Run on the worker thread
    void OnDispatchMessage(const Message& aMsg);
    virtual bool OnSpecialMessage(uint16 id, const Message& msg);
    void SendSpecialMessage(Message* msg) const;

    // Tell the IO thread to close the channel and wait for it to ACK.
    void SynchronouslyClose();

    bool MaybeHandleError(Result code, const char* channelName);
    void ReportConnectionError(const char* channelName) const;

    // Run on the worker thread

    void SendThroughTransport(Message* msg) const;

    void OnNotifyMaybeChannelError();
    virtual bool ShouldDeferNotifyMaybeError() const {
        return false;
    }
    void NotifyChannelClosed();
    void NotifyMaybeChannelError();

    virtual void Clear();

    // Run on the IO thread

    void OnChannelOpened();
    void OnCloseChannel();
    void PostErrorNotifyTask();
    void OnEchoMessage(Message* msg);

    // Return true if |msg| is a special message targeted at the IO
    // thread, in which case it shouldn't be delivered to the worker.
    bool MaybeInterceptSpecialIOMessage(const Message& msg);
    void ProcessGoodbyeMessage();

    Transport* mTransport;
    AsyncListener* mListener;
    ChannelState mChannelState;
    Monitor mMonitor;
    MessageLoop* mIOLoop;       // thread where IO happens
    MessageLoop* mWorkerLoop;   // thread where work is done
    bool mChild;                // am I the child or parent?
    CancelableTask* mChannelErrorTask; // NotifyMaybeChannelError runnable
    Transport::Listener* mExistingListener; // channel's previous listener
};


} // namespace ipc
} // namespace mozilla
#endif  // ifndef ipc_glue_AsyncChannel_h
