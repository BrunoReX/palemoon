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
 * The Original Code is SpiderMonkey JSON.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998-1999
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Robert Sayre <sayrer@gmail.com>
 *   Dave Camp <dcamp@mozilla.com>
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
#include "jsarena.h"
#include "jsarray.h"
#include "jsatom.h"
#include "jsbool.h"
#include "jscntxt.h"
#include "jsfun.h"
#include "jsinterp.h"
#include "jsiter.h"
#include "jsnum.h"
#include "jsobj.h"
#include "jsonparser.h"
#include "jsprf.h"
#include "jsscan.h"
#include "jsstr.h"
#include "jstypes.h"
#include "jsstdint.h"
#include "jsutil.h"
#include "jsxml.h"
#include "jsvector.h"

#include "json.h"

#include "jsatominlines.h"
#include "jsobjinlines.h"
#include "jsstrinlines.h"

using namespace js;
using namespace js::gc;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4351)
#endif

struct JSONParser
{
    JSONParser(JSContext *cx)
     : hexChar(), numHex(), statep(), stateStack(), rootVal(), objectStack(),
       objectKey(cx), buffer(cx), suppressErrors(false)
    {}

    /* Used while handling \uNNNN in strings */
    jschar hexChar;
    uint8 numHex;

    JSONParserState *statep;
    JSONParserState stateStack[JSON_MAX_DEPTH];
    Value *rootVal;
    JSObject *objectStack;
    js::Vector<jschar, 8> objectKey;
    js::Vector<jschar, 8> buffer;
    bool suppressErrors;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

Class js_JSONClass = {
    js_JSON_str,
    JSCLASS_HAS_CACHED_PROTO(JSProto_JSON),
    PropertyStub,        /* addProperty */
    PropertyStub,        /* delProperty */
    PropertyStub,        /* getProperty */
    StrictPropertyStub,  /* setProperty */
    EnumerateStub,
    ResolveStub,
    ConvertStub
};

JSBool
js_json_parse(JSContext *cx, uintN argc, Value *vp)
{
    JSString *s = NULL;
    Value *argv = vp + 2;
    Value reviver = UndefinedValue();

    if (!JS_ConvertArguments(cx, argc, Jsvalify(argv), "S / v", &s, &reviver))
        return JS_FALSE;

    JSLinearString *linearStr = s->ensureLinear(cx);
    if (!linearStr)
        return JS_FALSE;
    JS::Anchor<JSString *> anchor(linearStr);

    return ParseJSONWithReviver(cx, linearStr->chars(), linearStr->length(), reviver, vp);
}

JSBool
js_json_stringify(JSContext *cx, uintN argc, Value *vp)
{
    Value *argv = vp + 2;
    AutoValueRooter space(cx);
    AutoObjectRooter replacer(cx);

    // Must throw an Error if there isn't a first arg
    if (!JS_ConvertArguments(cx, argc, Jsvalify(argv), "v / o v", vp, replacer.addr(), space.addr()))
        return JS_FALSE;

    StringBuffer sb(cx);

    if (!js_Stringify(cx, vp, replacer.object(), space.value(), sb))
        return JS_FALSE;

    // XXX This can never happen to nsJSON.cpp, but the JSON object
    // needs to support returning undefined. So this is a little awkward
    // for the API, because we want to support streaming writers.
    if (!sb.empty()) {
        JSString *str = sb.finishString();
        if (!str)
            return JS_FALSE;
        vp->setString(str);
    } else {
        vp->setUndefined();
    }

    return JS_TRUE;
}

JSBool
js_TryJSON(JSContext *cx, Value *vp)
{
    if (!vp->isObject())
        return true;

    JSObject *obj = &vp->toObject();
    Value fval;
    jsid id = ATOM_TO_JSID(cx->runtime->atomState.toJSONAtom);
    if (!js_GetMethod(cx, obj, id, JSGET_NO_METHOD_BARRIER, &fval))
        return false;
    if (js_IsCallable(fval)) {
        if (!ExternalInvoke(cx, ObjectValue(*obj), fval, 0, NULL, vp))
            return false;
    }
    return true;
}


static const char quote = '\"';
static const char backslash = '\\';
static const char unicodeEscape[] = "\\u00";

static JSBool
write_string(JSContext *cx, StringBuffer &sb, const jschar *buf, uint32 len)
{
    if (!sb.append(quote))
        return JS_FALSE;

    uint32 mark = 0;
    uint32 i;
    for (i = 0; i < len; ++i) {
        if (buf[i] == quote || buf[i] == backslash) {
            if (!sb.append(&buf[mark], i - mark) || !sb.append(backslash) ||
                !sb.append(buf[i])) {
                return JS_FALSE;
            }
            mark = i + 1;
        } else if (buf[i] <= 31 || buf[i] == 127) {
            if (!sb.append(&buf[mark], i - mark) ||
                !sb.append(unicodeEscape)) {
                return JS_FALSE;
            }
            char ubuf[3];
            size_t len = JS_snprintf(ubuf, sizeof(ubuf), "%.2x", buf[i]);
            JS_ASSERT(len == 2);
            jschar wbuf[3];
            size_t wbufSize = JS_ARRAY_LENGTH(wbuf);
            if (!js_InflateStringToBuffer(cx, ubuf, len, wbuf, &wbufSize) ||
                !sb.append(wbuf, wbufSize)) {
                return JS_FALSE;
            }
            mark = i + 1;
        }
    }

    if (mark < len && !sb.append(&buf[mark], len - mark))
        return JS_FALSE;

    return sb.append(quote);
}

class StringifyContext
{
public:
    StringifyContext(JSContext *cx, StringBuffer &sb, JSObject *replacer)
    : sb(sb), gap(cx), replacer(replacer), depth(0), objectStack(cx)
    {}

