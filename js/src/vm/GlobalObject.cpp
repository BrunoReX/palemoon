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
 * The Original Code is SpiderMonkey global object code.
 *
 * The Initial Developer of the Original Code is
 * the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2011
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

#include "GlobalObject.h"

#include "jscntxt.h"
#include "jsexn.h"
#include "jsmath.h"
#include "json.h"

#include "builtin/RegExp.h"
#include "frontend/BytecodeEmitter.h"
#include "vm/GlobalObject-inl.h"

#include "jsobjinlines.h"
#include "vm/RegExpObject-inl.h"
#include "vm/RegExpStatics-inl.h"

using namespace js;

JSObject *
js_InitObjectClass(JSContext *cx, JSObject *obj)
{
    JS_ASSERT(obj->isNative());

    return obj->asGlobal()->getOrCreateObjectPrototype(cx);
}

JSObject *
js_InitFunctionClass(JSContext *cx, JSObject *obj)
{
    JS_ASSERT(obj->isNative());

    return obj->asGlobal()->getOrCreateFunctionPrototype(cx);
}

static JSBool
ThrowTypeError(JSContext *cx, uintN argc, Value *vp)
{
    JS_ReportErrorFlagsAndNumber(cx, JSREPORT_ERROR, js_GetErrorMessage, NULL,
                                 JSMSG_THROW_TYPE_ERROR);
    return false;
}

