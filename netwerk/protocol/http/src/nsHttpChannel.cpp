/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set expandtab ts=4 sw=4 sts=4 cin: */
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
 * The Original Code is Mozilla.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications.
 * Portions created by the Initial Developer are Copyright (C) 2001
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Darin Fisher <darin@meer.net> (original author)
 *   Christian Biesinger <cbiesinger@web.de>
 *   Google Inc.
 *   Jan Wrobel <wrobel@blues.ath.cx>
 *   Jan Odvarko <odvarko@gmail.com>
 *   Dave Camp <dcamp@mozilla.com>
 *   Honza Bambas <honzab@firemni.cz>
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

#include "nsHttpChannel.h"
#include "nsHttpTransaction.h"
#include "nsHttpConnection.h"
#include "nsHttpHandler.h"
#include "nsHttpAuthCache.h"
#include "nsHttpResponseHead.h"
#include "nsHttp.h"
#include "nsIHttpAuthenticator.h"
#include "nsIApplicationCacheService.h"
#include "nsIApplicationCacheContainer.h"
#include "nsIAuthInformation.h"
#include "nsIAuthPrompt2.h"
#include "nsIAuthPromptProvider.h"
#include "nsIStringBundle.h"
#include "nsXPCOM.h"
#include "nsISupportsPrimitives.h"
#include "nsIURL.h"
#include "nsIIDNService.h"
#include "nsIStreamListenerTee.h"
#include "nsISeekableStream.h"
#include "nsMimeTypes.h"
#include "nsNetUtil.h"
#include "nsString.h"
#include "nsPrintfCString.h"
#include "nsReadableUtils.h"
#include "nsUnicharUtils.h"
#include "nsAutoPtr.h"
#include "plstr.h"
#include "prprf.h"
#include "nsEscape.h"
#include "nsICookieService.h"
#include "nsIResumableChannel.h"
#include "nsInt64.h"
#include "nsIVariant.h"
#include "nsChannelProperties.h"
#include "nsStreamUtils.h"
#include "nsIOService.h"
#include "nsAuthInformationHolder.h"
#include "nsICacheService.h"
#include "nsDNSPrefetch.h"

// True if the local cache should be bypassed when processing a request.
#define BYPASS_LOCAL_CACHE(loadFlags) \
        (loadFlags & (nsIRequest::LOAD_BYPASS_CACHE | \
                      nsICachingChannel::LOAD_BYPASS_LOCAL_CACHE))

static NS_DEFINE_CID(kStreamListenerTeeCID, NS_STREAMLISTENERTEE_CID);

//-----------------------------------------------------------------------------
// nsHttpChannel <public>
//-----------------------------------------------------------------------------

nsHttpChannel::nsHttpChannel()
    : mResponseHead(nsnull)
    , mTransaction(nsnull)
    , mConnectionInfo(nsnull)
    , mLoadFlags(LOAD_NORMAL)
    , mStatus(NS_OK)
    , mLogicalOffset(0)
    , mCaps(0)
    , mPriority(PRIORITY_NORMAL)
    , mCachedResponseHead(nsnull)
    , mCacheAccess(0)
    , mPostID(0)
    , mRequestTime(0)
    , mProxyAuthContinuationState(nsnull)
    , mAuthContinuationState(nsnull)
    , mStartPos(LL_MAXUINT)
    , mPendingAsyncCallOnResume(nsnull)
    , mSuspendCount(0)
    , mRedirectionLimit(gHttpHandler->RedirectionLimit())
    , mIsPending(PR_FALSE)
    , mWasOpened(PR_FALSE)
    , mApplyConversion(PR_TRUE)
    , mAllowPipelining(PR_TRUE)
    , mCachedContentIsValid(PR_FALSE)
    , mCachedContentIsPartial(PR_FALSE)
    , mResponseHeadersModified(PR_FALSE)
    , mCanceled(PR_FALSE)
    , mTransactionReplaced(PR_FALSE)
    , mUploadStreamHasHeaders(PR_FALSE)
    , mAuthRetryPending(PR_FALSE)
    , mProxyAuth(PR_FALSE)
    , mSuppressDefensiveAuth(PR_FALSE)
    , mResuming(PR_FALSE)
    , mInitedCacheEntry(PR_FALSE)
    , mCacheForOfflineUse(PR_FALSE)
    , mCachingOpportunistically(PR_FALSE)
    , mFallbackChannel(PR_FALSE)
    , mInheritApplicationCache(PR_TRUE)
    , mChooseApplicationCache(PR_FALSE)
    , mLoadedFromApplicationCache(PR_FALSE)
    , mTracingEnabled(PR_TRUE)
    , mForceAllowThirdPartyCookie(PR_FALSE)
{
    LOG(("Creating nsHttpChannel [this=%p]\n", this));

    // grab a reference to the handler to ensure that it doesn't go away.
    nsHttpHandler *handler = gHttpHandler;
    NS_ADDREF(handler);
}

nsHttpChannel::~nsHttpChannel()
{
    LOG(("Destroying nsHttpChannel [this=%p]\n", this));

    NS_IF_RELEASE(mConnectionInfo);
    NS_IF_RELEASE(mTransaction);

    NS_IF_RELEASE(mProxyAuthContinuationState);
    NS_IF_RELEASE(mAuthContinuationState);

    delete mResponseHead;
    delete mCachedResponseHead;

    // release our reference to the handler
    nsHttpHandler *handler = gHttpHandler;
    NS_RELEASE(handler);
}

nsresult
nsHttpChannel::Init(nsIURI *uri,
                    PRUint8 caps,
                    nsProxyInfo *proxyInfo)
{
    LOG(("nsHttpChannel::Init [this=%p]\n", this));

    NS_PRECONDITION(uri, "null uri");

    nsresult rv = nsHashPropertyBag::Init();
    if (NS_FAILED(rv))
        return rv;

    mURI = uri;
    mOriginalURI = uri;
    mDocumentURI = nsnull;
    mCaps = caps;

    //
    // Construct connection info object
    //
    nsCAutoString host;
    PRInt32 port = -1;
    PRBool usingSSL = PR_FALSE;
    
    rv = mURI->SchemeIs("https", &usingSSL);
    if (NS_FAILED(rv)) return rv;

    rv = mURI->GetAsciiHost(host);
    if (NS_FAILED(rv)) return rv;

    // reject the URL if it doesn't specify a host
    if (host.IsEmpty())
        return NS_ERROR_MALFORMED_URI;

    rv = mURI->GetPort(&port);
    if (NS_FAILED(rv)) return rv;

    LOG(("host=%s port=%d\n", host.get(), port));

    rv = mURI->GetAsciiSpec(mSpec);
    if (NS_FAILED(rv)) return rv;

    LOG(("uri=%s\n", mSpec.get()));

    mConnectionInfo = new nsHttpConnectionInfo(host, port,
                                               proxyInfo, usingSSL);
    if (!mConnectionInfo)
        return NS_ERROR_OUT_OF_MEMORY;
    NS_ADDREF(mConnectionInfo);

    // Set default request method
    mRequestHead.SetMethod(nsHttp::Get);

    //
    // Set request headers
    //
    nsCAutoString hostLine;
    if (strchr(host.get(), ':')) {
        // host is an IPv6 address literal and must be encapsulated in []'s
        hostLine.Assign('[');
        // scope id is not needed for Host header.
        int scopeIdPos = host.FindChar('%');
        if (scopeIdPos == kNotFound)
            hostLine.Append(host);
        else if (scopeIdPos > 0)
            hostLine.Append(Substring(host, 0, scopeIdPos));
        else
          return NS_ERROR_MALFORMED_URI;
        hostLine.Append(']');
    }
    else
        hostLine.Assign(host);
    if (port != -1) {
        hostLine.Append(':');
        hostLine.AppendInt(port);
    }

    rv = mRequestHead.SetHeader(nsHttp::Host, hostLine);
    if (NS_FAILED(rv)) return rv;

    rv = gHttpHandler->
        AddStandardRequestHeaders(&mRequestHead.Headers(), caps,
                                  !mConnectionInfo->UsingSSL() &&
                                  mConnectionInfo->UsingHttpProxy());

    return rv;
}

//-----------------------------------------------------------------------------
// nsHttpChannel <private>
//-----------------------------------------------------------------------------

nsresult
nsHttpChannel::AsyncCall(nsAsyncCallback funcPtr,
                         nsRunnableMethod<nsHttpChannel> **retval)
{
    nsresult rv;

    nsRefPtr<nsRunnableMethod<nsHttpChannel> > event =
            new nsRunnableMethod<nsHttpChannel>(this, funcPtr);
    rv = NS_DispatchToCurrentThread(event);
    if (NS_SUCCEEDED(rv) && retval) {
        *retval = event;
    }

    return rv;
}

PRBool
nsHttpChannel::RequestIsConditional()
{
    // Is our consumer issuing a conditional request?
    return mRequestHead.PeekHeader(nsHttp::If_Modified_Since) ||
           mRequestHead.PeekHeader(nsHttp::If_None_Match) ||
           mRequestHead.PeekHeader(nsHttp::If_Unmodified_Since) ||
           mRequestHead.PeekHeader(nsHttp::If_Match) ||
           mRequestHead.PeekHeader(nsHttp::If_Range);
}

nsresult
nsHttpChannel::Connect(PRBool firstTime)
{
    nsresult rv;

    LOG(("nsHttpChannel::Connect [this=%p]\n", this));

    // ensure that we are using a valid hostname
    if (!net_IsValidHostName(nsDependentCString(mConnectionInfo->Host())))
        return NS_ERROR_UNKNOWN_HOST;

    // true when called from AsyncOpen
    if (firstTime) {
        PRBool delayed = PR_FALSE;

        // are we offline?
        PRBool offline = gIOService->IsOffline();
        if (offline)
            mLoadFlags |= LOAD_ONLY_FROM_CACHE;
        else if (PL_strcmp(mConnectionInfo->ProxyType(), "unknown") == 0)
            return ResolveProxy();  // Lazily resolve proxy info

        // Don't allow resuming when cache must be used
        if (mResuming && (mLoadFlags & LOAD_ONLY_FROM_CACHE)) {
            LOG(("Resuming from cache is not supported yet"));
            return NS_ERROR_DOCUMENT_NOT_CACHED;
        }

        // open a cache entry for this channel...
        rv = OpenCacheEntry(offline, &delayed);

        if (NS_FAILED(rv)) {
            LOG(("OpenCacheEntry failed [rv=%x]\n", rv));
            // if this channel is only allowed to pull from the cache, then
            // we must fail if we were unable to open a cache entry.
            if (mLoadFlags & LOAD_ONLY_FROM_CACHE) {
                // If we have a fallback URI (and we're not already
                // falling back), process the fallback asynchronously.
                if (!mFallbackChannel && !mFallbackKey.IsEmpty()) {
                    return AsyncCall(&nsHttpChannel::HandleAsyncFallback);
                }
                return NS_ERROR_DOCUMENT_NOT_CACHED;
            }
            // otherwise, let's just proceed without using the cache.
        }

        // if cacheForOfflineUse has been set, open up an offline cache
        // entry to update
        if (mCacheForOfflineUse) {
            rv = OpenOfflineCacheEntryForWriting();
            if (NS_FAILED(rv)) return rv;
        }

        if (NS_SUCCEEDED(rv) && delayed)
            return NS_OK;
    }

    // we may or may not have a cache entry at this point
    if (mCacheEntry) {
        // inspect the cache entry to determine whether or not we need to go
        // out to net to validate it.  this call sets mCachedContentIsValid
        // and may set request headers as required for cache validation.
        rv = CheckCache();
        if (NS_FAILED(rv))
            NS_WARNING("cache check failed");

        // read straight from the cache if possible...
        if (mCachedContentIsValid) {
            nsRunnableMethod<nsHttpChannel> *event = nsnull;
            if (!mCachedContentIsPartial) {
                AsyncCall(&nsHttpChannel::AsyncOnExamineCachedResponse, &event);
            }
            rv = ReadFromCache();
            if (NS_FAILED(rv) && event) {
                event->Revoke();
            }
            return rv;
        }
        else if (mLoadFlags & LOAD_ONLY_FROM_CACHE) {
            // the cache contains the requested resource, but it must be 
            // validated before we can reuse it.  since we are not allowed
            // to hit the net, there's nothing more to do.  the document
            // is effectively not in the cache.
            return NS_ERROR_DOCUMENT_NOT_CACHED;
        }
    }

    // check to see if authorization headers should be included
    AddAuthorizationHeaders();

    if (mLoadFlags & LOAD_NO_NETWORK_IO) {
        return NS_ERROR_DOCUMENT_NOT_CACHED;
    }

    // hit the net...
    rv = SetupTransaction();
    if (NS_FAILED(rv)) return rv;

    rv = gHttpHandler->InitiateTransaction(mTransaction, mPriority);
    if (NS_FAILED(rv)) return rv;

    return mTransactionPump->AsyncRead(this, nsnull);
}

// called when Connect fails
nsresult
nsHttpChannel::AsyncAbort(nsresult status)
{
    LOG(("nsHttpChannel::AsyncAbort [this=%p status=%x]\n", this, status));

    mStatus = status;
    mIsPending = PR_FALSE;

    nsresult rv = AsyncCall(&nsHttpChannel::HandleAsyncNotifyListener);
    // And if that fails?  Callers ignore our return value anyway....
    
    // finally remove ourselves from the load group.
    if (mLoadGroup)
        mLoadGroup->RemoveRequest(this, nsnull, status);

    return rv;
}

void
nsHttpChannel::HandleAsyncNotifyListener()
{
    NS_PRECONDITION(!mPendingAsyncCallOnResume, "How did that happen?");
    
    if (mSuspendCount) {
        LOG(("Waiting until resume to do async notification [this=%p]\n",
             this));
        mPendingAsyncCallOnResume = &nsHttpChannel::HandleAsyncNotifyListener;
        return;
    }

    DoNotifyListener();
}

void
nsHttpChannel::DoNotifyListener()
{
    if (mListener) {
        mListener->OnStartRequest(this, mListenerContext);
        mListener->OnStopRequest(this, mListenerContext, mStatus);
        mListener = 0;
        mListenerContext = 0;
    }
    // We have to make sure to drop the reference to the callbacks too
    mCallbacks = nsnull;
    mProgressSink = nsnull;
}

void
nsHttpChannel::HandleAsyncRedirect()
{
    NS_PRECONDITION(!mPendingAsyncCallOnResume, "How did that happen?");
    
    if (mSuspendCount) {
        LOG(("Waiting until resume to do async redirect [this=%p]\n", this));
        mPendingAsyncCallOnResume = &nsHttpChannel::HandleAsyncRedirect;
        return;
    }

    nsresult rv = NS_OK;

    LOG(("nsHttpChannel::HandleAsyncRedirect [this=%p]\n", this));

    // since this event is handled asynchronously, it is possible that this
    // channel could have been canceled, in which case there would be no point
    // in processing the redirect.
    if (NS_SUCCEEDED(mStatus)) {
        rv = ProcessRedirection(mResponseHead->Status());
        if (NS_FAILED(rv)) {
            // If ProcessRedirection fails, then we have to send out the
            // OnStart/OnStop notifications.
            LOG(("ProcessRedirection failed [rv=%x]\n", rv));
            mStatus = rv;
            DoNotifyListener();
        }
    }

    // close the cache entry.  Blow it away if we couldn't process the redirect
    // for some reason (the cache entry might be corrupt).
    if (mCacheEntry) {
        if (NS_FAILED(rv))
            mCacheEntry->Doom();
        CloseCacheEntry(PR_FALSE);
    }

    mIsPending = PR_FALSE;

    if (mLoadGroup)
        mLoadGroup->RemoveRequest(this, nsnull, mStatus);
}

void
nsHttpChannel::HandleAsyncNotModified()
{
    NS_PRECONDITION(!mPendingAsyncCallOnResume, "How did that happen?");
    
    if (mSuspendCount) {
        LOG(("Waiting until resume to do async not-modified [this=%p]\n",
             this));
        mPendingAsyncCallOnResume = &nsHttpChannel::HandleAsyncNotModified;
        return;
    }
    
    LOG(("nsHttpChannel::HandleAsyncNotModified [this=%p]\n", this));

    DoNotifyListener();

    CloseCacheEntry(PR_TRUE);

    mIsPending = PR_FALSE;

    if (mLoadGroup)
        mLoadGroup->RemoveRequest(this, nsnull, mStatus);
}

void
nsHttpChannel::HandleAsyncFallback()
{
    NS_PRECONDITION(!mPendingAsyncCallOnResume, "How did that happen?");

    if (mSuspendCount) {
        LOG(("Waiting until resume to do async fallback [this=%p]\n", this));
        mPendingAsyncCallOnResume = &nsHttpChannel::HandleAsyncFallback;
        return;
    }

    nsresult rv = NS_OK;

    LOG(("nsHttpChannel::HandleAsyncFallback [this=%p]\n", this));

    // since this event is handled asynchronously, it is possible that this
    // channel could have been canceled, in which case there would be no point
    // in processing the fallback.
    if (!mCanceled) {
        PRBool fallingBack;
        rv = ProcessFallback(&fallingBack);
        if (NS_FAILED(rv) || !fallingBack) {
            // If ProcessFallback fails, then we have to send out the
            // OnStart/OnStop notifications.
            LOG(("ProcessFallback failed [rv=%x, %d]\n", rv, fallingBack));
            mStatus = NS_FAILED(rv) ? rv : NS_ERROR_DOCUMENT_NOT_CACHED;
            DoNotifyListener();
        }
    }

    mIsPending = PR_FALSE;

    if (mLoadGroup)
        mLoadGroup->RemoveRequest(this, nsnull, mStatus);
}

nsresult
nsHttpChannel::SetupTransaction()
{
    LOG(("nsHttpChannel::SetupTransaction [this=%p]\n", this));

    NS_ENSURE_TRUE(!mTransaction, NS_ERROR_ALREADY_INITIALIZED);

    nsresult rv;

    if (mCaps & NS_HTTP_ALLOW_PIPELINING) {
        //
        // disable pipelining if:
        //   (1) pipelining has been explicitly disabled
        //   (2) request corresponds to a top-level document load (link click)
        //   (3) request method is non-idempotent
        //
        // XXX does the toplevel document check really belong here?  or, should
        //     we push it out entirely to necko consumers?
        //
        if (!mAllowPipelining || (mLoadFlags & LOAD_INITIAL_DOCUMENT_URI) ||
            !(mRequestHead.Method() == nsHttp::Get ||
              mRequestHead.Method() == nsHttp::Head ||
              mRequestHead.Method() == nsHttp::Propfind ||
              mRequestHead.Method() == nsHttp::Proppatch)) {
            LOG(("  pipelining disallowed\n"));
            mCaps &= ~NS_HTTP_ALLOW_PIPELINING;
        }
    }

    // use the URI path if not proxying (transparent proxying such as SSL proxy
    // does not count here). also, figure out what version we should be speaking.
    nsCAutoString buf, path;
    nsCString* requestURI;
    if (mConnectionInfo->UsingSSL() || !mConnectionInfo->UsingHttpProxy()) {
        rv = mURI->GetPath(path);
        if (NS_FAILED(rv)) return rv;
        // path may contain UTF-8 characters, so ensure that they're escaped.
        if (NS_EscapeURL(path.get(), path.Length(), esc_OnlyNonASCII, buf))
            requestURI = &buf;
        else
            requestURI = &path;
        mRequestHead.SetVersion(gHttpHandler->HttpVersion());
    }
    else {
        rv = mURI->GetUserPass(buf);
        if (NS_FAILED(rv)) return rv;
        if (!buf.IsEmpty() && ((strncmp(mSpec.get(), "http:", 5) == 0) ||
                                strncmp(mSpec.get(), "https:", 6) == 0)) {
            nsCOMPtr<nsIURI> tempURI;
            rv = mURI->Clone(getter_AddRefs(tempURI));
            if (NS_FAILED(rv)) return rv;
            rv = tempURI->SetUserPass(EmptyCString());
            if (NS_FAILED(rv)) return rv;
            rv = tempURI->GetAsciiSpec(path);
            if (NS_FAILED(rv)) return rv;
            requestURI = &path;
        }
        else
            requestURI = &mSpec;
        mRequestHead.SetVersion(gHttpHandler->ProxyHttpVersion());
    }

    // trim off the #ref portion if any...
    PRInt32 ref = requestURI->FindChar('#');
    if (ref != kNotFound)
        requestURI->SetLength(ref);

    mRequestHead.SetRequestURI(*requestURI);

    // set the request time for cache expiration calculations
    mRequestTime = NowInSeconds();

    // if doing a reload, force end-to-end
    if (mLoadFlags & LOAD_BYPASS_CACHE) {
        // We need to send 'Pragma:no-cache' to inhibit proxy caching even if
        // no proxy is configured since we might be talking with a transparent
        // proxy, i.e. one that operates at the network level.  See bug #14772.
        mRequestHead.SetHeader(nsHttp::Pragma, NS_LITERAL_CSTRING("no-cache"), PR_TRUE);
        // If we're configured to speak HTTP/1.1 then also send 'Cache-control:
        // no-cache'
        if (mRequestHead.Version() >= NS_HTTP_VERSION_1_1)
            mRequestHead.SetHeader(nsHttp::Cache_Control, NS_LITERAL_CSTRING("no-cache"), PR_TRUE);
    }
    else if ((mLoadFlags & VALIDATE_ALWAYS) && (mCacheAccess & nsICache::ACCESS_READ)) {
        // We need to send 'Cache-Control: max-age=0' to force each cache along
        // the path to the origin server to revalidate its own entry, if any,
        // with the next cache or server.  See bug #84847.
        //
        // If we're configured to speak HTTP/1.0 then just send 'Pragma: no-cache'
        if (mRequestHead.Version() >= NS_HTTP_VERSION_1_1)
            mRequestHead.SetHeader(nsHttp::Cache_Control, NS_LITERAL_CSTRING("max-age=0"), PR_TRUE);
        else
            mRequestHead.SetHeader(nsHttp::Pragma, NS_LITERAL_CSTRING("no-cache"), PR_TRUE);
    }

    if (mResuming) {
        char byteRange[32];
        PR_snprintf(byteRange, sizeof(byteRange), "bytes=%llu-", mStartPos);
        mRequestHead.SetHeader(nsHttp::Range, nsDependentCString(byteRange));

        if (!mEntityID.IsEmpty()) {
            // Also, we want an error if this resource changed in the meantime
            // Format of the entity id is: escaped_etag/size/lastmod
            nsCString::const_iterator start, end, slash;
            mEntityID.BeginReading(start);
            mEntityID.EndReading(end);
            mEntityID.BeginReading(slash);

            if (FindCharInReadable('/', slash, end)) {
                nsCAutoString ifMatch;
                mRequestHead.SetHeader(nsHttp::If_Match,
                        NS_UnescapeURL(Substring(start, slash), 0, ifMatch));

                ++slash; // Incrementing, so that searching for '/' won't find
                         // the same slash again
            }

            if (FindCharInReadable('/', slash, end)) {
                mRequestHead.SetHeader(nsHttp::If_Unmodified_Since,
                        Substring(++slash, end));
            }
        }
    }

    // create wrapper for this channel's notification callbacks
    nsCOMPtr<nsIInterfaceRequestor> callbacks;
    NS_NewNotificationCallbacksAggregation(mCallbacks, mLoadGroup,
                                           getter_AddRefs(callbacks));
    if (!callbacks)
        return NS_ERROR_OUT_OF_MEMORY;

    // create the transaction object
    mTransaction = new nsHttpTransaction();
    if (!mTransaction)
        return NS_ERROR_OUT_OF_MEMORY;
    NS_ADDREF(mTransaction);

    // See bug #466080. Transfer LOAD_ANONYMOUS flag to socket-layer.
    if (mLoadFlags & LOAD_ANONYMOUS) {
        mCaps |= NS_HTTP_LOAD_ANONYMOUS;
        mConnectionInfo->SetAnonymous();
    }

    nsCOMPtr<nsIAsyncInputStream> responseStream;
    rv = mTransaction->Init(mCaps, mConnectionInfo, &mRequestHead,
                            mUploadStream, mUploadStreamHasHeaders,
                            NS_GetCurrentThread(), callbacks, this,
                            getter_AddRefs(responseStream));
    if (NS_FAILED(rv)) {
        NS_RELEASE(mTransaction);
        return rv;
    }

    rv = nsInputStreamPump::Create(getter_AddRefs(mTransactionPump),
                                   responseStream);
    return rv;
}

void
nsHttpChannel::AddCookiesToRequest()
{
    if (mLoadFlags & LOAD_ANONYMOUS) {
      return;
    }

    nsXPIDLCString cookie;

    nsICookieService *cs = gHttpHandler->GetCookieService();
    if (cs)
        cs->GetCookieStringFromHttp(mURI,
                                    mDocumentURI ? mDocumentURI : mOriginalURI,
                                    this,
                                    getter_Copies(cookie));
    if (cookie.IsEmpty())
        cookie = mUserSetCookieHeader;
    else if (!mUserSetCookieHeader.IsEmpty())
        cookie.Append(NS_LITERAL_CSTRING("; ") + mUserSetCookieHeader);

    // overwrite any existing cookie headers.  be sure to clear any
    // existing cookies if we have no cookies to set or if the cookie
    // service is unavailable.
    mRequestHead.SetHeader(nsHttp::Cookie, cookie, PR_FALSE);
}

