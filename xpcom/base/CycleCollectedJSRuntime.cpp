/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// We're dividing JS objects into 3 categories:
//
// 1. "real" roots, held by the JS engine itself or rooted through the root
//    and lock JS APIs. Roots from this category are considered black in the
//    cycle collector, any cycle they participate in is uncollectable.
//
// 2. certain roots held by C++ objects that are guaranteed to be alive.
//    Roots from this category are considered black in the cycle collector,
//    and any cycle they participate in is uncollectable. These roots are
//    traced from TraceNativeBlackRoots.
//
// 3. all other roots held by C++ objects that participate in cycle
//    collection, held by us (see TraceNativeGrayRoots). Roots from this
//    category are considered grey in the cycle collector; whether or not
//    they are collected depends on the objects that hold them.
//
// Note that if a root is in multiple categories the fact that it is in
// category 1 or 2 that takes precedence, so it will be considered black.
//
// During garbage collection we switch to an additional mark color (gray)
// when tracing inside TraceNativeGrayRoots. This allows us to walk those
// roots later on and add all objects reachable only from them to the
// cycle collector.
//
// Phases:
//
// 1. marking of the roots in category 1 by having the JS GC do its marking
// 2. marking of the roots in category 2 by having the JS GC call us back
//    (via JS_SetExtraGCRootsTracer) and running TraceNativeBlackRoots
// 3. marking of the roots in category 3 by TraceNativeGrayRoots using an
//    additional color (gray).
// 4. end of GC, GC can sweep its heap
//
// At some later point, when the cycle collector runs:
//
// 5. walk gray objects and add them to the cycle collector, cycle collect
//
// JS objects that are part of cycles the cycle collector breaks will be
// collected by the next JS GC.
//
// If WantAllTraces() is false the cycle collector will not traverse roots
// from category 1 or any JS objects held by them. Any JS objects they hold
// will already be marked by the JS GC and will thus be colored black
// themselves. Any C++ objects they hold will have a missing (untraversed)
// edge from the JS object to the C++ object and so it will be marked black
// too. This decreases the number of objects that the cycle collector has to
// deal with.
// To improve debugging, if WantAllTraces() is true all JS objects are
// traversed.

#include "mozilla/CycleCollectedJSRuntime.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/DOMJSClass.h"
#include "jsfriendapi.h"
#include "jsprf.h"
#include "nsCycleCollectionNoteRootCallback.h"
#include "nsCycleCollectionParticipant.h"
#include "nsCycleCollector.h"
#include "nsDOMJSUtils.h"
#include "nsLayoutStatics.h"
#include "xpcpublic.h"

using namespace mozilla;
using namespace mozilla::dom;

inline bool
AddToCCKind(JSGCTraceKind kind)
{
  return kind == JSTRACE_OBJECT || kind == JSTRACE_SCRIPT;
}

struct NoteWeakMapChildrenTracer : public JSTracer
{
  NoteWeakMapChildrenTracer(nsCycleCollectionNoteRootCallback& cb)
  : mCb(cb)
  {
  }
  nsCycleCollectionNoteRootCallback& mCb;
  bool mTracedAny;
  JSObject* mMap;
  void* mKey;
  void* mKeyDelegate;
};

static void
TraceWeakMappingChild(JSTracer* trc, void** thingp, JSGCTraceKind kind)
{
  MOZ_ASSERT(trc->callback == TraceWeakMappingChild);
  void* thing = *thingp;
  NoteWeakMapChildrenTracer* tracer =
    static_cast<NoteWeakMapChildrenTracer*>(trc);

  if (kind == JSTRACE_STRING) {
    return;
  }

  if (!xpc_IsGrayGCThing(thing) && !tracer->mCb.WantAllTraces()) {
    return;
  }

  if (AddToCCKind(kind)) {
    tracer->mCb.NoteWeakMapping(tracer->mMap, tracer->mKey, tracer->mKeyDelegate, thing);
    tracer->mTracedAny = true;
  } else {
    JS_TraceChildren(trc, thing, kind);
  }
}

struct NoteWeakMapsTracer : public js::WeakMapTracer
{
  NoteWeakMapsTracer(JSRuntime* rt, js::WeakMapTraceCallback cb,
                     nsCycleCollectionNoteRootCallback& cccb)
  : js::WeakMapTracer(rt, cb), mCb(cccb), mChildTracer(cccb)
  {
    JS_TracerInit(&mChildTracer, rt, TraceWeakMappingChild);
  }
  nsCycleCollectionNoteRootCallback& mCb;
  NoteWeakMapChildrenTracer mChildTracer;
};

