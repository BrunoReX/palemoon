/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=78:
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
 * The Original Code is Mozilla XPConnect  code, released
 * June 30, 2009.
 *
 * The Initial Developer of the Original Code is
 *    The Mozilla Foundation
 *
 * Contributor(s):
 *    Andreas Gal <gal@mozilla.com>
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

#ifndef xpcpublic_h
#define xpcpublic_h

#include "jsapi.h"
#include "jsclass.h"
#include "jsfriendapi.h"
#include "jsgc.h"
#include "jspubtd.h"
#include "jsproxy.h"

#include "nsISupports.h"
#include "nsIPrincipal.h"
#include "nsWrapperCache.h"
#include "nsStringGlue.h"
#include "nsTArray.h"

class nsIPrincipal;
struct nsDOMClassInfoData;

nsresult
xpc_CreateGlobalObject(JSContext *cx, JSClass *clasp,
                       nsIPrincipal *principal, nsISupports *ptr,
                       bool wantXrays, JSObject **global,
                       JSCompartment **compartment);

nsresult
xpc_CreateMTGlobalObject(JSContext *cx, JSClass *clasp,
                         nsISupports *ptr, JSObject **global,
                         JSCompartment **compartment);

#define XPCONNECT_GLOBAL_FLAGS                                                \
    JSCLASS_XPCONNECT_GLOBAL | JSCLASS_HAS_PRIVATE |                          \
    JSCLASS_PRIVATE_IS_NSISUPPORTS | JSCLASS_GLOBAL_FLAGS_WITH_SLOTS(1)

void
TraceXPCGlobal(JSTracer *trc, JSObject *obj);

// XXX where should this live?
NS_EXPORT_(void)
xpc_LocalizeContext(JSContext *cx);

nsresult
xpc_MorphSlimWrapper(JSContext *cx, nsISupports *tomorph);

#define IS_WRAPPER_CLASS(clazz)                                               \
    ((clazz)->ext.isWrappedNative)

inline JSBool
DebugCheckWrapperClass(JSObject* obj)
{
    NS_ASSERTION(IS_WRAPPER_CLASS(js::GetObjectClass(obj)),
                 "Forgot to check if this is a wrapper?");
    return true;
}

// If IS_WRAPPER_CLASS for the JSClass of an object is true, the object can be
// a slim wrapper, holding a native in its private slot, or a wrappednative
// wrapper, holding the XPCWrappedNative in its private slot. A slim wrapper
// also holds a pointer to its XPCWrappedNativeProto in a reserved slot, we can
// check that slot for a non-void value to distinguish between the two.

// Only use these macros if IS_WRAPPER_CLASS(GetObjectClass(obj)) is true.
#define IS_WN_WRAPPER_OBJECT(obj)                                             \
    (DebugCheckWrapperClass(obj) && js::GetReservedSlot(obj, 0).isUndefined())
#define IS_SLIM_WRAPPER_OBJECT(obj)                                           \
    (DebugCheckWrapperClass(obj) && !js::GetReservedSlot(obj, 0).isUndefined())

// Use these macros if IS_WRAPPER_CLASS(GetObjectClass(obj)) might be false.
// Avoid calling them if IS_WRAPPER_CLASS(GetObjectClass(obj)) can only be
// true, as we'd do a redundant call to IS_WRAPPER_CLASS.
#define IS_WN_WRAPPER(obj)                                                    \
    (IS_WRAPPER_CLASS(js::GetObjectClass(obj)) && IS_WN_WRAPPER_OBJECT(obj))
#define IS_SLIM_WRAPPER(obj)                                                  \
    (IS_WRAPPER_CLASS(js::GetObjectClass(obj)) && IS_SLIM_WRAPPER_OBJECT(obj))

inline JSObject *
xpc_GetGlobalForObject(JSObject *obj)
{
    while (JSObject *parent = js::GetObjectParent(obj))
        obj = parent;
    return obj;
}

extern bool
xpc_OkToHandOutWrapper(nsWrapperCache *cache);

inline JSObject*
xpc_FastGetCachedWrapper(nsWrapperCache *cache, JSObject *scope, jsval *vp)
{
    if (cache) {
        JSObject* wrapper = cache->GetWrapper();
        NS_ASSERTION(!wrapper ||
                     !cache->IsProxy() ||
                     !IS_SLIM_WRAPPER(wrapper),
                     "Should never have a slim wrapper when IsProxy()");
        if (wrapper &&
            js::GetObjectCompartment(wrapper) == js::GetObjectCompartment(scope) &&
            (IS_SLIM_WRAPPER(wrapper) ||
             xpc_OkToHandOutWrapper(cache))) {
            *vp = OBJECT_TO_JSVAL(wrapper);
            return wrapper;
        }
    }

    return nsnull;
}

inline JSObject*
xpc_FastGetCachedWrapper(nsWrapperCache *cache, JSObject *scope)
{
    jsval dummy;
    return xpc_FastGetCachedWrapper(cache, scope, &dummy);
}

// The JS GC marks objects gray that are held alive directly or
// indirectly by an XPConnect root. The cycle collector explores only
// this subset of the JS heap.  JSStaticAtoms cause this to crash,
// because they are statically allocated in the data segment and thus
// are not really GCThings.
inline JSBool
xpc_IsGrayGCThing(void *thing)
{
    return js_GCThingIsMarked(thing, js::gc::GRAY);
}

// The cycle collector only cares about JS objects and XML objects that
// are held alive directly or indirectly by an XPConnect root.  This
// version is preferred to xpc_IsGrayGCThing when it isn't known if thing
// is a JSString or not. Implemented in nsXPConnect.cpp.
extern JSBool
xpc_GCThingIsGrayCCThing(void *thing);

