/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   John Bandhauer <jband@netscape.com> (original author)
 *   Pierre Phaneuf <pp@ludusdesign.com>
 *   Nate Nielsen <nielsen@memberwebs.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

/* High level class and public functions implementation. */

#include "xpcprivate.h"
#include "XPCWrapper.h"
#include "nsBaseHashtable.h"
#include "nsHashKeys.h"
#include "jsatom.h"
#include "jsfriendapi.h"
#include "jsgc.h"
#include "dom_quickstubs.h"
#include "nsNullPrincipal.h"
#include "nsIURI.h"
#include "nsJSEnvironment.h"
#include "nsThreadUtils.h"

#include "XrayWrapper.h"
#include "WrapperFactory.h"
#include "AccessCheck.h"

#include "jsdIDebuggerService.h"

#include "XPCQuickStubs.h"
#include "dombindings.h"

#include "mozilla/Assertions.h"
#include "mozilla/Base64.h"
#include "mozilla/Util.h"

#include "nsWrapperCacheInlines.h"

NS_IMPL_THREADSAFE_ISUPPORTS7(nsXPConnect,
                              nsIXPConnect,
                              nsISupportsWeakReference,
                              nsIThreadObserver,
                              nsIJSRuntimeService,
                              nsIJSContextStack,
                              nsIThreadJSContextStack,
                              nsIJSEngineTelemetryStats)

nsXPConnect* nsXPConnect::gSelf = nsnull;
JSBool       nsXPConnect::gOnceAliveNowDead = false;
PRUint32     nsXPConnect::gReportAllJSExceptions = 0;
JSBool       nsXPConnect::gDebugMode = false;
JSBool       nsXPConnect::gDesiredDebugMode = false;

// Global cache of the default script security manager (QI'd to
// nsIScriptSecurityManager)
nsIScriptSecurityManager *nsXPConnect::gScriptSecurityManager = nsnull;

const char XPC_CONTEXT_STACK_CONTRACTID[] = "@mozilla.org/js/xpc/ContextStack;1";
const char XPC_RUNTIME_CONTRACTID[]       = "@mozilla.org/js/xpc/RuntimeService;1";
const char XPC_EXCEPTION_CONTRACTID[]     = "@mozilla.org/js/xpc/Exception;1";
const char XPC_CONSOLE_CONTRACTID[]       = "@mozilla.org/consoleservice;1";
const char XPC_SCRIPT_ERROR_CONTRACTID[]  = "@mozilla.org/scripterror;1";
const char XPC_ID_CONTRACTID[]            = "@mozilla.org/js/xpc/ID;1";
const char XPC_XPCONNECT_CONTRACTID[]     = "@mozilla.org/js/xpc/XPConnect;1";

/***************************************************************************/

nsXPConnect::nsXPConnect()
    :   mRuntime(nsnull),
        mInterfaceInfoManager(do_GetService(NS_INTERFACEINFOMANAGER_SERVICE_CONTRACTID)),
        mDefaultSecurityManager(nsnull),
        mDefaultSecurityManagerFlags(0),
        mShuttingDown(false),
        mNeedGCBeforeCC(true),
        mEventDepth(0),
        mCycleCollectionContext(nsnull)
{
    mRuntime = XPCJSRuntime::newXPCJSRuntime(this);

    nsCycleCollector_registerRuntime(nsIProgrammingLanguage::JAVASCRIPT, this);
#ifdef DEBUG_CC
    mJSRoots.ops = nsnull;
#endif

    char* reportableEnv = PR_GetEnv("MOZ_REPORT_ALL_JS_EXCEPTIONS");
    if (reportableEnv && *reportableEnv)
        gReportAllJSExceptions = 1;
}

nsXPConnect::~nsXPConnect()
{
    nsCycleCollector_forgetRuntime(nsIProgrammingLanguage::JAVASCRIPT);

    JSContext *cx = nsnull;
    if (mRuntime) {
        // Create our own JSContext rather than an XPCCallContext, since
        // otherwise we will create a new safe JS context and attach a
        // components object that won't get GCed.
        // And do this before calling CleanupAllThreads, so that we
        // don't create an extra xpcPerThreadData.
        cx = JS_NewContext(mRuntime->GetJSRuntime(), 8192);
    }

    XPCPerThreadData::CleanupAllThreads();
    mShuttingDown = true;
    if (cx) {
        JS_BeginRequest(cx);

        // XXX Call even if |mRuntime| null?
        XPCWrappedNativeScope::SystemIsBeingShutDown(cx);

        mRuntime->SystemIsBeingShutDown(cx);

        JS_EndRequest(cx);
        JS_DestroyContext(cx);
    }

    NS_IF_RELEASE(mDefaultSecurityManager);

    gScriptSecurityManager = nsnull;

    // shutdown the logging system
    XPC_LOG_FINISH();

    delete mRuntime;

    gSelf = nsnull;
    gOnceAliveNowDead = true;
}

// static
nsXPConnect*
nsXPConnect::GetXPConnect()
{
    // Do a release-mode assert that we're not doing anything significant in
    // XPConnect off the main thread. If you're an extension developer hitting
    // this, you need to change your code. See bug 716167.
    if (!NS_LIKELY(NS_IsMainThread() || NS_IsCycleCollectorThread()))
        JS_Assert("NS_IsMainThread()", __FILE__, __LINE__);

    if (!gSelf) {
        if (gOnceAliveNowDead)
            return nsnull;
        gSelf = new nsXPConnect();
        if (!gSelf)
            return nsnull;

        if (!gSelf->mRuntime) {
            NS_RUNTIMEABORT("Couldn't create XPCJSRuntime.");
        }
        if (!gSelf->mInterfaceInfoManager) {
            NS_RUNTIMEABORT("Couldn't get global interface info manager.");
        }

        // Initial extra ref to keep the singleton alive
        // balanced by explicit call to ReleaseXPConnectSingleton()
        NS_ADDREF(gSelf);

        // Add XPConnect as an thread observer.
        //
        // The cycle collector sometimes calls GetXPConnect, but it should never
        // be the one that initializes gSelf.
        MOZ_ASSERT(NS_IsMainThread());
        nsCOMPtr<nsIThreadInternal> thread = do_QueryInterface(NS_GetCurrentThread());
        if (NS_FAILED(thread->AddObserver(gSelf))) {
            NS_RELEASE(gSelf);
            // Fall through to returning null
        }
    }
    return gSelf;
}

// static
nsXPConnect*
nsXPConnect::GetSingleton()
{
    nsXPConnect* xpc = nsXPConnect::GetXPConnect();
    NS_IF_ADDREF(xpc);
    return xpc;
}

// static
void
nsXPConnect::ReleaseXPConnectSingleton()
{
    nsXPConnect* xpc = gSelf;
    if (xpc) {

        // The thread subsystem may have been shut down already, so make sure
        // to check for null here.
        nsCOMPtr<nsIThreadInternal> thread = do_QueryInterface(NS_GetCurrentThread());
        if (thread) {
            MOZ_ASSERT(NS_IsMainThread());
            thread->RemoveObserver(xpc);
        }

#ifdef DEBUG
        // force a dump of the JavaScript gc heap if JS is still alive
        // if requested through XPC_SHUTDOWN_HEAP_DUMP environment variable
        {
            // autoscope
            XPCCallContext ccx(NATIVE_CALLER);
            if (ccx.IsValid()) {
                const char* dumpName = getenv("XPC_SHUTDOWN_HEAP_DUMP");
                if (dumpName) {
                    FILE* dumpFile = (*dumpName == '\0' ||
                                      strcmp(dumpName, "stdout") == 0)
                                     ? stdout
                                     : fopen(dumpName, "w");
                    if (dumpFile) {
                        JS_DumpHeap(ccx, dumpFile, nsnull, JSTRACE_OBJECT, nsnull,
                                    static_cast<size_t>(-1), nsnull);
                        if (dumpFile != stdout)
                            fclose(dumpFile);
                    }
                }
            }
        }
#endif
#ifdef XPC_DUMP_AT_SHUTDOWN
        // NOTE: to see really interesting stuff turn on the prlog stuff.
        // See the comment at the top of XPCLog.h to see how to do that.
        xpc->DebugDump(7);
#endif
        nsrefcnt cnt;
        NS_RELEASE2(xpc, cnt);
#ifdef XPC_DUMP_AT_SHUTDOWN
        if (0 != cnt)
            printf("*** dangling reference to nsXPConnect: refcnt=%d\n", cnt);
        else
            printf("+++ XPConnect had no dangling references.\n");
#endif
    }
}

// static
nsresult
nsXPConnect::GetInterfaceInfoManager(nsIInterfaceInfoSuperManager** iim,
                                     nsXPConnect* xpc /*= nsnull*/)
{
    if (!xpc && !(xpc = GetXPConnect()))
        return NS_ERROR_FAILURE;

    *iim = xpc->mInterfaceInfoManager;
    NS_IF_ADDREF(*iim);
    return NS_OK;
}

// static
XPCJSRuntime*
nsXPConnect::GetRuntimeInstance()
{
    nsXPConnect* xpc = GetXPConnect();
    NS_ASSERTION(xpc, "Must not be called if XPC failed to initialize");
    return xpc->GetRuntime();
}

// static
JSBool
nsXPConnect::IsISupportsDescendant(nsIInterfaceInfo* info)
{
    bool found = false;
    if (info)
        info->HasAncestor(&NS_GET_IID(nsISupports), &found);
    return found;
}

/***************************************************************************/

typedef bool (*InfoTester)(nsIInterfaceInfoManager* manager, const void* data,
                           nsIInterfaceInfo** info);

static bool IIDTester(nsIInterfaceInfoManager* manager, const void* data,
                      nsIInterfaceInfo** info)
{
    return NS_SUCCEEDED(manager->GetInfoForIID((const nsIID *) data, info)) &&
           *info;
}

static bool NameTester(nsIInterfaceInfoManager* manager, const void* data,
                       nsIInterfaceInfo** info)
{
    return NS_SUCCEEDED(manager->GetInfoForName((const char *) data, info)) &&
           *info;
}

static nsresult FindInfo(InfoTester tester, const void* data,
                         nsIInterfaceInfoSuperManager* iism,
                         nsIInterfaceInfo** info)
{
    if (tester(iism, data, info))
        return NS_OK;

    // If not found, then let's ask additional managers.

    bool yes;
    nsCOMPtr<nsISimpleEnumerator> list;

    if (NS_SUCCEEDED(iism->HasAdditionalManagers(&yes)) && yes &&
        NS_SUCCEEDED(iism->EnumerateAdditionalManagers(getter_AddRefs(list))) &&
        list) {
        bool more;
        nsCOMPtr<nsIInterfaceInfoManager> current;

        while (NS_SUCCEEDED(list->HasMoreElements(&more)) && more &&
               NS_SUCCEEDED(list->GetNext(getter_AddRefs(current))) && current) {
            if (tester(current, data, info))
                return NS_OK;
        }
    }

    return NS_ERROR_NO_INTERFACE;
}

nsresult
nsXPConnect::GetInfoForIID(const nsIID * aIID, nsIInterfaceInfo** info)
{
    return FindInfo(IIDTester, aIID, mInterfaceInfoManager, info);
}

nsresult
nsXPConnect::GetInfoForName(const char * name, nsIInterfaceInfo** info)
{
    return FindInfo(NameTester, name, mInterfaceInfoManager, info);
}

bool
nsXPConnect::NeedCollect()
{
    return !!mNeedGCBeforeCC;
}

void
nsXPConnect::Collect(PRUint32 reason, PRUint32 kind)
{
    // We're dividing JS objects into 2 categories:
    //
    // 1. "real" roots, held by the JS engine itself or rooted through the root
    //    and lock JS APIs. Roots from this category are considered black in the
    //    cycle collector, any cycle they participate in is uncollectable.
    //
    // 2. roots held by C++ objects that participate in cycle collection,
    //    held by XPConnect (see XPCJSRuntime::TraceXPConnectRoots). Roots from
    //    this category are considered grey in the cycle collector, their final
    //    color depends on the objects that hold them.
    //
    // Note that if a root is in both categories it is the fact that it is in
    // category 1 that takes precedence, so it will be considered black.
    //
    // During garbage collection we switch to an additional mark color (gray)
    // when tracing inside TraceXPConnectRoots. This allows us to walk those
    // roots later on and add all objects reachable only from them to the
    // cycle collector.
    //
    // Phases:
    //
    // 1. marking of the roots in category 1 by having the JS GC do its marking
    // 2. marking of the roots in category 2 by XPCJSRuntime::TraceXPConnectRoots
    //    using an additional color (gray).
    // 3. end of GC, GC can sweep its heap
    //
    // At some later point, when the cycle collector runs:
    //
    // 4. walk gray objects and add them to the cycle collector, cycle collect
    //
    // JS objects that are part of cycles the cycle collector breaks will be
    // collected by the next JS.
    //
    // If DEBUG_CC is not defined the cycle collector will not traverse roots
    // from category 1 or any JS objects held by them. Any JS objects they hold
    // will already be marked by the JS GC and will thus be colored black
    // themselves. Any C++ objects they hold will have a missing (untraversed)
    // edge from the JS object to the C++ object and so it will be marked black
    // too. This decreases the number of objects that the cycle collector has to
    // deal with.
    // To improve debugging, if DEBUG_CC is defined all JS objects are
    // traversed.

    mNeedGCBeforeCC = false;

    XPCCallContext ccx(NATIVE_CALLER);
    if (!ccx.IsValid())
        return;

    JSContext *cx = ccx.GetJSContext();

    // We want to scan the current thread for GC roots only if it was in a
    // request prior to the Collect call to avoid false positives during the
    // cycle collection. So to compensate for JS_BeginRequest in
    // XPCCallContext::Init we disable the conservative scanner if that call
    // has started the request on this thread.
    js::AutoSkipConservativeScan ascs(cx);
    MOZ_ASSERT(reason < js::gcreason::NUM_REASONS);
    js::gcreason::Reason gcreason = (js::gcreason::Reason)reason;
    if (kind == nsGCShrinking) {
        js::ShrinkingGC(cx, gcreason);
    } else {
        MOZ_ASSERT(kind == nsGCNormal);
        js::GCForReason(cx, gcreason);
    }
}