nsresult
nsHttpChannel::ApplyContentConversions()
{
    if (!mResponseHead)
        return NS_OK;

    LOG(("nsHttpChannel::ApplyContentConversions [this=%p]\n", this));

    if (!mApplyConversion) {
        LOG(("not applying conversion per mApplyConversion\n"));
        return NS_OK;
    }

    const char *val = mResponseHead->PeekHeader(nsHttp::Content_Encoding);
    if (gHttpHandler->IsAcceptableEncoding(val)) {
        nsCOMPtr<nsIStreamConverterService> serv;
        nsresult rv = gHttpHandler->
                GetStreamConverterService(getter_AddRefs(serv));
        // we won't fail to load the page just because we couldn't load the
        // stream converter service.. carry on..
        if (NS_SUCCEEDED(rv)) {
            nsCOMPtr<nsIStreamListener> converter;
            nsCAutoString from(val);
            ToLowerCase(from);
            rv = serv->AsyncConvertData(from.get(),
                                        "uncompressed",
                                        mListener,
                                        mListenerContext,
                                        getter_AddRefs(converter));
            if (NS_SUCCEEDED(rv)) {
                LOG(("converter installed from \'%s\' to \'uncompressed\'\n", val));
                mListener = converter;
            }
        }
    } else if (val != nsnull) {
        LOG(("Unknown content encoding '%s', ignoring\n", val));
    }

    return NS_OK;
}

// NOTE: This function duplicates code from nsBaseChannel. This will go away
// once HTTP uses nsBaseChannel (part of bug 312760)
static void
CallTypeSniffers(void *aClosure, const PRUint8 *aData, PRUint32 aCount)
{
  nsIChannel *chan = static_cast<nsIChannel*>(aClosure);

  const nsCOMArray<nsIContentSniffer>& sniffers =
    gIOService->GetContentSniffers();
  PRUint32 length = sniffers.Count();
  for (PRUint32 i = 0; i < length; ++i) {
    nsCAutoString newType;
    nsresult rv =
      sniffers[i]->GetMIMETypeFromContent(chan, aData, aCount, newType);
    if (NS_SUCCEEDED(rv) && !newType.IsEmpty()) {
      chan->SetContentType(newType);
      break;
    }
  }
}

nsresult
nsHttpChannel::CallOnStartRequest()
{
    mTracingEnabled = PR_FALSE;

    if (mResponseHead && mResponseHead->ContentType().IsEmpty()) {
        NS_ASSERTION(mConnectionInfo, "Should have connection info here");
        if (!mContentTypeHint.IsEmpty())
            mResponseHead->SetContentType(mContentTypeHint);
        else if (mResponseHead->Version() == NS_HTTP_VERSION_0_9 &&
                 mConnectionInfo->Port() != mConnectionInfo->DefaultPort())
            mResponseHead->SetContentType(NS_LITERAL_CSTRING(TEXT_PLAIN));
        else {
            // Uh-oh.  We had better find out what type we are!

            // XXX This does not work with content-encodings...  but
            // neither does applying the conversion from the URILoader

            nsCOMPtr<nsIStreamConverterService> serv;
            nsresult rv = gHttpHandler->
                GetStreamConverterService(getter_AddRefs(serv));
            // If we failed, we just fall through to the "normal" case
            if (NS_SUCCEEDED(rv)) {
                nsCOMPtr<nsIStreamListener> converter;
                rv = serv->AsyncConvertData(UNKNOWN_CONTENT_TYPE,
                                            "*/*",
                                            mListener,
                                            mListenerContext,
                                            getter_AddRefs(converter));
                if (NS_SUCCEEDED(rv)) {
                    mListener = converter;
                }
            }
        }
    }

    if (mResponseHead && mResponseHead->ContentCharset().IsEmpty())
        mResponseHead->SetContentCharset(mContentCharsetHint);

    if (mResponseHead)
        SetPropertyAsInt64(NS_CHANNEL_PROP_CONTENT_LENGTH,
                           mResponseHead->ContentLength());

    // Allow consumers to override our content type
    if ((mLoadFlags & LOAD_CALL_CONTENT_SNIFFERS) &&
        gIOService->GetContentSniffers().Count() != 0) {
        // NOTE: We can have both a txn pump and a cache pump when the cache
        // content is partial. In that case, we need to read from the cache,
        // because that's the one that has the initial contents. If that fails
        // then give the transaction pump a shot.

        nsIChannel* thisChannel = static_cast<nsIChannel*>(this);

        PRBool typeSniffersCalled = PR_FALSE;
        if (mCachePump) {
          typeSniffersCalled =
            NS_SUCCEEDED(mCachePump->PeekStream(CallTypeSniffers, thisChannel));
        }
        
        if (!typeSniffersCalled && mTransactionPump) {
          mTransactionPump->PeekStream(CallTypeSniffers, thisChannel);
        }
    }

    LOG(("  calling mListener->OnStartRequest\n"));
    nsresult rv = mListener->OnStartRequest(this, mListenerContext);
    if (NS_FAILED(rv)) return rv;

    // install stream converter if required
    rv = ApplyContentConversions();
    if (NS_FAILED(rv)) return rv;

    if (!mCanceled) {
        // create offline cache entry if offline caching was requested
        if (mCacheForOfflineUse) {
            PRBool shouldCacheForOfflineUse;
            rv = ShouldUpdateOfflineCacheEntry(&shouldCacheForOfflineUse);
            if (NS_FAILED(rv)) return rv;
            
            if (shouldCacheForOfflineUse) {
                LOG(("writing to the offline cache"));
                rv = InitOfflineCacheEntry();
                if (NS_FAILED(rv)) return rv;
                
                if (mOfflineCacheEntry) {
                  rv = InstallOfflineCacheListener();
                  if (NS_FAILED(rv)) return rv;
                }
            } else {
                LOG(("offline cache is up to date, not updating"));
                CloseOfflineCacheEntry();
            }
        }
    }

    return NS_OK;
}

nsresult
nsHttpChannel::ProcessFailedSSLConnect(PRUint32 httpStatus)
{
    // Failure to set up SSL proxy tunnel means one of the following:
    // 1) Proxy wants authorization, or forbids.
    // 2) DNS at proxy couldn't resolve target URL.
    // 3) Proxy connection to target failed or timed out.
    // 4) Eve noticed our proxy CONNECT, and is replying with malicious HTML.
    // 
    // Our current architecture will parse response content with the
    // permission of the target URL!  Given #4, we must avoid rendering the
    // body of the reply, and instead give the user a (hopefully helpful) 
    // boilerplate error page, based on just the HTTP status of the reply.

    NS_ABORT_IF_FALSE(mConnectionInfo->UsingSSL(),
                      "SSL connect failed but not using SSL?");
    nsresult rv;
    switch (httpStatus) 
    {
    case 300: case 301: case 302: case 303: case 307:
        // Bad redirect: not top-level, or it's a POST, bad/missing Location,
        // or ProcessRedirect() failed for some other reason.  Legal
        // redirects that fail because site not available, etc., are handled
        // elsewhere, in the regular codepath.
        rv = NS_ERROR_CONNECTION_REFUSED;
        break;
    case 403: // HTTP/1.1: "Forbidden"
    case 407: // ProcessAuthentication() failed
    case 501: // HTTP/1.1: "Not Implemented"
        // user sees boilerplate Mozilla "Proxy Refused Connection" page.
        rv = NS_ERROR_PROXY_CONNECTION_REFUSED; 
        break;
    // Squid sends 404 if DNS fails (regular 404 from target is tunneled)
    case 404: // HTTP/1.1: "Not Found"
    // RFC 2616: "some deployed proxies are known to return 400 or 500 when
    // DNS lookups time out."  (Squid uses 500 if it runs out of sockets: so
    // we have a conflict here).
    case 400: // HTTP/1.1 "Bad Request"
    case 500: // HTTP/1.1: "Internal Server Error"
        /* User sees: "Address Not Found: Firefox can't find the server at
         * www.foo.com."
         */
        rv = NS_ERROR_UNKNOWN_HOST; 
        break;
    case 502: // HTTP/1.1: "Bad Gateway" (invalid resp from target server)
    // Squid returns 503 if target request fails for anything but DNS.
    case 503: // HTTP/1.1: "Service Unavailable"
        /* User sees: "Failed to Connect:
         *  Firefox can't establish a connection to the server at
         *  www.foo.com.  Though the site seems valid, the browser
         *  was unable to establish a connection."
         */
        rv = NS_ERROR_CONNECTION_REFUSED;
        break;
    // RFC 2616 uses 504 for both DNS and target timeout, so not clear what to
    // do here: picking target timeout, as DNS covered by 400/404/500
    case 504: // HTTP/1.1: "Gateway Timeout" 
        // user sees: "Network Timeout: The server at www.foo.com
        //              is taking too long to respond."
        rv = NS_ERROR_NET_TIMEOUT;
        break;
    // Confused proxy server or malicious response
    default:
        rv = NS_ERROR_PROXY_CONNECTION_REFUSED; 
        break;
    }
    LOG(("Cancelling failed SSL proxy connection [this=%p httpStatus=%u]\n",
         this, httpStatus)); 
    Cancel(rv);
    CallOnStartRequest();
    return rv;
}

PRBool
nsHttpChannel::ShouldSSLProxyResponseContinue(PRUint32 httpStatus)
{
    // When SSL connect has failed, allow proxy reply to continue only if it's
    // an auth request, or a redirect of a non-POST top-level document load.
    switch (httpStatus) {
    case 407:
        return PR_TRUE;
    case 300: case 301: case 302: case 303: case 307:
      {
        return ( (mLoadFlags & nsIChannel::LOAD_DOCUMENT_URI) &&
                 mURI == mDocumentURI &&
                 mRequestHead.Method() != nsHttp::Post);
      }
    }
    return PR_FALSE;
}

nsresult
nsHttpChannel::ProcessResponse()
{
    nsresult rv;
    PRUint32 httpStatus = mResponseHead->Status();

    LOG(("nsHttpChannel::ProcessResponse [this=%p httpStatus=%u]\n",
        this, httpStatus));

    if (mTransaction->SSLConnectFailed() &&
        !ShouldSSLProxyResponseContinue(httpStatus))
        return ProcessFailedSSLConnect(httpStatus);

    // notify "http-on-examine-response" observers
    gHttpHandler->OnExamineResponse(this);

    // set cookies, if any exist; done after OnExamineResponse to allow those
    // observers to modify the cookie response headers
    SetCookie(mResponseHead->PeekHeader(nsHttp::Set_Cookie));

    // handle unused username and password in url (see bug 232567)
    if (httpStatus != 401 && httpStatus != 407) {
        CheckForSuperfluousAuth();
        if (mCanceled)
            return CallOnStartRequest();

        if (mAuthContinuationState) {
            // reset the current continuation state because our last
            // authentication attempt has been completed successfully
            NS_RELEASE(mAuthContinuationState);
            LOG(("  continuation state has been reset"));
        }
    }

    // handle different server response categories.  Note that we handle
    // caching or not caching of error pages in
    // nsHttpResponseHead::MustValidate; if you change this switch, update that
    // one
    switch (httpStatus) {
    case 200:
    case 203:
        // Per RFC 2616, 14.35.2, "A server MAY ignore the Range header".
        // So if a server does that and sends 200 instead of 206 that we
        // expect, notify our caller.
        // However, if we wanted to start from the beginning, let it go through
        if (mResuming && mStartPos != 0) {
            LOG(("Server ignored our Range header, cancelling [this=%p]\n", this));
            Cancel(NS_ERROR_NOT_RESUMABLE);
            rv = CallOnStartRequest();
            break;
        }
        // these can normally be cached
        rv = ProcessNormal();
        MaybeInvalidateCacheEntryForSubsequentGet();
        break;
    case 206:
        if (mCachedContentIsPartial) // an internal byte range request...
            rv = ProcessPartialContent();
        else
            rv = ProcessNormal();
        break;
    case 300:
    case 301:
    case 302:
    case 307:
    case 303:
#if 0
    case 305: // disabled as a security measure (see bug 187996).
#endif
        // don't store the response body for redirects
        MaybeInvalidateCacheEntryForSubsequentGet();
        rv = ProcessRedirection(httpStatus);
        if (NS_SUCCEEDED(rv)) {
            InitCacheEntry();
            CloseCacheEntry(PR_FALSE);

            if (mCacheForOfflineUse) {
                // Store response in the offline cache
                InitOfflineCacheEntry();
                CloseOfflineCacheEntry();
            }
        }    
        else {
            LOG(("ProcessRedirection failed [rv=%x]\n", rv));
            if (mTransaction->SSLConnectFailed())
                return ProcessFailedSSLConnect(httpStatus);
            rv = ProcessNormal();
        }
        break;
    case 304:
        rv = ProcessNotModified();
        if (NS_FAILED(rv)) {
            LOG(("ProcessNotModified failed [rv=%x]\n", rv));
            rv = ProcessNormal();
        }
        break;
    case 401:
    case 407:
        rv = ProcessAuthentication(httpStatus);
        if (NS_FAILED(rv)) {
            LOG(("ProcessAuthentication failed [rv=%x]\n", rv));
            if (mTransaction->SSLConnectFailed())
                return ProcessFailedSSLConnect(httpStatus);
            CheckForSuperfluousAuth();
            rv = ProcessNormal();
        }
        break;
    default:
        rv = ProcessNormal();
        MaybeInvalidateCacheEntryForSubsequentGet();
        break;
    }

    return rv;
}

nsresult
nsHttpChannel::ProcessNormal()
{
    nsresult rv;

    LOG(("nsHttpChannel::ProcessNormal [this=%p]\n", this));

    PRBool succeeded;
    rv = GetRequestSucceeded(&succeeded);
    if (NS_SUCCEEDED(rv) && !succeeded) {
        PRBool fallingBack;
        rv = ProcessFallback(&fallingBack);
        if (NS_FAILED(rv)) {
            DoNotifyListener();
            return rv;
        }

        if (fallingBack) {
            // Do not continue with normal processing, fallback is in
            // progress now.
            return NS_OK;
        }
    }

    // if we're here, then any byte-range requests failed to result in a partial
    // response.  we must clear this flag to prevent BufferPartialContent from
    // being called inside our OnDataAvailable (see bug 136678).
    mCachedContentIsPartial = PR_FALSE;

    ClearBogusContentEncodingIfNeeded();

    // this must be called before firing OnStartRequest, since http clients,
    // such as imagelib, expect our cache entry to already have the correct
    // expiration time (bug 87710).
    if (mCacheEntry) {
        rv = InitCacheEntry();
        if (NS_FAILED(rv))
            CloseCacheEntry(PR_TRUE);
    }

    // Check that the server sent us what we were asking for
    if (mResuming) {
        // Create an entity id from the response
        nsCAutoString id;
        rv = GetEntityID(id);
        if (NS_FAILED(rv)) {
            // If creating an entity id is not possible -> error
            Cancel(NS_ERROR_NOT_RESUMABLE);
        }
        else if (mResponseHead->Status() != 206 &&
                 mResponseHead->Status() != 200) {
            // Probably 404 Not Found, 412 Precondition Failed or
            // 416 Invalid Range -> error
            LOG(("Unexpected response status while resuming, aborting [this=%p]\n",
                 this));
            Cancel(NS_ERROR_ENTITY_CHANGED);
        }
        // If we were passed an entity id, verify it's equal to the server's
        else if (!mEntityID.IsEmpty()) {
            if (!mEntityID.Equals(id)) {
                LOG(("Entity mismatch, expected '%s', got '%s', aborting [this=%p]",
                     mEntityID.get(), id.get(), this));
                Cancel(NS_ERROR_ENTITY_CHANGED);
            }
        }
    }

    rv = CallOnStartRequest();
    if (NS_FAILED(rv)) return rv;

    // install cache listener if we still have a cache entry open
    if (mCacheEntry && (mCacheAccess & nsICache::ACCESS_WRITE)) {
        rv = InstallCacheListener();
        if (NS_FAILED(rv)) return rv;
    }

    return NS_OK;
}

nsresult
nsHttpChannel::PromptTempRedirect()
{
    if (!gHttpHandler->PromptTempRedirect()) {
        return NS_OK;
    }
    nsresult rv;
    nsCOMPtr<nsIStringBundleService> bundleService =
            do_GetService(NS_STRINGBUNDLE_CONTRACTID, &rv);
    if (NS_FAILED(rv)) return rv;

    nsCOMPtr<nsIStringBundle> stringBundle;
    rv = bundleService->CreateBundle(NECKO_MSGS_URL, getter_AddRefs(stringBundle));
    if (NS_FAILED(rv)) return rv;

    nsXPIDLString messageString;
    rv = stringBundle->GetStringFromName(NS_LITERAL_STRING("RepostFormData").get(), getter_Copies(messageString));
    // GetStringFromName can return NS_OK and NULL messageString.
    if (NS_SUCCEEDED(rv) && messageString) {
        PRBool repost = PR_FALSE;

        nsCOMPtr<nsIPrompt> prompt;
        GetCallback(prompt);
        if (!prompt)
            return NS_ERROR_NO_INTERFACE;

        prompt->Confirm(nsnull, messageString, &repost);
        if (!repost)
            return NS_ERROR_FAILURE;
    }

    return rv;
}

nsresult
nsHttpChannel::ProxyFailover()
{
    LOG(("nsHttpChannel::ProxyFailover [this=%p]\n", this));

    nsresult rv;

    nsCOMPtr<nsIProtocolProxyService> pps =
            do_GetService(NS_PROTOCOLPROXYSERVICE_CONTRACTID, &rv);
    if (NS_FAILED(rv))
        return rv;

    nsCOMPtr<nsIProxyInfo> pi;
    rv = pps->GetFailoverForProxy(mConnectionInfo->ProxyInfo(), mURI, mStatus,
                                  getter_AddRefs(pi));
    if (NS_FAILED(rv))
        return rv;

    // XXXbz so where does this codepath remove us from the loadgroup,
    // exactly?
    return DoReplaceWithProxy(pi);
}

void
nsHttpChannel::HandleAsyncReplaceWithProxy()
{
    NS_PRECONDITION(!mPendingAsyncCallOnResume, "How did that happen?");

    if (mSuspendCount) {
        LOG(("Waiting until resume to do async proxy replacement [this=%p]\n",
             this));
        mPendingAsyncCallOnResume =
            &nsHttpChannel::HandleAsyncReplaceWithProxy;
        return;
    }

    nsresult status = mStatus;
    
    nsCOMPtr<nsIProxyInfo> pi;
    pi.swap(mTargetProxyInfo);
    if (!mCanceled) {
        status = DoReplaceWithProxy(pi);
        if (mLoadGroup && NS_SUCCEEDED(status)) {
            mLoadGroup->RemoveRequest(this, nsnull, mStatus);
        }
    }

    if (NS_FAILED(status)) {
        AsyncAbort(status);
    }
}

nsresult
nsHttpChannel::DoReplaceWithProxy(nsIProxyInfo* pi)
{
    LOG(("nsHttpChannel::DoReplaceWithProxy [this=%p pi=%p]", this, pi));
    nsresult rv;

    nsCOMPtr<nsIChannel> newChannel;
    rv = gHttpHandler->NewProxiedChannel(mURI, pi, getter_AddRefs(newChannel));
    if (NS_FAILED(rv))
        return rv;

    rv = SetupReplacementChannel(mURI, newChannel, PR_TRUE);
    if (NS_FAILED(rv))
        return rv;

    // Inform consumers about this fake redirect
    PRUint32 flags = nsIChannelEventSink::REDIRECT_INTERNAL;
    rv = gHttpHandler->OnChannelRedirect(this, newChannel, flags);
    if (NS_FAILED(rv))
        return rv;

    // Make sure to do this _after_ calling OnChannelRedirect
    newChannel->SetOriginalURI(mOriginalURI);

    // open new channel
    rv = newChannel->AsyncOpen(mListener, mListenerContext);
    if (NS_FAILED(rv))
        return rv;

    mStatus = NS_BINDING_REDIRECTED;

    // disconnect from the old listeners...
    mListener = nsnull;
    mListenerContext = nsnull;

    // ...and the old callbacks
    mCallbacks = nsnull;
    mProgressSink = nsnull;

    return rv;
}

nsresult
nsHttpChannel::ResolveProxy()
{
    LOG(("nsHttpChannel::ResolveProxy [this=%p]\n", this));

    nsresult rv;

    nsCOMPtr<nsIProtocolProxyService> pps =
            do_GetService(NS_PROTOCOLPROXYSERVICE_CONTRACTID, &rv);
    if (NS_FAILED(rv))
        return rv;

    return pps->AsyncResolve(mURI, 0, this, getter_AddRefs(mProxyRequest));
}

PRBool
nsHttpChannel::ResponseWouldVary()
{
    PRBool result = PR_FALSE;
    nsCAutoString buf, metaKey;
    mCachedResponseHead->GetHeader(nsHttp::Vary, buf);
    if (!buf.IsEmpty()) {
        NS_NAMED_LITERAL_CSTRING(prefix, "request-");

        // enumerate the elements of the Vary header...
        char *val = buf.BeginWriting(); // going to munge buf
        char *token = nsCRT::strtok(val, NS_HTTP_HEADER_SEPS, &val);
        while (token) {
            //
            // if "*", then assume response would vary.  technically speaking,
            // "Vary: header, *" is not permitted, but we allow it anyways.
            //
            // if the response depends on the value of the "Cookie" header, then
            // bail since we do not store cookies in the cache.  this is done
            // for the following reasons:
            //
            //   1- cookies can be very large in size
            //
            //   2- cookies may contain sensitive information.  (for parity with
            //      out policy of not storing Set-cookie headers in the cache
            //      meta data, we likewise do not want to store cookie headers
            //      here.)
            //
            // this implementation is obviously not fully standards compliant, but
            // it is perhaps most prudent given the above issues.
            //
            if ((*token == '*') || (PL_strcasecmp(token, "cookie") == 0)) {
                result = PR_TRUE;
                break;
            }
            else {
                // build cache meta data key...
                metaKey = prefix + nsDependentCString(token);

                // check the last value of the given request header to see if it has
                // since changed.  if so, then indeed the cached response is invalid.
                nsXPIDLCString lastVal;
                mCacheEntry->GetMetaDataElement(metaKey.get(), getter_Copies(lastVal));
                if (lastVal) {
                    nsHttpAtom atom = nsHttp::ResolveAtom(token);
                    const char *newVal = mRequestHead.PeekHeader(atom);
                    if (newVal && (strcmp(newVal, lastVal) != 0)) {
                        result = PR_TRUE; // yes, response would vary
                        break;
                    }
                }
                
                // next token...
                token = nsCRT::strtok(val, NS_HTTP_HEADER_SEPS, &val);
            }
        }
    }
    return result;
}

//-----------------------------------------------------------------------------
// nsHttpChannel <byte-range>
//-----------------------------------------------------------------------------

nsresult
nsHttpChannel::SetupByteRangeRequest(PRUint32 partialLen)
{
    // cached content has been found to be partial, add necessary request
    // headers to complete cache entry.

    // use strongest validator available...
    const char *val = mCachedResponseHead->PeekHeader(nsHttp::ETag);
    if (!val)
        val = mCachedResponseHead->PeekHeader(nsHttp::Last_Modified);
    if (!val) {
        // if we hit this code it means mCachedResponseHead->IsResumable() is
        // either broken or not being called.
        NS_NOTREACHED("no cache validator");
        return NS_ERROR_FAILURE;
    }

    char buf[32];
    PR_snprintf(buf, sizeof(buf), "bytes=%u-", partialLen);

    mRequestHead.SetHeader(nsHttp::Range, nsDependentCString(buf));
    mRequestHead.SetHeader(nsHttp::If_Range, nsDependentCString(val));

    return NS_OK;
}

nsresult
nsHttpChannel::ProcessPartialContent()
{
    // ok, we've just received a 206
    //
    // we need to stream whatever data is in the cache out first, and then
    // pick up whatever data is on the wire, writing it into the cache.

    LOG(("nsHttpChannel::ProcessPartialContent [this=%p]\n", this)); 

    NS_ENSURE_TRUE(mCachedResponseHead, NS_ERROR_NOT_INITIALIZED);
    NS_ENSURE_TRUE(mCacheEntry, NS_ERROR_NOT_INITIALIZED);

    // Make sure to clear bogus content-encodings before looking at the header
    ClearBogusContentEncodingIfNeeded();
    
    // Check if the content-encoding we now got is different from the one we
    // got before
    if (PL_strcasecmp(mResponseHead->PeekHeader(nsHttp::Content_Encoding),
                      mCachedResponseHead->PeekHeader(nsHttp::Content_Encoding))
                      != 0) {
        Cancel(NS_ERROR_INVALID_CONTENT_ENCODING);
        return CallOnStartRequest();
    }


    // suspend the current transaction
    nsresult rv = mTransactionPump->Suspend();
    if (NS_FAILED(rv)) return rv;

    // merge any new headers with the cached response headers
    rv = mCachedResponseHead->UpdateHeaders(mResponseHead->Headers());
    if (NS_FAILED(rv)) return rv;

    // update the cached response head
    nsCAutoString head;
    mCachedResponseHead->Flatten(head, PR_TRUE);
    rv = mCacheEntry->SetMetaDataElement("response-head", head.get());
    if (NS_FAILED(rv)) return rv;

    // make the cached response be the current response
    delete mResponseHead;
    mResponseHead = mCachedResponseHead;
    mCachedResponseHead = 0;

    rv = UpdateExpirationTime();
    if (NS_FAILED(rv)) return rv;

    // notify observers interested in looking at a response that has been
    // merged with any cached headers (http-on-examine-merged-response).
    gHttpHandler->OnExamineMergedResponse(this);

    // the cached content is valid, although incomplete.
    mCachedContentIsValid = PR_TRUE;
    return ReadFromCache();
}