static void
TraceWeakMapping(js::WeakMapTracer* trc, JSObject* m,
               void* k, JSGCTraceKind kkind,
               void* v, JSGCTraceKind vkind)
{
  MOZ_ASSERT(trc->callback == TraceWeakMapping);
  NoteWeakMapsTracer* tracer = static_cast<NoteWeakMapsTracer* >(trc);

  // If nothing that could be held alive by this entry is marked gray, return.
  if ((!k || !xpc_IsGrayGCThing(k)) && MOZ_LIKELY(!tracer->mCb.WantAllTraces())) {
    if (!v || !xpc_IsGrayGCThing(v) || vkind == JSTRACE_STRING) {
      return;
    }
  }

  // The cycle collector can only properly reason about weak maps if it can
  // reason about the liveness of their keys, which in turn requires that
  // the key can be represented in the cycle collector graph.  All existing
  // uses of weak maps use either objects or scripts as keys, which are okay.
  MOZ_ASSERT(AddToCCKind(kkind));

  // As an emergency fallback for non-debug builds, if the key is not
  // representable in the cycle collector graph, we treat it as marked.  This
  // can cause leaks, but is preferable to ignoring the binding, which could
  // cause the cycle collector to free live objects.
  if (!AddToCCKind(kkind)) {
    k = nullptr;
  }

  JSObject* kdelegate = nullptr;
  if (k && kkind == JSTRACE_OBJECT) {
    kdelegate = js::GetWeakmapKeyDelegate((JSObject*)k);
  }

  if (AddToCCKind(vkind)) {
    tracer->mCb.NoteWeakMapping(m, k, kdelegate, v);
  } else {
    tracer->mChildTracer.mTracedAny = false;
    tracer->mChildTracer.mMap = m;
    tracer->mChildTracer.mKey = k;
    tracer->mChildTracer.mKeyDelegate = kdelegate;

    if (v && vkind != JSTRACE_STRING) {
      JS_TraceChildren(&tracer->mChildTracer, v, vkind);
    }

    // The delegate could hold alive the key, so report something to the CC
    // if we haven't already.
    if (!tracer->mChildTracer.mTracedAny && k && xpc_IsGrayGCThing(k) && kdelegate) {
      tracer->mCb.NoteWeakMapping(m, k, kdelegate, nullptr);
    }
  }
}

// This is based on the logic in TraceWeakMapping.
struct FixWeakMappingGrayBitsTracer : public js::WeakMapTracer
{
  FixWeakMappingGrayBitsTracer(JSRuntime* rt)
    : js::WeakMapTracer(rt, FixWeakMappingGrayBits)
  {}

  void
  FixAll()
  {
    do {
      mAnyMarked = false;
      js::TraceWeakMaps(this);
    } while (mAnyMarked);
  }

private:

  static void
  FixWeakMappingGrayBits(js::WeakMapTracer* trc, JSObject* m,
                         void* k, JSGCTraceKind kkind,
                         void* v, JSGCTraceKind vkind)
  {
    MOZ_ASSERT(!JS::IsIncrementalGCInProgress(trc->runtime),
               "Don't call FixWeakMappingGrayBits during a GC.");

    FixWeakMappingGrayBitsTracer* tracer = static_cast<FixWeakMappingGrayBitsTracer*>(trc);

    // If nothing that could be held alive by this entry is marked gray, return.
    bool delegateMightNeedMarking = k && xpc_IsGrayGCThing(k);
    bool valueMightNeedMarking = v && xpc_IsGrayGCThing(v) && vkind != JSTRACE_STRING;
    if (!delegateMightNeedMarking && !valueMightNeedMarking) {
      return;
    }

    if (!AddToCCKind(kkind)) {
      k = nullptr;
    }

    if (delegateMightNeedMarking && kkind == JSTRACE_OBJECT) {
      JSObject* kdelegate = js::GetWeakmapKeyDelegate((JSObject*)k);
      if (kdelegate && !xpc_IsGrayGCThing(kdelegate)) {
        JS::UnmarkGrayGCThingRecursively(k, JSTRACE_OBJECT);
        tracer->mAnyMarked = true;
      }
    }

    if (v && xpc_IsGrayGCThing(v) &&
        (!k || !xpc_IsGrayGCThing(k)) &&
        (!m || !xpc_IsGrayGCThing(m)) &&
        vkind != JSTRACE_SHAPE) {
      JS::UnmarkGrayGCThingRecursively(v, vkind);
      tracer->mAnyMarked = true;
    }
  }

  bool mAnyMarked;
};

