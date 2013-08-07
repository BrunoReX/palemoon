/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=99:
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

/*
 * JS function support.
 */
#include <string.h>

#include "mozilla/Util.h"

#include "jstypes.h"
#include "jsutil.h"
#include "jsapi.h"
#include "jsarray.h"
#include "jsatom.h"
#include "jsbool.h"
#include "jscntxt.h"
#include "jsversion.h"
#include "jsfun.h"
#include "jsgc.h"
#include "jsgcmark.h"
#include "jsinterp.h"
#include "jslock.h"
#include "jsnum.h"
#include "jsobj.h"
#include "jsopcode.h"
#include "jspropertytree.h"
#include "jsproxy.h"
#include "jsscope.h"
#include "jsscript.h"
#include "jsstr.h"
#include "jsexn.h"

#include "frontend/BytecodeCompiler.h"
#include "frontend/BytecodeEmitter.h"
#include "frontend/TokenStream.h"
#include "vm/ScopeObject.h"
#include "vm/Debugger.h"

#if JS_HAS_GENERATORS
# include "jsiter.h"
#endif

#if JS_HAS_XDR
# include "jsxdrapi.h"
#endif

#ifdef JS_METHODJIT
#include "methodjit/MethodJIT.h"
#endif

#include "jsatominlines.h"
#include "jsfuninlines.h"
#include "jsinferinlines.h"
#include "jsobjinlines.h"
#include "jsscriptinlines.h"
#include "vm/ScopeObject-inl.h"
#include "vm/ArgumentsObject-inl.h"
#include "vm/Stack-inl.h"

using namespace mozilla;
using namespace js;
using namespace js::gc;
using namespace js::types;

inline JSObject *
JSObject::getThrowTypeError() const
{
    return global().getThrowTypeError();
}

JSBool
js_GetArgsValue(JSContext *cx, StackFrame *fp, Value *vp)
{
    JSObject *argsobj;
    if (fp->hasOverriddenArgs()) {
        JS_ASSERT(fp->hasCallObj());
        return fp->callObj().getProperty(cx, cx->runtime->atomState.argumentsAtom, vp);
    }
    argsobj = js_GetArgsObject(cx, fp);
    if (!argsobj)
        return JS_FALSE;
    vp->setObject(*argsobj);
    return JS_TRUE;
}

js::ArgumentsObject *
ArgumentsObject::create(JSContext *cx, uint32_t argc, JSObject &callee)
{
    JS_ASSERT(argc <= StackSpace::ARGS_LENGTH_MAX);

    JSObject *proto = callee.global().getOrCreateObjectPrototype(cx);
    if (!proto)
        return NULL;

    RootedVarTypeObject type(cx);

    type = proto->getNewType(cx);
    if (!type)
        return NULL;

    bool strict = callee.toFunction()->inStrictMode();
    Class *clasp = strict ? &StrictArgumentsObjectClass : &NormalArgumentsObjectClass;

    RootedVarShape emptyArgumentsShape(cx);
    emptyArgumentsShape =
        EmptyShape::getInitialShape(cx, clasp, proto,
                                    proto->getParent(), FINALIZE_KIND,
                                    BaseShape::INDEXED);
    if (!emptyArgumentsShape)
        return NULL;

    ArgumentsData *data = (ArgumentsData *)
        cx->malloc_(offsetof(ArgumentsData, slots) + argc * sizeof(Value));
    if (!data)
        return NULL;

    data->callee.init(ObjectValue(callee));
    InitValueRange(data->slots, argc, false);

    /* We have everything needed to fill in the object, so make the object. */
    JSObject *obj = JSObject::create(cx, FINALIZE_KIND, emptyArgumentsShape, type, NULL);
    if (!obj)
        return NULL;

    ArgumentsObject &argsobj = obj->asArguments();

    JS_ASSERT(UINT32_MAX > (uint64_t(argc) << PACKED_BITS_COUNT));
    argsobj.initInitialLength(argc);
    argsobj.initData(data);
    argsobj.setStackFrame(NULL);

    JS_ASSERT(argsobj.numFixedSlots() >= NormalArgumentsObject::RESERVED_SLOTS);
    JS_ASSERT(argsobj.numFixedSlots() >= StrictArgumentsObject::RESERVED_SLOTS);

    return &argsobj;
}

struct STATIC_SKIP_INFERENCE PutArg
{
    PutArg(JSCompartment *comp, HeapValue *dst) : dst(dst), compartment(comp) {}
    HeapValue *dst;
    JSCompartment *compartment;
    bool operator()(uintN, Value *src) {
        JS_ASSERT(dst->isMagic(JS_ARGS_HOLE) || dst->isUndefined());
        if (!dst->isMagic(JS_ARGS_HOLE))
            dst->set(compartment, *src);
        ++dst;
        return true;
    }
};

JSObject *
js_GetArgsObject(JSContext *cx, StackFrame *fp)
{
    /*
     * Arguments and Call objects are owned by the enclosing non-eval function
     * frame, thus any eval frames must be skipped before testing hasArgsObj.
     */
    JS_ASSERT(fp->isFunctionFrame());
    while (fp->isEvalInFunction())
        fp = fp->prev();

    /*
     * Mark all functions which have ever had arguments objects constructed,
     * which will prevent lazy arguments optimizations in the method JIT.
     */
    if (!fp->script()->createdArgs)
        types::MarkArgumentsCreated(cx, fp->script());

    /* Create an arguments object for fp only if it lacks one. */
    JS_ASSERT_IF(fp->fun()->isHeavyweight(), fp->hasCallObj());
    if (fp->hasArgsObj())
        return &fp->argsObj();

    ArgumentsObject *argsobj = ArgumentsObject::create(cx, fp->numActualArgs(), fp->callee());
    if (!argsobj)
        return argsobj;

    /*
     * Strict mode functions have arguments objects that copy the initial
     * actual parameter values.  It is the caller's responsibility to get the
     * arguments object before any parameters are modified!  (The emitter
     * ensures this by synthesizing an arguments access at the start of any
     * strict mode function that contains an assignment to a parameter, or
     * that calls eval.)  Non-strict mode arguments use the frame pointer to
     * retrieve up-to-date parameter values.
     */
    if (argsobj->isStrictArguments())
        fp->forEachCanonicalActualArg(PutArg(cx->compartment, argsobj->data()->slots));
    else
        argsobj->setStackFrame(fp);

    fp->setArgsObj(*argsobj);
    return argsobj;
}

void
js_PutArgsObject(StackFrame *fp)
{
    ArgumentsObject &argsobj = fp->argsObj();
    if (argsobj.isNormalArguments()) {
        JS_ASSERT(argsobj.maybeStackFrame() == fp);
        JSCompartment *comp = fp->scopeChain().compartment();
        fp->forEachCanonicalActualArg(PutArg(comp, argsobj.data()->slots));
        argsobj.setStackFrame(NULL);
    } else {
        JS_ASSERT(!argsobj.maybeStackFrame());
    }
}

static JSBool
args_delProperty(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    ArgumentsObject &argsobj = obj->asArguments();
    if (JSID_IS_INT(id)) {
        uintN arg = uintN(JSID_TO_INT(id));
        if (arg < argsobj.initialLength())
            argsobj.setElement(arg, MagicValue(JS_ARGS_HOLE));
    } else if (JSID_IS_ATOM(id, cx->runtime->atomState.lengthAtom)) {
        argsobj.markLengthOverridden();
    } else if (JSID_IS_ATOM(id, cx->runtime->atomState.calleeAtom)) {
        argsobj.asNormalArguments().clearCallee();
    }
    return true;
}

static JSBool
ArgGetter(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    if (!obj->isNormalArguments())
        return true;

    NormalArgumentsObject &argsobj = obj->asNormalArguments();
    if (JSID_IS_INT(id)) {
        /*
         * arg can exceed the number of arguments if a script changed the
         * prototype to point to another Arguments object with a bigger argc.
         */
        uintN arg = uintN(JSID_TO_INT(id));
        if (arg < argsobj.initialLength()) {
            JS_ASSERT(!argsobj.element(arg).isMagic(JS_ARGS_HOLE));
            if (StackFrame *fp = argsobj.maybeStackFrame())
                *vp = fp->canonicalActualArg(arg);
            else
                *vp = argsobj.element(arg);
        }
    } else if (JSID_IS_ATOM(id, cx->runtime->atomState.lengthAtom)) {
        if (!argsobj.hasOverriddenLength())
            vp->setInt32(argsobj.initialLength());
    } else {
        JS_ASSERT(JSID_IS_ATOM(id, cx->runtime->atomState.calleeAtom));
        const Value &v = argsobj.callee();
        if (!v.isMagic(JS_ARGS_HOLE))
            *vp = v;
    }
    return true;
}

static JSBool
ArgSetter(JSContext *cx, JSObject *obj, jsid id, JSBool strict, Value *vp)
{
    if (!obj->isNormalArguments())
        return true;

    NormalArgumentsObject &argsobj = obj->asNormalArguments();

    if (JSID_IS_INT(id)) {
        uintN arg = uintN(JSID_TO_INT(id));
        if (arg < argsobj.initialLength()) {
            if (StackFrame *fp = argsobj.maybeStackFrame()) {
                JSScript *script = fp->functionScript();
                if (script->usesArguments) {
                    if (arg < fp->numFormalArgs())
                        TypeScript::SetArgument(cx, script, arg, *vp);
                    fp->canonicalActualArg(arg) = *vp;
                }
                return true;
            }
        }
    } else {
        JS_ASSERT(JSID_IS_ATOM(id, cx->runtime->atomState.lengthAtom) ||
                  JSID_IS_ATOM(id, cx->runtime->atomState.calleeAtom));
    }

    /*
     * For simplicity we use delete/define to replace the property with one
     * backed by the default Object getter and setter. Note that we rely on
     * args_delProperty to clear the corresponding reserved slot so the GC can
     * collect its value. Note also that we must define the property instead
     * of setting it in case the user has changed the prototype to an object
     * that has a setter for this id.
     */
    AutoValueRooter tvr(cx);
    return js_DeleteGeneric(cx, &argsobj, id, tvr.addr(), false) &&
           js_DefineProperty(cx, &argsobj, id, vp, NULL, NULL, JSPROP_ENUMERATE);
}