nsresult
nsHttpChannel::OnDoneReadingPartialCacheEntry(PRBool *streamDone)
{
    nsresult rv;

    LOG(("nsHttpChannel::OnDoneReadingPartialCacheEntry [this=%p]", this));

    // by default, assume we would have streamed all data or failed...
    *streamDone = PR_TRUE;

    // setup cache listener to append to cache entry
    PRUint32 size;
    rv = mCacheEntry->GetDataSize(&size);
    if (NS_FAILED(rv)) return rv;

    rv = InstallCacheListener(size);
    if (NS_FAILED(rv)) return rv;

    // need to track the logical offset of the data being sent to our listener
    mLogicalOffset = size;

    // we're now completing the cached content, so we can clear this flag.
    // this puts us in the state of a regular download.
    mCachedContentIsPartial = PR_FALSE;

    // resume the transaction if it exists, otherwise the pipe contained the
    // remaining part of the document and we've now streamed all of the data.
    if (mTransactionPump) {
        rv = mTransactionPump->Resume();
        if (NS_SUCCEEDED(rv))
            *streamDone = PR_FALSE;
    }
    else
        NS_NOTREACHED("no transaction");
    return rv;
}

//-----------------------------------------------------------------------------
// nsHttpChannel <cache>
//-----------------------------------------------------------------------------

nsresult
nsHttpChannel::ProcessNotModified()
{
    nsresult rv;

    LOG(("nsHttpChannel::ProcessNotModified [this=%p]\n", this)); 

    NS_ENSURE_TRUE(mCachedResponseHead, NS_ERROR_NOT_INITIALIZED);
    NS_ENSURE_TRUE(mCacheEntry, NS_ERROR_NOT_INITIALIZED);

    // merge any new headers with the cached response headers
    rv = mCachedResponseHead->UpdateHeaders(mResponseHead->Headers());
    if (NS_FAILED(rv)) return rv;

    // update the cached response head
    nsCAutoString head;
    mCachedResponseHead->Flatten(head, PR_TRUE);
    rv = mCacheEntry->SetMetaDataElement("response-head", head.get());
    if (NS_FAILED(rv)) return rv;

    // make the cached response be the current response
    delete mResponseHead;
    mResponseHead = mCachedResponseHead;
    mCachedResponseHead = 0;

    rv = UpdateExpirationTime();
    if (NS_FAILED(rv)) return rv;

    // notify observers interested in looking at a reponse that has been
    // merged with any cached headers
    gHttpHandler->OnExamineMergedResponse(this);

    mCachedContentIsValid = PR_TRUE;
    rv = ReadFromCache();
    if (NS_FAILED(rv)) return rv;

    mTransactionReplaced = PR_TRUE;
    return NS_OK;
}

