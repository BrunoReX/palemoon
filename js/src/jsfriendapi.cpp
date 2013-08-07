/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=99:
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
 * The Original Code is SpiderMonkey code.
 *
 * The Initial Developer of the Original Code is
 *   Mozilla Foundation
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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

#include "jscntxt.h"
#include "jscompartment.h"
#include "jsfriendapi.h"
#include "jswrapper.h"
#include "jsweakmap.h"
#include "jswatchpoint.h"

#include "mozilla/GuardObjects.h"

#include "jsobjinlines.h"

using namespace js;
using namespace JS;

JS_FRIEND_API(void)
JS_SetGrayGCRootsTracer(JSRuntime *rt, JSTraceDataOp traceOp, void *data)
{
    rt->gcGrayRootsTraceOp = traceOp;
    rt->gcGrayRootsData = data;
}

JS_FRIEND_API(JSString *)
JS_GetAnonymousString(JSRuntime *rt)
{
    JS_ASSERT(rt->hasContexts());
    return rt->atomState.anonymousAtom;
}

JS_FRIEND_API(JSObject *)
JS_FindCompilationScope(JSContext *cx, JSObject *obj)
{
    /*
     * We unwrap wrappers here. This is a little weird, but it's what's being
     * asked of us.
     */
    if (obj->isWrapper())
        obj = UnwrapObject(obj);
    
    /*
     * Innerize the target_obj so that we compile in the correct (inner)
     * scope.
     */
    if (JSObjectOp op = obj->getClass()->ext.innerObject)
        obj = op(cx, obj);
    return obj;
}

JS_FRIEND_API(JSFunction *)
JS_GetObjectFunction(JSObject *obj)
{
    if (obj->isFunction())
        return obj->toFunction();
    return NULL;
}

JS_FRIEND_API(JSObject *)
JS_GetGlobalForFrame(JSStackFrame *fp)
{
    return &Valueify(fp)->scopeChain().global();
}

JS_FRIEND_API(JSBool)
JS_SplicePrototype(JSContext *cx, JSObject *obj, JSObject *proto)
{
    /*
     * Change the prototype of an object which hasn't been used anywhere
     * and does not share its type with another object. Unlike JS_SetPrototype,
     * does not nuke type information for the object.
     */
    CHECK_REQUEST(cx);

    if (!obj->hasSingletonType()) {
        /*
         * We can see non-singleton objects when trying to splice prototypes
         * due to mutable __proto__ (ugh).
         */
        return JS_SetPrototype(cx, obj, proto);
    }

    return obj->splicePrototype(cx, proto);
}

JS_FRIEND_API(JSObject *)
JS_NewObjectWithUniqueType(JSContext *cx, JSClass *clasp, JSObject *proto, JSObject *parent)
{
    JSObject *obj = JS_NewObject(cx, clasp, proto, parent);
    if (!obj || !obj->setSingletonType(cx))
        return NULL;
    return obj;
}

JS_FRIEND_API(void)
js::GCForReason(JSContext *cx, gcreason::Reason reason)
{
    js_GC(cx, NULL, GC_NORMAL, reason);
}

JS_FRIEND_API(void)
js::ShrinkingGC(JSContext *cx, gcreason::Reason reason)
{
    js_GC(cx, NULL, GC_SHRINK, reason);
}

JS_FRIEND_API(void)
JS_ShrinkGCBuffers(JSRuntime *rt)
{
    ShrinkGCBuffers(rt);
}

JS_FRIEND_API(JSPrincipals *)
JS_GetCompartmentPrincipals(JSCompartment *compartment)
{
    return compartment->principals;
}

JS_FRIEND_API(JSBool)
JS_WrapPropertyDescriptor(JSContext *cx, js::PropertyDescriptor *desc)
{
    return cx->compartment->wrap(cx, desc);
}

JS_FRIEND_API(void)
JS_TraceShapeCycleCollectorChildren(JSTracer *trc, void *shape)
{
    MarkCycleCollectorChildren(trc, (const Shape *)shape);
}