NS_IMETHODIMP
nsXPConnect::GarbageCollect(PRUint32 reason, PRUint32 kind)
{
    Collect(reason, kind);
    return NS_OK;
}

#ifdef DEBUG_CC
struct NoteJSRootTracer : public JSTracer
{
    NoteJSRootTracer(PLDHashTable *aObjects,
                     nsCycleCollectionTraversalCallback& cb)
      : mObjects(aObjects),
        mCb(cb)
    {
    }
    PLDHashTable* mObjects;
    nsCycleCollectionTraversalCallback& mCb;
};

static void
NoteJSRoot(JSTracer *trc, void *thing, JSGCTraceKind kind)
{
    if (AddToCCKind(kind)) {
        NoteJSRootTracer *tracer = static_cast<NoteJSRootTracer*>(trc);
        PLDHashEntryHdr *entry = PL_DHashTableOperate(tracer->mObjects, thing,
                                                      PL_DHASH_ADD);
        if (entry && !reinterpret_cast<PLDHashEntryStub*>(entry)->key) {
            reinterpret_cast<PLDHashEntryStub*>(entry)->key = thing;
            tracer->mCb.NoteRoot(nsIProgrammingLanguage::JAVASCRIPT, thing,
                                 nsXPConnect::GetXPConnect());
        }
    } else if (kind != JSTRACE_STRING) {
        JS_TraceChildren(trc, thing, kind);
    }
}
#endif

struct NoteWeakMapChildrenTracer : public JSTracer
{
    NoteWeakMapChildrenTracer(nsCycleCollectionTraversalCallback &cb)
        : mCb(cb)
    {
    }
    nsCycleCollectionTraversalCallback &mCb;
    JSObject *mMap;
    void *mKey;
};

static void
TraceWeakMappingChild(JSTracer *trc, void *thing, JSGCTraceKind kind)
{
    JS_ASSERT(trc->callback == TraceWeakMappingChild);
    NoteWeakMapChildrenTracer *tracer =
        static_cast<NoteWeakMapChildrenTracer *>(trc);
    if (kind == JSTRACE_STRING)
        return;
    if (!xpc_IsGrayGCThing(thing) && !tracer->mCb.WantAllTraces())
        return;
    if (AddToCCKind(kind)) {
        tracer->mCb.NoteWeakMapping(tracer->mMap, tracer->mKey, thing);
    } else {
        JS_TraceChildren(trc, thing, kind);
    }
}

struct NoteWeakMapsTracer : public js::WeakMapTracer
{
    NoteWeakMapsTracer(JSContext *cx, js::WeakMapTraceCallback cb,
                       nsCycleCollectionTraversalCallback &cccb)
        : js::WeakMapTracer(cx, cb), mCb(cccb), mChildTracer(cccb)
    {
        JS_TracerInit(&mChildTracer, cx, TraceWeakMappingChild);
    }
    nsCycleCollectionTraversalCallback &mCb;
    NoteWeakMapChildrenTracer mChildTracer;
};

static void
TraceWeakMapping(js::WeakMapTracer *trc, JSObject *m, 
                 void *k, JSGCTraceKind kkind,
                 void *v, JSGCTraceKind vkind)
{
    JS_ASSERT(trc->callback == TraceWeakMapping);
    NoteWeakMapsTracer *tracer = static_cast<NoteWeakMapsTracer *>(trc);
    if (vkind == JSTRACE_STRING)
        return;
    if (!xpc_IsGrayGCThing(v) && !tracer->mCb.WantAllTraces())
        return;

    // The cycle collector can only properly reason about weak maps if it can
    // reason about the liveness of their keys, which in turn requires that
    // the key can be represented in the cycle collector graph.  All existing
    // uses of weak maps use either objects or scripts as keys, which are okay.
    JS_ASSERT(AddToCCKind(kkind));

    // As an emergency fallback for non-debug builds, if the key is not
    // representable in the cycle collector graph, we treat it as marked.  This
    // can cause leaks, but is preferable to ignoring the binding, which could
    // cause the cycle collector to free live objects.
    if (!AddToCCKind(kkind))
        k = nsnull;

    if (AddToCCKind(vkind)) {
        tracer->mCb.NoteWeakMapping(m, k, v);
    } else {
        tracer->mChildTracer.mMap = m;
        tracer->mChildTracer.mKey = k;
        JS_TraceChildren(&tracer->mChildTracer, v, vkind);
    }
}

nsresult
nsXPConnect::BeginCycleCollection(nsCycleCollectionTraversalCallback &cb,
                                  bool explainLiveExpectedGarbage)
{
    // It is important not to call GetSafeJSContext while on the
    // cycle-collector thread since this context will be destroyed
    // asynchronously and race with the main thread. In particular, we must
    // ensure that a context is passed to the XPCCallContext constructor.
    JSContext *cx = mRuntime->GetJSCycleCollectionContext();
    if (!cx)
        return NS_ERROR_OUT_OF_MEMORY;

    NS_ASSERTION(!mCycleCollectionContext, "Didn't call FinishTraverse?");
    mCycleCollectionContext = new XPCCallContext(NATIVE_CALLER, cx);
    if (!mCycleCollectionContext->IsValid()) {
        mCycleCollectionContext = nsnull;
        return NS_ERROR_FAILURE;
    }

    static bool gcHasRun = false;
    if (!gcHasRun) {
        JSRuntime* rt = JS_GetRuntime(mCycleCollectionContext->GetJSContext());
        if (!rt)
            NS_RUNTIMEABORT("Failed to get JS runtime!");
        uint32_t gcNumber = JS_GetGCParameter(rt, JSGC_NUMBER);
        if (!gcNumber)
            NS_RUNTIMEABORT("Cannot cycle collect if GC has not run first!");
        gcHasRun = true;
    }

#ifdef DEBUG_CC
    NS_ASSERTION(!mJSRoots.ops, "Didn't call FinishCycleCollection?");

    if (explainLiveExpectedGarbage) {
        // Being called from nsCycleCollector::ExplainLiveExpectedGarbage.

        // Record all objects held by the JS runtime. This avoids doing a
        // complete GC if we're just tracing to explain (from
        // ExplainLiveExpectedGarbage), which makes the results of cycle
        // collection identical for DEBUG_CC and non-DEBUG_CC builds.
        if (!PL_DHashTableInit(&mJSRoots, PL_DHashGetStubOps(), nsnull,
                               sizeof(PLDHashEntryStub), PL_DHASH_MIN_SIZE)) {
            mJSRoots.ops = nsnull;

            return NS_ERROR_OUT_OF_MEMORY;
        }

        NoteJSRootTracer trc(&mJSRoots, cb);
        JS_TracerInit(&trc, mCycleCollectionContext->GetJSContext(), NoteJSRoot);
        JS_TraceRuntime(&trc);
    }
#else
    NS_ASSERTION(!explainLiveExpectedGarbage, "Didn't call nsXPConnect::Collect()?");
#endif

    GetRuntime()->AddXPConnectRoots(mCycleCollectionContext->GetJSContext(), cb);
 
    NoteWeakMapsTracer trc(mCycleCollectionContext->GetJSContext(),
                           TraceWeakMapping, cb);
    js::TraceWeakMaps(&trc);

    return NS_OK;
}

bool
nsXPConnect::NotifyLeaveMainThread()
{
    NS_ABORT_IF_FALSE(NS_IsMainThread(), "Off main thread");
    JSRuntime *rt = mRuntime->GetJSRuntime();
    if (JS_IsInRequest(rt) || JS_IsInSuspendedRequest(rt))
        return false;
    JS_ClearRuntimeThread(rt);
    return true;
}

void
nsXPConnect::NotifyEnterCycleCollectionThread()
{
    NS_ABORT_IF_FALSE(!NS_IsMainThread(), "On main thread");
    JS_SetRuntimeThread(mRuntime->GetJSRuntime());
}

void
nsXPConnect::NotifyLeaveCycleCollectionThread()
{
    NS_ABORT_IF_FALSE(!NS_IsMainThread(), "On main thread");
    JS_ClearRuntimeThread(mRuntime->GetJSRuntime());
}

void
nsXPConnect::NotifyEnterMainThread()
{
    NS_ABORT_IF_FALSE(NS_IsMainThread(), "Off main thread");
    JS_SetRuntimeThread(mRuntime->GetJSRuntime());
}

nsresult
nsXPConnect::FinishTraverse()
{
    if (mCycleCollectionContext)
        mCycleCollectionContext = nsnull;
    return NS_OK;
}

nsresult
nsXPConnect::FinishCycleCollection()
{
#ifdef DEBUG_CC
    if (mJSRoots.ops) {
        PL_DHashTableFinish(&mJSRoots);
        mJSRoots.ops = nsnull;
    }
#endif

    return NS_OK;
}

nsCycleCollectionParticipant *
nsXPConnect::ToParticipant(void *p)
{
    if (!AddToCCKind(js_GetGCThingTraceKind(p)))
        return NULL;
    return this;
}

NS_IMETHODIMP
nsXPConnect::Root(void *p)
{
    return NS_OK;
}

#ifdef DEBUG_CC
void
nsXPConnect::PrintAllReferencesTo(void *p)
{
#ifdef DEBUG
    XPCCallContext ccx(NATIVE_CALLER);
    if (ccx.IsValid())
        JS_DumpHeap(ccx.GetJSContext(), stdout, nsnull, 0, p,
                    0x7fffffff, nsnull);
#endif
}
#endif

NS_IMETHODIMP
nsXPConnect::Unlink(void *p)
{
    return NS_OK;
}

NS_IMETHODIMP
nsXPConnect::Unroot(void *p)
{
    return NS_OK;
}

JSBool
xpc_GCThingIsGrayCCThing(void *thing)
{
    return AddToCCKind(js_GetGCThingTraceKind(thing)) &&
           xpc_IsGrayGCThing(thing);
}

/*
 * The GC and CC are run independently. Consequently, the following sequence of
 * events can occur:
 * 1. GC runs and marks an object gray.
 * 2. Some JS code runs that creates a pointer from a JS root to the gray
 *    object. If we re-ran a GC at this point, the object would now be black.
 * 3. Now we run the CC. It may think it can collect the gray object, even
 *    though it's reachable from the JS heap.
 *
 * To prevent this badness, we unmark the gray bit of an object when it is
 * accessed by callers outside XPConnect. This would cause the object to go
 * black in step 2 above. This must be done on everything reachable from the
 * object being returned. The following code takes care of the recursive
 * re-coloring.
 */
static void
UnmarkGrayChildren(JSTracer *trc, void *thing, JSGCTraceKind kind)
{
    int stackDummy;
    if (!JS_CHECK_STACK_SIZE(js::GetContextStackLimit(trc->context), &stackDummy)) {
        /*
         * If we run out of stack, we take a more drastic measure: require that
         * we GC again before the next CC.
         */
        nsXPConnect* xpc = nsXPConnect::GetXPConnect();
        xpc->EnsureGCBeforeCC();
        return;
    }

    // If this thing is not a CC-kind or already non-gray then we're done.
    if (!AddToCCKind(kind) || !xpc_IsGrayGCThing(thing))
        return;

    // Unmark.
    static_cast<js::gc::Cell *>(thing)->unmark(js::gc::GRAY);

    // Trace children.
    JS_TraceChildren(trc, thing, kind);
}

void
xpc_UnmarkGrayObjectRecursive(JSObject *obj)
{
    NS_ASSERTION(obj, "Don't pass me null!");

    // Unmark.
    js::gc::AsCell(obj)->unmark(js::gc::GRAY);

    // Tracing requires a JSContext...
    JSContext *cx;
    nsXPConnect* xpc = nsXPConnect::GetXPConnect();
    if (!xpc || NS_FAILED(xpc->GetSafeJSContext(&cx)) || !cx) {
        NS_ERROR("Failed to get safe JSContext!");
        return;
    }

    // Trace children.
    JSTracer trc;
    JS_TracerInit(&trc, cx, UnmarkGrayChildren);
    JS_TraceChildren(&trc, obj, JSTRACE_OBJECT);
}

struct TraversalTracer : public JSTracer
{
    TraversalTracer(nsCycleCollectionTraversalCallback &aCb) : cb(aCb)
    {
    }
    nsCycleCollectionTraversalCallback &cb;
};

