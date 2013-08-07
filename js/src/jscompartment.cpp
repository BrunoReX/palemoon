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
 * The Original Code is Mozilla SpiderMonkey JavaScript 1.9 code, released
 * May 28, 2008.
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
#include "jsgc.h"
#include "jsgcmark.h"
#include "jsiter.h"
#include "jsmath.h"
#include "jsproxy.h"
#include "jsscope.h"
#include "jstracer.h"
#include "jswatchpoint.h"
#include "jswrapper.h"
#include "assembler/wtf/Platform.h"
#include "assembler/jit/ExecutableAllocator.h"
#include "yarr/BumpPointerAllocator.h"
#include "methodjit/MethodJIT.h"
#include "methodjit/PolyIC.h"
#include "methodjit/MonoIC.h"
#include "vm/Debugger.h"

#include "jsgcinlines.h"
#include "jsscopeinlines.h"

#if ENABLE_YARR_JIT
#include "assembler/jit/ExecutableAllocator.h"
#endif

using namespace js;
using namespace js::gc;

JSCompartment::JSCompartment(JSRuntime *rt)
  : rt(rt),
    principals(NULL),
    gcBytes(0),
    gcTriggerBytes(0),
    gcLastBytes(0),
    hold(false),
#ifdef JS_TRACER
    traceMonitor_(NULL),
#endif
    data(NULL),
    active(false),
    hasDebugModeCodeToDrop(false),
#ifdef JS_METHODJIT
    jaegerCompartment_(NULL),
#endif
#if ENABLE_YARR_JIT
    regExpAllocator(NULL),
#endif
    propertyTree(thisForCtor()),
    emptyArgumentsShape(NULL),
    emptyBlockShape(NULL),
    emptyCallShape(NULL),
    emptyDeclEnvShape(NULL),
    emptyEnumeratorShape(NULL),
    emptyWithShape(NULL),
    initialRegExpShape(NULL),
    initialStringShape(NULL),
    debugModeBits(rt->debugMode ? DebugFromC : 0),
    mathCache(NULL),
    breakpointSites(rt),
    watchpointMap(NULL)
{
    PodArrayZero(evalCache);
}

JSCompartment::~JSCompartment()
{
#if ENABLE_YARR_JIT
    Foreground::delete_(regExpAllocator);
#endif

#ifdef JS_METHODJIT
    Foreground::delete_(jaegerCompartment_);
#endif

#ifdef JS_TRACER
    Foreground::delete_(traceMonitor_);
#endif

    Foreground::delete_(mathCache);
    Foreground::delete_(watchpointMap);

#ifdef DEBUG
    for (size_t i = 0; i != JS_ARRAY_LENGTH(evalCache); ++i)
        JS_ASSERT(!evalCache[i]);
#endif
}

bool
JSCompartment::init(JSContext *cx)
{
    activeAnalysis = activeInference = false;
    types.init(cx);

    /* Duplicated from jscntxt.cpp. :XXX: bug 675150 fix hack. */
    static const size_t ARENA_HEADER_SIZE_HACK = 40;

    JS_InitArenaPool(&pool, "analysis", 4096 - ARENA_HEADER_SIZE_HACK, 8);

    if (!crossCompartmentWrappers.init())
        return false;

    if (!scriptFilenameTable.init())
        return false;

    regExpAllocator = rt->new_<WTF::BumpPointerAllocator>();
    if (!regExpAllocator)
        return false;

    if (!backEdgeTable.init())
        return false;

    return debuggees.init() && breakpointSites.init();
}

#ifdef JS_METHODJIT
bool
JSCompartment::ensureJaegerCompartmentExists(JSContext *cx)
{
    if (jaegerCompartment_)
        return true;

    mjit::JaegerCompartment *jc = cx->new_<mjit::JaegerCompartment>();
    if (!jc)
        return false;
    if (!jc->Initialize()) {
        cx->delete_(jc);
        return false;
    }
    jaegerCompartment_ = jc;
    return true;
}

