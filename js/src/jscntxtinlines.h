/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=78:
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
 * Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Jeff Walden <jwalden+code@mit.edu> (original author)
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

#ifndef jscntxtinlines_h___
#define jscntxtinlines_h___

#include "jscntxt.h"
#include "jscompartment.h"
#include "jsfriendapi.h"
#include "jsinterp.h"
#include "jsxml.h"
#include "jsgc.h"

#include "frontend/ParseMaps.h"
#include "vm/RegExpObject.h"

namespace js {

struct PreserveRegsGuard
{
    PreserveRegsGuard(JSContext *cx, FrameRegs &regs)
      : prevContextRegs(cx->maybeRegs()), cx(cx), regs_(regs) {
        cx->stack.repointRegs(&regs_);
    }
    ~PreserveRegsGuard() {
        JS_ASSERT(cx->maybeRegs() == &regs_);
        *prevContextRegs = regs_;
        cx->stack.repointRegs(prevContextRegs);
    }

    FrameRegs *prevContextRegs;

  private:
    JSContext *cx;
    FrameRegs &regs_;
};

static inline GlobalObject *
GetGlobalForScopeChain(JSContext *cx)
{
    if (cx->hasfp())
        return &cx->fp()->scopeChain().global();

    JSObject *scope = JS_ObjectToInnerObject(cx, cx->globalObject);
    if (!scope)
        return NULL;
    return &scope->asGlobal();
}

inline GSNCache *
GetGSNCache(JSContext *cx)
{
    return &cx->runtime->gsnCache;
}

class AutoNamespaceArray : protected AutoGCRooter {
  public:
    AutoNamespaceArray(JSContext *cx) : AutoGCRooter(cx, NAMESPACES) {
        array.init();
    }

    ~AutoNamespaceArray() {
        array.finish(context);
    }

    uint32_t length() const { return array.length; }

  public:
    friend void AutoGCRooter::trace(JSTracer *trc);

    JSXMLArray<JSObject> array;
};

template <typename T>
class AutoPtr
{
    JSContext *cx;
    T *value;

    AutoPtr(const AutoPtr &other) MOZ_DELETE;

  public:
    explicit AutoPtr(JSContext *cx) : cx(cx), value(NULL) {}
    ~AutoPtr() {
        cx->delete_<T>(value);
    }

    void operator=(T *ptr) { value = ptr; }

    typedef void ***** ConvertibleToBool;
    operator ConvertibleToBool() const { return (ConvertibleToBool) value; }

    const T *operator->() const { return value; }
    T *operator->() { return value; }

    T *get() { return value; }
};

#ifdef DEBUG
class CompartmentChecker
{
    JSContext *context;
    JSCompartment *compartment;

  public:
    explicit CompartmentChecker(JSContext *cx) : context(cx), compartment(cx->compartment) {
        check(cx->hasfp() ? JS_GetGlobalForScopeChain(cx) : cx->globalObject);
    }

    /*
     * Set a breakpoint here (break js::CompartmentChecker::fail) to debug
     * compartment mismatches.
     */
    static void fail(JSCompartment *c1, JSCompartment *c2) {
        printf("*** Compartment mismatch %p vs. %p\n", (void *) c1, (void *) c2);
        JS_NOT_REACHED("compartment mismatched");
    }

    /* Note: should only be used when neither c1 nor c2 may be the default compartment. */
    static void check(JSCompartment *c1, JSCompartment *c2) {
        JS_ASSERT(c1 != c1->rt->atomsCompartment);
        JS_ASSERT(c2 != c2->rt->atomsCompartment);
        if (c1 != c2)
            fail(c1, c2);
    }

    void check(JSCompartment *c) {
        if (c && c != context->runtime->atomsCompartment) {
            if (!compartment)
                compartment = c;
            else if (c != compartment)
                fail(compartment, c);
        }
    }

    void check(JSPrincipals *) { /* nothing for now */ }

    void check(JSObject *obj) {
        if (obj)
            check(obj->compartment());
    }

    void check(JSString *str) {
        if (!str->isAtom())
            check(str->compartment());
    }

    void check(const js::Value &v) {
        if (v.isObject())
            check(&v.toObject());
        else if (v.isString())
            check(v.toString());
    }

    void check(const ValueArray &arr) {
        for (size_t i = 0; i < arr.length; i++)
            check(arr.array[i]);
    }

    void check(const JSValueArray &arr) {
        for (size_t i = 0; i < arr.length; i++)
            check(arr.array[i]);
    }

    void check(const CallArgs &args) {
        for (Value *p = args.base(); p != args.end(); ++p)
            check(*p);
    }