static void
NoteJSChild(JSTracer *trc, void *thing, JSGCTraceKind kind)
{
    TraversalTracer *tracer = static_cast<TraversalTracer*>(trc);

    // Don't traverse non-gray objects, unless we want all traces.
    if (!xpc_IsGrayGCThing(thing) && !tracer->cb.WantAllTraces())
        return;

    /*
     * This function needs to be careful to avoid stack overflow. Normally, when
     * AddToCCKind is true, the recursion terminates immediately as we just add
     * |thing| to the CC graph. So overflow is only possible when there are long
     * chains of non-AddToCCKind GC things. Currently, this only can happen via
     * shape parent pointers. The special JSTRACE_SHAPE case below handles
     * parent pointers iteratively, rather than recursively, to avoid overflow.
     */
    if (AddToCCKind(kind)) {
#if defined(DEBUG)
        if (NS_UNLIKELY(tracer->cb.WantDebugInfo())) {
            // based on DumpNotify in jsapi.c
            if (tracer->debugPrinter) {
                char buffer[200];
                tracer->debugPrinter(trc, buffer, sizeof(buffer));
                tracer->cb.NoteNextEdgeName(buffer);
            } else if (tracer->debugPrintIndex != (size_t)-1) {
                char buffer[200];
                JS_snprintf(buffer, sizeof(buffer), "%s[%lu]",
                            static_cast<const char *>(tracer->debugPrintArg),
                            tracer->debugPrintIndex);
                tracer->cb.NoteNextEdgeName(buffer);
            } else {
                tracer->cb.NoteNextEdgeName(static_cast<const char*>(tracer->debugPrintArg));
            }
        }
#endif
        tracer->cb.NoteScriptChild(nsIProgrammingLanguage::JAVASCRIPT, thing);
    } else if (kind == JSTRACE_SHAPE) {
        JS_TraceShapeCycleCollectorChildren(trc, thing);
    } else if (kind != JSTRACE_STRING) {
        JS_TraceChildren(trc, thing, kind);
    }
}

void
xpc_MarkInCCGeneration(nsISupports* aVariant, PRUint32 aGeneration)
{
    nsCOMPtr<XPCVariant> variant = do_QueryInterface(aVariant);
    if (variant) {
        variant->SetCCGeneration(aGeneration);
        variant->GetJSVal(); // Unmarks gray JSObject.
        XPCVariant* weak = variant.get();
        variant = nsnull;
        if (weak->IsPurple()) {
          weak->RemovePurple();
        }
    }
}

void
xpc_UnmarkGrayObject(nsIXPConnectWrappedJS* aWrappedJS)
{
    if (aWrappedJS) {
        // Unmarks gray JSObject.
        static_cast<nsXPCWrappedJS*>(aWrappedJS)->GetJSObject();
    }
}

static JSBool
WrapperIsNotMainThreadOnly(XPCWrappedNative *wrapper)
{
    XPCWrappedNativeProto *proto = wrapper->GetProto();
    if (proto && proto->ClassIsMainThreadOnly())
        return false;

    // If the native participates in cycle collection then we know it can only
    // be used on the main thread, in that case we assume the wrapped native
    // can only be used on the main thread too.
    nsXPCOMCycleCollectionParticipant* participant;
    return NS_FAILED(CallQueryInterface(wrapper->Native(), &participant));
}

NS_IMETHODIMP
nsXPConnect::Traverse(void *p, nsCycleCollectionTraversalCallback &cb)
{
    JSContext *cx = mCycleCollectionContext->GetJSContext();

    JSGCTraceKind traceKind = js_GetGCThingTraceKind(p);
    JSObject *obj = nsnull;
    js::Class *clazz = nsnull;

    // We do not want to add wrappers to the cycle collector if they're not
    // explicitly marked as main thread only, because the cycle collector isn't
    // able to deal with objects that might be used off of the main thread. We
    // do want to explicitly mark them for cycle collection if the wrapper has
    // an external reference, because the wrapper would mark the JS object if
    // we did add the wrapper to the cycle collector.
    JSBool dontTraverse = false;
    JSBool markJSObject = false;
    if (traceKind == JSTRACE_OBJECT) {
        obj = static_cast<JSObject*>(p);
        clazz = js::GetObjectClass(obj);

        if (clazz == &XPC_WN_Tearoff_JSClass) {
            XPCWrappedNative *wrapper =
                (XPCWrappedNative*)xpc_GetJSPrivate(js::GetObjectParent(obj));
            dontTraverse = WrapperIsNotMainThreadOnly(wrapper);
        } else if (IS_WRAPPER_CLASS(clazz) && IS_WN_WRAPPER_OBJECT(obj)) {
            XPCWrappedNative *wrapper = (XPCWrappedNative*)xpc_GetJSPrivate(obj);
            dontTraverse = WrapperIsNotMainThreadOnly(wrapper);
            markJSObject = dontTraverse && wrapper->HasExternalReference();
        }
    }

    bool isMarked;

#ifdef DEBUG_CC
    // Note that the conditions under which we specify GCMarked vs.
    // GCUnmarked are different between ExplainLiveExpectedGarbage and
    // the normal case.  In the normal case, we're saying that anything
    // reachable from a JS runtime root is itself such a root.  This
    // doesn't actually break anything; it really just does some of the
    // cycle collector's work for it.  However, when debugging, we
    // (1) actually need to know what the root is and (2) don't want to
    // do an extra GC, so we use mJSRoots, built from JS_TraceRuntime,
    // which produces a different result because we didn't call
    // JS_TraceChildren to trace everything that was reachable.
    if (mJSRoots.ops) {
        // ExplainLiveExpectedGarbage codepath
        PLDHashEntryHdr* entry =
            PL_DHashTableOperate(&mJSRoots, p, PL_DHASH_LOOKUP);
        isMarked = markJSObject || PL_DHASH_ENTRY_IS_BUSY(entry);
    } else
#endif
    {
        // Normal codepath (matches non-DEBUG_CC codepath).
        isMarked = markJSObject || !xpc_IsGrayGCThing(p);
    }

    if (cb.WantDebugInfo()) {
        char name[72];
        if (traceKind == JSTRACE_OBJECT) {
            XPCNativeScriptableInfo* si = nsnull;
            if (IS_PROTO_CLASS(clazz)) {
                XPCWrappedNativeProto* p =
                    (XPCWrappedNativeProto*) xpc_GetJSPrivate(obj);
                si = p->GetScriptableInfo();
            }
            if (si) {
                JS_snprintf(name, sizeof(name), "JS Object (%s - %s)",
                            clazz->name, si->GetJSClass()->name);
            } else if (clazz == &js::FunctionClass) {
                JSFunction* fun = JS_GetObjectFunction(obj);
                JSString* str = JS_GetFunctionId(fun);
                if (str) {
                    NS_ConvertUTF16toUTF8 fname(JS_GetInternedStringChars(str));
                    JS_snprintf(name, sizeof(name),
                                "JS Object (Function - %s)", fname.get());
                } else {
                    JS_snprintf(name, sizeof(name), "JS Object (Function)");
                }
            } else {
                JS_snprintf(name, sizeof(name), "JS Object (%s)",
                            clazz->name);
            }
        } else {
            static const char trace_types[][11] = {
                "Object",
                "String",
                "Script",
                "Xml",
                "Shape",
                "BaseShape",
                "TypeObject",
            };
            JS_STATIC_ASSERT(NS_ARRAY_LENGTH(trace_types) == JSTRACE_LAST + 1);
            JS_snprintf(name, sizeof(name), "JS %s", trace_types[traceKind]);
        }

        // Disable printing global for objects while we figure out ObjShrink fallout.
        cb.DescribeGCedNode(isMarked, sizeof(js::shadow::Object), name);
    } else {
        cb.DescribeGCedNode(isMarked, sizeof(js::shadow::Object), "JS Object");
    }

    // There's no need to trace objects that have already been marked by the JS
    // GC. Any JS objects hanging from them will already be marked. Only do this
    // if DEBUG_CC is not defined, else we do want to know about all JS objects
    // to get better graphs and explanations.
    if (!cb.WantAllTraces() && isMarked)
        return NS_OK;

    TraversalTracer trc(cb);

    JS_TracerInit(&trc, cx, NoteJSChild);
    trc.eagerlyTraceWeakMaps = false;
    JS_TraceChildren(&trc, p, traceKind);

    if (traceKind != JSTRACE_OBJECT || dontTraverse)
        return NS_OK;

    if (clazz == &XPC_WN_Tearoff_JSClass) {
        // A tearoff holds a strong reference to its native object
        // (see XPCWrappedNative::FlatJSObjectFinalized). Its XPCWrappedNative
        // will be held alive through the parent of the JSObject of the tearoff.
        XPCWrappedNativeTearOff *to =
            (XPCWrappedNativeTearOff*) xpc_GetJSPrivate(obj);
        NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "xpc_GetJSPrivate(obj)->mNative");
        cb.NoteXPCOMChild(to->GetNative());
    }
    // XXX This test does seem fragile, we should probably whitelist classes
    //     that do hold a strong reference, but that might not be possible.
    else if (clazz->flags & JSCLASS_HAS_PRIVATE &&
             clazz->flags & JSCLASS_PRIVATE_IS_NSISUPPORTS) {
        NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "xpc_GetJSPrivate(obj)");
        cb.NoteXPCOMChild(static_cast<nsISupports*>(xpc_GetJSPrivate(obj)));
    } else if (mozilla::dom::binding::instanceIsProxy(obj)) {
        NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "js::GetProxyPrivate(obj)");
        nsISupports *identity =
            static_cast<nsISupports*>(js::GetProxyPrivate(obj).toPrivate());
        cb.NoteXPCOMChild(identity);
    }

    return NS_OK;
}

unsigned
nsXPConnect::GetOutstandingRequests(JSContext* cx)
{
    unsigned n = js::GetContextOutstandingRequests(cx);
    XPCCallContext* context = mCycleCollectionContext;
    // Ignore the contribution from the XPCCallContext we created for cycle
    // collection.
    if (context && cx == context->GetJSContext()) {
        JS_ASSERT(n);
        --n;
    }
    return n;
}

class JSContextParticipant : public nsCycleCollectionParticipant
{
public:
    NS_IMETHOD Root(void *n)
    {
        return NS_OK;
    }
    NS_IMETHOD Unlink(void *n)
    {
        JSContext *cx = static_cast<JSContext*>(n);
        JSAutoRequest ar(cx);
        NS_ASSERTION(JS_GetGlobalObject(cx), "global object NULL before unlinking");
        JS_SetGlobalObject(cx, NULL);
        return NS_OK;
    }
    NS_IMETHOD Unroot(void *n)
    {
        return NS_OK;
    }
    NS_IMETHODIMP Traverse(void *n, nsCycleCollectionTraversalCallback &cb)
    {
        JSContext *cx = static_cast<JSContext*>(n);

        // Add outstandingRequests to the count, if there are outstanding
        // requests the context needs to be kept alive and adding unknown
        // edges will ensure that any cycles this context is in won't be
        // collected.
        unsigned refCount = nsXPConnect::GetXPConnect()->GetOutstandingRequests(cx) + 1;
        cb.DescribeRefCountedNode(refCount, js::SizeOfJSContext(), "JSContext");
        if (JSObject *global = JS_GetGlobalObject(cx)) {
            NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "[global object]");
            cb.NoteScriptChild(nsIProgrammingLanguage::JAVASCRIPT, global);
        }

        return NS_OK;
    }
};

static JSContextParticipant JSContext_cycleCollectorGlobal;

// static
nsCycleCollectionParticipant*
nsXPConnect::JSContextParticipant()
{
    return &JSContext_cycleCollectorGlobal;
}

NS_IMETHODIMP_(void)
nsXPConnect::NoteJSContext(JSContext *aJSContext,
                           nsCycleCollectionTraversalCallback &aCb)
{
    aCb.NoteNativeChild(aJSContext, &JSContext_cycleCollectorGlobal);
}


/***************************************************************************/
/***************************************************************************/
// nsIXPConnect interface methods...

inline nsresult UnexpectedFailure(nsresult rv)
{
    NS_ERROR("This is not supposed to fail!");
    return rv;
}

/* void initClasses (in JSContextPtr aJSContext, in JSObjectPtr aGlobalJSObj); */
NS_IMETHODIMP
nsXPConnect::InitClasses(JSContext * aJSContext, JSObject * aGlobalJSObj)
{
    NS_ASSERTION(aJSContext, "bad param");
    NS_ASSERTION(aGlobalJSObj, "bad param");

    // Nest frame chain save/restore in request created by XPCCallContext.
    XPCCallContext ccx(NATIVE_CALLER, aJSContext);
    if (!ccx.IsValid())
        return UnexpectedFailure(NS_ERROR_FAILURE);

    JSAutoEnterCompartment ac;
    if (!ac.enter(ccx, aGlobalJSObj))
        return UnexpectedFailure(NS_ERROR_FAILURE);

    xpc_InitJSxIDClassObjects();

    XPCWrappedNativeScope* scope =
        XPCWrappedNativeScope::GetNewOrUsed(ccx, aGlobalJSObj);

    if (!scope)
        return UnexpectedFailure(NS_ERROR_FAILURE);

    scope->RemoveWrappedNativeProtos();

    if (!nsXPCComponents::AttachNewComponentsObject(ccx, scope, aGlobalJSObj))
        return UnexpectedFailure(NS_ERROR_FAILURE);

    if (XPCPerThreadData::IsMainThread(ccx)) {
        if (!XPCNativeWrapper::AttachNewConstructorObject(ccx, aGlobalJSObj))
            return UnexpectedFailure(NS_ERROR_FAILURE);
    }

    return NS_OK;
}

static JSBool
TempGlobalResolve(JSContext *aJSContext, JSObject *obj, jsid id)
{
    JSBool resolved;
    return JS_ResolveStandardClass(aJSContext, obj, id, &resolved);
}

static JSClass xpcTempGlobalClass = {
    "xpcTempGlobalClass", JSCLASS_GLOBAL_FLAGS,
    JS_PropertyStub,  JS_PropertyStub,  JS_PropertyStub,  JS_StrictPropertyStub,
    JS_EnumerateStub, TempGlobalResolve, JS_ConvertStub,   nsnull,
    JSCLASS_NO_OPTIONAL_MEMBERS
};