class JSContextParticipant : public nsCycleCollectionParticipant
{
public:
  static NS_METHOD RootImpl(void *n)
  {
    return NS_OK;
  }
  static NS_METHOD UnlinkImpl(void *n)
  {
    return NS_OK;
  }
  static NS_METHOD UnrootImpl(void *n)
  {
    return NS_OK;
  }
  static NS_METHOD_(void) UnmarkIfPurpleImpl(void *n)
  {
  }
  static NS_METHOD TraverseImpl(JSContextParticipant *that, void *n,
                                nsCycleCollectionTraversalCallback &cb)
  {
    JSContext *cx = static_cast<JSContext*>(n);

    // JSContexts do not have an internal refcount and always have a single
    // owner (e.g., nsJSContext). Thus, the default refcount is 1. However,
    // in the (abnormal) case of synchronous cycle-collection, the context
    // may be actively executing code in which case we want to treat it as
    // rooted by adding an extra refcount.
    unsigned refCount = js::ContextHasOutstandingRequests(cx) ? 2 : 1;

    cb.DescribeRefCountedNode(refCount, "JSContext");
    if (JSObject *global = js::GetDefaultGlobalForContext(cx)) {
      NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "[global object]");
      cb.NoteJSChild(global);
    }

    return NS_OK;
  }
};

static const CCParticipantVTable<JSContextParticipant>::Type
JSContext_cycleCollectorGlobal =
{
  NS_IMPL_CYCLE_COLLECTION_NATIVE_VTABLE(JSContextParticipant)
};

struct Closure
{
  bool cycleCollectionEnabled;
  nsCycleCollectionNoteRootCallback *cb;
};

static void
CheckParticipatesInCycleCollection(void *aThing, const char *name, void *aClosure)
{
  Closure *closure = static_cast<Closure*>(aClosure);

  if (closure->cycleCollectionEnabled) {
    return;
  }

  if (AddToCCKind(js::GCThingTraceKind(aThing)) &&
      xpc_IsGrayGCThing(aThing))
  {
    closure->cycleCollectionEnabled = true;
  }
}

static PLDHashOperator
NoteJSHolder(void *holder, nsScriptObjectTracer *&tracer, void *arg)
{
  Closure *closure = static_cast<Closure*>(arg);

  closure->cycleCollectionEnabled = false;
  tracer->Trace(holder, TraceCallbackFunc(CheckParticipatesInCycleCollection), closure);
  if (closure->cycleCollectionEnabled) {
    closure->cb->NoteNativeRoot(holder, tracer);
  }

  return PL_DHASH_NEXT;
}

NS_METHOD
JSGCThingParticipant::TraverseImpl(JSGCThingParticipant* that, void* p,
                                   nsCycleCollectionTraversalCallback& cb)
{
  CycleCollectedJSRuntime* runtime = reinterpret_cast<CycleCollectedJSRuntime*>
    (reinterpret_cast<char*>(that) -
     offsetof(CycleCollectedJSRuntime, mGCThingCycleCollectorGlobal));

  runtime->TraverseGCThing(CycleCollectedJSRuntime::TRAVERSE_FULL,
                           p, js::GCThingTraceKind(p), cb);
  return NS_OK;
}

// NB: This is only used to initialize the participant in
// CycleCollectedJSRuntime. It should never be used directly.
static const CCParticipantVTable<JSGCThingParticipant>::Type
sGCThingCycleCollectorGlobal =
{
  NS_IMPL_CYCLE_COLLECTION_NATIVE_VTABLE(JSGCThingParticipant)
};

NS_METHOD
JSZoneParticipant::TraverseImpl(JSZoneParticipant* that, void* p,
                                nsCycleCollectionTraversalCallback& cb)
{
  CycleCollectedJSRuntime* runtime = reinterpret_cast<CycleCollectedJSRuntime*>
    (reinterpret_cast<char*>(that) -
     offsetof(CycleCollectedJSRuntime, mJSZoneCycleCollectorGlobal));

  MOZ_ASSERT(!cb.WantAllTraces());
  JS::Zone* zone = static_cast<JS::Zone*>(p);

  runtime->TraverseZone(zone, cb);
  return NS_OK;
}

struct TraversalTracer : public JSTracer
{
  TraversalTracer(nsCycleCollectionTraversalCallback& aCb) : mCb(aCb)
  {
  }
  nsCycleCollectionTraversalCallback& mCb;
};