    bool initializeGap(JSContext *cx, const Value &space) {
        AutoValueRooter gapValue(cx, space);

        if (space.isObject()) {
            JSObject &obj = space.toObject();
            Class *clasp = obj.getClass();
            if (clasp == &js_NumberClass || clasp == &js_StringClass)
                *gapValue.addr() = obj.getPrimitiveThis();
        }

        if (gapValue.value().isString()) {
            if (!gap.append(gapValue.value().toString()))
                return false;
            if (gap.length() > 10)
                gap.resize(10);
        } else if (gapValue.value().isNumber()) {
            jsdouble d = gapValue.value().isInt32()
                         ? gapValue.value().toInt32()
                         : js_DoubleToInteger(gapValue.value().toDouble());
            d = JS_MIN(10, d);
            if (d >= 1 && !gap.appendN(' ', uint32(d)))
                return false;
        }

        return true;
    }

    bool initializeStack() {
        return objectStack.init(16);
    }

#ifdef DEBUG
    ~StringifyContext() { JS_ASSERT(objectStack.empty()); }
#endif

    StringBuffer &sb;
    StringBuffer gap;
    JSObject *replacer;
    uint32 depth;
    HashSet<JSObject *> objectStack;
};

static JSBool Str(JSContext *cx, const Value &v, StringifyContext *scx);

static JSBool
WriteIndent(JSContext *cx, StringifyContext *scx, uint32 limit)
{
    if (!scx->gap.empty()) {
        if (!scx->sb.append('\n'))
            return JS_FALSE;
        for (uint32 i = 0; i < limit; i++) {
            if (!scx->sb.append(scx->gap.begin(), scx->gap.end()))
                return JS_FALSE;
        }
    }

    return JS_TRUE;
}

class CycleDetector
{
  public:
    CycleDetector(StringifyContext *scx, JSObject *obj)
      : objectStack(scx->objectStack), obj(obj) {
    }

    bool init(JSContext *cx) {
        HashSet<JSObject *>::AddPtr ptr = objectStack.lookupForAdd(obj);
        if (ptr) {
            JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_CYCLIC_VALUE, js_object_str);
            return false;
        }
        return objectStack.add(ptr, obj);
    }

    ~CycleDetector() {
        objectStack.remove(obj);
    }

  private:
    HashSet<JSObject *> &objectStack;
    JSObject *const obj;
};

/*
 * ES5 15.12.3 Str, steps 2-4, extracted to enable preprocessing of property
 * values when stringifying objects in JO.
 */
static bool
PreprocessValue(JSContext *cx, JSObject *holder, jsid key, Value *vp, StringifyContext *scx)
{
    JSString *keyStr = NULL;

    /* Step 2. */
    if (vp->isObject()) {
        Value toJSON;
        jsid id = ATOM_TO_JSID(cx->runtime->atomState.toJSONAtom);
        if (!js_GetMethod(cx, &vp->toObject(), id, JSGET_NO_METHOD_BARRIER, &toJSON))
            return false;

        if (js_IsCallable(toJSON)) {
            keyStr = IdToString(cx, key);
            if (!keyStr)
                return false;

            LeaveTrace(cx);
            InvokeArgsGuard args;
            if (!cx->stack().pushInvokeArgs(cx, 1, &args))
                return false;

            args.callee() = toJSON;
            args.thisv() = *vp;
            args[0] = StringValue(keyStr);

            if (!Invoke(cx, args, 0))
                return false;
            *vp = args.rval();
        }
    }

    /* Step 3. */
    if (scx->replacer && scx->replacer->isCallable()) {
        if (!keyStr) {
            keyStr = IdToString(cx, key);
            if (!keyStr)
                return false;
        }

        LeaveTrace(cx);
        InvokeArgsGuard args;
        if (!cx->stack().pushInvokeArgs(cx, 2, &args))
            return false;

        args.callee() = ObjectValue(*scx->replacer);
        args.thisv() = ObjectValue(*holder);
        args[0] = StringValue(keyStr);
        args[1] = *vp;

        if (!Invoke(cx, args, 0))
            return false;
        *vp = args.rval();
    }

    /* Step 4. */
    if (vp->isObject()) {
        JSObject *obj = &vp->toObject();
        Class *clasp = obj->getClass();
        if (clasp == &js_NumberClass) {
            double d;
            if (!ValueToNumber(cx, *vp, &d))
                return false;
            vp->setNumber(d);
        } else if (clasp == &js_StringClass) {
            JSString *str = js_ValueToString(cx, *vp);
            if (!str)
                return false;
            vp->setString(str);
        } else if (clasp == &js_BooleanClass) {
            *vp = obj->getPrimitiveThis();
            JS_ASSERT(vp->isBoolean());
        }
    }

    return true;
}

/*
 * Determines whether a value which has passed by ES5 150.2.3 Str steps 1-4's
 * gauntlet will result in Str returning |undefined|.  This function is used to
 * properly omit properties resulting in such values when stringifying objects,
 * while properly stringifying such properties as null when they're encountered
 * in arrays.
 */
