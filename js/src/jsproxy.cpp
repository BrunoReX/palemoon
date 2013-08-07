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
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Andreas Gal <gal@mozilla.com>
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

#include <string.h>
#include "jsapi.h"
#include "jscntxt.h"
#include "jsgc.h"
#include "jsgcmark.h"
#include "jsprvtd.h"
#include "jsnum.h"
#include "jsobj.h"
#include "jsproxy.h"
#include "jsscope.h"

#include "jsatominlines.h"
#include "jsinferinlines.h"
#include "jsobjinlines.h"

using namespace js;
using namespace js::gc;

static inline const HeapValue &
GetCall(JSObject *proxy)
{
    JS_ASSERT(IsFunctionProxy(proxy));
    return proxy->getSlotRef(JSSLOT_PROXY_CALL);
}

static inline Value
GetConstruct(JSObject *proxy)
{
    if (proxy->slotSpan() <= JSSLOT_PROXY_CONSTRUCT)
        return UndefinedValue();
    return proxy->getSlot(JSSLOT_PROXY_CONSTRUCT);
}

static inline const HeapValue &
GetFunctionProxyConstruct(JSObject *proxy)
{
    JS_ASSERT(IsFunctionProxy(proxy));
    JS_ASSERT(proxy->slotSpan() > JSSLOT_PROXY_CONSTRUCT);
    return proxy->getSlotRef(JSSLOT_PROXY_CONSTRUCT);
}

static bool
OperationInProgress(JSContext *cx, JSObject *proxy)
{
    PendingProxyOperation *op = JS_THREAD_DATA(cx)->pendingProxyOperation;
    while (op) {
        if (op->object == proxy)
            return true;
        op = op->next;
    }
    return false;
}

ProxyHandler::ProxyHandler(void *family) : mFamily(family)
{
}

ProxyHandler::~ProxyHandler()
{
}

bool
ProxyHandler::has(JSContext *cx, JSObject *proxy, jsid id, bool *bp)
{
    JS_ASSERT(OperationInProgress(cx, proxy));
    AutoPropertyDescriptorRooter desc(cx);
    if (!getPropertyDescriptor(cx, proxy, id, false, &desc))
        return false;
    *bp = !!desc.obj;
    return true;
}

bool
ProxyHandler::hasOwn(JSContext *cx, JSObject *proxy, jsid id, bool *bp)
{
    JS_ASSERT(OperationInProgress(cx, proxy));
    AutoPropertyDescriptorRooter desc(cx);
    if (!getOwnPropertyDescriptor(cx, proxy, id, false, &desc))
        return false;
    *bp = !!desc.obj;
    return true;
}

bool
ProxyHandler::get(JSContext *cx, JSObject *proxy, JSObject *receiver, jsid id, Value *vp)
{
    JS_ASSERT(OperationInProgress(cx, proxy));
    AutoPropertyDescriptorRooter desc(cx);
    if (!getPropertyDescriptor(cx, proxy, id, false, &desc))
        return false;
    if (!desc.obj) {
        vp->setUndefined();
        return true;
    }
    if (!desc.getter ||
        (!(desc.attrs & JSPROP_GETTER) && desc.getter == JS_PropertyStub)) {
        *vp = desc.value;
        return true;
    }
    if (desc.attrs & JSPROP_GETTER)
        return InvokeGetterOrSetter(cx, receiver, CastAsObjectJsval(desc.getter), 0, NULL, vp);
    if (!(desc.attrs & JSPROP_SHARED))
        *vp = desc.value;
    else
        vp->setUndefined();
    if (desc.attrs & JSPROP_SHORTID)
        id = INT_TO_JSID(desc.shortid);
    return CallJSPropertyOp(cx, desc.getter, receiver, id, vp);
}

bool
ProxyHandler::getElementIfPresent(JSContext *cx, JSObject *proxy, JSObject *receiver, uint32_t index, Value *vp, bool *present)
{
    jsid id;
    if (!IndexToId(cx, index, &id))
        return false;

    if (!has(cx, proxy, id, present))
        return false;

    if (!*present) {
        Debug_SetValueRangeToCrashOnTouch(vp, 1);
        return true;
    }

    return get(cx, proxy, receiver, id, vp);
}   

bool
ProxyHandler::set(JSContext *cx, JSObject *proxy, JSObject *receiver, jsid id, bool strict,
                  Value *vp)
{
    JS_ASSERT(OperationInProgress(cx, proxy));
    AutoPropertyDescriptorRooter desc(cx);
    if (!getOwnPropertyDescriptor(cx, proxy, id, true, &desc))
        return false;
    /* The control-flow here differs from ::get() because of the fall-through case below. */
    if (desc.obj) {
        if (desc.attrs & JSPROP_READONLY)
            return true;
        if (!desc.setter) {
            desc.setter = JS_StrictPropertyStub;
        } else if ((desc.attrs & JSPROP_SETTER) || desc.setter != JS_StrictPropertyStub) {
            if (!CallSetter(cx, receiver, id, desc.setter, desc.attrs, desc.shortid, strict, vp))
                return false;
            if (!proxy->isProxy() || GetProxyHandler(proxy) != this)
                return true;
            if (desc.attrs & JSPROP_SHARED)
                return true;
        }
        if (!desc.getter)
            desc.getter = JS_PropertyStub;
        desc.value = *vp;
        return defineProperty(cx, receiver, id, &desc);
    }
    if (!getPropertyDescriptor(cx, proxy, id, true, &desc))
        return false;
    if (desc.obj) {
        if (desc.attrs & JSPROP_READONLY)
            return true;
        if (!desc.setter) {
            desc.setter = JS_StrictPropertyStub;
        } else if ((desc.attrs & JSPROP_SETTER) || desc.setter != JS_StrictPropertyStub) {
            if (!CallSetter(cx, receiver, id, desc.setter, desc.attrs, desc.shortid, strict, vp))
                return false;
            if (!proxy->isProxy() || GetProxyHandler(proxy) != this)
                return true;
            if (desc.attrs & JSPROP_SHARED)
                return true;
        }
        if (!desc.getter)
            desc.getter = JS_PropertyStub;
        return defineProperty(cx, receiver, id, &desc);
    }

    desc.obj = receiver;
    desc.value = *vp;
    desc.attrs = JSPROP_ENUMERATE;
    desc.shortid = 0;
    desc.getter = NULL;
    desc.setter = NULL; // Pick up the class getter/setter.
    return defineProperty(cx, receiver, id, &desc);
}

bool
ProxyHandler::keys(JSContext *cx, JSObject *proxy, AutoIdVector &props)
{
    JS_ASSERT(OperationInProgress(cx, proxy));
    JS_ASSERT(props.length() == 0);

    if (!getOwnPropertyNames(cx, proxy, props))
        return false;

    /* Select only the enumerable properties through in-place iteration. */
    AutoPropertyDescriptorRooter desc(cx);
    size_t i = 0;
    for (size_t j = 0, len = props.length(); j < len; j++) {
        JS_ASSERT(i <= j);
        jsid id = props[j];
        if (!getOwnPropertyDescriptor(cx, proxy, id, false, &desc))
            return false;
        if (desc.obj && (desc.attrs & JSPROP_ENUMERATE))
            props[i++] = id;
    }

    JS_ASSERT(i <= props.length());
    props.resize(i);

    return true;
}

bool
ProxyHandler::iterate(JSContext *cx, JSObject *proxy, uintN flags, Value *vp)
{
    JS_ASSERT(OperationInProgress(cx, proxy));
    AutoIdVector props(cx);
    if ((flags & JSITER_OWNONLY)
        ? !keys(cx, proxy, props)
        : !enumerate(cx, proxy, props)) {
        return false;
    }
    return EnumeratedIdVectorToIterator(cx, proxy, flags, props, vp);
}