static void
NoteJSChild(JSTracer* aTrc, void* aThing, JSGCTraceKind aTraceKind)
{
  TraversalTracer* tracer = static_cast<TraversalTracer*>(aTrc);

  // Don't traverse non-gray objects, unless we want all traces.
  if (!xpc_IsGrayGCThing(aThing) && !tracer->mCb.WantAllTraces()) {
    return;
  }

  /*
   * This function needs to be careful to avoid stack overflow. Normally, when
   * AddToCCKind is true, the recursion terminates immediately as we just add
   * |thing| to the CC graph. So overflow is only possible when there are long
   * chains of non-AddToCCKind GC things. Currently, this only can happen via
   * shape parent pointers. The special JSTRACE_SHAPE case below handles
   * parent pointers iteratively, rather than recursively, to avoid overflow.
   */
if (AddToCCKind(aTraceKind)) {
    if (MOZ_UNLIKELY(tracer->mCb.WantDebugInfo())) {
      // based on DumpNotify in jsapi.c
      if (tracer->debugPrinter) {
        char buffer[200];
        tracer->debugPrinter(aTrc, buffer, sizeof(buffer));
        tracer->mCb.NoteNextEdgeName(buffer);
      } else if (tracer->debugPrintIndex != (size_t)-1) {
        char buffer[200];
        JS_snprintf(buffer, sizeof(buffer), "%s[%lu]",
                    static_cast<const char *>(tracer->debugPrintArg),
                    tracer->debugPrintIndex);
        tracer->mCb.NoteNextEdgeName(buffer);
      } else {
        tracer->mCb.NoteNextEdgeName(static_cast<const char*>(tracer->debugPrintArg));
      }
    }
    tracer->mCb.NoteJSChild(aThing);
  } else if (aTraceKind == JSTRACE_SHAPE) {
    JS_TraceShapeCycleCollectorChildren(aTrc, aThing);
  } else if (aTraceKind != JSTRACE_STRING) {
    JS_TraceChildren(aTrc, aThing, aTraceKind);
  }
}

static void
NoteJSChildTracerShim(JSTracer* aTrc, void** aThingp, JSGCTraceKind aTraceKind)
{
  NoteJSChild(aTrc, *aThingp, aTraceKind);
}

static void
NoteJSChildGrayWrapperShim(void* aData, void* aThing)
{
  TraversalTracer* trc = static_cast<TraversalTracer*>(aData);
  NoteJSChild(trc, aThing, js::GCThingTraceKind(aThing));
}

/*
 * The cycle collection participant for a Zone is intended to produce the same
 * results as if all of the gray GCthings in a zone were merged into a single node,
 * except for self-edges. This avoids the overhead of representing all of the GCthings in
 * the zone in the cycle collector graph, which should be much faster if many of
 * the GCthings in the zone are gray.
 *
 * Zone merging should not always be used, because it is a conservative
 * approximation of the true cycle collector graph that can incorrectly identify some
 * garbage objects as being live. For instance, consider two cycles that pass through a
 * zone, where one is garbage and the other is live. If we merge the entire
 * zone, the cycle collector will think that both are alive.
 *
 * We don't have to worry about losing track of a garbage cycle, because any such garbage
 * cycle incorrectly identified as live must contain at least one C++ to JS edge, and
 * XPConnect will always add the C++ object to the CC graph. (This is in contrast to pure
 * C++ garbage cycles, which must always be properly identified, because we clear the
 * purple buffer during every CC, which may contain the last reference to a garbage
 * cycle.)
 */

// NB: This is only used to initialize the participant in
// CycleCollectedJSRuntime. It should never be used directly.
static const CCParticipantVTable<JSZoneParticipant>::Type
sJSZoneCycleCollectorGlobal = {
  NS_IMPL_CYCLE_COLLECTION_NATIVE_VTABLE(JSZoneParticipant)
};

CycleCollectedJSRuntime::CycleCollectedJSRuntime(uint32_t aMaxbytes,
                                                 JSUseHelperThreads aUseHelperThreads,
                                                 bool aExpectUnrootedGlobals)
  : mGCThingCycleCollectorGlobal(sGCThingCycleCollectorGlobal),
    mJSZoneCycleCollectorGlobal(sJSZoneCycleCollectorGlobal),
    mJSRuntime(nullptr)
#ifdef DEBUG
  , mObjectToUnlink(nullptr)
  , mExpectUnrootedGlobals(aExpectUnrootedGlobals)
#endif
{
  mJSRuntime = JS_NewRuntime(aMaxbytes, aUseHelperThreads);
  if (!mJSRuntime) {
    MOZ_CRASH();
  }

  if (!JS_AddExtraGCRootsTracer(mJSRuntime, TraceBlackJS, this)) {
    MOZ_CRASH();
  }
  JS_SetGrayGCRootsTracer(mJSRuntime, TraceGrayJS, this);

  mJSHolders.Init(512);

  nsCycleCollector_registerJSRuntime(this);
}

CycleCollectedJSRuntime::~CycleCollectedJSRuntime()
{
  nsCycleCollector_forgetJSRuntime();

  JS_DestroyRuntime(mJSRuntime);
  mJSRuntime = nullptr;
}