void
JSCompartment::getMjitCodeStats(size_t& method, size_t& regexp, size_t& unused) const
{
    if (jaegerCompartment_) {
        jaegerCompartment_->execAlloc()->getCodeStats(method, regexp, unused);
    } else {
        method = 0;
        regexp = 0;
        unused = 0;
    }
}
#endif

static bool
IsCrossCompartmentWrapper(JSObject *wrapper)
{
    return wrapper->isWrapper() &&
           !!(JSWrapper::wrapperHandler(wrapper)->flags() & JSWrapper::CROSS_COMPARTMENT);
}

bool
JSCompartment::wrap(JSContext *cx, Value *vp)
{
    JS_ASSERT(cx->compartment == this);

    uintN flags = 0;

    JS_CHECK_RECURSION(cx, return false);

    /* Only GC things have to be wrapped or copied. */
    if (!vp->isMarkable())
        return true;

    if (vp->isString()) {
        JSString *str = vp->toString();

        /* If the string is already in this compartment, we are done. */
        if (str->compartment() == this)
            return true;

        /* If the string is an atom, we don't have to copy. */
        if (str->isAtom()) {
            JS_ASSERT(str->compartment() == cx->runtime->atomsCompartment);
            return true;
        }
    }

    /*
     * Wrappers should really be parented to the wrapped parent of the wrapped
     * object, but in that case a wrapped global object would have a NULL
     * parent without being a proper global object (JSCLASS_IS_GLOBAL). Instead,
     * we parent all wrappers to the global object in their home compartment.
     * This loses us some transparency, and is generally very cheesy.
     */
    JSObject *global;
    if (cx->hasfp()) {
        global = cx->fp()->scopeChain().getGlobal();
    } else {
        global = cx->globalObject;
        if (!NULLABLE_OBJ_TO_INNER_OBJECT(cx, global))
            return false;
    }

    /* Unwrap incoming objects. */
    if (vp->isObject()) {
        JSObject *obj = &vp->toObject();

        /* If the object is already in this compartment, we are done. */
        if (obj->compartment() == this)
            return true;

        /* Translate StopIteration singleton. */
        if (obj->isStopIteration())
            return js_FindClassObject(cx, NULL, JSProto_StopIteration, vp);

        /* Don't unwrap an outer window proxy. */
        if (!obj->getClass()->ext.innerObject) {
            obj = vp->toObject().unwrap(&flags);
            vp->setObject(*obj);
            if (obj->getCompartment() == this)
                return true;

            if (cx->runtime->preWrapObjectCallback) {
                obj = cx->runtime->preWrapObjectCallback(cx, global, obj, flags);
                if (!obj)
                    return false;
            }

            vp->setObject(*obj);
            if (obj->getCompartment() == this)
                return true;
        } else {
            if (cx->runtime->preWrapObjectCallback) {
                obj = cx->runtime->preWrapObjectCallback(cx, global, obj, flags);
                if (!obj)
                    return false;
            }

            JS_ASSERT(!obj->isWrapper() || obj->getClass()->ext.innerObject);
            vp->setObject(*obj);
        }

#ifdef DEBUG
        {
            JSObject *outer = obj;
            OBJ_TO_OUTER_OBJECT(cx, outer);
            JS_ASSERT(outer && outer == obj);
        }
#endif
    }

    /* If we already have a wrapper for this value, use it. */
    if (WrapperMap::Ptr p = crossCompartmentWrappers.lookup(*vp)) {
        *vp = p->value;
        if (vp->isObject()) {
            JSObject *obj = &vp->toObject();
            JS_ASSERT(IsCrossCompartmentWrapper(obj));
            if (global->getJSClass() != &js_dummy_class && obj->getParent() != global) {
                do {
                    obj->setParent(global);
                    obj = obj->getProto();
                } while (obj && IsCrossCompartmentWrapper(obj));
            }
        }
        return true;
    }

    if (vp->isString()) {
        Value orig = *vp;
        JSString *str = vp->toString();
        const jschar *chars = str->getChars(cx);
        if (!chars)
            return false;
        JSString *wrapped = js_NewStringCopyN(cx, chars, str->length());
        if (!wrapped)
            return false;
        vp->setString(wrapped);
        return crossCompartmentWrappers.put(orig, *vp);
    }

    JSObject *obj = &vp->toObject();

    /*
     * Recurse to wrap the prototype. Long prototype chains will run out of
     * stack, causing an error in CHECK_RECURSE.
     *
     * Wrapping the proto before creating the new wrapper and adding it to the
     * cache helps avoid leaving a bad entry in the cache on OOM. But note that
     * if we wrapped both proto and parent, we would get infinite recursion
     * here (since Object.prototype->parent->proto leads to Object.prototype
     * itself).
     */
    JSObject *proto = obj->getProto();
    if (!wrap(cx, &proto))
        return false;

    /*
     * We hand in the original wrapped object into the wrap hook to allow
     * the wrap hook to reason over what wrappers are currently applied
     * to the object.
     */
    JSObject *wrapper = cx->runtime->wrapObjectCallback(cx, obj, proto, global, flags);
    if (!wrapper)
        return false;

    vp->setObject(*wrapper);

    if (wrapper->getProto() != proto && !SetProto(cx, wrapper, proto, false))
        return false;

    if (!crossCompartmentWrappers.put(wrapper->getProxyPrivate(), *vp))
        return false;

    wrapper->setParent(global);
    return true;
}