static JSBool
args_resolve(JSContext *cx, JSObject *obj, jsid id, uintN flags,
             JSObject **objp)
{
    *objp = NULL;

    NormalArgumentsObject &argsobj = obj->asNormalArguments();

    uintN attrs = JSPROP_SHARED | JSPROP_SHADOWABLE;
    if (JSID_IS_INT(id)) {
        uint32_t arg = uint32_t(JSID_TO_INT(id));
        if (arg >= argsobj.initialLength() || argsobj.element(arg).isMagic(JS_ARGS_HOLE))
            return true;

        attrs |= JSPROP_ENUMERATE;
    } else if (JSID_IS_ATOM(id, cx->runtime->atomState.lengthAtom)) {
        if (argsobj.hasOverriddenLength())
            return true;
    } else {
        if (!JSID_IS_ATOM(id, cx->runtime->atomState.calleeAtom))
            return true;

        if (argsobj.callee().isMagic(JS_ARGS_HOLE))
            return true;
    }

    Value undef = UndefinedValue();
    if (!js_DefineProperty(cx, &argsobj, id, &undef, ArgGetter, ArgSetter, attrs))
        return JS_FALSE;

    *objp = &argsobj;
    return true;
}

static JSBool
args_enumerate(JSContext *cx, JSObject *obj)
{
    NormalArgumentsObject &argsobj = obj->asNormalArguments();

    /*
     * Trigger reflection in args_resolve using a series of js_LookupProperty
     * calls.
     */
    int argc = int(argsobj.initialLength());
    for (int i = -2; i != argc; i++) {
        jsid id = (i == -2)
                  ? ATOM_TO_JSID(cx->runtime->atomState.lengthAtom)
                  : (i == -1)
                  ? ATOM_TO_JSID(cx->runtime->atomState.calleeAtom)
                  : INT_TO_JSID(i);

        JSObject *pobj;
        JSProperty *prop;
        if (!js_LookupProperty(cx, &argsobj, id, &pobj, &prop))
            return false;
    }
    return true;
}

static JSBool
StrictArgGetter(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    if (!obj->isStrictArguments())
        return true;

    StrictArgumentsObject &argsobj = obj->asStrictArguments();

    if (JSID_IS_INT(id)) {
        /*
         * arg can exceed the number of arguments if a script changed the
         * prototype to point to another Arguments object with a bigger argc.
         */
        uintN arg = uintN(JSID_TO_INT(id));
        if (arg < argsobj.initialLength()) {
            const Value &v = argsobj.element(arg);
            if (!v.isMagic(JS_ARGS_HOLE))
                *vp = v;
        }
    } else {
        JS_ASSERT(JSID_IS_ATOM(id, cx->runtime->atomState.lengthAtom));
        if (!argsobj.hasOverriddenLength())
            vp->setInt32(argsobj.initialLength());
    }
    return true;
}

static JSBool
StrictArgSetter(JSContext *cx, JSObject *obj, jsid id, JSBool strict, Value *vp)
{
    if (!obj->isStrictArguments())
        return true;

    StrictArgumentsObject &argsobj = obj->asStrictArguments();

    if (JSID_IS_INT(id)) {
        uintN arg = uintN(JSID_TO_INT(id));
        if (arg < argsobj.initialLength()) {
            argsobj.setElement(arg, *vp);
            return true;
        }
    } else {
        JS_ASSERT(JSID_IS_ATOM(id, cx->runtime->atomState.lengthAtom));
    }

    /*
     * For simplicity we use delete/set to replace the property with one
     * backed by the default Object getter and setter. Note that we rely on
     * args_delProperty to clear the corresponding reserved slot so the GC can
     * collect its value.
     */
    AutoValueRooter tvr(cx);
    return js_DeleteGeneric(cx, &argsobj, id, tvr.addr(), strict) &&
           js_SetPropertyHelper(cx, &argsobj, id, 0, vp, strict);
}

static JSBool
strictargs_resolve(JSContext *cx, JSObject *obj, jsid id, uintN flags, JSObject **objp)
{
    *objp = NULL;

    StrictArgumentsObject &argsobj = obj->asStrictArguments();

    uintN attrs = JSPROP_SHARED | JSPROP_SHADOWABLE;
    PropertyOp getter = StrictArgGetter;
    StrictPropertyOp setter = StrictArgSetter;

    if (JSID_IS_INT(id)) {
        uint32_t arg = uint32_t(JSID_TO_INT(id));
        if (arg >= argsobj.initialLength() || argsobj.element(arg).isMagic(JS_ARGS_HOLE))
            return true;

        attrs |= JSPROP_ENUMERATE;
    } else if (JSID_IS_ATOM(id, cx->runtime->atomState.lengthAtom)) {
        if (argsobj.hasOverriddenLength())
            return true;
    } else {
        if (!JSID_IS_ATOM(id, cx->runtime->atomState.calleeAtom) &&
            !JSID_IS_ATOM(id, cx->runtime->atomState.callerAtom)) {
            return true;
        }

        attrs = JSPROP_PERMANENT | JSPROP_GETTER | JSPROP_SETTER | JSPROP_SHARED;
        getter = CastAsPropertyOp(argsobj.getThrowTypeError());
        setter = CastAsStrictPropertyOp(argsobj.getThrowTypeError());
    }

    Value undef = UndefinedValue();
    if (!js_DefineProperty(cx, &argsobj, id, &undef, getter, setter, attrs))
        return false;

    *objp = &argsobj;
    return true;
}

static JSBool
strictargs_enumerate(JSContext *cx, JSObject *obj)
{
    StrictArgumentsObject *argsobj = &obj->asStrictArguments();

    /*
     * Trigger reflection in strictargs_resolve using a series of
     * js_LookupProperty calls.
     */
    JSObject *pobj;
    JSProperty *prop;

    // length
    if (!js_LookupProperty(cx, argsobj, ATOM_TO_JSID(cx->runtime->atomState.lengthAtom), &pobj, &prop))
        return false;

    // callee
    if (!js_LookupProperty(cx, argsobj, ATOM_TO_JSID(cx->runtime->atomState.calleeAtom), &pobj, &prop))
        return false;

    // caller
    if (!js_LookupProperty(cx, argsobj, ATOM_TO_JSID(cx->runtime->atomState.callerAtom), &pobj, &prop))
        return false;

    for (uint32_t i = 0, argc = argsobj->initialLength(); i < argc; i++) {
        if (!js_LookupProperty(cx, argsobj, INT_TO_JSID(i), &pobj, &prop))
            return false;
    }

    return true;
}

static void
args_finalize(JSContext *cx, JSObject *obj)
{
    cx->free_(reinterpret_cast<void *>(obj->asArguments().data()));
}

static void
args_trace(JSTracer *trc, JSObject *obj)
{
    ArgumentsObject &argsobj = obj->asArguments();
    ArgumentsData *data = argsobj.data();
    MarkValue(trc, data->callee, js_callee_str);
    MarkValueRange(trc, argsobj.initialLength(), data->slots, js_arguments_str);

    /*
     * If a generator's arguments or call object escapes, and the generator
     * frame is not executing, the generator object needs to be marked because
     * it is not otherwise reachable. An executing generator is rooted by its
     * invocation.  To distinguish the two cases (which imply different access
     * paths to the generator object), we use the JSFRAME_FLOATING_GENERATOR
     * flag, which is only set on the StackFrame kept in the generator object's
     * JSGenerator.
     */
#if JS_HAS_GENERATORS
    StackFrame *fp = argsobj.maybeStackFrame();
    if (fp && fp->isFloatingGenerator())
        MarkObject(trc, js_FloatingFrameToGenerator(fp)->obj, "generator object");
#endif
}

/*
 * The classes below collaborate to lazily reflect and synchronize actual
 * argument values, argument count, and callee function object stored in a
 * StackFrame with their corresponding property values in the frame's
 * arguments object.
 */
Class js::NormalArgumentsObjectClass = {
    "Arguments",
    JSCLASS_NEW_RESOLVE |
    JSCLASS_HAS_RESERVED_SLOTS(NormalArgumentsObject::RESERVED_SLOTS) |
    JSCLASS_HAS_CACHED_PROTO(JSProto_Object),
    JS_PropertyStub,         /* addProperty */
    args_delProperty,
    JS_PropertyStub,         /* getProperty */
    JS_StrictPropertyStub,   /* setProperty */
    args_enumerate,
    reinterpret_cast<JSResolveOp>(args_resolve),
    JS_ConvertStub,
    args_finalize,           /* finalize   */
    NULL,                    /* reserved0   */
    NULL,                    /* checkAccess */
    NULL,                    /* call        */
    NULL,                    /* construct   */
    NULL,                    /* xdrObject   */
    NULL,                    /* hasInstance */
    args_trace
};

/*
 * Strict mode arguments is significantly less magical than non-strict mode
 * arguments, so it is represented by a different class while sharing some
 * functionality.
 */
