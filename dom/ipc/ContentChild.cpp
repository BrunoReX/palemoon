/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
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
 * The Original Code is Mozilla Content App.
 *
 * The Initial Developer of the Original Code is
 *   The Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Frederic Plourde <frederic.plourde@collabora.co.uk>
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

#ifdef MOZ_WIDGET_GTK2
#include <gtk/gtk.h>
#endif

#ifdef MOZ_WIDGET_QT
#include "nsQAppInstance.h"
#endif

#include "ContentChild.h"
#include "CrashReporterChild.h"
#include "TabChild.h"
#if defined(MOZ_SYDNEYAUDIO)
#include "AudioChild.h"
#endif

#include "mozilla/ipc/TestShellChild.h"
#include "mozilla/net/NeckoChild.h"
#include "mozilla/ipc/XPCShellEnvironment.h"
#include "mozilla/jsipc/PContextWrapperChild.h"
#include "mozilla/dom/ExternalHelperAppChild.h"
#include "mozilla/dom/StorageChild.h"
#include "mozilla/dom/PCrashReporterChild.h"

#if defined(MOZ_SYDNEYAUDIO)
#include "nsAudioStream.h"
#endif
#include "nsIMemoryReporter.h"
#include "nsIObserverService.h"
#include "nsTObserverArray.h"
#include "nsIObserver.h"
#include "nsIPrefService.h"
#include "nsServiceManagerUtils.h"
#include "nsXULAppAPI.h"
#include "nsWeakReference.h"
#include "nsIScriptError.h"
#include "nsIConsoleService.h"

#include "History.h"
#include "nsDocShellCID.h"
#include "nsNetUtil.h"

#include "base/message_loop.h"
#include "base/task.h"

#include "nsChromeRegistryContent.h"
#include "mozilla/chrome/RegistryMessageUtils.h"
#include "nsFrameMessageManager.h"

#include "nsIGeolocationProvider.h"
#include "mozilla/dom/PMemoryReportRequestChild.h"

#ifdef MOZ_PERMISSIONS
#include "nsPermission.h"
#include "nsPermissionManager.h"
#endif

#include "nsDeviceMotion.h"

#if defined(ANDROID)
#include "APKOpen.h"
#endif

#ifdef XP_WIN
#include <process.h>
#define getpid _getpid
#endif

#ifdef ACCESSIBILITY
#include "nsIAccessibilityService.h"
#endif

using namespace mozilla::ipc;
using namespace mozilla::net;
using namespace mozilla::places;
using namespace mozilla::docshell;

namespace mozilla {
namespace dom {

nsString* gIndexedDBPath = nsnull;

class MemoryReportRequestChild : public PMemoryReportRequestChild
{
public:
    MemoryReportRequestChild();
    virtual ~MemoryReportRequestChild();
};

MemoryReportRequestChild::MemoryReportRequestChild()
{
    MOZ_COUNT_CTOR(MemoryReportRequestChild);
}

MemoryReportRequestChild::~MemoryReportRequestChild()
{
    MOZ_COUNT_DTOR(MemoryReportRequestChild);
}

class AlertObserver
{
public:

    AlertObserver(nsIObserver *aObserver, const nsString& aData)
        : mObserver(aObserver)
        , mData(aData)
    {
    }

    ~AlertObserver() {}

    bool ShouldRemoveFrom(nsIObserver* aObserver,
                          const nsString& aData) const
    {
        return (mObserver == aObserver &&
                mData == aData);
    }

    bool Observes(const nsString& aData) const
    {
        return mData.Equals(aData);
    }

    bool Notify(const nsCString& aType) const
    {
        mObserver->Observe(nsnull, aType.get(), mData.get());
        return true;
    }

private:
    nsCOMPtr<nsIObserver> mObserver;
    nsString mData;
};

class ConsoleListener : public nsIConsoleListener
{
public:
    ConsoleListener(ContentChild* aChild)
    : mChild(aChild) {}