bool
JSCompartment::wrap(JSContext *cx, JSString **strp)
{
    AutoValueRooter tvr(cx, StringValue(*strp));
    if (!wrap(cx, tvr.addr()))
        return false;
    *strp = tvr.value().toString();
    return true;
}

bool
JSCompartment::wrap(JSContext *cx, JSObject **objp)
{
    if (!*objp)
        return true;
    AutoValueRooter tvr(cx, ObjectValue(**objp));
    if (!wrap(cx, tvr.addr()))
        return false;
    *objp = &tvr.value().toObject();
    return true;
}

bool
JSCompartment::wrapId(JSContext *cx, jsid *idp)
{
    if (JSID_IS_INT(*idp))
        return true;
    AutoValueRooter tvr(cx, IdToValue(*idp));
    if (!wrap(cx, tvr.addr()))
        return false;
    return ValueToId(cx, tvr.value(), idp);
}

bool
JSCompartment::wrap(JSContext *cx, PropertyOp *propp)
{
    Value v = CastAsObjectJsval(*propp);
    if (!wrap(cx, &v))
        return false;
    *propp = CastAsPropertyOp(v.toObjectOrNull());
    return true;
}

bool
JSCompartment::wrap(JSContext *cx, StrictPropertyOp *propp)
{
    Value v = CastAsObjectJsval(*propp);
    if (!wrap(cx, &v))
        return false;
    *propp = CastAsStrictPropertyOp(v.toObjectOrNull());
    return true;
}

bool
JSCompartment::wrap(JSContext *cx, PropertyDescriptor *desc)
{
    return wrap(cx, &desc->obj) &&
           (!(desc->attrs & JSPROP_GETTER) || wrap(cx, &desc->getter)) &&
           (!(desc->attrs & JSPROP_SETTER) || wrap(cx, &desc->setter)) &&
           wrap(cx, &desc->value);
}

bool
JSCompartment::wrap(JSContext *cx, AutoIdVector &props)
{
    jsid *vector = props.begin();
    jsint length = props.length();
    for (size_t n = 0; n < size_t(length); ++n) {
        if (!wrapId(cx, &vector[n]))
            return false;
    }
    return true;
}

#if defined JS_METHODJIT && defined JS_MONOIC
/*
 * Check if the pool containing the code for jit should be destroyed, per the
 * heuristics in JSCompartment::sweep.
 */