JSString *
ProxyHandler::obj_toString(JSContext *cx, JSObject *proxy)
{
    JS_ASSERT(proxy->isProxy());

    return JS_NewStringCopyZ(cx, IsFunctionProxy(proxy)
                                 ? "[object Function]"
                                 : "[object Object]");
}

JSString *
ProxyHandler::fun_toString(JSContext *cx, JSObject *proxy, uintN indent)
{
    JS_ASSERT(proxy->isProxy());
    Value fval = GetCall(proxy);
    if (IsFunctionProxy(proxy) &&
        (fval.isPrimitive() || !fval.toObject().isFunction())) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                             JSMSG_INCOMPATIBLE_PROTO,
                             js_Function_str, js_toString_str,
                             "object");
        return NULL;
    }
    return fun_toStringHelper(cx, &fval.toObject(), indent);
}

bool
ProxyHandler::defaultValue(JSContext *cx, JSObject *proxy, JSType hint, Value *vp)
{
    return DefaultValue(cx, proxy, hint, vp);
}

bool
ProxyHandler::call(JSContext *cx, JSObject *proxy, uintN argc, Value *vp)
{
    JS_ASSERT(OperationInProgress(cx, proxy));
    AutoValueRooter rval(cx);
    JSBool ok = Invoke(cx, vp[1], GetCall(proxy), argc, JS_ARGV(cx, vp), rval.addr());
    if (ok)
        JS_SET_RVAL(cx, vp, rval.value());
    return ok;
}

bool
ProxyHandler::construct(JSContext *cx, JSObject *proxy,
                        uintN argc, Value *argv, Value *rval)
{
    JS_ASSERT(OperationInProgress(cx, proxy));
    Value fval = GetConstruct(proxy);
    if (fval.isUndefined())
        return InvokeConstructor(cx, GetCall(proxy), argc, argv, rval);
    return Invoke(cx, UndefinedValue(), fval, argc, argv, rval);
}

bool
ProxyHandler::nativeCall(JSContext *cx, JSObject *proxy, Class *clasp, Native native, CallArgs args)
{
    JS_ASSERT(OperationInProgress(cx, proxy));
    ReportIncompatibleMethod(cx, args, clasp);
    return false;
}

bool
ProxyHandler::hasInstance(JSContext *cx, JSObject *proxy, const Value *vp, bool *bp)
{
    JS_ASSERT(OperationInProgress(cx, proxy));
    js_ReportValueError(cx, JSMSG_BAD_INSTANCEOF_RHS,
                        JSDVG_SEARCH_STACK, ObjectValue(*proxy), NULL);
    return false;
}

JSType
ProxyHandler::typeOf(JSContext *cx, JSObject *proxy)
{
    JS_ASSERT(OperationInProgress(cx, proxy));
    return IsFunctionProxy(proxy) ? JSTYPE_FUNCTION : JSTYPE_OBJECT;
}

bool
ProxyHandler::objectClassIs(JSObject *proxy, ESClassValue classValue, JSContext *cx)
{
    JS_ASSERT(OperationInProgress(cx, proxy));
    return false;
}

void
ProxyHandler::finalize(JSContext *cx, JSObject *proxy)
{
}

void
ProxyHandler::trace(JSTracer *trc, JSObject *proxy)
{
}

static bool
GetTrap(JSContext *cx, JSObject *handler, JSAtom *atom, Value *fvalp)
{
    JS_CHECK_RECURSION(cx, return false);

    return handler->getGeneric(cx, ATOM_TO_JSID(atom), fvalp);
}

static bool
GetFundamentalTrap(JSContext *cx, JSObject *handler, JSAtom *atom, Value *fvalp)
{
    if (!GetTrap(cx, handler, atom, fvalp))
        return false;

    if (!js_IsCallable(*fvalp)) {
        JSAutoByteString bytes;
        if (js_AtomToPrintableString(cx, atom, &bytes))
            JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_NOT_FUNCTION, bytes.ptr());
        return false;
    }

    return true;
}

static bool
GetDerivedTrap(JSContext *cx, JSObject *handler, JSAtom *atom, Value *fvalp)
{
    JS_ASSERT(atom == ATOM(has) ||
              atom == ATOM(hasOwn) ||
              atom == ATOM(get) ||
              atom == ATOM(set) ||
              atom == ATOM(keys) ||
              atom == ATOM(iterate));

    return GetTrap(cx, handler, atom, fvalp);
}

static bool
Trap(JSContext *cx, JSObject *handler, Value fval, uintN argc, Value* argv, Value *rval)
{
    return Invoke(cx, ObjectValue(*handler), fval, argc, argv, rval);
}

static bool
Trap1(JSContext *cx, JSObject *handler, Value fval, jsid id, Value *rval)
{
    JSString *str = ToString(cx, IdToValue(id));
    if (!str)
        return false;
    rval->setString(str);
    return Trap(cx, handler, fval, 1, rval, rval);
}

static bool
Trap2(JSContext *cx, JSObject *handler, Value fval, jsid id, Value v, Value *rval)
{
    JSString *str = ToString(cx, IdToValue(id));
    if (!str)
        return false;
    rval->setString(str);
    Value argv[2] = { *rval, v };
    return Trap(cx, handler, fval, 2, argv, rval);
}

static bool
ParsePropertyDescriptorObject(JSContext *cx, JSObject *obj, jsid id, const Value &v,
                              PropertyDescriptor *desc)
{
    AutoPropDescArrayRooter descs(cx);
    PropDesc *d = descs.append();
    if (!d || !d->initialize(cx, v))
        return false;
    desc->obj = obj;
    desc->value = d->value;
    JS_ASSERT(!(d->attrs & JSPROP_SHORTID));
    desc->attrs = d->attrs;
    desc->getter = d->getter();
    desc->setter = d->setter();
    desc->shortid = 0;
    return true;
}

static bool
IndicatePropertyNotFound(JSContext *cx, PropertyDescriptor *desc)
{
    desc->obj = NULL;
    return true;
}

static bool
ValueToBool(JSContext *cx, const Value &v, bool *bp)
{
    *bp = !!js_ValueToBoolean(v);
    return true;
}

static bool
ArrayToIdVector(JSContext *cx, const Value &array, AutoIdVector &props)
{
    JS_ASSERT(props.length() == 0);

    if (array.isPrimitive())
        return true;

    JSObject *obj = &array.toObject();
    jsuint length;
    if (!js_GetLengthProperty(cx, obj, &length))
        return false;

    for (jsuint n = 0; n < length; ++n) {
        if (!JS_CHECK_OPERATION_LIMIT(cx))
            return false;
        Value v;
        if (!obj->getElement(cx, n, &v))
            return false;
        jsid id;
        if (!ValueToId(cx, v, &id))
            return false;
        if (!props.append(js_CheckForStringIndex(id)))
            return false;
    }

    return true;
}

/* Derived class for all scripted proxy handlers. */
class ScriptedProxyHandler : public ProxyHandler {
  public:
    ScriptedProxyHandler();
    virtual ~ScriptedProxyHandler();

    /* ES5 Harmony fundamental proxy traps. */
    virtual bool getPropertyDescriptor(JSContext *cx, JSObject *proxy, jsid id, bool set,
                                       PropertyDescriptor *desc);
    virtual bool getOwnPropertyDescriptor(JSContext *cx, JSObject *proxy, jsid id, bool set,
                                          PropertyDescriptor *desc);
    virtual bool defineProperty(JSContext *cx, JSObject *proxy, jsid id,
                                PropertyDescriptor *desc);
    virtual bool getOwnPropertyNames(JSContext *cx, JSObject *proxy, AutoIdVector &props);
    virtual bool delete_(JSContext *cx, JSObject *proxy, jsid id, bool *bp);
    virtual bool enumerate(JSContext *cx, JSObject *proxy, AutoIdVector &props);
    virtual bool fix(JSContext *cx, JSObject *proxy, Value *vp);