nsresult
nsHttpChannel::ProcessFallback(PRBool *fallingBack)
{
    LOG(("nsHttpChannel::ProcessFallback [this=%p]\n", this));
    nsresult rv;

    *fallingBack = PR_FALSE;

    // At this point a load has failed (either due to network problems
    // or an error returned on the server).  Perform an application
    // cache fallback if we have a URI to fall back to.
    if (!mApplicationCache || mFallbackKey.IsEmpty() || mFallbackChannel) {
        LOG(("  choosing not to fallback [%p,%s,%d]",
             mApplicationCache.get(), mFallbackKey.get(), mFallbackChannel));
        return NS_OK;
    }

    // Make sure the fallback entry hasn't been marked as a foreign
    // entry.
    PRUint32 fallbackEntryType;
    rv = mApplicationCache->GetTypes(mFallbackKey, &fallbackEntryType);
    NS_ENSURE_SUCCESS(rv, rv);

    if (fallbackEntryType & nsIApplicationCache::ITEM_FOREIGN) {
        // This cache points to a fallback that refers to a different
        // manifest.  Refuse to fall back.
        return NS_OK;
    }

    NS_ASSERTION(fallbackEntryType & nsIApplicationCache::ITEM_FALLBACK,
                 "Fallback entry not marked correctly!");

    // Kill any opportunistic cache entry, and disable opportunistic
    // caching for the fallback.
    if (mOfflineCacheEntry) {
        mOfflineCacheEntry->Doom();
        mOfflineCacheEntry = 0;
        mOfflineCacheAccess = 0;
    }

    mCacheForOfflineUse = PR_FALSE;
    mCachingOpportunistically = PR_FALSE;
    mOfflineCacheClientID.Truncate();
    mOfflineCacheEntry = 0;
    mOfflineCacheAccess = 0;

    // Close the current cache entry.
    if (mCacheEntry)
        CloseCacheEntry(PR_TRUE);

    // Create a new channel to load the fallback entry.
    nsRefPtr<nsIChannel> newChannel;
    rv = gHttpHandler->NewChannel(mURI, getter_AddRefs(newChannel));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = SetupReplacementChannel(mURI, newChannel, PR_TRUE);
    NS_ENSURE_SUCCESS(rv, rv);

    // Make sure the new channel loads from the fallback key.
    nsCOMPtr<nsIHttpChannelInternal> httpInternal =
        do_QueryInterface(newChannel, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = httpInternal->SetupFallbackChannel(mFallbackKey.get());
    NS_ENSURE_SUCCESS(rv, rv);

    // ... and fallbacks should only load from the cache.
    PRUint32 newLoadFlags = mLoadFlags | LOAD_REPLACE | LOAD_ONLY_FROM_CACHE;
    rv = newChannel->SetLoadFlags(newLoadFlags);

    // Inform consumers about this fake redirect
    PRUint32 redirectFlags = nsIChannelEventSink::REDIRECT_INTERNAL;
    rv = gHttpHandler->OnChannelRedirect(this, newChannel, redirectFlags);
    if (NS_FAILED(rv))
        return rv;

    // Make sure to do this _after_ calling OnChannelRedirect
    newChannel->SetOriginalURI(mOriginalURI);
    
    rv = newChannel->AsyncOpen(mListener, mListenerContext);
    NS_ENSURE_SUCCESS(rv, rv);

    // close down this channel
    Cancel(NS_BINDING_REDIRECTED);

    // disconnect from our listener
    mListener = 0;
    mListenerContext = 0;
    // and from our callbacks
    mCallbacks = nsnull;
    mProgressSink = nsnull;

    *fallingBack = PR_TRUE;

    return NS_OK;
}

// Determines if a request is a byte range request for a subrange,
// i.e. is a byte range request, but not a 0- byte range request.
static PRBool
IsSubRangeRequest(nsHttpRequestHead &aRequestHead)
{
    if (!aRequestHead.PeekHeader(nsHttp::Range))
        return PR_FALSE;
    nsCAutoString byteRange;
    aRequestHead.GetHeader(nsHttp::Range, byteRange);
    return !byteRange.EqualsLiteral("bytes=0-");
}

nsresult
nsHttpChannel::OpenCacheEntry(PRBool offline, PRBool *delayed)
{
    nsresult rv;

    *delayed = PR_FALSE;
    mLoadedFromApplicationCache = PR_FALSE;

    LOG(("nsHttpChannel::OpenCacheEntry [this=%p]", this));

    // make sure we're not abusing this function
    NS_PRECONDITION(!mCacheEntry, "cache entry already open");

    nsCAutoString cacheKey;

    if (mRequestHead.Method() == nsHttp::Post) {
        // If the post id is already set then this is an attempt to replay
        // a post transaction via the cache.  Otherwise, we need a unique
        // post id for this transaction.
        if (mPostID == 0)
            mPostID = gHttpHandler->GenerateUniqueID();
    }
    else if ((mRequestHead.Method() != nsHttp::Get) &&
             (mRequestHead.Method() != nsHttp::Head)) {
        // don't use the cache for other types of requests
        return NS_OK;
    }

    if (mResuming) {
        // We don't support caching for requests initiated
        // via nsIResumableChannel.
        return NS_OK;
    }

    // Don't cache byte range requests which are subranges, only cache 0-
    // byte range requests.
    if (IsSubRangeRequest(mRequestHead))
        return NS_OK;

    if (RequestIsConditional()) {
        // don't use the cache if our consumer is making a conditional request
        // (see bug 331825).
        return NS_OK;
    }

    GenerateCacheKey(mPostID, cacheKey);

    // Get a cache session with appropriate storage policy
    nsCacheStoragePolicy storagePolicy = DetermineStoragePolicy();

    // Set the desired cache access mode accordingly...
    nsCacheAccessMode accessRequested;
    if (offline || (mLoadFlags & INHIBIT_CACHING)) {
        // If we have been asked to bypass the cache and not write to the
        // cache, then don't use the cache at all.  Unless we're actually
        // offline, which takes precedence over BYPASS_LOCAL_CACHE.
        if (BYPASS_LOCAL_CACHE(mLoadFlags) && !offline)
            return NS_ERROR_NOT_AVAILABLE;
        accessRequested = nsICache::ACCESS_READ;
    }
    else if (BYPASS_LOCAL_CACHE(mLoadFlags))
        accessRequested = nsICache::ACCESS_WRITE; // replace cache entry
    else
        accessRequested = nsICache::ACCESS_READ_WRITE; // normal browsing

    if (!mApplicationCache && mInheritApplicationCache) {
        // Pick up an application cache from the notification
        // callbacks if available
        nsCOMPtr<nsIApplicationCacheContainer> appCacheContainer;
        GetCallback(appCacheContainer);

        if (appCacheContainer) {
            appCacheContainer->GetApplicationCache(getter_AddRefs(mApplicationCache));
        }
    }

    if (!mApplicationCache &&
        (mChooseApplicationCache || (mLoadFlags & LOAD_CHECK_OFFLINE_CACHE))) {
        // We're supposed to load from an application cache, but
        // one was not supplied by the load group.  Ask the
        // application cache service to choose one for us.
        nsCOMPtr<nsIApplicationCacheService> appCacheService =
            do_GetService(NS_APPLICATIONCACHESERVICE_CONTRACTID);
        if (appCacheService) {
            nsresult rv = appCacheService->ChooseApplicationCache
                (cacheKey, getter_AddRefs(mApplicationCache));
            NS_ENSURE_SUCCESS(rv, rv);
        }
    }

    nsCOMPtr<nsICacheSession> session;

    // Will be set to true if we've found the right session, but need
    // to open the cache entry asynchronously.
    PRBool waitingForValidation = PR_FALSE;

    // If we have an application cache, we check it first.
    if (mApplicationCache) {
        nsCAutoString appCacheClientID;
        mApplicationCache->GetClientID(appCacheClientID);

        nsCOMPtr<nsICacheService> serv =
            do_GetService(NS_CACHESERVICE_CONTRACTID, &rv);
        NS_ENSURE_SUCCESS(rv, rv);

        rv = serv->CreateSession(appCacheClientID.get(),
                                 nsICache::STORE_OFFLINE,
                                 nsICache::STREAM_BASED,
                                 getter_AddRefs(session));
        NS_ENSURE_SUCCESS(rv, rv);

        // we'll try to synchronously open the cache entry... however,
        // it may be in use and not yet validated, in which case we'll
        // try asynchronously opening the cache entry.
        //
        // We open with ACCESS_READ only, because we don't want to
        // overwrite the offline cache entry non-atomically.
        // ACCESS_READ will prevent us from writing to the offline
        // cache as a normal cache entry.
        rv = session->OpenCacheEntry(cacheKey,
                                     nsICache::ACCESS_READ, PR_FALSE,
                                     getter_AddRefs(mCacheEntry));
        if (rv == NS_ERROR_CACHE_WAIT_FOR_VALIDATION) {
            accessRequested = nsICache::ACCESS_READ;
            waitingForValidation = PR_TRUE;
            rv = NS_OK;
        }

        if (NS_FAILED(rv) && !mCacheForOfflineUse && !mFallbackChannel) {
            // Check for namespace match.
            nsCOMPtr<nsIApplicationCacheNamespace> namespaceEntry;
            rv = mApplicationCache->GetMatchingNamespace
                (cacheKey, getter_AddRefs(namespaceEntry));
            NS_ENSURE_SUCCESS(rv, rv);

            PRUint32 namespaceType = 0;
            if (!namespaceEntry ||
                NS_FAILED(namespaceEntry->GetItemType(&namespaceType)) ||
                (namespaceType &
                 (nsIApplicationCacheNamespace::NAMESPACE_FALLBACK |
                  nsIApplicationCacheNamespace::NAMESPACE_OPPORTUNISTIC |
                  nsIApplicationCacheNamespace::NAMESPACE_BYPASS)) == 0) {
                // When loading from an application cache, only items
                // on the whitelist or matching a
                // fallback/opportunistic namespace should hit the
                // network...
                mLoadFlags |= LOAD_ONLY_FROM_CACHE;

                // ... and if there were an application cache entry,
                // we would have found it earlier.
                return NS_ERROR_CACHE_KEY_NOT_FOUND;
            }

            if (namespaceType &
                nsIApplicationCacheNamespace::NAMESPACE_FALLBACK) {
                rv = namespaceEntry->GetData(mFallbackKey);
                NS_ENSURE_SUCCESS(rv, rv);
            }

            if ((namespaceType &
                 nsIApplicationCacheNamespace::NAMESPACE_OPPORTUNISTIC) &&
                mLoadFlags & LOAD_DOCUMENT_URI) {
                // Document loads for items in an opportunistic namespace
                // should be placed in the offline cache.
                nsCString clientID;
                mApplicationCache->GetClientID(clientID);

                mCacheForOfflineUse = !clientID.IsEmpty();
                SetOfflineCacheClientID(clientID);
                mCachingOpportunistically = PR_TRUE;
            }
        }
        else if (NS_SUCCEEDED(rv)) {
            // We successfully opened an offline cache session and the entry,
            // now indiciate we load from the offline cache.
            mLoadedFromApplicationCache = PR_TRUE;
        }
    }

    if (!mCacheEntry && !waitingForValidation) {
        rv = gHttpHandler->GetCacheSession(storagePolicy,
                                           getter_AddRefs(session));
        if (NS_FAILED(rv)) return rv;

        rv = session->OpenCacheEntry(cacheKey, accessRequested, PR_FALSE,
                                     getter_AddRefs(mCacheEntry));
        if (rv == NS_ERROR_CACHE_WAIT_FOR_VALIDATION) {
            waitingForValidation = PR_TRUE;
            rv = NS_OK;
        }
        if (NS_FAILED(rv)) return rv;
    }

    if (waitingForValidation) {
        // access to the cache entry has been denied (because the
        // cache entry is probably in use by another channel).
        if (mLoadFlags & LOAD_BYPASS_LOCAL_CACHE_IF_BUSY) {
            LOG(("bypassing local cache since it is busy\n"));
            return NS_ERROR_NOT_AVAILABLE;
        }
        rv = session->AsyncOpenCacheEntry(cacheKey, accessRequested, this);
        if (NS_FAILED(rv)) return rv;
        // we'll have to wait for the cache entry
        *delayed = PR_TRUE;
    }
    else if (NS_SUCCEEDED(rv)) {
        mCacheEntry->GetAccessGranted(&mCacheAccess);
        LOG(("nsHttpChannel::OpenCacheEntry [this=%p grantedAccess=%d]", this, mCacheAccess));
    }
    return rv;
}


nsresult
nsHttpChannel::OpenOfflineCacheEntryForWriting()
{
    nsresult rv;

    LOG(("nsHttpChannel::OpenOfflineCacheEntryForWriting [this=%p]", this));

    // make sure we're not abusing this function
    NS_PRECONDITION(!mOfflineCacheEntry, "cache entry already open");

    PRBool offline = gIOService->IsOffline();
    if (offline) {
        // only put things in the offline cache while online
        return NS_OK;
    }

    if (mRequestHead.Method() != nsHttp::Get) {
        // only cache complete documents offline
        return NS_OK;
    }

    // Don't cache byte range requests which are subranges, only cache 0-
    // byte range requests.
    if (IsSubRangeRequest(mRequestHead))
        return NS_OK;

    if (RequestIsConditional()) {
        // don't use the cache if our consumer is making a conditional request
        // (see bug 331825).
        return NS_OK;
    }

    nsCAutoString cacheKey;
    GenerateCacheKey(mPostID, cacheKey);

    NS_ENSURE_TRUE(!mOfflineCacheClientID.IsEmpty(),
                   NS_ERROR_NOT_AVAILABLE);

    nsCOMPtr<nsICacheSession> session;
    nsCOMPtr<nsICacheService> serv =
        do_GetService(NS_CACHESERVICE_CONTRACTID, &rv);
    if (NS_FAILED(rv)) return rv;

    rv = serv->CreateSession(mOfflineCacheClientID.get(),
                             nsICache::STORE_OFFLINE,
                             nsICache::STREAM_BASED,
                             getter_AddRefs(session));
    if (NS_FAILED(rv)) return rv;

    rv = session->OpenCacheEntry(cacheKey, nsICache::ACCESS_READ_WRITE,
                                 PR_FALSE, getter_AddRefs(mOfflineCacheEntry));

    if (rv == NS_ERROR_CACHE_WAIT_FOR_VALIDATION) {
        // access to the cache entry has been denied (because the cache entry
        // is probably in use by another channel).  Either the cache is being
        // read from (we're offline) or it's being updated elsewhere.
        return NS_OK;
    }

    if (NS_SUCCEEDED(rv)) {
        mOfflineCacheEntry->GetAccessGranted(&mOfflineCacheAccess);
        LOG(("got offline cache entry [access=%x]\n", mOfflineCacheAccess));
    }

    return rv;
}

nsresult
nsHttpChannel::GenerateCacheKey(PRUint32 postID, nsACString &cacheKey)
{
    cacheKey.Truncate();

    if (mLoadFlags & LOAD_ANONYMOUS) {
      cacheKey.AssignLiteral("anon&");
    }

    if (postID) {
        char buf[32];
        PR_snprintf(buf, sizeof(buf), "id=%x&", postID);
        cacheKey.Append(buf);
    }

    if (!cacheKey.IsEmpty()) {
      cacheKey.AppendLiteral("uri=");
    }

    // Strip any trailing #ref from the URL before using it as the key
    const char *spec = mFallbackChannel ? mFallbackKey.get() : mSpec.get();
    const char *p = strchr(spec, '#');
    if (p)
        cacheKey.Append(spec, p - spec);
    else
        cacheKey.Append(spec);
    return NS_OK;
}

// UpdateExpirationTime is called when a new response comes in from the server.
// It updates the stored response-time and sets the expiration time on the
// cache entry.  
//
// From section 13.2.4 of RFC2616, we compute expiration time as follows:
//
//    timeRemaining = freshnessLifetime - currentAge
//    expirationTime = now + timeRemaining
// 
nsresult
nsHttpChannel::UpdateExpirationTime()
{
    NS_ENSURE_TRUE(mResponseHead, NS_ERROR_FAILURE);

    nsresult rv;

    PRUint32 expirationTime = 0;
    if (!mResponseHead->MustValidate()) {
        PRUint32 freshnessLifetime = 0;

        rv = mResponseHead->ComputeFreshnessLifetime(&freshnessLifetime);
        if (NS_FAILED(rv)) return rv;

        if (freshnessLifetime > 0) {
            PRUint32 now = NowInSeconds(), currentAge = 0;

            rv = mResponseHead->ComputeCurrentAge(now, mRequestTime, &currentAge); 
            if (NS_FAILED(rv)) return rv;

            LOG(("freshnessLifetime = %u, currentAge = %u\n",
                freshnessLifetime, currentAge));

            if (freshnessLifetime > currentAge) {
                PRUint32 timeRemaining = freshnessLifetime - currentAge;
                // be careful... now + timeRemaining may overflow
                if (now + timeRemaining < now)
                    expirationTime = PRUint32(-1);
                else
                    expirationTime = now + timeRemaining;
            }
            else
                expirationTime = now;
        }
    }

    rv = mCacheEntry->SetExpirationTime(expirationTime);
    NS_ENSURE_SUCCESS(rv, rv);

    if (mOfflineCacheEntry) {
        rv = mOfflineCacheEntry->SetExpirationTime(expirationTime);
        NS_ENSURE_SUCCESS(rv, rv);
    }

    return NS_OK;
}

// CheckCache is called from Connect after a cache entry has been opened for
// this URL but before going out to net.  It's purpose is to set or clear the 
// mCachedContentIsValid flag, and to configure an If-Modified-Since request
// if validation is required.
nsresult
nsHttpChannel::CheckCache()
{
    nsresult rv = NS_OK;

    LOG(("nsHTTPChannel::CheckCache enter [this=%p entry=%p access=%d]",
        this, mCacheEntry.get(), mCacheAccess));
    
    // Be pessimistic: assume the cache entry has no useful data.
    mCachedContentIsValid = PR_FALSE;

    // Don't proceed unless we have opened a cache entry for reading.
    if (!mCacheEntry || !(mCacheAccess & nsICache::ACCESS_READ))
        return NS_OK;

    nsXPIDLCString buf;

    // Get the method that was used to generate the cached response
    rv = mCacheEntry->GetMetaDataElement("request-method", getter_Copies(buf));
    NS_ENSURE_SUCCESS(rv, rv);

    nsHttpAtom method = nsHttp::ResolveAtom(buf);
    if (method == nsHttp::Head) {
        // The cached response does not contain an entity.  We can only reuse
        // the response if the current request is also HEAD.
        if (mRequestHead.Method() != nsHttp::Head)
            return NS_OK;
    }
    buf.Adopt(0);

    // We'll need this value in later computations...
    PRUint32 lastModifiedTime;
    rv = mCacheEntry->GetLastModified(&lastModifiedTime);
    NS_ENSURE_SUCCESS(rv, rv);

    // Determine if this is the first time that this cache entry
    // has been accessed during this session.
    PRBool fromPreviousSession =
            (gHttpHandler->SessionStartTime() > lastModifiedTime);

    // Get the cached HTTP response headers
    rv = mCacheEntry->GetMetaDataElement("response-head", getter_Copies(buf));
    NS_ENSURE_SUCCESS(rv, rv);

    // Parse the cached HTTP response headers
    NS_ASSERTION(!mCachedResponseHead, "memory leak detected");
    mCachedResponseHead = new nsHttpResponseHead();
    if (!mCachedResponseHead)
        return NS_ERROR_OUT_OF_MEMORY;
    rv = mCachedResponseHead->Parse((char *) buf.get());
    NS_ENSURE_SUCCESS(rv, rv);
    buf.Adopt(0);

    // Don't bother to validate items that are read-only,
    // unless they are read-only because of INHIBIT_CACHING or because
    // we're updating the offline cache.
    // Don't bother to validate if this is a fallback entry.
    if (!mCacheForOfflineUse &&
        (mLoadedFromApplicationCache ||
         (mCacheAccess == nsICache::ACCESS_READ &&
          !(mLoadFlags & INHIBIT_CACHING)) ||
         mFallbackChannel)) {
        mCachedContentIsValid = PR_TRUE;
        return NS_OK;
    }

    PRUint16 isCachedRedirect = mCachedResponseHead->Status()/100 == 3;

    if (method != nsHttp::Head && !isCachedRedirect) {
        // If the cached content-length is set and it does not match the data
        // size of the cached content, then the cached response is partial...
        // either we need to issue a byte range request or we need to refetch
        // the entire document.
        nsInt64 contentLength = mCachedResponseHead->ContentLength();
        if (contentLength != nsInt64(-1)) {
            PRUint32 size;
            rv = mCacheEntry->GetDataSize(&size);
            NS_ENSURE_SUCCESS(rv, rv);

            if (nsInt64(size) != contentLength) {
                LOG(("Cached data size does not match the Content-Length header "
                     "[content-length=%lld size=%u]\n", PRInt64(contentLength), size));
                if ((nsInt64(size) < contentLength) && mCachedResponseHead->IsResumable()) {
                    // looks like a partial entry.
                    rv = SetupByteRangeRequest(size);
                    NS_ENSURE_SUCCESS(rv, rv);
                    mCachedContentIsPartial = PR_TRUE;
                }
                return NS_OK;
            }
        }
    }

    PRBool doValidation = PR_FALSE;
    PRBool canAddImsHeader = PR_TRUE;

    // Be optimistic: assume that we won't need to do validation
    mRequestHead.ClearHeader(nsHttp::If_Modified_Since);
    mRequestHead.ClearHeader(nsHttp::If_None_Match);

    // If the LOAD_FROM_CACHE flag is set, any cached data can simply be used.
    if (mLoadFlags & LOAD_FROM_CACHE) {
        LOG(("NOT validating based on LOAD_FROM_CACHE load flag\n"));
        doValidation = PR_FALSE;
    }
    // If the VALIDATE_ALWAYS flag is set, any cached data won't be used until
    // it's revalidated with the server.
    else if (mLoadFlags & VALIDATE_ALWAYS) {
        LOG(("Validating based on VALIDATE_ALWAYS load flag\n"));
        doValidation = PR_TRUE;
    }
    // Even if the VALIDATE_NEVER flag is set, there are still some cases in
    // which we must validate the cached response with the server.
    else if (mLoadFlags & VALIDATE_NEVER) {
        LOG(("VALIDATE_NEVER set\n"));
        // if no-store or if no-cache and ssl, validate cached response (see
        // bug 112564 for an explanation of this logic)
        if (mCachedResponseHead->NoStore() ||
           (mCachedResponseHead->NoCache() && mConnectionInfo->UsingSSL())) {
            LOG(("Validating based on (no-store || (no-cache && ssl)) logic\n"));
            doValidation = PR_TRUE;
        }
        else {
            LOG(("NOT validating based on VALIDATE_NEVER load flag\n"));
            doValidation = PR_FALSE;
        }
    }
    // check if validation is strictly required...
    else if (mCachedResponseHead->MustValidate()) {
        LOG(("Validating based on MustValidate() returning TRUE\n"));
        doValidation = PR_TRUE;
    }

    else if (ResponseWouldVary()) {
        LOG(("Validating based on Vary headers returning TRUE\n"));
        canAddImsHeader = PR_FALSE;
        doValidation = PR_TRUE;
    }
    
    else if (MustValidateBasedOnQueryUrl()) {
        LOG(("Validating based on RFC 2616 section 13.9 "
             "(query-url w/o explicit expiration-time)\n"));
        doValidation = PR_TRUE;
    }
    // Check if the cache entry has expired...
    else {
        PRUint32 time = 0; // a temporary variable for storing time values...

        rv = mCacheEntry->GetExpirationTime(&time);
        NS_ENSURE_SUCCESS(rv, rv);

        if (NowInSeconds() <= time)
            doValidation = PR_FALSE;
        else if (mCachedResponseHead->MustValidateIfExpired())
            doValidation = PR_TRUE;
        else if (mLoadFlags & VALIDATE_ONCE_PER_SESSION) {
            // If the cached response does not include expiration infor-
            // mation, then we must validate the response, despite whether
            // or not this is the first access this session.  This behavior
            // is consistent with existing browsers and is generally expected
            // by web authors.
            rv = mCachedResponseHead->ComputeFreshnessLifetime(&time);
            NS_ENSURE_SUCCESS(rv, rv);

            if (time == 0)
                doValidation = PR_TRUE;
            else
                doValidation = fromPreviousSession;
        }
        else
            doValidation = PR_TRUE;

        LOG(("%salidating based on expiration time\n", doValidation ? "V" : "Not v"));
    }

    if (!doValidation) {
        //
        // Check the authorization headers used to generate the cache entry.
        // We must validate the cache entry if:
        //
        // 1) the cache entry was generated prior to this session w/
        //    credentials (see bug 103402).
        // 2) the cache entry was generated w/o credentials, but would now
        //    require credentials (see bug 96705).
        //
        // NOTE: this does not apply to proxy authentication.
        //
        mCacheEntry->GetMetaDataElement("auth", getter_Copies(buf));
        doValidation =
            (fromPreviousSession && !buf.IsEmpty()) ||
            (buf.IsEmpty() && mRequestHead.PeekHeader(nsHttp::Authorization));
    }

    if (!doValidation) {
        // Sites redirect back to the original URI after setting a session/tracking
        // cookie. In such cases, force revalidation so that we hit the net and do not
        // cycle thru cached responses.
        if (isCachedRedirect && mRequestHead.PeekHeader(nsHttp::Cookie))
            doValidation = PR_TRUE;
    }

    mCachedContentIsValid = !doValidation;

    if (doValidation) {
        //
        // now, we are definitely going to issue a HTTP request to the server.
        // make it conditional if possible.
        //
        // do not attempt to validate no-store content, since servers will not
        // expect it to be cached.  (we only keep it in our cache for the
        // purposes of back/forward, etc.)
        //
        // the request method MUST be either GET or HEAD (see bug 175641).
        //
        if (!mCachedResponseHead->NoStore() &&
            (mRequestHead.Method() == nsHttp::Get ||
             mRequestHead.Method() == nsHttp::Head)) {
            const char *val;
            // Add If-Modified-Since header if a Last-Modified was given
            // and we are allowed to do this (see bugs 510359 and 269303)
            if (canAddImsHeader) {
                val = mCachedResponseHead->PeekHeader(nsHttp::Last_Modified);
                if (val)
                    mRequestHead.SetHeader(nsHttp::If_Modified_Since,
                                           nsDependentCString(val));
            }
            // Add If-None-Match header if an ETag was given in the response
            val = mCachedResponseHead->PeekHeader(nsHttp::ETag);
            if (val)
                mRequestHead.SetHeader(nsHttp::If_None_Match,
                                       nsDependentCString(val));
        }
    }

    LOG(("nsHTTPChannel::CheckCache exit [this=%p doValidation=%d]\n", this, doValidation));
    return NS_OK;
}

PRBool
nsHttpChannel::MustValidateBasedOnQueryUrl()
{
    // RFC 2616, section 13.9 states that GET-requests with a query-url
    // MUST NOT be treated as fresh unless the server explicitly provides
    // an expiration-time in the response. See bug #468594
    // Section 13.2.1 (6th paragraph) defines "explicit expiration time"
    if (mRequestHead.Method() == nsHttp::Get)
    {
        nsCAutoString query;
        nsCOMPtr<nsIURL> url = do_QueryInterface(mURI);
        nsresult rv = url->GetQuery(query);
        if (NS_SUCCEEDED(rv) && !query.IsEmpty()) {
            PRUint32 tmp; // we don't need the value, just whether it's set
            rv = mCachedResponseHead->GetExpiresValue(&tmp);
            if (NS_FAILED(rv)) {
                rv = mCachedResponseHead->GetMaxAgeValue(&tmp);
                if (NS_FAILED(rv)) {
                    return PR_TRUE;
                }
            }
        }
    }
    return PR_FALSE;
}


nsresult
nsHttpChannel::ShouldUpdateOfflineCacheEntry(PRBool *shouldCacheForOfflineUse)
{
    *shouldCacheForOfflineUse = PR_FALSE;

    if (!mOfflineCacheEntry) {
        return NS_OK;
    }

    // if we're updating the cache entry, update the offline cache entry too
    if (mCacheEntry && (mCacheAccess & nsICache::ACCESS_WRITE)) {
        *shouldCacheForOfflineUse = PR_TRUE;
        return NS_OK;
    }

    // if there's nothing in the offline cache, add it
    if (mOfflineCacheEntry && (mOfflineCacheAccess == nsICache::ACCESS_WRITE)) {
        *shouldCacheForOfflineUse = PR_TRUE;
        return NS_OK;
    }

    // if the document is newer than the offline entry, update it
    PRUint32 docLastModifiedTime;
    nsresult rv = mResponseHead->GetLastModifiedValue(&docLastModifiedTime);
    if (NS_FAILED(rv)) {
        *shouldCacheForOfflineUse = PR_TRUE;
        return NS_OK;
    }

    PRUint32 offlineLastModifiedTime;
    rv = mOfflineCacheEntry->GetLastModified(&offlineLastModifiedTime);
    NS_ENSURE_SUCCESS(rv, rv);

    if (docLastModifiedTime > offlineLastModifiedTime) {
        *shouldCacheForOfflineUse = PR_TRUE;
        return NS_OK;
    }

    return NS_OK;
}

// If the data in the cache hasn't expired, then there's no need to
// talk with the server, not even to do an if-modified-since.  This
// method creates a stream from the cache, synthesizing all the various
// channel-related events.
nsresult
nsHttpChannel::ReadFromCache()
{
    nsresult rv;

    NS_ENSURE_TRUE(mCacheEntry, NS_ERROR_FAILURE);
    NS_ENSURE_TRUE(mCachedContentIsValid, NS_ERROR_FAILURE);

    LOG(("nsHttpChannel::ReadFromCache [this=%p] "
         "Using cached copy of: %s\n", this, mSpec.get()));

    if (mCachedResponseHead) {
        NS_ASSERTION(!mResponseHead, "memory leak");
        mResponseHead = mCachedResponseHead;
        mCachedResponseHead = 0;
    }

    // if we don't already have security info, try to get it from the cache 
    // entry. there are two cases to consider here: 1) we are just reading
    // from the cache, or 2) this may be due to a 304 not modified response,
    // in which case we could have security info from a socket transport.
    if (!mSecurityInfo)
        mCacheEntry->GetSecurityInfo(getter_AddRefs(mSecurityInfo));

    if ((mCacheAccess & nsICache::ACCESS_WRITE) && !mCachedContentIsPartial) {
        // We have write access to the cache, but we don't need to go to the
        // server to validate at this time, so just mark the cache entry as
        // valid in order to allow others access to this cache entry.
        mCacheEntry->MarkValid();
    }

    // if this is a cached redirect, we must process the redirect asynchronously
    // since AsyncOpen may not have returned yet.  Make sure there is a Location
    // header, otherwise we'll have to treat this like a normal 200 response.
    if (mResponseHead && (mResponseHead->Status() / 100 == 3) 
                      && (mResponseHead->PeekHeader(nsHttp::Location)))
        return AsyncCall(&nsHttpChannel::HandleAsyncRedirect);

    // have we been configured to skip reading from the cache?
    if ((mLoadFlags & LOAD_ONLY_IF_MODIFIED) && !mCachedContentIsPartial) {
        // if offline caching has been requested and the offline cache needs
        // updating, complete the call even if the main cache entry is
        // up-to-date
        PRBool shouldUpdateOffline;
        if (!mCacheForOfflineUse ||
            NS_FAILED(ShouldUpdateOfflineCacheEntry(&shouldUpdateOffline)) ||
            !shouldUpdateOffline) {

            LOG(("skipping read from cache based on LOAD_ONLY_IF_MODIFIED "
                 "load flag\n"));
            return AsyncCall(&nsHttpChannel::HandleAsyncNotModified);
        }
    }

    // open input stream for reading...
    nsCOMPtr<nsIInputStream> stream;
    rv = mCacheEntry->OpenInputStream(0, getter_AddRefs(stream));
    if (NS_FAILED(rv)) return rv;

    rv = nsInputStreamPump::Create(getter_AddRefs(mCachePump),
                                   stream, nsInt64(-1), nsInt64(-1), 0, 0,
                                   PR_TRUE);
    if (NS_FAILED(rv)) return rv;

    return mCachePump->AsyncRead(this, mListenerContext);
}

void
nsHttpChannel::CloseCacheEntry(PRBool doomOnFailure)
{
    if (!mCacheEntry)
        return;

    LOG(("nsHttpChannel::CloseCacheEntry [this=%p]", this));

    // If we have begun to create or replace a cache entry, and that cache
    // entry is not complete and not resumable, then it needs to be doomed.
    // Otherwise, CheckCache will make the mistake of thinking that the
    // partial cache entry is complete.

    PRBool doom = PR_FALSE;
    if (mInitedCacheEntry) {
        NS_ASSERTION(mResponseHead, "oops");
        if (NS_FAILED(mStatus) && doomOnFailure &&
            (mCacheAccess & nsICache::ACCESS_WRITE) &&
            !mResponseHead->IsResumable())
            doom = PR_TRUE;
    }
    else if (mCacheAccess == nsICache::ACCESS_WRITE)
        doom = PR_TRUE;

    if (doom) {
        LOG(("  dooming cache entry!!"));
        mCacheEntry->Doom();
    }

    if (mCachedResponseHead) {
        delete mCachedResponseHead;
        mCachedResponseHead = 0;
    }

    mCachePump = 0;
    mCacheEntry = 0;
    mCacheAccess = 0;
    mInitedCacheEntry = PR_FALSE;
}


void
nsHttpChannel::CloseOfflineCacheEntry()
{
    if (!mOfflineCacheEntry)
        return;

    LOG(("nsHttpChannel::CloseOfflineCacheEntry [this=%p]", this));

    if (NS_FAILED(mStatus)) {
        mOfflineCacheEntry->Doom();
    }
    else {
        PRBool succeeded;
        if (NS_SUCCEEDED(GetRequestSucceeded(&succeeded)) && !succeeded)
            mOfflineCacheEntry->Doom();
    }

    mOfflineCacheEntry = 0;
    mOfflineCacheAccess = 0;

    if (mCachingOpportunistically) {
        nsCOMPtr<nsIApplicationCacheService> appCacheService =
            do_GetService(NS_APPLICATIONCACHESERVICE_CONTRACTID);
        if (appCacheService) {
            nsCAutoString cacheKey;
            GenerateCacheKey(mPostID, cacheKey);
            appCacheService->CacheOpportunistically(mApplicationCache,
                                                    cacheKey);
        }
    }
}


// Initialize the cache entry for writing.
//  - finalize storage policy
//  - store security info
//  - update expiration time
//  - store headers and other meta data
nsresult
nsHttpChannel::InitCacheEntry()
{
    nsresult rv;

    NS_ENSURE_TRUE(mCacheEntry, NS_ERROR_UNEXPECTED);
    // if only reading, nothing to be done here.
    if (mCacheAccess == nsICache::ACCESS_READ)
        return NS_OK;

    // Don't cache the response again if already cached...
    if (mCachedContentIsValid)
        return NS_OK;

    LOG(("nsHttpChannel::InitCacheEntry [this=%p entry=%p]\n",
        this, mCacheEntry.get()));

    // The no-store directive within the 'Cache-Control:' header indicates
    // that we must not store the response in a persistent cache.
    if (mResponseHead->NoStore())
        mLoadFlags |= INHIBIT_PERSISTENT_CACHING;

    // Only cache SSL content on disk if the server sent a
    // Cache-Control: public header, or if the user set the pref
    if (!gHttpHandler->CanCacheAllSSLContent() &&
        mConnectionInfo->UsingSSL() && !mResponseHead->CacheControlPublic())
        mLoadFlags |= INHIBIT_PERSISTENT_CACHING;

    if (mLoadFlags & INHIBIT_PERSISTENT_CACHING) {
        rv = mCacheEntry->SetStoragePolicy(nsICache::STORE_IN_MEMORY);
        if (NS_FAILED(rv)) return rv;
    }

    // Set the expiration time for this cache entry
    rv = UpdateExpirationTime();
    if (NS_FAILED(rv)) return rv;

    rv = AddCacheEntryHeaders(mCacheEntry);
    if (NS_FAILED(rv)) return rv;

    mInitedCacheEntry = PR_TRUE;
    return NS_OK;
}


nsresult
nsHttpChannel::InitOfflineCacheEntry()
{
    if (!mOfflineCacheEntry) {
        return NS_OK;
    }

    if (mResponseHead->NoStore()) {
        CloseOfflineCacheEntry();

        return NS_OK;
    }

    // This entry's expiration time should match the main entry's expiration
    // time.  UpdateExpirationTime() will keep it in sync once the offline
    // cache entry has been created.
    if (mCacheEntry) {
        PRUint32 expirationTime;
        nsresult rv = mCacheEntry->GetExpirationTime(&expirationTime);
        NS_ENSURE_SUCCESS(rv, rv);

        mOfflineCacheEntry->SetExpirationTime(expirationTime);
    }

    return AddCacheEntryHeaders(mOfflineCacheEntry);
}


nsresult
nsHttpChannel::AddCacheEntryHeaders(nsICacheEntryDescriptor *entry)
{
    nsresult rv;

    // Store secure data in memory only
    if (mSecurityInfo)
        entry->SetSecurityInfo(mSecurityInfo);

    // Store the HTTP request method with the cache entry so we can distinguish
    // for example GET and HEAD responses.
    rv = entry->SetMetaDataElement("request-method",
                                   mRequestHead.Method().get());
    if (NS_FAILED(rv)) return rv;

    // Store the HTTP authorization scheme used if any...
    rv = StoreAuthorizationMetaData(entry);
    if (NS_FAILED(rv)) return rv;

    // Iterate over the headers listed in the Vary response header, and
    // store the value of the corresponding request header so we can verify
    // that it has not varied when we try to re-use the cached response at
    // a later time.  Take care not to store "Cookie" headers though.  We
    // take care of "Vary: cookie" in ResponseWouldVary.
    //
    // NOTE: if "Vary: accept, cookie", then we will store the "accept" header
    // in the cache.  we could try to avoid needlessly storing the "accept"
    // header in this case, but it doesn't seem worth the extra code to perform
    // the check.
    {
        nsCAutoString buf, metaKey;
        mResponseHead->GetHeader(nsHttp::Vary, buf);
        if (!buf.IsEmpty()) {
            NS_NAMED_LITERAL_CSTRING(prefix, "request-");
           
            char *val = buf.BeginWriting(); // going to munge buf
            char *token = nsCRT::strtok(val, NS_HTTP_HEADER_SEPS, &val);
            while (token) {
                if ((*token != '*') && (PL_strcasecmp(token, "cookie") != 0)) {
                    nsHttpAtom atom = nsHttp::ResolveAtom(token);
                    const char *requestVal = mRequestHead.PeekHeader(atom);
                    if (requestVal) {
                        // build cache meta data key and set meta data element...
                        metaKey = prefix + nsDependentCString(token);
                        entry->SetMetaDataElement(metaKey.get(), requestVal);
                    }
                }
                token = nsCRT::strtok(val, NS_HTTP_HEADER_SEPS, &val);
            }
        }
    }


    // Store the received HTTP head with the cache entry as an element of
    // the meta data.
    nsCAutoString head;
    mResponseHead->Flatten(head, PR_TRUE);
    rv = entry->SetMetaDataElement("response-head", head.get());

    return rv;
}

inline void
GetAuthType(const char *challenge, nsCString &authType)
{
    const char *p;

    // get the challenge type
    if ((p = strchr(challenge, ' ')) != nsnull)
        authType.Assign(challenge, p - challenge);
    else
        authType.Assign(challenge);
}

nsresult
nsHttpChannel::StoreAuthorizationMetaData(nsICacheEntryDescriptor *entry)
{
    // Not applicable to proxy authorization...
    const char *val = mRequestHead.PeekHeader(nsHttp::Authorization);
    if (!val)
        return NS_OK;

    // eg. [Basic realm="wally world"]
    nsCAutoString buf;
    GetAuthType(val, buf);
    return entry->SetMetaDataElement("auth", buf.get());
}

// Finalize the cache entry
//  - may need to rewrite response headers if any headers changed
//  - may need to recalculate the expiration time if any headers changed
//  - called only for freshly written cache entries
nsresult
nsHttpChannel::FinalizeCacheEntry()
{
    LOG(("nsHttpChannel::FinalizeCacheEntry [this=%p]\n", this));

    if (mResponseHead && mResponseHeadersModified) {
        // Set the expiration time for this cache entry
        nsresult rv = UpdateExpirationTime();
        if (NS_FAILED(rv)) return rv;
    }
    return NS_OK;
}

// Open an output stream to the cache entry and insert a listener tee into
// the chain of response listeners.
nsresult
nsHttpChannel::InstallCacheListener(PRUint32 offset)
{
    nsresult rv;

    LOG(("Preparing to write data into the cache [uri=%s]\n", mSpec.get()));

    NS_ASSERTION(mCacheEntry, "no cache entry");
    NS_ASSERTION(mListener, "no listener");

    nsCOMPtr<nsIOutputStream> out;
    rv = mCacheEntry->OpenOutputStream(offset, getter_AddRefs(out));
    if (NS_FAILED(rv)) return rv;

    // XXX disk cache does not support overlapped i/o yet
#if 0
    // Mark entry valid inorder to allow simultaneous reading...
    rv = mCacheEntry->MarkValid();
    if (NS_FAILED(rv)) return rv;
#endif

    nsCOMPtr<nsIStreamListenerTee> tee =
        do_CreateInstance(kStreamListenerTeeCID, &rv);
    if (NS_FAILED(rv)) return rv;

    rv = tee->Init(mListener, out);
    if (NS_FAILED(rv)) return rv;

    mListener = tee;
    return NS_OK;
}

nsresult
nsHttpChannel::InstallOfflineCacheListener()
{
    nsresult rv;

    LOG(("Preparing to write data into the offline cache [uri=%s]\n",
         mSpec.get()));

    NS_ASSERTION(mOfflineCacheEntry, "no offline cache entry");
    NS_ASSERTION(mListener, "no listener");

    nsCOMPtr<nsIOutputStream> out;
    rv = mOfflineCacheEntry->OpenOutputStream(0, getter_AddRefs(out));
    if (NS_FAILED(rv)) return rv;

    nsCOMPtr<nsIStreamListenerTee> tee =
        do_CreateInstance(kStreamListenerTeeCID, &rv);
    if (NS_FAILED(rv)) return rv;

    rv = tee->Init(mListener, out);
    if (NS_FAILED(rv)) return rv;

    mListener = tee;

    return NS_OK;
}

void
nsHttpChannel::ClearBogusContentEncodingIfNeeded()
{
    // For .gz files, apache sends both a Content-Type: application/x-gzip
    // as well as Content-Encoding: gzip, which is completely wrong.  In
    // this case, we choose to ignore the rogue Content-Encoding header. We
    // must do this early on so as to prevent it from being seen up stream.
    // The same problem exists for Content-Encoding: compress in default
    // Apache installs.
    if (mResponseHead->HasHeaderValue(nsHttp::Content_Encoding, "gzip") && (
        mResponseHead->ContentType().EqualsLiteral(APPLICATION_GZIP) ||
        mResponseHead->ContentType().EqualsLiteral(APPLICATION_GZIP2) ||
        mResponseHead->ContentType().EqualsLiteral(APPLICATION_GZIP3))) {
        // clear the Content-Encoding header
        mResponseHead->ClearHeader(nsHttp::Content_Encoding);
    }
    else if (mResponseHead->HasHeaderValue(nsHttp::Content_Encoding, "compress") && (
             mResponseHead->ContentType().EqualsLiteral(APPLICATION_COMPRESS) ||
             mResponseHead->ContentType().EqualsLiteral(APPLICATION_COMPRESS2))) {
        // clear the Content-Encoding header
        mResponseHead->ClearHeader(nsHttp::Content_Encoding);
    }
}

//-----------------------------------------------------------------------------
// nsHttpChannel <redirect>
//-----------------------------------------------------------------------------

static PLDHashOperator
CopyProperties(const nsAString& aKey, nsIVariant *aData, void *aClosure)
{
    nsIWritablePropertyBag* bag = static_cast<nsIWritablePropertyBag*>
                                             (aClosure);
    bag->SetProperty(aKey, aData);
    return PL_DHASH_NEXT;
}

nsresult
nsHttpChannel::SetupReplacementChannel(nsIURI       *newURI, 
                                       nsIChannel   *newChannel,
                                       PRBool        preserveMethod)
{
    LOG(("nsHttpChannel::SetupReplacementChannel "
         "[this=%p newChannel=%p preserveMethod=%d]",
         this, newChannel, preserveMethod));
    PRUint32 newLoadFlags = mLoadFlags | LOAD_REPLACE;
    // if the original channel was using SSL and this channel is not using
    // SSL, then no need to inhibit persistent caching.  however, if the
    // original channel was not using SSL and has INHIBIT_PERSISTENT_CACHING
    // set, then allow the flag to apply to the redirected channel as well.
    // since we force set INHIBIT_PERSISTENT_CACHING on all HTTPS channels,
    // we only need to check if the original channel was using SSL.
    if (mConnectionInfo->UsingSSL())
        newLoadFlags &= ~INHIBIT_PERSISTENT_CACHING;

    // Do not pass along LOAD_CHECK_OFFLINE_CACHE
    newLoadFlags &= ~LOAD_CHECK_OFFLINE_CACHE;

    newChannel->SetLoadGroup(mLoadGroup); 
    newChannel->SetNotificationCallbacks(mCallbacks);
    newChannel->SetLoadFlags(newLoadFlags);

    nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(newChannel);
    if (!httpChannel)
        return NS_OK; // no other options to set

    if (preserveMethod) {
        nsCOMPtr<nsIUploadChannel> uploadChannel =
            do_QueryInterface(httpChannel);
        nsCOMPtr<nsIUploadChannel2> uploadChannel2 =
            do_QueryInterface(httpChannel);
        if (mUploadStream && (uploadChannel2 || uploadChannel)) {
            // rewind upload stream
            nsCOMPtr<nsISeekableStream> seekable = do_QueryInterface(mUploadStream);
            if (seekable)
                seekable->Seek(nsISeekableStream::NS_SEEK_SET, 0);

            // replicate original call to SetUploadStream...
            if (uploadChannel2) {
                const char *ctype = mRequestHead.PeekHeader(nsHttp::Content_Type);
                if (!ctype)
                    ctype = "";
                const char *clen  = mRequestHead.PeekHeader(nsHttp::Content_Length);
                PRInt64 len = clen ? nsCRT::atoll(clen) : -1;
                uploadChannel2->ExplicitSetUploadStream(
                        mUploadStream,
                        nsDependentCString(ctype),
                        len,
                        nsDependentCString(mRequestHead.Method()),
                        mUploadStreamHasHeaders);
            }
            else {
                if (mUploadStreamHasHeaders)
                    uploadChannel->SetUploadStream(mUploadStream, EmptyCString(),
                                                   -1);
                else {
                    const char *ctype =
                        mRequestHead.PeekHeader(nsHttp::Content_Type);
                    const char *clen =
                        mRequestHead.PeekHeader(nsHttp::Content_Length);
                    if (!ctype) {
                        ctype = "application/octet-stream";
                    }
                    if (clen) {
                        uploadChannel->SetUploadStream(mUploadStream,
                                                       nsDependentCString(ctype),
                                                       atoi(clen));
                    }
                }
            }
        }
        // since preserveMethod is true, we need to ensure that the appropriate 
        // request method gets set on the channel, regardless of whether or not 
        // we set the upload stream above. This means SetRequestMethod() will
        // be called twice if ExplicitSetUploadStream() gets called above.

        httpChannel->SetRequestMethod(nsDependentCString(mRequestHead.Method()));
    }
    // convey the referrer if one was used for this channel to the next one
    if (mReferrer)
        httpChannel->SetReferrer(mReferrer);
    // convey the mAllowPipelining flag
    httpChannel->SetAllowPipelining(mAllowPipelining);
    // convey the new redirection limit
    httpChannel->SetRedirectionLimit(mRedirectionLimit - 1);

    nsCOMPtr<nsIHttpChannelInternal> httpInternal = do_QueryInterface(newChannel);
    if (httpInternal) {
        // convey the mForceAllowThirdPartyCookie flag
        httpInternal->SetForceAllowThirdPartyCookie(mForceAllowThirdPartyCookie);

        // update the DocumentURI indicator since we are being redirected.
        // if this was a top-level document channel, then the new channel
        // should have its mDocumentURI point to newURI; otherwise, we
        // just need to pass along our mDocumentURI to the new channel.
        if (newURI && (mURI == mDocumentURI))
            httpInternal->SetDocumentURI(newURI);
        else
            httpInternal->SetDocumentURI(mDocumentURI);
    } 
    
    // convey the mApplyConversion flag (bug 91862)
    nsCOMPtr<nsIEncodedChannel> encodedChannel = do_QueryInterface(httpChannel);
    if (encodedChannel)
        encodedChannel->SetApplyConversion(mApplyConversion);

    // transfer the resume information
    if (mResuming) {
        nsCOMPtr<nsIResumableChannel> resumableChannel(do_QueryInterface(newChannel));
        if (!resumableChannel) {
            NS_WARNING("Got asked to resume, but redirected to non-resumable channel!");
            return NS_ERROR_NOT_RESUMABLE;
        }
        resumableChannel->ResumeAt(mStartPos, mEntityID);
    }

    // transfer application cache information
    nsCOMPtr<nsIApplicationCacheChannel> appCacheChannel =
        do_QueryInterface(newChannel);
    if (appCacheChannel) {
        appCacheChannel->SetApplicationCache(mApplicationCache);
        appCacheChannel->SetInheritApplicationCache(mInheritApplicationCache);
        // We purposely avoid transfering mChooseApplicationCache.
    }

    // transfer any properties
    nsCOMPtr<nsIWritablePropertyBag> bag(do_QueryInterface(newChannel));
    if (bag)
        mPropertyHash.EnumerateRead(CopyProperties, bag.get());

    return NS_OK;
}

nsresult
nsHttpChannel::ProcessRedirection(PRUint32 redirectType)
{
    LOG(("nsHttpChannel::ProcessRedirection [this=%p type=%u]\n",
        this, redirectType));

    const char *location = mResponseHead->PeekHeader(nsHttp::Location);

    // if a location header was not given, then we can't perform the redirect,
    // so just carry on as though this were a normal response.
    if (!location)
        return NS_ERROR_FAILURE;

    // make sure non-ASCII characters in the location header are escaped.
    nsCAutoString locationBuf;
    if (NS_EscapeURL(location, -1, esc_OnlyNonASCII, locationBuf))
        location = locationBuf.get();

    if (mRedirectionLimit == 0) {
        LOG(("redirection limit reached!\n"));
        // this error code is fatal, and should be conveyed to our listener.
        Cancel(NS_ERROR_REDIRECT_LOOP);
        return NS_ERROR_REDIRECT_LOOP;
    }

    LOG(("redirecting to: %s [redirection-limit=%u]\n",
        location, PRUint32(mRedirectionLimit)));

    nsresult rv;
    nsCOMPtr<nsIChannel> newChannel;
    nsCOMPtr<nsIURI> newURI;

    // create a new URI using the location header and the current URL
    // as a base...
    nsCOMPtr<nsIIOService> ioService;
    rv = gHttpHandler->GetIOService(getter_AddRefs(ioService));
    if (NS_FAILED(rv)) return rv;

    // the new uri should inherit the origin charset of the current uri
    nsCAutoString originCharset;
    rv = mURI->GetOriginCharset(originCharset);
    if (NS_FAILED(rv))
        originCharset.Truncate();

    rv = ioService->NewURI(nsDependentCString(location), originCharset.get(), mURI,
                           getter_AddRefs(newURI));
    if (NS_FAILED(rv)) return rv;

    if (mApplicationCache) {
        // if we are redirected to a different origin check if there is a fallback
        // cache entry to fall back to. we don't care about file strict 
        // checking, at least mURI is not a file URI.
        if (!NS_SecurityCompareURIs(mURI, newURI, PR_FALSE)) {
            PRBool fallingBack;
            rv = ProcessFallback(&fallingBack);
            if (NS_SUCCEEDED(rv) && fallingBack) {
                // do not continue with redirect processing, fallback is in
                // progress now.
                return NS_OK;
            }
        }
    }

    // Kill the current cache entry if we are redirecting
    // back to ourself.
    PRBool redirectingBackToSameURI = PR_FALSE;
    if (mCacheEntry && (mCacheAccess & nsICache::ACCESS_WRITE) &&
        NS_SUCCEEDED(mURI->Equals(newURI, &redirectingBackToSameURI)) &&
        redirectingBackToSameURI)
            mCacheEntry->Doom();

    // move the reference of the old location to the new one if the new
    // one has none.
    nsCOMPtr<nsIURL> newURL = do_QueryInterface(newURI);
    if (newURL) {
        nsCAutoString ref;
        rv = newURL->GetRef(ref);
        if (NS_SUCCEEDED(rv) && ref.IsEmpty()) {
            nsCOMPtr<nsIURL> baseURL(do_QueryInterface(mURI));
            if (baseURL) {
                baseURL->GetRef(ref);
                if (!ref.IsEmpty())
                    newURL->SetRef(ref);
            }
        }
    }

    // if we need to re-send POST data then be sure to ask the user first.
    PRBool preserveMethod = (redirectType == 307);
    if (preserveMethod && mUploadStream) {
        rv = PromptTempRedirect();
        if (NS_FAILED(rv)) return rv;
    }

    rv = ioService->NewChannelFromURI(newURI, getter_AddRefs(newChannel));
    if (NS_FAILED(rv)) return rv;

    rv = SetupReplacementChannel(newURI, newChannel, preserveMethod);
    if (NS_FAILED(rv)) return rv;

    PRUint32 redirectFlags;
    if (redirectType == 301) // Moved Permanently
        redirectFlags = nsIChannelEventSink::REDIRECT_PERMANENT;
    else
        redirectFlags = nsIChannelEventSink::REDIRECT_TEMPORARY;

    // verify that this is a legal redirect
    rv = gHttpHandler->OnChannelRedirect(this, newChannel, redirectFlags);
    if (NS_FAILED(rv))
        return rv;

    // Make sure to do this _after_ calling OnChannelRedirect
    newChannel->SetOriginalURI(mOriginalURI);    

    // And now, the deprecated way
    nsCOMPtr<nsIHttpEventSink> httpEventSink;
    GetCallback(httpEventSink);
    if (httpEventSink) {
        // NOTE: nsIHttpEventSink is only used for compatibility with pre-1.8
        // versions.
        rv = httpEventSink->OnRedirect(this, newChannel);
        if (NS_FAILED(rv)) return rv;
    }
    // XXX we used to talk directly with the script security manager, but that
    // should really be handled by the event sink implementation.

    // begin loading the new channel
    rv = newChannel->AsyncOpen(mListener, mListenerContext);
    if (NS_FAILED(rv)) return rv;

    // close down this channel
    Cancel(NS_BINDING_REDIRECTED);
    
    // disconnect from our listener
    mListener = 0;
    mListenerContext = 0;
    // and from our callbacks
    mCallbacks = nsnull;
    mProgressSink = nsnull;
    return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel <auth>
//-----------------------------------------------------------------------------

// buf contains "domain\user"
static void
ParseUserDomain(PRUnichar *buf,
                const PRUnichar **user,
                const PRUnichar **domain)
{
    PRUnichar *p = buf;
    while (*p && *p != '\\') ++p;
    if (!*p)
        return;
    *p = '\0';
    *domain = buf;
    *user = p + 1;
}

// helper function for setting identity from raw user:pass
static void
SetIdent(nsHttpAuthIdentity &ident,
         PRUint32 authFlags,
         PRUnichar *userBuf,
         PRUnichar *passBuf)
{
    const PRUnichar *user = userBuf;
    const PRUnichar *domain = nsnull;

    if (authFlags & nsIHttpAuthenticator::IDENTITY_INCLUDES_DOMAIN)
        ParseUserDomain(userBuf, &user, &domain);

    ident.Set(domain, user, passBuf);
}

// helper function for getting an auth prompt from an interface requestor
static void
GetAuthPrompt(nsIInterfaceRequestor *ifreq, PRBool proxyAuth,
              nsIAuthPrompt2 **result)
{
    if (!ifreq)
        return;

    PRUint32 promptReason;
    if (proxyAuth)
        promptReason = nsIAuthPromptProvider::PROMPT_PROXY;
    else 
        promptReason = nsIAuthPromptProvider::PROMPT_NORMAL;

    nsCOMPtr<nsIAuthPromptProvider> promptProvider = do_GetInterface(ifreq);
    if (promptProvider)
        promptProvider->GetAuthPrompt(promptReason,
                                      NS_GET_IID(nsIAuthPrompt2),
                                      reinterpret_cast<void**>(result));
    else
        NS_QueryAuthPrompt2(ifreq, result);
}

// generate credentials for the given challenge, and update the auth cache.
nsresult
nsHttpChannel::GenCredsAndSetEntry(nsIHttpAuthenticator *auth,
                                   PRBool proxyAuth,
                                   const char *scheme,
                                   const char *host,
                                   PRInt32 port,
                                   const char *directory,
                                   const char *realm,
                                   const char *challenge,
                                   const nsHttpAuthIdentity &ident,
                                   nsCOMPtr<nsISupports> &sessionState,
                                   char **result)
{
    nsresult rv;
    PRUint32 authFlags;

    rv = auth->GetAuthFlags(&authFlags);
    if (NS_FAILED(rv)) return rv;

    nsISupports *ss = sessionState;

    // set informations that depend on whether
    // we're authenticating against a proxy
    // or a webserver
    nsISupports **continuationState;

    if (proxyAuth) {
        continuationState = &mProxyAuthContinuationState;
    } else {
        continuationState = &mAuthContinuationState;
    }
    
    nsCOMPtr<nsIHttpAuthenticator_1_9_2> auth192 = 
        do_QueryInterface(auth);
        
    PRUint32 generateFlags;
    if (auth192) {
        rv = auth192->GenerateCredentials_1_9_2(this,
                                                challenge,
                                                proxyAuth,
                                                ident.Domain(),
                                                ident.User(),
                                                ident.Password(),
                                                &ss,
                                                &*continuationState,
                                                &generateFlags,
                                                result);
    }
    else {
        generateFlags = 0;
        rv = auth->GenerateCredentials(this,
                                       challenge,
                                       proxyAuth,
                                       ident.Domain(),
                                       ident.User(),
                                       ident.Password(),
                                       &ss,
                                       &*continuationState,
                                       result);
    }

    sessionState.swap(ss);
    if (NS_FAILED(rv)) return rv;

    // don't log this in release build since it could contain sensitive info.
#ifdef DEBUG 
    LOG(("generated creds: %s\n", *result));
#endif

    // find out if this authenticator allows reuse of credentials and/or
    // challenge.
    PRBool saveCreds =
        0 != (authFlags & nsIHttpAuthenticator::REUSABLE_CREDENTIALS);
    PRBool saveChallenge =
        0 != (authFlags & nsIHttpAuthenticator::REUSABLE_CHALLENGE);

    PRBool saveIdentity =
        0 == (generateFlags & nsIHttpAuthenticator_1_9_2::USING_INTERNAL_IDENTITY);

    // this getter never fails
    nsHttpAuthCache *authCache = gHttpHandler->AuthCache();

    // create a cache entry.  we do this even though we don't yet know that
    // these credentials are valid b/c we need to avoid prompting the user
    // more than once in case the credentials are valid.
    //
    // if the credentials are not reusable, then we don't bother sticking
    // them in the auth cache.
    rv = authCache->SetAuthEntry(scheme, host, port, directory, realm,
                                 saveCreds ? *result : nsnull,
                                 saveChallenge ? challenge : nsnull,
                                 saveIdentity ? &ident : nsnull,
                                 sessionState);
    return rv;
}

nsresult
nsHttpChannel::ProcessAuthentication(PRUint32 httpStatus)
{
    LOG(("nsHttpChannel::ProcessAuthentication [this=%p code=%u]\n",
        this, httpStatus));

    if (mLoadFlags & LOAD_ANONYMOUS) {
      return NS_ERROR_NOT_AVAILABLE;
    }

    const char *challenges;
    mProxyAuth = (httpStatus == 407);

    nsresult rv = PrepareForAuthentication(mProxyAuth);
    if (NS_FAILED(rv))
        return rv;

    if (mProxyAuth) {
        // only allow a proxy challenge if we have a proxy server configured.
        // otherwise, we could inadvertantly expose the user's proxy
        // credentials to an origin server.  We could attempt to proceed as
        // if we had received a 401 from the server, but why risk flirting
        // with trouble?  IE similarly rejects 407s when a proxy server is
        // not configured, so there's no reason not to do the same.
        if (!mConnectionInfo->UsingHttpProxy()) {
            LOG(("rejecting 407 when proxy server not configured!\n"));
            return NS_ERROR_UNEXPECTED;
        }
        if (mConnectionInfo->UsingSSL() && !mTransaction->SSLConnectFailed()) {
            // we need to verify that this challenge came from the proxy
            // server itself, and not some server on the other side of the
            // SSL tunnel.
            LOG(("rejecting 407 from origin server!\n"));
            return NS_ERROR_UNEXPECTED;
        }
        challenges = mResponseHead->PeekHeader(nsHttp::Proxy_Authenticate);
    }
    else
        challenges = mResponseHead->PeekHeader(nsHttp::WWW_Authenticate);
    NS_ENSURE_TRUE(challenges, NS_ERROR_UNEXPECTED);

    nsCAutoString creds;
    rv = GetCredentials(challenges, mProxyAuth, creds);
    if (rv == NS_ERROR_IN_PROGRESS)  {
        // authentication prompt has been invoked and result
        // is expected asynchronously
        mAuthRetryPending = PR_TRUE;
        // suspend the transaction pump to stop receiving the
        // unauthenticated content data. We will throw that data
        // away when user provides credentials or resume the pump
        // when user refuses to authenticate.
        LOG(("Suspending the transaction, asynchronously prompting for credentials"));
        mTransactionPump->Suspend();
        return NS_OK;
    }
    else if (NS_FAILED(rv))
        LOG(("unable to authenticate\n"));
    else {
        // set the authentication credentials
        if (mProxyAuth)
            mRequestHead.SetHeader(nsHttp::Proxy_Authorization, creds);
        else
            mRequestHead.SetHeader(nsHttp::Authorization, creds);

        mAuthRetryPending = PR_TRUE; // see DoAuthRetry
    }
    return rv;
}

nsresult
nsHttpChannel::PrepareForAuthentication(PRBool proxyAuth)
{
    LOG(("nsHttpChannel::PrepareForAuthentication [this=%p]\n", this));

    if (!proxyAuth) {
        // reset the current proxy continuation state because our last
        // authentication attempt was completed successfully.
        NS_IF_RELEASE(mProxyAuthContinuationState);
        LOG(("  proxy continuation state has been reset"));
    }

    if (!mConnectionInfo->UsingHttpProxy() || mProxyAuthType.IsEmpty())
        return NS_OK;

    // We need to remove any Proxy_Authorization header left over from a
    // non-request based authentication handshake (e.g., for NTLM auth).

    nsCAutoString contractId;
    contractId.Assign(NS_HTTP_AUTHENTICATOR_CONTRACTID_PREFIX);
    contractId.Append(mProxyAuthType);

    nsresult rv;
    nsCOMPtr<nsIHttpAuthenticator> precedingAuth =
        do_GetService(contractId.get(), &rv);
    if (NS_FAILED(rv))
        return rv;

    PRUint32 precedingAuthFlags;
    rv = precedingAuth->GetAuthFlags(&precedingAuthFlags);
    if (NS_FAILED(rv))
        return rv;

    if (!(precedingAuthFlags & nsIHttpAuthenticator::REQUEST_BASED)) {
        const char *challenges =
                mResponseHead->PeekHeader(nsHttp::Proxy_Authenticate);
        if (!challenges) {
            // delete the proxy authorization header because we weren't
            // asked to authenticate
            mRequestHead.ClearHeader(nsHttp::Proxy_Authorization);
            LOG(("  cleared proxy authorization header"));
        }
    }

    return NS_OK;
}

nsresult
nsHttpChannel::GetCredentials(const char *challenges,
                              PRBool proxyAuth,
                              nsAFlatCString &creds)
{
    nsCOMPtr<nsIHttpAuthenticator> auth;
    nsCAutoString challenge;

    nsCString authType; // force heap allocation to enable string sharing since
                        // we'll be assigning this value into mAuthType.

    // set informations that depend on whether we're authenticating against a
    // proxy or a webserver
    nsISupports **currentContinuationState;
    nsCString *currentAuthType;

    if (proxyAuth) {
        currentContinuationState = &mProxyAuthContinuationState;
        currentAuthType = &mProxyAuthType;
    } else {
        currentContinuationState = &mAuthContinuationState;
        currentAuthType = &mAuthType;
    }

    nsresult rv = NS_ERROR_NOT_AVAILABLE;
    PRBool gotCreds = PR_FALSE;
    
    // figure out which challenge we can handle and which authenticator to use.
    for (const char *eol = challenges - 1; eol; ) {
        const char *p = eol + 1;

        // get the challenge string (LF separated -- see nsHttpHeaderArray)
        if ((eol = strchr(p, '\n')) != nsnull)
            challenge.Assign(p, eol - p);
        else
            challenge.Assign(p);

        rv = GetAuthenticator(challenge.get(), authType, getter_AddRefs(auth));
        if (NS_SUCCEEDED(rv)) {
            //
            // if we've already selected an auth type from a previous challenge
            // received while processing this channel, then skip others until
            // we find a challenge corresponding to the previously tried auth
            // type.
            //
            if (!currentAuthType->IsEmpty() && authType != *currentAuthType)
                continue;

            //
            // we allow the routines to run all the way through before we
            // decide if they are valid.
            //
            // we don't worry about the auth cache being altered because that
            // would have been the last step, and if the error is from updating
            // the authcache it wasn't really altered anyway. -CTN 
            //
            // at this point the code is really only useful for client side
            // errors (it will not automatically fail over to do a different
            // auth type if the server keeps rejecting what is being sent, even
            // if a particular auth method only knows 1 thing, like a
            // non-identity based authentication method)
            //
            rv = GetCredentialsForChallenge(challenge.get(), authType.get(),
                                            proxyAuth, auth, creds);
            if (NS_SUCCEEDED(rv)) {
                gotCreds = PR_TRUE;
                *currentAuthType = authType;

                break;
            }
            else if (rv == NS_ERROR_IN_PROGRESS) {
                // authentication prompt has been invoked and result is
                // expected asynchronously, save current challenge being
                // processed and all remaining challenges to use later in
                // OnAuthAvailable and now immediately return
                mCurrentChallenge = challenge;
                mRemainingChallenges = eol ? eol+1 : nsnull;
                return rv;
            }

            // reset the auth type and continuation state
            NS_IF_RELEASE(*currentContinuationState);
            currentAuthType->Truncate();
        }
    }

    if (!gotCreds && !currentAuthType->IsEmpty()) {
        // looks like we never found the auth type we were looking for.
        // reset the auth type and continuation state, and try again.
        currentAuthType->Truncate();
        NS_IF_RELEASE(*currentContinuationState);

        rv = GetCredentials(challenges, proxyAuth, creds);
    }

    return rv;
}

nsresult
nsHttpChannel::GetAuthorizationMembers(PRBool proxyAuth,
                                       nsCSubstring& scheme,
                                       const char*& host,
                                       PRInt32& port,
                                       nsCSubstring& path,
                                       nsHttpAuthIdentity*& ident,
                                       nsISupports**& continuationState)
{
    if (proxyAuth) {
        NS_ASSERTION (mConnectionInfo->UsingHttpProxy(), "proxyAuth is true, but no HTTP proxy is configured!");

        host = mConnectionInfo->ProxyHost();
        port = mConnectionInfo->ProxyPort();
        ident = &mProxyIdent;
        scheme.AssignLiteral("http");

        continuationState = &mProxyAuthContinuationState;
    }
    else {
        host = mConnectionInfo->Host();
        port = mConnectionInfo->Port();
        ident = &mIdent;

        nsresult rv;
        rv = GetCurrentPath(path);
        if (NS_FAILED(rv)) return rv;

        rv = mURI->GetScheme(scheme);
        if (NS_FAILED(rv)) return rv;

        continuationState = &mAuthContinuationState;
    }

    return NS_OK;
}

nsresult
nsHttpChannel::GetCredentialsForChallenge(const char *challenge,
                                          const char *authType,
                                          PRBool proxyAuth,
                                          nsIHttpAuthenticator *auth,
                                          nsAFlatCString &creds)
{
    LOG(("nsHttpChannel::GetCredentialsForChallenge [this=%p proxyAuth=%d challenges=%s]\n",
        this, proxyAuth, challenge));

    // this getter never fails
    nsHttpAuthCache *authCache = gHttpHandler->AuthCache();

    PRUint32 authFlags;
    nsresult rv = auth->GetAuthFlags(&authFlags);
    if (NS_FAILED(rv)) return rv;

    nsCAutoString realm;
    ParseRealm(challenge, realm);

    // if no realm, then use the auth type as the realm.  ToUpperCase so the
    // ficticious realm stands out a bit more.
    // XXX this will cause some single signon misses!
    // XXX this was meant to be used with NTLM, which supplies no realm.
    /*
    if (realm.IsEmpty()) {
        realm = authType;
        ToUpperCase(realm);
    }
    */

    // set informations that depend on whether
    // we're authenticating against a proxy
    // or a webserver
    const char *host;
    PRInt32 port;
    nsHttpAuthIdentity *ident;
    nsCAutoString path, scheme;
    PRBool identFromURI = PR_FALSE;
    nsISupports **continuationState;

    rv = GetAuthorizationMembers(proxyAuth, scheme, host, port, path, ident, continuationState);
    if (NS_FAILED(rv)) return rv;

    if (!proxyAuth) {
        // if this is the first challenge, then try using the identity
        // specified in the URL.
        if (mIdent.IsEmpty()) {
            GetIdentityFromURI(authFlags, mIdent);
            identFromURI = !mIdent.IsEmpty();
        }
    }

    //
    // if we already tried some credentials for this transaction, then
    // we need to possibly clear them from the cache, unless the credentials
    // in the cache have changed, in which case we'd want to give them a
    // try instead.
    //
    nsHttpAuthEntry *entry = nsnull;
    authCache->GetAuthEntryForDomain(scheme.get(), host, port, realm.get(), &entry);

    // hold reference to the auth session state (in case we clear our
    // reference to the entry).
    nsCOMPtr<nsISupports> sessionStateGrip;
    if (entry)
        sessionStateGrip = entry->mMetaData;

    // for digest auth, maybe our cached nonce value simply timed out...
    PRBool identityInvalid;
    nsISupports *sessionState = sessionStateGrip;
    rv = auth->ChallengeReceived(this,
                                 challenge,
                                 proxyAuth,
                                 &sessionState,
                                 &*continuationState,
                                 &identityInvalid);
    sessionStateGrip.swap(sessionState);
    if (NS_FAILED(rv)) return rv;

    LOG(("  identity invalid = %d\n", identityInvalid));

    if (identityInvalid) {
        if (entry) {
            if (ident->Equals(entry->Identity())) {
                LOG(("  clearing bad auth cache entry\n"));
                // ok, we've already tried this user identity, so clear the
                // corresponding entry from the auth cache.
                authCache->ClearAuthEntry(scheme.get(), host, port, realm.get());
                entry = nsnull;
                ident->Clear();
            }
            else if (!identFromURI || nsCRT::strcmp(ident->User(), entry->Identity().User()) == 0) {
                LOG(("  taking identity from auth cache\n"));
                // the password from the auth cache is more likely to be
                // correct than the one in the URL.  at least, we know that it
                // works with the given username.  it is possible for a server
                // to distinguish logons based on the supplied password alone,
                // but that would be quite unusual... and i don't think we need
                // to worry about such unorthodox cases.
                ident->Set(entry->Identity());
                identFromURI = PR_FALSE;
                if (entry->Creds()[0] != '\0') {
                    LOG(("    using cached credentials!\n"));
                    creds.Assign(entry->Creds());
                    return entry->AddPath(path.get());
                }
            }
        }
        else if (!identFromURI) {
            // hmm... identity invalid, but no auth entry!  the realm probably
            // changed (see bug 201986).
            ident->Clear();
        }

        if (!entry && ident->IsEmpty()) {
            PRUint32 level = nsIAuthPrompt2::LEVEL_NONE;
            if (scheme.EqualsLiteral("https"))
                level = nsIAuthPrompt2::LEVEL_SECURE;
            else if (authFlags & nsIHttpAuthenticator::IDENTITY_ENCRYPTED)
                level = nsIAuthPrompt2::LEVEL_PW_ENCRYPTED;

            // at this point we are forced to interact with the user to get
            // their username and password for this domain.
            rv = PromptForIdentity(level, proxyAuth, realm.get(), 
                                   authType, authFlags, *ident);
            if (NS_FAILED(rv)) return rv;
            identFromURI = PR_FALSE;
        }
    }

    if (identFromURI) {
        // Warn the user before automatically using the identity from the URL
        // to automatically log them into a site (see bug 232567).
        if (!ConfirmAuth(NS_LITERAL_STRING("AutomaticAuth"), PR_FALSE)) {
            // calling cancel here sets our mStatus and aborts the HTTP
            // transaction, which prevents OnDataAvailable events.
            Cancel(NS_ERROR_ABORT);
            // this return code alone is not equivalent to Cancel, since
            // it only instructs our caller that authentication failed.
            // without an explicit call to Cancel, our caller would just
            // load the page that accompanies the HTTP auth challenge.
            return NS_ERROR_ABORT;
        }
    }

    //
    // get credentials for the given user:pass
    //
    // always store the credentials we're trying now so that they will be used
    // on subsequent links.  This will potentially remove good credentials from
    // the cache.  This is ok as we don't want to use cached credentials if the
    // user specified something on the URI or in another manner.  This is so
    // that we don't transparently authenticate as someone they're not
    // expecting to authenticate as.
    //
    nsXPIDLCString result;
    rv = GenCredsAndSetEntry(auth, proxyAuth, scheme.get(), host, port, path.get(),
                             realm.get(), challenge, *ident, sessionStateGrip,
                             getter_Copies(result));
    if (NS_SUCCEEDED(rv))
        creds = result;
    return rv;
}

nsresult
nsHttpChannel::GetAuthenticator(const char *challenge,
                                nsCString &authType,
                                nsIHttpAuthenticator **auth)
{
    LOG(("nsHttpChannel::GetAuthenticator [this=%p]\n", this));

    GetAuthType(challenge, authType);
 
    // normalize to lowercase
    ToLowerCase(authType);

    nsCAutoString contractid;
    contractid.Assign(NS_HTTP_AUTHENTICATOR_CONTRACTID_PREFIX);
    contractid.Append(authType);

    return CallGetService(contractid.get(), auth);
}

void
nsHttpChannel::GetIdentityFromURI(PRUint32 authFlags, nsHttpAuthIdentity &ident)
{
    LOG(("nsHttpChannel::GetIdentityFromURI [this=%p]\n", this));

    nsAutoString userBuf;
    nsAutoString passBuf;

    // XXX i18n
    nsCAutoString buf;
    mURI->GetUsername(buf);
    if (!buf.IsEmpty()) {
        NS_UnescapeURL(buf);
        CopyASCIItoUTF16(buf, userBuf);
        mURI->GetPassword(buf);
        if (!buf.IsEmpty()) {
            NS_UnescapeURL(buf);
            CopyASCIItoUTF16(buf, passBuf);
        }
    }

    if (!userBuf.IsEmpty())
        SetIdent(ident, authFlags, (PRUnichar *) userBuf.get(), (PRUnichar *) passBuf.get());
}

void
nsHttpChannel::ParseRealm(const char *challenge, nsACString &realm)
{
    //
    // From RFC2617 section 1.2, the realm value is defined as such:
    //
    //    realm       = "realm" "=" realm-value
    //    realm-value = quoted-string
    //
    // but, we'll accept anything after the the "=" up to the first space, or
    // end-of-line, if the string is not quoted.
    //
    const char *p = PL_strcasestr(challenge, "realm=");
    if (p) {
        PRBool has_quote = PR_FALSE;
        p += 6;
        if (*p == '"') {
            has_quote = PR_TRUE;
            p++;
        }

        const char *end = p;
        while (*end && has_quote) {
           // Loop through all the string characters to find the closing
           // quote, ignoring escaped quotes.
            if (*end == '"' && end[-1] != '\\')
                break;
            ++end;
        }

        if (!has_quote)
            end = strchr(p, ' '); 
        if (end)
            realm.Assign(p, end - p);
        else
            realm.Assign(p);
    }
}


class nsHTTPAuthInformation : public nsAuthInformationHolder {
public:
    nsHTTPAuthInformation(PRUint32 aFlags, const nsString& aRealm,
                          const nsCString& aAuthType)
        : nsAuthInformationHolder(aFlags, aRealm, aAuthType) {}

    void SetToHttpAuthIdentity(PRUint32 authFlags, nsHttpAuthIdentity& identity);
};

void
nsHTTPAuthInformation::SetToHttpAuthIdentity(PRUint32 authFlags, nsHttpAuthIdentity& identity)
{
    identity.Set(Domain().get(), User().get(), Password().get());
}

nsresult
nsHttpChannel::PromptForIdentity(PRUint32    level,
                                 PRBool      proxyAuth,
                                 const char *realm,
                                 const char *authType,
                                 PRUint32 authFlags,
                                 nsHttpAuthIdentity &ident)
{
    LOG(("nsHttpChannel::PromptForIdentity [this=%p]\n", this));

    nsCOMPtr<nsIAuthPrompt2> authPrompt;
    GetAuthPrompt(mCallbacks, proxyAuth, getter_AddRefs(authPrompt));
    if (!authPrompt && mLoadGroup) {
        nsCOMPtr<nsIInterfaceRequestor> cbs;
        mLoadGroup->GetNotificationCallbacks(getter_AddRefs(cbs));
        GetAuthPrompt(cbs, proxyAuth, getter_AddRefs(authPrompt));
    }
    if (!authPrompt)
        return NS_ERROR_NO_INTERFACE;

    // XXX i18n: need to support non-ASCII realm strings (see bug 41489)
    NS_ConvertASCIItoUTF16 realmU(realm);

    nsresult rv;

    // prompt the user...
    PRUint32 promptFlags = 0;
    if (proxyAuth)
        promptFlags |= nsIAuthInformation::AUTH_PROXY;
    else
        promptFlags |= nsIAuthInformation::AUTH_HOST;

    if (authFlags & nsIHttpAuthenticator::IDENTITY_INCLUDES_DOMAIN)
        promptFlags |= nsIAuthInformation::NEED_DOMAIN;

    nsRefPtr<nsHTTPAuthInformation> holder =
        new nsHTTPAuthInformation(promptFlags, realmU,
                                  nsDependentCString(authType));
    if (!holder)
        return NS_ERROR_OUT_OF_MEMORY;

    rv = authPrompt->AsyncPromptAuth(this, this, nsnull, level, holder,
                     getter_AddRefs(mAsyncPromptAuthCancelable));

    if (NS_SUCCEEDED(rv)) {
        // indicate using this error code that authentication prompt
        // result is expected asynchronously
        rv = NS_ERROR_IN_PROGRESS;
    }
    else {
        // Fall back to synchronous prompt
        PRBool retval = PR_FALSE;
        rv = authPrompt->PromptAuth(this, level, holder, &retval);
        if (NS_FAILED(rv))
            return rv;

        if (!retval)
            rv = NS_ERROR_ABORT;
        else
            holder->SetToHttpAuthIdentity(authFlags, ident);
    }

    // remember that we successfully showed the user an auth dialog
    if (!proxyAuth)
        mSuppressDefensiveAuth = PR_TRUE;

    return rv;
}

NS_IMETHODIMP nsHttpChannel::OnAuthAvailable(nsISupports *aContext,
                                             nsIAuthInformation *aAuthInfo)
{
    LOG(("nsHttpChannel::OnAuthAvailable [this=%p]", this));
    mAsyncPromptAuthCancelable = nsnull;

    nsresult rv;

    const char *host;
    PRInt32 port;
    nsHttpAuthIdentity *ident;
    nsCAutoString path, scheme;
    nsISupports **continuationState;
    rv = GetAuthorizationMembers(mProxyAuth, scheme, host, port, path, ident, continuationState);
    if (NS_FAILED(rv))
        OnAuthCancelled(aContext, PR_FALSE);

    nsCAutoString realm;
    ParseRealm(mCurrentChallenge.get(), realm);

    nsHttpAuthCache *authCache = gHttpHandler->AuthCache();
    nsHttpAuthEntry *entry = nsnull;
    authCache->GetAuthEntryForDomain(scheme.get(), host, port, realm.get(), &entry);

    nsCOMPtr<nsISupports> sessionStateGrip;
    if (entry)
        sessionStateGrip = entry->mMetaData;

    nsAuthInformationHolder* holder =
            static_cast<nsAuthInformationHolder*>(aAuthInfo);
    ident->Set(holder->Domain().get(),
               holder->User().get(),
               holder->Password().get());

    nsCAutoString unused;
    nsCOMPtr<nsIHttpAuthenticator> auth;
    rv = GetAuthenticator(mCurrentChallenge.get(), unused, getter_AddRefs(auth));
    if (NS_FAILED(rv)) {
        NS_ASSERTION(PR_FALSE, "GetAuthenticator failed");
        OnAuthCancelled(aContext, PR_TRUE);
        return NS_OK;
    }

    nsXPIDLCString creds;
    rv = GenCredsAndSetEntry(auth, mProxyAuth,
                             scheme.get(), host, port, path.get(),
                             realm.get(), mCurrentChallenge.get(), *ident, sessionStateGrip,
                             getter_Copies(creds));

    mCurrentChallenge.Truncate();
    if (NS_FAILED(rv)) {
        OnAuthCancelled(aContext, PR_TRUE);
        return NS_OK;
    }

    return ContinueOnAuthAvailable(creds);
}

NS_IMETHODIMP nsHttpChannel::OnAuthCancelled(nsISupports *aContext, 
                                             PRBool userCancel)
{
    LOG(("nsHttpChannel::OnAuthCancelled [this=%p]", this));
    mAsyncPromptAuthCancelable = nsnull;
    if (userCancel) {
        if (!mRemainingChallenges.IsEmpty()) {
            // there are still some challenges to process, do so
            nsresult rv;

            nsCAutoString creds;
            rv = GetCredentials(mRemainingChallenges.get(), mProxyAuth, creds);
            if (NS_SUCCEEDED(rv)) {
                // GetCredentials loaded the credentials from the cache or
                // some other way in a synchronous manner, process those
                // credentials now
                mRemainingChallenges.Truncate();
                return ContinueOnAuthAvailable(creds);
            }
            else if (rv == NS_ERROR_IN_PROGRESS) {
                // GetCredentials successfully queued another authprompt for
                // a challenge from the list, we are now waiting for the user
                // to provide the credentials
                return NS_OK;
            }

            // otherwise, we failed...
        }

        mRemainingChallenges.Truncate();

        // ensure call of OnStartRequest of the current listener here,
        // it would not be called otherwise at all
        nsresult rv = CallOnStartRequest();

        // drop mAuthRetryPending flag and resume the transaction
        // this resumes load of the unauthenticated content data
        mAuthRetryPending = PR_FALSE;
        LOG(("Resuming the transaction, user cancelled the auth dialog"));
        mTransactionPump->Resume();

        if (NS_FAILED(rv))
            mTransactionPump->Cancel(rv);
    }

    return NS_OK;
}

nsresult
nsHttpChannel::ContinueOnAuthAvailable(const nsCSubstring& creds)
{
    if (mProxyAuth)
        mRequestHead.SetHeader(nsHttp::Proxy_Authorization, creds);
    else
        mRequestHead.SetHeader(nsHttp::Authorization, creds);

    // drop our remaining list of challenges.  We don't need them, because we
    // have now authenticated against a challenge and will be sending that
    // information to the server (or proxy).  If it doesn't accept our
    // authentication it'll respond with failure and resend the challenge list
    mRemainingChallenges.Truncate();

    // setting mAuthRetryPending flag and resuming the transaction
    // triggers process of throwing away the unauthenticated data already
    // coming from the network
    mAuthRetryPending = PR_TRUE;
    LOG(("Resuming the transaction, we got credentials from user"));
    mTransactionPump->Resume();

    return NS_OK;
}

PRBool
nsHttpChannel::ConfirmAuth(const nsString &bundleKey, PRBool doYesNoPrompt)
{
    // skip prompting the user if
    //   1) we've already prompted the user
    //   2) we're not a toplevel channel
    //   3) the userpass length is less than the "phishy" threshold

    if (mSuppressDefensiveAuth || !(mLoadFlags & LOAD_INITIAL_DOCUMENT_URI))
        return PR_TRUE;

    nsresult rv;
    nsCAutoString userPass;
    rv = mURI->GetUserPass(userPass);
    if (NS_FAILED(rv) || (userPass.Length() < gHttpHandler->PhishyUserPassLength()))
        return PR_TRUE;

    // we try to confirm by prompting the user.  if we cannot do so, then
    // assume the user said ok.  this is done to keep things working in
    // embedded builds, where the string bundle might not be present, etc.

    nsCOMPtr<nsIStringBundleService> bundleService =
            do_GetService(NS_STRINGBUNDLE_CONTRACTID);
    if (!bundleService)
        return PR_TRUE;

    nsCOMPtr<nsIStringBundle> bundle;
    bundleService->CreateBundle(NECKO_MSGS_URL, getter_AddRefs(bundle));
    if (!bundle)
        return PR_TRUE;

    nsCAutoString host;
    rv = mURI->GetHost(host);
    if (NS_FAILED(rv))
        return PR_TRUE;

    nsCAutoString user;
    rv = mURI->GetUsername(user);
    if (NS_FAILED(rv))
        return PR_TRUE;

    NS_ConvertUTF8toUTF16 ucsHost(host), ucsUser(user);
    const PRUnichar *strs[2] = { ucsHost.get(), ucsUser.get() };

    nsXPIDLString msg;
    bundle->FormatStringFromName(bundleKey.get(), strs, 2, getter_Copies(msg));
    if (!msg)
        return PR_TRUE;
    
    nsCOMPtr<nsIPrompt> prompt;
    GetCallback(prompt);
    if (!prompt)
        return PR_TRUE;

    // do not prompt again
    mSuppressDefensiveAuth = PR_TRUE;

    PRBool confirmed;
    if (doYesNoPrompt) {
        PRInt32 choice;
        PRBool checkState;
        rv = prompt->ConfirmEx(nsnull, msg,
                               nsIPrompt::BUTTON_POS_1_DEFAULT +
                               nsIPrompt::STD_YES_NO_BUTTONS,
                               nsnull, nsnull, nsnull, nsnull, &checkState, &choice);
        if (NS_FAILED(rv))
            return PR_TRUE;

        confirmed = choice == 0;
    }
    else {
        rv = prompt->Confirm(nsnull, msg, &confirmed);
        if (NS_FAILED(rv))
            return PR_TRUE;
    }

    return confirmed;
}

void
nsHttpChannel::CheckForSuperfluousAuth()
{
    // we've been called because it has been determined that this channel is
    // getting loaded without taking the userpass from the URL.  if the URL
    // contained a userpass, then (provided some other conditions are true),
    // we'll give the user an opportunity to abort the channel as this might be
    // an attempt to spoof a different site (see bug 232567).
    if (!mAuthRetryPending) {
        // ask user...
        if (!ConfirmAuth(NS_LITERAL_STRING("SuperfluousAuth"), PR_TRUE)) {
            // calling cancel here sets our mStatus and aborts the HTTP
            // transaction, which prevents OnDataAvailable events.
            Cancel(NS_ERROR_ABORT);
        }
    }
}

void
nsHttpChannel::SetAuthorizationHeader(nsHttpAuthCache *authCache,
                                      nsHttpAtom header,
                                      const char *scheme,
                                      const char *host,
                                      PRInt32 port,
                                      const char *path,
                                      nsHttpAuthIdentity &ident)
{
    nsHttpAuthEntry *entry = nsnull;
    nsresult rv;

    // set informations that depend on whether
    // we're authenticating against a proxy
    // or a webserver
    nsISupports **continuationState;

    if (header == nsHttp::Proxy_Authorization) {
        continuationState = &mProxyAuthContinuationState;
    } else {
        continuationState = &mAuthContinuationState;
    }

    rv = authCache->GetAuthEntryForPath(scheme, host, port, path, &entry);
    if (NS_SUCCEEDED(rv)) {
        // if we are trying to add a header for origin server auth and if the
        // URL contains an explicit username, then try the given username first.
        // we only want to do this, however, if we know the URL requires auth
        // based on the presence of an auth cache entry for this URL (which is
        // true since we are here).  but, if the username from the URL matches
        // the username from the cache, then we should prefer the password
        // stored in the cache since that is most likely to be valid.
        if (header == nsHttp::Authorization && entry->Domain()[0] == '\0') {
            GetIdentityFromURI(0, ident);
            // if the usernames match, then clear the ident so we will pick
            // up the one from the auth cache instead.
            if (nsCRT::strcmp(ident.User(), entry->User()) == 0)
                ident.Clear();
        }
        PRBool identFromURI;
        if (ident.IsEmpty()) {
            ident.Set(entry->Identity());
            identFromURI = PR_FALSE;
        }
        else
            identFromURI = PR_TRUE;

        nsXPIDLCString temp;
        const char *creds     = entry->Creds();
        const char *challenge = entry->Challenge();
        // we can only send a preemptive Authorization header if we have either
        // stored credentials or a stored challenge from which to derive
        // credentials.  if the identity is from the URI, then we cannot use
        // the stored credentials.
        if ((!creds[0] || identFromURI) && challenge[0]) {
            nsCOMPtr<nsIHttpAuthenticator> auth;
            nsCAutoString unused;
            rv = GetAuthenticator(challenge, unused, getter_AddRefs(auth));
            if (NS_SUCCEEDED(rv)) {
                PRBool proxyAuth = (header == nsHttp::Proxy_Authorization);
                rv = GenCredsAndSetEntry(auth, proxyAuth, scheme, host, port, path,
                                         entry->Realm(), challenge, ident,
                                         entry->mMetaData, getter_Copies(temp));
                if (NS_SUCCEEDED(rv))
                    creds = temp.get();

                // make sure the continuation state is null since we do not
                // support mixing preemptive and 'multirequest' authentication.
                NS_IF_RELEASE(*continuationState);
            }
        }
        if (creds[0]) {
            LOG(("   adding \"%s\" request header\n", header.get()));
            mRequestHead.SetHeader(header, nsDependentCString(creds));

            // suppress defensive auth prompting for this channel since we know
            // that we already prompted at least once this session.  we only do
            // this for non-proxy auth since the URL's userpass is not used for
            // proxy auth.
            if (header == nsHttp::Authorization)
                mSuppressDefensiveAuth = PR_TRUE;
        }
        else
            ident.Clear(); // don't remember the identity
    }
}

void
nsHttpChannel::AddAuthorizationHeaders()
{
    LOG(("nsHttpChannel::AddAuthorizationHeaders? [this=%p]\n", this));

    if (mLoadFlags & LOAD_ANONYMOUS) {
      return;
    }

    // this getter never fails
    nsHttpAuthCache *authCache = gHttpHandler->AuthCache();

    // check if proxy credentials should be sent
    const char *proxyHost = mConnectionInfo->ProxyHost();
    if (proxyHost && mConnectionInfo->UsingHttpProxy())
        SetAuthorizationHeader(authCache, nsHttp::Proxy_Authorization,
                               "http", proxyHost, mConnectionInfo->ProxyPort(),
                               nsnull, // proxy has no path
                               mProxyIdent);

    // check if server credentials should be sent
    nsCAutoString path, scheme;
    if (NS_SUCCEEDED(GetCurrentPath(path)) &&
        NS_SUCCEEDED(mURI->GetScheme(scheme))) {
        SetAuthorizationHeader(authCache, nsHttp::Authorization,
                               scheme.get(),
                               mConnectionInfo->Host(),
                               mConnectionInfo->Port(),
                               path.get(),
                               mIdent);
    }
}

nsresult
nsHttpChannel::GetCurrentPath(nsACString &path)
{
    nsresult rv;
    nsCOMPtr<nsIURL> url = do_QueryInterface(mURI);
    if (url)
        rv = url->GetDirectory(path);
    else
        rv = mURI->GetPath(path);
    return rv;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsISupports
//-----------------------------------------------------------------------------

NS_IMPL_ADDREF_INHERITED(nsHttpChannel, nsHashPropertyBag)
NS_IMPL_RELEASE_INHERITED(nsHttpChannel, nsHashPropertyBag)

NS_INTERFACE_MAP_BEGIN(nsHttpChannel)
    NS_INTERFACE_MAP_ENTRY(nsIRequest)
    NS_INTERFACE_MAP_ENTRY(nsIChannel)
    NS_INTERFACE_MAP_ENTRY(nsIRequestObserver)
    NS_INTERFACE_MAP_ENTRY(nsIStreamListener)
    NS_INTERFACE_MAP_ENTRY(nsIHttpChannel)
    NS_INTERFACE_MAP_ENTRY(nsICachingChannel)
    NS_INTERFACE_MAP_ENTRY(nsIUploadChannel)
    NS_INTERFACE_MAP_ENTRY(nsIUploadChannel2)
    NS_INTERFACE_MAP_ENTRY(nsICacheListener)
    NS_INTERFACE_MAP_ENTRY(nsIEncodedChannel)
    NS_INTERFACE_MAP_ENTRY(nsIHttpChannelInternal)
    NS_INTERFACE_MAP_ENTRY(nsIResumableChannel)
    NS_INTERFACE_MAP_ENTRY(nsITransportEventSink)
    NS_INTERFACE_MAP_ENTRY(nsISupportsPriority)
    NS_INTERFACE_MAP_ENTRY(nsIProtocolProxyCallback)
    NS_INTERFACE_MAP_ENTRY(nsIProxiedChannel)
    NS_INTERFACE_MAP_ENTRY(nsITraceableChannel)
    NS_INTERFACE_MAP_ENTRY(nsIApplicationCacheContainer)
    NS_INTERFACE_MAP_ENTRY(nsIApplicationCacheChannel)
    NS_INTERFACE_MAP_ENTRY(nsIAuthPromptCallback)
NS_INTERFACE_MAP_END_INHERITING(nsHashPropertyBag)

//-----------------------------------------------------------------------------
// nsHttpChannel::nsIRequest
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::GetName(nsACString &aName)
{
    aName = mSpec;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::IsPending(PRBool *value)
{
    NS_ENSURE_ARG_POINTER(value);
    *value = mIsPending;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetStatus(nsresult *aStatus)
{
    NS_ENSURE_ARG_POINTER(aStatus);
    *aStatus = mStatus;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::Cancel(nsresult status)
{
    LOG(("nsHttpChannel::Cancel [this=%p status=%x]\n", this, status));
    if (mCanceled) {
        LOG(("  ignoring; already canceled\n"));
        return NS_OK;
    }
    mCanceled = PR_TRUE;
    mStatus = status;
    if (mProxyRequest)
        mProxyRequest->Cancel(status);
    if (mTransaction)
        gHttpHandler->CancelTransaction(mTransaction, status);
    if (mTransactionPump)
        mTransactionPump->Cancel(status);
    if (mCachePump)
        mCachePump->Cancel(status);
    if (mAsyncPromptAuthCancelable)
        mAsyncPromptAuthCancelable->Cancel(status);
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::Suspend()
{
    NS_ENSURE_TRUE(mIsPending, NS_ERROR_NOT_AVAILABLE);
    
    LOG(("nsHttpChannel::Suspend [this=%p]\n", this));

    ++mSuspendCount;

    if (mTransactionPump)
        return mTransactionPump->Suspend();
    if (mCachePump)
        return mCachePump->Suspend();

    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::Resume()
{
    NS_ENSURE_TRUE(mSuspendCount > 0, NS_ERROR_UNEXPECTED);
    
    LOG(("nsHttpChannel::Resume [this=%p]\n", this));
        
    if (--mSuspendCount == 0 && mPendingAsyncCallOnResume) {
        nsresult rv = AsyncCall(mPendingAsyncCallOnResume);
        mPendingAsyncCallOnResume = nsnull;
        NS_ENSURE_SUCCESS(rv, rv);
    }

    if (mTransactionPump)
        return mTransactionPump->Resume();
    if (mCachePump)
        return mCachePump->Resume();

    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetLoadGroup(nsILoadGroup **aLoadGroup)
{
    NS_ENSURE_ARG_POINTER(aLoadGroup);
    *aLoadGroup = mLoadGroup;
    NS_IF_ADDREF(*aLoadGroup);
    return NS_OK;
}
NS_IMETHODIMP
nsHttpChannel::SetLoadGroup(nsILoadGroup *aLoadGroup)
{
    mLoadGroup = aLoadGroup;
    mProgressSink = nsnull;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetLoadFlags(nsLoadFlags *aLoadFlags)
{
    NS_ENSURE_ARG_POINTER(aLoadFlags);
    *aLoadFlags = mLoadFlags;
    return NS_OK;
}
NS_IMETHODIMP
nsHttpChannel::SetLoadFlags(nsLoadFlags aLoadFlags)
{
    mLoadFlags = aLoadFlags;
    return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsIChannel
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::GetOriginalURI(nsIURI **originalURI)
{
    NS_ENSURE_ARG_POINTER(originalURI);
    *originalURI = mOriginalURI;
    NS_ADDREF(*originalURI);
    return NS_OK;
}
NS_IMETHODIMP
nsHttpChannel::SetOriginalURI(nsIURI *originalURI)
{
    NS_ENSURE_ARG_POINTER(originalURI);
    mOriginalURI = originalURI;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetURI(nsIURI **URI)
{
    NS_ENSURE_ARG_POINTER(URI);
    *URI = mURI;
    NS_IF_ADDREF(*URI);
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetOwner(nsISupports **owner)
{
    NS_ENSURE_ARG_POINTER(owner);
    *owner = mOwner;
    NS_IF_ADDREF(*owner);
    return NS_OK;
}
NS_IMETHODIMP
nsHttpChannel::SetOwner(nsISupports *owner)
{
    mOwner = owner;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetNotificationCallbacks(nsIInterfaceRequestor **callbacks)
{
    NS_IF_ADDREF(*callbacks = mCallbacks);
    return NS_OK;
}
NS_IMETHODIMP
nsHttpChannel::SetNotificationCallbacks(nsIInterfaceRequestor *callbacks)
{
    mCallbacks = callbacks;
    mProgressSink = nsnull;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetSecurityInfo(nsISupports **securityInfo)
{
    NS_ENSURE_ARG_POINTER(securityInfo);
    *securityInfo = mSecurityInfo;
    NS_IF_ADDREF(*securityInfo);
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetContentType(nsACString &value)
{
    if (!mResponseHead) {
        // We got no data, we got no headers, we got nothing
        value.Truncate();
        return NS_ERROR_NOT_AVAILABLE;
    }

    if (!mResponseHead->ContentType().IsEmpty()) {
        value = mResponseHead->ContentType();
        return NS_OK;
    }

    
    value.AssignLiteral(UNKNOWN_CONTENT_TYPE);
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetContentType(const nsACString &value)
{
    if (mListener || mWasOpened) {
        if (!mResponseHead)
            return NS_ERROR_NOT_AVAILABLE;

        nsCAutoString contentTypeBuf, charsetBuf;
        PRBool hadCharset;
        net_ParseContentType(value, contentTypeBuf, charsetBuf, &hadCharset);

        mResponseHead->SetContentType(contentTypeBuf);

        // take care not to stomp on an existing charset
        if (hadCharset)
            mResponseHead->SetContentCharset(charsetBuf);
    } else {
        // We are being given a content-type hint.
        PRBool dummy;
        net_ParseContentType(value, mContentTypeHint, mContentCharsetHint,
                             &dummy);
    }
    
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetContentCharset(nsACString &value)
{
    if (!mResponseHead)
        return NS_ERROR_NOT_AVAILABLE;

    value = mResponseHead->ContentCharset();
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetContentCharset(const nsACString &value)
{
    if (mListener) {
        if (!mResponseHead)
            return NS_ERROR_NOT_AVAILABLE;

        mResponseHead->SetContentCharset(value);
    } else {
        // Charset hint
        mContentCharsetHint = value;
    }
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetContentLength(PRInt32 *value)
{
    NS_ENSURE_ARG_POINTER(value);

    if (!mResponseHead)
        return NS_ERROR_NOT_AVAILABLE;

    // XXX truncates to 32 bit
    LL_L2I(*value, mResponseHead->ContentLength());
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetContentLength(PRInt32 value)
{
    NS_NOTYETIMPLEMENTED("nsHttpChannel::SetContentLength");
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsHttpChannel::Open(nsIInputStream **_retval)
{
    NS_ENSURE_TRUE(!mWasOpened, NS_ERROR_IN_PROGRESS);
    return NS_ImplementChannelOpen(this, _retval);
}

NS_IMETHODIMP
nsHttpChannel::AsyncOpen(nsIStreamListener *listener, nsISupports *context)
{
    LOG(("nsHttpChannel::AsyncOpen [this=%p]\n", this));

    NS_ENSURE_ARG_POINTER(listener);
    NS_ENSURE_TRUE(!mIsPending, NS_ERROR_IN_PROGRESS);
    NS_ENSURE_TRUE(!mWasOpened, NS_ERROR_ALREADY_OPENED);

    nsresult rv;

    rv = NS_CheckPortSafety(mURI);
    if (NS_FAILED(rv))
        return rv;

    if (!(mConnectionInfo && mConnectionInfo->UsingHttpProxy())) {
        // Start a DNS lookup very early in case the real open is queued the DNS can 
        // happen in parallel. Do not do so in the presence of an HTTP proxy as 
        // all lookups other than for the proxy itself are done by the proxy.
        nsRefPtr<nsDNSPrefetch> prefetch = new nsDNSPrefetch(mURI);
        if (prefetch) {
            prefetch->PrefetchHigh();
        }
    }
    
    // Remember the cookie header that was set, if any
    const char *cookieHeader = mRequestHead.PeekHeader(nsHttp::Cookie);
    if (cookieHeader)
        mUserSetCookieHeader = cookieHeader;

    // fetch cookies, and add them to the request header
    AddCookiesToRequest();

    // notify "http-on-modify-request" observers
    gHttpHandler->OnModifyRequest(this);

    // Adjust mCaps according to our request headers:
    //  - If "Connection: close" is set as a request header, then do not bother
    //    trying to establish a keep-alive connection.
    if (mRequestHead.HasHeaderValue(nsHttp::Connection, "close"))
        mCaps &= ~(NS_HTTP_ALLOW_KEEPALIVE | NS_HTTP_ALLOW_PIPELINING);
    
    if ((mLoadFlags & VALIDATE_ALWAYS) || 
        (BYPASS_LOCAL_CACHE(mLoadFlags)))
        mCaps |= NS_HTTP_REFRESH_DNS;

    mIsPending = PR_TRUE;
    mWasOpened = PR_TRUE;

    mListener = listener;
    mListenerContext = context;

    // add ourselves to the load group.  from this point forward, we'll report
    // all failures asynchronously.
    if (mLoadGroup)
        mLoadGroup->AddRequest(this, nsnull);

    // We may have been cancelled already, either by on-modify-request
    // listeners or by load group observers; in that case, we should
    // not send the request to the server
    if (mCanceled)
        rv = mStatus;
    else
        rv = Connect();
    if (NS_FAILED(rv)) {
        LOG(("Calling AsyncAbort [rv=%x mCanceled=%i]\n", rv, mCanceled));
        CloseCacheEntry(PR_TRUE);
        AsyncAbort(rv);
    }
    return NS_OK;
}
//-----------------------------------------------------------------------------
// nsHttpChannel::nsIHttpChannel
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::GetRequestMethod(nsACString &method)
{
    method = mRequestHead.Method();
    return NS_OK;
}
NS_IMETHODIMP
nsHttpChannel::SetRequestMethod(const nsACString &method)
{
    NS_ENSURE_TRUE(!mIsPending, NS_ERROR_IN_PROGRESS);

    const nsCString &flatMethod = PromiseFlatCString(method);

    // Method names are restricted to valid HTTP tokens.
    if (!nsHttp::IsValidToken(flatMethod))
        return NS_ERROR_INVALID_ARG;

    nsHttpAtom atom = nsHttp::ResolveAtom(flatMethod.get());
    if (!atom)
        return NS_ERROR_FAILURE;

    mRequestHead.SetMethod(atom);
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetReferrer(nsIURI **referrer)
{
    NS_ENSURE_ARG_POINTER(referrer);
    *referrer = mReferrer;
    NS_IF_ADDREF(*referrer);
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetReferrer(nsIURI *referrer)
{
    NS_ENSURE_TRUE(!mIsPending, NS_ERROR_IN_PROGRESS);

    // clear existing referrer, if any
    mReferrer = nsnull;
    mRequestHead.ClearHeader(nsHttp::Referer);

    if (!referrer)
        return NS_OK;

    // check referrer blocking pref
    PRUint32 referrerLevel;
    if (mLoadFlags & LOAD_INITIAL_DOCUMENT_URI)
        referrerLevel = 1; // user action
    else
        referrerLevel = 2; // inline content
    if (gHttpHandler->ReferrerLevel() < referrerLevel)
        return NS_OK;

    nsCOMPtr<nsIURI> referrerGrip;
    nsresult rv;
    PRBool match;

    //
    // Strip off "wyciwyg://123/" from wyciwyg referrers.
    //
    // XXX this really belongs elsewhere since wyciwyg URLs aren't part of necko.
    //     perhaps some sort of generic nsINestedURI could be used.  then, if an URI
    //     fails the whitelist test, then we could check for an inner URI and try
    //     that instead.  though, that might be too automatic.
    // 
    rv = referrer->SchemeIs("wyciwyg", &match);
    if (NS_FAILED(rv)) return rv;
    if (match) {
        nsCAutoString path;
        rv = referrer->GetPath(path);
        if (NS_FAILED(rv)) return rv;

        PRUint32 pathLength = path.Length();
        if (pathLength <= 2) return NS_ERROR_FAILURE;

        // Path is of the form "//123/http://foo/bar", with a variable number of digits.
        // To figure out where the "real" URL starts, search path for a '/', starting at 
        // the third character.
        PRInt32 slashIndex = path.FindChar('/', 2);
        if (slashIndex == kNotFound) return NS_ERROR_FAILURE;

        // Get the charset of the original URI so we can pass it to our fixed up URI.
        nsCAutoString charset;
        referrer->GetOriginCharset(charset);

        // Replace |referrer| with a URI without wyciwyg://123/.
        rv = NS_NewURI(getter_AddRefs(referrerGrip),
                       Substring(path, slashIndex + 1, pathLength - slashIndex - 1),
                       charset.get());
        if (NS_FAILED(rv)) return rv;

        referrer = referrerGrip.get();
    }

    //
    // block referrer if not on our white list...
    //
    static const char *const referrerWhiteList[] = {
        "http",
        "https",
        "ftp",
        "gopher",
        nsnull
    };
    match = PR_FALSE;
    const char *const *scheme = referrerWhiteList;
    for (; *scheme && !match; ++scheme) {
        rv = referrer->SchemeIs(*scheme, &match);
        if (NS_FAILED(rv)) return rv;
    }
    if (!match)
        return NS_OK; // kick out....

    //
    // Handle secure referrals.
    //
    // Support referrals from a secure server if this is a secure site
    // and (optionally) if the host names are the same.
    //
    rv = referrer->SchemeIs("https", &match);
    if (NS_FAILED(rv)) return rv;
    if (match) {
        rv = mURI->SchemeIs("https", &match);
        if (NS_FAILED(rv)) return rv;
        if (!match)
            return NS_OK;

        if (!gHttpHandler->SendSecureXSiteReferrer()) {
            nsCAutoString referrerHost;
            nsCAutoString host;

            rv = referrer->GetAsciiHost(referrerHost);
            if (NS_FAILED(rv)) return rv;

            rv = mURI->GetAsciiHost(host);
            if (NS_FAILED(rv)) return rv;

            // GetAsciiHost returns lowercase hostname.
            if (!referrerHost.Equals(host))
                return NS_OK;
        }
    }

    nsCOMPtr<nsIURI> clone;
    //
    // we need to clone the referrer, so we can:
    //  (1) modify it
    //  (2) keep a reference to it after returning from this function
    //
    rv = referrer->Clone(getter_AddRefs(clone));
    if (NS_FAILED(rv)) return rv;

    // strip away any userpass; we don't want to be giving out passwords ;-)
    clone->SetUserPass(EmptyCString());

    // strip away any fragment per RFC 2616 section 14.36
    nsCOMPtr<nsIURL> url = do_QueryInterface(clone);
    if (url)
        url->SetRef(EmptyCString());

    nsCAutoString spec;
    rv = clone->GetAsciiSpec(spec);
    if (NS_FAILED(rv)) return rv;

    // finally, remember the referrer URI and set the Referer header.
    mReferrer = clone;
    mRequestHead.SetHeader(nsHttp::Referer, spec);
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetRequestHeader(const nsACString &header, nsACString &value)
{
    // XXX might be better to search the header list directly instead of
    // hitting the http atom hash table.

    nsHttpAtom atom = nsHttp::ResolveAtom(header);
    if (!atom)
        return NS_ERROR_NOT_AVAILABLE;

    return mRequestHead.GetHeader(atom, value);
}

NS_IMETHODIMP
nsHttpChannel::SetRequestHeader(const nsACString &header,
                                const nsACString &value,
                                PRBool merge)
{
    NS_ENSURE_TRUE(!mIsPending, NS_ERROR_IN_PROGRESS);

    const nsCString &flatHeader = PromiseFlatCString(header);
    const nsCString &flatValue  = PromiseFlatCString(value);

    LOG(("nsHttpChannel::SetRequestHeader [this=%p header=\"%s\" value=\"%s\" merge=%u]\n",
        this, flatHeader.get(), flatValue.get(), merge));

    // Header names are restricted to valid HTTP tokens.
    if (!nsHttp::IsValidToken(flatHeader))
        return NS_ERROR_INVALID_ARG;
    
    // Header values MUST NOT contain line-breaks.  RFC 2616 technically
    // permits CTL characters, including CR and LF, in header values provided
    // they are quoted.  However, this can lead to problems if servers do not
    // interpret quoted strings properly.  Disallowing CR and LF here seems
    // reasonable and keeps things simple.  We also disallow a null byte.
    if (flatValue.FindCharInSet("\r\n") != kNotFound ||
        flatValue.Length() != strlen(flatValue.get()))
        return NS_ERROR_INVALID_ARG;

    nsHttpAtom atom = nsHttp::ResolveAtom(flatHeader.get());
    if (!atom) {
        NS_WARNING("failed to resolve atom");
        return NS_ERROR_NOT_AVAILABLE;
    }

    return mRequestHead.SetHeader(atom, flatValue, merge);
}

NS_IMETHODIMP
nsHttpChannel::VisitRequestHeaders(nsIHttpHeaderVisitor *visitor)
{
    return mRequestHead.Headers().VisitHeaders(visitor);
}

NS_IMETHODIMP
nsHttpChannel::GetUploadStream(nsIInputStream **stream)
{
    NS_ENSURE_ARG_POINTER(stream);
    *stream = mUploadStream;
    NS_IF_ADDREF(*stream);
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetUploadStream(nsIInputStream *stream,
                               const nsACString &contentType,
                               PRInt32 contentLength)
{
    // NOTE: for backwards compatibility and for compatibility with old style
    // plugins, |stream| may include headers, specifically Content-Type and
    // Content-Length headers.  in this case, |contentType| and |contentLength|
    // would be unspecified.  this is traditionally the case of a POST request,
    // and so we select POST as the request method if contentType and
    // contentLength are unspecified.
    
    if (stream) {
        if (!contentType.IsEmpty()) {
            if (contentLength < 0) {
                stream->Available((PRUint32 *) &contentLength);
                if (contentLength < 0) {
                    NS_ERROR("unable to determine content length");
                    return NS_ERROR_FAILURE;
                }
            }
            mRequestHead.SetHeader(nsHttp::Content_Length,
                                   nsPrintfCString("%d", contentLength));
            mRequestHead.SetHeader(nsHttp::Content_Type, contentType);
            mUploadStreamHasHeaders = PR_FALSE;
            mRequestHead.SetMethod(nsHttp::Put); // PUT request
        }
        else {
            mUploadStreamHasHeaders = PR_TRUE;
            mRequestHead.SetMethod(nsHttp::Post); // POST request
        }
    }
    else {
        mUploadStreamHasHeaders = PR_FALSE;
        mRequestHead.SetMethod(nsHttp::Get); // revert to GET request
    }
    mUploadStream = stream;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::ExplicitSetUploadStream(nsIInputStream *aStream,
                                       const nsACString &aContentType,
                                       PRInt64 aContentLength,
                                       const nsACString &aMethod,
                                       PRBool aStreamHasHeaders)
{
    // Ensure stream is set and method is valid 
    NS_ENSURE_TRUE(aStream, NS_ERROR_FAILURE);

    if (aContentLength < 0 && !aStreamHasHeaders) {
        PRUint32 streamLength;
        aStream->Available(&streamLength);
        aContentLength = streamLength;
        if (aContentLength < 0) {
            NS_ERROR("unable to determine content length");
            return NS_ERROR_FAILURE;
        }
    }

    nsresult rv = SetRequestMethod(aMethod);
    NS_ENSURE_SUCCESS(rv, rv);

    if (!aStreamHasHeaders) {
        mRequestHead.SetHeader(nsHttp::Content_Length,
                               nsPrintfCString("%lld", aContentLength));
        mRequestHead.SetHeader(nsHttp::Content_Type, aContentType);
    }

    mUploadStreamHasHeaders = aStreamHasHeaders;
    mUploadStream = aStream;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetResponseStatus(PRUint32 *value)
{
    NS_ENSURE_ARG_POINTER(value);
    if (!mResponseHead)
        return NS_ERROR_NOT_AVAILABLE;
    *value = mResponseHead->Status();
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetResponseStatusText(nsACString &value)
{
    if (!mResponseHead)
        return NS_ERROR_NOT_AVAILABLE;
    value = mResponseHead->StatusText();
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetRequestSucceeded(PRBool *value)
{
    NS_PRECONDITION(value, "Don't ever pass a null arg to this function");
    if (!mResponseHead)
        return NS_ERROR_NOT_AVAILABLE;
    PRUint32 status = mResponseHead->Status();
    *value = (status / 100 == 2);
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetResponseHeader(const nsACString &header, nsACString &value)
{
    if (!mResponseHead)
        return NS_ERROR_NOT_AVAILABLE;
    nsHttpAtom atom = nsHttp::ResolveAtom(header);
    if (!atom)
        return NS_ERROR_NOT_AVAILABLE;
    return mResponseHead->GetHeader(atom, value);
}

NS_IMETHODIMP
nsHttpChannel::SetResponseHeader(const nsACString &header,
                                 const nsACString &value,
                                 PRBool merge)
{
    LOG(("nsHttpChannel::SetResponseHeader [this=%p header=\"%s\" value=\"%s\" merge=%u]\n",
        this, PromiseFlatCString(header).get(), PromiseFlatCString(value).get(), merge));

    if (!mResponseHead)
        return NS_ERROR_NOT_AVAILABLE;
    nsHttpAtom atom = nsHttp::ResolveAtom(header);
    if (!atom)
        return NS_ERROR_NOT_AVAILABLE;

    // these response headers must not be changed 
    if (atom == nsHttp::Content_Type ||
        atom == nsHttp::Content_Length ||
        atom == nsHttp::Content_Encoding ||
        atom == nsHttp::Trailer ||
        atom == nsHttp::Transfer_Encoding)
        return NS_ERROR_ILLEGAL_VALUE;

    mResponseHeadersModified = PR_TRUE;

    return mResponseHead->SetHeader(atom, value, merge);
}

NS_IMETHODIMP
nsHttpChannel::VisitResponseHeaders(nsIHttpHeaderVisitor *visitor)
{
    if (!mResponseHead)
        return NS_ERROR_NOT_AVAILABLE;
    return mResponseHead->Headers().VisitHeaders(visitor);
}

NS_IMETHODIMP
nsHttpChannel::IsNoStoreResponse(PRBool *value)
{
    if (!mResponseHead)
        return NS_ERROR_NOT_AVAILABLE;
    *value = mResponseHead->NoStore();
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::IsNoCacheResponse(PRBool *value)
{
    if (!mResponseHead)
        return NS_ERROR_NOT_AVAILABLE;
    *value = mResponseHead->NoCache();
    if (!*value)
        *value = mResponseHead->ExpiresInPast();
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetApplyConversion(PRBool *value)
{
    NS_ENSURE_ARG_POINTER(value);
    *value = mApplyConversion;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetApplyConversion(PRBool value)
{
    LOG(("nsHttpChannel::SetApplyConversion [this=%p value=%d]\n", this, value));
    mApplyConversion = value;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetAllowPipelining(PRBool *value)
{
    NS_ENSURE_ARG_POINTER(value);
    *value = mAllowPipelining;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetAllowPipelining(PRBool value)
{
    if (mIsPending)
        return NS_ERROR_FAILURE;
    mAllowPipelining = value;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetRedirectionLimit(PRUint32 *value)
{
    NS_ENSURE_ARG_POINTER(value);
    *value = PRUint32(mRedirectionLimit);
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetRedirectionLimit(PRUint32 value)
{
    mRedirectionLimit = PR_MIN(value, 0xff);
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetContentEncodings(nsIUTF8StringEnumerator** aEncodings)
{
    NS_PRECONDITION(aEncodings, "Null out param");
    if (!mResponseHead) {
        *aEncodings = nsnull;
        return NS_OK;
    }
    
    const char *encoding = mResponseHead->PeekHeader(nsHttp::Content_Encoding);
    if (!encoding) {
        *aEncodings = nsnull;
        return NS_OK;
    }
    nsContentEncodings* enumerator = new nsContentEncodings(this, encoding);
    if (!enumerator)
        return NS_ERROR_OUT_OF_MEMORY;

    NS_ADDREF(*aEncodings = enumerator);
    return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsIHttpChannelInternal
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::GetDocumentURI(nsIURI **aDocumentURI)
{
    NS_ENSURE_ARG_POINTER(aDocumentURI);
    *aDocumentURI = mDocumentURI;
    NS_IF_ADDREF(*aDocumentURI);
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetDocumentURI(nsIURI *aDocumentURI)
{
    mDocumentURI = aDocumentURI;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetForceAllowThirdPartyCookie(PRBool *aForceAllowThirdPartyCookie)
{
    *aForceAllowThirdPartyCookie = mForceAllowThirdPartyCookie;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetForceAllowThirdPartyCookie(PRBool aForceAllowThirdPartyCookie)
{
    mForceAllowThirdPartyCookie = aForceAllowThirdPartyCookie;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetRequestVersion(PRUint32 *major, PRUint32 *minor)
{
  int version = mRequestHead.Version();

  if (major) { *major = version / 10; }
  if (minor) { *minor = version % 10; }

  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetResponseVersion(PRUint32 *major, PRUint32 *minor)
{
  if (!mResponseHead)
  {
    *major = *minor = 0;                   // we should at least be kind about it
    return NS_ERROR_NOT_AVAILABLE;
  }

  int version = mResponseHead->Version();

  if (major) { *major = version / 10; }
  if (minor) { *minor = version % 10; }

  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetCookie(const char *aCookieHeader)
{
    if (mLoadFlags & LOAD_ANONYMOUS) {
      return NS_OK;
    }

    // empty header isn't an error
    if (!(aCookieHeader && *aCookieHeader))
        return NS_OK;

    nsICookieService *cs = gHttpHandler->GetCookieService();
    NS_ENSURE_TRUE(cs, NS_ERROR_FAILURE);

    nsCOMPtr<nsIPrompt> prompt;
    GetCallback(prompt);

    return cs->SetCookieStringFromHttp(mURI,
                                       mDocumentURI ? mDocumentURI : mOriginalURI,
                                       prompt,
                                       aCookieHeader,
                                       mResponseHead->PeekHeader(nsHttp::Date),
                                       this);
}

NS_IMETHODIMP
nsHttpChannel::SetupFallbackChannel(const char *aFallbackKey)
{
    LOG(("nsHttpChannel::SetupFallbackChannel [this=%p, key=%s]",
         this, aFallbackKey));
    mFallbackChannel = PR_TRUE;
    mFallbackKey = aFallbackKey;

    return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsISupportsPriority
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::GetPriority(PRInt32 *value)
{
    *value = mPriority;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetPriority(PRInt32 value)
{
    PRInt16 newValue = CLAMP(value, PR_INT16_MIN, PR_INT16_MAX);
    if (mPriority == newValue)
        return NS_OK;
    mPriority = newValue;
    if (mTransaction)
        gHttpHandler->RescheduleTransaction(mTransaction, mPriority);
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::AdjustPriority(PRInt32 delta)
{
    return SetPriority(mPriority + delta);
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsIProtocolProxyCallback
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::OnProxyAvailable(nsICancelable *request, nsIURI *uri,
                                nsIProxyInfo *pi, nsresult status)
{
    mProxyRequest = nsnull;

    // If status is a failure code, then it means that we failed to resolve
    // proxy info.  That is a non-fatal error assuming it wasn't because the
    // request was canceled.  We just failover to DIRECT when proxy resolution
    // fails (failure can mean that the PAC URL could not be loaded).
    
    // Need to replace this channel with a new one.  It would be complex to try
    // to change the value of mConnectionInfo since so much of our state may
    // depend on its state.
    mTargetProxyInfo = pi;
    HandleAsyncReplaceWithProxy();
    return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsIProxiedChannel
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::GetProxyInfo(nsIProxyInfo **result)
{
    if (!mConnectionInfo)
        *result = nsnull;
    else {
        *result = mConnectionInfo->ProxyInfo();
        NS_IF_ADDREF(*result);
    }
    return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsIRequestObserver
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::OnStartRequest(nsIRequest *request, nsISupports *ctxt)
{
    if (!(mCanceled || NS_FAILED(mStatus))) {
        // capture the request's status, so our consumers will know ASAP of any
        // connection failures, etc - bug 93581
        request->GetStatus(&mStatus);
    }

    LOG(("nsHttpChannel::OnStartRequest [this=%p request=%p status=%x]\n",
        this, request, mStatus));

    // Make sure things are what we expect them to be...
    NS_ASSERTION(request == mCachePump || request == mTransactionPump,
                 "Unexpected request");
    NS_ASSERTION(!(mTransactionPump && mCachePump) || mCachedContentIsPartial,
                 "If we have both pumps, the cache content must be partial");

    if (!mSecurityInfo && !mCachePump && mTransaction) {
        // grab the security info from the connection object; the transaction
        // is guaranteed to own a reference to the connection.
        mSecurityInfo = mTransaction->SecurityInfo();
    }

    // don't enter this block if we're reading from the cache...
    if (NS_SUCCEEDED(mStatus) && !mCachePump && mTransaction) {
        NS_ASSERTION(mResponseHead == nsnull, "leaking mResponseHead");

        // all of the response headers have been acquired, so we can take ownership
        // of them from the transaction.
        mResponseHead = mTransaction->TakeResponseHead();
        // the response head may be null if the transaction was cancelled.  in
        // which case we just need to call OnStartRequest/OnStopRequest.
        if (mResponseHead)
            return ProcessResponse();

        NS_WARNING("No response head in OnStartRequest");
    }

    // avoid crashing if mListener happens to be null...
    if (!mListener) {
        NS_NOTREACHED("mListener is null");
        return NS_OK;
    }

    // on proxy errors, try to failover
    if (mConnectionInfo->ProxyInfo() &&
           (mStatus == NS_ERROR_PROXY_CONNECTION_REFUSED ||
            mStatus == NS_ERROR_UNKNOWN_PROXY_HOST ||
            mStatus == NS_ERROR_NET_TIMEOUT)) {
        if (NS_SUCCEEDED(ProxyFailover()))
            return NS_OK;
    }

    // on other request errors, try to fall back
    PRBool fallingBack;
    if (NS_FAILED(mStatus) &&
        NS_SUCCEEDED(ProcessFallback(&fallingBack)) &&
        fallingBack) {

        return NS_OK;
    }

    return CallOnStartRequest();
}

NS_IMETHODIMP
nsHttpChannel::OnStopRequest(nsIRequest *request, nsISupports *ctxt, nsresult status)
{
    LOG(("nsHttpChannel::OnStopRequest [this=%p request=%p status=%x]\n",
        this, request, status));

    // honor the cancelation status even if the underlying transaction completed.
    if (mCanceled || NS_FAILED(mStatus))
        status = mStatus;

    if (mCachedContentIsPartial) {
        if (NS_SUCCEEDED(status)) {
            // mTransactionPump should be suspended
            NS_ASSERTION(request != mTransactionPump,
                "byte-range transaction finished prematurely");

            if (request == mCachePump) {
                PRBool streamDone;
                status = OnDoneReadingPartialCacheEntry(&streamDone);
                if (NS_SUCCEEDED(status) && !streamDone)
                    return status;
                // otherwise, fall through and fire OnStopRequest...
            }
            else
                NS_NOTREACHED("unexpected request");
        }
        // Do not to leave the transaction in a suspended state in error cases.
        if (NS_FAILED(status) && mTransaction)
            gHttpHandler->CancelTransaction(mTransaction, status); 
    }

    if (mTransaction) {
        // determine if we should call DoAuthRetry
        PRBool authRetry = mAuthRetryPending && NS_SUCCEEDED(status);

        //
        // grab reference to connection in case we need to retry an
        // authentication request over it.  this applies to connection based
        // authentication schemes only.  for request based schemes, conn is not
        // needed, so it may be null.
        // 
        // this code relies on the code in nsHttpTransaction::Close, which
        // tests for NS_HTTP_STICKY_CONNECTION to determine whether or not to
        // keep the connection around after the transaction is finished.
        //
        nsRefPtr<nsAHttpConnection> conn;
        if (authRetry && (mCaps & NS_HTTP_STICKY_CONNECTION)) {
            conn = mTransaction->Connection();
            // This is so far a workaround to fix leak when reusing unpersistent
            // connection for authentication retry. See bug 459620 comment 4
            // for details.
            if (conn && !conn->IsPersistent())
                conn = nsnull;
        }

        // at this point, we're done with the transaction
        NS_RELEASE(mTransaction);
        mTransactionPump = 0;

        // handle auth retry...
        if (authRetry) {
            mAuthRetryPending = PR_FALSE;
            status = DoAuthRetry(conn);
            if (NS_SUCCEEDED(status))
                return NS_OK;
        }

        // If DoAuthRetry failed, or if we have been cancelled since showing
        // the auth. dialog, then we need to send OnStartRequest now
        if (authRetry || (mAuthRetryPending && NS_FAILED(status))) {
            NS_ASSERTION(NS_FAILED(status), "should have a failure code here");
            // NOTE: since we have a failure status, we can ignore the return
            // value from onStartRequest.
            mListener->OnStartRequest(this, mListenerContext);
        }

        // if this transaction has been replaced, then bail.
        if (mTransactionReplaced)
            return NS_OK;
    }

    mIsPending = PR_FALSE;
    mStatus = status;

    // perform any final cache operations before we close the cache entry.
    if (mCacheEntry && (mCacheAccess & nsICache::ACCESS_WRITE))
        FinalizeCacheEntry();
    
    if (mListener) {
        LOG(("  calling OnStopRequest\n"));
        mListener->OnStopRequest(this, mListenerContext, status);
        mListener = 0;
        mListenerContext = 0;
    }

    if (mCacheEntry)
        CloseCacheEntry(PR_TRUE);

    if (mOfflineCacheEntry)
        CloseOfflineCacheEntry();

    if (mLoadGroup)
        mLoadGroup->RemoveRequest(this, nsnull, status);

    mCallbacks = nsnull;
    mProgressSink = nsnull;
    
    return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsIStreamListener
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::OnDataAvailable(nsIRequest *request, nsISupports *ctxt,
                               nsIInputStream *input,
                               PRUint32 offset, PRUint32 count)
{
    LOG(("nsHttpChannel::OnDataAvailable [this=%p request=%p offset=%u count=%u]\n",
        this, request, offset, count));

    // don't send out OnDataAvailable notifications if we've been canceled.
    if (mCanceled)
        return mStatus;

    NS_ASSERTION(mResponseHead, "No response head in ODA!!");

    NS_ASSERTION(!(mCachedContentIsPartial && (request == mTransactionPump)),
            "transaction pump not suspended");

    if (mAuthRetryPending || (request == mTransactionPump && mTransactionReplaced)) {
        PRUint32 n;
        return input->ReadSegments(NS_DiscardSegment, nsnull, count, &n);
    }

    if (mListener) {
        //
        // synthesize transport progress event.  we do this here since we want
        // to delay OnProgress events until we start streaming data.  this is
        // crucially important since it impacts the lock icon (see bug 240053).
        //
        nsresult transportStatus;
        if (request == mCachePump)
            transportStatus = nsITransport::STATUS_READING;
        else
            transportStatus = nsISocketTransport::STATUS_RECEIVING_FROM;

        // mResponseHead may reference new or cached headers, but either way it
        // holds our best estimate of the total content length.  Even in the case
        // of a byte range request, the content length stored in the cached
        // response headers is what we want to use here.

        PRUint64 progressMax(PRUint64(mResponseHead->ContentLength()));
        PRUint64 progress = mLogicalOffset + PRUint64(count);
        NS_ASSERTION(progress <= progressMax, "unexpected progress values");

        OnTransportStatus(nsnull, transportStatus, progress, progressMax);

        //
        // we have to manually keep the logical offset of the stream up-to-date.
        // we cannot depend solely on the offset provided, since we may have 
        // already streamed some data from another source (see, for example,
        // OnDoneReadingPartialCacheEntry).
        //
        nsresult rv =  mListener->OnDataAvailable(this,
                                                  mListenerContext,
                                                  input,
                                                  mLogicalOffset,
                                                  count);
        if (NS_SUCCEEDED(rv))
            mLogicalOffset = progress;
        return rv;
    }

    return NS_ERROR_ABORT;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsITransportEventSink
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::OnTransportStatus(nsITransport *trans, nsresult status,
                                 PRUint64 progress, PRUint64 progressMax)
{
    // cache the progress sink so we don't have to query for it each time.
    if (!mProgressSink)
        GetCallback(mProgressSink);

    // block socket status event after Cancel or OnStopRequest has been called.
    if (mProgressSink && NS_SUCCEEDED(mStatus) && mIsPending && !(mLoadFlags & LOAD_BACKGROUND)) {
        LOG(("sending status notification [this=%p status=%x progress=%llu/%llu]\n",
            this, status, progress, progressMax));

        nsCAutoString host;
        mURI->GetHost(host);
        mProgressSink->OnStatus(this, nsnull, status,
                                NS_ConvertUTF8toUTF16(host).get());

        if (progress > 0) {
            NS_ASSERTION(progress <= progressMax, "unexpected progress values");
            mProgressSink->OnProgress(this, nsnull, progress, progressMax);
        }
    }
#ifdef DEBUG
    else
        LOG(("skipping status notification [this=%p sink=%p pending=%u background=%x]\n",
            this, mProgressSink.get(), mIsPending, (mLoadFlags & LOAD_BACKGROUND)));
#endif

    return NS_OK;
} 

//-----------------------------------------------------------------------------
// nsHttpChannel::nsICachingChannel
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::GetCacheToken(nsISupports **token)
{
    NS_ENSURE_ARG_POINTER(token);
    if (!mCacheEntry)
        return NS_ERROR_NOT_AVAILABLE;
    return CallQueryInterface(mCacheEntry, token);
}

NS_IMETHODIMP
nsHttpChannel::SetCacheToken(nsISupports *token)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsHttpChannel::GetOfflineCacheToken(nsISupports **token)
{
    NS_ENSURE_ARG_POINTER(token);
    if (!mOfflineCacheEntry)
        return NS_ERROR_NOT_AVAILABLE;
    return CallQueryInterface(mOfflineCacheEntry, token);
}

NS_IMETHODIMP
nsHttpChannel::SetOfflineCacheToken(nsISupports *token)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsHttpChannel::GetCacheKey(nsISupports **key)
{
    nsresult rv;
    NS_ENSURE_ARG_POINTER(key);

    LOG(("nsHttpChannel::GetCacheKey [this=%p]\n", this));

    *key = nsnull;

    nsCOMPtr<nsISupportsPRUint32> container =
        do_CreateInstance(NS_SUPPORTS_PRUINT32_CONTRACTID, &rv);
    if (NS_FAILED(rv)) return rv;

    rv = container->SetData(mPostID);
    if (NS_FAILED(rv)) return rv;

    return CallQueryInterface(container, key);
}

NS_IMETHODIMP
nsHttpChannel::SetCacheKey(nsISupports *key)
{
    nsresult rv;

    LOG(("nsHttpChannel::SetCacheKey [this=%p key=%p]\n", this, key));

    // can only set the cache key if a load is not in progress
    NS_ENSURE_TRUE(!mIsPending, NS_ERROR_IN_PROGRESS);

    if (!key)
        mPostID = 0;
    else {
        // extract the post id
        nsCOMPtr<nsISupportsPRUint32> container = do_QueryInterface(key, &rv);
        if (NS_FAILED(rv)) return rv;

        rv = container->GetData(&mPostID);
        if (NS_FAILED(rv)) return rv;
    }
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetCacheAsFile(PRBool *value)
{
    NS_ENSURE_ARG_POINTER(value);
    if (!mCacheEntry)
        return NS_ERROR_NOT_AVAILABLE;
    nsCacheStoragePolicy storagePolicy;
    mCacheEntry->GetStoragePolicy(&storagePolicy);
    *value = (storagePolicy == nsICache::STORE_ON_DISK_AS_FILE);
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetCacheAsFile(PRBool value)
{
    if (!mCacheEntry || mLoadFlags & INHIBIT_PERSISTENT_CACHING)
        return NS_ERROR_NOT_AVAILABLE;
    nsCacheStoragePolicy policy;
    if (value)
        policy = nsICache::STORE_ON_DISK_AS_FILE;
    else
        policy = nsICache::STORE_ANYWHERE;
    return mCacheEntry->SetStoragePolicy(policy);
}


NS_IMETHODIMP
nsHttpChannel::GetCacheForOfflineUse(PRBool *value)
{
    *value = mCacheForOfflineUse;

    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetCacheForOfflineUse(PRBool value)
{
    mCacheForOfflineUse = value;

    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetOfflineCacheClientID(nsACString &value)
{
    value = mOfflineCacheClientID;

    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetOfflineCacheClientID(const nsACString &value)
{
    mOfflineCacheClientID = value;

    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetCacheFile(nsIFile **cacheFile)
{
    if (!mCacheEntry)
        return NS_ERROR_NOT_AVAILABLE;
    return mCacheEntry->GetFile(cacheFile);
}

NS_IMETHODIMP
nsHttpChannel::IsFromCache(PRBool *value)
{
    if (!mIsPending)
        return NS_ERROR_NOT_AVAILABLE;

    // return false if reading a partial cache entry; the data isn't entirely
    // from the cache!

    *value = (mCachePump || (mLoadFlags & LOAD_ONLY_IF_MODIFIED)) &&
              mCachedContentIsValid && !mCachedContentIsPartial;

    return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsIResumableChannel
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::ResumeAt(PRUint64 aStartPos,
                        const nsACString& aEntityID)
{
    LOG(("nsHttpChannel::ResumeAt [this=%p startPos=%llu id='%s']\n",
         this, aStartPos, PromiseFlatCString(aEntityID).get()));
    mEntityID = aEntityID;
    mStartPos = aStartPos;
    mResuming = PR_TRUE;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetEntityID(nsACString& aEntityID)
{
    // Don't return an entity ID for Non-GET requests which require
    // additional data
    if (mRequestHead.Method() != nsHttp::Get) {
        return NS_ERROR_NOT_RESUMABLE;
    }

    // Don't return an entity if the server sent the following header:
    // Accept-Ranges: none
    // Not sending the Accept-Ranges header means we can still try
    // sending range requests.
    const char* acceptRanges =
        mResponseHead->PeekHeader(nsHttp::Accept_Ranges);
    if (acceptRanges &&
        !nsHttp::FindToken(acceptRanges, "bytes", HTTP_HEADER_VALUE_SEPS)) {
        return NS_ERROR_NOT_RESUMABLE;
    }

    PRUint64 size = LL_MAXUINT;
    nsCAutoString etag, lastmod;
    if (mResponseHead) {
        size = mResponseHead->TotalEntitySize();
        const char* cLastMod = mResponseHead->PeekHeader(nsHttp::Last_Modified);
        if (cLastMod)
            lastmod = cLastMod;
        const char* cEtag = mResponseHead->PeekHeader(nsHttp::ETag);
        if (cEtag)
            etag = cEtag;
    }
    nsCString entityID;
    NS_EscapeURL(etag.BeginReading(), etag.Length(), esc_AlwaysCopy |
            esc_FileBaseName | esc_Forced, entityID);
    entityID.Append('/');
    entityID.AppendInt(PRInt64(size));
    entityID.Append('/');
    entityID.Append(lastmod);
    // NOTE: Appending lastmod as the last part avoids having to escape it

    aEntityID = entityID;

    return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsICacheListener
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::OnCacheEntryAvailable(nsICacheEntryDescriptor *entry,
                                     nsCacheAccessMode access,
                                     nsresult status)
{
    LOG(("nsHttpChannel::OnCacheEntryAvailable [this=%p entry=%p "
         "access=%x status=%x]\n", this, entry, access, status));

    // if the channel's already fired onStopRequest, then we should ignore
    // this event.
    if (!mIsPending)
        return NS_OK;

    // otherwise, we have to handle this event.
    if (NS_SUCCEEDED(status)) {
        mCacheEntry = entry;
        mCacheAccess = access;
    }

    nsresult rv;

    if (mCanceled && NS_FAILED(mStatus)) {
        LOG(("channel was canceled [this=%p status=%x]\n", this, mStatus));
        rv = mStatus;
    }
    else if ((mLoadFlags & LOAD_ONLY_FROM_CACHE) && NS_FAILED(status))
        // if this channel is only allowed to pull from the cache, then
        // we must fail if we were unable to open a cache entry.
        rv = NS_ERROR_DOCUMENT_NOT_CACHED;
    else
        // advance to the next state...
        rv = Connect(PR_FALSE);

    // a failure from Connect means that we have to abort the channel.
    if (NS_FAILED(rv)) {
        CloseCacheEntry(PR_TRUE);
        AsyncAbort(rv);
    }

    return NS_OK;
}

nsresult
nsHttpChannel::DoAuthRetry(nsAHttpConnection *conn)
{
    LOG(("nsHttpChannel::DoAuthRetry [this=%p]\n", this));

    NS_ASSERTION(!mTransaction, "should not have a transaction");
    nsresult rv;

    // toggle mIsPending to allow nsIObserver implementations to modify
    // the request headers (bug 95044).
    mIsPending = PR_FALSE;

    // fetch cookies, and add them to the request header.
    // the server response could have included cookies that must be sent with
    // this authentication attempt (bug 84794).
    AddCookiesToRequest();

    // notify "http-on-modify-request" observers
    gHttpHandler->OnModifyRequest(this);

    mIsPending = PR_TRUE;

    // get rid of the old response headers
    delete mResponseHead;
    mResponseHead = nsnull;

    // set sticky connection flag and disable pipelining.
    mCaps |=  NS_HTTP_STICKY_CONNECTION;
    mCaps &= ~NS_HTTP_ALLOW_PIPELINING;
   
    // and create a new one...
    rv = SetupTransaction();
    if (NS_FAILED(rv)) return rv;

    // transfer ownership of connection to transaction
    if (conn)
        mTransaction->SetConnection(conn);

    // rewind the upload stream
    if (mUploadStream) {
        nsCOMPtr<nsISeekableStream> seekable = do_QueryInterface(mUploadStream);
        if (seekable)
            seekable->Seek(nsISeekableStream::NS_SEEK_SET, 0);
    }

    rv = gHttpHandler->InitiateTransaction(mTransaction, mPriority);
    if (NS_FAILED(rv)) return rv;

    return mTransactionPump->AsyncRead(this, nsnull);
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsIApplicationCacheChannel
//-----------------------------------------------------------------------------
NS_IMETHODIMP
nsHttpChannel::GetApplicationCache(nsIApplicationCache **out)
{
    NS_IF_ADDREF(*out = mApplicationCache);
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetApplicationCache(nsIApplicationCache *appCache)
{
    NS_ENSURE_TRUE(!mWasOpened, NS_ERROR_ALREADY_OPENED);

    mApplicationCache = appCache;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetLoadedFromApplicationCache(PRBool *aLoadedFromApplicationCache)
{
    *aLoadedFromApplicationCache = mLoadedFromApplicationCache;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetInheritApplicationCache(PRBool *aInherit)
{
    *aInherit = mInheritApplicationCache;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetInheritApplicationCache(PRBool aInherit)
{
    NS_ENSURE_TRUE(!mWasOpened, NS_ERROR_ALREADY_OPENED);

    mInheritApplicationCache = aInherit;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetChooseApplicationCache(PRBool *aChoose)
{
    *aChoose = mChooseApplicationCache;
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetChooseApplicationCache(PRBool aChoose)
{
    NS_ENSURE_TRUE(!mWasOpened, NS_ERROR_ALREADY_OPENED);

    mChooseApplicationCache = aChoose;
    return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsContentEncodings <public>
//-----------------------------------------------------------------------------

nsHttpChannel::nsContentEncodings::nsContentEncodings(nsIHttpChannel* aChannel,
                                                          const char* aEncodingHeader) :
    mEncodingHeader(aEncodingHeader), mChannel(aChannel), mReady(PR_FALSE)
{
    mCurEnd = aEncodingHeader + strlen(aEncodingHeader);
    mCurStart = mCurEnd;
}
    
nsHttpChannel::nsContentEncodings::~nsContentEncodings()
{
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsContentEncodings::nsISimpleEnumerator
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::nsContentEncodings::HasMore(PRBool* aMoreEncodings)
{
    if (mReady) {
        *aMoreEncodings = PR_TRUE;
        return NS_OK;
    }
    
    nsresult rv = PrepareForNext();
    *aMoreEncodings = NS_SUCCEEDED(rv);
    return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::nsContentEncodings::GetNext(nsACString& aNextEncoding)
{
    aNextEncoding.Truncate();
    if (!mReady) {
        nsresult rv = PrepareForNext();
        if (NS_FAILED(rv)) {
            return NS_ERROR_FAILURE;
        }
    }

    const nsACString & encoding = Substring(mCurStart, mCurEnd);

    nsACString::const_iterator start, end;
    encoding.BeginReading(start);
    encoding.EndReading(end);

    PRBool haveType = PR_FALSE;
    if (CaseInsensitiveFindInReadable(NS_LITERAL_CSTRING("gzip"),
                                      start,
                                      end)) {
        aNextEncoding.AssignLiteral(APPLICATION_GZIP);
        haveType = PR_TRUE;
    }

    if (!haveType) {
        encoding.BeginReading(start);
        if (CaseInsensitiveFindInReadable(NS_LITERAL_CSTRING("compress"),
                                          start,
                                          end)) {
            aNextEncoding.AssignLiteral(APPLICATION_COMPRESS);
                                           
            haveType = PR_TRUE;
        }
    }
    
    if (! haveType) {
        encoding.BeginReading(start);
        if (CaseInsensitiveFindInReadable(NS_LITERAL_CSTRING("deflate"),
                                          start,
                                          end)) {
            aNextEncoding.AssignLiteral(APPLICATION_ZIP);
            haveType = PR_TRUE;
        }
    }

    // Prepare to fetch the next encoding
    mCurEnd = mCurStart;
    mReady = PR_FALSE;
    
    if (haveType)
        return NS_OK;

    NS_WARNING("Unknown encoding type");
    return NS_ERROR_FAILURE;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsContentEncodings::nsISupports
//-----------------------------------------------------------------------------

NS_IMPL_ISUPPORTS1(nsHttpChannel::nsContentEncodings, nsIUTF8StringEnumerator)

//-----------------------------------------------------------------------------
// nsHttpChannel::nsContentEncodings <private>
//-----------------------------------------------------------------------------

nsresult
nsHttpChannel::nsContentEncodings::PrepareForNext(void)
{
    NS_PRECONDITION(mCurStart == mCurEnd, "Indeterminate state");
    
    // At this point both mCurStart and mCurEnd point to somewhere
    // past the end of the next thing we want to return
    
    while (mCurEnd != mEncodingHeader) {
        --mCurEnd;
        if (*mCurEnd != ',' && !nsCRT::IsAsciiSpace(*mCurEnd))
            break;
    }
    if (mCurEnd == mEncodingHeader)
        return NS_ERROR_NOT_AVAILABLE; // no more encodings
    ++mCurEnd;
        
    // At this point mCurEnd points to the first char _after_ the
    // header we want.  Furthermore, mCurEnd - 1 != mEncodingHeader
    
    mCurStart = mCurEnd - 1;
    while (mCurStart != mEncodingHeader &&
           *mCurStart != ',' && !nsCRT::IsAsciiSpace(*mCurStart))
        --mCurStart;
    if (*mCurStart == ',' || nsCRT::IsAsciiSpace(*mCurStart))
        ++mCurStart; // we stopped because of a weird char, so move up one
        
    // At this point mCurStart and mCurEnd bracket the encoding string
    // we want.  Check that it's not "identity"
    if (Substring(mCurStart, mCurEnd).Equals("identity",
                                             nsCaseInsensitiveCStringComparator())) {
        mCurEnd = mCurStart;
        return PrepareForNext();
    }
        
    mReady = PR_TRUE;
    return NS_OK;
}

//-----------------------------------------------------------------------------
// nsStreamListenerWrapper <private>
//-----------------------------------------------------------------------------

// Wrapper class to make replacement of nsHttpChannel's listener
// from JavaScript possible. It is workaround for bug 433711.
class nsStreamListenerWrapper : public nsIStreamListener
{
public:
    nsStreamListenerWrapper(nsIStreamListener *listener);

    NS_DECL_ISUPPORTS
    NS_FORWARD_NSIREQUESTOBSERVER(mListener->)
    NS_FORWARD_NSISTREAMLISTENER(mListener->)

private:
    ~nsStreamListenerWrapper() {}
    nsCOMPtr<nsIStreamListener> mListener;
};

nsStreamListenerWrapper::nsStreamListenerWrapper(nsIStreamListener *listener)
    : mListener(listener) 
{
    NS_ASSERTION(mListener, "no stream listener specified");
}

NS_IMPL_ISUPPORTS2(nsStreamListenerWrapper,
                   nsIStreamListener,
                   nsIRequestObserver)

//-----------------------------------------------------------------------------
// nsHttpChannel::nsITraceableChannel
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::SetNewListener(nsIStreamListener *aListener, nsIStreamListener **_retval)
{
    if (!mTracingEnabled)
        return NS_ERROR_FAILURE;

    NS_ENSURE_ARG_POINTER(aListener);

    nsCOMPtr<nsIStreamListener> wrapper = 
        new nsStreamListenerWrapper(mListener);

    if (!wrapper)
        return NS_ERROR_OUT_OF_MEMORY;

    wrapper.forget(_retval);
    mListener = aListener;
    return NS_OK;
}

void
nsHttpChannel::MaybeInvalidateCacheEntryForSubsequentGet()
{
    // See RFC 2616 section 5.1.1. These are considered valid
    // methods which DO NOT invalidate cache-entries for the
    // referred resource. POST, PUT and DELETE as well as any
    // other method not listed here will potentially invalidate
    // any cached copy of the resource
    if (mRequestHead.Method() == nsHttp::Options ||
       mRequestHead.Method() == nsHttp::Get ||
       mRequestHead.Method() == nsHttp::Head ||
       mRequestHead.Method() == nsHttp::Trace ||
       mRequestHead.Method() == nsHttp::Connect)
        return;
        
    // NOTE:
    // Following comments 24,32 and 33 in bug #327765, we only care about
    // the cache in the protocol-handler.
    // The logic below deviates from the original logic in OpenCacheEntry on
    // one point by using only READ_ONLY access-policy. I think this is safe.
    LOG(("MaybeInvalidateCacheEntryForSubsequentGet [this=%p]\n", this));

    nsCAutoString tmpCacheKey;
    // passing 0 in first param gives the cache-key for a GET to my resource
    GenerateCacheKey(0, tmpCacheKey);

    // Now, find the session holding the cache-entry
    nsCOMPtr<nsICacheSession> session;
    nsCacheStoragePolicy storagePolicy = DetermineStoragePolicy();

    nsresult rv;
    rv = gHttpHandler->GetCacheSession(storagePolicy,
                                       getter_AddRefs(session));

    if (NS_FAILED(rv)) return;

    // Finally, find the actual cache-entry
    nsCOMPtr<nsICacheEntryDescriptor> tmpCacheEntry;
    rv = session->OpenCacheEntry(tmpCacheKey, nsICache::ACCESS_READ,
                                 PR_FALSE,
                                 getter_AddRefs(tmpCacheEntry));
    
    // If entry was found, set its expiration-time = 0
    if(NS_SUCCEEDED(rv)) {
       tmpCacheEntry->SetExpirationTime(0);
    }
}

nsCacheStoragePolicy
nsHttpChannel::DetermineStoragePolicy()
{
    nsCacheStoragePolicy policy = nsICache::STORE_ANYWHERE;
    if (mLoadFlags & INHIBIT_PERSISTENT_CACHING)
        policy = nsICache::STORE_IN_MEMORY;

    return policy;
}

void
nsHttpChannel::AsyncOnExamineCachedResponse()
{
    gHttpHandler->OnExamineCachedResponse(this);
}