static inline bool
ScriptPoolDestroyed(JSContext *cx, mjit::JITScript *jit,
                    uint32 releaseInterval, uint32 &counter)
{
    JSC::ExecutablePool *pool = jit->code.m_executablePool;
    if (pool->m_gcNumber != cx->runtime->gcNumber) {
        /*
         * The m_destroy flag may have been set in a previous GC for a pool which had
         * references we did not remove (e.g. from the compartment's ExecutableAllocator)
         * and is still around. Forget we tried to destroy it in such cases.
         */
        pool->m_destroy = false;
        pool->m_gcNumber = cx->runtime->gcNumber;
        if (--counter == 0) {
            pool->m_destroy = true;
            counter = releaseInterval;
        }
    }
    return pool->m_destroy;
}

static inline void
ScriptTryDestroyCode(JSContext *cx, JSScript *script, bool normal,
                     uint32 releaseInterval, uint32 &counter)
{
    mjit::JITScript *jit = normal ? script->jitNormal : script->jitCtor;
    if (jit && ScriptPoolDestroyed(cx, jit, releaseInterval, counter))
        mjit::ReleaseScriptCode(cx, script, !normal);
}
#endif // JS_METHODJIT && JS_MONOIC

/*
 * This method marks pointers that cross compartment boundaries. It should be
 * called only for per-compartment GCs, since full GCs naturally follow pointers
 * across compartments.
 */
void
JSCompartment::markCrossCompartmentWrappers(JSTracer *trc)
{
    JS_ASSERT(trc->context->runtime->gcCurrentCompartment);

    for (WrapperMap::Enum e(crossCompartmentWrappers); !e.empty(); e.popFront())
        MarkValue(trc, e.front().key, "cross-compartment wrapper");
}

void
JSCompartment::markTypes(JSTracer *trc)
{
    /*
     * Mark all scripts, type objects and singleton JS objects in the
     * compartment. These can be referred to directly by type sets, which we
     * cannot modify while code which depends on these type sets is active.
     */
    JS_ASSERT(activeAnalysis);

    for (CellIterUnderGC i(this, FINALIZE_SCRIPT); !i.done(); i.next()) {
        JSScript *script = i.get<JSScript>();
        MarkScript(trc, script, "mark_types_script");
    }

    for (size_t thingKind = FINALIZE_OBJECT0;
         thingKind <= FINALIZE_FUNCTION_AND_OBJECT_LAST;
         thingKind++) {
        for (CellIterUnderGC i(this, AllocKind(thingKind)); !i.done(); i.next()) {
            JSObject *object = i.get<JSObject>();
            if (!object->isNewborn() && object->hasSingletonType())
                MarkObject(trc, *object, "mark_types_singleton");
        }
    }

    for (CellIterUnderGC i(this, FINALIZE_TYPE_OBJECT); !i.done(); i.next())
        MarkTypeObject(trc, i.get<types::TypeObject>(), "mark_types_scan");
}