Class js::StrictArgumentsObjectClass = {
    "Arguments",
    JSCLASS_NEW_RESOLVE |
    JSCLASS_HAS_RESERVED_SLOTS(StrictArgumentsObject::RESERVED_SLOTS) |
    JSCLASS_HAS_CACHED_PROTO(JSProto_Object),
    JS_PropertyStub,         /* addProperty */
    args_delProperty,
    JS_PropertyStub,         /* getProperty */
    JS_StrictPropertyStub,   /* setProperty */
    strictargs_enumerate,
    reinterpret_cast<JSResolveOp>(strictargs_resolve),
    JS_ConvertStub,
    args_finalize,           /* finalize   */
    NULL,                    /* reserved0   */
    NULL,                    /* checkAccess */
    NULL,                    /* call        */
    NULL,                    /* construct   */
    NULL,                    /* xdrObject   */
    NULL,                    /* hasInstance */
    args_trace
};

namespace js {

CallObject *
CreateFunCallObject(JSContext *cx, StackFrame *fp)
{
    JS_ASSERT(fp->isNonEvalFunctionFrame());
    JS_ASSERT(!fp->hasCallObj());

    JSObject *scopeChain = &fp->scopeChain();
    JS_ASSERT_IF(scopeChain->isWith() || scopeChain->isBlock() || scopeChain->isCall(),
                 scopeChain->getPrivate() != fp);

    /*
     * For a named function expression Call's parent points to an environment
     * object holding function's name.
     */
    if (JSAtom *lambdaName = CallObjectLambdaName(fp->fun())) {
        scopeChain = DeclEnvObject::create(cx, fp);
        if (!scopeChain)
            return NULL;

        if (!DefineNativeProperty(cx, scopeChain, ATOM_TO_JSID(lambdaName),
                                  ObjectValue(fp->callee()), NULL, NULL,
                                  JSPROP_PERMANENT | JSPROP_READONLY, 0, 0)) {
            return NULL;
        }
    }

    CallObject *callobj = CallObject::create(cx, fp->script(), *scopeChain, &fp->callee());
    if (!callobj)
        return NULL;

    callobj->setStackFrame(fp);
    fp->setScopeChainWithOwnCallObj(*callobj);
    return callobj;
}

CallObject *
CreateEvalCallObject(JSContext *cx, StackFrame *fp)
{
    CallObject *callobj = CallObject::create(cx, fp->script(), fp->scopeChain(), NULL);
    if (!callobj)
        return NULL;

    callobj->setStackFrame(fp);
    fp->setScopeChainWithOwnCallObj(*callobj);
    return callobj;
}

} // namespace js

void
js_PutCallObject(StackFrame *fp)
{
    CallObject &callobj = fp->callObj().asCall();
    JS_ASSERT(callobj.maybeStackFrame() == fp);
    JS_ASSERT_IF(fp->isEvalFrame(), fp->isStrictEvalFrame());
    JS_ASSERT(fp->isEvalFrame() == callobj.isForEval());

    /* Get the arguments object to snapshot fp's actual argument values. */
    if (fp->hasArgsObj()) {
        if (!fp->hasOverriddenArgs())
            callobj.initArguments(ObjectValue(fp->argsObj()));
        js_PutArgsObject(fp);
    }

    JSScript *script = fp->script();
    Bindings &bindings = script->bindings;

    if (callobj.isForEval()) {
        JS_ASSERT(script->strictModeCode);
        JS_ASSERT(bindings.countArgs() == 0);

        /* This could be optimized as below, but keep it simple for now. */
        callobj.copyValues(0, NULL, bindings.countVars(), fp->slots());
    } else {
        JSFunction *fun = fp->fun();
        JS_ASSERT(script == callobj.getCalleeFunction()->script());
        JS_ASSERT(script == fun->script());

        uintN n = bindings.countArgsAndVars();
        if (n > 0) {
            uint32_t nvars = bindings.countVars();
            uint32_t nargs = bindings.countArgs();
            JS_ASSERT(fun->nargs == nargs);
            JS_ASSERT(nvars + nargs == n);

            JSScript *script = fun->script();
            if (script->usesEval
#ifdef JS_METHODJIT
                || script->debugMode
#endif
                ) {
                callobj.copyValues(nargs, fp->formalArgs(), nvars, fp->slots());
            } else {
                /*
                 * For each arg & var that is closed over, copy it from the stack
                 * into the call object. We use initArg/VarUnchecked because,
                 * when you call a getter on a call object, js_NativeGetInline
                 * caches the return value in the slot, so we can't assert that
                 * it's undefined.
                 */
                uint32_t nclosed = script->nClosedArgs;
                for (uint32_t i = 0; i < nclosed; i++) {
                    uint32_t e = script->getClosedArg(i);
#ifdef JS_GC_ZEAL
                    callobj.setArg(e, fp->formalArg(e));
#else
                    callobj.initArgUnchecked(e, fp->formalArg(e));
#endif
                }

                nclosed = script->nClosedVars;
                for (uint32_t i = 0; i < nclosed; i++) {
                    uint32_t e = script->getClosedVar(i);
#ifdef JS_GC_ZEAL
                    callobj.setVar(e, fp->slots()[e]);
#else
                    callobj.initVarUnchecked(e, fp->slots()[e]);
#endif
                }
            }

            /*
             * Update the args and vars for the active call if this is an outer
             * function in a script nesting.
             */
            types::TypeScriptNesting *nesting = script->nesting();
            if (nesting && script->isOuterFunction) {
                nesting->argArray = callobj.argArray();
                nesting->varArray = callobj.varArray();
            }
        }

        /* Clear private pointers to fp, which is about to go away. */
        if (js_IsNamedLambda(fun)) {
            JSObject &env = callobj.enclosingScope();
            JS_ASSERT(env.asDeclEnv().maybeStackFrame() == fp);
            env.setPrivate(NULL);
        }
    }

    callobj.setStackFrame(NULL);
}

namespace js {

static JSBool
GetCallArguments(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    CallObject &callobj = obj->asCall();

    StackFrame *fp = callobj.maybeStackFrame();
    if (fp && !fp->hasOverriddenArgs()) {
        JSObject *argsobj = js_GetArgsObject(cx, fp);
        if (!argsobj)
            return false;
        vp->setObject(*argsobj);
    } else {
        *vp = callobj.getArguments();
    }
    return true;
}

static JSBool
SetCallArguments(JSContext *cx, JSObject *obj, jsid id, JSBool strict, Value *vp)
{
    CallObject &callobj = obj->asCall();

    if (StackFrame *fp = callobj.maybeStackFrame())
        fp->setOverriddenArgs();
    callobj.setArguments(*vp);
    return true;
}

JSBool
GetCallArg(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    CallObject &callobj = obj->asCall();
    JS_ASSERT((int16_t) JSID_TO_INT(id) == JSID_TO_INT(id));
    uintN i = (uint16_t) JSID_TO_INT(id);

    if (StackFrame *fp = callobj.maybeStackFrame())
        *vp = fp->formalArg(i);
    else
        *vp = callobj.arg(i);
    return true;
}

JSBool
SetCallArg(JSContext *cx, JSObject *obj, jsid id, JSBool strict, Value *vp)
{
    CallObject &callobj = obj->asCall();
    JS_ASSERT((int16_t) JSID_TO_INT(id) == JSID_TO_INT(id));
    uintN i = (uint16_t) JSID_TO_INT(id);

    if (StackFrame *fp = callobj.maybeStackFrame())
        fp->formalArg(i) = *vp;
    else
        callobj.setArg(i, *vp);

    JSFunction *fun = callobj.getCalleeFunction();
    JSScript *script = fun->script();
    if (!script->ensureHasTypes(cx))
        return false;

    TypeScript::SetArgument(cx, script, i, *vp);

    return true;
}

JSBool
GetCallUpvar(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    CallObject &callobj = obj->asCall();
    JS_ASSERT((int16_t) JSID_TO_INT(id) == JSID_TO_INT(id));
    uintN i = (uint16_t) JSID_TO_INT(id);

    *vp = callobj.getCallee()->toFunction()->getFlatClosureUpvar(i);
    return true;
}

JSBool
SetCallUpvar(JSContext *cx, JSObject *obj, jsid id, JSBool strict, Value *vp)
{
    CallObject &callobj = obj->asCall();
    JS_ASSERT((int16_t) JSID_TO_INT(id) == JSID_TO_INT(id));
    uintN i = (uint16_t) JSID_TO_INT(id);

    callobj.getCallee()->toFunction()->setFlatClosureUpvar(i, *vp);
    return true;
}

JSBool
GetCallVar(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    CallObject &callobj = obj->asCall();
    JS_ASSERT((int16_t) JSID_TO_INT(id) == JSID_TO_INT(id));
    uintN i = (uint16_t) JSID_TO_INT(id);

    if (StackFrame *fp = callobj.maybeStackFrame())
        *vp = fp->varSlot(i);
    else
        *vp = callobj.var(i);
    return true;
}

JSBool
SetCallVar(JSContext *cx, JSObject *obj, jsid id, JSBool strict, Value *vp)
{
    CallObject &callobj = obj->asCall();

    JS_ASSERT((int16_t) JSID_TO_INT(id) == JSID_TO_INT(id));
    uintN i = (uint16_t) JSID_TO_INT(id);

    if (StackFrame *fp = callobj.maybeStackFrame())
        fp->varSlot(i) = *vp;
    else
        callobj.setVar(i, *vp);

    JSFunction *fun = callobj.getCalleeFunction();
    JSScript *script = fun->script();
    if (!script->ensureHasTypes(cx))
        return false;

    TypeScript::SetLocal(cx, script, i, *vp);

    return true;
}

} // namespace js