size_t
CycleCollectedJSRuntime::SizeOfExcludingThis(nsMallocSizeOfFun aMallocSizeOf) const
{
  size_t n = 0;

  // NULL for the second arg;  we're not measuring anything hanging off the
  // entries in mJSHolders.
  n += mJSHolders.SizeOfExcludingThis(nullptr, aMallocSizeOf);

  return n;
}

static PLDHashOperator
UnmarkJSHolder(void* holder, nsScriptObjectTracer*& tracer, void* arg)
{
  tracer->CanSkip(holder, true);
  return PL_DHASH_NEXT;
}

void
CycleCollectedJSRuntime::UnmarkSkippableJSHolders()
{
  mJSHolders.Enumerate(UnmarkJSHolder, nullptr);
}

void
CycleCollectedJSRuntime::MaybeTraceGlobals(JSTracer* aTracer) const
{
  JSContext* iter = nullptr;
  while (JSContext* acx = JS_ContextIterator(Runtime(), &iter)) {
    MOZ_ASSERT(js::HasUnrootedGlobal(acx) == mExpectUnrootedGlobals);
    if (!js::HasUnrootedGlobal(acx)) {
      continue;
    }

    if (JSObject* global = js::GetDefaultGlobalForContext(acx)) {
      JS_CallObjectTracer(aTracer, &global, "Global Object");
    }
  }
}

void
CycleCollectedJSRuntime::DescribeGCThing(bool aIsMarked, void* aThing,
                                         JSGCTraceKind aTraceKind,
                                         nsCycleCollectionTraversalCallback& aCb) const
{
  if (!aCb.WantDebugInfo()) {
    aCb.DescribeGCedNode(aIsMarked, "JS Object");
    return;
  }

  char name[72];
  if (aTraceKind == JSTRACE_OBJECT) {
    JSObject* obj = static_cast<JSObject*>(aThing);
    js::Class* clasp = js::GetObjectClass(obj);

    // Give the subclass a chance to do something
    if (DescribeCustomObjects(obj, clasp, name)) {
      // Nothing else to do!
    } else if (js::IsFunctionObject(obj)) {
      JSFunction* fun = JS_GetObjectFunction(obj);
      JSString* str = JS_GetFunctionDisplayId(fun);
      if (str) {
        NS_ConvertUTF16toUTF8 fname(JS_GetInternedStringChars(str));
        JS_snprintf(name, sizeof(name),
                    "JS Object (Function - %s)", fname.get());
      } else {
        JS_snprintf(name, sizeof(name), "JS Object (Function)");
      }
    } else {
      JS_snprintf(name, sizeof(name), "JS Object (%s)",
                  clasp->name);
    }
  } else {
    static const char trace_types[][11] = {
      "Object",
      "String",
      "Script",
      "LazyScript",
      "IonCode",
      "Shape",
      "BaseShape",
      "TypeObject",
    };
    JS_STATIC_ASSERT(NS_ARRAY_LENGTH(trace_types) == JSTRACE_LAST + 1);
    JS_snprintf(name, sizeof(name), "JS %s", trace_types[aTraceKind]);
  }

  // Disable printing global for objects while we figure out ObjShrink fallout.
  aCb.DescribeGCedNode(aIsMarked, name);
}

void
CycleCollectedJSRuntime::NoteGCThingJSChildren(void* aThing,
                                               JSGCTraceKind aTraceKind,
                                               nsCycleCollectionTraversalCallback& aCb) const
{
  MOZ_ASSERT(mJSRuntime);
  TraversalTracer trc(aCb);
  JS_TracerInit(&trc, mJSRuntime, NoteJSChildTracerShim);
  trc.eagerlyTraceWeakMaps = DoNotTraceWeakMaps;
  JS_TraceChildren(&trc, aThing, aTraceKind);
}

void
CycleCollectedJSRuntime::NoteGCThingXPCOMChildren(js::Class* aClasp, JSObject* aObj,
                                                  nsCycleCollectionTraversalCallback& aCb) const
{
  MOZ_ASSERT(aClasp);
  MOZ_ASSERT(aClasp == js::GetObjectClass(aObj));

  if (NoteCustomGCThingXPCOMChildren(aClasp, aObj, aCb)) {
    // Nothing else to do!
    return;
  }
  // XXX This test does seem fragile, we should probably whitelist classes
  //     that do hold a strong reference, but that might not be possible.
  else if (aClasp->flags & JSCLASS_HAS_PRIVATE &&
           aClasp->flags & JSCLASS_PRIVATE_IS_NSISUPPORTS) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(aCb, "js::GetObjectPrivate(obj)");
    aCb.NoteXPCOMChild(static_cast<nsISupports*>(js::GetObjectPrivate(aObj)));
  } else {
    const DOMClass* domClass = GetDOMClass(aObj);
    if (domClass) {
      NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(aCb, "UnwrapDOMObject(obj)");
      if (domClass->mDOMObjectIsISupports) {
        aCb.NoteXPCOMChild(UnwrapDOMObject<nsISupports>(aObj));
      } else if (domClass->mParticipant) {
        aCb.NoteNativeChild(UnwrapDOMObject<void>(aObj),
                            domClass->mParticipant);
      }
    }
  }
}