void
JSCompartment::sweep(JSContext *cx, uint32 releaseInterval)
{
    /* Remove dead wrappers from the table. */
    for (WrapperMap::Enum e(crossCompartmentWrappers); !e.empty(); e.popFront()) {
        JS_ASSERT_IF(IsAboutToBeFinalized(cx, e.front().key.toGCThing()) &&
                     !IsAboutToBeFinalized(cx, e.front().value.toGCThing()),
                     e.front().key.isString());
        if (IsAboutToBeFinalized(cx, e.front().key.toGCThing()) ||
            IsAboutToBeFinalized(cx, e.front().value.toGCThing())) {
            e.removeFront();
        }
    }

    /* Remove dead empty shapes. */
    if (emptyArgumentsShape && IsAboutToBeFinalized(cx, emptyArgumentsShape))
        emptyArgumentsShape = NULL;
    if (emptyBlockShape && IsAboutToBeFinalized(cx, emptyBlockShape))
        emptyBlockShape = NULL;
    if (emptyCallShape && IsAboutToBeFinalized(cx, emptyCallShape))
        emptyCallShape = NULL;
    if (emptyDeclEnvShape && IsAboutToBeFinalized(cx, emptyDeclEnvShape))
        emptyDeclEnvShape = NULL;
    if (emptyEnumeratorShape && IsAboutToBeFinalized(cx, emptyEnumeratorShape))
        emptyEnumeratorShape = NULL;
    if (emptyWithShape && IsAboutToBeFinalized(cx, emptyWithShape))
        emptyWithShape = NULL;

    if (initialRegExpShape && IsAboutToBeFinalized(cx, initialRegExpShape))
        initialRegExpShape = NULL;
    if (initialStringShape && IsAboutToBeFinalized(cx, initialStringShape))
        initialStringShape = NULL;

    sweepBreakpoints(cx);

#ifdef JS_TRACER
    if (hasTraceMonitor())
        traceMonitor()->sweep(cx);
#endif

#ifdef JS_METHODJIT
    /*
     * Purge PICs in the compartment, along with native call stubs for
     * compartments which do not have such stubs on the stack. PICs can
     * reference shapes and type data, and native call stubs are disassociated
     * from the PIC or MIC they were generated for.
     */
    bool canPurgeNativeCalls = true;
    VMFrame *f = hasJaegerCompartment() ? jaegerCompartment()->activeFrame() : NULL;
    for (; f; f = f->previous) {
        if (f->stubRejoin)
            canPurgeNativeCalls = false;
    }
    for (CellIterUnderGC i(this, FINALIZE_SCRIPT); !i.done(); i.next()) {
        JSScript *script = i.get<JSScript>();
        if (script->hasJITCode()) {
#ifdef JS_POLYIC
            mjit::ic::PurgePICs(cx, script);
#endif
            if (canPurgeNativeCalls) {
                if (script->jitNormal)
                    script->jitNormal->purgeNativeCallStubs();
                if (script->jitCtor)
                    script->jitCtor->purgeNativeCallStubs();
            }
        }
    }
#endif

    bool discardScripts = !active && (releaseInterval != 0 || hasDebugModeCodeToDrop);

#if defined JS_METHODJIT && defined JS_MONOIC

    /*
     * The release interval is the frequency with which we should try to destroy
     * executable pools by releasing all JIT code in them, zero to never destroy pools.
     * Initialize counter so that the first pool will be destroyed, and eventually drive
     * the amount of JIT code in never-used compartments to zero. Don't discard anything
     * for compartments which currently have active stack frames.
     */
    uint32 counter = 1;
    if (discardScripts)
        hasDebugModeCodeToDrop = false;

    for (CellIterUnderGC i(this, FINALIZE_SCRIPT); !i.done(); i.next()) {
        JSScript *script = i.get<JSScript>();
        if (script->hasJITCode()) {
            mjit::ic::SweepCallICs(cx, script, discardScripts);
            if (discardScripts) {
                ScriptTryDestroyCode(cx, script, true, releaseInterval, counter);
                ScriptTryDestroyCode(cx, script, false, releaseInterval, counter);
            }
        }
    }

#endif

#ifdef JS_METHODJIT
    if (types.inferenceEnabled)
        mjit::ClearAllFrames(this);
#endif

    if (activeAnalysis) {
        /*
         * Analysis information is in use, so don't clear the analysis pool.
         * jitcode still needs to be released, if this is a shape-regenerating
         * GC then shape numbers baked into the code may change.
         */
#ifdef JS_METHODJIT
        if (types.inferenceEnabled) {
            for (CellIterUnderGC i(this, FINALIZE_SCRIPT); !i.done(); i.next()) {
                JSScript *script = i.get<JSScript>();
                mjit::ReleaseScriptCode(cx, script);
            }
        }
#endif
    } else {
        /*
         * Clear the analysis pool, but don't release its data yet. While
         * sweeping types any live data will be allocated into the pool.
         */
        JSArenaPool oldPool;
        MoveArenaPool(&pool, &oldPool);

        /*
         * Sweep analysis information and everything depending on it from the
         * compartment, including all remaining mjit code if inference is
         * enabled in the compartment.
         */
        if (types.inferenceEnabled) {
            for (CellIterUnderGC i(this, FINALIZE_SCRIPT); !i.done(); i.next()) {
                JSScript *script = i.get<JSScript>();
                if (script->types) {
                    types::TypeScript::Sweep(cx, script);

                    /*
                     * On each 1/8 lifetime, release observed types for all scripts.
                     * This is always safe to do when there are no frames for the
                     * compartment on the stack.
                     */
                    if (discardScripts) {
                        script->types->destroy();
                        script->types = NULL;
                        script->typesPurged = true;
                    }
                }
            }
        }

        types.sweep(cx);

        for (CellIterUnderGC i(this, FINALIZE_SCRIPT); !i.done(); i.next()) {
            JSScript *script = i.get<JSScript>();
            script->clearAnalysis();
        }

        /* Reset the analysis pool, releasing all analysis and intermediate type data. */
        JS_FinishArenaPool(&oldPool);
    }

    active = false;
}