static JSBool
call_resolve(JSContext *cx, JSObject *obj, jsid id, uintN flags, JSObject **objp)
{
    JS_ASSERT(!obj->getProto());

    if (!JSID_IS_ATOM(id))
        return true;

    JSObject *callee = obj->asCall().getCallee();
#ifdef DEBUG
    if (callee) {
        JSScript *script = callee->toFunction()->script();
        JS_ASSERT(!script->bindings.hasBinding(cx, JSID_TO_ATOM(id)));
    }
#endif

    /*
     * Resolve arguments so that we never store a particular Call object's
     * arguments object reference in a Call prototype's |arguments| slot.
     *
     * Include JSPROP_ENUMERATE for consistency with all other Call object
     * properties; see js::Bindings::add and js::Interpret's JSOP_DEFFUN
     * rebinding-Call-property logic.
     */
    if (callee && id == ATOM_TO_JSID(cx->runtime->atomState.argumentsAtom)) {
        if (!DefineNativeProperty(cx, obj, id, UndefinedValue(),
                                  GetCallArguments, SetCallArguments,
                                  JSPROP_PERMANENT | JSPROP_SHARED | JSPROP_ENUMERATE,
                                  0, 0, DNP_DONT_PURGE)) {
            return false;
        }
        *objp = obj;
        return true;
    }

    /* Control flow reaches here only if id was not resolved. */
    return true;
}

static void
call_trace(JSTracer *trc, JSObject *obj)
{
    JS_ASSERT(obj->isCall());

    /* Mark any generator frame, as for arguments objects. */
#if JS_HAS_GENERATORS
    StackFrame *fp = (StackFrame *) obj->getPrivate();
    if (fp && fp->isFloatingGenerator())
        MarkObject(trc, js_FloatingFrameToGenerator(fp)->obj, "generator object");
#endif
}

JS_PUBLIC_DATA(Class) js::CallClass = {
    "Call",
    JSCLASS_HAS_PRIVATE |
    JSCLASS_HAS_RESERVED_SLOTS(CallObject::RESERVED_SLOTS) |
    JSCLASS_NEW_RESOLVE | JSCLASS_IS_ANONYMOUS,
    JS_PropertyStub,         /* addProperty */
    JS_PropertyStub,         /* delProperty */
    JS_PropertyStub,         /* getProperty */
    JS_StrictPropertyStub,   /* setProperty */
    JS_EnumerateStub,
    (JSResolveOp)call_resolve,
    NULL,                    /* convert: Leave it NULL so we notice if calls ever escape */
    NULL,                    /* finalize */
    NULL,                    /* reserved0   */
    NULL,                    /* checkAccess */
    NULL,                    /* call        */
    NULL,                    /* construct   */
    NULL,                    /* xdrObject   */
    NULL,                    /* hasInstance */
    call_trace
};

bool
StackFrame::getValidCalleeObject(JSContext *cx, Value *vp)
{
    if (!isFunctionFrame()) {
        vp->setNull();
        return true;
    }

    JSFunction *fun = this->callee().toFunction();
    vp->setObject(*fun);

    /*
     * Check for an escape attempt by a joined function object, which must go
     * through the frame's |this| object's method read barrier for the method
     * atom by which it was uniquely associated with a property.
     */
    const Value &thisv = functionThis();
    if (thisv.isObject() && fun->methodAtom() && !fun->isClonedMethod()) {
        JSObject *thisp = &thisv.toObject();
        JSObject *first_barriered_thisp = NULL;

        do {
            /*
             * While a non-native object is responsible for handling its
             * entire prototype chain, notable non-natives including dense
             * and typed arrays have native prototypes, so keep going.
             */
            if (!thisp->isNative())
                continue;

            const Shape *shape = thisp->nativeLookup(cx, ATOM_TO_JSID(fun->methodAtom()));
            if (shape) {
                /*
                 * Two cases follow: the method barrier was not crossed
                 * yet, so we cross it here; the method barrier *was*
                 * crossed but after the call, in which case we fetch
                 * and validate the cloned (unjoined) funobj from the
                 * method property's slot.
                 *
                 * In either case we must allow for the method property
                 * to have been replaced, or its value overwritten.
                 */
                if (shape->isMethod() && thisp->nativeGetMethod(shape) == fun) {
                    if (!thisp->methodReadBarrier(cx, *shape, vp))
                        return false;
                    overwriteCallee(vp->toObject());
                    return true;
                }

                if (shape->hasSlot()) {
                    Value v = thisp->getSlot(shape->slot());
                    JSFunction *clone;

                    if (IsFunctionObject(v, &clone) &&
                        clone->isInterpreted() &&
                        clone->script() == fun->script() &&
                        clone->methodObj() == thisp) {
                        /*
                         * N.B. If the method barrier was on a function
                         * with singleton type, then while crossing the
                         * method barrier CloneFunctionObject will have
                         * ignored the attempt to clone the function.
                         */
                        JS_ASSERT_IF(!clone->hasSingletonType(), clone != fun);
                        *vp = v;
                        overwriteCallee(*clone);
                        return true;
                    }
                }
            }

            if (!first_barriered_thisp)
                first_barriered_thisp = thisp;
        } while ((thisp = thisp->getProto()) != NULL);

        if (!first_barriered_thisp)
            return true;

        /*
         * At this point, we couldn't find an already-existing clone (or
         * force to exist a fresh clone) created via thisp's method read
         * barrier, so we must clone fun and store it in fp's callee to
         * avoid re-cloning upon repeated foo.caller access.
         *
         * This must mean the code in js_DeleteGeneric could not find this
         * stack frame on the stack when the method was deleted. We've lost
         * track of the method, so we associate it with the first barriered
         * object found starting from thisp on the prototype chain.
         */
        JSFunction *newfunobj = CloneFunctionObject(cx, fun);
        if (!newfunobj)
            return false;
        newfunobj->setMethodObj(*first_barriered_thisp);
        overwriteCallee(*newfunobj);
        vp->setObject(*newfunobj);
        return true;
    }

    return true;
}

static JSBool
fun_getProperty(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    while (!obj->isFunction()) {
        obj = obj->getProto();
        if (!obj)
            return true;
    }
    JSFunction *fun = obj->toFunction();

    /*
     * Mark the function's script as uninlineable, to expand any of its
     * frames on the stack before we go looking for them. This allows the
     * below walk to only check each explicit frame rather than needing to
     * check any calls that were inlined.
     */
    if (fun->isInterpreted()) {
        fun->script()->uninlineable = true;
        MarkTypeObjectFlags(cx, fun, OBJECT_FLAG_UNINLINEABLE);
    }

    /* Set to early to null in case of error */
    vp->setNull();

    /* Find fun's top-most activation record. */
    StackFrame *fp = js_GetTopStackFrame(cx, FRAME_EXPAND_NONE);
    if (!fp)
        return true;

    while (!fp->isFunctionFrame() || &fp->callee() != fun || fp->isEvalFrame()) {
        fp = fp->prev();
        if (!fp)
            return true;
    }

#ifdef JS_METHODJIT
    if (JSID_IS_ATOM(id, cx->runtime->atomState.callerAtom) && fp && fp->prev()) {
        /*
         * If the frame was called from within an inlined frame, mark the
         * innermost function as uninlineable to expand its frame and allow us
         * to recover its callee object.
         */
        JSInlinedSite *inlined;
        jsbytecode *prevpc = fp->prev()->pcQuadratic(cx->stack, fp, &inlined);
        if (inlined) {
            mjit::JITChunk *chunk = fp->prev()->jit()->chunk(prevpc);
            JSFunction *fun = chunk->inlineFrames()[inlined->inlineIndex].fun;
            fun->script()->uninlineable = true;
            MarkTypeObjectFlags(cx, fun, OBJECT_FLAG_UNINLINEABLE);
        }
    }
#endif

    if (JSID_IS_ATOM(id, cx->runtime->atomState.argumentsAtom)) {
        /* Warn if strict about f.arguments or equivalent unqualified uses. */
        if (!JS_ReportErrorFlagsAndNumber(cx, JSREPORT_WARNING | JSREPORT_STRICT, js_GetErrorMessage,
                                          NULL, JSMSG_DEPRECATED_USAGE, js_arguments_str)) {
            return false;
        }

        /*
         * Purposefully disconnect the returned arguments object from the frame
         * by always creating a new copy that does not alias formal parameters.
         * This allows function-local analysis to determine that formals are
         * not aliased and generally simplifies arguments objects.
         */
        ArgumentsObject *argsobj = ArgumentsObject::create(cx, fp->numActualArgs(), fp->callee());
        if (!argsobj)
            return false;

        fp->forEachCanonicalActualArg(PutArg(cx->compartment, argsobj->data()->slots));
        *vp = ObjectValue(*argsobj);
        return true;
    }

    if (JSID_IS_ATOM(id, cx->runtime->atomState.callerAtom)) {
        if (!fp->prev())
            return true;

        StackFrame *frame = js_GetScriptedCaller(cx, fp->prev());
        if (frame && !frame->getValidCalleeObject(cx, vp))
            return false;

        if (!vp->isObject()) {
            JS_ASSERT(vp->isNull());
            return true;
        }

        /* Censor the caller if it is from another compartment. */
        JSObject &caller = vp->toObject();
        if (caller.compartment() != cx->compartment) {
            vp->setNull();
        } else if (caller.isFunction()) {
            JSFunction *callerFun = caller.toFunction();
            if (callerFun->isInterpreted() && callerFun->inStrictMode()) {
                JS_ReportErrorFlagsAndNumber(cx, JSREPORT_ERROR, js_GetErrorMessage, NULL,
                                             JSMSG_CALLER_IS_STRICT);
                return false;
            }
        }

        return true;
    }

    JS_NOT_REACHED("fun_getProperty");
    return false;
}



/* NB: no sentinels at ends -- use ArrayLength to bound loops.
 * Properties censored into [[ThrowTypeError]] in strict mode. */
static const uint16_t poisonPillProps[] = {
    ATOM_OFFSET(arguments),
    ATOM_OFFSET(caller),
};