    /* ES5 Harmony derived proxy traps. */
    virtual bool has(JSContext *cx, JSObject *proxy, jsid id, bool *bp);
    virtual bool hasOwn(JSContext *cx, JSObject *proxy, jsid id, bool *bp);
    virtual bool get(JSContext *cx, JSObject *proxy, JSObject *receiver, jsid id, Value *vp);
    virtual bool set(JSContext *cx, JSObject *proxy, JSObject *receiver, jsid id, bool strict,
                     Value *vp);
    virtual bool keys(JSContext *cx, JSObject *proxy, AutoIdVector &props);
    virtual bool iterate(JSContext *cx, JSObject *proxy, uintN flags, Value *vp);

    static ScriptedProxyHandler singleton;
};

static int sScriptedProxyHandlerFamily = 0;

ScriptedProxyHandler::ScriptedProxyHandler() : ProxyHandler(&sScriptedProxyHandlerFamily)
{
}

ScriptedProxyHandler::~ScriptedProxyHandler()
{
}

static bool
ReturnedValueMustNotBePrimitive(JSContext *cx, JSObject *proxy, JSAtom *atom, const Value &v)
{
    if (v.isPrimitive()) {
        JSAutoByteString bytes;
        if (js_AtomToPrintableString(cx, atom, &bytes)) {
            js_ReportValueError2(cx, JSMSG_BAD_TRAP_RETURN_VALUE,
                                 JSDVG_SEARCH_STACK, ObjectOrNullValue(proxy), NULL, bytes.ptr());
        }
        return false;
    }
    return true;
}

static JSObject *
GetProxyHandlerObject(JSContext *cx, JSObject *proxy)
{
    JS_ASSERT(OperationInProgress(cx, proxy));
    return GetProxyPrivate(proxy).toObjectOrNull();
}

bool
ScriptedProxyHandler::getPropertyDescriptor(JSContext *cx, JSObject *proxy, jsid id, bool set,
                                            PropertyDescriptor *desc)
{
    JSObject *handler = GetProxyHandlerObject(cx, proxy);
    AutoValueRooter tvr(cx);
    return GetFundamentalTrap(cx, handler, ATOM(getPropertyDescriptor), tvr.addr()) &&
           Trap1(cx, handler, tvr.value(), id, tvr.addr()) &&
           ((tvr.value().isUndefined() && IndicatePropertyNotFound(cx, desc)) ||
            (ReturnedValueMustNotBePrimitive(cx, proxy, ATOM(getPropertyDescriptor), tvr.value()) &&
             ParsePropertyDescriptorObject(cx, proxy, id, tvr.value(), desc)));
}

bool
ScriptedProxyHandler::getOwnPropertyDescriptor(JSContext *cx, JSObject *proxy, jsid id, bool set,
                                               PropertyDescriptor *desc)
{
    JSObject *handler = GetProxyHandlerObject(cx, proxy);
    AutoValueRooter tvr(cx);
    return GetFundamentalTrap(cx, handler, ATOM(getOwnPropertyDescriptor), tvr.addr()) &&
           Trap1(cx, handler, tvr.value(), id, tvr.addr()) &&
           ((tvr.value().isUndefined() && IndicatePropertyNotFound(cx, desc)) ||
            (ReturnedValueMustNotBePrimitive(cx, proxy, ATOM(getPropertyDescriptor), tvr.value()) &&
             ParsePropertyDescriptorObject(cx, proxy, id, tvr.value(), desc)));
}

bool
ScriptedProxyHandler::defineProperty(JSContext *cx, JSObject *proxy, jsid id,
                                     PropertyDescriptor *desc)
{
    JSObject *handler = GetProxyHandlerObject(cx, proxy);
    AutoValueRooter tvr(cx);
    AutoValueRooter fval(cx);
    return GetFundamentalTrap(cx, handler, ATOM(defineProperty), fval.addr()) &&
           NewPropertyDescriptorObject(cx, desc, tvr.addr()) &&
           Trap2(cx, handler, fval.value(), id, tvr.value(), tvr.addr());
}

bool
ScriptedProxyHandler::getOwnPropertyNames(JSContext *cx, JSObject *proxy, AutoIdVector &props)
{
    JSObject *handler = GetProxyHandlerObject(cx, proxy);
    AutoValueRooter tvr(cx);
    return GetFundamentalTrap(cx, handler, ATOM(getOwnPropertyNames), tvr.addr()) &&
           Trap(cx, handler, tvr.value(), 0, NULL, tvr.addr()) &&
           ArrayToIdVector(cx, tvr.value(), props);
}

bool
ScriptedProxyHandler::delete_(JSContext *cx, JSObject *proxy, jsid id, bool *bp)
{
    JSObject *handler = GetProxyHandlerObject(cx, proxy);
    AutoValueRooter tvr(cx);
    return GetFundamentalTrap(cx, handler, ATOM(delete), tvr.addr()) &&
           Trap1(cx, handler, tvr.value(), id, tvr.addr()) &&
           ValueToBool(cx, tvr.value(), bp);
}

bool
ScriptedProxyHandler::enumerate(JSContext *cx, JSObject *proxy, AutoIdVector &props)
{
    JSObject *handler = GetProxyHandlerObject(cx, proxy);
    AutoValueRooter tvr(cx);
    return GetFundamentalTrap(cx, handler, ATOM(enumerate), tvr.addr()) &&
           Trap(cx, handler, tvr.value(), 0, NULL, tvr.addr()) &&
           ArrayToIdVector(cx, tvr.value(), props);
}

bool
ScriptedProxyHandler::fix(JSContext *cx, JSObject *proxy, Value *vp)
{
    JSObject *handler = GetProxyHandlerObject(cx, proxy);
    return GetFundamentalTrap(cx, handler, ATOM(fix), vp) &&
           Trap(cx, handler, *vp, 0, NULL, vp);
}

bool
ScriptedProxyHandler::has(JSContext *cx, JSObject *proxy, jsid id, bool *bp)
{
    JSObject *handler = GetProxyHandlerObject(cx, proxy);
    AutoValueRooter tvr(cx);
    if (!GetDerivedTrap(cx, handler, ATOM(has), tvr.addr()))
        return false;
    if (!js_IsCallable(tvr.value()))
        return ProxyHandler::has(cx, proxy, id, bp);
    return Trap1(cx, handler, tvr.value(), id, tvr.addr()) &&
           ValueToBool(cx, tvr.value(), bp);
}

bool
ScriptedProxyHandler::hasOwn(JSContext *cx, JSObject *proxy, jsid id, bool *bp)
{
    JSObject *handler = GetProxyHandlerObject(cx, proxy);
    AutoValueRooter tvr(cx);
    if (!GetDerivedTrap(cx, handler, ATOM(hasOwn), tvr.addr()))
        return false;
    if (!js_IsCallable(tvr.value()))
        return ProxyHandler::hasOwn(cx, proxy, id, bp);
    return Trap1(cx, handler, tvr.value(), id, tvr.addr()) &&
           ValueToBool(cx, tvr.value(), bp);
}

bool
ScriptedProxyHandler::get(JSContext *cx, JSObject *proxy, JSObject *receiver, jsid id, Value *vp)
{
    JSObject *handler = GetProxyHandlerObject(cx, proxy);
    JSString *str = ToString(cx, IdToValue(id));
    if (!str)
        return false;
    AutoValueRooter tvr(cx, StringValue(str));
    Value argv[] = { ObjectOrNullValue(receiver), tvr.value() };
    AutoValueRooter fval(cx);
    if (!GetDerivedTrap(cx, handler, ATOM(get), fval.addr()))
        return false;
    if (!js_IsCallable(fval.value()))
        return ProxyHandler::get(cx, proxy, receiver, id, vp);
    return Trap(cx, handler, fval.value(), 2, argv, vp);
}