static inline bool
IsFilteredValue(const Value &v)
{
    return v.isUndefined() || js_IsCallable(v) || (v.isObject() && v.toObject().isXML());
}

/* ES5 15.12.3 JO. */
static JSBool
JO(JSContext *cx, JSObject *obj, StringifyContext *scx)
{
    /*
     * This method implements the JO algorithm in ES5 15.12.3, but:
     *
     *   * The algorithm is somewhat reformulated to allow the final string to
     *     be streamed into a single buffer, rather than be created and copied
     *     into place incrementally as the ES5 algorithm specifies it.  This
     *     requires moving portions of the Str call in 8a into this algorithm
     *     (and in JA as well).
     */

    /* Steps 1-2, 11. */
    CycleDetector detect(scx, obj);
    if (!detect.init(cx))
        return JS_FALSE;

    if (!scx->sb.append('{'))
        return JS_FALSE;

    AutoIdRooter idr(cx);
    jsid& id = *idr.addr();

    /* Steps 5-7. */
    /* XXX Bug 648471: Do this in js_Stringify, rename keySource. */
    Value keySource = ObjectValue(*obj);
    bool usingWhitelist = false;

    // if the replacer is an array, we use the keys from it
    if (scx->replacer && JS_IsArrayObject(cx, scx->replacer)) {
        usingWhitelist = true;
        keySource.setObject(*scx->replacer);
    }

    bool wroteMember = false;
    AutoIdVector props(cx);
    if (!GetPropertyNames(cx, &keySource.toObject(), JSITER_OWNONLY, &props))
        return JS_FALSE;

    /* Steps 8-10, 13. */
    for (size_t i = 0, len = props.length(); i < len; i++) {
        if (!usingWhitelist) {
            if (!js_ValueToStringId(cx, IdToValue(props[i]), &id))
                return JS_FALSE;
        } else {
            // skip non-index properties
            jsuint index = 0;
            if (!js_IdIsIndex(props[i], &index))
                continue;

            Value whitelistElement;
            if (!scx->replacer->getProperty(cx, props[i], &whitelistElement))
                return JS_FALSE;

            if (!js_ValueToStringId(cx, whitelistElement, &id))
                return JS_FALSE;
        }

        /*
         * Steps 8a-8b.  Note that the call to Str is broken up into 1) getting
         * the property; 2) processing for toJSON, calling the replacer, and
         * handling boxed Number/String/Boolean objects; 3) filtering out
         * values which process to |undefined|, and 4) stringifying all values
         * which pass the filter.
         */
        Value outputValue;
        if (!obj->getProperty(cx, id, &outputValue))
            return JS_FALSE;
        if (!PreprocessValue(cx, obj, id, &outputValue, scx))
            return JS_FALSE;
        if (IsFilteredValue(outputValue))
            continue;

        /* Output a comma unless this is the first member to write. */
        if (wroteMember && !scx->sb.append(','))
            return JS_FALSE;
        wroteMember = true;

        if (!WriteIndent(cx, scx, scx->depth))
            return JS_FALSE;

        // Be careful below: this string is weakly rooted!
        JSString *s = js_ValueToString(cx, IdToValue(id));
        if (!s)
            return JS_FALSE;

        JS::Anchor<JSString *> anchor(s);
        size_t length = s->length();
        const jschar *chars = s->getChars(cx);
        if (!chars)
            return JS_FALSE;

        if (!write_string(cx, scx->sb, chars, length) ||
            !scx->sb.append(':') ||
            !(scx->gap.empty() || scx->sb.append(' ')) ||
            !Str(cx, outputValue, scx)) {
            return JS_FALSE;
        }
    }

    if (wroteMember && !WriteIndent(cx, scx, scx->depth - 1))
        return JS_FALSE;

    return scx->sb.append('}');
}

/* ES5 15.12.3 JA. */
static JSBool
JA(JSContext *cx, JSObject *obj, StringifyContext *scx)
{
    /*
     * This method implements the JA algorithm in ES5 15.12.3, but:
     *
     *   * The algorithm is somewhat reformulated to allow the final string to
     *     be streamed into a single buffer, rather than be created and copied
     *     into place incrementally as the ES5 algorithm specifies it.  This
     *     requires moving portions of the Str call in 8a into this algorithm
     *     (and in JO as well).
     */

    /* Steps 1-2, 11. */
    CycleDetector detect(scx, obj);
    if (!detect.init(cx))
        return JS_FALSE;

    if (!scx->sb.append('['))
        return JS_FALSE;

    /* Step 6. */
    jsuint length;
    if (!js_GetLengthProperty(cx, obj, &length))
        return JS_FALSE;

    /* Steps 7-10. */
    if (length != 0) {
        /* Steps 4, 10b(i). */
        if (!WriteIndent(cx, scx, scx->depth))
            return JS_FALSE;

        /* Steps 7-10. */
        Value outputValue;
        for (jsuint i = 0; i < length; i++) {
            jsid id = INT_TO_JSID(i);

            /*
             * Steps 8a-8c.  Again note how the call to the spec's Str method
             * is broken up into getting the property, running it past toJSON
             * and the replacer and maybe unboxing, and interpreting some
             * values as |null| in separate steps.
             */
            if (!obj->getProperty(cx, id, &outputValue))
                return JS_FALSE;
            if (!PreprocessValue(cx, obj, id, &outputValue, scx))
                return JS_FALSE;
            if (IsFilteredValue(outputValue)) {
                if (!scx->sb.append("null"))
                    return JS_FALSE;
            } else {
                if (!Str(cx, outputValue, scx))
                    return JS_FALSE;
            }

            /* Steps 3, 4, 10b(i). */
            if (i < length - 1) {
                if (!scx->sb.append(','))
                    return JS_FALSE;
                if (!WriteIndent(cx, scx, scx->depth))
                    return JS_FALSE;
            }
        }

        /* Step 10(b)(iii). */
        if (!WriteIndent(cx, scx, scx->depth - 1))
            return JS_FALSE;
    }

    return scx->sb.append(']');
}