AutoPreserveCompartment::AutoPreserveCompartment(JSContext *cx
                                                 JS_GUARD_OBJECT_NOTIFIER_PARAM_NO_INIT)
  : cx(cx), oldCompartment(cx->compartment)
{
    JS_GUARD_OBJECT_NOTIFIER_INIT;
}

AutoPreserveCompartment::~AutoPreserveCompartment()
{
    /* The old compartment may have been destroyed, so we can't use cx->setCompartment. */
    cx->compartment = oldCompartment;
}

AutoSwitchCompartment::AutoSwitchCompartment(JSContext *cx, JSCompartment *newCompartment
                                             JS_GUARD_OBJECT_NOTIFIER_PARAM_NO_INIT)
  : cx(cx), oldCompartment(cx->compartment)
{
    JS_GUARD_OBJECT_NOTIFIER_INIT;
    cx->setCompartment(newCompartment);
}

AutoSwitchCompartment::AutoSwitchCompartment(JSContext *cx, JSObject *target
                                             JS_GUARD_OBJECT_NOTIFIER_PARAM_NO_INIT)
  : cx(cx), oldCompartment(cx->compartment)
{
    JS_GUARD_OBJECT_NOTIFIER_INIT;
    cx->setCompartment(target->compartment());
}

AutoSwitchCompartment::~AutoSwitchCompartment()
{
    /* The old compartment may have been destroyed, so we can't use cx->setCompartment. */
    cx->compartment = oldCompartment;
}

JS_FRIEND_API(bool)
js::IsSystemCompartment(const JSCompartment *c)
{
    return c->isSystemCompartment;
}

JS_FRIEND_API(bool)
js::IsAtomsCompartmentFor(const JSContext *cx, const JSCompartment *c)
{
    return c == cx->runtime->atomsCompartment;
}

JS_FRIEND_API(bool)
js::IsScopeObject(JSObject *obj)
{
    return obj->isScope();
}

JS_FRIEND_API(JSObject *)
js::GetObjectParentMaybeScope(JSObject *obj)
{
    return obj->enclosingScope();
}

JS_FRIEND_API(JSObject *)
js::GetGlobalForObjectCrossCompartment(JSObject *obj)
{
    return &obj->global();
}

JS_FRIEND_API(uint32_t)
js::GetObjectSlotSpan(JSObject *obj)
{
    return obj->slotSpan();
}

JS_FRIEND_API(bool)
js::IsObjectInContextCompartment(const JSObject *obj, const JSContext *cx)
{
    return obj->compartment() == cx->compartment;
}

JS_FRIEND_API(bool)
js::IsOriginalScriptFunction(JSFunction *fun)
{
    return fun->script()->function() == fun;
}

JS_FRIEND_API(JSFunction *)
js::DefineFunctionWithReserved(JSContext *cx, JSObject *obj, const char *name, JSNative call,
                               uintN nargs, uintN attrs)
{
    RootObject objRoot(cx, &obj);

    JS_THREADSAFE_ASSERT(cx->compartment != cx->runtime->atomsCompartment);
    CHECK_REQUEST(cx);
    assertSameCompartment(cx, obj);
    JSAtom *atom = js_Atomize(cx, name, strlen(name));
    if (!atom)
        return NULL;
    return js_DefineFunction(cx, objRoot, ATOM_TO_JSID(atom), call, nargs, attrs,
                             JSFunction::ExtendedFinalizeKind);
}

JS_FRIEND_API(JSFunction *)
js::NewFunctionWithReserved(JSContext *cx, JSNative native, uintN nargs, uintN flags,
                            JSObject *parent, const char *name)
{
    RootObject parentRoot(cx, &parent);

    JS_THREADSAFE_ASSERT(cx->compartment != cx->runtime->atomsCompartment);
    JSAtom *atom;

    CHECK_REQUEST(cx);
    assertSameCompartment(cx, parent);

    if (!name) {
        atom = NULL;
    } else {
        atom = js_Atomize(cx, name, strlen(name));
        if (!atom)
            return NULL;
    }

    return js_NewFunction(cx, NULL, native, nargs, flags, parentRoot, atom,
                          JSFunction::ExtendedFinalizeKind);
}

