/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */

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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 *  The Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Daniel Witte <dwitte@mozilla.com>
 *   Jason Duell <jduell.mcbugs@gmail.com>
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

#ifndef mozilla_net_HttpBaseChannel_h
#define mozilla_net_HttpBaseChannel_h

#include "nsHttp.h"
#include "nsAutoPtr.h"
#include "nsHashPropertyBag.h"
#include "nsProxyInfo.h"
#include "nsHttpRequestHead.h"
#include "nsHttpResponseHead.h"
#include "nsHttpConnectionInfo.h"
#include "nsIEncodedChannel.h"
#include "nsIHttpChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIUploadChannel.h"
#include "nsIUploadChannel2.h"
#include "nsIProgressEventSink.h"
#include "nsIURI.h"
#include "nsIStringEnumerator.h"
#include "nsISupportsPriority.h"
#include "nsIApplicationCache.h"
#include "nsIResumableChannel.h"
#include "nsITraceableChannel.h"
#include "mozilla/net/NeckoCommon.h"
#include "nsThreadUtils.h"

namespace mozilla {
namespace net {

/*
 * This class is a partial implementation of nsIHttpChannel.  It contains code
 * shared by nsHttpChannel and HttpChannelChild. 
 * - Note that this class has nothing to do with nsBaseChannel, which is an
 *   earlier effort at a base class for channels that somehow never made it all
 *   the way to the HTTP channel.
 */
class HttpBaseChannel : public nsHashPropertyBag
                      , public nsIEncodedChannel
                      , public nsIHttpChannel
                      , public nsIHttpChannelInternal
                      , public nsIUploadChannel
                      , public nsIUploadChannel2
                      , public nsISupportsPriority
                      , public nsIResumableChannel
                      , public nsITraceableChannel
{
public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIUPLOADCHANNEL
  NS_DECL_NSIUPLOADCHANNEL2
  NS_DECL_NSITRACEABLECHANNEL

  HttpBaseChannel();
  virtual ~HttpBaseChannel();

  virtual nsresult Init(nsIURI *aURI, PRUint8 aCaps, nsProxyInfo *aProxyInfo);

  // nsIRequest
  NS_IMETHOD GetName(nsACString& aName);
  NS_IMETHOD IsPending(PRBool *aIsPending);
  NS_IMETHOD GetStatus(nsresult *aStatus);
  NS_IMETHOD GetLoadGroup(nsILoadGroup **aLoadGroup);
  NS_IMETHOD SetLoadGroup(nsILoadGroup *aLoadGroup);
  NS_IMETHOD GetLoadFlags(nsLoadFlags *aLoadFlags);
  NS_IMETHOD SetLoadFlags(nsLoadFlags aLoadFlags);

  // nsIChannel
  NS_IMETHOD GetOriginalURI(nsIURI **aOriginalURI);
  NS_IMETHOD SetOriginalURI(nsIURI *aOriginalURI);
  NS_IMETHOD GetURI(nsIURI **aURI);
  NS_IMETHOD GetOwner(nsISupports **aOwner);
  NS_IMETHOD SetOwner(nsISupports *aOwner);
  NS_IMETHOD GetNotificationCallbacks(nsIInterfaceRequestor **aCallbacks);
  NS_IMETHOD SetNotificationCallbacks(nsIInterfaceRequestor *aCallbacks);
  NS_IMETHOD GetContentType(nsACString& aContentType);
  NS_IMETHOD SetContentType(const nsACString& aContentType);
  NS_IMETHOD GetContentCharset(nsACString& aContentCharset);
  NS_IMETHOD SetContentCharset(const nsACString& aContentCharset);
  NS_IMETHOD GetContentDisposition(PRUint32 *aContentDisposition);
  NS_IMETHOD GetContentDispositionFilename(nsAString& aContentDispositionFilename);
  NS_IMETHOD GetContentDispositionHeader(nsACString& aContentDispositionHeader);
  NS_IMETHOD GetContentLength(PRInt32 *aContentLength);
  NS_IMETHOD SetContentLength(PRInt32 aContentLength);
  NS_IMETHOD Open(nsIInputStream **aResult);

  // nsIEncodedChannel
  NS_IMETHOD GetApplyConversion(PRBool *value);
  NS_IMETHOD SetApplyConversion(PRBool value);
  NS_IMETHOD GetContentEncodings(nsIUTF8StringEnumerator** aEncodings);

  // HttpBaseChannel::nsIHttpChannel
  NS_IMETHOD GetRequestMethod(nsACString& aMethod);
  NS_IMETHOD SetRequestMethod(const nsACString& aMethod);
  NS_IMETHOD GetReferrer(nsIURI **referrer);
  NS_IMETHOD SetReferrer(nsIURI *referrer);
  NS_IMETHOD GetRequestHeader(const nsACString& aHeader, nsACString& aValue);
  NS_IMETHOD SetRequestHeader(const nsACString& aHeader, 
                              const nsACString& aValue, PRBool aMerge);
  NS_IMETHOD VisitRequestHeaders(nsIHttpHeaderVisitor *visitor);
  NS_IMETHOD GetResponseHeader(const nsACString &header, nsACString &value);
  NS_IMETHOD SetResponseHeader(const nsACString& header, 
                               const nsACString& value, PRBool merge);
  NS_IMETHOD VisitResponseHeaders(nsIHttpHeaderVisitor *visitor);
  NS_IMETHOD GetAllowPipelining(PRBool *value);
  NS_IMETHOD SetAllowPipelining(PRBool value);
  NS_IMETHOD GetRedirectionLimit(PRUint32 *value);
  NS_IMETHOD SetRedirectionLimit(PRUint32 value);
  NS_IMETHOD IsNoStoreResponse(PRBool *value);
  NS_IMETHOD IsNoCacheResponse(PRBool *value);
  NS_IMETHOD GetResponseStatus(PRUint32 *aValue);
  NS_IMETHOD GetResponseStatusText(nsACString& aValue);
  NS_IMETHOD GetRequestSucceeded(PRBool *aValue);

  // nsIHttpChannelInternal
  NS_IMETHOD GetDocumentURI(nsIURI **aDocumentURI);
  NS_IMETHOD SetDocumentURI(nsIURI *aDocumentURI);
  NS_IMETHOD GetRequestVersion(PRUint32 *major, PRUint32 *minor);
  NS_IMETHOD GetResponseVersion(PRUint32 *major, PRUint32 *minor);
  NS_IMETHOD SetCookie(const char *aCookieHeader);
  NS_IMETHOD GetForceAllowThirdPartyCookie(PRBool *aForce);
  NS_IMETHOD SetForceAllowThirdPartyCookie(PRBool aForce);
  NS_IMETHOD GetCanceled(PRBool *aCanceled);
  NS_IMETHOD GetChannelIsForDownload(PRBool *aChannelIsForDownload);
  NS_IMETHOD SetChannelIsForDownload(PRBool aChannelIsForDownload);
  NS_IMETHOD SetCacheKeysRedirectChain(nsTArray<nsCString> *cacheKeys);
  NS_IMETHOD GetLocalAddress(nsACString& addr);
  NS_IMETHOD GetLocalPort(PRInt32* port);
  NS_IMETHOD GetRemoteAddress(nsACString& addr);
  NS_IMETHOD GetRemotePort(PRInt32* port);
  inline void CleanRedirectCacheChainIfNecessary()
  {
      if (mRedirectedCachekeys) {
          delete mRedirectedCachekeys;
          mRedirectedCachekeys = nsnull;
      }
  }
  NS_IMETHOD HTTPUpgrade(const nsACString & aProtocolName,
                         nsIHttpUpgradeListener *aListener); 

  // nsISupportsPriority
  NS_IMETHOD GetPriority(PRInt32 *value);
  NS_IMETHOD AdjustPriority(PRInt32 delta);

  // nsIResumableChannel
  NS_IMETHOD GetEntityID(nsACString& aEntityID);

  class nsContentEncodings : public nsIUTF8StringEnumerator
    {
    public:
        NS_DECL_ISUPPORTS
        NS_DECL_NSIUTF8STRINGENUMERATOR

        nsContentEncodings(nsIHttpChannel* aChannel, const char* aEncodingHeader);
        virtual ~nsContentEncodings();
        
    private:
        nsresult PrepareForNext(void);
        
        // We do not own the buffer.  The channel owns it.
        const char* mEncodingHeader;
        const char* mCurStart;  // points to start of current header
        const char* mCurEnd;  // points to end of current header
        
        // Hold a ref to our channel so that it can't go away and take the
        // header with it.
        nsCOMPtr<nsIHttpChannel> mChannel;
        
        PRPackedBool mReady;
    };

    nsHttpResponseHead * GetResponseHead() const { return mResponseHead; }
    nsHttpRequestHead * GetRequestHead() { return &mRequestHead; }

    const PRNetAddr& GetSelfAddr() { return mSelfAddr; }
    const PRNetAddr& GetPeerAddr() { return mPeerAddr; }

public: /* Necko internal use only... */

protected:

  // Handle notifying listener, removing from loadgroup if request failed.
  void     DoNotifyListener();
  virtual void DoNotifyListenerCleanup() = 0;

  nsresult ApplyContentConversions();

  void AddCookiesToRequest();
  virtual nsresult SetupReplacementChannel(nsIURI *,
                                           nsIChannel *,
                                           PRBool preserveMethod);

  // Helper function to simplify getting notification callbacks.
  template <class T>
  void GetCallback(nsCOMPtr<T> &aResult)
  {
    NS_QueryNotificationCallbacks(mCallbacks, mLoadGroup,
                                  NS_GET_TEMPLATE_IID(T),
                                  getter_AddRefs(aResult));
  }

  nsCOMPtr<nsIURI>                  mURI;
  nsCOMPtr<nsIURI>                  mOriginalURI;
  nsCOMPtr<nsIURI>                  mDocumentURI;
  nsCOMPtr<nsIStreamListener>       mListener;
  nsCOMPtr<nsISupports>             mListenerContext;
  nsCOMPtr<nsILoadGroup>            mLoadGroup;
  nsCOMPtr<nsISupports>             mOwner;
  nsCOMPtr<nsIInterfaceRequestor>   mCallbacks;
  nsCOMPtr<nsIProgressEventSink>    mProgressSink;
  nsCOMPtr<nsIURI>                  mReferrer;
  nsCOMPtr<nsIApplicationCache>     mApplicationCache;

  nsHttpRequestHead                 mRequestHead;
  nsCOMPtr<nsIInputStream>          mUploadStream;
  nsAutoPtr<nsHttpResponseHead>     mResponseHead;
  nsRefPtr<nsHttpConnectionInfo>    mConnectionInfo;

  nsCString                         mSpec; // ASCII encoded URL spec
  nsCString                         mContentTypeHint;
  nsCString                         mContentCharsetHint;
  nsCString                         mUserSetCookieHeader;

  PRNetAddr                         mSelfAddr;
  PRNetAddr                         mPeerAddr;

  // HTTP Upgrade Data
  nsCString                        mUpgradeProtocol;
  nsCOMPtr<nsIHttpUpgradeListener> mUpgradeProtocolCallback;

  // Resumable channel specific data
  nsCString                         mEntityID;
  PRUint64                          mStartPos;

  nsresult                          mStatus;
  PRUint32                          mLoadFlags;
  PRInt16                           mPriority;
  PRUint8                           mCaps;
  PRUint8                           mRedirectionLimit;

  PRUint32                          mApplyConversion            : 1;
  PRUint32                          mCanceled                   : 1;
  PRUint32                          mIsPending                  : 1;
  PRUint32                          mWasOpened                  : 1;
  PRUint32                          mResponseHeadersModified    : 1;
  PRUint32                          mAllowPipelining            : 1;
  PRUint32                          mForceAllowThirdPartyCookie : 1;
  PRUint32                          mUploadStreamHasHeaders     : 1;
  PRUint32                          mInheritApplicationCache    : 1;
  PRUint32                          mChooseApplicationCache     : 1;
  PRUint32                          mLoadedFromApplicationCache : 1;
  PRUint32                          mChannelIsForDownload       : 1;
  PRUint32                          mTracingEnabled             : 1;
  // True if timing collection is enabled
  PRUint32                          mTimingEnabled              : 1;

  // Current suspension depth for this channel object
  PRUint32                          mSuspendCount;

  nsTArray<nsCString>              *mRedirectedCachekeys;
};

// Share some code while working around C++'s absurd inability to handle casting
// of member functions between base/derived types.
// - We want to store member function pointer to call at resume time, but one
//   such function--HandleAsyncAbort--we want to share between the
//   nsHttpChannel/HttpChannelChild.  Can't define it in base class, because
//   then we'd have to cast member function ptr between base/derived class
//   types.  Sigh...
template <class T>
class HttpAsyncAborter
{
public:
  HttpAsyncAborter(T *derived) : mThis(derived), mCallOnResume(0) {}