static JSBool
fun_enumerate(JSContext *cx, JSObject *obj)
{
    JS_ASSERT(obj->isFunction());

    RootObject root(cx, &obj);

    jsid id;
    bool found;

    if (!obj->isBoundFunction()) {
        id = ATOM_TO_JSID(cx->runtime->atomState.classPrototypeAtom);
        if (!obj->hasProperty(cx, id, &found, JSRESOLVE_QUALIFIED))
            return false;
    }

    id = ATOM_TO_JSID(cx->runtime->atomState.lengthAtom);
    if (!obj->hasProperty(cx, id, &found, JSRESOLVE_QUALIFIED))
        return false;
        
    id = ATOM_TO_JSID(cx->runtime->atomState.nameAtom);
    if (!obj->hasProperty(cx, id, &found, JSRESOLVE_QUALIFIED))
        return false;

    for (uintN i = 0; i < ArrayLength(poisonPillProps); i++) {
        const uint16_t offset = poisonPillProps[i];
        id = ATOM_TO_JSID(OFFSET_TO_ATOM(cx->runtime, offset));
        if (!obj->hasProperty(cx, id, &found, JSRESOLVE_QUALIFIED))
            return false;
    }

    return true;
}

static JSObject *
ResolveInterpretedFunctionPrototype(JSContext *cx, JSObject *obj)
{
#ifdef DEBUG
    JSFunction *fun = obj->toFunction();
    JS_ASSERT(fun->isInterpreted());
    JS_ASSERT(!fun->isFunctionPrototype());
#endif

    /*
     * Assert that fun is not a compiler-created function object, which
     * must never leak to script or embedding code and then be mutated.
     * Also assert that obj is not bound, per the ES5 15.3.4.5 ref above.
     */
    JS_ASSERT(!IsInternalFunctionObject(obj));
    JS_ASSERT(!obj->isBoundFunction());

    /*
     * Make the prototype object an instance of Object with the same parent
     * as the function object itself.
     */
    JSObject *objProto;
    if (!js_GetClassPrototype(cx, obj->getParent(), JSProto_Object, &objProto))
        return NULL;
    JSObject *proto = NewObjectWithGivenProto(cx, &ObjectClass, objProto, NULL);
    if (!proto || !proto->setSingletonType(cx))
        return NULL;

    /*
     * Per ES5 15.3.5.2 a user-defined function's .prototype property is
     * initially non-configurable, non-enumerable, and writable.  Per ES5 13.2
     * the prototype's .constructor property is configurable, non-enumerable,
     * and writable.
     */
    if (!obj->defineProperty(cx, cx->runtime->atomState.classPrototypeAtom,
                             ObjectValue(*proto), JS_PropertyStub, JS_StrictPropertyStub,
                             JSPROP_PERMANENT) ||
        !proto->defineProperty(cx, cx->runtime->atomState.constructorAtom,
                               ObjectValue(*obj), JS_PropertyStub, JS_StrictPropertyStub, 0))
    {
       return NULL;
    }

    return proto;
}

static JSBool
fun_resolve(JSContext *cx, JSObject *obj, jsid id, uintN flags,
            JSObject **objp)
{
    if (!JSID_IS_ATOM(id))
        return true;

    RootedVarFunction fun(cx);
    fun = obj->toFunction();

    if (JSID_IS_ATOM(id, cx->runtime->atomState.classPrototypeAtom)) {
        /*
         * Native or "built-in" functions do not have a .prototype property per
         * ECMA-262, or (Object.prototype, Function.prototype, etc.) have that
         * property created eagerly.
         *
         * ES5 15.3.4: the non-native function object named Function.prototype
         * does not have a .prototype property.
         *
         * ES5 15.3.4.5: bound functions don't have a prototype property. The
         * isNative() test covers this case because bound functions are native
         * functions by definition/construction.
         */
        if (fun->isNative() || fun->isFunctionPrototype())
            return true;

        if (!ResolveInterpretedFunctionPrototype(cx, fun))
            return false;
        *objp = fun;
        return true;
    }

    if (JSID_IS_ATOM(id, cx->runtime->atomState.lengthAtom) ||
        JSID_IS_ATOM(id, cx->runtime->atomState.nameAtom)) {
        JS_ASSERT(!IsInternalFunctionObject(obj));

        Value v;
        if (JSID_IS_ATOM(id, cx->runtime->atomState.lengthAtom))
            v.setInt32(fun->nargs);
        else
            v.setString(fun->atom ? fun->atom : cx->runtime->emptyString);
        
        if (!DefineNativeProperty(cx, fun, id, v, JS_PropertyStub, JS_StrictPropertyStub,
                                  JSPROP_PERMANENT | JSPROP_READONLY, 0, 0)) {
            return false;
        }
        *objp = fun;
        return true;
    }

    for (uintN i = 0; i < ArrayLength(poisonPillProps); i++) {
        const uint16_t offset = poisonPillProps[i];

        if (JSID_IS_ATOM(id, OFFSET_TO_ATOM(cx->runtime, offset))) {
            JS_ASSERT(!IsInternalFunctionObject(fun));

            PropertyOp getter;
            StrictPropertyOp setter;
            uintN attrs = JSPROP_PERMANENT;
            if (fun->isInterpreted() ? fun->inStrictMode() : fun->isBoundFunction()) {
                JSObject *throwTypeError = fun->getThrowTypeError();

                getter = CastAsPropertyOp(throwTypeError);
                setter = CastAsStrictPropertyOp(throwTypeError);
                attrs |= JSPROP_GETTER | JSPROP_SETTER;
            } else {
                getter = fun_getProperty;
                setter = JS_StrictPropertyStub;
            }

            if (!DefineNativeProperty(cx, fun, id, UndefinedValue(), getter, setter,
                                      attrs, 0, 0)) {
                return false;
            }
            *objp = fun;
            return true;
        }
    }

    return true;
}

#if JS_HAS_XDR

/* XXX store parent and proto, if defined */
JSBool
js_XDRFunctionObject(JSXDRState *xdr, JSObject **objp)
{
    JSContext *cx;
    JSFunction *fun;
    uint32_t firstword;           /* flag telling whether fun->atom is non-null,
                                   plus for fun->u.i.skipmin, fun->u.i.wrapper,
                                   and 14 bits reserved for future use */
    uint32_t flagsword;           /* word for argument count and fun->flags */

    cx = xdr->cx;
    JSScript *script;
    if (xdr->mode == JSXDR_ENCODE) {
        fun = (*objp)->toFunction();
        if (!fun->isInterpreted()) {
            JSAutoByteString funNameBytes;
            if (const char *name = GetFunctionNameBytes(cx, fun, &funNameBytes)) {
                JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_NOT_SCRIPTED_FUNCTION,
                                     name);
            }
            return false;
        }
        firstword = !!fun->atom;
        flagsword = (fun->nargs << 16) | fun->flags;
        script = fun->script();
    } else {
        RootedVarObject parent(cx, NULL);
        fun = js_NewFunction(cx, NULL, NULL, 0, JSFUN_INTERPRETED, parent, NULL);
        if (!fun)
            return false;
        if (!fun->clearParent(cx))
            return false;
        if (!fun->clearType(cx))
            return false;
        script = NULL;
    }

    if (!JS_XDRUint32(xdr, &firstword))
        return false;
    if ((firstword & 1U) && !js_XDRAtom(xdr, &fun->atom))
        return false;
    if (!JS_XDRUint32(xdr, &flagsword))
        return false;

    if (!js_XDRScript(xdr, &script))
        return false;

    if (xdr->mode == JSXDR_DECODE) {
        fun->nargs = flagsword >> 16;
        JS_ASSERT((flagsword & JSFUN_KINDMASK) >= JSFUN_INTERPRETED);
        fun->flags = uint16_t(flagsword);
        fun->setScript(script);
        if (!script->typeSetFunction(cx, fun))
            return false;
        JS_ASSERT(fun->nargs == fun->script()->bindings.countArgs());
        js_CallNewScriptHook(cx, fun->script(), fun);
        *objp = fun;
    }

    return true;
}

#else  /* !JS_HAS_XDR */

#define js_XDRFunctionObject NULL

#endif /* !JS_HAS_XDR */

/*
 * [[HasInstance]] internal method for Function objects: fetch the .prototype
 * property of its 'this' parameter, and walks the prototype chain of v (only
 * if v is an object) returning true if .prototype is found.
 */
static JSBool
fun_hasInstance(JSContext *cx, JSObject *obj, const Value *v, JSBool *bp)
{
    while (obj->isFunction()) {
        if (!obj->isBoundFunction())
            break;
        obj = obj->toFunction()->getBoundFunctionTarget();
    }

    Value pval;
    if (!obj->getProperty(cx, cx->runtime->atomState.classPrototypeAtom, &pval))
        return JS_FALSE;

    if (pval.isPrimitive()) {
        /*
         * Throw a runtime error if instanceof is called on a function that
         * has a non-object as its .prototype value.
         */
        js_ReportValueError(cx, JSMSG_BAD_PROTOTYPE, -1, ObjectValue(*obj), NULL);
        return JS_FALSE;
    }

    *bp = js_IsDelegate(cx, &pval.toObject(), *v);
    return JS_TRUE;
}

inline void
JSFunction::trace(JSTracer *trc)
{
    if (isFlatClosure() && hasFlatClosureUpvars()) {
        if (HeapValue *upvars = getFlatClosureUpvars())
            MarkValueRange(trc, script()->bindings.countUpvars(), upvars, "upvars");
    }

    if (isExtended()) {
        MarkValueRange(trc, ArrayLength(toExtended()->extendedSlots),
                       toExtended()->extendedSlots, "nativeReserved");
    }

    if (atom)
        MarkStringUnbarriered(trc, atom, "atom");

    if (isInterpreted()) {
        if (script())
            MarkScript(trc, script(), "script");
        if (environment())
            MarkObjectUnbarriered(trc, environment(), "fun_callscope");
    }
}

static void
fun_trace(JSTracer *trc, JSObject *obj)
{
    obj->toFunction()->trace(trc);
}