JS_FRIEND_API(JSFunction *)
js::NewFunctionByIdWithReserved(JSContext *cx, JSNative native, uintN nargs, uintN flags, JSObject *parent,
                                jsid id)
{
    RootObject parentRoot(cx, &parent);

    JS_ASSERT(JSID_IS_STRING(id));
    JS_THREADSAFE_ASSERT(cx->compartment != cx->runtime->atomsCompartment);
    CHECK_REQUEST(cx);
    assertSameCompartment(cx, parent);

    return js_NewFunction(cx, NULL, native, nargs, flags, parentRoot, JSID_TO_ATOM(id),
                          JSFunction::ExtendedFinalizeKind);
}

JS_FRIEND_API(JSObject *)
js::InitClassWithReserved(JSContext *cx, JSObject *obj, JSObject *parent_proto,
                          JSClass *clasp, JSNative constructor, uintN nargs,
                          JSPropertySpec *ps, JSFunctionSpec *fs,
                          JSPropertySpec *static_ps, JSFunctionSpec *static_fs)
{
    CHECK_REQUEST(cx);
    assertSameCompartment(cx, obj, parent_proto);
    RootObject objRoot(cx, &obj);
    return js_InitClass(cx, objRoot, parent_proto, Valueify(clasp), constructor,
                        nargs, ps, fs, static_ps, static_fs, NULL,
                        JSFunction::ExtendedFinalizeKind);
}

JS_FRIEND_API(const Value &)
js::GetFunctionNativeReserved(JSObject *fun, size_t which)
{
    JS_ASSERT(fun->toFunction()->isNative());
    return fun->toFunction()->getExtendedSlot(which);
}

JS_FRIEND_API(void)
js::SetFunctionNativeReserved(JSObject *fun, size_t which, const Value &val)
{
    JS_ASSERT(fun->toFunction()->isNative());
    fun->toFunction()->setExtendedSlot(which, val);
}

JS_FRIEND_API(void)
js::SetReservedSlotWithBarrier(JSObject *obj, size_t slot, const js::Value &value)
{
    obj->setSlot(slot, value);
}

void
js::SetPreserveWrapperCallback(JSRuntime *rt, PreserveWrapperCallback callback)
{
    rt->preserveWrapperCallback = callback;
}

/*
 * The below code is for temporary telemetry use. It can be removed when
 * sufficient data has been harvested.
 */

extern size_t sE4XObjectsCreated;

JS_FRIEND_API(size_t)
JS_GetE4XObjectsCreated(JSContext *)
{
    return sE4XObjectsCreated;
}

extern size_t sSetProtoCalled;

JS_FRIEND_API(size_t)
JS_SetProtoCalled(JSContext *)
{
    return sSetProtoCalled;
}

extern size_t sCustomIteratorCount;

JS_FRIEND_API(size_t)
JS_GetCustomIteratorCount(JSContext *cx)
{
    return sCustomIteratorCount;
}

void
js::TraceWeakMaps(WeakMapTracer *trc)
{
    WeakMapBase::traceAllMappings(trc);
    WatchpointMap::traceAll(trc);
}

JS_FRIEND_API(void)
JS_SetAccumulateTelemetryCallback(JSRuntime *rt, JSAccumulateTelemetryDataCallback callback)
{
    rt->telemetryCallback = callback;
}

JS_FRIEND_API(void)
JS_SetGCFinishedCallback(JSRuntime *rt, JSGCFinishedCallback callback)
{
    rt->gcFinishedCallback = callback;
}

#ifdef DEBUG

struct DumpingChildInfo {
    void *node;
    JSGCTraceKind kind;

    DumpingChildInfo (void *n, JSGCTraceKind k)
        : node(n), kind(k)
    {}
};

typedef HashSet<void *, DefaultHasher<void *>, ContextAllocPolicy> PtrSet;

struct JSDumpHeapTracer : public JSTracer {
    PtrSet visited;
    FILE   *output;
    Vector<DumpingChildInfo, 0, ContextAllocPolicy> nodes;
    char   buffer[200];
    bool   rootTracing;