void
JSCompartment::purge(JSContext *cx)
{
    arenas.purge();
    dtoaCache.purge();

    /*
     * Clear the hash and reset all evalHashLink to null before the GC. This
     * way MarkChildren(trc, JSScript *) can assume that JSScript::u.object is
     * not null when we have script owned by an object and not from the eval
     * cache.
     */
    for (size_t i = 0; i != JS_ARRAY_LENGTH(evalCache); ++i) {
        for (JSScript **listHeadp = &evalCache[i]; *listHeadp; ) {
            JSScript *script = *listHeadp;
            JS_ASSERT(GetGCThingTraceKind(script) == JSTRACE_SCRIPT);
            *listHeadp = NULL;
            listHeadp = &script->u.evalHashLink;
        }
    }

    nativeIterCache.purge();
    toSourceCache.destroyIfConstructed();

#ifdef JS_TRACER
    /*
     * If we are about to regenerate shapes, we have to flush the JIT cache,
     * which will eventually abort any current recording.
     */
    if (cx->runtime->gcRegenShapes)
        if (hasTraceMonitor())
            traceMonitor()->needFlush = JS_TRUE;
#endif

#if defined JS_METHODJIT && defined JS_MONOIC
    /*
     * MICs do not refer to data which can be GC'ed and do not generate stubs
     * which might need to be discarded, but are sensitive to shape regeneration.
     */
    if (cx->runtime->gcRegenShapes) {
        for (CellIterUnderGC i(this, FINALIZE_SCRIPT); !i.done(); i.next()) {
            JSScript *script = i.get<JSScript>();
            if (script->hasJITCode())
                mjit::ic::PurgeMICs(cx, script);
        }
    }
#endif
}

MathCache *
JSCompartment::allocMathCache(JSContext *cx)
{
    JS_ASSERT(!mathCache);
    mathCache = cx->new_<MathCache>();
    if (!mathCache)
        js_ReportOutOfMemory(cx);
    return mathCache;
}

#ifdef JS_TRACER
TraceMonitor *
JSCompartment::allocAndInitTraceMonitor(JSContext *cx)
{
    JS_ASSERT(!traceMonitor_);
    traceMonitor_ = cx->new_<TraceMonitor>();
    if (!traceMonitor_)
        return NULL;
    if (!traceMonitor_->init(cx->runtime)) {
        Foreground::delete_(traceMonitor_);
        return NULL;
    }
    return traceMonitor_;
}
#endif

size_t
JSCompartment::backEdgeCount(jsbytecode *pc) const
{
    if (BackEdgeMap::Ptr p = backEdgeTable.lookup(pc))
        return p->value;

    return 0;
}