bool
ScriptedProxyHandler::set(JSContext *cx, JSObject *proxy, JSObject *receiver, jsid id, bool strict,
                          Value *vp)
{
    JSObject *handler = GetProxyHandlerObject(cx, proxy);
    JSString *str = ToString(cx, IdToValue(id));
    if (!str)
        return false;
    AutoValueRooter tvr(cx, StringValue(str));
    Value argv[] = { ObjectOrNullValue(receiver), tvr.value(), *vp };
    AutoValueRooter fval(cx);
    if (!GetDerivedTrap(cx, handler, ATOM(set), fval.addr()))
        return false;
    if (!js_IsCallable(fval.value()))
        return ProxyHandler::set(cx, proxy, receiver, id, strict, vp);
    return Trap(cx, handler, fval.value(), 3, argv, tvr.addr());
}

bool
ScriptedProxyHandler::keys(JSContext *cx, JSObject *proxy, AutoIdVector &props)
{
    JSObject *handler = GetProxyHandlerObject(cx, proxy);
    AutoValueRooter tvr(cx);
    if (!GetDerivedTrap(cx, handler, ATOM(keys), tvr.addr()))
        return false;
    if (!js_IsCallable(tvr.value()))
        return ProxyHandler::keys(cx, proxy, props);
    return Trap(cx, handler, tvr.value(), 0, NULL, tvr.addr()) &&
           ArrayToIdVector(cx, tvr.value(), props);
}

bool
ScriptedProxyHandler::iterate(JSContext *cx, JSObject *proxy, uintN flags, Value *vp)
{
    JSObject *handler = GetProxyHandlerObject(cx, proxy);
    AutoValueRooter tvr(cx);
    if (!GetDerivedTrap(cx, handler, ATOM(iterate), tvr.addr()))
        return false;
    if (!js_IsCallable(tvr.value()))
        return ProxyHandler::iterate(cx, proxy, flags, vp);
    return Trap(cx, handler, tvr.value(), 0, NULL, vp) &&
           ReturnedValueMustNotBePrimitive(cx, proxy, ATOM(iterate), *vp);
}

ScriptedProxyHandler ScriptedProxyHandler::singleton;

class AutoPendingProxyOperation {
    ThreadData              *data;
    PendingProxyOperation   op;
  public:
    AutoPendingProxyOperation(JSContext *cx, JSObject *proxy) : data(JS_THREAD_DATA(cx)) {
        op.next = data->pendingProxyOperation;
        op.object = proxy;
        data->pendingProxyOperation = &op;
    }

    ~AutoPendingProxyOperation() {
        JS_ASSERT(data->pendingProxyOperation == &op);
        data->pendingProxyOperation = op.next;
    }
};

bool
Proxy::getPropertyDescriptor(JSContext *cx, JSObject *proxy, jsid id, bool set,
                             PropertyDescriptor *desc)
{
    JS_CHECK_RECURSION(cx, return false);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->getPropertyDescriptor(cx, proxy, id, set, desc);
}

bool
Proxy::getPropertyDescriptor(JSContext *cx, JSObject *proxy, jsid id, bool set, Value *vp)
{
    JS_CHECK_RECURSION(cx, return false);
    AutoPendingProxyOperation pending(cx, proxy);
    AutoPropertyDescriptorRooter desc(cx);
    return Proxy::getPropertyDescriptor(cx, proxy, id, set, &desc) &&
           NewPropertyDescriptorObject(cx, &desc, vp);
}

bool
Proxy::getOwnPropertyDescriptor(JSContext *cx, JSObject *proxy, jsid id, bool set,
                                PropertyDescriptor *desc)
{
    JS_CHECK_RECURSION(cx, return false);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->getOwnPropertyDescriptor(cx, proxy, id, set, desc);
}

bool
Proxy::getOwnPropertyDescriptor(JSContext *cx, JSObject *proxy, jsid id, bool set, Value *vp)
{
    JS_CHECK_RECURSION(cx, return false);
    AutoPendingProxyOperation pending(cx, proxy);
    AutoPropertyDescriptorRooter desc(cx);
    return Proxy::getOwnPropertyDescriptor(cx, proxy, id, set, &desc) &&
           NewPropertyDescriptorObject(cx, &desc, vp);
}

bool
Proxy::defineProperty(JSContext *cx, JSObject *proxy, jsid id, PropertyDescriptor *desc)
{
    JS_CHECK_RECURSION(cx, return false);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->defineProperty(cx, proxy, id, desc);
}

bool
Proxy::defineProperty(JSContext *cx, JSObject *proxy, jsid id, const Value &v)
{
    JS_CHECK_RECURSION(cx, return false);
    AutoPendingProxyOperation pending(cx, proxy);
    AutoPropertyDescriptorRooter desc(cx);
    return ParsePropertyDescriptorObject(cx, proxy, id, v, &desc) &&
           Proxy::defineProperty(cx, proxy, id, &desc);
}

bool
Proxy::getOwnPropertyNames(JSContext *cx, JSObject *proxy, AutoIdVector &props)
{
    JS_CHECK_RECURSION(cx, return false);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->getOwnPropertyNames(cx, proxy, props);
}

bool
Proxy::delete_(JSContext *cx, JSObject *proxy, jsid id, bool *bp)
{
    JS_CHECK_RECURSION(cx, return false);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->delete_(cx, proxy, id, bp);
}

bool
Proxy::enumerate(JSContext *cx, JSObject *proxy, AutoIdVector &props)
{
    JS_CHECK_RECURSION(cx, return false);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->enumerate(cx, proxy, props);
}

bool
Proxy::fix(JSContext *cx, JSObject *proxy, Value *vp)
{
    JS_CHECK_RECURSION(cx, return false);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->fix(cx, proxy, vp);
}

bool
Proxy::has(JSContext *cx, JSObject *proxy, jsid id, bool *bp)
{
    JS_CHECK_RECURSION(cx, return false);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->has(cx, proxy, id, bp);
}

bool
Proxy::hasOwn(JSContext *cx, JSObject *proxy, jsid id, bool *bp)
{
    JS_CHECK_RECURSION(cx, return false);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->hasOwn(cx, proxy, id, bp);
}

bool
Proxy::get(JSContext *cx, JSObject *proxy, JSObject *receiver, jsid id, Value *vp)
{
    JS_CHECK_RECURSION(cx, return false);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->get(cx, proxy, receiver, id, vp);
}

bool
Proxy::getElementIfPresent(JSContext *cx, JSObject *proxy, JSObject *receiver, uint32_t index,
                           Value *vp, bool *present)
{
    JS_CHECK_RECURSION(cx, return false);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->getElementIfPresent(cx, proxy, receiver, index, vp, present);
}

bool
Proxy::set(JSContext *cx, JSObject *proxy, JSObject *receiver, jsid id, bool strict, Value *vp)
{
    JS_CHECK_RECURSION(cx, return false);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->set(cx, proxy, receiver, id, strict, vp);
}

bool
Proxy::keys(JSContext *cx, JSObject *proxy, AutoIdVector &props)
{
    JS_CHECK_RECURSION(cx, return false);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->keys(cx, proxy, props);
}

bool
Proxy::iterate(JSContext *cx, JSObject *proxy, uintN flags, Value *vp)
{
    JS_CHECK_RECURSION(cx, return false);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->iterate(cx, proxy, flags, vp);
}

bool
Proxy::call(JSContext *cx, JSObject *proxy, uintN argc, Value *vp)
{
    JS_CHECK_RECURSION(cx, return false);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->call(cx, proxy, argc, vp);
}