static JSBool
Str(JSContext *cx, const Value &v, StringifyContext *scx)
{
    /* Step 11 must be handled by the caller. */
    JS_ASSERT(!IsFilteredValue(v));

    JS_CHECK_RECURSION(cx, return false);

    /*
     * This method implements the Str algorithm in ES5 15.12.3, but:
     *
     *   * We move property retrieval (step 1) into callers to stream the
     *     stringification process and avoid constantly copying strings.
     *   * We move the preprocessing in steps 2-4 into a helper function to
     *     allow both JO and JA to use this method.  While JA could use it
     *     without this move, JO must omit any |undefined|-valued property per
     *     so it can't stream out a value using the Str method exactly as
     *     defined by ES5.
     *   * We move step 11 into callers, again to ease streaming.
     */

    /* Step 8. */
    if (v.isString()) {
        JSString *str = v.toString();
        size_t length = str->length();
        const jschar *chars = str->getChars(cx);
        if (!chars)
            return false;
        return write_string(cx, scx->sb, chars, length);
    }

    /* Step 5. */
    if (v.isNull())
        return scx->sb.append("null");

    /* Steps 6-7. */
    if (v.isBoolean())
        return v.toBoolean() ? scx->sb.append("true") : scx->sb.append("false");

    /* Step 9. */
    if (v.isNumber()) {
        if (v.isDouble()) {
            if (!JSDOUBLE_IS_FINITE(v.toDouble()))
                return scx->sb.append("null");
        }

        StringBuffer sb(cx);
        if (!NumberValueToStringBuffer(cx, v, sb))
            return false;

        return scx->sb.append(sb.begin(), sb.length());
    }

    /* Step 10. */
    JS_ASSERT(v.isObject());
    JSBool ok;

    scx->depth++;
    ok = (JS_IsArrayObject(cx, &v.toObject()) ? JA : JO)(cx, &v.toObject(), scx);
    scx->depth--;

    return ok;
}

JSBool
js_Stringify(JSContext *cx, Value *vp, JSObject *replacer, const Value &space,
             StringBuffer &sb)
{
    StringifyContext scx(cx, sb, replacer);
    if (!scx.initializeGap(cx, space) || !scx.initializeStack())
        return JS_FALSE;

    JSObject *obj = NewBuiltinClassInstance(cx, &js_ObjectClass);
    if (!obj)
        return JS_FALSE;

    AutoObjectRooter tvr(cx, obj);
    jsid emptyId = ATOM_TO_JSID(cx->runtime->atomState.emptyAtom);
    if (!obj->defineProperty(cx, emptyId, *vp, NULL, NULL, JSPROP_ENUMERATE))
        return JS_FALSE;

    if (!PreprocessValue(cx, obj, emptyId, vp, &scx))
        return JS_FALSE;
    if (IsFilteredValue(*vp))
        return JS_TRUE;
    return Str(cx, *vp, &scx);
}

// helper to determine whether a character could be part of a number
static JSBool IsNumChar(jschar c)
{
    return ((c <= '9' && c >= '0') || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E');
}

static JSBool HandleDataString(JSContext *cx, JSONParser *jp);
static JSBool HandleDataKeyString(JSContext *cx, JSONParser *jp);
static JSBool HandleDataNumber(JSContext *cx, JSONParser *jp);
static JSBool HandleDataKeyword(JSContext *cx, JSONParser *jp);
static JSBool PopState(JSContext *cx, JSONParser *jp);

static bool
Walk(JSContext *cx, jsid id, JSObject *holder, const Value &reviver, Value *vp)
{
    JS_CHECK_RECURSION(cx, return false);

    if (!holder->getProperty(cx, id, vp))
        return false;

    JSObject *obj;

    if (vp->isObject() && !(obj = &vp->toObject())->isCallable()) {
        AutoValueRooter propValue(cx);

        if(obj->isArray()) {
            jsuint length = 0;
            if (!js_GetLengthProperty(cx, obj, &length))
                return false;

            for (jsuint i = 0; i < length; i++) {
                jsid index;
                if (!js_IndexToId(cx, i, &index))
                    return false;

                if (!Walk(cx, index, obj, reviver, propValue.addr()))
                    return false;

                if (!obj->defineProperty(cx, index, propValue.value(), NULL, NULL, JSPROP_ENUMERATE))
                    return false;
            }
        } else {
            AutoIdVector props(cx);
            if (!GetPropertyNames(cx, obj, JSITER_OWNONLY, &props))
                return false;

            for (size_t i = 0, len = props.length(); i < len; i++) {
                jsid idName = props[i];
                if (!Walk(cx, idName, obj, reviver, propValue.addr()))
                    return false;
                if (propValue.value().isUndefined()) {
                    if (!js_DeleteProperty(cx, obj, idName, propValue.addr(), false))
                        return false;
                } else {
                    if (!obj->defineProperty(cx, idName, propValue.value(), NULL, NULL,
                                             JSPROP_ENUMERATE)) {
                        return false;
                    }
                }
            }
        }
    }

    // return reviver.call(holder, key, value);
    const Value &value = *vp;
    JSString *key = js_ValueToString(cx, IdToValue(id));
    if (!key)
        return false;

    Value vec[2] = { StringValue(key), value };
    Value reviverResult;
    if (!JS_CallFunctionValue(cx, holder, Jsvalify(reviver),
                              2, Jsvalify(vec), Jsvalify(&reviverResult))) {
        return false;
    }

    *vp = reviverResult;
    return true;
}

static JSBool
JSONParseError(JSONParser *jp, JSContext *cx)
{
    if (!jp->suppressErrors)
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_JSON_BAD_PARSE, "syntax error");
    return JS_FALSE;
}