static bool
CreateNewCompartment(JSContext *cx, JSClass *clasp, nsIPrincipal *principal,
                     xpc::CompartmentPrivate *priv, JSObject **global,
                     JSCompartment **compartment)
{
    // We take ownership of |priv|. Ensure that either we free it in the case
    // of failure or give ownership to the compartment in case of success (in
    // that case it will be free'd in CompartmentCallback during GC).
    nsAutoPtr<xpc::CompartmentPrivate> priv_holder(priv);
    JSPrincipals *principals = nsnull;
    if (principal)
        principal->GetJSPrincipals(cx, &principals);
    JSObject *tempGlobal = JS_NewCompartmentAndGlobalObject(cx, clasp, principals);
    if (principals)
        JSPRINCIPALS_DROP(cx, principals);

    if (!tempGlobal)
        return false;

    *global = tempGlobal;
    *compartment = js::GetObjectCompartment(tempGlobal);

    js::AutoSwitchCompartment sc(cx, *compartment);
    JS_SetCompartmentPrivate(cx, *compartment, priv_holder.forget());
    return true;
}

#ifdef DEBUG
struct VerifyTraceXPCGlobalCalledTracer
{
    JSTracer base;
    bool ok;
};

static void
VerifyTraceXPCGlobalCalled(JSTracer *trc, void *thing, JSGCTraceKind kind)
{
    // We don't do anything here, we only want to verify that TraceXPCGlobal
    // was called.
}
#endif

void
TraceXPCGlobal(JSTracer *trc, JSObject *obj)
{
#ifdef DEBUG
    if (trc->callback == VerifyTraceXPCGlobalCalled) {
        // We don't do anything here, we only want to verify that TraceXPCGlobal
        // was called.
        reinterpret_cast<VerifyTraceXPCGlobalCalledTracer*>(trc)->ok = true;
        return;
    }
#endif

    XPCWrappedNativeScope *scope =
        XPCWrappedNativeScope::GetNativeScope(trc->context, obj);
    if (scope)
        scope->TraceDOMPrototypes(trc);
}

nsresult
xpc_CreateGlobalObject(JSContext *cx, JSClass *clasp,
                       nsIPrincipal *principal, nsISupports *ptr,
                       bool wantXrays, JSObject **global,
                       JSCompartment **compartment)
{
    NS_ABORT_IF_FALSE(NS_IsMainThread(), "using a principal off the main thread?");
    NS_ABORT_IF_FALSE(principal, "bad key");

    XPCCompartmentMap& map = nsXPConnect::GetRuntimeInstance()->GetCompartmentMap();
    xpc::PtrAndPrincipalHashKey key(ptr, principal);
    if (!map.Get(&key, compartment)) {
        xpc::PtrAndPrincipalHashKey *priv_key =
            new xpc::PtrAndPrincipalHashKey(ptr, principal);
        xpc::CompartmentPrivate *priv =
            new xpc::CompartmentPrivate(priv_key, wantXrays, NS_IsMainThread());
        if (!CreateNewCompartment(cx, clasp, principal, priv,
                                  global, compartment)) {
            return UnexpectedFailure(NS_ERROR_FAILURE);
        }

        map.Put(&key, *compartment);
    } else {
        js::AutoSwitchCompartment sc(cx, *compartment);

        JSObject *tempGlobal = JS_NewGlobalObject(cx, clasp);
        if (!tempGlobal)
            return UnexpectedFailure(NS_ERROR_FAILURE);
        *global = tempGlobal;
    }

#ifdef DEBUG
    if (clasp->flags & JSCLASS_XPCONNECT_GLOBAL) {
        VerifyTraceXPCGlobalCalledTracer trc;
        JS_TracerInit(&trc.base, cx, VerifyTraceXPCGlobalCalled);
        trc.ok = false;
        JS_TraceChildren(&trc.base, *global, JSTRACE_OBJECT);
        NS_ABORT_IF_FALSE(trc.ok, "Trace hook needs to call TraceXPCGlobal if JSCLASS_XPCONNECT_GLOBAL is set.");
    }
#endif

    return NS_OK;
}

nsresult
xpc_CreateMTGlobalObject(JSContext *cx, JSClass *clasp,
                         nsISupports *ptr, JSObject **global,
                         JSCompartment **compartment)
{
    // NB: We can be either on or off the main thread here.
    XPCMTCompartmentMap& map = nsXPConnect::GetRuntimeInstance()->GetMTCompartmentMap();
    if (!map.Get(ptr, compartment)) {
        // We allow the pointer to be a principal, in which case it becomes
        // the principal for the newly created compartment. The caller is
        // responsible for ensuring that doing this doesn't violate
        // threadsafety assumptions.
        nsCOMPtr<nsIPrincipal> principal(do_QueryInterface(ptr));
        xpc::CompartmentPrivate *priv =
            new xpc::CompartmentPrivate(ptr, false, NS_IsMainThread());
        if (!CreateNewCompartment(cx, clasp, principal, priv, global,
                                  compartment)) {
            return UnexpectedFailure(NS_ERROR_UNEXPECTED);
        }

        map.Put(ptr, *compartment);
    } else {
        js::AutoSwitchCompartment sc(cx, *compartment);

        JSObject *tempGlobal = JS_NewGlobalObject(cx, clasp);
        if (!tempGlobal)
            return UnexpectedFailure(NS_ERROR_FAILURE);
        *global = tempGlobal;
    }

    return NS_OK;
}

NS_IMETHODIMP
nsXPConnect::InitClassesWithNewWrappedGlobal(JSContext * aJSContext,
                                             nsISupports *aCOMObj,
                                             const nsIID & aIID,
                                             nsIPrincipal * aPrincipal,
                                             nsISupports * aExtraPtr,
                                             PRUint32 aFlags,
                                             nsIXPConnectJSObjectHolder **_retval)
{
    NS_ASSERTION(aJSContext, "bad param");
    NS_ASSERTION(aCOMObj, "bad param");
    NS_ASSERTION(_retval, "bad param");
    NS_ASSERTION(aExtraPtr || aPrincipal, "must be able to find a compartment");

    // XXX This is not pretty. We make a temporary global object and
    // init it with all the Components object junk just so we have a
    // parent with an xpc scope to use when wrapping the object that will
    // become the 'real' global.

    XPCCallContext ccx(NATIVE_CALLER, aJSContext);

    JSCompartment* compartment;
    JSObject* tempGlobal;

    nsresult rv = aPrincipal
                  ? xpc_CreateGlobalObject(ccx, &xpcTempGlobalClass, aPrincipal,
                                           aExtraPtr, false, &tempGlobal,
                                           &compartment)
                  : xpc_CreateMTGlobalObject(ccx, &xpcTempGlobalClass,
                                             aExtraPtr, &tempGlobal,
                                             &compartment);
    NS_ENSURE_SUCCESS(rv, rv);

    JSAutoEnterCompartment ac;
    if (!ac.enter(ccx, tempGlobal))
        return UnexpectedFailure(NS_ERROR_FAILURE);
    ccx.SetScopeForNewJSObjects(tempGlobal);

    bool system = (aFlags & nsIXPConnect::FLAG_SYSTEM_GLOBAL_OBJECT) != 0;
    if (system && !JS_MakeSystemObject(aJSContext, tempGlobal))
        return UnexpectedFailure(NS_ERROR_FAILURE);

    jsval v;
    nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
    {
        // Scope for our auto-marker; it just needs to keep tempGlobal alive
        // long enough for InitClasses and WrapNative to do their work
        AUTO_MARK_JSVAL(ccx, OBJECT_TO_JSVAL(tempGlobal));

        if (NS_FAILED(InitClasses(aJSContext, tempGlobal)))
            return UnexpectedFailure(NS_ERROR_FAILURE);

        nsresult rv;
        xpcObjectHelper helper(aCOMObj);
        if (!XPCConvert::NativeInterface2JSObject(ccx, &v,
                                                  getter_AddRefs(holder),
                                                  helper, &aIID, nsnull,
                                                  false, OBJ_IS_GLOBAL, &rv))
            return UnexpectedFailure(rv);

        NS_ASSERTION(NS_SUCCEEDED(rv) && holder, "Didn't wrap properly");
    }

    JSObject* globalJSObj = JSVAL_TO_OBJECT(v);
    if (!globalJSObj)
        return UnexpectedFailure(NS_ERROR_FAILURE);

    if (aFlags & nsIXPConnect::FLAG_SYSTEM_GLOBAL_OBJECT)
        NS_ASSERTION(JS_IsSystemObject(aJSContext, globalJSObj), "huh?!");

    // voodoo to fixup scoping and parenting...

    JS_ASSERT(!js::GetObjectParent(globalJSObj));

    JSObject* oldGlobal = JS_GetGlobalObject(aJSContext);
    if (!oldGlobal || oldGlobal == tempGlobal)
        JS_SetGlobalObject(aJSContext, globalJSObj);

    if ((aFlags & nsIXPConnect::INIT_JS_STANDARD_CLASSES) &&
        !JS_InitStandardClasses(aJSContext, globalJSObj))
        return UnexpectedFailure(NS_ERROR_FAILURE);

    XPCWrappedNative* wrapper =
        reinterpret_cast<XPCWrappedNative*>(holder.get());
    XPCWrappedNativeScope* scope = wrapper->GetScope();

    if (!scope)
        return UnexpectedFailure(NS_ERROR_FAILURE);

    // Note: This call cooperates with a call to wrapper->RefreshPrototype()
    // in nsJSEnvironment::SetOuterObject in order to ensure that the
    // prototype defines its constructor on the right global object.
    if (wrapper->GetProto()->GetScriptableInfo())
        scope->RemoveWrappedNativeProtos();

    NS_ASSERTION(scope->GetGlobalJSObject() == tempGlobal, "stealing scope!");

    scope->SetGlobal(ccx, globalJSObj);

    JSObject* protoJSObject = wrapper->HasProto() ?
                                    wrapper->GetProto()->GetJSProtoObject() :
                                    globalJSObj;
    if (protoJSObject) {
        if (protoJSObject != globalJSObj)
            JS_SetParent(aJSContext, protoJSObject, globalJSObj);
        if (!JS_SplicePrototype(aJSContext, protoJSObject, scope->GetPrototypeJSObject()))
            return UnexpectedFailure(NS_ERROR_FAILURE);
    }

    if (!(aFlags & nsIXPConnect::OMIT_COMPONENTS_OBJECT)) {
        // XPCCallContext gives us an active request needed to save/restore.
        if (!nsXPCComponents::AttachNewComponentsObject(ccx, scope, globalJSObj))
            return UnexpectedFailure(NS_ERROR_FAILURE);

        if (XPCPerThreadData::IsMainThread(ccx)) {
            if (!XPCNativeWrapper::AttachNewConstructorObject(ccx, globalJSObj))
                return UnexpectedFailure(NS_ERROR_FAILURE);
        }
    }

    NS_ADDREF(*_retval = holder);

    return NS_OK;
}

nsresult
xpc_MorphSlimWrapper(JSContext *cx, nsISupports *tomorph)
{
    nsWrapperCache *cache;
    CallQueryInterface(tomorph, &cache);
    if (!cache)
        return NS_OK;

    JSObject *obj = cache->GetWrapper();
    if (!obj || !IS_SLIM_WRAPPER(obj))
        return NS_OK;
    return MorphSlimWrapper(cx, obj);
}

static nsresult
NativeInterface2JSObject(XPCLazyCallContext & lccx,
                         JSObject * aScope,
                         nsISupports *aCOMObj,
                         nsWrapperCache *aCache,
                         const nsIID * aIID,
                         bool aAllowWrapping,
                         jsval *aVal,
                         nsIXPConnectJSObjectHolder **aHolder)
{
    JSAutoEnterCompartment ac;
    if (!ac.enter(lccx.GetJSContext(), aScope))
        return NS_ERROR_OUT_OF_MEMORY;

    lccx.SetScopeForNewJSObjects(aScope);

    nsresult rv;
    xpcObjectHelper helper(aCOMObj, aCache);
    if (!XPCConvert::NativeInterface2JSObject(lccx, aVal, aHolder, helper, aIID,
                                              nsnull, aAllowWrapping,
                                              OBJ_IS_NOT_GLOBAL, &rv))
        return rv;

#ifdef DEBUG
    NS_ASSERTION(aAllowWrapping ||
                 !xpc::WrapperFactory::IsXrayWrapper(JSVAL_TO_OBJECT(*aVal)),
                 "Shouldn't be returning a xray wrapper here");
#endif

    return NS_OK;
}

/* nsIXPConnectJSObjectHolder wrapNative (in JSContextPtr aJSContext, in JSObjectPtr aScope, in nsISupports aCOMObj, in nsIIDRef aIID); */
NS_IMETHODIMP
nsXPConnect::WrapNative(JSContext * aJSContext,
                        JSObject * aScope,
                        nsISupports *aCOMObj,
                        const nsIID & aIID,
                        nsIXPConnectJSObjectHolder **aHolder)
{
    NS_ASSERTION(aHolder, "bad param");
    NS_ASSERTION(aJSContext, "bad param");
    NS_ASSERTION(aScope, "bad param");
    NS_ASSERTION(aCOMObj, "bad param");

    XPCCallContext ccx(NATIVE_CALLER, aJSContext);
    if (!ccx.IsValid())
        return UnexpectedFailure(NS_ERROR_FAILURE);
    XPCLazyCallContext lccx(ccx);

    jsval v;
    return NativeInterface2JSObject(lccx, aScope, aCOMObj, nsnull, &aIID,
                                    false, &v, aHolder);
}