    void check(jsid id) {
        if (JSID_IS_OBJECT(id))
            check(JSID_TO_OBJECT(id));
    }
    
    void check(JSIdArray *ida) {
        if (ida) {
            for (jsint i = 0; i < ida->length; i++) {
                if (JSID_IS_OBJECT(ida->vector[i]))
                    check(ida->vector[i]);
            }
        }
    }

    void check(JSScript *script) {
        if (script) {
            check(script->compartment());
            if (!script->isCachedEval && script->globalObject)
                check(script->globalObject);
        }
    }

    void check(StackFrame *fp) {
        if (fp)
            check(&fp->scopeChain());
    }
};

#endif

/*
 * Don't perform these checks when called from a finalizer. The checking
 * depends on other objects not having been swept yet.
 */
#define START_ASSERT_SAME_COMPARTMENT()                                       \
    if (cx->runtime->gcRunning)                                               \
        return;                                                               \
    CompartmentChecker c(cx)

template <class T1> inline void
assertSameCompartment(JSContext *cx, T1 t1)
{
#ifdef DEBUG
    START_ASSERT_SAME_COMPARTMENT();
    c.check(t1);
#endif
}

template <class T1, class T2> inline void
assertSameCompartment(JSContext *cx, T1 t1, T2 t2)
{
#ifdef DEBUG
    START_ASSERT_SAME_COMPARTMENT();
    c.check(t1);
    c.check(t2);
#endif
}

template <class T1, class T2, class T3> inline void
assertSameCompartment(JSContext *cx, T1 t1, T2 t2, T3 t3)
{
#ifdef DEBUG
    START_ASSERT_SAME_COMPARTMENT();
    c.check(t1);
    c.check(t2);
    c.check(t3);
#endif
}

template <class T1, class T2, class T3, class T4> inline void
assertSameCompartment(JSContext *cx, T1 t1, T2 t2, T3 t3, T4 t4)
{
#ifdef DEBUG
    START_ASSERT_SAME_COMPARTMENT();
    c.check(t1);
    c.check(t2);
    c.check(t3);
    c.check(t4);
#endif
}

template <class T1, class T2, class T3, class T4, class T5> inline void
assertSameCompartment(JSContext *cx, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5)
{
#ifdef DEBUG
    START_ASSERT_SAME_COMPARTMENT();
    c.check(t1);
    c.check(t2);
    c.check(t3);
    c.check(t4);
    c.check(t5);
#endif
}

#undef START_ASSERT_SAME_COMPARTMENT

STATIC_PRECONDITION_ASSUME(ubound(args.argv_) >= argc)
JS_ALWAYS_INLINE bool
CallJSNative(JSContext *cx, Native native, const CallArgs &args)
{
#ifdef DEBUG
    JSBool alreadyThrowing = cx->isExceptionPending();
#endif
    assertSameCompartment(cx, args);
    bool ok = native(cx, args.length(), args.base());
    if (ok) {
        assertSameCompartment(cx, args.rval());
        JS_ASSERT_IF(!alreadyThrowing, !cx->isExceptionPending());
    }
    return ok;
}

extern JSBool CallOrConstructBoundFunction(JSContext *, uintN, js::Value *);

STATIC_PRECONDITION(ubound(args.argv_) >= argc)
JS_ALWAYS_INLINE bool
CallJSNativeConstructor(JSContext *cx, Native native, const CallArgs &args)
{
#ifdef DEBUG
    JSObject &callee = args.callee();
#endif

    JS_ASSERT(args.thisv().isMagic());
    if (!CallJSNative(cx, native, args))
        return false;

    /*
     * Native constructors must return non-primitive values on success.
     * Although it is legal, if a constructor returns the callee, there is a
     * 99.9999% chance it is a bug. If any valid code actually wants the
     * constructor to return the callee, the assertion can be removed or
     * (another) conjunct can be added to the antecedent.
     *
     * Proxies are exceptions to both rules: they can return primitives and
     * they allow content to return the callee.
     *
     * CallOrConstructBoundFunction is an exception as well because we
     * might have used bind on a proxy function.
     *
     * (new Object(Object)) returns the callee.
     */
    JS_ASSERT_IF(native != FunctionProxyClass.construct &&
                 native != CallableObjectClass.construct &&
                 native != js::CallOrConstructBoundFunction &&
                 (!callee.isFunction() || callee.toFunction()->u.n.clasp != &ObjectClass),
                 !args.rval().isPrimitive() && callee != args.rval().toObject());

    return true;
}

JS_ALWAYS_INLINE bool
CallJSPropertyOp(JSContext *cx, PropertyOp op, JSObject *receiver, jsid id, Value *vp)
{
    assertSameCompartment(cx, receiver, id, *vp);
    JSBool ok = op(cx, receiver, id, vp);
    if (ok)
        assertSameCompartment(cx, receiver, *vp);
    return ok;
}

JS_ALWAYS_INLINE bool
CallJSPropertyOpSetter(JSContext *cx, StrictPropertyOp op, JSObject *obj, jsid id,
                       JSBool strict, Value *vp)
{
    assertSameCompartment(cx, obj, id, *vp);
    return op(cx, obj, id, strict, vp);
}

inline bool
CallSetter(JSContext *cx, JSObject *obj, jsid id, StrictPropertyOp op, uintN attrs,
           uintN shortid, JSBool strict, Value *vp)
{
    if (attrs & JSPROP_SETTER)
        return InvokeGetterOrSetter(cx, obj, CastAsObjectJsval(op), 1, vp, vp);

    if (attrs & JSPROP_GETTER)
        return js_ReportGetterOnlyAssignment(cx);

    if (attrs & JSPROP_SHORTID)
        id = INT_TO_JSID(shortid);
    return CallJSPropertyOpSetter(cx, op, obj, id, strict, vp);
}

static inline JSAtom **
FrameAtomBase(JSContext *cx, js::StackFrame *fp)
{
    return fp->script()->atoms;
}

}  /* namespace js */