void
CycleCollectedJSRuntime::TraverseGCThing(TraverseSelect aTs, void* aThing,
                                         JSGCTraceKind aTraceKind,
                                         nsCycleCollectionTraversalCallback& aCb)
{
  MOZ_ASSERT(aTraceKind == js::GCThingTraceKind(aThing));
  bool isMarkedGray = xpc_IsGrayGCThing(aThing);

  if (aTs == TRAVERSE_FULL) {
    DescribeGCThing(!isMarkedGray, aThing, aTraceKind, aCb);
  }

  // If this object is alive, then all of its children are alive. For JS objects,
  // the black-gray invariant ensures the children are also marked black. For C++
  // objects, the ref count from this object will keep them alive. Thus we don't
  // need to trace our children, unless we are debugging using WantAllTraces.
  if (!isMarkedGray && !aCb.WantAllTraces()) {
    return;
  }

  if (aTs == TRAVERSE_FULL) {
    NoteGCThingJSChildren(aThing, aTraceKind, aCb);
  }

  if (aTraceKind == JSTRACE_OBJECT) {
    JSObject* obj = static_cast<JSObject*>(aThing);
    NoteGCThingXPCOMChildren(js::GetObjectClass(obj), obj, aCb);
  }
}

struct TraverseObjectShimClosure {
  nsCycleCollectionTraversalCallback& cb;
  CycleCollectedJSRuntime* self;
};

void
CycleCollectedJSRuntime::TraverseZone(JS::Zone* aZone,
                                      nsCycleCollectionTraversalCallback& aCb)
{
  /*
   * We treat the zone as being gray. We handle non-gray GCthings in the
   * zone by not reporting their children to the CC. The black-gray invariant
   * ensures that any JS children will also be non-gray, and thus don't need to be
   * added to the graph. For C++ children, not representing the edge from the
   * non-gray JS GCthings to the C++ object will keep the child alive.
   *
   * We don't allow zone merging in a WantAllTraces CC, because then these
   * assumptions don't hold.
   */
  aCb.DescribeGCedNode(false, "JS Zone");

  /*
   * Every JS child of everything in the zone is either in the zone
   * or is a cross-compartment wrapper. In the former case, we don't need to
   * represent these edges in the CC graph because JS objects are not ref counted.
   * In the latter case, the JS engine keeps a map of these wrappers, which we
   * iterate over. Edges between compartments in the same zone will add
   * unnecessary loop edges to the graph (bug 842137).
   */
  TraversalTracer trc(aCb);
  JS_TracerInit(&trc, mJSRuntime, NoteJSChildTracerShim);
  trc.eagerlyTraceWeakMaps = DoNotTraceWeakMaps;
  js::VisitGrayWrapperTargets(aZone, NoteJSChildGrayWrapperShim, &trc);

  /*
   * To find C++ children of things in the zone, we scan every JS Object in
   * the zone. Only JS Objects can have C++ children.
   */
  TraverseObjectShimClosure closure = { aCb, this };
  js::IterateGrayObjects(aZone, TraverseObjectShim, &closure);
}

/* static */ void
CycleCollectedJSRuntime::TraverseObjectShim(void* aData, void* aThing)
{
  TraverseObjectShimClosure* closure =
      static_cast<TraverseObjectShimClosure*>(aData);

  MOZ_ASSERT(js::GCThingTraceKind(aThing) == JSTRACE_OBJECT);
  closure->self->TraverseGCThing(CycleCollectedJSRuntime::TRAVERSE_CPP, aThing,
                                 JSTRACE_OBJECT, closure->cb);
}

// For all JS objects that are held by native objects but aren't held
// through rooting or locking, we need to add all the native objects that
// hold them so that the JS objects are colored correctly in the cycle
// collector. This includes JSContexts that don't have outstanding requests,
// because their global object wasn't marked by the JS GC. All other JS
// roots were marked by the JS GC and will be colored correctly in the cycle
// collector.
void
CycleCollectedJSRuntime::MaybeTraverseGlobals(nsCycleCollectionNoteRootCallback& aCb) const
{
  JSContext *iter = nullptr, *acx;
  while ((acx = JS_ContextIterator(Runtime(), &iter))) {
    // Add the context to the CC graph only if traversing it would
    // end up doing something.
    JSObject* global = js::GetDefaultGlobalForContext(acx);
    if (global && xpc_IsGrayGCThing(global)) {
      aCb.NoteNativeRoot(acx, JSContextParticipant());
    }
  }
}