    JSDumpHeapTracer(JSContext *cx, FILE *fp)
        : visited(cx), output(fp), nodes(cx)
    {}
};

static void
DumpHeapVisitChild(JSTracer *trc, void *thing, JSGCTraceKind kind);

static void
DumpHeapPushIfNew(JSTracer *trc, void *thing, JSGCTraceKind kind)
{
    JS_ASSERT(trc->callback == DumpHeapPushIfNew ||
              trc->callback == DumpHeapVisitChild);
    JSDumpHeapTracer *dtrc = static_cast<JSDumpHeapTracer *>(trc);

    /*
     * If we're tracing roots, print root information.  Do this even if we've
     * already seen thing, for complete root information.
     */
    if (dtrc->rootTracing) {
        fprintf(dtrc->output, "%p %s\n", thing,
                JS_GetTraceEdgeName(dtrc, dtrc->buffer, sizeof(dtrc->buffer)));
    }

    PtrSet::AddPtr ptrEntry = dtrc->visited.lookupForAdd(thing);
    if (ptrEntry || !dtrc->visited.add(ptrEntry, thing))
        return;

    dtrc->nodes.append(DumpingChildInfo(thing, kind));
}

static void
DumpHeapVisitChild(JSTracer *trc, void *thing, JSGCTraceKind kind)
{
    JS_ASSERT(trc->callback == DumpHeapVisitChild);
    JSDumpHeapTracer *dtrc = static_cast<JSDumpHeapTracer *>(trc);
    const char *edgeName = JS_GetTraceEdgeName(dtrc, dtrc->buffer, sizeof(dtrc->buffer));
    fprintf(dtrc->output, "> %p %s\n", (void *)thing, edgeName);
    DumpHeapPushIfNew(dtrc, thing, kind);
}

void
js::DumpHeapComplete(JSContext *cx, FILE *fp)
{
    JSDumpHeapTracer dtrc(cx, fp);
    JS_TracerInit(&dtrc, cx, DumpHeapPushIfNew);
    if (!dtrc.visited.init(10000))
        return;

    /* Store and log the root information. */
    dtrc.rootTracing = true;
    TraceRuntime(&dtrc);
    fprintf(dtrc.output, "==========\n");

    /* Log the graph. */
    dtrc.rootTracing = false;
    dtrc.callback = DumpHeapVisitChild;

    while (!dtrc.nodes.empty()) {
        DumpingChildInfo dci = dtrc.nodes.popCopy();
        JS_PrintTraceThingInfo(dtrc.buffer, sizeof(dtrc.buffer),
                               &dtrc, dci.node, dci.kind, JS_TRUE);
        fprintf(fp, "%p %s\n", dci.node, dtrc.buffer);
        JS_TraceChildren(&dtrc, dci.node, dci.kind);
    }

    dtrc.visited.finish();
}

#endif