size_t
JSCompartment::incBackEdgeCount(jsbytecode *pc)
{
    if (BackEdgeMap::Ptr p = backEdgeTable.lookupWithDefault(pc, 0))
        return ++p->value;
    return 1;  /* oom not reported by backEdgeTable, so ignore. */
}

bool
JSCompartment::hasScriptsOnStack(JSContext *cx)
{
    for (AllFramesIter i(cx->stack.space()); !i.done(); ++i) {
        JSScript *script = i.fp()->maybeScript();
        if (script && script->compartment() == this)
            return true;
    }
    return false;
}

bool
JSCompartment::setDebugModeFromC(JSContext *cx, bool b)
{
    bool enabledBefore = debugMode();
    bool enabledAfter = (debugModeBits & ~uintN(DebugFromC)) || b;

    // Debug mode can be enabled only when no scripts from the target
    // compartment are on the stack. It would even be incorrect to discard just
    // the non-live scripts' JITScripts because they might share ICs with live
    // scripts (bug 632343).
    //
    // We do allow disabling debug mode while scripts are on the stack.  In
    // that case the debug-mode code for those scripts remains, so subsequently
    // hooks may be called erroneously, even though debug mode is supposedly
    // off, and we have to live with it.
    //
    bool onStack = false;
    if (enabledBefore != enabledAfter) {
        onStack = hasScriptsOnStack(cx);
        if (b && onStack) {
            JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_DEBUG_NOT_IDLE);
            return false;
        }
    }

    debugModeBits = (debugModeBits & ~uintN(DebugFromC)) | (b ? DebugFromC : 0);
    JS_ASSERT(debugMode() == enabledAfter);
    if (enabledBefore != enabledAfter)
        updateForDebugMode(cx);
    return true;
}

void
JSCompartment::updateForDebugMode(JSContext *cx)
{
    for (ThreadContextRange r(cx); !r.empty(); r.popFront()) {
        JSContext *cx = r.front();
        if (cx->compartment == this) 
            cx->updateJITEnabled();
    }

#ifdef JS_METHODJIT
    bool enabled = debugMode();

    if (enabled) {
        JS_ASSERT(!hasScriptsOnStack(cx));
    } else if (hasScriptsOnStack(cx)) {
        hasDebugModeCodeToDrop = true;
        return;
    }

    /*
     * Discard JIT code for any scripts that change debugMode. This assumes
     * that 'comp' is in the same thread as 'cx'.
     */
    for (gc::CellIter i(cx, this, gc::FINALIZE_SCRIPT); !i.done(); i.next()) {
        JSScript *script = i.get<JSScript>();
        if (script->debugMode != enabled) {
            mjit::ReleaseScriptCode(cx, script);
            script->debugMode = enabled;
        }
    }
    hasDebugModeCodeToDrop = false;
#endif
}

bool
JSCompartment::addDebuggee(JSContext *cx, js::GlobalObject *global)
{
    bool wasEnabled = debugMode();
    if (!debuggees.put(global)) {
        js_ReportOutOfMemory(cx);
        return false;
    }
    debugModeBits |= DebugFromJS;
    if (!wasEnabled)
        updateForDebugMode(cx);
    return true;
}

void
JSCompartment::removeDebuggee(JSContext *cx,
                              js::GlobalObject *global,
                              js::GlobalObjectSet::Enum *debuggeesEnum)
{
    bool wasEnabled = debugMode();
    JS_ASSERT(debuggees.has(global));
    if (debuggeesEnum)
        debuggeesEnum->removeFront();
    else
        debuggees.remove(global);

    if (debuggees.empty()) {
        debugModeBits &= ~DebugFromJS;
        if (wasEnabled && !debugMode())
            updateForDebugMode(cx);
    }
}

BreakpointSite *
JSCompartment::getBreakpointSite(jsbytecode *pc)
{
    BreakpointSiteMap::Ptr p = breakpointSites.lookup(pc);
    return p ? p->value : NULL;
}