/* void wrapNativeToJSVal (in JSContextPtr aJSContext, in JSObjectPtr aScope, in nsISupports aCOMObj, in nsIIDPtr aIID, out jsval aVal, out nsIXPConnectJSObjectHolder aHolder); */
NS_IMETHODIMP
nsXPConnect::WrapNativeToJSVal(JSContext * aJSContext,
                               JSObject * aScope,
                               nsISupports *aCOMObj,
                               nsWrapperCache *aCache,
                               const nsIID * aIID,
                               bool aAllowWrapping,
                               jsval *aVal,
                               nsIXPConnectJSObjectHolder **aHolder)
{
    NS_ASSERTION(aJSContext, "bad param");
    NS_ASSERTION(aScope, "bad param");
    NS_ASSERTION(aCOMObj, "bad param");

    if (aHolder)
        *aHolder = nsnull;

    XPCLazyCallContext lccx(NATIVE_CALLER, aJSContext);

    return NativeInterface2JSObject(lccx, aScope, aCOMObj, aCache, aIID,
                                    aAllowWrapping, aVal, aHolder);
}

/* void wrapJS (in JSContextPtr aJSContext, in JSObjectPtr aJSObj, in nsIIDRef aIID, [iid_is (aIID), retval] out nsQIResult result); */
NS_IMETHODIMP
nsXPConnect::WrapJS(JSContext * aJSContext,
                    JSObject * aJSObj,
                    const nsIID & aIID,
                    void * *result)
{
    NS_ASSERTION(aJSContext, "bad param");
    NS_ASSERTION(aJSObj, "bad param");
    NS_ASSERTION(result, "bad param");

    *result = nsnull;

    XPCCallContext ccx(NATIVE_CALLER, aJSContext);
    if (!ccx.IsValid())
        return UnexpectedFailure(NS_ERROR_FAILURE);

    JSAutoEnterCompartment aec;

    nsresult rv = NS_ERROR_UNEXPECTED;
    if (!aec.enter(ccx, aJSObj) ||
        !XPCConvert::JSObject2NativeInterface(ccx, result, aJSObj,
                                              &aIID, nsnull, &rv))
        return rv;
    return NS_OK;
}

NS_IMETHODIMP
nsXPConnect::JSValToVariant(JSContext *cx,
                            jsval *aJSVal,
                            nsIVariant ** aResult)
{
    NS_PRECONDITION(aJSVal, "bad param");
    NS_PRECONDITION(aResult, "bad param");
    *aResult = nsnull;

    XPCCallContext ccx(NATIVE_CALLER, cx);
    if (!ccx.IsValid())
      return NS_ERROR_FAILURE;

    *aResult = XPCVariant::newVariant(ccx, *aJSVal);
    NS_ENSURE_TRUE(*aResult, NS_ERROR_OUT_OF_MEMORY);

    return NS_OK;
}

/* void wrapJSAggregatedToNative (in nsISupports aOuter, in JSContextPtr aJSContext, in JSObjectPtr aJSObj, in nsIIDRef aIID, [iid_is (aIID), retval] out nsQIResult result); */
NS_IMETHODIMP
nsXPConnect::WrapJSAggregatedToNative(nsISupports *aOuter,
                                      JSContext * aJSContext,
                                      JSObject * aJSObj,
                                      const nsIID & aIID,
                                      void * *result)
{
    NS_ASSERTION(aOuter, "bad param");
    NS_ASSERTION(aJSContext, "bad param");
    NS_ASSERTION(aJSObj, "bad param");
    NS_ASSERTION(result, "bad param");

    *result = nsnull;

    XPCCallContext ccx(NATIVE_CALLER, aJSContext);
    if (!ccx.IsValid())
        return UnexpectedFailure(NS_ERROR_FAILURE);

    nsresult rv;
    if (!XPCConvert::JSObject2NativeInterface(ccx, result, aJSObj,
                                              &aIID, aOuter, &rv))
        return rv;
    return NS_OK;
}

/* nsIXPConnectWrappedNative getWrappedNativeOfJSObject (in JSContextPtr aJSContext, in JSObjectPtr aJSObj); */
NS_IMETHODIMP
nsXPConnect::GetWrappedNativeOfJSObject(JSContext * aJSContext,
                                        JSObject * aJSObj,
                                        nsIXPConnectWrappedNative **_retval)
{
    NS_ASSERTION(aJSContext, "bad param");
    NS_ASSERTION(aJSObj, "bad param");
    NS_ASSERTION(_retval, "bad param");

    XPCCallContext ccx(NATIVE_CALLER, aJSContext);
    if (!ccx.IsValid())
        return UnexpectedFailure(NS_ERROR_FAILURE);

    SLIM_LOG_WILL_MORPH(aJSContext, aJSObj);
    nsIXPConnectWrappedNative* wrapper =
        XPCWrappedNative::GetAndMorphWrappedNativeOfJSObject(aJSContext, aJSObj);
    if (wrapper) {
        NS_ADDREF(wrapper);
        *_retval = wrapper;
        return NS_OK;
    }

    // else...
    *_retval = nsnull;
    return NS_ERROR_FAILURE;
}

/* nsISupports getNativeOfWrapper(in JSContextPtr aJSContext, in JSObjectPtr  aJSObj); */
NS_IMETHODIMP_(nsISupports*)
nsXPConnect::GetNativeOfWrapper(JSContext * aJSContext,
                                JSObject * aJSObj)
{
    NS_ASSERTION(aJSContext, "bad param");
    NS_ASSERTION(aJSObj, "bad param");

    XPCCallContext ccx(NATIVE_CALLER, aJSContext);
    if (!ccx.IsValid()) {
        UnexpectedFailure(NS_ERROR_FAILURE);
        return nsnull;
    }

    JSObject* obj2 = nsnull;
    nsIXPConnectWrappedNative* wrapper =
        XPCWrappedNative::GetWrappedNativeOfJSObject(aJSContext, aJSObj, nsnull,
                                                     &obj2);
    if (wrapper)
        return wrapper->Native();

    if (obj2)
        return (nsISupports*)xpc_GetJSPrivate(obj2);

    if (mozilla::dom::binding::instanceIsProxy(aJSObj)) {
        // FIXME: Provide a fast non-refcounting way to get the canonical
        //        nsISupports from the proxy.
        nsISupports *supports =
            static_cast<nsISupports*>(js::GetProxyPrivate(aJSObj).toPrivate());
        nsCOMPtr<nsISupports> canonical = do_QueryInterface(supports);
        return canonical.get();
    }

    return nsnull;
}

/* JSObjectPtr getJSObjectOfWrapper (in JSContextPtr aJSContext, in JSObjectPtr aJSObj); */
NS_IMETHODIMP
nsXPConnect::GetJSObjectOfWrapper(JSContext * aJSContext,
                                  JSObject * aJSObj,
                                  JSObject **_retval)
{
    NS_ASSERTION(aJSContext, "bad param");
    NS_ASSERTION(aJSObj, "bad param");
    NS_ASSERTION(_retval, "bad param");

    XPCCallContext ccx(NATIVE_CALLER, aJSContext);
    if (!ccx.IsValid())
        return UnexpectedFailure(NS_ERROR_FAILURE);

    JSObject* obj2 = nsnull;
    nsIXPConnectWrappedNative* wrapper =
        XPCWrappedNative::GetWrappedNativeOfJSObject(aJSContext, aJSObj, nsnull,
                                                     &obj2);
    if (wrapper) {
        wrapper->GetJSObject(_retval);
        return NS_OK;
    }
    if (obj2) {
        *_retval = obj2;
        return NS_OK;
    }
    if (mozilla::dom::binding::instanceIsProxy(aJSObj)) {
        *_retval = aJSObj;
        return NS_OK;
    }
    // else...
    *_retval = nsnull;
    return NS_ERROR_FAILURE;
}

/* nsIXPConnectWrappedNative getWrappedNativeOfNativeObject (in JSContextPtr aJSContext, in JSObjectPtr aScope, in nsISupports aCOMObj, in nsIIDRef aIID); */
NS_IMETHODIMP
nsXPConnect::GetWrappedNativeOfNativeObject(JSContext * aJSContext,
                                            JSObject * aScope,
                                            nsISupports *aCOMObj,
                                            const nsIID & aIID,
                                            nsIXPConnectWrappedNative **_retval)
{
    NS_ASSERTION(aJSContext, "bad param");
    NS_ASSERTION(aScope, "bad param");
    NS_ASSERTION(aCOMObj, "bad param");
    NS_ASSERTION(_retval, "bad param");

    *_retval = nsnull;

    XPCCallContext ccx(NATIVE_CALLER, aJSContext);
    if (!ccx.IsValid())
        return UnexpectedFailure(NS_ERROR_FAILURE);

    XPCWrappedNativeScope* scope =
        XPCWrappedNativeScope::FindInJSObjectScope(ccx, aScope);
    if (!scope)
        return UnexpectedFailure(NS_ERROR_FAILURE);

    AutoMarkingNativeInterfacePtr iface(ccx);
    iface = XPCNativeInterface::GetNewOrUsed(ccx, &aIID);
    if (!iface)
        return NS_ERROR_FAILURE;

    XPCWrappedNative* wrapper;

    nsresult rv = XPCWrappedNative::GetUsedOnly(ccx, aCOMObj, scope, iface,
                                                &wrapper);
    if (NS_FAILED(rv))
        return NS_ERROR_FAILURE;
    *_retval = static_cast<nsIXPConnectWrappedNative*>(wrapper);
    return NS_OK;
}

/* nsIXPConnectJSObjectHolder reparentWrappedNativeIfFound (in JSContextPtr aJSContext, in JSObjectPtr aScope, in JSObjectPtr aNewParent, in nsISupports aCOMObj); */
NS_IMETHODIMP
nsXPConnect::ReparentWrappedNativeIfFound(JSContext * aJSContext,
                                          JSObject * aScope,
                                          JSObject * aNewParent,
                                          nsISupports *aCOMObj,
                                          nsIXPConnectJSObjectHolder **_retval)
{
    XPCCallContext ccx(NATIVE_CALLER, aJSContext);
    if (!ccx.IsValid())
        return UnexpectedFailure(NS_ERROR_FAILURE);

    XPCWrappedNativeScope* scope =
        XPCWrappedNativeScope::FindInJSObjectScope(ccx, aScope);
    if (!scope)
        return UnexpectedFailure(NS_ERROR_FAILURE);

    XPCWrappedNativeScope* scope2 =
        XPCWrappedNativeScope::FindInJSObjectScope(ccx, aNewParent);
    if (!scope2)
        return UnexpectedFailure(NS_ERROR_FAILURE);

    return XPCWrappedNative::
        ReparentWrapperIfFound(ccx, scope, scope2, aNewParent, aCOMObj,
                               (XPCWrappedNative**) _retval);
}

static JSDHashOperator
MoveableWrapperFinder(JSDHashTable *table, JSDHashEntryHdr *hdr,
                      uint32_t number, void *arg)
{
    nsTArray<nsRefPtr<XPCWrappedNative> > *array =
        static_cast<nsTArray<nsRefPtr<XPCWrappedNative> > *>(arg);
    XPCWrappedNative *wn = ((Native2WrappedNativeMap::Entry*)hdr)->value;

    // If a wrapper is expired, then there are no references to it from JS, so
    // we don't have to move it.
    if (!wn->IsWrapperExpired())
        array->AppendElement(wn);
    return JS_DHASH_NEXT;
}

static nsresult
MoveWrapper(XPCCallContext& ccx, XPCWrappedNative *wrapper,
            XPCWrappedNativeScope *newScope, XPCWrappedNativeScope *oldScope)
{
    // First, check to see if this wrapper really needs to be
    // reparented.

    if (wrapper->GetScope() == newScope) {
        // The wrapper already got moved, nothing to do here.
        return NS_OK;
    }

    nsISupports *identity = wrapper->GetIdentityObject();
    nsCOMPtr<nsIClassInfo> info(do_QueryInterface(identity));

    // ClassInfo is implemented as singleton objects. If the identity
    // object here is the same object as returned by the QI, then it
    // is the singleton classinfo, so we don't need to reparent it.
    if (SameCOMIdentity(identity, info))
        info = nsnull;

    if (!info)
        return NS_OK;

    XPCNativeScriptableCreateInfo sciProto;
    XPCNativeScriptableCreateInfo sci;
    const XPCNativeScriptableCreateInfo& sciWrapper =
        XPCWrappedNative::GatherScriptableCreateInfo(identity, info,
                                                     sciProto, sci);

    // If the wrapper doesn't want precreate, then we don't need to
    // worry about reparenting it.
    if (!sciWrapper.GetFlags().WantPreCreate())
        return NS_OK;

    JSObject *newParent = oldScope->GetGlobalJSObject();
    nsresult rv = sciWrapper.GetCallback()->PreCreate(identity, ccx,
                                                      newParent,
                                                      &newParent);
    if (NS_FAILED(rv))
        return rv;

    if (newParent == oldScope->GetGlobalJSObject()) {
        // The old scope still works for this wrapper. We have to
        // assume that the wrapper will continue to return the old
        // scope from PreCreate, so don't move it.
        return NS_OK;
    }

    // The wrapper returned a new parent. If the new parent is in a
    // different scope, then we need to reparent it, otherwise, the
    // old scope is fine.

    XPCWrappedNativeScope *betterScope =
        XPCWrappedNativeScope::FindInJSObjectScope(ccx, newParent);
    if (betterScope == oldScope) {
        // The wrapper asked for a different object, but that object
        // was in the same scope. This means that the new parent
        // simply hasn't been reparented yet, so reparent it first,
        // and then continue reparenting the wrapper itself.

        if (!IS_WN_WRAPPER_OBJECT(newParent)) {
            // The parent of wrapper is a slim wrapper, in this case
            // we need to morph the parent so that we can reparent it.

            rv = MorphSlimWrapper(ccx, newParent);
            NS_ENSURE_SUCCESS(rv, rv);
        }

        XPCWrappedNative *parentWrapper =
            XPCWrappedNative::GetWrappedNativeOfJSObject(ccx, newParent);

        rv = MoveWrapper(ccx, parentWrapper, newScope, oldScope);

        NS_ENSURE_SUCCESS(rv, rv);

        newParent = parentWrapper->GetFlatJSObject();
    } else
        NS_ASSERTION(betterScope == newScope, "Weird scope returned");

    // Now, reparent the wrapper, since we know that it wants to be
    // reparented.

    nsRefPtr<XPCWrappedNative> junk;
    rv = XPCWrappedNative::ReparentWrapperIfFound(ccx, oldScope,
                                                  newScope, newParent,
                                                  wrapper->GetIdentityObject(),
                                                  getter_AddRefs(junk));
    return rv;
}