inline JSVersion
JSContext::findVersion() const
{
    if (hasVersionOverride)
        return versionOverride;

    if (stack.hasfp()) {
        /* There may be a scripted function somewhere on the stack! */
        js::StackFrame *f = fp();
        while (f && !f->isScriptFrame())
            f = f->prev();
        if (f)
            return f->script()->getVersion();
    }

    return defaultVersion;
}

inline bool
JSContext::canSetDefaultVersion() const
{
    return !stack.hasfp() && !hasVersionOverride;
}

inline void
JSContext::overrideVersion(JSVersion newVersion)
{
    JS_ASSERT(!canSetDefaultVersion());
    versionOverride = newVersion;
    hasVersionOverride = true;
}

inline bool
JSContext::maybeOverrideVersion(JSVersion newVersion)
{
    if (canSetDefaultVersion()) {
        setDefaultVersion(newVersion);
        return false;
    }
    overrideVersion(newVersion);
    return true;
}

inline uintN
JSContext::getCompileOptions() const { return js::VersionFlagsToOptions(findVersion()); }

inline uintN
JSContext::allOptions() const { return getRunOptions() | getCompileOptions(); }

inline void
JSContext::setCompileOptions(uintN newcopts)
{
    JS_ASSERT((newcopts & JSCOMPILEOPTION_MASK) == newcopts);
    if (JS_LIKELY(getCompileOptions() == newcopts))
        return;
    JSVersion version = findVersion();
    JSVersion newVersion = js::OptionFlagsToVersion(newcopts, version);
    maybeOverrideVersion(newVersion);
}

inline void
JSContext::assertValidStackDepth(uintN depth)
{
#ifdef DEBUG
    JS_ASSERT(0 <= regs().sp - fp()->base());
    JS_ASSERT(depth <= uintptr_t(regs().sp - fp()->base()));
#endif
}

#ifdef JS_METHODJIT
inline js::mjit::JaegerCompartment *JSContext::jaegerCompartment()
{
    return compartment->jaegerCompartment();
}
#endif

inline js::LifoAlloc &
JSContext::typeLifoAlloc()
{
    return compartment->typeLifoAlloc;
}

inline bool
JSContext::ensureGeneratorStackSpace()
{
    bool ok = genStack.reserve(genStack.length() + 1);
    if (!ok)
        js_ReportOutOfMemory(this);
    return ok;
}

inline void
JSContext::setPendingException(js::Value v) {
    this->throwing = true;
    this->exception = v;
    js::assertSameCompartment(this, v);
}

inline bool
JSContext::ensureParseMapPool()
{
    if (parseMapPool_)
        return true;
    parseMapPool_ = js::OffTheBooks::new_<js::ParseMapPool>(this);
    return parseMapPool_;
}

/* Get the current frame, first lazily instantiating stack frames if needed. */
static inline js::StackFrame *
js_GetTopStackFrame(JSContext *cx, FrameExpandKind expand)
{
#ifdef JS_METHODJIT
    if (expand)
        js::mjit::ExpandInlineFrames(cx->compartment);
#endif

    return cx->maybefp();
}

#endif /* jscntxtinlines_h___ */