static bool
Revive(JSContext *cx, const Value &reviver, Value *vp)
{

    JSObject *obj = NewBuiltinClassInstance(cx, &js_ObjectClass);
    if (!obj)
        return false;

    AutoObjectRooter tvr(cx, obj);
    if (!obj->defineProperty(cx, ATOM_TO_JSID(cx->runtime->atomState.emptyAtom),
                             *vp, NULL, NULL, JSPROP_ENUMERATE)) {
        return false;
    }

    return Walk(cx, ATOM_TO_JSID(cx->runtime->atomState.emptyAtom), obj, reviver, vp);
}

JSONParser *
js_BeginJSONParse(JSContext *cx, Value *rootVal, bool suppressErrors /*= false*/)
{
    if (!cx)
        return NULL;

    JSObject *arr = NewDenseEmptyArray(cx);
    if (!arr)
        return NULL;

    JSONParser *jp = cx->new_<JSONParser>(cx);
    if (!jp)
        return NULL;

    jp->objectStack = arr;
    if (!JS_AddNamedObjectRoot(cx, &jp->objectStack, "JSON parse stack"))
        goto bad;

    jp->statep = jp->stateStack;
    *jp->statep = JSON_PARSE_STATE_INIT;
    jp->rootVal = rootVal;
    jp->suppressErrors = suppressErrors;

    return jp;

bad:
    js_FinishJSONParse(cx, jp, NullValue());
    return NULL;
}

bool
js_FinishJSONParse(JSContext *cx, JSONParser *jp, const Value &reviver)
{
    if (!jp)
        return true;

    JSBool early_ok = JS_TRUE;

    // Check for unprocessed primitives at the root. This doesn't happen for
    // strings because a closing quote triggers value processing.
    if ((jp->statep - jp->stateStack) == 1) {
        if (*jp->statep == JSON_PARSE_STATE_KEYWORD) {
            early_ok = HandleDataKeyword(cx, jp);
            if (early_ok)
                PopState(cx, jp);
        } else if (*jp->statep == JSON_PARSE_STATE_NUMBER) {
            early_ok = HandleDataNumber(cx, jp);
            if (early_ok)
                PopState(cx, jp);
        }
    }

    // This internal API is infallible, in spite of its JSBool return type.
    js_RemoveRoot(cx->runtime, &jp->objectStack);

    bool ok = *jp->statep == JSON_PARSE_STATE_FINISHED;
    Value *vp = jp->rootVal;

    if (!early_ok) {
        ok = false;
    } else if (!ok) {
        JSONParseError(jp, cx);
    } else if (js_IsCallable(reviver)) {
        ok = Revive(cx, reviver, vp);
    }

    cx->delete_(jp);

    return ok;
}

namespace js {

JSBool
ParseJSONWithReviver(JSContext *cx, const jschar *chars, size_t length, const Value &reviver,
                     Value *vp, DecodingMode decodingMode /* = STRICT */)
{
#if USE_OLD_AND_BUSTED_JSON_PARSER
    JSONParser *jp = js_BeginJSONParse(cx, vp);
    if (!jp)
        return false;
    JSBool ok = js_ConsumeJSONText(cx, jp, chars, length, decodingMode);
    ok &= !!js_FinishJSONParse(cx, jp, reviver);
    return ok;
#else
    JSONSourceParser parser(cx, chars, length,
                            decodingMode == STRICT
                            ? JSONSourceParser::StrictJSON
                            : JSONSourceParser::LegacyJSON);
    if (!parser.parse(vp))
        return false;
    if (js_IsCallable(reviver))
        return Revive(cx, reviver, vp);
    return true;
#endif
}

} /* namespace js */

static JSBool
PushState(JSContext *cx, JSONParser *jp, JSONParserState state)
{
    if (*jp->statep == JSON_PARSE_STATE_FINISHED) {
        // extra input
        return JSONParseError(jp, cx);
    }

    jp->statep++;
    if ((uint32)(jp->statep - jp->stateStack) >= JS_ARRAY_LENGTH(jp->stateStack)) {
        // too deep
        return JSONParseError(jp, cx);
    }

    *jp->statep = state;

    return JS_TRUE;
}

static JSBool
PopState(JSContext *cx, JSONParser *jp)
{
    jp->statep--;
    if (jp->statep < jp->stateStack) {
        jp->statep = jp->stateStack;
        return JSONParseError(jp, cx);
    }

    if (*jp->statep == JSON_PARSE_STATE_INIT)
        *jp->statep = JSON_PARSE_STATE_FINISHED;

    return JS_TRUE;
}