/* void moveWrappers(in JSContextPtr aJSContext, in JSObjectPtr  aOldScope, in JSObjectPtr  aNewScope); */
NS_IMETHODIMP
nsXPConnect::MoveWrappers(JSContext *aJSContext,
                          JSObject *aOldScope,
                          JSObject *aNewScope)
{
    XPCCallContext ccx(NATIVE_CALLER, aJSContext);
    if (!ccx.IsValid())
        return UnexpectedFailure(NS_ERROR_FAILURE);

    XPCWrappedNativeScope *oldScope =
        XPCWrappedNativeScope::FindInJSObjectScope(ccx, aOldScope);
    if (!oldScope)
        return UnexpectedFailure(NS_ERROR_FAILURE);

    XPCWrappedNativeScope *newScope =
        XPCWrappedNativeScope::FindInJSObjectScope(ccx, aNewScope);
    if (!newScope)
        return UnexpectedFailure(NS_ERROR_FAILURE);

    // First, look through the old scope and find all of the wrappers that
    // we're going to move.
    nsTArray<nsRefPtr<XPCWrappedNative> > wrappersToMove;

    {   // scoped lock
        XPCAutoLock lock(GetRuntime()->GetMapLock());
        Native2WrappedNativeMap *map = oldScope->GetWrappedNativeMap();
        wrappersToMove.SetCapacity(map->Count());
        map->Enumerate(MoveableWrapperFinder, &wrappersToMove);
    }

    // Now that we have the wrappers, reparent them to the new scope.
    for (PRUint32 i = 0, stop = wrappersToMove.Length(); i < stop; ++i) {
        nsresult rv = MoveWrapper(ccx, wrappersToMove[i], newScope, oldScope);
        NS_ENSURE_SUCCESS(rv, rv);
    }

    return NS_OK;
}

/* void setSecurityManagerForJSContext (in JSContextPtr aJSContext, in nsIXPCSecurityManager aManager, in PRUint16 flags); */
NS_IMETHODIMP
nsXPConnect::SetSecurityManagerForJSContext(JSContext * aJSContext,
                                            nsIXPCSecurityManager *aManager,
                                            PRUint16 flags)
{
    NS_ASSERTION(aJSContext, "bad param");

    XPCCallContext ccx(NATIVE_CALLER, aJSContext);
    if (!ccx.IsValid())
        return UnexpectedFailure(NS_ERROR_FAILURE);

    XPCContext* xpcc = ccx.GetXPCContext();

    NS_IF_ADDREF(aManager);
    nsIXPCSecurityManager* oldManager = xpcc->GetSecurityManager();
    NS_IF_RELEASE(oldManager);

    xpcc->SetSecurityManager(aManager);
    xpcc->SetSecurityManagerFlags(flags);
    return NS_OK;
}

/* void getSecurityManagerForJSContext (in JSContextPtr aJSContext, out nsIXPCSecurityManager aManager, out PRUint16 flags); */
NS_IMETHODIMP
nsXPConnect::GetSecurityManagerForJSContext(JSContext * aJSContext,
                                            nsIXPCSecurityManager **aManager,
                                            PRUint16 *flags)
{
    NS_ASSERTION(aJSContext, "bad param");
    NS_ASSERTION(aManager, "bad param");
    NS_ASSERTION(flags, "bad param");

    XPCCallContext ccx(NATIVE_CALLER, aJSContext);
    if (!ccx.IsValid())
        return UnexpectedFailure(NS_ERROR_FAILURE);

    XPCContext* xpcc = ccx.GetXPCContext();

    nsIXPCSecurityManager* manager = xpcc->GetSecurityManager();
    NS_IF_ADDREF(manager);
    *aManager = manager;
    *flags = xpcc->GetSecurityManagerFlags();
    return NS_OK;
}

/* void setDefaultSecurityManager (in nsIXPCSecurityManager aManager, in PRUint16 flags); */
NS_IMETHODIMP
nsXPConnect::SetDefaultSecurityManager(nsIXPCSecurityManager *aManager,
                                       PRUint16 flags)
{
    NS_IF_ADDREF(aManager);
    NS_IF_RELEASE(mDefaultSecurityManager);
    mDefaultSecurityManager = aManager;
    mDefaultSecurityManagerFlags = flags;

    nsCOMPtr<nsIScriptSecurityManager> ssm =
        do_QueryInterface(mDefaultSecurityManager);

    // Remember the result of the above QI for fast access to the
    // script securityt manager.
    gScriptSecurityManager = ssm;

    return NS_OK;
}

/* void getDefaultSecurityManager (out nsIXPCSecurityManager aManager, out PRUint16 flags); */
NS_IMETHODIMP
nsXPConnect::GetDefaultSecurityManager(nsIXPCSecurityManager **aManager,
                                       PRUint16 *flags)
{
    NS_ASSERTION(aManager, "bad param");
    NS_ASSERTION(flags, "bad param");

    NS_IF_ADDREF(mDefaultSecurityManager);
    *aManager = mDefaultSecurityManager;
    *flags = mDefaultSecurityManagerFlags;
    return NS_OK;
}

/* nsIStackFrame createStackFrameLocation (in PRUint32 aLanguage, in string aFilename, in string aFunctionName, in PRInt32 aLineNumber, in nsIStackFrame aCaller); */
NS_IMETHODIMP
nsXPConnect::CreateStackFrameLocation(PRUint32 aLanguage,
                                      const char *aFilename,
                                      const char *aFunctionName,
                                      PRInt32 aLineNumber,
                                      nsIStackFrame *aCaller,
                                      nsIStackFrame **_retval)
{
    NS_ASSERTION(_retval, "bad param");

    return XPCJSStack::CreateStackFrameLocation(aLanguage,
                                                aFilename,
                                                aFunctionName,
                                                aLineNumber,
                                                aCaller,
                                                _retval);
}

/* readonly attribute nsIStackFrame CurrentJSStack; */
NS_IMETHODIMP
nsXPConnect::GetCurrentJSStack(nsIStackFrame * *aCurrentJSStack)
{
    NS_ASSERTION(aCurrentJSStack, "bad param");
    *aCurrentJSStack = nsnull;

    JSContext* cx;
    // is there a current context available?
    if (NS_SUCCEEDED(Peek(&cx)) && cx) {
        nsCOMPtr<nsIStackFrame> stack;
        XPCJSStack::CreateStack(cx, getter_AddRefs(stack));
        if (stack) {
            // peel off native frames...
            PRUint32 language;
            nsCOMPtr<nsIStackFrame> caller;
            while (stack &&
                   NS_SUCCEEDED(stack->GetLanguage(&language)) &&
                   language != nsIProgrammingLanguage::JAVASCRIPT &&
                   NS_SUCCEEDED(stack->GetCaller(getter_AddRefs(caller))) &&
                   caller) {
                stack = caller;
            }
            NS_IF_ADDREF(*aCurrentJSStack = stack);
        }
    }
    return NS_OK;
}

/* readonly attribute nsIXPCNativeCallContext CurrentNativeCallContext; */
NS_IMETHODIMP
nsXPConnect::GetCurrentNativeCallContext(nsAXPCNativeCallContext * *aCurrentNativeCallContext)
{
    NS_ASSERTION(aCurrentNativeCallContext, "bad param");

    XPCPerThreadData* data = XPCPerThreadData::GetData(nsnull);
    if (data) {
        *aCurrentNativeCallContext = data->GetCallContext();
        return NS_OK;
    }
    //else...
    *aCurrentNativeCallContext = nsnull;
    return UnexpectedFailure(NS_ERROR_FAILURE);
}

/* attribute nsIException PendingException; */
NS_IMETHODIMP
nsXPConnect::GetPendingException(nsIException * *aPendingException)
{
    NS_ASSERTION(aPendingException, "bad param");

    XPCPerThreadData* data = XPCPerThreadData::GetData(nsnull);
    if (!data) {
        *aPendingException = nsnull;
        return UnexpectedFailure(NS_ERROR_FAILURE);
    }

    return data->GetException(aPendingException);
}

NS_IMETHODIMP
nsXPConnect::SetPendingException(nsIException * aPendingException)
{
    XPCPerThreadData* data = XPCPerThreadData::GetData(nsnull);
    if (!data)
        return UnexpectedFailure(NS_ERROR_FAILURE);

    data->SetException(aPendingException);
    return NS_OK;
}

NS_IMETHODIMP
nsXPConnect::SyncJSContexts(void)
{
    // Do-nothing compatibility function
    return NS_OK;
}

/* nsIXPCFunctionThisTranslator setFunctionThisTranslator (in nsIIDRef aIID, in nsIXPCFunctionThisTranslator aTranslator); */
NS_IMETHODIMP
nsXPConnect::SetFunctionThisTranslator(const nsIID & aIID,
                                       nsIXPCFunctionThisTranslator *aTranslator,
                                       nsIXPCFunctionThisTranslator **_retval)
{
    XPCJSRuntime* rt = GetRuntime();
    nsIXPCFunctionThisTranslator* old;
    IID2ThisTranslatorMap* map = rt->GetThisTranslatorMap();

    {
        XPCAutoLock lock(rt->GetMapLock()); // scoped lock
        if (_retval) {
            old = map->Find(aIID);
            NS_IF_ADDREF(old);
            *_retval = old;
        }
        map->Add(aIID, aTranslator);
    }
    return NS_OK;
}

/* nsIXPCFunctionThisTranslator getFunctionThisTranslator (in nsIIDRef aIID); */
NS_IMETHODIMP
nsXPConnect::GetFunctionThisTranslator(const nsIID & aIID,
                                       nsIXPCFunctionThisTranslator **_retval)
{
    XPCJSRuntime* rt = GetRuntime();
    nsIXPCFunctionThisTranslator* old;
    IID2ThisTranslatorMap* map = rt->GetThisTranslatorMap();

    {
        XPCAutoLock lock(rt->GetMapLock()); // scoped lock
        old = map->Find(aIID);
        NS_IF_ADDREF(old);
        *_retval = old;
    }
    return NS_OK;
}

/* void clearAllWrappedNativeSecurityPolicies (); */
NS_IMETHODIMP
nsXPConnect::ClearAllWrappedNativeSecurityPolicies()
{
    XPCCallContext ccx(NATIVE_CALLER);
    if (!ccx.IsValid())
        return UnexpectedFailure(NS_ERROR_FAILURE);

    return XPCWrappedNativeScope::ClearAllWrappedNativeSecurityPolicies(ccx);
}

NS_IMETHODIMP
nsXPConnect::CreateSandbox(JSContext *cx, nsIPrincipal *principal,
                           nsIXPConnectJSObjectHolder **_retval)
{
    XPCCallContext ccx(NATIVE_CALLER, cx);
    if (!ccx.IsValid())
        return UnexpectedFailure(NS_ERROR_FAILURE);

    *_retval = nsnull;

    jsval rval = JSVAL_VOID;
    AUTO_MARK_JSVAL(ccx, &rval);

    nsresult rv = xpc_CreateSandboxObject(cx, &rval, principal, NULL, false,
                                          EmptyCString());
    NS_ASSERTION(NS_FAILED(rv) || !JSVAL_IS_PRIMITIVE(rval),
                 "Bad return value from xpc_CreateSandboxObject()!");

    if (NS_SUCCEEDED(rv) && !JSVAL_IS_PRIMITIVE(rval)) {
        *_retval = XPCJSObjectHolder::newHolder(ccx, JSVAL_TO_OBJECT(rval));
        NS_ENSURE_TRUE(*_retval, NS_ERROR_OUT_OF_MEMORY);

        NS_ADDREF(*_retval);
    }

    return rv;
}

NS_IMETHODIMP
nsXPConnect::EvalInSandboxObject(const nsAString& source, JSContext *cx,
                                 nsIXPConnectJSObjectHolder *sandbox,
                                 bool returnStringOnly, jsval *rval)
{
    if (!sandbox)
        return NS_ERROR_INVALID_ARG;

    JSObject *obj;
    nsresult rv = sandbox->GetJSObject(&obj);
    NS_ENSURE_SUCCESS(rv, rv);

    return xpc_EvalInSandbox(cx, obj, source,
                             NS_ConvertUTF16toUTF8(source).get(), 1,
                             JSVERSION_DEFAULT, returnStringOnly, rval);
}