static void
fun_finalize(JSContext *cx, JSObject *obj)
{
    if (obj->toFunction()->isFlatClosure())
        obj->toFunction()->finalizeUpvars();
}

/*
 * Reserve two slots in all function objects for XPConnect.  Note that this
 * does not bloat every instance, only those on which reserved slots are set,
 * and those on which ad-hoc properties are defined.
 */
JS_FRIEND_DATA(Class) js::FunctionClass = {
    js_Function_str,
    JSCLASS_NEW_RESOLVE |
    JSCLASS_HAS_CACHED_PROTO(JSProto_Function),
    JS_PropertyStub,         /* addProperty */
    JS_PropertyStub,         /* delProperty */
    JS_PropertyStub,         /* getProperty */
    JS_StrictPropertyStub,   /* setProperty */
    fun_enumerate,
    (JSResolveOp)fun_resolve,
    JS_ConvertStub,
    fun_finalize,
    NULL,                    /* reserved0   */
    NULL,                    /* checkAccess */
    NULL,                    /* call        */
    NULL,                    /* construct   */
    NULL,
    fun_hasInstance,
    fun_trace
};

JSString *
fun_toStringHelper(JSContext *cx, JSObject *obj, uintN indent)
{
    if (!obj->isFunction()) {
        if (IsFunctionProxy(obj))
            return Proxy::fun_toString(cx, obj, indent);
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                             JSMSG_INCOMPATIBLE_PROTO,
                             js_Function_str, js_toString_str,
                             "object");
        return NULL;
    }

    JSFunction *fun = obj->toFunction();
    if (!fun)
        return NULL;

    if (!indent && !cx->compartment->toSourceCache.empty()) {
        ToSourceCache::Ptr p = cx->compartment->toSourceCache.ref().lookup(fun);
        if (p)
            return p->value;
    }

    JSString *str = JS_DecompileFunction(cx, fun, indent);
    if (!str)
        return NULL;

    if (!indent) {
        Maybe<ToSourceCache> &lazy = cx->compartment->toSourceCache;

        if (lazy.empty()) {
            lazy.construct();
            if (!lazy.ref().init())
                return NULL;
        }

        if (!lazy.ref().put(fun, str))
            return NULL;
    }

    return str;
}

static JSBool
fun_toString(JSContext *cx, uintN argc, Value *vp)
{
    JS_ASSERT(IsFunctionObject(vp[0]));
    uint32_t indent = 0;

    if (argc != 0 && !ToUint32(cx, vp[2], &indent))
        return false;

    JSObject *obj = ToObject(cx, &vp[1]);
    if (!obj)
        return false;

    JSString *str = fun_toStringHelper(cx, obj, indent);
    if (!str)
        return false;

    vp->setString(str);
    return true;
}

#if JS_HAS_TOSOURCE
static JSBool
fun_toSource(JSContext *cx, uintN argc, Value *vp)
{
    JS_ASSERT(IsFunctionObject(vp[0]));

    JSObject *obj = ToObject(cx, &vp[1]);
    if (!obj)
        return false;

    JSString *str = fun_toStringHelper(cx, obj, JS_DONT_PRETTY_PRINT);
    if (!str)
        return false;

    vp->setString(str);
    return true;
}
#endif

JSBool
js_fun_call(JSContext *cx, uintN argc, Value *vp)
{
    Value fval = vp[1];

    if (!js_IsCallable(fval)) {
        ReportIncompatibleMethod(cx, CallReceiverFromVp(vp), &FunctionClass);
        return false;
    }

    Value *argv = vp + 2;
    Value thisv;
    if (argc == 0) {
        thisv.setUndefined();
    } else {
        thisv = argv[0];

        argc--;
        argv++;
    }

    /* Allocate stack space for fval, obj, and the args. */
    InvokeArgsGuard args;
    if (!cx->stack.pushInvokeArgs(cx, argc, &args))
        return JS_FALSE;

    /* Push fval, thisv, and the args. */
    args.calleev() = fval;
    args.thisv() = thisv;
    PodCopy(args.array(), argv, argc);

    bool ok = Invoke(cx, args);
    *vp = args.rval();
    return ok;
}

/* ES5 15.3.4.3 */
JSBool
js_fun_apply(JSContext *cx, uintN argc, Value *vp)
{
    /* Step 1. */
    Value fval = vp[1];
    if (!js_IsCallable(fval)) {
        ReportIncompatibleMethod(cx, CallReceiverFromVp(vp), &FunctionClass);
        return false;
    }

    /* Step 2. */
    if (argc < 2 || vp[3].isNullOrUndefined())
        return js_fun_call(cx, (argc > 0) ? 1 : 0, vp);

    /* N.B. Changes need to be propagated to stubs::SplatApplyArgs. */

    /* Step 3. */
    if (!vp[3].isObject()) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_BAD_APPLY_ARGS, js_apply_str);
        return false;
    }

    /*
     * Steps 4-5 (note erratum removing steps originally numbered 5 and 7 in
     * original version of ES5).
     */
    JSObject *aobj = &vp[3].toObject();
    jsuint length;
    if (!js_GetLengthProperty(cx, aobj, &length))
        return false;

    /* Step 6. */
    if (length > StackSpace::ARGS_LENGTH_MAX) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_TOO_MANY_FUN_APPLY_ARGS);
        return false;
    }

    InvokeArgsGuard args;
    if (!cx->stack.pushInvokeArgs(cx, length, &args))
        return false;

    /* Push fval, obj, and aobj's elements as args. */
    args.calleev() = fval;
    args.thisv() = vp[2];

    /* Steps 7-8. */
    if (!GetElements(cx, aobj, length, args.array()))
        return false;

    /* Step 9. */
    if (!Invoke(cx, args))
        return false;
    *vp = args.rval();
    return true;
}

namespace js {

JSBool
CallOrConstructBoundFunction(JSContext *cx, uintN argc, Value *vp);

}

static const uint32_t JSSLOT_BOUND_FUNCTION_THIS       = 0;
static const uint32_t JSSLOT_BOUND_FUNCTION_ARGS_COUNT = 1;

static const uint32_t BOUND_FUNCTION_RESERVED_SLOTS = 2;

inline bool
JSFunction::initBoundFunction(JSContext *cx, const Value &thisArg,
                              const Value *args, uintN argslen)
{
    JS_ASSERT(isFunction());

    /*
     * Convert to a dictionary to set the BOUND_FUNCTION flag and increase
     * the slot span to cover the arguments and additional slots for the 'this'
     * value and arguments count.
     */
    if (!toDictionaryMode(cx))
        return false;

    if (!setFlag(cx, BaseShape::BOUND_FUNCTION))
        return false;

    if (!setSlotSpan(cx, BOUND_FUNCTION_RESERVED_SLOTS + argslen))
        return false;

    setSlot(JSSLOT_BOUND_FUNCTION_THIS, thisArg);
    setSlot(JSSLOT_BOUND_FUNCTION_ARGS_COUNT, PrivateUint32Value(argslen));

    initSlotRange(BOUND_FUNCTION_RESERVED_SLOTS, args, argslen);

    return true;
}

inline JSObject *
JSFunction::getBoundFunctionTarget() const
{
    JS_ASSERT(isFunction());
    JS_ASSERT(isBoundFunction());

    /* Bound functions abuse |parent| to store their target function. */
    return getParent();
}

inline const js::Value &
JSFunction::getBoundFunctionThis() const
{
    JS_ASSERT(isFunction());
    JS_ASSERT(isBoundFunction());

    return getSlot(JSSLOT_BOUND_FUNCTION_THIS);
}

inline const js::Value &
JSFunction::getBoundFunctionArgument(uintN which) const
{
    JS_ASSERT(isFunction());
    JS_ASSERT(isBoundFunction());
    JS_ASSERT(which < getBoundFunctionArgumentCount());

    return getSlot(BOUND_FUNCTION_RESERVED_SLOTS + which);
}

inline size_t
JSFunction::getBoundFunctionArgumentCount() const
{
    JS_ASSERT(isFunction());
    JS_ASSERT(isBoundFunction());

    return getSlot(JSSLOT_BOUND_FUNCTION_ARGS_COUNT).toPrivateUint32();
}

namespace js {

/* ES5 15.3.4.5.1 and 15.3.4.5.2. */
JSBool
CallOrConstructBoundFunction(JSContext *cx, uintN argc, Value *vp)
{
    JSFunction *fun = vp[0].toObject().toFunction();
    JS_ASSERT(fun->isBoundFunction());

    bool constructing = IsConstructing(vp);

    /* 15.3.4.5.1 step 1, 15.3.4.5.2 step 3. */
    uintN argslen = fun->getBoundFunctionArgumentCount();

    if (argc + argslen > StackSpace::ARGS_LENGTH_MAX) {
        js_ReportAllocationOverflow(cx);
        return false;
    }

    /* 15.3.4.5.1 step 3, 15.3.4.5.2 step 1. */
    JSObject *target = fun->getBoundFunctionTarget();

    /* 15.3.4.5.1 step 2. */
    const Value &boundThis = fun->getBoundFunctionThis();

    InvokeArgsGuard args;
    if (!cx->stack.pushInvokeArgs(cx, argc + argslen, &args))
        return false;

    /* 15.3.4.5.1, 15.3.4.5.2 step 4. */
    for (uintN i = 0; i < argslen; i++)
        args[i] = fun->getBoundFunctionArgument(i);
    PodCopy(args.array() + argslen, vp + 2, argc);

    /* 15.3.4.5.1, 15.3.4.5.2 step 5. */
    args.calleev().setObject(*target);

    if (!constructing)
        args.thisv() = boundThis;

    if (constructing ? !InvokeConstructor(cx, args) : !Invoke(cx, args))
        return false;

    *vp = args.rval();
    return true;
}

}