// Implemented in nsXPConnect.cpp.
extern void
xpc_UnmarkGrayObjectRecursive(JSObject* obj);

// Remove the gray color from the given JSObject and any other objects that can
// be reached through it.
inline void
xpc_UnmarkGrayObject(JSObject *obj)
{
    if (obj && xpc_IsGrayGCThing(obj))
        xpc_UnmarkGrayObjectRecursive(obj);
}

// No JS can be on the stack when this is called. Probably only useful from
// xpcshell.
NS_EXPORT_(void)
xpc_ActivateDebugMode();

class nsIMemoryMultiReporterCallback;

namespace mozilla {
namespace xpconnect {
namespace memory {

struct CompartmentStats
{
    CompartmentStats(JSContext *cx, JSCompartment *c);

    nsCString name;
    PRInt64 gcHeapArenaHeaders;
    PRInt64 gcHeapArenaPadding;
    PRInt64 gcHeapArenaUnused;

    PRInt64 gcHeapObjectsNonFunction;
    PRInt64 gcHeapObjectsFunction;
    PRInt64 gcHeapStrings;
    PRInt64 gcHeapShapesTree;
    PRInt64 gcHeapShapesDict;
    PRInt64 gcHeapShapesBase;
    PRInt64 gcHeapScripts;
    PRInt64 gcHeapTypeObjects;
    PRInt64 gcHeapXML;

    PRInt64 objectSlots;
    PRInt64 stringChars;
    PRInt64 shapesExtraTreeTables;
    PRInt64 shapesExtraDictTables;
    PRInt64 shapesExtraTreeShapeKids;
    PRInt64 shapesCompartmentTables;
    PRInt64 scriptData;

#ifdef JS_METHODJIT
    PRInt64 mjitCode;
    PRInt64 mjitData;
#endif
    TypeInferenceMemoryStats typeInferenceMemory;
};

struct IterateData
{
    IterateData()
      : runtimeObject(0),
        runtimeAtomsTable(0),
        runtimeContexts(0),
        runtimeThreadsNormal(0),
        runtimeThreadsTemporary(0),
        runtimeThreadsRegexpCode(0),
        runtimeThreadsStackCommitted(0),
        xpconnect(0),
        gcHeapChunkTotal(0),
        gcHeapChunkCleanUnused(0),
        gcHeapChunkDirtyUnused(0),
        gcHeapChunkCleanDecommitted(0),
        gcHeapChunkDirtyDecommitted(0),
        gcHeapArenaUnused(0),
        gcHeapChunkAdmin(0),
        gcHeapUnusedPercentage(0),
        totalObjects(0),
        totalShapes(0),
        totalScripts(0),
        totalStrings(0),
#ifdef JS_METHODJIT
        totalMjit(0),
#endif
        totalTypeInference(0),
        totalAnalysisTemp(0),
        compartmentStatsVector(),
        currCompartmentStats(NULL) { }

    PRInt64 runtimeObject;
    PRInt64 runtimeAtomsTable;
    PRInt64 runtimeContexts;
    PRInt64 runtimeThreadsNormal;
    PRInt64 runtimeThreadsTemporary;
    PRInt64 runtimeThreadsRegexpCode;
    PRInt64 runtimeThreadsStackCommitted;
    PRInt64 xpconnect;
    PRInt64 gcHeapChunkTotal;
    PRInt64 gcHeapChunkCleanUnused;
    PRInt64 gcHeapChunkDirtyUnused;
    PRInt64 gcHeapChunkCleanDecommitted;
    PRInt64 gcHeapChunkDirtyDecommitted;
    PRInt64 gcHeapArenaUnused;
    PRInt64 gcHeapChunkAdmin;
    PRInt64 gcHeapUnusedPercentage;
    PRInt64 totalObjects;
    PRInt64 totalShapes;
    PRInt64 totalScripts;
    PRInt64 totalStrings;
#ifdef JS_METHODJIT
    PRInt64 totalMjit;
#endif
    PRInt64 totalTypeInference;
    PRInt64 totalAnalysisTemp;

    nsTArray<CompartmentStats> compartmentStatsVector;
    CompartmentStats *currCompartmentStats;
};

JSBool
CollectCompartmentStatsForRuntime(JSRuntime *rt, IterateData *data);

void
ReportJSRuntimeStats(const IterateData &data, const nsACString &pathPrefix,
                     nsIMemoryMultiReporterCallback *callback,
                     nsISupports *closure);

} // namespace memory
} // namespace xpconnect

namespace dom {
namespace binding {

extern int HandlerFamily;
inline void* ProxyFamily() { return &HandlerFamily; }
inline bool instanceIsProxy(JSObject *obj)
{
    return js::IsProxy(obj) &&
           js::GetProxyHandler(obj)->family() == ProxyFamily();
}
extern JSClass ExpandoClass;
inline bool isExpandoObject(JSObject *obj)
{
    return js::GetObjectJSClass(obj) == &ExpandoClass;
}

enum {
    JSPROXYSLOT_PROTOSHAPE = 0,
    JSPROXYSLOT_EXPANDO = 1
};

typedef JSObject*
(*DefineInterface)(JSContext *cx, XPCWrappedNativeScope *scope, bool *enabled);

extern bool
DefineStaticJSVals(JSContext *cx);
void
Register(nsDOMClassInfoData *aData);
extern bool
DefineConstructor(JSContext *cx, JSObject *obj, DefineInterface aDefine,
                  nsresult *aResult);

} // namespace binding
} // namespace dom
} // namespace mozilla

#endif