bool
Proxy::construct(JSContext *cx, JSObject *proxy, uintN argc, Value *argv, Value *rval)
{
    JS_CHECK_RECURSION(cx, return false);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->construct(cx, proxy, argc, argv, rval);
}

bool
Proxy::nativeCall(JSContext *cx, JSObject *proxy, Class *clasp, Native native, CallArgs args)
{
    JS_CHECK_RECURSION(cx, return false);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->nativeCall(cx, proxy, clasp, native, args);
}

bool
Proxy::hasInstance(JSContext *cx, JSObject *proxy, const js::Value *vp, bool *bp)
{
    JS_CHECK_RECURSION(cx, return false);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->hasInstance(cx, proxy, vp, bp);
}

JSType
Proxy::typeOf(JSContext *cx, JSObject *proxy)
{
    // FIXME: API doesn't allow us to report error (bug 618906).
    JS_CHECK_RECURSION(cx, return JSTYPE_OBJECT);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->typeOf(cx, proxy);
}

bool
Proxy::objectClassIs(JSObject *proxy, ESClassValue classValue, JSContext *cx)
{
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->objectClassIs(proxy, classValue, cx);
}

JSString *
Proxy::obj_toString(JSContext *cx, JSObject *proxy)
{
    JS_CHECK_RECURSION(cx, return NULL);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->obj_toString(cx, proxy);
}

JSString *
Proxy::fun_toString(JSContext *cx, JSObject *proxy, uintN indent)
{
    JS_CHECK_RECURSION(cx, return NULL);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->fun_toString(cx, proxy, indent);
}

bool
Proxy::defaultValue(JSContext *cx, JSObject *proxy, JSType hint, Value *vp)
{
    JS_CHECK_RECURSION(cx, return NULL);
    AutoPendingProxyOperation pending(cx, proxy);
    return GetProxyHandler(proxy)->defaultValue(cx, proxy, hint, vp);
}

static JSObject *
proxy_innerObject(JSContext *cx, JSObject *obj)
{
    return GetProxyPrivate(obj).toObjectOrNull();
}

static JSBool
proxy_LookupGeneric(JSContext *cx, JSObject *obj, jsid id, JSObject **objp,
                    JSProperty **propp)
{
    id = js_CheckForStringIndex(id);

    bool found;
    if (!Proxy::has(cx, obj, id, &found))
        return false;

    if (found) {
        *propp = (JSProperty *)0x1;
        *objp = obj;
    } else {
        *objp = NULL;
        *propp = NULL;
    }
    return true;
}

static JSBool
proxy_LookupProperty(JSContext *cx, JSObject *obj, PropertyName *name, JSObject **objp,
                     JSProperty **propp)
{
    return proxy_LookupGeneric(cx, obj, ATOM_TO_JSID(name), objp, propp);
}

static JSBool
proxy_LookupElement(JSContext *cx, JSObject *obj, uint32_t index, JSObject **objp,
                    JSProperty **propp)
{
    jsid id;
    if (!IndexToId(cx, index, &id))
        return false;
    return proxy_LookupGeneric(cx, obj, id, objp, propp);
}

static JSBool
proxy_LookupSpecial(JSContext *cx, JSObject *obj, SpecialId sid, JSObject **objp, JSProperty **propp)
{
    return proxy_LookupGeneric(cx, obj, SPECIALID_TO_JSID(sid), objp, propp);
}

static JSBool
proxy_DefineGeneric(JSContext *cx, JSObject *obj, jsid id, const Value *value,
                    PropertyOp getter, StrictPropertyOp setter, uintN attrs)
{
    id = js_CheckForStringIndex(id);

    AutoPropertyDescriptorRooter desc(cx);
    desc.obj = obj;
    desc.value = *value;
    desc.attrs = (attrs & (~JSPROP_SHORTID));
    desc.getter = getter;
    desc.setter = setter;
    desc.shortid = 0;
    return Proxy::defineProperty(cx, obj, id, &desc);
}

static JSBool
proxy_DefineProperty(JSContext *cx, JSObject *obj, PropertyName *name, const Value *value,
                     PropertyOp getter, StrictPropertyOp setter, uintN attrs)
{
    return proxy_DefineGeneric(cx, obj, ATOM_TO_JSID(name), value, getter, setter, attrs);
}

static JSBool
proxy_DefineElement(JSContext *cx, JSObject *obj, uint32_t index, const Value *value,
                    PropertyOp getter, StrictPropertyOp setter, uintN attrs)
{
    jsid id;
    if (!IndexToId(cx, index, &id))
        return false;
    return proxy_DefineGeneric(cx, obj, id, value, getter, setter, attrs);
}

static JSBool
proxy_DefineSpecial(JSContext *cx, JSObject *obj, SpecialId sid, const Value *value,
                    PropertyOp getter, StrictPropertyOp setter, uintN attrs)
{
    return proxy_DefineGeneric(cx, obj, SPECIALID_TO_JSID(sid), value, getter, setter, attrs);
}

static JSBool
proxy_GetGeneric(JSContext *cx, JSObject *obj, JSObject *receiver, jsid id, Value *vp)
{
    id = js_CheckForStringIndex(id);

    return Proxy::get(cx, obj, receiver, id, vp);
}

static JSBool
proxy_GetProperty(JSContext *cx, JSObject *obj, JSObject *receiver, PropertyName *name, Value *vp)
{
    return proxy_GetGeneric(cx, obj, receiver, ATOM_TO_JSID(name), vp);
}

static JSBool
proxy_GetElement(JSContext *cx, JSObject *obj, JSObject *receiver, uint32_t index, Value *vp)
{
    jsid id;
    if (!IndexToId(cx, index, &id))
        return false;
    return proxy_GetGeneric(cx, obj, receiver, id, vp);
}

static JSBool
proxy_GetElementIfPresent(JSContext *cx, JSObject *obj, JSObject *receiver, uint32_t index,
                          Value *vp, bool *present)
{
    return Proxy::getElementIfPresent(cx, obj, receiver, index, vp, present);
}

static JSBool
proxy_GetSpecial(JSContext *cx, JSObject *obj, JSObject *receiver, SpecialId sid, Value *vp)
{
    return proxy_GetGeneric(cx, obj, receiver, SPECIALID_TO_JSID(sid), vp);
}

static JSBool
proxy_SetGeneric(JSContext *cx, JSObject *obj, jsid id, Value *vp, JSBool strict)
{
    id = js_CheckForStringIndex(id);

    return Proxy::set(cx, obj, obj, id, strict, vp);
}

static JSBool
proxy_SetProperty(JSContext *cx, JSObject *obj, PropertyName *name, Value *vp, JSBool strict)
{
    return proxy_SetGeneric(cx, obj, ATOM_TO_JSID(name), vp, strict);
}

static JSBool
proxy_SetElement(JSContext *cx, JSObject *obj, uint32_t index, Value *vp, JSBool strict)
{
    jsid id;
    if (!IndexToId(cx, index, &id))
        return false;
    return proxy_SetGeneric(cx, obj, id, vp, strict);
}

static JSBool
proxy_SetSpecial(JSContext *cx, JSObject *obj, SpecialId sid, Value *vp, JSBool strict)
{
    return proxy_SetGeneric(cx, obj, SPECIALID_TO_JSID(sid), vp, strict);
}

static JSBool
proxy_GetGenericAttributes(JSContext *cx, JSObject *obj, jsid id, uintN *attrsp)
{
    id = js_CheckForStringIndex(id);

    AutoPropertyDescriptorRooter desc(cx);
    if (!Proxy::getOwnPropertyDescriptor(cx, obj, id, false, &desc))
        return false;
    *attrsp = desc.attrs;
    return true;
}