#if JS_HAS_GENERATORS
static JSBool
fun_isGenerator(JSContext *cx, uintN argc, Value *vp)
{
    JSFunction *fun;
    if (!IsFunctionObject(vp[1], &fun)) {
        JS_SET_RVAL(cx, vp, BooleanValue(false));
        return true;
    }

    bool result = false;
    if (fun->isInterpreted()) {
        JSScript *script = fun->script();
        JS_ASSERT(script->length != 0);
        result = script->code[0] == JSOP_GENERATOR;
    }

    JS_SET_RVAL(cx, vp, BooleanValue(result));
    return true;
}
#endif

/* ES5 15.3.4.5. */
static JSBool
fun_bind(JSContext *cx, uintN argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);

    /* Step 1. */
    Value &thisv = args.thisv();

    /* Step 2. */
    if (!js_IsCallable(thisv)) {
        ReportIncompatibleMethod(cx, args, &FunctionClass);
        return false;
    }

    RootedVarObject target(cx);
    target = &thisv.toObject();

    /* Step 3. */
    Value *boundArgs = NULL;
    uintN argslen = 0;
    if (args.length() > 1) {
        boundArgs = args.array() + 1;
        argslen = args.length() - 1;
    }

    /* Steps 15-16. */
    uintN length = 0;
    if (target->isFunction()) {
        uintN nargs = target->toFunction()->nargs;
        if (nargs > argslen)
            length = nargs - argslen;
    }

    /* Step 4-6, 10-11. */
    JSAtom *name = target->isFunction() ? target->toFunction()->atom : NULL;

    JSObject *funobj =
        js_NewFunction(cx, NULL, CallOrConstructBoundFunction, length,
                       JSFUN_CONSTRUCTOR, target, name);
    if (!funobj)
        return false;

    /* NB: Bound functions abuse |parent| to store their target. */
    if (!funobj->setParent(cx, target))
        return false;

    /* Steps 7-9. */
    Value thisArg = args.length() >= 1 ? args[0] : UndefinedValue();
    if (!funobj->toFunction()->initBoundFunction(cx, thisArg, boundArgs, argslen))
        return false;

    /* Steps 17, 19-21 are handled by fun_resolve. */
    /* Step 18 is the default for new functions. */

    /* Step 22. */
    args.rval().setObject(*funobj);
    return true;
}

/*
 * Report "malformed formal parameter" iff no illegal char or similar scanner
 * error was already reported.
 */
static bool
OnBadFormal(JSContext *cx, TokenKind tt)
{
    if (tt != TOK_ERROR)
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_BAD_FORMAL);
    else
        JS_ASSERT(cx->isExceptionPending());
    return false;
}

namespace js {

JSFunctionSpec function_methods[] = {
#if JS_HAS_TOSOURCE
    JS_FN(js_toSource_str,   fun_toSource,   0,0),
#endif
    JS_FN(js_toString_str,   fun_toString,   0,0),
    JS_FN(js_apply_str,      js_fun_apply,   2,0),
    JS_FN(js_call_str,       js_fun_call,    1,0),
    JS_FN("bind",            fun_bind,       1,0),
#if JS_HAS_GENERATORS
    JS_FN("isGenerator",     fun_isGenerator,0,0),
#endif
    JS_FS_END
};

JSBool
Function(JSContext *cx, uintN argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);

    /* Block this call if security callbacks forbid it. */
    RootedVar<GlobalObject*> global(cx);
    global = &args.callee().global();
    if (!global->isRuntimeCodeGenEnabled(cx)) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_CSP_BLOCKED_FUNCTION);
        return false;
    }

    Bindings bindings(cx);

    const char *filename;
    uintN lineno;
    JSPrincipals *originPrincipals;
    CurrentScriptFileLineOrigin(cx, &filename, &lineno, &originPrincipals);
    JSPrincipals *principals = PrincipalsForCompiledCode(args, cx);

    uintN n = args.length() ? args.length() - 1 : 0;
    if (n > 0) {
        /*
         * Collect the function-argument arguments into one string, separated
         * by commas, then make a tokenstream from that string, and scan it to
         * get the arguments.  We need to throw the full scanner at the
         * problem, because the argument string can legitimately contain
         * comments and linefeeds.  XXX It might be better to concatenate
         * everything up into a function definition and pass it to the
         * compiler, but doing it this way is less of a delta from the old
         * code.  See ECMA 15.3.2.1.
         */
        size_t args_length = 0;
        for (uintN i = 0; i < n; i++) {
            /* Collect the lengths for all the function-argument arguments. */
            JSString *arg = ToString(cx, args[i]);
            if (!arg)
                return false;
            args[i].setString(arg);

            /*
             * Check for overflow.  The < test works because the maximum
             * JSString length fits in 2 fewer bits than size_t has.
             */
            size_t old_args_length = args_length;
            args_length = old_args_length + arg->length();
            if (args_length < old_args_length) {
                js_ReportAllocationOverflow(cx);
                return false;
            }
        }

        /* Add 1 for each joining comma and check for overflow (two ways). */
        size_t old_args_length = args_length;
        args_length = old_args_length + n - 1;
        if (args_length < old_args_length ||
            args_length >= ~(size_t)0 / sizeof(jschar)) {
            js_ReportAllocationOverflow(cx);
            return false;
        }

        /*
         * Allocate a string to hold the concatenated arguments, including room
         * for a terminating 0. Mark cx->tempLifeAlloc for later release, to
         * free collected_args and its tokenstream in one swoop.
         */
        LifoAllocScope las(&cx->tempLifoAlloc());
        jschar *cp = cx->tempLifoAlloc().newArray<jschar>(args_length + 1);
        if (!cp) {
            js_ReportOutOfMemory(cx);
            return false;
        }
        jschar *collected_args = cp;

        /*
         * Concatenate the arguments into the new string, separated by commas.
         */
        for (uintN i = 0; i < n; i++) {
            JSString *arg = args[i].toString();
            size_t arg_length = arg->length();
            const jschar *arg_chars = arg->getChars(cx);
            if (!arg_chars)
                return false;
            (void) js_strncpy(cp, arg_chars, arg_length);
            cp += arg_length;

            /* Add separating comma or terminating 0. */
            *cp++ = (i + 1 < n) ? ',' : 0;
        }

        /* Initialize a tokenstream that reads from the given string. */
        TokenStream ts(cx, principals, originPrincipals);
        if (!ts.init(collected_args, args_length, filename, lineno, cx->findVersion()))
            return false;

        /* The argument string may be empty or contain no tokens. */
        TokenKind tt = ts.getToken();
        if (tt != TOK_EOF) {
            for (;;) {
                /*
                 * Check that it's a name.  This also implicitly guards against
                 * TOK_ERROR, which was already reported.
                 */
                if (tt != TOK_NAME)
                    return OnBadFormal(cx, tt);

                /* Check for a duplicate parameter name. */
                PropertyName *name = ts.currentToken().name();
                if (bindings.hasBinding(cx, name)) {
                    JSAutoByteString bytes;
                    if (!js_AtomToPrintableString(cx, name, &bytes))
                        return false;
                    if (!ReportCompileErrorNumber(cx, &ts, NULL,
                                                  JSREPORT_WARNING | JSREPORT_STRICT,
                                                  JSMSG_DUPLICATE_FORMAL, bytes.ptr()))
                    {
                        return false;
                    }
                }

                uint16_t dummy;
                if (!bindings.addArgument(cx, name, &dummy))
                    return false;

                /*
                 * Get the next token.  Stop on end of stream.  Otherwise
                 * insist on a comma, get another name, and iterate.
                 */
                tt = ts.getToken();
                if (tt == TOK_EOF)
                    break;
                if (tt != TOK_COMMA)
                    return OnBadFormal(cx, tt);
                tt = ts.getToken();
            }
        }
    }

    JS::Anchor<JSString *> strAnchor(NULL);
    const jschar *chars;
    size_t length;

    if (args.length()) {
        JSString *str = ToString(cx, args[args.length() - 1]);
        if (!str)
            return false;
        strAnchor.set(str);
        chars = str->getChars(cx);
        length = str->length();
    } else {
        chars = cx->runtime->emptyString->chars();
        length = 0;
    }

    /*
     * NB: (new Function) is not lexically closed by its caller, it's just an
     * anonymous function in the top-level scope that its constructor inhabits.
     * Thus 'var x = 42; f = new Function("return x"); print(f())' prints 42,
     * and so would a call to f from another top-level's script or function.
     */
    JSFunction *fun = js_NewFunction(cx, NULL, NULL, 0, JSFUN_LAMBDA | JSFUN_INTERPRETED,
                                     global, cx->runtime->atomState.anonymousAtom);
    if (!fun)
        return false;

    bool ok = frontend::CompileFunctionBody(cx, fun, principals, originPrincipals,
                                            &bindings, chars, length, filename, lineno,
                                            cx->findVersion());
    args.rval().setObject(*fun);
    return ok;
}

bool
IsBuiltinFunctionConstructor(JSFunction *fun)
{
    return fun->maybeNative() == Function;
}

const Shape *
LookupInterpretedFunctionPrototype(JSContext *cx, JSObject *funobj)
{
#ifdef DEBUG
    JSFunction *fun = funobj->toFunction();
    JS_ASSERT(fun->isInterpreted());
    JS_ASSERT(!fun->isFunctionPrototype());
    JS_ASSERT(!funobj->isBoundFunction());
#endif

    jsid id = ATOM_TO_JSID(cx->runtime->atomState.classPrototypeAtom);
    const Shape *shape = funobj->nativeLookup(cx, id);
    if (!shape) {
        if (!ResolveInterpretedFunctionPrototype(cx, funobj))
            return NULL;
        shape = funobj->nativeLookup(cx, id);
    }
    JS_ASSERT(!shape->configurable());
    JS_ASSERT(shape->isDataDescriptor());
    JS_ASSERT(shape->hasSlot());
    JS_ASSERT(!shape->isMethod());
    return shape;
}

} /* namespace js */