/* nsIXPConnectJSObjectHolder getWrappedNativePrototype (in JSContextPtr aJSContext, in JSObjectPtr aScope, in nsIClassInfo aClassInfo); */
NS_IMETHODIMP
nsXPConnect::GetWrappedNativePrototype(JSContext * aJSContext,
                                       JSObject * aScope,
                                       nsIClassInfo *aClassInfo,
                                       nsIXPConnectJSObjectHolder **_retval)
{
    XPCCallContext ccx(NATIVE_CALLER, aJSContext);
    if (!ccx.IsValid())
        return UnexpectedFailure(NS_ERROR_FAILURE);

    JSAutoEnterCompartment ac;
    if (!ac.enter(aJSContext, aScope))
        return UnexpectedFailure(NS_ERROR_FAILURE);

    XPCWrappedNativeScope* scope =
        XPCWrappedNativeScope::FindInJSObjectScope(ccx, aScope);
    if (!scope)
        return UnexpectedFailure(NS_ERROR_FAILURE);

    XPCNativeScriptableCreateInfo sciProto;
    XPCWrappedNative::GatherProtoScriptableCreateInfo(aClassInfo, sciProto);

    AutoMarkingWrappedNativeProtoPtr proto(ccx);
    proto = XPCWrappedNativeProto::GetNewOrUsed(ccx, scope, aClassInfo,
                                                &sciProto, OBJ_IS_NOT_GLOBAL);
    if (!proto)
        return UnexpectedFailure(NS_ERROR_FAILURE);

    nsIXPConnectJSObjectHolder* holder;
    *_retval = holder = XPCJSObjectHolder::newHolder(ccx,
                                                     proto->GetJSProtoObject());
    if (!holder)
        return UnexpectedFailure(NS_ERROR_FAILURE);

    NS_ADDREF(holder);
    return NS_OK;
}

/* void releaseJSContext (in JSContextPtr aJSContext, in bool noGC); */
NS_IMETHODIMP
nsXPConnect::ReleaseJSContext(JSContext * aJSContext, bool noGC)
{
    NS_ASSERTION(aJSContext, "bad param");
    XPCPerThreadData* tls = XPCPerThreadData::GetData(aJSContext);
    if (tls) {
        XPCCallContext* ccx = nsnull;
        for (XPCCallContext* cur = tls->GetCallContext();
             cur;
             cur = cur->GetPrevCallContext()) {
            if (cur->GetJSContext() == aJSContext) {
                ccx = cur;
                // Keep looping to find the deepest matching call context.
            }
        }

        if (ccx) {
#ifdef DEBUG_xpc_hacker
            printf("!xpc - deferring destruction of JSContext @ %p\n",
                   (void *)aJSContext);
#endif
            ccx->SetDestroyJSContextInDestructor(true);
            return NS_OK;
        }
        // else continue on and synchronously destroy the JSContext ...

        NS_ASSERTION(!tls->GetJSContextStack() ||
                     !tls->GetJSContextStack()->
                     DEBUG_StackHasJSContext(aJSContext),
                     "JSContext still in threadjscontextstack!");
    }

    if (noGC)
        JS_DestroyContextNoGC(aJSContext);
    else
        JS_DestroyContext(aJSContext);
    return NS_OK;
}

/* void debugDump (in short depth); */
NS_IMETHODIMP
nsXPConnect::DebugDump(PRInt16 depth)
{
#ifdef DEBUG
    depth-- ;
    XPC_LOG_ALWAYS(("nsXPConnect @ %x with mRefCnt = %d", this, mRefCnt.get()));
    XPC_LOG_INDENT();
        XPC_LOG_ALWAYS(("gSelf @ %x", gSelf));
        XPC_LOG_ALWAYS(("gOnceAliveNowDead is %d", (int)gOnceAliveNowDead));
        XPC_LOG_ALWAYS(("mDefaultSecurityManager @ %x", mDefaultSecurityManager));
        XPC_LOG_ALWAYS(("mDefaultSecurityManagerFlags of %x", mDefaultSecurityManagerFlags));
        XPC_LOG_ALWAYS(("mInterfaceInfoManager @ %x", mInterfaceInfoManager.get()));
        if (mRuntime) {
            if (depth)
                mRuntime->DebugDump(depth);
            else
                XPC_LOG_ALWAYS(("XPCJSRuntime @ %x", mRuntime));
        } else
            XPC_LOG_ALWAYS(("mRuntime is null"));
        XPCWrappedNativeScope::DebugDumpAllScopes(depth);
    XPC_LOG_OUTDENT();
#endif
    return NS_OK;
}

/* void debugDumpObject (in nsISupports aCOMObj, in short depth); */
NS_IMETHODIMP
nsXPConnect::DebugDumpObject(nsISupports *p, PRInt16 depth)
{
#ifdef DEBUG
    if (!depth)
        return NS_OK;
    if (!p) {
        XPC_LOG_ALWAYS(("*** Cound not dump object with NULL address"));
        return NS_OK;
    }

    nsIXPConnect* xpc;
    nsIXPCWrappedJSClass* wjsc;
    nsIXPConnectWrappedNative* wn;
    nsIXPConnectWrappedJS* wjs;

    if (NS_SUCCEEDED(p->QueryInterface(NS_GET_IID(nsIXPConnect),
                                       (void**)&xpc))) {
        XPC_LOG_ALWAYS(("Dumping a nsIXPConnect..."));
        xpc->DebugDump(depth);
        NS_RELEASE(xpc);
    } else if (NS_SUCCEEDED(p->QueryInterface(NS_GET_IID(nsIXPCWrappedJSClass),
                                              (void**)&wjsc))) {
        XPC_LOG_ALWAYS(("Dumping a nsIXPCWrappedJSClass..."));
        wjsc->DebugDump(depth);
        NS_RELEASE(wjsc);
    } else if (NS_SUCCEEDED(p->QueryInterface(NS_GET_IID(nsIXPConnectWrappedNative),
                                              (void**)&wn))) {
        XPC_LOG_ALWAYS(("Dumping a nsIXPConnectWrappedNative..."));
        wn->DebugDump(depth);
        NS_RELEASE(wn);
    } else if (NS_SUCCEEDED(p->QueryInterface(NS_GET_IID(nsIXPConnectWrappedJS),
                                              (void**)&wjs))) {
        XPC_LOG_ALWAYS(("Dumping a nsIXPConnectWrappedJS..."));
        wjs->DebugDump(depth);
        NS_RELEASE(wjs);
    } else
        XPC_LOG_ALWAYS(("*** Could not dump the nsISupports @ %x", p));
#endif
    return NS_OK;
}

/* void debugDumpJSStack (in bool showArgs, in bool showLocals, in bool showThisProps); */
NS_IMETHODIMP
nsXPConnect::DebugDumpJSStack(bool showArgs,
                              bool showLocals,
                              bool showThisProps)
{
    JSContext* cx;
    if (NS_FAILED(Peek(&cx)))
        printf("failed to peek into nsIThreadJSContextStack service!\n");
    else if (!cx)
        printf("there is no JSContext on the nsIThreadJSContextStack!\n");
    else
        xpc_DumpJSStack(cx, showArgs, showLocals, showThisProps);

    return NS_OK;
}

char*
nsXPConnect::DebugPrintJSStack(bool showArgs,
                               bool showLocals,
                               bool showThisProps)
{
    JSContext* cx;
    if (NS_FAILED(Peek(&cx)))
        printf("failed to peek into nsIThreadJSContextStack service!\n");
    else if (!cx)
        printf("there is no JSContext on the nsIThreadJSContextStack!\n");
    else
        return xpc_PrintJSStack(cx, showArgs, showLocals, showThisProps);

    return nsnull;
}

/* void debugDumpEvalInJSStackFrame (in PRUint32 aFrameNumber, in string aSourceText); */
NS_IMETHODIMP
nsXPConnect::DebugDumpEvalInJSStackFrame(PRUint32 aFrameNumber, const char *aSourceText)
{
    JSContext* cx;
    if (NS_FAILED(Peek(&cx)))
        printf("failed to peek into nsIThreadJSContextStack service!\n");
    else if (!cx)
        printf("there is no JSContext on the nsIThreadJSContextStack!\n");
    else
        xpc_DumpEvalInJSStackFrame(cx, aFrameNumber, aSourceText);

    return NS_OK;
}

/* jsval variantToJS (in JSContextPtr ctx, in JSObjectPtr scope, in nsIVariant value); */
NS_IMETHODIMP
nsXPConnect::VariantToJS(JSContext* ctx, JSObject* scope, nsIVariant* value, jsval* _retval)
{
    NS_PRECONDITION(ctx, "bad param");
    NS_PRECONDITION(scope, "bad param");
    NS_PRECONDITION(value, "bad param");
    NS_PRECONDITION(_retval, "bad param");

    XPCCallContext ccx(NATIVE_CALLER, ctx);
    if (!ccx.IsValid())
        return NS_ERROR_FAILURE;
    XPCLazyCallContext lccx(ccx);

    ccx.SetScopeForNewJSObjects(scope);

    nsresult rv = NS_OK;
    if (!XPCVariant::VariantDataToJS(lccx, value, &rv, _retval)) {
        if (NS_FAILED(rv))
            return rv;

        return NS_ERROR_FAILURE;
    }

    return NS_OK;
}

/* nsIVariant JSToVariant (in JSContextPtr ctx, in jsval value); */
NS_IMETHODIMP
nsXPConnect::JSToVariant(JSContext* ctx, const jsval &value, nsIVariant** _retval)
{
    NS_PRECONDITION(ctx, "bad param");
    NS_PRECONDITION(value != JSVAL_NULL, "bad param");
    NS_PRECONDITION(_retval, "bad param");

    XPCCallContext ccx(NATIVE_CALLER, ctx);
    if (!ccx.IsValid())
        return NS_ERROR_FAILURE;

    *_retval = XPCVariant::newVariant(ccx, value);
    if (!(*_retval))
        return NS_ERROR_FAILURE;

    return NS_OK;
}

NS_IMETHODIMP
nsXPConnect::OnProcessNextEvent(nsIThreadInternal *aThread, bool aMayWait,
                                PRUint32 aRecursionDepth)
{
    // Record this event.
    mEventDepth++;

    // Push a null JSContext so that we don't see any script during
    // event processing.
    MOZ_ASSERT(NS_IsMainThread());
    return Push(nsnull);
}

NS_IMETHODIMP
nsXPConnect::AfterProcessNextEvent(nsIThreadInternal *aThread,
                                   PRUint32 aRecursionDepth)
{
    // Watch out for unpaired events during observer registration.
    if (NS_UNLIKELY(mEventDepth == 0))
        return NS_OK;
    mEventDepth--;

    // Call cycle collector occasionally.
    MOZ_ASSERT(NS_IsMainThread());
    nsJSContext::MaybePokeCC();

    return Pop(nsnull);
}