void
CycleCollectedJSRuntime::TraverseNativeRoots(nsCycleCollectionNoteRootCallback& aCb)
{
  MaybeTraverseGlobals(aCb);

  // NB: This is here just to preserve the existing XPConnect order. I doubt it
  // would hurt to do this after the JS holders.
  TraverseAdditionalNativeRoots(aCb);

  Closure closure = { true, &aCb };
  mJSHolders.Enumerate(NoteJSHolder, &closure);
}

/* static */ void
CycleCollectedJSRuntime::TraceBlackJS(JSTracer* aTracer, void* aData)
{
  CycleCollectedJSRuntime* self = static_cast<CycleCollectedJSRuntime*>(aData);

  self->TraceNativeBlackRoots(aTracer);
}

/* static */ void
CycleCollectedJSRuntime::TraceGrayJS(JSTracer* aTracer, void* aData)
{
  CycleCollectedJSRuntime* self = static_cast<CycleCollectedJSRuntime*>(aData);

  // Mark these roots as gray so the CC can walk them later.
  self->TraceNativeGrayRoots(aTracer);
}

struct JsGcTracer : public TraceCallbacks
{
  virtual void Trace(JS::Heap<JS::Value> *p, const char *name, void *closure) const MOZ_OVERRIDE {
    JS_CallHeapValueTracer(static_cast<JSTracer*>(closure), p, name);
  }
  virtual void Trace(JS::Heap<jsid> *p, const char *name, void *closure) const MOZ_OVERRIDE {
    JS_CallHeapIdTracer(static_cast<JSTracer*>(closure), p, name);
  }
  virtual void Trace(JS::Heap<JSObject *> *p, const char *name, void *closure) const MOZ_OVERRIDE {
    JS_CallHeapObjectTracer(static_cast<JSTracer*>(closure), p, name);
  }
  virtual void Trace(JS::Heap<JSString *> *p, const char *name, void *closure) const MOZ_OVERRIDE {
    JS_CallHeapStringTracer(static_cast<JSTracer*>(closure), p, name);
  }
  virtual void Trace(JS::Heap<JSScript *> *p, const char *name, void *closure) const MOZ_OVERRIDE {
    JS_CallHeapScriptTracer(static_cast<JSTracer*>(closure), p, name);
  }
};

static PLDHashOperator
TraceJSHolder(void* aHolder, nsScriptObjectTracer*& aTracer, void* aArg)
{
  aTracer->Trace(aHolder, JsGcTracer(), aArg);

  return PL_DHASH_NEXT;
}

void
CycleCollectedJSRuntime::TraceNativeGrayRoots(JSTracer* aTracer)
{
  MaybeTraceGlobals(aTracer);

  // NB: This is here just to preserve the existing XPConnect order. I doubt it
  // would hurt to do this after the JS holders.
  TraceAdditionalNativeGrayRoots(aTracer);

  mJSHolders.Enumerate(TraceJSHolder, aTracer);
}

void
CycleCollectedJSRuntime::AddJSHolder(void* aHolder, nsScriptObjectTracer* aTracer)
{
  MOZ_ASSERT(aTracer->Trace, "AddJSHolder needs a non-null Trace function");
  bool wasEmpty = mJSHolders.Count() == 0;
  mJSHolders.Put(aHolder, aTracer);
  if (wasEmpty && mJSHolders.Count() == 1) {
    nsLayoutStatics::AddRef();
  }
}

void
CycleCollectedJSRuntime::RemoveJSHolder(void* aHolder)
{
#ifdef DEBUG
  // Assert that the holder doesn't try to keep any GC things alive.
  // In case of unlinking cycle collector calls AssertNoObjectsToTrace
  // manually because we don't want to check the holder before we are
  // finished unlinking it
  if (aHolder != mObjectToUnlink) {
    AssertNoObjectsToTrace(aHolder);
  }
#endif
  bool hadOne = mJSHolders.Count() == 1;
  mJSHolders.Remove(aHolder);
  if (hadOne && mJSHolders.Count() == 0) {
    nsLayoutStatics::Release();
  }
}

#ifdef DEBUG
bool
CycleCollectedJSRuntime::TestJSHolder(void* aHolder)
{
  return mJSHolders.Get(aHolder, nullptr);
}