JSFunction *
js_NewFunction(JSContext *cx, JSObject *funobj, Native native, uintN nargs,
               uintN flags, HandleObject parent, JSAtom *atom, js::gc::AllocKind kind)
{
    JS_ASSERT(kind == JSFunction::FinalizeKind || kind == JSFunction::ExtendedFinalizeKind);
    JS_ASSERT(sizeof(JSFunction) <= gc::Arena::thingSize(JSFunction::FinalizeKind));
    JS_ASSERT(sizeof(FunctionExtended) <= gc::Arena::thingSize(JSFunction::ExtendedFinalizeKind));

    JSFunction *fun;

    if (funobj) {
        JS_ASSERT(funobj->isFunction());
        JS_ASSERT(funobj->getParent() == parent);
    } else {
        funobj = NewObjectWithClassProto(cx, &FunctionClass, NULL, SkipScopeParent(parent), kind);
        if (!funobj)
            return NULL;
    }
    fun = static_cast<JSFunction *>(funobj);

    /* Initialize all function members. */
    fun->nargs = uint16_t(nargs);
    fun->flags = flags & (JSFUN_FLAGS_MASK | JSFUN_KINDMASK);
    if ((flags & JSFUN_KINDMASK) >= JSFUN_INTERPRETED) {
        JS_ASSERT(!native);
        fun->script().init(NULL);
        fun->initEnvironment(parent);
    } else {
        fun->u.n.clasp = NULL;
        fun->u.n.native = native;
        JS_ASSERT(fun->u.n.native);
    }
    if (kind == JSFunction::ExtendedFinalizeKind) {
        fun->flags |= JSFUN_EXTENDED;
        fun->initializeExtended();
    }
    fun->atom = atom;

    if (native && !fun->setSingletonType(cx))
        return NULL;

    return fun;
}

JSFunction * JS_FASTCALL
js_CloneFunctionObject(JSContext *cx, JSFunction *fun, JSObject *parent,
                       JSObject *proto, gc::AllocKind kind)
{
    JS_ASSERT(parent);
    JS_ASSERT(proto);

    JSObject *cloneobj = NewObjectWithClassProto(cx, &FunctionClass, NULL, SkipScopeParent(parent), kind);
    if (!cloneobj)
        return NULL;
    JSFunction *clone = static_cast<JSFunction *>(cloneobj);

    clone->nargs = fun->nargs;
    clone->flags = fun->flags & ~JSFUN_EXTENDED;
    if (fun->isInterpreted()) {
        clone->initScript(fun->script());
        clone->initEnvironment(parent);
    } else {
        clone->u.n = fun->u.n;
    }
    clone->atom = fun->atom;

    if (kind == JSFunction::ExtendedFinalizeKind) {
        clone->flags |= JSFUN_EXTENDED;
        clone->initializeExtended();
    }

    if (cx->compartment == fun->compartment()) {
        /*
         * We can use the same type as the original function provided that (a)
         * its prototype is correct, and (b) its type is not a singleton. The
         * first case will hold in all compileAndGo code, and the second case
         * will have been caught by CloneFunctionObject coming from function
         * definitions or read barriers, so will not get here.
         */
        if (fun->getProto() == proto && !fun->hasSingletonType())
            clone->setType(fun->type());
    } else {
        /*
         * Across compartments we have to clone the script for interpreted
         * functions.
         */
        if (clone->isInterpreted()) {
            JSScript *script = clone->script();
            JS_ASSERT(script);
            JS_ASSERT(script->compartment() == fun->compartment());
            JS_ASSERT(script->compartment() != cx->compartment);

            clone->script().init(NULL);
            JSScript *cscript = js_CloneScript(cx, script);
            if (!cscript)
                return NULL;

            cscript->globalObject = &clone->global();
            clone->setScript(cscript);
            if (!cscript->typeSetFunction(cx, clone))
                return NULL;

            js_CallNewScriptHook(cx, clone->script(), clone);
            Debugger::onNewScript(cx, clone->script(), NULL);
        }
    }
    return clone;
}

/*
 * Create a new flat closure, but don't initialize the imported upvar
 * values. The tracer calls this function and then initializes the upvar
 * slots on trace.
 */
JSFunction * JS_FASTCALL
js_AllocFlatClosure(JSContext *cx, JSFunction *fun, JSObject *scopeChain)
{
    JS_ASSERT(fun->isFlatClosure());
    JS_ASSERT(JSScript::isValidOffset(fun->script()->upvarsOffset) ==
              fun->script()->bindings.hasUpvars());
    JS_ASSERT_IF(JSScript::isValidOffset(fun->script()->upvarsOffset),
                 fun->script()->upvars()->length == fun->script()->bindings.countUpvars());

    JSFunction *closure = CloneFunctionObject(cx, fun, scopeChain, JSFunction::ExtendedFinalizeKind);
    if (!closure)
        return closure;

    uint32_t nslots = fun->script()->bindings.countUpvars();
    if (nslots == 0)
        return closure;

    HeapValue *data = (HeapValue *) cx->malloc_(nslots * sizeof(HeapValue));
    if (!data)
        return NULL;

    closure->setExtendedSlot(JSFunction::FLAT_CLOSURE_UPVARS_SLOT, PrivateValue(data));
    return closure;
}

JSFunction *
js_NewFlatClosure(JSContext *cx, JSFunction *fun)
{
    /*
     * Flat closures cannot yet be partial, that is, all upvars must be copied,
     * or the closure won't be flattened. Therefore they do not need to search
     * enclosing scope objects via JSOP_NAME, etc.
     */
    JSObject *scopeChain = &cx->fp()->scopeChain();

    JSFunction *closure = js_AllocFlatClosure(cx, fun, scopeChain);
    if (!closure || !fun->script()->bindings.hasUpvars())
        return closure;

    uintN level = fun->script()->staticLevel;
    JSUpvarArray *uva = fun->script()->upvars();

    for (uint32_t i = 0, n = uva->length; i < n; i++)
        closure->initFlatClosureUpvar(i, GetUpvar(cx, level, uva->vector[i]));

    return closure;
}

JSFunction *
js_DefineFunction(JSContext *cx, HandleObject obj, jsid id, Native native,
                  uintN nargs, uintN attrs, AllocKind kind)
{
    RootId idRoot(cx, &id);

    PropertyOp gop;
    StrictPropertyOp sop;

    RootedVarFunction fun(cx);

    if (attrs & JSFUN_STUB_GSOPS) {
        /*
         * JSFUN_STUB_GSOPS is a request flag only, not stored in fun->flags or
         * the defined property's attributes. This allows us to encode another,
         * internal flag using the same bit, JSFUN_EXPR_CLOSURE -- see jsfun.h
         * for more on this.
         */
        attrs &= ~JSFUN_STUB_GSOPS;
        gop = JS_PropertyStub;
        sop = JS_StrictPropertyStub;
    } else {
        gop = NULL;
        sop = NULL;
    }

    fun = js_NewFunction(cx, NULL, native, nargs,
                         attrs & (JSFUN_FLAGS_MASK),
                         obj,
                         JSID_IS_ATOM(id) ? JSID_TO_ATOM(id) : NULL,
                         kind);
    if (!fun)
        return NULL;

    if (!obj->defineGeneric(cx, id, ObjectValue(*fun), gop, sop, attrs & ~JSFUN_FLAGS_MASK))
        return NULL;

    return fun;
}

JS_STATIC_ASSERT((JSV2F_CONSTRUCT & JSV2F_SEARCH_STACK) == 0);

JSFunction *
js_ValueToFunction(JSContext *cx, const Value *vp, uintN flags)
{
    JSFunction *fun;
    if (!IsFunctionObject(*vp, &fun)) {
        js_ReportIsNotFunction(cx, vp, flags);
        return NULL;
    }
    return fun;
}

JSObject *
js_ValueToCallableObject(JSContext *cx, Value *vp, uintN flags)
{
    if (vp->isObject()) {
        JSObject *callable = &vp->toObject();
        if (callable->isCallable())
            return callable;
    }

    js_ReportIsNotFunction(cx, vp, flags);
    return NULL;
}

void
js_ReportIsNotFunction(JSContext *cx, const Value *vp, uintN flags)
{
    const char *name = NULL, *source = NULL;
    AutoValueRooter tvr(cx);
    uintN error = (flags & JSV2F_CONSTRUCT) ? JSMSG_NOT_CONSTRUCTOR : JSMSG_NOT_FUNCTION;

    /*
     * We try to the print the code that produced vp if vp is a value in the
     * most recent interpreted stack frame. Note that additional values, not
     * directly produced by the script, may have been pushed onto the frame's
     * expression stack (e.g. by pushInvokeArgs) thereby incrementing sp past
     * the depth simulated by ReconstructPCStack.
     *
     * Conversely, values may have been popped from the stack in preparation
     * for a call (e.g., by SplatApplyArgs). Since we must pass an offset from
     * the top of the simulated stack to js_ReportValueError3, we do bounds
     * checking using the minimum of both the simulated and actual stack depth.
     */
    ptrdiff_t spindex = 0;

    FrameRegsIter i(cx);
    if (!i.done()) {
        uintN depth = js_ReconstructStackDepth(cx, i.fp()->script(), i.pc());
        Value *simsp = i.fp()->base() + depth;
        if (i.fp()->base() <= vp && vp < Min(simsp, i.sp()))
            spindex = vp - simsp;
    }

    if (!spindex)
        spindex = ((flags & JSV2F_SEARCH_STACK) ? JSDVG_SEARCH_STACK : JSDVG_IGNORE_STACK);

    js_ReportValueError3(cx, error, spindex, *vp, NULL, name, source);
}