    NS_DECL_ISUPPORTS
    NS_DECL_NSICONSOLELISTENER

private:
    ContentChild* mChild;
    friend class ContentChild;
};

NS_IMPL_ISUPPORTS1(ConsoleListener, nsIConsoleListener)

NS_IMETHODIMP
ConsoleListener::Observe(nsIConsoleMessage* aMessage)
{
    if (!mChild)
        return NS_OK;
    
    nsCOMPtr<nsIScriptError> scriptError = do_QueryInterface(aMessage);
    if (scriptError) {
        nsString msg, sourceName, sourceLine;
        nsXPIDLCString category;
        PRUint32 lineNum, colNum, flags;

        nsresult rv = scriptError->GetErrorMessage(msg);
        NS_ENSURE_SUCCESS(rv, rv);
        rv = scriptError->GetSourceName(sourceName);
        NS_ENSURE_SUCCESS(rv, rv);
        rv = scriptError->GetSourceLine(sourceLine);
        NS_ENSURE_SUCCESS(rv, rv);
        rv = scriptError->GetCategory(getter_Copies(category));
        NS_ENSURE_SUCCESS(rv, rv);
        rv = scriptError->GetLineNumber(&lineNum);
        NS_ENSURE_SUCCESS(rv, rv);
        rv = scriptError->GetColumnNumber(&colNum);
        NS_ENSURE_SUCCESS(rv, rv);
        rv = scriptError->GetFlags(&flags);
        NS_ENSURE_SUCCESS(rv, rv);
        mChild->SendScriptError(msg, sourceName, sourceLine,
                               lineNum, colNum, flags, category);
        return NS_OK;
    }

    nsXPIDLString msg;
    nsresult rv = aMessage->GetMessageMoz(getter_Copies(msg));
    NS_ENSURE_SUCCESS(rv, rv);
    mChild->SendConsoleMessage(msg);
    return NS_OK;
}

ContentChild* ContentChild::sSingleton;

ContentChild::ContentChild()
#ifdef ANDROID
 : mScreenSize(0, 0)
#endif
{
}

ContentChild::~ContentChild()
{
    delete gIndexedDBPath;
    gIndexedDBPath = nsnull;
}

bool
ContentChild::Init(MessageLoop* aIOLoop,
                   base::ProcessHandle aParentHandle,
                   IPC::Channel* aChannel)
{
#ifdef MOZ_WIDGET_GTK2
    // sigh
    gtk_init(NULL, NULL);
#endif

#ifdef MOZ_WIDGET_QT
    // sigh, seriously
    nsQAppInstance::AddRef();
#endif

#ifdef MOZ_X11
    // Do this after initializing GDK, or GDK will install its own handler.
    XRE_InstallX11ErrorHandler();
#endif

    NS_ASSERTION(!sSingleton, "only one ContentChild per child");

    Open(aChannel, aParentHandle, aIOLoop);
    sSingleton = this;

#if defined(ANDROID) && defined(MOZ_CRASHREPORTER)
    PCrashReporterChild* crashreporter = SendPCrashReporterConstructor();
    InfallibleTArray<Mapping> mappings;
    const struct mapping_info *info = getLibraryMapping();
    while (info && info->name) {
        mappings.AppendElement(Mapping(nsDependentCString(info->name),
                                       nsDependentCString(info->file_id),
                                       info->base,
                                       info->len,
                                       info->offset));
        info++;
    }
    crashreporter->SendAddLibraryMappings(mappings);
#endif

    return true;
}

void
ContentChild::InitXPCOM()
{
    nsCOMPtr<nsIConsoleService> svc(do_GetService(NS_CONSOLESERVICE_CONTRACTID));
    if (!svc) {
        NS_WARNING("Couldn't acquire console service");
        return;
    }

    mConsoleListener = new ConsoleListener(this);
    if (NS_FAILED(svc->RegisterListener(mConsoleListener)))
        NS_WARNING("Couldn't register console listener for child process");
}

PMemoryReportRequestChild*
ContentChild::AllocPMemoryReportRequest()
{
    return new MemoryReportRequestChild();
}

// This is just a wrapper for InfallibleTArray<MemoryReport> that implements
// nsISupports, so it can be passed to nsIMemoryMultiReporter::CollectReports.
class MemoryReportsWrapper : public nsISupports {
public:
    NS_DECL_ISUPPORTS
    MemoryReportsWrapper(InfallibleTArray<MemoryReport> *r) : mReports(r) { }
    InfallibleTArray<MemoryReport> *mReports;
};
NS_IMPL_ISUPPORTS0(MemoryReportsWrapper)

class MemoryReportCallback : public nsIMemoryMultiReporterCallback
{
public:
    NS_DECL_ISUPPORTS