  // Aborts channel: calls OnStart/Stop with provided status, removes channel
  // from loadGroup.
  nsresult AsyncAbort(nsresult status);

  // Does most the actual work.
  void HandleAsyncAbort();

  // AsyncCall calls a member function asynchronously (via an event).
  // retval isn't refcounted and is set only when event was successfully
  // posted, the event is returned for the purpose of cancelling when needed
  nsresult AsyncCall(void (T::*funcPtr)(),
                     nsRunnableMethod<T> **retval = nsnull);
private:
  T *mThis;

protected:
  // Function to be called at resume time
  void (T::* mCallOnResume)(void);
};

template <class T>
nsresult HttpAsyncAborter<T>::AsyncAbort(nsresult status)
{
  LOG(("HttpAsyncAborter::AsyncAbort [this=%p status=%x]\n", mThis, status));

  mThis->mStatus = status;
  mThis->mIsPending = PR_FALSE;

  // if this fails?  Callers ignore our return value anyway....
  return AsyncCall(&T::HandleAsyncAbort);
}

// Each subclass needs to define its own version of this (which just calls this
// base version), else we wind up casting base/derived member function ptrs
template <class T>
inline void HttpAsyncAborter<T>::HandleAsyncAbort()
{
  NS_PRECONDITION(!mCallOnResume, "How did that happen?");

  if (mThis->mSuspendCount) {
    LOG(("Waiting until resume to do async notification [this=%p]\n",
         mThis));
    mCallOnResume = &T::HandleAsyncAbort;
    return;
  }

  mThis->DoNotifyListener();

  // finally remove ourselves from the load group.
  if (mThis->mLoadGroup)
    mThis->mLoadGroup->RemoveRequest(mThis, nsnull, mThis->mStatus);
}

template <class T>
nsresult HttpAsyncAborter<T>::AsyncCall(void (T::*funcPtr)(),
                                   nsRunnableMethod<T> **retval)
{
  nsresult rv;

  nsRefPtr<nsRunnableMethod<T> > event = NS_NewRunnableMethod(mThis, funcPtr);
  rv = NS_DispatchToCurrentThread(event);
  if (NS_SUCCEEDED(rv) && retval) {
    *retval = event;
  }

  return rv;
}

} // namespace net
} // namespace mozilla

#endif // mozilla_net_HttpBaseChannel_h