NS_IMETHODIMP
nsXPConnect::OnDispatchedEvent(nsIThreadInternal* aThread)
{
    NS_NOTREACHED("Why tell us?");
    return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
nsXPConnect::AddJSHolder(void* aHolder, nsScriptObjectTracer* aTracer)
{
    return mRuntime->AddJSHolder(aHolder, aTracer);
}

NS_IMETHODIMP
nsXPConnect::RemoveJSHolder(void* aHolder)
{
    return mRuntime->RemoveJSHolder(aHolder);
}

NS_IMETHODIMP
nsXPConnect::SetReportAllJSExceptions(bool newval)
{
    // Ignore if the environment variable was set.
    if (gReportAllJSExceptions != 1)
        gReportAllJSExceptions = newval ? 2 : 0;

    return NS_OK;
}

/* [noscript, notxpcom] bool defineDOMQuickStubs (in JSContextPtr cx, in JSObjectPtr proto, in PRUint32 flags, in PRUint32 interfaceCount, [array, size_is (interfaceCount)] in nsIIDPtr interfaceArray); */
NS_IMETHODIMP_(bool)
nsXPConnect::DefineDOMQuickStubs(JSContext * cx,
                                 JSObject * proto,
                                 PRUint32 flags,
                                 PRUint32 interfaceCount,
                                 const nsIID * *interfaceArray)
{
    return DOM_DefineQuickStubs(cx, proto, flags,
                                interfaceCount, interfaceArray);
}

/* attribute JSRuntime runtime; */
NS_IMETHODIMP
nsXPConnect::GetRuntime(JSRuntime **runtime)
{
    if (!runtime)
        return NS_ERROR_NULL_POINTER;

    JSRuntime *rt = GetRuntime()->GetJSRuntime();
    JS_AbortIfWrongThread(rt);
    *runtime = rt;
    return NS_OK;
}

/* attribute nsIXPCScriptable backstagePass; */
NS_IMETHODIMP
nsXPConnect::GetBackstagePass(nsIXPCScriptable **bsp)
{
    if (!mBackstagePass) {
        nsCOMPtr<nsIPrincipal> sysprin;
        nsCOMPtr<nsIScriptSecurityManager> secman =
            do_GetService(NS_SCRIPTSECURITYMANAGER_CONTRACTID);
        if (!secman)
            return NS_ERROR_NOT_AVAILABLE;
        if (NS_FAILED(secman->GetSystemPrincipal(getter_AddRefs(sysprin))))
            return NS_ERROR_NOT_AVAILABLE;

        mBackstagePass = new BackstagePass(sysprin);
        if (!mBackstagePass)
            return NS_ERROR_OUT_OF_MEMORY;
    }
    NS_ADDREF(*bsp = mBackstagePass);
    return NS_OK;
}

/* [noscript, notxpcom] void registerGCCallback(in JSGCCallback func); */
NS_IMETHODIMP_(void)
nsXPConnect::RegisterGCCallback(JSGCCallback func)
{
    mRuntime->AddGCCallback(func);
}

/* [noscript, notxpcom] void unregisterGCCallback(in JSGCCallback func); */
NS_IMETHODIMP_(void)
nsXPConnect::UnregisterGCCallback(JSGCCallback func)
{
    mRuntime->RemoveGCCallback(func);
}

//  nsIJSContextStack and nsIThreadJSContextStack implementations

/* readonly attribute PRInt32 Count; */
NS_IMETHODIMP
nsXPConnect::GetCount(PRInt32 *aCount)
{
    MOZ_ASSERT(aCount);

    XPCPerThreadData* data = XPCPerThreadData::GetData(nsnull);

    if (!data) {
        *aCount = 0;
        return NS_ERROR_FAILURE;
    }

    *aCount = data->GetJSContextStack()->Count();
    return NS_OK;
}

/* JSContext Peek (); */
NS_IMETHODIMP
nsXPConnect::Peek(JSContext * *_retval)
{
    MOZ_ASSERT(_retval);

    XPCPerThreadData* data = XPCPerThreadData::GetData(nsnull);

    if (!data) {
        *_retval = nsnull;
        return NS_ERROR_FAILURE;
    }

    *_retval = data->GetJSContextStack()->Peek();
    return NS_OK;
}

void
nsXPConnect::CheckForDebugMode(JSRuntime *rt) {
    JSContext *cx = NULL;

    if (gDebugMode == gDesiredDebugMode) {
        return;
    }

    // This can happen if a Worker is running, but we don't have the ability to
    // debug workers right now, so just return.
    if (!NS_IsMainThread()) {
        return;
    }

    JS_SetRuntimeDebugMode(rt, gDesiredDebugMode);

    nsresult rv;
    const char jsdServiceCtrID[] = "@mozilla.org/js/jsd/debugger-service;1";
    nsCOMPtr<jsdIDebuggerService> jsds = do_GetService(jsdServiceCtrID, &rv);
    if (NS_FAILED(rv)) {
        goto fail;
    }

    if (!(cx = JS_NewContext(rt, 256))) {
        goto fail;
    }

    {
        struct AutoDestroyContext {
            JSContext *cx;
            AutoDestroyContext(JSContext *cx) : cx(cx) {}
            ~AutoDestroyContext() { JS_DestroyContext(cx); }
        } adc(cx);
        JSAutoRequest ar(cx);

        const js::CompartmentVector &vector = js::GetRuntimeCompartments(rt);
        for (JSCompartment * const *p = vector.begin(); p != vector.end(); ++p) {
            JSCompartment *comp = *p;
            if (!JS_GetCompartmentPrincipals(comp)) {
                /* Ignore special compartments (atoms, JSD compartments) */
                continue;
            }

            /* ParticipatesInCycleCollection means "on the main thread" */
            if (xpc::CompartmentParticipatesInCycleCollection(cx, comp)) {
                if (!JS_SetDebugModeForCompartment(cx, comp, gDesiredDebugMode))
                    goto fail;
            }
        }
    }

    if (gDesiredDebugMode) {
        rv = jsds->ActivateDebugger(rt);
    }

    gDebugMode = gDesiredDebugMode;
    return;

fail:
    if (jsds)
        jsds->DeactivateDebugger();

    /*
     * If an attempt to turn debug mode on fails, cancel the request. It's
     * always safe to turn debug mode off, since DeactivateDebugger prevents
     * debugger callbacks from having any effect.
     */
    if (gDesiredDebugMode)
        JS_SetRuntimeDebugMode(rt, false);
    gDesiredDebugMode = gDebugMode = false;
}

NS_EXPORT_(void)
xpc_ActivateDebugMode()
{
    XPCJSRuntime* rt = nsXPConnect::GetRuntimeInstance();
    nsXPConnect::GetXPConnect()->SetDebugModeWhenPossible(true, true);
    nsXPConnect::CheckForDebugMode(rt->GetJSRuntime());
}

/* JSContext Pop (); */
NS_IMETHODIMP
nsXPConnect::Pop(JSContext * *_retval)
{
    XPCPerThreadData* data = XPCPerThreadData::GetData(nsnull);

    if (!data) {
        if (_retval)
            *_retval = NULL;
        return NS_ERROR_FAILURE;
    }

    JSContext *cx = data->GetJSContextStack()->Pop();
    if (_retval)
        *_retval = cx;
    return NS_OK;
}

/* void Push (in JSContext cx); */
NS_IMETHODIMP
nsXPConnect::Push(JSContext * cx)
{
    XPCPerThreadData* data = XPCPerThreadData::GetData(cx);

    if (!data)
        return NS_ERROR_FAILURE;

     if (gDebugMode != gDesiredDebugMode && NS_IsMainThread()) {
         const InfallibleTArray<XPCJSContextInfo>* stack = data->GetJSContextStack()->GetStack();
         if (!gDesiredDebugMode) {
             /* Turn off debug mode immediately, even if JS code is currently running */
             CheckForDebugMode(mRuntime->GetJSRuntime());
         } else {
             bool runningJS = false;
             for (PRUint32 i = 0; i < stack->Length(); ++i) {
                 JSContext *cx = (*stack)[i].cx;
                 if (cx && js::IsContextRunningJS(cx)) {
                     runningJS = true;
                     break;
                 }
             }
             if (!runningJS)
                 CheckForDebugMode(mRuntime->GetJSRuntime());
         }
     }

     return data->GetJSContextStack()->Push(cx) ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

/* attribute JSContext SafeJSContext; */
NS_IMETHODIMP
nsXPConnect::GetSafeJSContext(JSContext * *aSafeJSContext)
{
    NS_ASSERTION(aSafeJSContext, "loser!");

    XPCPerThreadData* data = XPCPerThreadData::GetData(nsnull);

    if (!data) {
        *aSafeJSContext = nsnull;
        return NS_ERROR_FAILURE;
    }

    *aSafeJSContext = data->GetJSContextStack()->GetSafeJSContext();
    return *aSafeJSContext ? NS_OK : NS_ERROR_FAILURE;
}

nsIPrincipal*
nsXPConnect::GetPrincipal(JSObject* obj, bool allowShortCircuit) const
{
    NS_ASSERTION(IS_WRAPPER_CLASS(js::GetObjectClass(obj)),
                 "What kind of wrapper is this?");

    if (IS_WN_WRAPPER_OBJECT(obj)) {
        XPCWrappedNative *xpcWrapper =
            (XPCWrappedNative *)xpc_GetJSPrivate(obj);
        if (xpcWrapper) {
            if (allowShortCircuit) {
                nsIPrincipal *result = xpcWrapper->GetObjectPrincipal();
                if (result) {
                    return result;
                }
            }

            // If not, check if it points to an nsIScriptObjectPrincipal
            nsCOMPtr<nsIScriptObjectPrincipal> objPrin =
                do_QueryInterface(xpcWrapper->Native());
            if (objPrin) {
                nsIPrincipal *result = objPrin->GetPrincipal();
                if (result) {
                    return result;
                }
            }
        }
    } else {
        if (allowShortCircuit) {
            nsIPrincipal *result =
                GetSlimWrapperProto(obj)->GetScope()->GetPrincipal();
            if (result) {
                return result;
            }
        }

        nsCOMPtr<nsIScriptObjectPrincipal> objPrin =
            do_QueryInterface((nsISupports*)xpc_GetJSPrivate(obj));
        if (objPrin) {
            nsIPrincipal *result = objPrin->GetPrincipal();
            if (result) {
                return result;
            }
        }
    }

    return nsnull;
}

NS_IMETHODIMP
nsXPConnect::HoldObject(JSContext *aJSContext, JSObject *aObject,
                        nsIXPConnectJSObjectHolder **aHolder)
{
    XPCCallContext ccx(NATIVE_CALLER, aJSContext);
    XPCJSObjectHolder* objHolder = XPCJSObjectHolder::newHolder(ccx, aObject);
    if (!objHolder)
        return NS_ERROR_OUT_OF_MEMORY;

    NS_ADDREF(*aHolder = objHolder);
    return NS_OK;
}

NS_IMETHODIMP_(void)
nsXPConnect::GetCaller(JSContext **aJSContext, JSObject **aObj)
{
    XPCCallContext *ccx = XPCPerThreadData::GetData(nsnull)->GetCallContext();
    *aJSContext = ccx->GetJSContext();

    // Set to the caller in XPC_WN_Helper_{Call,Construct}
    *aObj = ccx->GetFlattenedJSObject();
}

namespace xpc {

bool
Base64Encode(JSContext *cx, JS::Value val, JS::Value *out)
{
    MOZ_ASSERT(cx);
    MOZ_ASSERT(out);

    JS::Value root = val;
    xpc_qsACString encodedString(cx, root, &root, xpc_qsACString::eNull,
                                 xpc_qsACString::eStringify);
    if (!encodedString.IsValid())
        return false;

    nsCAutoString result;
    if (NS_FAILED(mozilla::Base64Encode(encodedString, result))) {
        JS_ReportError(cx, "Failed to encode base64 data!");
        return false;
    }

    JSString *str = JS_NewStringCopyN(cx, result.get(), result.Length());
    if (!str)
        return false;

    *out = STRING_TO_JSVAL(str);
    return true;
}

bool
Base64Decode(JSContext *cx, JS::Value val, JS::Value *out)
{
    MOZ_ASSERT(cx);
    MOZ_ASSERT(out);

    JS::Value root = val;
    xpc_qsACString encodedString(cx, root, &root, xpc_qsACString::eNull,
                                 xpc_qsACString::eNull);
    if (!encodedString.IsValid())
        return false;

    nsCAutoString result;
    if (NS_FAILED(mozilla::Base64Decode(encodedString, result))) {
        JS_ReportError(cx, "Failed to decode base64 string!");
        return false;
    }

    JSString *str = JS_NewStringCopyN(cx, result.get(), result.Length());
    if (!str)
        return false;

    *out = STRING_TO_JSVAL(str);
    return true;
}

} // namespace xpc

NS_IMETHODIMP
nsXPConnect::SetDebugModeWhenPossible(bool mode, bool allowSyncDisable)
{
    gDesiredDebugMode = mode;
    if (!mode && allowSyncDisable)
        CheckForDebugMode(mRuntime->GetJSRuntime());
    return NS_OK;
}

NS_IMETHODIMP
nsXPConnect::GetTelemetryValue(JSContext *cx, jsval *rval)
{
    JSObject *obj = JS_NewObject(cx, NULL, NULL, NULL);
    if (!obj)
        return NS_ERROR_OUT_OF_MEMORY;

    uintN attrs = JSPROP_ENUMERATE | JSPROP_READONLY | JSPROP_PERMANENT;

    size_t i = JS_GetE4XObjectsCreated(cx);
    jsval v = DOUBLE_TO_JSVAL(i);
    if (!JS_DefineProperty(cx, obj, "e4x", v, NULL, NULL, attrs))
        return NS_ERROR_OUT_OF_MEMORY;

    i = JS_SetProtoCalled(cx);
    v = DOUBLE_TO_JSVAL(i);
    if (!JS_DefineProperty(cx, obj, "setProto", v, NULL, NULL, attrs))
        return NS_ERROR_OUT_OF_MEMORY;

    i = JS_GetCustomIteratorCount(cx);
    v = DOUBLE_TO_JSVAL(i);
    if (!JS_DefineProperty(cx, obj, "customIter", v, NULL, NULL, attrs))
        return NS_ERROR_OUT_OF_MEMORY;

    *rval = OBJECT_TO_JSVAL(obj);
    return NS_OK;
}

/* These are here to be callable from a debugger */
JS_BEGIN_EXTERN_C
JS_EXPORT_API(void) DumpJSStack()
{
    nsresult rv;
    nsCOMPtr<nsIXPConnect> xpc(do_GetService(nsIXPConnect::GetCID(), &rv));
    if (NS_SUCCEEDED(rv) && xpc)
        xpc->DebugDumpJSStack(true, true, false);
    else
        printf("failed to get XPConnect service!\n");
}

JS_EXPORT_API(char*) PrintJSStack()
{
    nsresult rv;
    nsCOMPtr<nsIXPConnect> xpc(do_GetService(nsIXPConnect::GetCID(), &rv));
    return (NS_SUCCEEDED(rv) && xpc) ?
        xpc->DebugPrintJSStack(true, true, false) :
        nsnull;
}

JS_EXPORT_API(void) DumpJSEval(PRUint32 frameno, const char* text)
{
    nsresult rv;
    nsCOMPtr<nsIXPConnect> xpc(do_GetService(nsIXPConnect::GetCID(), &rv));
    if (NS_SUCCEEDED(rv) && xpc)
        xpc->DebugDumpEvalInJSStackFrame(frameno, text);
    else
        printf("failed to get XPConnect service!\n");
}

JS_EXPORT_API(void) DumpJSObject(JSObject* obj)
{
    xpc_DumpJSObject(obj);
}

JS_EXPORT_API(void) DumpJSValue(jsval val)
{
    printf("Dumping 0x%llu.\n", (long long) JSVAL_TO_IMPL(val).asBits);
    if (JSVAL_IS_NULL(val)) {
        printf("Value is null\n");
    } else if (JSVAL_IS_OBJECT(val) || JSVAL_IS_NULL(val)) {
        printf("Value is an object\n");
        JSObject* obj = JSVAL_TO_OBJECT(val);
        DumpJSObject(obj);
    } else if (JSVAL_IS_NUMBER(val)) {
        printf("Value is a number: ");
        if (JSVAL_IS_INT(val))
          printf("Integer %i\n", JSVAL_TO_INT(val));
        else if (JSVAL_IS_DOUBLE(val))
          printf("Floating-point value %f\n", JSVAL_TO_DOUBLE(val));
    } else if (JSVAL_IS_STRING(val)) {
        printf("Value is a string: ");
        putc('<', stdout);
        JS_FileEscapedString(stdout, JSVAL_TO_STRING(val), 0);
        fputs(">\n", stdout);
    } else if (JSVAL_IS_BOOLEAN(val)) {
        printf("Value is boolean: ");
        printf(JSVAL_TO_BOOLEAN(val) ? "true" : "false");
    } else if (JSVAL_IS_VOID(val)) {
        printf("Value is undefined\n");
    } else {
        printf("No idea what this value is.\n");
    }
}
JS_END_EXTERN_C