static JSBool
proxy_GetPropertyAttributes(JSContext *cx, JSObject *obj, PropertyName *name, uintN *attrsp)
{
    return proxy_GetGenericAttributes(cx, obj, ATOM_TO_JSID(name), attrsp);
}

static JSBool
proxy_GetElementAttributes(JSContext *cx, JSObject *obj, uint32_t index, uintN *attrsp)
{
    jsid id;
    if (!IndexToId(cx, index, &id))
        return false;
    return proxy_GetGenericAttributes(cx, obj, id, attrsp);
}

static JSBool
proxy_GetSpecialAttributes(JSContext *cx, JSObject *obj, SpecialId sid, uintN *attrsp)
{
    return proxy_GetGenericAttributes(cx, obj, SPECIALID_TO_JSID(sid), attrsp);
}

static JSBool
proxy_SetGenericAttributes(JSContext *cx, JSObject *obj, jsid id, uintN *attrsp)
{
    id = js_CheckForStringIndex(id);

    /* Lookup the current property descriptor so we have setter/getter/value. */
    AutoPropertyDescriptorRooter desc(cx);
    if (!Proxy::getOwnPropertyDescriptor(cx, obj, id, true, &desc))
        return false;
    desc.attrs = (*attrsp & (~JSPROP_SHORTID));
    return Proxy::defineProperty(cx, obj, id, &desc);
}

static JSBool
proxy_SetPropertyAttributes(JSContext *cx, JSObject *obj, PropertyName *name, uintN *attrsp)
{
    return proxy_SetGenericAttributes(cx, obj, ATOM_TO_JSID(name), attrsp);
}

static JSBool
proxy_SetElementAttributes(JSContext *cx, JSObject *obj, uint32_t index, uintN *attrsp)
{
    jsid id;
    if (!IndexToId(cx, index, &id))
        return false;
    return proxy_SetGenericAttributes(cx, obj, id, attrsp);
}

static JSBool
proxy_SetSpecialAttributes(JSContext *cx, JSObject *obj, SpecialId sid, uintN *attrsp)
{
    return proxy_SetGenericAttributes(cx, obj, SPECIALID_TO_JSID(sid), attrsp);
}

static JSBool
proxy_DeleteGeneric(JSContext *cx, JSObject *obj, jsid id, Value *rval, JSBool strict)
{
    id = js_CheckForStringIndex(id);

    // TODO: throwing away strict
    bool deleted;
    if (!Proxy::delete_(cx, obj, id, &deleted) || !js_SuppressDeletedProperty(cx, obj, id))
        return false;
    rval->setBoolean(deleted);
    return true;
}

static JSBool
proxy_DeleteProperty(JSContext *cx, JSObject *obj, PropertyName *name, Value *rval, JSBool strict)
{
    return proxy_DeleteGeneric(cx, obj, ATOM_TO_JSID(name), rval, strict);
}

static JSBool
proxy_DeleteElement(JSContext *cx, JSObject *obj, uint32_t index, Value *rval, JSBool strict)
{
    jsid id;
    if (!IndexToId(cx, index, &id))
        return false;
    return proxy_DeleteGeneric(cx, obj, id, rval, strict);
}

static JSBool
proxy_DeleteSpecial(JSContext *cx, JSObject *obj, SpecialId sid, Value *rval, JSBool strict)
{
    return proxy_DeleteGeneric(cx, obj, SPECIALID_TO_JSID(sid), rval, strict);
}

static void
proxy_TraceObject(JSTracer *trc, JSObject *obj)
{
    GetProxyHandler(obj)->trace(trc, obj);
    MarkCrossCompartmentValue(trc, obj->getReservedSlotRef(JSSLOT_PROXY_PRIVATE), "private");
    MarkCrossCompartmentValue(trc, obj->getReservedSlotRef(JSSLOT_PROXY_EXTRA + 0), "extra0");
    MarkCrossCompartmentValue(trc, obj->getReservedSlotRef(JSSLOT_PROXY_EXTRA + 1), "extra1");
    if (IsFunctionProxy(obj)) {
        MarkCrossCompartmentValue(trc, GetCall(obj), "call");
        MarkCrossCompartmentValue(trc, GetFunctionProxyConstruct(obj), "construct");
    }
}

static void
proxy_TraceFunction(JSTracer *trc, JSObject *obj)
{
    proxy_TraceObject(trc, obj);
    MarkCrossCompartmentValue(trc, GetCall(obj), "call");
    MarkCrossCompartmentValue(trc, GetFunctionProxyConstruct(obj), "construct");
}

static JSBool
proxy_Convert(JSContext *cx, JSObject *proxy, JSType hint, Value *vp)
{
    JS_ASSERT(proxy->isProxy());
    return Proxy::defaultValue(cx, proxy, hint, vp);
}

static JSBool
proxy_Fix(JSContext *cx, JSObject *obj, bool *fixed, AutoIdVector *props)
{
    JS_ASSERT(obj->isProxy());
    JSBool isFixed;
    bool ok = FixProxy(cx, obj, &isFixed);
    if (ok) {
        *fixed = isFixed;
        return GetPropertyNames(cx, obj, JSITER_OWNONLY | JSITER_HIDDEN, props);
    }
    return false;
}

static void
proxy_Finalize(JSContext *cx, JSObject *obj)
{
    JS_ASSERT(obj->isProxy());
    if (!obj->getSlot(JSSLOT_PROXY_HANDLER).isUndefined())
        GetProxyHandler(obj)->finalize(cx, obj);
}

static JSBool
proxy_HasInstance(JSContext *cx, JSObject *proxy, const Value *v, JSBool *bp)
{
    AutoPendingProxyOperation pending(cx, proxy);
    bool b;
    if (!Proxy::hasInstance(cx, proxy, v, &b))
        return false;
    *bp = !!b;
    return true;
}

static JSType
proxy_TypeOf(JSContext *cx, JSObject *proxy)
{
    JS_ASSERT(proxy->isProxy());
    return Proxy::typeOf(cx, proxy);
}

JS_FRIEND_DATA(Class) js::ObjectProxyClass = {
    "Proxy",
    Class::NON_NATIVE | JSCLASS_HAS_RESERVED_SLOTS(4),
    JS_PropertyStub,         /* addProperty */
    JS_PropertyStub,         /* delProperty */
    JS_PropertyStub,         /* getProperty */
    JS_StrictPropertyStub,   /* setProperty */
    JS_EnumerateStub,
    JS_ResolveStub,
    proxy_Convert,
    proxy_Finalize,          /* finalize    */
    NULL,                    /* reserved0   */
    NULL,                    /* checkAccess */
    NULL,                    /* call        */
    NULL,                    /* construct   */
    NULL,                    /* xdrObject   */
    proxy_HasInstance,       /* hasInstance */
    proxy_TraceObject,       /* trace       */
    JS_NULL_CLASS_EXT,
    {
        proxy_LookupGeneric,
        proxy_LookupProperty,
        proxy_LookupElement,
        proxy_LookupSpecial,
        proxy_DefineGeneric,
        proxy_DefineProperty,
        proxy_DefineElement,
        proxy_DefineSpecial,
        proxy_GetGeneric,
        proxy_GetProperty,
        proxy_GetElement,
        proxy_GetElementIfPresent,
        proxy_GetSpecial,
        proxy_SetGeneric,
        proxy_SetProperty,
        proxy_SetElement,
        proxy_SetSpecial,
        proxy_GetGenericAttributes,
        proxy_GetPropertyAttributes,
        proxy_GetElementAttributes,
        proxy_GetSpecialAttributes,
        proxy_SetGenericAttributes,
        proxy_SetPropertyAttributes,
        proxy_SetElementAttributes,
        proxy_SetSpecialAttributes,
        proxy_DeleteGeneric,
        proxy_DeleteProperty,
        proxy_DeleteElement,
        proxy_DeleteSpecial,
        NULL,                /* enumerate       */
        proxy_TypeOf,
        proxy_Fix,           /* fix             */
        NULL,                /* thisObject      */
        NULL,                /* clear           */
    }
};