static JSBool
PushValue(JSContext *cx, JSONParser *jp, JSObject *parent, const Value &value)
{
    JSBool ok;
    if (parent->isArray()) {
        jsuint len;
        ok = js_GetLengthProperty(cx, parent, &len);
        if (ok) {
            jsid index;
            if (!js_IndexToId(cx, len, &index))
                return JS_FALSE;
            ok = parent->defineProperty(cx, index, value, NULL, NULL, JSPROP_ENUMERATE);
        }
    } else {
        ok = JS_DefineUCProperty(cx, parent, jp->objectKey.begin(),
                                 jp->objectKey.length(), Jsvalify(value),
                                 NULL, NULL, JSPROP_ENUMERATE);
        jp->objectKey.clear();
    }

    return ok;
}

static JSBool
PushObject(JSContext *cx, JSONParser *jp, JSObject *obj)
{
    jsuint len;
    if (!js_GetLengthProperty(cx, jp->objectStack, &len))
        return JS_FALSE;
    if (len >= JSON_MAX_DEPTH)
        return JSONParseError(jp, cx);

    AutoObjectRooter tvr(cx, obj);
    Value v = ObjectOrNullValue(obj);

    // Check if this is the root object
    if (len == 0) {
        *jp->rootVal = v;
        // This property must be enumerable to keep the array dense
        if (!jp->objectStack->defineProperty(cx, INT_TO_JSID(0), *jp->rootVal,
                                             NULL, NULL, JSPROP_ENUMERATE)) {
            return JS_FALSE;
        }
        return JS_TRUE;
    }

    Value p;
    if (!jp->objectStack->getProperty(cx, INT_TO_JSID(len - 1), &p))
        return JS_FALSE;

    JSObject *parent = &p.toObject();
    if (!PushValue(cx, jp, parent, v))
        return JS_FALSE;

    // This property must be enumerable to keep the array dense
    if (!jp->objectStack->defineProperty(cx, INT_TO_JSID(len), v,
                                         NULL, NULL, JSPROP_ENUMERATE)) {
        return JS_FALSE;
    }

    return JS_TRUE;
}

static JSBool
OpenObject(JSContext *cx, JSONParser *jp)
{
    JSObject *obj = NewBuiltinClassInstance(cx, &js_ObjectClass);
    if (!obj)
        return JS_FALSE;

    return PushObject(cx, jp, obj);
}

static JSBool
OpenArray(JSContext *cx, JSONParser *jp)
{
    // Add an array to an existing array or object
    JSObject *arr = NewDenseEmptyArray(cx);
    if (!arr)
        return JS_FALSE;

    return PushObject(cx, jp, arr);
}

static JSBool
CloseObject(JSContext *cx, JSONParser *jp)
{
    jsuint len;
    if (!js_GetLengthProperty(cx, jp->objectStack, &len))
        return JS_FALSE;
    if (!js_SetLengthProperty(cx, jp->objectStack, len - 1))
        return JS_FALSE;

    return JS_TRUE;
}

static JSBool
CloseArray(JSContext *cx, JSONParser *jp)
{
    return CloseObject(cx, jp);
}

static JSBool
PushPrimitive(JSContext *cx, JSONParser *jp, const Value &value)
{
    AutoValueRooter tvr(cx, value);

    jsuint len;
    if (!js_GetLengthProperty(cx, jp->objectStack, &len))
        return JS_FALSE;

    if (len > 0) {
        Value o;
        if (!jp->objectStack->getProperty(cx, INT_TO_JSID(len - 1), &o))
            return JS_FALSE;

        return PushValue(cx, jp, &o.toObject(), value);
    }

    // root value must be primitive
    *jp->rootVal = value;
    return JS_TRUE;
}

static JSBool
HandleNumber(JSContext *cx, JSONParser *jp, const jschar *buf, uint32 len)
{
    const jschar *ep;
    double val;
    if (!js_strtod(cx, buf, buf + len, &ep, &val))
        return JS_FALSE;
    if (ep != buf + len) {
        // bad number input
        return JSONParseError(jp, cx);
    }

    return PushPrimitive(cx, jp, NumberValue(val));
}

static JSBool
HandleString(JSContext *cx, JSONParser *jp, const jschar *buf, uint32 len)
{
    JSString *str = js_NewStringCopyN(cx, buf, len);
    if (!str)
        return JS_FALSE;

    return PushPrimitive(cx, jp, StringValue(str));
}

static JSBool
HandleKeyword(JSContext *cx, JSONParser *jp, const jschar *buf, uint32 len)
{
    const KeywordInfo *ki = FindKeyword(buf, len);
    if (!ki || ki->tokentype != TOK_PRIMARY) {
        // bad keyword
        return JSONParseError(jp, cx);
    }

    Value keyword;
    if (buf[0] == 'n') {
        keyword.setNull();
    } else if (buf[0] == 't') {
        keyword.setBoolean(true);
    } else if (buf[0] == 'f') {
        keyword.setBoolean(false);
    } else {
        return JSONParseError(jp, cx);
    }

    return PushPrimitive(cx, jp, keyword);
}