BreakpointSite *
JSCompartment::getOrCreateBreakpointSite(JSContext *cx, JSScript *script, jsbytecode *pc, JSObject *scriptObject)
{
    JS_ASSERT(script->code <= pc);
    JS_ASSERT(pc < script->code + script->length);
    JS_ASSERT_IF(scriptObject, scriptObject->isScript() || scriptObject->isFunction());
    JS_ASSERT_IF(scriptObject && scriptObject->isFunction(),
                 scriptObject->getFunctionPrivate()->script() == script);
    JS_ASSERT_IF(scriptObject && scriptObject->isScript(), scriptObject->getScript() == script);

    BreakpointSiteMap::AddPtr p = breakpointSites.lookupForAdd(pc);
    if (!p) {
        BreakpointSite *site = cx->runtime->new_<BreakpointSite>(script, pc);
        if (!site || !breakpointSites.add(p, pc, site)) {
            js_ReportOutOfMemory(cx);
            return NULL;
        }
    }

    BreakpointSite *site = p->value;
    if (site->scriptObject)
        JS_ASSERT_IF(scriptObject, site->scriptObject == scriptObject);
    else
        site->scriptObject = scriptObject;

    return site;
}

void
JSCompartment::clearBreakpointsIn(JSContext *cx, js::Debugger *dbg, JSScript *script,
                                  JSObject *handler)
{
    JS_ASSERT_IF(script, script->compartment() == this);

    for (BreakpointSiteMap::Enum e(breakpointSites); !e.empty(); e.popFront()) {
        BreakpointSite *site = e.front().value;
        if (!script || site->script == script) {
            Breakpoint *nextbp;
            for (Breakpoint *bp = site->firstBreakpoint(); bp; bp = nextbp) {
                nextbp = bp->nextInSite();
                if ((!dbg || bp->debugger == dbg) && (!handler || bp->getHandler() == handler))
                    bp->destroy(cx, &e);
            }
        }
    }
}

void
JSCompartment::clearTraps(JSContext *cx, JSScript *script)
{
    for (BreakpointSiteMap::Enum e(breakpointSites); !e.empty(); e.popFront()) {
        BreakpointSite *site = e.front().value;
        if (!script || site->script == script)
            site->clearTrap(cx, &e);
    }
}

bool
JSCompartment::markTrapClosuresIteratively(JSTracer *trc)
{
    bool markedAny = false;
    JSContext *cx = trc->context;
    for (BreakpointSiteMap::Range r = breakpointSites.all(); !r.empty(); r.popFront()) {
        BreakpointSite *site = r.front().value;

        // Mark jsdbgapi state if any. But if we know the scriptObject, put off
        // marking trap state until we know the scriptObject is live.
        if (site->trapHandler &&
            (!site->scriptObject || !IsAboutToBeFinalized(cx, site->scriptObject)))
        {
            if (site->trapClosure.isMarkable() &&
                IsAboutToBeFinalized(cx, site->trapClosure.toGCThing()))
            {
                markedAny = true;
            }
            MarkValue(trc, site->trapClosure, "trap closure");
        }
    }
    return markedAny;
}

void
JSCompartment::sweepBreakpoints(JSContext *cx)
{
    for (BreakpointSiteMap::Enum e(breakpointSites); !e.empty(); e.popFront()) {
        BreakpointSite *site = e.front().value;
        if (site->scriptObject) {
            // clearTrap and nextbp are necessary here to avoid possibly
            // reading *site or *bp after destroying it.
            bool scriptGone = IsAboutToBeFinalized(cx, site->scriptObject);
            bool clearTrap = scriptGone && site->hasTrap();

            Breakpoint *nextbp;
            for (Breakpoint *bp = site->firstBreakpoint(); bp; bp = nextbp) {
                nextbp = bp->nextInSite();
                if (scriptGone || IsAboutToBeFinalized(cx, bp->debugger->toJSObject()))
                    bp->destroy(cx, &e);
            }

            if (clearTrap)
                site->clearTrap(cx, &e);
        }
    }
}