namespace js {

JSObject *
GlobalObject::initFunctionAndObjectClasses(JSContext *cx)
{
    JS_THREADSAFE_ASSERT(cx->compartment != cx->runtime->atomsCompartment);
    JS_ASSERT(isNative());

    /*
     * Calling a function from a cleared global triggers this (yeah, I know).
     * Uncomment this once bug 470510 is fixed (if that bug doesn't remove
     * isCleared entirely).
     */
    // JS_ASSERT(!isCleared());

    /* If cx has no global object, make this the global object. */
    if (!cx->globalObject)
        JS_SetGlobalObject(cx, this);

    /*
     * Create |Object.prototype| first, mirroring CreateBlankProto but for the
     * prototype of the created object.
     */
    JSObject *objectProto = NewObjectWithGivenProto(cx, &ObjectClass, NULL, this);
    if (!objectProto || !objectProto->setSingletonType(cx))
        return NULL;

    /*
     * The default 'new' type of Object.prototype is required by type inference
     * to have unknown properties, to simplify handling of e.g. heterogenous
     * objects in JSON and script literals.
     */
    if (!objectProto->setNewTypeUnknown(cx))
        return NULL;

    /* Create |Function.prototype| next so we can create other functions. */
    JSFunction *functionProto;
    {
        JSObject *proto = NewObjectWithGivenProto(cx, &FunctionClass, objectProto, this);
        if (!proto)
            return NULL;

        /*
         * Bizarrely, |Function.prototype| must be an interpreted function, so
         * give it the guts to be one.
         */
        functionProto = js_NewFunction(cx, proto, NULL, 0, JSFUN_INTERPRETED, this, NULL);
        if (!functionProto)
            return NULL;
        JS_ASSERT(proto == functionProto);
        functionProto->flags |= JSFUN_PROTOTYPE;

        JSScript *script =
            JSScript::NewScript(cx, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, JSVERSION_DEFAULT);
        if (!script)
            return NULL;
        script->noScriptRval = true;
        script->code[0] = JSOP_STOP;
        script->code[1] = SRC_NULL;
        functionProto->initScript(script);
        functionProto->getType(cx)->interpretedFunction = functionProto;
        script->setFunction(functionProto);

        if (!proto->setSingletonType(cx))
            return NULL;

        /*
         * The default 'new' type of Function.prototype is required by type
         * inference to have unknown properties, to simplify handling of e.g.
         * CloneFunctionObject.
         */
        if (!proto->setNewTypeUnknown(cx))
            return NULL;
    }

    /* Create the Object function now that we have a [[Prototype]] for it. */
    jsid objectId = ATOM_TO_JSID(CLASS_ATOM(cx, Object));
    JSFunction *objectCtor;
    {
        JSObject *ctor = NewObjectWithGivenProto(cx, &FunctionClass, functionProto, this);
        if (!ctor)
            return NULL;
        objectCtor = js_NewFunction(cx, ctor, js_Object, 1, JSFUN_CONSTRUCTOR, this,
                                    JSID_TO_ATOM(objectId));
        if (!objectCtor)
            return NULL;
        JS_ASSERT(ctor == objectCtor);

        objectCtor->setConstructorClass(&ObjectClass);
    }

    /*
     * Install |Object| and |Object.prototype| for the benefit of subsequent
     * code that looks for them.
     */
    setObjectClassDetails(objectCtor, objectProto);

    /* Create |Function| so it and |Function.prototype| can be installed. */
    jsid functionId = ATOM_TO_JSID(CLASS_ATOM(cx, Function));
    JSFunction *functionCtor;
    {
        JSObject *ctor =
            NewObjectWithGivenProto(cx, &FunctionClass, functionProto, this);
        if (!ctor)
            return NULL;
        functionCtor = js_NewFunction(cx, ctor, Function, 1, JSFUN_CONSTRUCTOR, this,
                                      JSID_TO_ATOM(functionId));
        if (!functionCtor)
            return NULL;
        JS_ASSERT(ctor == functionCtor);

        functionCtor->setConstructorClass(&FunctionClass);
    }

    /*
     * Install |Function| and |Function.prototype| so that we can freely create
     * functions and objects without special effort.
     */
    setFunctionClassDetails(functionCtor, functionProto);

    /*
     * The hard part's done: now go back and add all the properties these
     * primordial values have.
     */
    if (!LinkConstructorAndPrototype(cx, objectCtor, objectProto) ||
        !DefinePropertiesAndBrand(cx, objectProto, object_props, object_methods) ||
        !DefinePropertiesAndBrand(cx, objectCtor, NULL, object_static_methods) ||
        !LinkConstructorAndPrototype(cx, functionCtor, functionProto) ||
        !DefinePropertiesAndBrand(cx, functionProto, NULL, function_methods) ||
        !DefinePropertiesAndBrand(cx, functionCtor, NULL, NULL))
    {
        return NULL;
    }

    /* Add the global Function and Object properties now. */
    if (!addDataProperty(cx, objectId, JSProto_Object + JSProto_LIMIT * 2, 0))
        return NULL;
    if (!addDataProperty(cx, functionId, JSProto_Function + JSProto_LIMIT * 2, 0))
        return NULL;

    /* Heavy lifting done, but lingering tasks remain. */

    /* ES5 15.1.2.1. */
    jsid id = ATOM_TO_JSID(cx->runtime->atomState.evalAtom);
    JSObject *evalobj = js_DefineFunction(cx, this, id, eval, 1, JSFUN_STUB_GSOPS);
    if (!evalobj)
        return NULL;
    setOriginalEval(evalobj);

    /* ES5 13.2.3: Construct the unique [[ThrowTypeError]] function object. */
    JSFunction *throwTypeError = js_NewFunction(cx, NULL, ThrowTypeError, 0, 0, this, NULL);
    if (!throwTypeError)
        return NULL;
    AutoIdVector ids(cx);
    if (!throwTypeError->preventExtensions(cx, &ids))
        return NULL;
    setThrowTypeError(throwTypeError);

    /*
     * The global object should have |Object.prototype| as its [[Prototype]].
     * Eventually we'd like to have standard classes be there from the start,
     * and thus we would know we were always setting what had previously been a
     * null [[Prototype]], but right now some code assumes it can set the
     * [[Prototype]] before standard classes have been initialized.  For now,
     * only set the [[Prototype]] if it hasn't already been set.
     */
    if (shouldSplicePrototype(cx) && !splicePrototype(cx, objectProto))
        return NULL;

    /*
     * Notify any debuggers about the creation of the script for
     * |Function.prototype| -- after all initialization, for simplicity.
     */
    js_CallNewScriptHook(cx, functionProto->script(), functionProto);
    return functionProto;
}

GlobalObject *
GlobalObject::create(JSContext *cx, Class *clasp)
{
    JS_ASSERT(clasp->flags & JSCLASS_IS_GLOBAL);

    JSObject *obj = NewObjectWithGivenProto(cx, clasp, NULL, NULL);
    if (!obj || !obj->setSingletonType(cx))
        return NULL;

    GlobalObject *globalObj = obj->asGlobal();
    if (!globalObj->setVarObj(cx))
        return NULL;

    /* Construct a regexp statics object for this global object. */
    JSObject *res = RegExpStatics::create(cx, globalObj);
    if (!res)
        return NULL;
    globalObj->initSlot(REGEXP_STATICS, ObjectValue(*res));
    globalObj->initFlags(0);

    return globalObj;
}

bool
GlobalObject::initStandardClasses(JSContext *cx)
{
    JSAtomState &state = cx->runtime->atomState;

    /* Define a top-level property 'undefined' with the undefined value. */
    if (!defineProperty(cx, state.typeAtoms[JSTYPE_VOID], UndefinedValue(),
                        JS_PropertyStub, JS_StrictPropertyStub, JSPROP_PERMANENT | JSPROP_READONLY))
    {
        return false;
    }

    if (!initFunctionAndObjectClasses(cx))
        return false;

    /* Initialize the rest of the standard objects and functions. */
    return js_InitArrayClass(cx, this) &&
           js_InitBooleanClass(cx, this) &&
           js_InitExceptionClasses(cx, this) &&
           js_InitMathClass(cx, this) &&
           js_InitNumberClass(cx, this) &&
           js_InitJSONClass(cx, this) &&
           js_InitRegExpClass(cx, this) &&
           js_InitStringClass(cx, this) &&
           js_InitTypedArrayClasses(cx, this) &&
#if JS_HAS_XML_SUPPORT
           js_InitXMLClasses(cx, this) &&
#endif
#if JS_HAS_GENERATORS
           js_InitIteratorClasses(cx, this) &&
#endif
           js_InitDateClass(cx, this) &&
           js_InitProxyClass(cx, this);
}

void
GlobalObject::clear(JSContext *cx)
{
    for (int key = JSProto_Null; key < JSProto_LIMIT * 3; key++)
        setSlot(key, UndefinedValue());

    /* Clear regexp statics. */
    getRegExpStatics()->clear();

    /* Clear the runtime-codegen-enabled cache. */
    setSlot(RUNTIME_CODEGEN_ENABLED, UndefinedValue());

    /*
     * Clear the original-eval and [[ThrowTypeError]] slots, in case throwing
     * trying to execute a script for this global must reinitialize standard
     * classes.  See bug 470150.
     */
    setSlot(EVAL, UndefinedValue());
    setSlot(THROWTYPEERROR, UndefinedValue());

    /*
     * Mark global as cleared. If we try to execute any compile-and-go
     * scripts from here on, we will throw.
     */
    int32_t flags = getSlot(FLAGS).toInt32();
    flags |= FLAGS_CLEARED;
    setSlot(FLAGS, Int32Value(flags));

    /*
     * Reset the new object cache in the compartment, which assumes that
     * prototypes cached on the global object are immutable.
     */
    cx->compartment->newObjectCache.reset();
}

bool
GlobalObject::isRuntimeCodeGenEnabled(JSContext *cx)
{
    HeapValue &v = getSlotRef(RUNTIME_CODEGEN_ENABLED);
    if (v.isUndefined()) {
        JSSecurityCallbacks *callbacks = JS_GetSecurityCallbacks(cx);

        /*
         * If there are callbacks, make sure that the CSP callback is installed
         * and that it permits runtime code generation, then cache the result.
         */
        v.set(compartment(),
              BooleanValue((!callbacks || !callbacks->contentSecurityPolicyAllows) ||
                           callbacks->contentSecurityPolicyAllows(cx)));
    }
    return !v.isFalse();
}

JSFunction *
GlobalObject::createConstructor(JSContext *cx, Native ctor, Class *clasp, JSAtom *name,
                                uintN length, gc::AllocKind kind)
{
    JSFunction *fun = js_NewFunction(cx, NULL, ctor, length, JSFUN_CONSTRUCTOR, this, name, kind);
    if (!fun)
        return NULL;

    /*
     * Remember the class this function is a constructor for so that we know to
     * create an object of this class when we call the constructor.
     */
    fun->setConstructorClass(clasp);
    return fun;
}

static JSObject *
CreateBlankProto(JSContext *cx, Class *clasp, JSObject &proto, GlobalObject &global)
{
    JS_ASSERT(clasp != &ObjectClass);
    JS_ASSERT(clasp != &FunctionClass);

    JSObject *blankProto = NewObjectWithGivenProto(cx, clasp, &proto, &global);
    if (!blankProto || !blankProto->setSingletonType(cx))
        return NULL;

    return blankProto;
}

JSObject *
GlobalObject::createBlankPrototype(JSContext *cx, Class *clasp)
{
    JSObject *objectProto = getOrCreateObjectPrototype(cx);
    if (!objectProto)
        return NULL;

    return CreateBlankProto(cx, clasp, *objectProto, *this);
}

JSObject *
GlobalObject::createBlankPrototypeInheriting(JSContext *cx, Class *clasp, JSObject &proto)
{
    return CreateBlankProto(cx, clasp, proto, *this);
}

bool
LinkConstructorAndPrototype(JSContext *cx, JSObject *ctor, JSObject *proto)
{
    return ctor->defineProperty(cx, cx->runtime->atomState.classPrototypeAtom,
                                ObjectValue(*proto), JS_PropertyStub, JS_StrictPropertyStub,
                                JSPROP_PERMANENT | JSPROP_READONLY) &&
           proto->defineProperty(cx, cx->runtime->atomState.constructorAtom,
                                 ObjectValue(*ctor), JS_PropertyStub, JS_StrictPropertyStub, 0);
}

bool
DefinePropertiesAndBrand(JSContext *cx, JSObject *obj, JSPropertySpec *ps, JSFunctionSpec *fs)
{
    if ((ps && !JS_DefineProperties(cx, obj, ps)) || (fs && !JS_DefineFunctions(cx, obj, fs)))
        return false;
    return true;
}

void
GlobalDebuggees_finalize(JSContext *cx, JSObject *obj)
{
    cx->delete_((GlobalObject::DebuggerVector *) obj->getPrivate());
}

static Class
GlobalDebuggees_class = {
    "GlobalDebuggee", JSCLASS_HAS_PRIVATE,
    JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_StrictPropertyStub,
    JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, GlobalDebuggees_finalize
};

GlobalObject::DebuggerVector *
GlobalObject::getDebuggers()
{
    Value debuggers = getReservedSlot(DEBUGGERS);
    if (debuggers.isUndefined())
        return NULL;
    JS_ASSERT(debuggers.toObject().getClass() == &GlobalDebuggees_class);
    return (DebuggerVector *) debuggers.toObject().getPrivate();
}

GlobalObject::DebuggerVector *
GlobalObject::getOrCreateDebuggers(JSContext *cx)
{
    assertSameCompartment(cx, this);
    DebuggerVector *debuggers = getDebuggers();
    if (debuggers)
        return debuggers;

    JSObject *obj = NewObjectWithGivenProto(cx, &GlobalDebuggees_class, NULL, this);
    if (!obj)
        return NULL;
    debuggers = cx->new_<DebuggerVector>();
    if (!debuggers)
        return NULL;
    obj->setPrivate(debuggers);
    setReservedSlot(DEBUGGERS, ObjectValue(*obj));
    return debuggers;
}

bool
GlobalObject::addDebugger(JSContext *cx, Debugger *dbg)
{
    DebuggerVector *debuggers = getOrCreateDebuggers(cx);
    if (!debuggers)
        return false;
#ifdef DEBUG
    for (Debugger **p = debuggers->begin(); p != debuggers->end(); p++)
        JS_ASSERT(*p != dbg);
#endif
    if (debuggers->empty() && !compartment()->addDebuggee(cx, this))
        return false;
    if (!debuggers->append(dbg)) {
        compartment()->removeDebuggee(cx, this);
        return false;
    }
    return true;
}

} // namespace js
