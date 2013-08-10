/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsHttp.h"
#include "mozilla/net/NeckoParent.h"
#include "mozilla/net/HttpChannelParent.h"
#include "mozilla/net/CookieServiceParent.h"
#include "mozilla/net/WyciwygChannelParent.h"
#include "mozilla/net/FTPChannelParent.h"
#include "mozilla/net/WebSocketChannelParent.h"
#include "mozilla/net/RemoteOpenFileParent.h"
#include "mozilla/dom/TabParent.h"
#include "mozilla/dom/network/TCPSocketParent.h"
#include "mozilla/ipc/URIUtils.h"
#include "mozilla/Preferences.h"
#include "mozilla/LoadContext.h"
#include "nsPrintfCString.h"
#include "nsHTMLDNSPrefetch.h"
#include "nsIAppsService.h"
#include "nsEscape.h"

using mozilla::dom::TabParent;
using mozilla::net::PTCPSocketParent;
using mozilla::dom::TCPSocketParent;
using IPC::SerializedLoadContext;

namespace mozilla {
namespace net {

static bool gDisableIPCSecurity = false;
static const char kPrefDisableIPCSecurity[] = "network.disable.ipc.security";

// C++ file contents
NeckoParent::NeckoParent()
{
  Preferences::AddBoolVarCache(&gDisableIPCSecurity, kPrefDisableIPCSecurity);

  if (!gDisableIPCSecurity) {
    // cache values for core/packaged apps basepaths
    nsAutoString corePath, webPath;
    nsCOMPtr<nsIAppsService> appsService = do_GetService(APPS_SERVICE_CONTRACTID);
    if (appsService) {
      appsService->GetCoreAppsBasePath(corePath);
      appsService->GetWebAppsBasePath(webPath);
    }
    // corePath may be empty: we don't use it for all build types
    MOZ_ASSERT(!webPath.IsEmpty());

    LossyCopyUTF16toASCII(corePath, mCoreAppsBasePath);
    LossyCopyUTF16toASCII(webPath, mWebAppsBasePath);
  }
}

NeckoParent::~NeckoParent()
{
}

static PBOverrideStatus
PBOverrideStatusFromLoadContext(const SerializedLoadContext& aSerialized)
{
  if (!aSerialized.IsNotNull() && aSerialized.IsPrivateBitValid()) {
    return aSerialized.mUsePrivateBrowsing ?
      kPBOverride_Private :
      kPBOverride_NotPrivate;
  }
  return kPBOverride_Unset;
}

const char*
NeckoParent::GetValidatedAppInfo(const SerializedLoadContext& aSerialized,
                                 PBrowserParent* aBrowser,
                                 uint32_t* aAppId,
                                 bool* aInBrowserElement)
{
  if (!gDisableIPCSecurity) {
    if (!aBrowser) {
      return "missing required PBrowser argument";
    }
    if (!aSerialized.IsNotNull()) {
      return "SerializedLoadContext from child is null";
    }
  }

  *aAppId = NECKO_UNKNOWN_APP_ID;
  *aInBrowserElement = false;

  if (aBrowser) {
    nsRefPtr<TabParent> tabParent = static_cast<TabParent*>(aBrowser);

    *aAppId = tabParent->OwnOrContainingAppId();
    *aInBrowserElement = tabParent->IsBrowserElement();

    if (*aAppId == NECKO_UNKNOWN_APP_ID) {
      return "TabParent reports appId=NECKO_UNKNOWN_APP_ID!";
    }
    // We may get appID=NO_APP if child frame is neither a browser nor an app
    if (*aAppId == NECKO_NO_APP_ID) {
      if (tabParent->HasOwnApp()) {
        return "TabParent reports NECKO_NO_APP_ID but also is an app";
      }
      if (!gDisableIPCSecurity && tabParent->IsBrowserElement()) {
        // <iframe mozbrowser> which doesn't have an <iframe mozapp> above it.
        // This is not supported now, and we'll need to do a code audit to make
        // sure we can handle it (i.e don't short-circuit using separate
        // namespace if just appID==0)
        return "TabParent reports appId=NECKO_NO_APP_ID but is a mozbrowser";
      }
    }
  } else {
    // Only trust appId/inBrowser from child-side loadcontext if we're in
    // testing mode: allows xpcshell tests to masquerade as apps
    MOZ_ASSERT(gDisableIPCSecurity);
    if (!gDisableIPCSecurity) {
      return "internal error";
    }
    if (aSerialized.IsNotNull()) {
      *aAppId = aSerialized.mAppId;
      *aInBrowserElement = aSerialized.mIsInBrowserElement;
    } else {
      *aAppId = NECKO_NO_APP_ID;
    }
  }
  return nullptr;
}

const char *
NeckoParent::CreateChannelLoadContext(PBrowserParent* aBrowser,
                                      const SerializedLoadContext& aSerialized,
                                      nsCOMPtr<nsILoadContext> &aResult)
{
  uint32_t appId = NECKO_UNKNOWN_APP_ID;
  bool inBrowser = false;
  nsIDOMElement* topFrameElement = nullptr;
  const char* error = GetValidatedAppInfo(aSerialized, aBrowser, &appId, &inBrowser);
  if (error) {
    return error;
  }

  if (aBrowser) {
    nsRefPtr<TabParent> tabParent = static_cast<TabParent*>(aBrowser);
    topFrameElement = tabParent->GetOwnerElement();
  }

  // if gDisableIPCSecurity, we may not have a LoadContext to set. This is
  // the common case for most xpcshell tests.
  if (aSerialized.IsNotNull()) {
    aResult = new LoadContext(aSerialized, topFrameElement, appId, inBrowser);
  }

  return nullptr;
}

PHttpChannelParent*
NeckoParent::AllocPHttpChannel(PBrowserParent* aBrowser,
                               const SerializedLoadContext& aSerialized)
{
  nsCOMPtr<nsILoadContext> loadContext;
  const char *error = CreateChannelLoadContext(aBrowser, aSerialized,
                                               loadContext);
  if (error) {
    printf_stderr("NeckoParent::AllocPHttpChannel: "
                  "FATAL error: %s: KILLING CHILD PROCESS\n",
                  error);
    return nullptr;
  }
  PBOverrideStatus overrideStatus = PBOverrideStatusFromLoadContext(aSerialized);
  HttpChannelParent *p = new HttpChannelParent(aBrowser, loadContext, overrideStatus);
  p->AddRef();
  return p;
}

bool
NeckoParent::DeallocPHttpChannel(PHttpChannelParent* channel)
{
  HttpChannelParent *p = static_cast<HttpChannelParent *>(channel);
  p->Release();
  return true;
}

PFTPChannelParent*
NeckoParent::AllocPFTPChannel(PBrowserParent* aBrowser,
                              const SerializedLoadContext& aSerialized)
{
  nsCOMPtr<nsILoadContext> loadContext;
  const char *error = CreateChannelLoadContext(aBrowser, aSerialized,
                                               loadContext);
  if (error) {
    printf_stderr("NeckoParent::AllocPFTPChannel: "
                  "FATAL error: %s: KILLING CHILD PROCESS\n",
                  error);
    return nullptr;
  }
  PBOverrideStatus overrideStatus = PBOverrideStatusFromLoadContext(aSerialized);
  FTPChannelParent *p = new FTPChannelParent(loadContext, overrideStatus);
  p->AddRef();
  return p;
}

bool
NeckoParent::DeallocPFTPChannel(PFTPChannelParent* channel)
{
  FTPChannelParent *p = static_cast<FTPChannelParent *>(channel);
  p->Release();
  return true;
}

PCookieServiceParent* 
NeckoParent::AllocPCookieService()
{
  return new CookieServiceParent();
}

bool 
NeckoParent::DeallocPCookieService(PCookieServiceParent* cs)
{
  delete cs;
  return true;
}

PWyciwygChannelParent*
NeckoParent::AllocPWyciwygChannel()
{
  WyciwygChannelParent *p = new WyciwygChannelParent();
  p->AddRef();
  return p;
}

bool
NeckoParent::DeallocPWyciwygChannel(PWyciwygChannelParent* channel)
{
  WyciwygChannelParent *p = static_cast<WyciwygChannelParent *>(channel);
  p->Release();
  return true;
}

PWebSocketParent*
NeckoParent::AllocPWebSocket(PBrowserParent* browser,
                             const SerializedLoadContext& serialized)
{
  nsCOMPtr<nsILoadContext> loadContext;
  const char *error = CreateChannelLoadContext(browser, serialized,
                                               loadContext);
  if (error) {
    printf_stderr("NeckoParent::AllocPWebSocket: "
                  "FATAL error: %s: KILLING CHILD PROCESS\n",
                  error);
    return nullptr;
  }

  TabParent* tabParent = static_cast<TabParent*>(browser);
  PBOverrideStatus overrideStatus = PBOverrideStatusFromLoadContext(serialized);
  WebSocketChannelParent* p = new WebSocketChannelParent(tabParent, loadContext,
                                                         overrideStatus);
  p->AddRef();
  return p;
}

bool
NeckoParent::DeallocPWebSocket(PWebSocketParent* actor)
{
  WebSocketChannelParent* p = static_cast<WebSocketChannelParent*>(actor);
  p->Release();
  return true;
}

PTCPSocketParent*
NeckoParent::AllocPTCPSocket(const nsString& aHost,
                             const uint16_t& aPort,
                             const bool& useSSL,
                             const nsString& aBinaryType,
                             PBrowserParent* aBrowser)
{
  TCPSocketParent* p = new TCPSocketParent();
  p->AddRef();
  return p;
}

bool
NeckoParent::RecvPTCPSocketConstructor(PTCPSocketParent* aActor,
                                       const nsString& aHost,
                                       const uint16_t& aPort,
                                       const bool& useSSL,
                                       const nsString& aBinaryType,
                                       PBrowserParent* aBrowser)
{
  return static_cast<TCPSocketParent*>(aActor)->
      Init(aHost, aPort, useSSL, aBinaryType, aBrowser);
}

bool
NeckoParent::DeallocPTCPSocket(PTCPSocketParent* actor)
{
  TCPSocketParent* p = static_cast<TCPSocketParent*>(actor);
  p->Release();
  return true;
}

PRemoteOpenFileParent*
NeckoParent::AllocPRemoteOpenFile(const URIParams& aURI,
                                  PBrowserParent* aBrowser)
{
  nsCOMPtr<nsIURI> uri = DeserializeURI(aURI);
  nsCOMPtr<nsIFileURL> fileURL = do_QueryInterface(uri);
  if (!fileURL) {
    return nullptr;
  }

  // security checks
  if (!gDisableIPCSecurity) {
    if (!aBrowser) {
      printf_stderr("NeckoParent::AllocPRemoteOpenFile: "
                    "FATAL error: missing TabParent: KILLING CHILD PROCESS\n");
      return nullptr;
    }
    nsRefPtr<TabParent> tabParent = static_cast<TabParent*>(aBrowser);
    uint32_t appId = tabParent->OwnOrContainingAppId();
    nsCOMPtr<nsIAppsService> appsService =
      do_GetService(APPS_SERVICE_CONTRACTID);
    if (!appsService) {
      return nullptr;
    }
    nsCOMPtr<mozIDOMApplication> domApp;
    nsresult rv = appsService->GetAppByLocalId(appId, getter_AddRefs(domApp));
    if (!domApp) {
      return nullptr;
    }
    nsCOMPtr<mozIApplication> mozApp = do_QueryInterface(domApp);
    if (!mozApp) {
      return nullptr;
    }
    bool hasManage = false;
    rv = mozApp->HasPermission("webapps-manage", &hasManage);
    if (NS_FAILED(rv)) {
      return nullptr;
    }

    nsAutoCString requestedPath;
    fileURL->GetPath(requestedPath);
    NS_UnescapeURL(requestedPath);

    if (hasManage) {
      // webapps-manage permission means allow reading any application.zip file
      // in either the regular webapps directory, or the core apps directory (if
      // we're using one).
      NS_NAMED_LITERAL_CSTRING(appzip, "/application.zip");
      nsAutoCString pathEnd;
      requestedPath.Right(pathEnd, appzip.Length());
      if (!pathEnd.Equals(appzip)) {
        return nullptr;
      }
      nsAutoCString pathStart;
      requestedPath.Left(pathStart, mWebAppsBasePath.Length());
      if (!pathStart.Equals(mWebAppsBasePath)) {
        if (mCoreAppsBasePath.IsEmpty()) {
          return nullptr;
        }
        requestedPath.Left(pathStart, mCoreAppsBasePath.Length());
        if (!pathStart.Equals(mCoreAppsBasePath)) {
          return nullptr;
        }
      }
      // Finally: make sure there are no "../" in URI.
      // Note: not checking for symlinks (would cause I/O for each path
      // component).  So it's up to us to avoid creating symlinks that could
      // provide attack vectors.
      if (PL_strnstr(requestedPath.BeginReading(), "/../",
                     requestedPath.Length())) {
        printf_stderr("NeckoParent::AllocPRemoteOpenFile: "
                      "FATAL error: requested file URI contains '/../' "
                      "KILLING CHILD PROCESS\n");
        return nullptr;
      }
    } else {
      // regular packaged apps can only access their own application.zip file
      nsAutoString basePath;
      rv = mozApp->GetBasePath(basePath);
      if (NS_FAILED(rv)) {
        return nullptr;
      }
      nsAutoString uuid;
      rv = mozApp->GetId(uuid);
      if (NS_FAILED(rv)) {
        return nullptr;
      }
      nsPrintfCString mustMatch("%s/%s/application.zip",
                                NS_LossyConvertUTF16toASCII(basePath).get(),
                                NS_LossyConvertUTF16toASCII(uuid).get());
      if (!requestedPath.Equals(mustMatch)) {
        printf_stderr("NeckoParent::AllocPRemoteOpenFile: "
                      "FATAL error: requesting file other than application.zip: "
                      "KILLING CHILD PROCESS\n");
        return nullptr;
      }
    }
  }

  RemoteOpenFileParent* parent = new RemoteOpenFileParent(fileURL);
  return parent;
}

bool
NeckoParent::DeallocPRemoteOpenFile(PRemoteOpenFileParent* actor)
{
  delete actor;
  return true;
}

bool
NeckoParent::RecvHTMLDNSPrefetch(const nsString& hostname,
                                 const uint16_t& flags)
{
  nsHTMLDNSPrefetch::Prefetch(hostname, flags);
  return true;
}

bool
NeckoParent::RecvCancelHTMLDNSPrefetch(const nsString& hostname,
                                 const uint16_t& flags,
                                 const nsresult& reason)
{
  nsHTMLDNSPrefetch::CancelPrefetch(hostname, flags, reason);
  return true;
}

}} // mozilla::net