JS_FRIEND_DATA(Class) js::OuterWindowProxyClass = {
    "Proxy",
    Class::NON_NATIVE | JSCLASS_HAS_RESERVED_SLOTS(4),
    JS_PropertyStub,         /* addProperty */
    JS_PropertyStub,         /* delProperty */
    JS_PropertyStub,         /* getProperty */
    JS_StrictPropertyStub,   /* setProperty */
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub,
    proxy_Finalize,          /* finalize    */
    NULL,                    /* reserved0   */
    NULL,                    /* checkAccess */
    NULL,                    /* call        */
    NULL,                    /* construct   */
    NULL,                    /* xdrObject   */
    NULL,                    /* hasInstance */
    proxy_TraceObject,       /* trace       */
    {
        NULL,                /* equality    */
        NULL,                /* outerObject */
        proxy_innerObject,
        NULL                 /* unused */
    },
    {
        proxy_LookupGeneric,
        proxy_LookupProperty,
        proxy_LookupElement,
        proxy_LookupSpecial,
        proxy_DefineGeneric,
        proxy_DefineProperty,
        proxy_DefineElement,
        proxy_DefineSpecial,
        proxy_GetGeneric,
        proxy_GetProperty,
        proxy_GetElement,
        proxy_GetElementIfPresent,
        proxy_GetSpecial,
        proxy_SetGeneric,
        proxy_SetProperty,
        proxy_SetElement,
        proxy_SetSpecial,
        proxy_GetGenericAttributes,
        proxy_GetPropertyAttributes,
        proxy_GetElementAttributes,
        proxy_GetSpecialAttributes,
        proxy_SetGenericAttributes,
        proxy_SetPropertyAttributes,
        proxy_SetElementAttributes,
        proxy_SetSpecialAttributes,
        proxy_DeleteGeneric,
        proxy_DeleteProperty,
        proxy_DeleteElement,
        proxy_DeleteSpecial,
        NULL,                /* enumerate       */
        NULL,                /* typeof          */
        NULL,                /* fix             */
        NULL,                /* thisObject      */
        NULL,                /* clear           */
    }
};

static JSBool
proxy_Call(JSContext *cx, uintN argc, Value *vp)
{
    JSObject *proxy = &JS_CALLEE(cx, vp).toObject();
    JS_ASSERT(proxy->isProxy());
    return Proxy::call(cx, proxy, argc, vp);
}

static JSBool
proxy_Construct(JSContext *cx, uintN argc, Value *vp)
{
    JSObject *proxy = &JS_CALLEE(cx, vp).toObject();
    JS_ASSERT(proxy->isProxy());
    bool ok = Proxy::construct(cx, proxy, argc, JS_ARGV(cx, vp), vp);
    return ok;
}

JS_FRIEND_DATA(Class) js::FunctionProxyClass = {
    "Proxy",
    Class::NON_NATIVE | JSCLASS_HAS_RESERVED_SLOTS(6),
    JS_PropertyStub,         /* addProperty */
    JS_PropertyStub,         /* delProperty */
    JS_PropertyStub,         /* getProperty */
    JS_StrictPropertyStub,   /* setProperty */
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub,
    NULL,                    /* finalize */
    NULL,                    /* reserved0   */
    NULL,                    /* checkAccess */
    proxy_Call,
    proxy_Construct,
    NULL,                    /* xdrObject   */
    FunctionClass.hasInstance,
    proxy_TraceFunction,     /* trace       */
    JS_NULL_CLASS_EXT,
    {
        proxy_LookupGeneric,
        proxy_LookupProperty,
        proxy_LookupElement,
        proxy_LookupSpecial,
        proxy_DefineGeneric,
        proxy_DefineProperty,
        proxy_DefineElement,
        proxy_DefineSpecial,
        proxy_GetGeneric,
        proxy_GetProperty,
        proxy_GetElement,
        proxy_GetElementIfPresent,
        proxy_GetSpecial,
        proxy_SetGeneric,
        proxy_SetProperty,
        proxy_SetElement,
        proxy_SetSpecial,
        proxy_GetGenericAttributes,
        proxy_GetPropertyAttributes,
        proxy_GetElementAttributes,
        proxy_GetSpecialAttributes,
        proxy_SetGenericAttributes,
        proxy_SetPropertyAttributes,
        proxy_SetElementAttributes,
        proxy_SetSpecialAttributes,
        proxy_DeleteGeneric,
        proxy_DeleteProperty,
        proxy_DeleteElement,
        proxy_DeleteSpecial,
        NULL,                /* enumerate       */
        proxy_TypeOf,
        NULL,                /* fix             */
        NULL,                /* thisObject      */
        NULL,                /* clear           */
    }
};

JS_FRIEND_API(JSObject *)
js::NewProxyObject(JSContext *cx, ProxyHandler *handler, const Value &priv, JSObject *proto,
                   JSObject *parent, JSObject *call, JSObject *construct)
{
    JS_ASSERT_IF(proto, cx->compartment == proto->compartment());
    JS_ASSERT_IF(parent, cx->compartment == parent->compartment());
    bool fun = call || construct;
    Class *clasp;
    if (fun)
        clasp = &FunctionProxyClass;
    else
        clasp = handler->isOuterWindow() ? &OuterWindowProxyClass : &ObjectProxyClass;

    /*
     * Eagerly mark properties unknown for proxies, so we don't try to track
     * their properties and so that we don't need to walk the compartment if
     * their prototype changes later.
     */
    if (proto && !proto->setNewTypeUnknown(cx))
        return NULL;

    JSObject *obj = NewObjectWithGivenProto(cx, clasp, proto, parent);
    if (!obj)
        return NULL;
    obj->setSlot(JSSLOT_PROXY_HANDLER, PrivateValue(handler));
    obj->setSlot(JSSLOT_PROXY_PRIVATE, priv);
    if (fun) {
        obj->setSlot(JSSLOT_PROXY_CALL, call ? ObjectValue(*call) : UndefinedValue());
        if (construct) {
            obj->setSlot(JSSLOT_PROXY_CONSTRUCT, ObjectValue(*construct));
        }
    }

    /* Don't track types of properties of proxies. */
    MarkTypeObjectUnknownProperties(cx, obj->type());

    return obj;
}

static JSBool
proxy_create(JSContext *cx, uintN argc, Value *vp)
{
    if (argc < 1) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_MORE_ARGS_NEEDED,
                             "create", "0", "s");
        return false;
    }
    JSObject *handler = NonNullObject(cx, vp[2]);
    if (!handler)
        return false;
    JSObject *proto, *parent = NULL;
    if (argc > 1 && vp[3].isObject()) {
        proto = &vp[3].toObject();
        parent = proto->getParent();
    } else {
        JS_ASSERT(IsFunctionObject(vp[0]));
        proto = NULL;
    }
    if (!parent)
        parent = vp[0].toObject().getParent();
    JSObject *proxy = NewProxyObject(cx, &ScriptedProxyHandler::singleton, ObjectValue(*handler),
                                     proto, parent);
    if (!proxy)
        return false;

    vp->setObject(*proxy);
    return true;
}