namespace js {

JS_FRIEND_API(bool)
IsIncrementalBarrierNeeded(JSRuntime *rt)
{
    return !!rt->gcIncrementalTracer && !rt->gcRunning;
}

JS_FRIEND_API(bool)
IsIncrementalBarrierNeeded(JSContext *cx)
{
    return IsIncrementalBarrierNeeded(cx->runtime);
}

extern JS_FRIEND_API(void)
IncrementalReferenceBarrier(void *ptr)
{
    if (!ptr)
        return;
    JS_ASSERT(!static_cast<gc::Cell *>(ptr)->compartment()->rt->gcRunning);
    uint32_t kind = gc::GetGCThingTraceKind(ptr);
    if (kind == JSTRACE_OBJECT)
        JSObject::writeBarrierPre((JSObject *) ptr);
    else if (kind == JSTRACE_STRING)
        JSString::writeBarrierPre((JSString *) ptr);
    else
        JS_NOT_REACHED("invalid trace kind");
}

extern JS_FRIEND_API(void)
IncrementalValueBarrier(const Value &v)
{
    HeapValue::writeBarrierPre(v);
}

/* static */ void
AutoLockGC::LockGC(JSRuntime *rt)
{
    JS_ASSERT(rt);
    JS_LOCK_GC(rt);
}

/* static */ void
AutoLockGC::UnlockGC(JSRuntime *rt)
{
    JS_ASSERT(rt);
    JS_UNLOCK_GC(rt);
}

void
AutoLockGC::lock(JSRuntime *rt)
{
    JS_ASSERT(rt);
    JS_ASSERT(!runtime);
    runtime = rt;
    JS_LOCK_GC(rt);
}

JS_FRIEND_API(const JSStructuredCloneCallbacks *)
GetContextStructuredCloneCallbacks(JSContext *cx)
{
    return cx->runtime->structuredCloneCallbacks;
}

JS_FRIEND_API(JSVersion)
VersionSetXML(JSVersion version, bool enable)
{
    return enable ? JSVersion(uint32_t(version) | VersionFlags::HAS_XML)
                  : JSVersion(uint32_t(version) & ~VersionFlags::HAS_XML);
}

JS_FRIEND_API(bool)
CanCallContextDebugHandler(JSContext *cx)
{
    return cx->debugHooks && cx->debugHooks->debuggerHandler;
}

JS_FRIEND_API(JSTrapStatus)
CallContextDebugHandler(JSContext *cx, JSScript *script, jsbytecode *bc, Value *rval)
{
    if (!CanCallContextDebugHandler(cx))
        return JSTRAP_RETURN;

    return cx->debugHooks->debuggerHandler(cx, script, bc, rval,
                                           cx->debugHooks->debuggerHandlerData);
}

#ifdef JS_THREADSAFE
void *
GetOwnerThread(const JSContext *cx)
{
    return cx->runtime->ownerThread();
}

JS_FRIEND_API(unsigned)
GetContextOutstandingRequests(const JSContext *cx)
{
    return cx->outstandingRequests;
}

JS_FRIEND_API(PRLock *)
GetRuntimeGCLock(const JSRuntime *rt)
{
    return rt->gcLock;
}

AutoSkipConservativeScan::AutoSkipConservativeScan(JSContext *cx
                                                   MOZ_GUARD_OBJECT_NOTIFIER_PARAM_IN_IMPL)
  : context(cx)
{
    MOZ_GUARD_OBJECT_NOTIFIER_INIT;

    JSRuntime *rt = context->runtime;
    JS_ASSERT(rt->requestDepth >= 1);
    JS_ASSERT(!rt->conservativeGC.requestThreshold);
    if (rt->requestDepth == 1)
        rt->conservativeGC.requestThreshold = 1;
}

AutoSkipConservativeScan::~AutoSkipConservativeScan()
{
    JSRuntime *rt = context->runtime;
    if (rt->requestDepth == 1)
        rt->conservativeGC.requestThreshold = 0;
}
#endif

JS_FRIEND_API(JSCompartment *)
GetContextCompartment(const JSContext *cx)
{
    return cx->compartment;
}

JS_FRIEND_API(bool)
HasUnrootedGlobal(const JSContext *cx)
{
    return cx->hasRunOption(JSOPTION_UNROOTED_GLOBAL);
}

JS_FRIEND_API(void)
SetActivityCallback(JSRuntime *rt, ActivityCallback cb, void *arg)
{
    rt->activityCallback = cb;
    rt->activityCallbackArg = arg;
}

JS_FRIEND_API(bool)
IsContextRunningJS(JSContext *cx)
{
    return !cx->stack.empty();
}

JS_FRIEND_API(void)
TriggerOperationCallback(JSRuntime *rt)
{
    rt->triggerOperationCallback();
}

JS_FRIEND_API(const CompartmentVector&)
GetRuntimeCompartments(JSRuntime *rt)
{
    return rt->compartments;
}

JS_FRIEND_API(uintptr_t)
GetContextStackLimit(const JSContext *cx)
{
    return cx->stackLimit;
}

JS_FRIEND_API(size_t)
SizeOfJSContext()
{
    return sizeof(JSContext);
}

} // namespace js