static void
AssertNoGcThing(void* aGCThing, const char* aName, void* aClosure)
{
  MOZ_ASSERT(!aGCThing);
}

void
CycleCollectedJSRuntime::AssertNoObjectsToTrace(void* aPossibleJSHolder)
{
  nsScriptObjectTracer* tracer = mJSHolders.Get(aPossibleJSHolder);
  if (tracer && tracer->Trace) {
    tracer->Trace(aPossibleJSHolder, TraceCallbackFunc(AssertNoGcThing), nullptr);
  }
}
#endif

// static
nsCycleCollectionParticipant*
CycleCollectedJSRuntime::JSContextParticipant()
{
  return JSContext_cycleCollectorGlobal.GetParticipant();
}

nsCycleCollectionParticipant*
CycleCollectedJSRuntime::GCThingParticipant() const
{
    return mGCThingCycleCollectorGlobal.GetParticipant();
}

nsCycleCollectionParticipant*
CycleCollectedJSRuntime::ZoneParticipant() const
{
    return mJSZoneCycleCollectorGlobal.GetParticipant();
}

bool
CycleCollectedJSRuntime::NotifyLeaveMainThread() const
{
  MOZ_ASSERT(NS_IsMainThread());
  if (JS_IsInRequest(mJSRuntime)) {
    return false;
  }
  JS_ClearRuntimeThread(mJSRuntime);
  return true;
}

void
CycleCollectedJSRuntime::NotifyEnterCycleCollectionThread() const
{
  MOZ_ASSERT(!NS_IsMainThread());
  JS_SetRuntimeThread(mJSRuntime);
}

void
CycleCollectedJSRuntime::NotifyLeaveCycleCollectionThread() const
{
  MOZ_ASSERT(!NS_IsMainThread());
  JS_ClearRuntimeThread(mJSRuntime);
}

void
CycleCollectedJSRuntime::NotifyEnterMainThread() const
{
  MOZ_ASSERT(NS_IsMainThread());
  JS_SetRuntimeThread(mJSRuntime);
}

nsresult
CycleCollectedJSRuntime::BeginCycleCollection(nsCycleCollectionNoteRootCallback &aCb)
{
  static bool gcHasRun = false;
  if (!gcHasRun) {
    uint32_t gcNumber = JS_GetGCParameter(mJSRuntime, JSGC_NUMBER);
    if (!gcNumber) {
      // Cannot cycle collect if GC has not run first!
      MOZ_CRASH();
    }
    gcHasRun = true;
  }

  TraverseNativeRoots(aCb);

  NoteWeakMapsTracer trc(mJSRuntime, TraceWeakMapping, aCb);
  js::TraceWeakMaps(&trc);

  return NS_OK;
}

/*
 * Return true if there exists a JSContext with a default global whose current
 * inner is gray. The intent is to look for JS Object windows. We don't merge
 * system compartments, so we don't use them to trigger merging CCs.
 */
bool
CycleCollectedJSRuntime::UsefulToMergeZones() const
{
  JSContext* iter = nullptr;
  JSContext* cx;
  JSAutoRequest ar(nsContentUtils::GetSafeJSContext());
  while ((cx = JS_ContextIterator(mJSRuntime, &iter))) {
    // Skip anything without an nsIScriptContext, as well as any scx whose
    // NativeGlobal() is not an outer window (this happens with XUL Prototype
    // compilation scopes, for example, which we're not interested in).
    nsIScriptContext* scx = GetScriptContextFromJSContext(cx);
    JS::RootedObject global(cx, scx ? scx->GetNativeGlobal() : nullptr);
    if (!global || !js::GetObjectParent(global)) {
      continue;
    }
    // Grab the inner from the outer.
    global = JS_ObjectToInnerObject(cx, global);
    MOZ_ASSERT(!js::GetObjectParent(global));
    if (JS::GCThingIsMarkedGray(global) &&
        !js::IsSystemCompartment(js::GetObjectCompartment(global))) {
      return true;
    }
  }
  return false;
}

void
CycleCollectedJSRuntime::FixWeakMappingGrayBits() const
{
  FixWeakMappingGrayBitsTracer fixer(mJSRuntime);
  fixer.FixAll();
}

bool
CycleCollectedJSRuntime::NeedCollect() const
{
  return !js::AreGCGrayBitsValid(mJSRuntime);
}

void
CycleCollectedJSRuntime::Collect(uint32_t aReason) const
{
  MOZ_ASSERT(aReason < JS::gcreason::NUM_REASONS);
  JS::gcreason::Reason gcreason = static_cast<JS::gcreason::Reason>(aReason);

  JS::PrepareForFullGC(mJSRuntime);
  JS::GCForReason(mJSRuntime, gcreason);
}