static JSBool
proxy_createFunction(JSContext *cx, uintN argc, Value *vp)
{
    if (argc < 2) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_MORE_ARGS_NEEDED,
                             "createFunction", "1", "");
        return false;
    }
    JSObject *handler = NonNullObject(cx, vp[2]);
    if (!handler)
        return false;
    JSObject *proto, *parent;
    parent = vp[0].toObject().getParent();
    proto = parent->getGlobal()->getOrCreateFunctionPrototype(cx);
    if (!proto)
        return false;
    parent = proto->getParent();

    JSObject *call = js_ValueToCallableObject(cx, &vp[3], JSV2F_SEARCH_STACK);
    if (!call)
        return false;
    JSObject *construct = NULL;
    if (argc > 2) {
        construct = js_ValueToCallableObject(cx, &vp[4], JSV2F_SEARCH_STACK);
        if (!construct)
            return false;
    }

    JSObject *proxy = NewProxyObject(cx, &ScriptedProxyHandler::singleton,
                                     ObjectValue(*handler),
                                     proto, parent, call, construct);
    if (!proxy)
        return false;

    vp->setObject(*proxy);
    return true;
}

#ifdef DEBUG

static JSBool
proxy_isTrapping(JSContext *cx, uintN argc, Value *vp)
{
    if (argc < 1) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_MORE_ARGS_NEEDED,
                             "isTrapping", "0", "s");
        return false;
    }
    JSObject *obj = NonNullObject(cx, vp[2]);
    if (!obj)
        return false;
    vp->setBoolean(obj->isProxy());
    return true;
}

static JSBool
proxy_fix(JSContext *cx, uintN argc, Value *vp)
{
    if (argc < 1) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_MORE_ARGS_NEEDED,
                             "fix", "0", "s");
        return false;
    }
    JSObject *obj = NonNullObject(cx, vp[2]);
    if (!obj)
        return false;
    if (obj->isProxy()) {
        JSBool flag;
        if (!FixProxy(cx, obj, &flag))
            return false;
        vp->setBoolean(flag);
    } else {
        vp->setBoolean(true);
    }
    return true;
}

#endif

static JSFunctionSpec static_methods[] = {
    JS_FN("create",         proxy_create,          2, 0),
    JS_FN("createFunction", proxy_createFunction,  3, 0),
#ifdef DEBUG
    JS_FN("isTrapping",     proxy_isTrapping,      1, 0),
    JS_FN("fix",            proxy_fix,             1, 0),
#endif
    JS_FS_END
};

static const uint32_t JSSLOT_CALLABLE_CALL = 0;
static const uint32_t JSSLOT_CALLABLE_CONSTRUCT = 1;

static JSBool
callable_Call(JSContext *cx, uintN argc, Value *vp)
{
    JSObject *callable = &JS_CALLEE(cx, vp).toObject();
    JS_ASSERT(callable->getClass() == &CallableObjectClass);
    const Value &fval = callable->getSlot(JSSLOT_CALLABLE_CALL);
    const Value &thisval = vp[1];
    bool ok = Invoke(cx, thisval, fval, argc, JS_ARGV(cx, vp), vp);
    return ok;
}

JSBool
callable_Construct(JSContext *cx, uintN argc, Value *vp)
{
    JSObject *thisobj = js_CreateThis(cx, &JS_CALLEE(cx, vp).toObject());
    if (!thisobj)
        return false;

    JSObject *callable = &vp[0].toObject();
    JS_ASSERT(callable->getClass() == &CallableObjectClass);
    Value fval = callable->getSlot(JSSLOT_CALLABLE_CONSTRUCT);
    if (fval.isUndefined()) {
        /* We don't have an explicit constructor so allocate a new object and use the call. */
        fval = callable->getSlot(JSSLOT_CALLABLE_CALL);
        JS_ASSERT(fval.isObject());

        /* callable is the constructor, so get callable.prototype is the proto of the new object. */
        Value protov;
        if (!callable->getProperty(cx, ATOM(classPrototype), &protov))
            return false;

        JSObject *proto;
        if (protov.isObject()) {
            proto = &protov.toObject();
        } else {
            proto = callable->getGlobal()->getOrCreateObjectPrototype(cx);
            if (!proto)
                return false;
        }

        JSObject *newobj = NewObjectWithGivenProto(cx, &ObjectClass, proto, NULL);
        if (!newobj)
            return false;

        /* If the call returns an object, return that, otherwise the original newobj. */
        Value rval;
        if (!Invoke(cx, ObjectValue(*newobj), callable->getSlot(JSSLOT_CALLABLE_CALL),
                    argc, vp + 2, &rval)) {
            return false;
        }
        if (rval.isPrimitive())
            vp->setObject(*newobj);
        else
            *vp = rval;
        return true;
    }

    bool ok = Invoke(cx, ObjectValue(*thisobj), fval, argc, vp + 2, vp);
    return ok;
}

Class js::CallableObjectClass = {
    "Function",
    JSCLASS_HAS_RESERVED_SLOTS(2),
    JS_PropertyStub,         /* addProperty */
    JS_PropertyStub,         /* delProperty */
    JS_PropertyStub,         /* getProperty */
    JS_StrictPropertyStub,   /* setProperty */
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub,
    NULL,                    /* finalize    */
    NULL,                    /* reserved0   */
    NULL,                    /* checkAccess */
    callable_Call,
    callable_Construct,
};

JS_FRIEND_API(JSBool)
js::FixProxy(JSContext *cx, JSObject *proxy, JSBool *bp)
{
    if (OperationInProgress(cx, proxy)) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_BAD_PROXY_FIX);
        return false;
    }

    AutoValueRooter tvr(cx);
    if (!Proxy::fix(cx, proxy, tvr.addr()))
        return false;
    if (tvr.value().isUndefined()) {
        *bp = false;
        return true;
    }

    JSObject *props = NonNullObject(cx, tvr.value());
    if (!props)
        return false;

    JSObject *proto = proxy->getProto();
    JSObject *parent = proxy->getParent();
    Class *clasp = IsFunctionProxy(proxy) ? &CallableObjectClass : &ObjectClass;

    /*
     * Make a blank object from the recipe fix provided to us.  This must have
     * number of fixed slots as the proxy so that we can swap their contents.
     */
    gc::AllocKind kind = proxy->getAllocKind();
    JSObject *newborn = NewObjectWithGivenProto(cx, clasp, proto, parent, kind);
    if (!newborn)
        return false;
    AutoObjectRooter tvr2(cx, newborn);

    if (clasp == &CallableObjectClass) {
        newborn->setSlot(JSSLOT_CALLABLE_CALL, GetCall(proxy));
        newborn->setSlot(JSSLOT_CALLABLE_CONSTRUCT, GetConstruct(proxy));
    }

    {
        AutoPendingProxyOperation pending(cx, proxy);
        if (!js_PopulateObject(cx, newborn, props))
            return false;
    }

    /* Trade contents between the newborn object and the proxy. */
    if (!proxy->swap(cx, newborn))
        return false;

    /* The GC will dispose of the proxy object. */

    *bp = true;
    return true;
}

Class js::ProxyClass = {
    "Proxy",
    JSCLASS_HAS_CACHED_PROTO(JSProto_Proxy),
    JS_PropertyStub,         /* addProperty */
    JS_PropertyStub,         /* delProperty */
    JS_PropertyStub,         /* getProperty */
    JS_StrictPropertyStub,   /* setProperty */
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub
};

JS_FRIEND_API(JSObject *)
js_InitProxyClass(JSContext *cx, JSObject *obj)
{
    JSObject *module = NewObjectWithClassProto(cx, &ProxyClass, NULL, obj);
    if (!module || !module->setSingletonType(cx))
        return NULL;

    if (!JS_DefineProperty(cx, obj, "Proxy", OBJECT_TO_JSVAL(module),
                           JS_PropertyStub, JS_StrictPropertyStub, 0)) {
        return NULL;
    }
    if (!JS_DefineFunctions(cx, module, static_methods))
        return NULL;

    MarkStandardClassInitializedNoProto(obj, &ProxyClass);

    return module;
}
