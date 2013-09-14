/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * JS boolean implementation.
 */

#include "jsbool.h"

#include "jstypes.h"
#include "jsapi.h"
#include "jsatom.h"
#include "jscntxt.h"
#include "jsobj.h"

#include "vm/GlobalObject.h"
#include "vm/StringBuffer.h"

#include "jsboolinlines.h"

#include "vm/BooleanObject-inl.h"
#include "vm/GlobalObject-inl.h"

using namespace js;
using namespace js::types;

Class BooleanObject::class_ = {
    "Boolean",
    JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_HAS_CACHED_PROTO(JSProto_Boolean),
    JS_PropertyStub,         /* addProperty */
    JS_DeletePropertyStub,   /* delProperty */
    JS_PropertyStub,         /* getProperty */
    JS_StrictPropertyStub,   /* setProperty */
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub
};

JS_ALWAYS_INLINE bool
IsBoolean(const Value &v)
{
    return v.isBoolean() || (v.isObject() && v.toObject().is<BooleanObject>());
}

#if JS_HAS_TOSOURCE
JS_ALWAYS_INLINE bool
bool_toSource_impl(JSContext *cx, CallArgs args)
{
    const Value &thisv = args.thisv();
    JS_ASSERT(IsBoolean(thisv));

    bool b = thisv.isBoolean() ? thisv.toBoolean() : thisv.toObject().as<BooleanObject>().unbox();

    StringBuffer sb(cx);
    if (!sb.append("(new Boolean(") || !BooleanToStringBuffer(cx, b, sb) || !sb.append("))"))
        return false;

    JSString *str = sb.finishString();
    if (!str)
        return false;
    args.rval().setString(str);
    return true;
}

JSBool
bool_toSource(JSContext *cx, unsigned argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    return CallNonGenericMethod<IsBoolean, bool_toSource_impl>(cx, args);
}
#endif

JS_ALWAYS_INLINE bool
bool_toString_impl(JSContext *cx, CallArgs args)
{
    const Value &thisv = args.thisv();
    JS_ASSERT(IsBoolean(thisv));

    bool b = thisv.isBoolean() ? thisv.toBoolean() : thisv.toObject().as<BooleanObject>().unbox();
    args.rval().setString(js_BooleanToString(cx, b));
    return true;
}

JSBool
bool_toString(JSContext *cx, unsigned argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    return CallNonGenericMethod<IsBoolean, bool_toString_impl>(cx, args);
}

JS_ALWAYS_INLINE bool
bool_valueOf_impl(JSContext *cx, CallArgs args)
{
    const Value &thisv = args.thisv();
    JS_ASSERT(IsBoolean(thisv));

    bool b = thisv.isBoolean() ? thisv.toBoolean() : thisv.toObject().as<BooleanObject>().unbox();
    args.rval().setBoolean(b);
    return true;
}

JSBool
bool_valueOf(JSContext *cx, unsigned argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    return CallNonGenericMethod<IsBoolean, bool_valueOf_impl>(cx, args);
}

static const JSFunctionSpec boolean_methods[] = {
#if JS_HAS_TOSOURCE
    JS_FN(js_toSource_str,  bool_toSource,  0, 0),
#endif
    JS_FN(js_toString_str,  bool_toString,  0, 0),
    JS_FS_END
};

static JSBool
Boolean(JSContext *cx, unsigned argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);

    bool b = args.length() != 0 ? JS::ToBoolean(args[0]) : false;

    if (IsConstructing(vp)) {
        JSObject *obj = BooleanObject::create(cx, b);
        if (!obj)
            return false;
        args.rval().setObject(*obj);
    } else {
        args.rval().setBoolean(b);
    }
    return true;
}

JSObject *
js_InitBooleanClass(JSContext *cx, HandleObject obj)
{
    JS_ASSERT(obj->isNative());

    Rooted<GlobalObject*> global(cx, &obj->as<GlobalObject>());

    RootedObject booleanProto (cx, global->createBlankPrototype(cx, &BooleanObject::class_));
    if (!booleanProto)
        return NULL;
    booleanProto->setFixedSlot(BooleanObject::PRIMITIVE_VALUE_SLOT, BooleanValue(false));

    RootedFunction ctor(cx, global->createConstructor(cx, Boolean, cx->names().Boolean, 1));
    if (!ctor)
        return NULL;

    if (!LinkConstructorAndPrototype(cx, ctor, booleanProto))
        return NULL;

    if (!DefinePropertiesAndBrand(cx, booleanProto, NULL, boolean_methods))
        return NULL;

    Handle<PropertyName*> valueOfName = cx->names().valueOf;
    RootedFunction
        valueOf(cx, NewFunction(cx, NullPtr(), bool_valueOf, 0, JSFunction::NATIVE_FUN,
                                global, valueOfName));
    if (!valueOf)
        return NULL;

    RootedValue value(cx, ObjectValue(*valueOf));
    if (!JSObject::defineProperty(cx, booleanProto, valueOfName, value,
                                  JS_PropertyStub, JS_StrictPropertyStub, 0))
    {
        return NULL;
    }

    if (!DefineConstructorAndPrototype(cx, global, JSProto_Boolean, ctor, booleanProto))
        return NULL;

    return booleanProto;
}

JSString *
js_BooleanToString(JSContext *cx, JSBool b)
{
    return b ? cx->runtime()->atomState.true_ : cx->runtime()->atomState.false_;
}

JS_PUBLIC_API(bool)
js::ToBooleanSlow(const Value &v)
{
    if (v.isString())
        return v.toString()->length() != 0;

    JS_ASSERT(v.isObject());
    return !EmulatesUndefined(&v.toObject());
}

/*
 * This slow path is only ever taken for proxies wrapping Boolean objects
 * The only caller of the fast path, JSON's PreprocessValue, ensures that.
 */
bool
js::BooleanGetPrimitiveValueSlow(HandleObject wrappedBool, JSContext *cx)
{
    JSObject *obj = GetProxyTargetObject(wrappedBool);
    JS_ASSERT(obj);
    return obj->as<BooleanObject>().unbox();
}