static JSBool
HandleDataString(JSContext *cx, JSONParser *jp)
{
    JSBool ok = HandleString(cx, jp, jp->buffer.begin(), jp->buffer.length());
    if (ok)
        jp->buffer.clear();
    return ok;
}

static JSBool
HandleDataKeyString(JSContext *cx, JSONParser *jp)
{
    JSBool ok = jp->objectKey.append(jp->buffer.begin(), jp->buffer.end());
    if (ok)
        jp->buffer.clear();
    return ok;
}

static JSBool
HandleDataNumber(JSContext *cx, JSONParser *jp)
{
    JSBool ok = HandleNumber(cx, jp, jp->buffer.begin(), jp->buffer.length());
    if (ok)
        jp->buffer.clear();
    return ok;
}

static JSBool
HandleDataKeyword(JSContext *cx, JSONParser *jp)
{
    JSBool ok = HandleKeyword(cx, jp, jp->buffer.begin(), jp->buffer.length());
    if (ok)
        jp->buffer.clear();
    return ok;
}

JSBool
js_ConsumeJSONText(JSContext *cx, JSONParser *jp, const jschar *data, uint32 len,
                   DecodingMode decodingMode)
{
    CHECK_REQUEST(cx);

    if (*jp->statep == JSON_PARSE_STATE_INIT) {
        PushState(cx, jp, JSON_PARSE_STATE_VALUE);
    }

    for (uint32 i = 0; i < len; i++) {
        jschar c = data[i];
        switch (*jp->statep) {
          case JSON_PARSE_STATE_ARRAY_INITIAL_VALUE:
            if (c == ']') {
                if (!PopState(cx, jp))
                    return JS_FALSE;
                JS_ASSERT(*jp->statep == JSON_PARSE_STATE_ARRAY_AFTER_ELEMENT);
                if (!CloseArray(cx, jp) || !PopState(cx, jp))
                    return JS_FALSE;
                break;
            }
            // fall through if non-empty array or whitespace

          case JSON_PARSE_STATE_VALUE:
            if (c == '"') {
                *jp->statep = JSON_PARSE_STATE_STRING;
                break;
            }

            if (IsNumChar(c)) {
                *jp->statep = JSON_PARSE_STATE_NUMBER;
                if (!jp->buffer.append(c))
                    return JS_FALSE;
                break;
            }

            if (JS7_ISLET(c)) {
                *jp->statep = JSON_PARSE_STATE_KEYWORD;
                if (!jp->buffer.append(c))
                    return JS_FALSE;
                break;
            }

            if (c == '{') {
                *jp->statep = JSON_PARSE_STATE_OBJECT_AFTER_PAIR;
                if (!OpenObject(cx, jp) || !PushState(cx, jp, JSON_PARSE_STATE_OBJECT_INITIAL_PAIR))
                    return JS_FALSE;
            } else if (c == '[') {
                *jp->statep = JSON_PARSE_STATE_ARRAY_AFTER_ELEMENT;
                if (!OpenArray(cx, jp) || !PushState(cx, jp, JSON_PARSE_STATE_ARRAY_INITIAL_VALUE))
                    return JS_FALSE;
            } else if (JS_ISXMLSPACE(c)) {
                // nothing to do
            } else if (decodingMode == LEGACY && c == ']') {
                if (!PopState(cx, jp))
                    return JS_FALSE;
                JS_ASSERT(*jp->statep == JSON_PARSE_STATE_ARRAY_AFTER_ELEMENT);
                if (!CloseArray(cx, jp) || !PopState(cx, jp))
                    return JS_FALSE;
            } else {
                return JSONParseError(jp, cx);
            }
            break;

          case JSON_PARSE_STATE_ARRAY_AFTER_ELEMENT:
            if (c == ',') {
                if (!PushState(cx, jp, JSON_PARSE_STATE_VALUE))
                    return JS_FALSE;
            } else if (c == ']') {
                if (!CloseArray(cx, jp) || !PopState(cx, jp))
                    return JS_FALSE;
            } else if (!JS_ISXMLSPACE(c)) {
                return JSONParseError(jp, cx);
            }
            break;

          case JSON_PARSE_STATE_OBJECT_AFTER_PAIR:
            if (c == ',') {
                if (!PushState(cx, jp, JSON_PARSE_STATE_OBJECT_PAIR))
                    return JS_FALSE;
            } else if (c == '}') {
                if (!CloseObject(cx, jp) || !PopState(cx, jp))
                    return JS_FALSE;
            } else if (!JS_ISXMLSPACE(c)) {
                return JSONParseError(jp, cx);
            }
            break;

          case JSON_PARSE_STATE_OBJECT_INITIAL_PAIR:
            if (c == '}') {
                if (!PopState(cx, jp))
                    return JS_FALSE;
                JS_ASSERT(*jp->statep == JSON_PARSE_STATE_OBJECT_AFTER_PAIR);
                if (!CloseObject(cx, jp) || !PopState(cx, jp))
                    return JS_FALSE;
                break;
            }
            // fall through if non-empty object or whitespace

          case JSON_PARSE_STATE_OBJECT_PAIR:
            if (c == '"') {
                // we want to be waiting for a : when the string has been read
                *jp->statep = JSON_PARSE_STATE_OBJECT_IN_PAIR;
                if (!PushState(cx, jp, JSON_PARSE_STATE_STRING))
                    return JS_FALSE;
            } else if (JS_ISXMLSPACE(c)) {
                // nothing to do
            } else if (decodingMode == LEGACY && c == '}') {
                if (!PopState(cx, jp))
                    return JS_FALSE;
                JS_ASSERT(*jp->statep == JSON_PARSE_STATE_OBJECT_AFTER_PAIR);
                if (!CloseObject(cx, jp) || !PopState(cx, jp))
                    return JS_FALSE;
            } else {
                return JSONParseError(jp, cx);
            }
            break;

          case JSON_PARSE_STATE_OBJECT_IN_PAIR:
            if (c == ':') {
                *jp->statep = JSON_PARSE_STATE_VALUE;
            } else if (!JS_ISXMLSPACE(c)) {
                return JSONParseError(jp, cx);
            }
            break;

          case JSON_PARSE_STATE_STRING:
            if (c == '"') {
                if (!PopState(cx, jp))
                    return JS_FALSE;
                if (*jp->statep == JSON_PARSE_STATE_OBJECT_IN_PAIR) {
                    if (!HandleDataKeyString(cx, jp))
                        return JS_FALSE;
                } else {
                    if (!HandleDataString(cx, jp))
                        return JS_FALSE;
                }
            } else if (c == '\\') {
                *jp->statep = JSON_PARSE_STATE_STRING_ESCAPE;
            } else if (c <= 0x1F) {
                // The JSON lexical grammer does not allow a JSONStringCharacter to be
                // any of the Unicode characters U+0000 thru U+001F (control characters).
                return JSONParseError(jp, cx);
            } else {
                if (!jp->buffer.append(c))
                    return JS_FALSE;
            }
            break;

          case JSON_PARSE_STATE_STRING_ESCAPE:
            switch (c) {
              case '"':
              case '\\':
              case '/':
                break;
              case 'b' : c = '\b'; break;
              case 'f' : c = '\f'; break;
              case 'n' : c = '\n'; break;
              case 'r' : c = '\r'; break;
              case 't' : c = '\t'; break;
              default :
                if (c == 'u') {
                    jp->numHex = 0;
                    jp->hexChar = 0;
                    *jp->statep = JSON_PARSE_STATE_STRING_HEX;
                    continue;
                } else {
                    return JSONParseError(jp, cx);
                }
            }

            if (!jp->buffer.append(c))
                return JS_FALSE;
            *jp->statep = JSON_PARSE_STATE_STRING;
            break;

          case JSON_PARSE_STATE_STRING_HEX:
            if (('0' <= c) && (c <= '9')) {
                jp->hexChar = (jp->hexChar << 4) | (c - '0');
            } else if (('a' <= c) && (c <= 'f')) {
                jp->hexChar = (jp->hexChar << 4) | (c - 'a' + 0x0a);
            } else if (('A' <= c) && (c <= 'F')) {
                jp->hexChar = (jp->hexChar << 4) | (c - 'A' + 0x0a);
            } else {
                return JSONParseError(jp, cx);
            }

            if (++(jp->numHex) == 4) {
                if (!jp->buffer.append(jp->hexChar))
                    return JS_FALSE;
                jp->hexChar = 0;
                jp->numHex = 0;
                *jp->statep = JSON_PARSE_STATE_STRING;
            }
            break;

          case JSON_PARSE_STATE_KEYWORD:
            if (JS7_ISLET(c)) {
                if (!jp->buffer.append(c))
                    return JS_FALSE;
            } else {
                // this character isn't part of the keyword, process it again
                i--;
                if (!PopState(cx, jp))
                    return JS_FALSE;

                if (!HandleDataKeyword(cx, jp))
                    return JS_FALSE;
            }
            break;

          case JSON_PARSE_STATE_NUMBER:
            if (IsNumChar(c)) {
                if (!jp->buffer.append(c))
                    return JS_FALSE;
            } else {
                // this character isn't part of the number, process it again
                i--;
                if (!PopState(cx, jp))
                    return JS_FALSE;
                if (!HandleDataNumber(cx, jp))
                    return JS_FALSE;
            }
            break;

          case JSON_PARSE_STATE_FINISHED:
            if (!JS_ISXMLSPACE(c)) {
                // extra input
                return JSONParseError(jp, cx);
            }
            break;

          default:
            JS_NOT_REACHED("Invalid JSON parser state");
        }
    }

    return JS_TRUE;
}

#if JS_HAS_TOSOURCE
static JSBool
json_toSource(JSContext *cx, uintN argc, Value *vp)
{
    vp->setString(CLASS_ATOM(cx, JSON));
    return JS_TRUE;
}
#endif

static JSFunctionSpec json_static_methods[] = {
#if JS_HAS_TOSOURCE
    JS_FN(js_toSource_str,  json_toSource,      0, 0),
#endif
    JS_FN("parse",          js_json_parse,      2, 0),
    JS_FN("stringify",      js_json_stringify,  3, 0),
    JS_FS_END
};

JSObject *
js_InitJSONClass(JSContext *cx, JSObject *obj)
{
    JSObject *JSON;

    JSON = NewNonFunction<WithProto::Class>(cx, &js_JSONClass, NULL, obj);
    if (!JSON)
        return NULL;
    if (!JS_DefineProperty(cx, obj, js_JSON_str, OBJECT_TO_JSVAL(JSON),
                           JS_PropertyStub, JS_StrictPropertyStub, 0))
        return NULL;

    if (!JS_DefineFunctions(cx, JSON, json_static_methods))
        return NULL;

    return JSON;
}