    MemoryReportCallback(const nsACString &aProcess)
    : mProcess(aProcess)
    {
    }

    NS_IMETHOD Callback(const nsACString &aProcess, const nsACString &aPath,
                        PRInt32 aKind, PRInt32 aUnits, PRInt64 aAmount,
                        const nsACString &aDescription,
                        nsISupports *aiWrappedReports)
    {
        MemoryReportsWrapper *wrappedReports =
            static_cast<MemoryReportsWrapper *>(aiWrappedReports);

        MemoryReport memreport(mProcess, nsCString(aPath), aKind, aUnits,
                               aAmount, nsCString(aDescription));
        wrappedReports->mReports->AppendElement(memreport);
        return NS_OK;
    }
private:
    const nsCString mProcess;
};
NS_IMPL_ISUPPORTS1(
  MemoryReportCallback
, nsIMemoryMultiReporterCallback
)

bool
ContentChild::RecvPMemoryReportRequestConstructor(PMemoryReportRequestChild* child)
{
    
    nsCOMPtr<nsIMemoryReporterManager> mgr = do_GetService("@mozilla.org/memory-reporter-manager;1");

    InfallibleTArray<MemoryReport> reports;

    static const int maxLength = 31;   // big enough; pid is only a few chars
    nsPrintfCString process(maxLength, "Content (%d)", getpid());

    // First do the vanilla memory reporters.
    nsCOMPtr<nsISimpleEnumerator> e;
    mgr->EnumerateReporters(getter_AddRefs(e));
    PRBool more;
    while (NS_SUCCEEDED(e->HasMoreElements(&more)) && more) {
      nsCOMPtr<nsIMemoryReporter> r;
      e->GetNext(getter_AddRefs(r));

      nsCString path;
      PRInt32 kind;
      PRInt32 units;
      PRInt64 amount;
      nsCString desc;
      r->GetPath(path);
      r->GetKind(&kind);
      r->GetUnits(&units);
      r->GetAmount(&amount);
      r->GetDescription(desc);

      MemoryReport memreport(process, path, kind, units, amount, desc);
      reports.AppendElement(memreport);
    }

    // Then do the memory multi-reporters, by calling CollectReports on each
    // one, whereupon the callback will turn each measurement into a
    // MemoryReport.
    mgr->EnumerateMultiReporters(getter_AddRefs(e));
    nsRefPtr<MemoryReportsWrapper> wrappedReports =
        new MemoryReportsWrapper(&reports);
    nsRefPtr<MemoryReportCallback> cb = new MemoryReportCallback(process);
    while (NS_SUCCEEDED(e->HasMoreElements(&more)) && more) {
      nsCOMPtr<nsIMemoryMultiReporter> r;
      e->GetNext(getter_AddRefs(r));
      r->CollectReports(cb, wrappedReports);
    }

    child->Send__delete__(child, reports);
    return true;
}

bool
ContentChild::DeallocPMemoryReportRequest(PMemoryReportRequestChild* actor)
{
    delete actor;
    return true;
}

PBrowserChild*
ContentChild::AllocPBrowser(const PRUint32& aChromeFlags)
{
    nsRefPtr<TabChild> iframe = new TabChild(aChromeFlags);
    return NS_SUCCEEDED(iframe->Init()) ? iframe.forget().get() : NULL;
}

bool
ContentChild::DeallocPBrowser(PBrowserChild* iframe)
{
    TabChild* child = static_cast<TabChild*>(iframe);
    NS_RELEASE(child);
    return true;
}

PCrashReporterChild*
ContentChild::AllocPCrashReporter()
{
    return new CrashReporterChild();
}

bool
ContentChild::DeallocPCrashReporter(PCrashReporterChild* crashreporter)
{
    delete crashreporter;
    return true;
}

PTestShellChild*
ContentChild::AllocPTestShell()
{
    return new TestShellChild();
}

bool
ContentChild::DeallocPTestShell(PTestShellChild* shell)
{
    delete shell;
    return true;
}

bool
ContentChild::RecvPTestShellConstructor(PTestShellChild* actor)
{
    actor->SendPContextWrapperConstructor()->SendPObjectWrapperConstructor(true);
    return true;
}

PAudioChild*
ContentChild::AllocPAudio(const PRInt32& numChannels,
                          const PRInt32& rate,
                          const PRInt32& format)
{
#if defined(MOZ_SYDNEYAUDIO)
    AudioChild *child = new AudioChild();
    NS_ADDREF(child);
    return child;
#else
    return nsnull;
#endif
}

bool
ContentChild::DeallocPAudio(PAudioChild* doomed)
{
#if defined(MOZ_SYDNEYAUDIO)
    AudioChild *child = static_cast<AudioChild*>(doomed);
    NS_RELEASE(child);
#endif
    return true;
}

PNeckoChild* 
ContentChild::AllocPNecko()
{
    return new NeckoChild();
}

bool 
ContentChild::DeallocPNecko(PNeckoChild* necko)
{
    delete necko;
    return true;
}

PExternalHelperAppChild*
ContentChild::AllocPExternalHelperApp(const IPC::URI& uri,
                                      const nsCString& aMimeContentType,
                                      const nsCString& aContentDisposition,
                                      const bool& aForceSave,
                                      const PRInt64& aContentLength,
                                      const IPC::URI& aReferrer)
{
    ExternalHelperAppChild *child = new ExternalHelperAppChild();
    child->AddRef();
    return child;
}

bool
ContentChild::DeallocPExternalHelperApp(PExternalHelperAppChild* aService)
{
    ExternalHelperAppChild *child = static_cast<ExternalHelperAppChild*>(aService);
    child->Release();
    return true;
}

PStorageChild*
ContentChild::AllocPStorage(const StorageConstructData& aData)
{
    NS_NOTREACHED("We should never be manually allocating PStorageChild actors");
    return nsnull;
}

bool
ContentChild::DeallocPStorage(PStorageChild* aActor)
{
    StorageChild* child = static_cast<StorageChild*>(aActor);
    child->ReleaseIPDLReference();
    return true;
}

bool
ContentChild::RecvRegisterChrome(const InfallibleTArray<ChromePackage>& packages,
                                 const InfallibleTArray<ResourceMapping>& resources,
                                 const InfallibleTArray<OverrideMapping>& overrides,
                                 const nsCString& locale)
{
    nsCOMPtr<nsIChromeRegistry> registrySvc = nsChromeRegistry::GetService();
    nsChromeRegistryContent* chromeRegistry =
        static_cast<nsChromeRegistryContent*>(registrySvc.get());
    chromeRegistry->RegisterRemoteChrome(packages, resources, overrides, locale);
    return true;
}

bool
ContentChild::RecvSetOffline(const PRBool& offline)
{
  nsCOMPtr<nsIIOService> io (do_GetIOService());
  NS_ASSERTION(io, "IO Service can not be null");

  io->SetOffline(offline);

  return true;
}

void
ContentChild::ActorDestroy(ActorDestroyReason why)
{
    if (AbnormalShutdown == why) {
        NS_WARNING("shutting down early because of crash!");
        QuickExit();
    }

#ifndef DEBUG
    // In release builds, there's no point in the content process
    // going through the full XPCOM shutdown path, because it doesn't
    // keep persistent state.
    QuickExit();
#endif

    mAlertObservers.Clear();

    nsCOMPtr<nsIConsoleService> svc(do_GetService(NS_CONSOLESERVICE_CONTRACTID));
    if (svc) {
        svc->UnregisterListener(mConsoleListener);
        mConsoleListener->mChild = nsnull;
    }

    XRE_ShutdownChildProcess();
}

void
ContentChild::ProcessingError(Result what)
{
    switch (what) {
    case MsgDropped:
        QuickExit();

    case MsgNotKnown:
    case MsgNotAllowed:
    case MsgPayloadError:
    case MsgProcessingError:
    case MsgRouteError:
    case MsgValueError:
        NS_RUNTIMEABORT("aborting because of fatal error");

    default:
        NS_RUNTIMEABORT("not reached");
    }
}

void
ContentChild::QuickExit()
{
    NS_WARNING("content process _exit()ing");
    _exit(0);
}

nsresult
ContentChild::AddRemoteAlertObserver(const nsString& aData,
                                     nsIObserver* aObserver)
{
    NS_ASSERTION(aObserver, "Adding a null observer?");
    mAlertObservers.AppendElement(new AlertObserver(aObserver, aData));
    return NS_OK;
}

bool
ContentChild::RecvPreferenceUpdate(const PrefTuple& aPref)
{
    nsCOMPtr<nsIPrefServiceInternal> prefs = do_GetService("@mozilla.org/preferences-service;1");
    if (!prefs)
        return false;

    prefs->SetPreference(&aPref);

    return true;
}

bool
ContentChild::RecvClearUserPreference(const nsCString& aPrefName)
{
    nsCOMPtr<nsIPrefServiceInternal> prefs = do_GetService("@mozilla.org/preferences-service;1");
    if (!prefs)
        return false;

    prefs->ClearContentPref(aPrefName);

    return true;
}

bool
ContentChild::RecvNotifyAlertsObserver(const nsCString& aType, const nsString& aData)
{
    for (PRUint32 i = 0; i < mAlertObservers.Length();
         /*we mutate the array during the loop; ++i iff no mutation*/) {
        AlertObserver* observer = mAlertObservers[i];
        if (observer->Observes(aData) && observer->Notify(aType)) {
            // if aType == alertfinished, this alert is done.  we can
            // remove the observer.
            if (aType.Equals(nsDependentCString("alertfinished"))) {
                mAlertObservers.RemoveElementAt(i);
                continue;
            }
        }
        ++i;
    }
    return true;
}

bool
ContentChild::RecvNotifyVisited(const IPC::URI& aURI)
{
    nsCOMPtr<nsIURI> newURI(aURI);
    History::GetService()->NotifyVisited(newURI);
    return true;
}


bool
ContentChild::RecvAsyncMessage(const nsString& aMsg, const nsString& aJSON)
{
  nsRefPtr<nsFrameMessageManager> cpm = nsFrameMessageManager::sChildProcessManager;
  if (cpm) {
    cpm->ReceiveMessage(static_cast<nsIContentFrameMessageManager*>(cpm.get()),
                        aMsg, PR_FALSE, aJSON, nsnull, nsnull);
  }
  return true;
}

bool
ContentChild::RecvGeolocationUpdate(const GeoPosition& somewhere)
{
  nsCOMPtr<nsIGeolocationUpdate> gs = do_GetService("@mozilla.org/geolocation/service;1");
  if (!gs) {
    return true;
  }
  nsCOMPtr<nsIDOMGeoPosition> position = somewhere;
  gs->Update(position);
  return true;
}

bool
ContentChild::RecvAddPermission(const IPC::Permission& permission)
{
#if MOZ_PERMISSIONS
  nsRefPtr<nsPermissionManager> permissionManager =
    nsPermissionManager::GetSingleton();
  NS_ABORT_IF_FALSE(permissionManager, 
                   "We have no permissionManager in the Content process !");

  permissionManager->AddInternal(nsCString(permission.host),
                                 nsCString(permission.type),
                                 permission.capability,
                                 0,
                                 permission.expireType,
                                 permission.expireTime,
                                 nsPermissionManager::eNotify,
                                 nsPermissionManager::eNoDBOperation);
#endif

  return true;
}

bool
ContentChild::RecvDeviceMotionChanged(const long int& type,
                                      const double& x, const double& y,
                                      const double& z)
{
    nsCOMPtr<nsIDeviceMotionUpdate> dmu = 
        do_GetService(NS_DEVICE_MOTION_CONTRACTID);
    if (dmu)
        dmu->DeviceMotionChanged(type, x, y, z);
    return true;
}

bool
ContentChild::RecvScreenSizeChanged(const gfxIntSize& size)
{
#ifdef ANDROID
    mScreenSize = size;
#else
    NS_RUNTIMEABORT("Message currently only expected on android");
#endif
  return true;
}

bool
ContentChild::RecvFlushMemory(const nsString& reason)
{
    nsCOMPtr<nsIObserverService> os =
        mozilla::services::GetObserverService();
    if (os)
        os->NotifyObservers(nsnull, "memory-pressure", reason.get());
  return true;
}

bool
ContentChild::RecvActivateA11y()
{
#ifdef ACCESSIBILITY
    // Start accessibility in content process if it's running in chrome
    // process.
    nsCOMPtr<nsIAccessibilityService> accService =
        do_GetService("@mozilla.org/accessibilityService;1");
#endif
    return true;
}

nsString&
ContentChild::GetIndexedDBPath()
{
    if (!gIndexedDBPath) {
        gIndexedDBPath = new nsString(); // cleaned up in the destructor
        SendGetIndexedDBDirectory(gIndexedDBPath);
    }

    return *gIndexedDBPath;
}

} // namespace dom
} // namespace mozilla
