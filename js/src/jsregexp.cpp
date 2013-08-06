/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set sw=4 ts=8 et tw=99:
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
 * JS regular expressions, after Perl.
 */
#include <stdlib.h>
#include <string.h>
#include "jstypes.h"
#include "jsstdint.h"
#include "jsutil.h"
#include "jsapi.h"
#include "jscntxt.h"
#include "jsgc.h"
#include "jsgcmark.h"
#include "jsnum.h"
#include "jsobj.h"
#include "jsregexp.h"
#include "jsstr.h"
#include "jsvector.h"

#include "jsobjinlines.h"
#include "jsregexpinlines.h"

#include "yarr/RegexParser.h"

#ifdef JS_TRACER
#include "jstracer.h"
using namespace nanojit;
#endif

using namespace js;
using namespace js::gc;

/*
 * RegExpStatics allocates memory -- in order to keep the statics stored
 * per-global and not leak, we create a js::Class to wrap the C++ instance and
 * provide an appropriate finalizer. We store an instance of that js::Class in
 * a global reserved slot.
 */

static void
resc_finalize(JSContext *cx, JSObject *obj)
{
    RegExpStatics *res = static_cast<RegExpStatics *>(obj->getPrivate());
    cx->delete_(res);
}

static void
resc_trace(JSTracer *trc, JSObject *obj)
{
    void *pdata = obj->getPrivate();
    JS_ASSERT(pdata);
    RegExpStatics *res = static_cast<RegExpStatics *>(pdata);
    res->mark(trc);
}

Class js::regexp_statics_class = {
    "RegExpStatics",
    JSCLASS_HAS_PRIVATE,
    PropertyStub,         /* addProperty */
    PropertyStub,         /* delProperty */
    PropertyStub,         /* getProperty */
    StrictPropertyStub,   /* setProperty */
    EnumerateStub,
    ResolveStub,
    ConvertStub,
    resc_finalize,
    NULL,                 /* reserved0   */
    NULL,                 /* checkAccess */
    NULL,                 /* call        */
    NULL,                 /* construct   */
    NULL,                 /* xdrObject   */
    NULL,                 /* hasInstance */
    resc_trace
};

/*
 * Replace the regexp internals of |obj| with |newRegExp|.
 * Decref the replaced regexp internals.
 * Note that the refcount of |newRegExp| is unchanged.
 */
static void
SwapObjectRegExp(JSContext *cx, JSObject *obj, AlreadyIncRefed<RegExp> newRegExp)
{
    RegExp *oldRegExp = RegExp::extractFrom(obj);
#ifdef DEBUG
    if (oldRegExp)
        assertSameCompartment(cx, obj, oldRegExp->compartment);
    assertSameCompartment(cx, obj, newRegExp->compartment);
#endif

    /*
     * |obj| isn't a new regular expression, so it won't fail due to failing to
     * define the initial set of properties.
     */
    JS_ALWAYS_TRUE(obj->initRegExp(cx, newRegExp.get()));
    if (oldRegExp)
        oldRegExp->decref(cx);
}

JSObject * JS_FASTCALL
js_CloneRegExpObject(JSContext *cx, JSObject *obj, JSObject *proto)
{
    JS_ASSERT(obj->getClass() == &js_RegExpClass);
    JS_ASSERT(proto);
    JS_ASSERT(proto->getClass() == &js_RegExpClass);

    JSObject *clone = NewNativeClassInstance(cx, &js_RegExpClass, proto, proto->getParent());
    if (!clone)
        return NULL;

    /*
     * This clone functionality does not duplicate the JITted code blob, which is necessary for
     * cross-compartment cloning functionality.
     */
    assertSameCompartment(cx, obj, clone);

    RegExpStatics *res = cx->regExpStatics();
    RegExp *re = RegExp::extractFrom(obj);
    {
        uint32 origFlags = re->getFlags();
        uint32 staticsFlags = res->getFlags();
        if ((origFlags & staticsFlags) != staticsFlags) {
            /*
             * This regex is lacking flags from the statics, so we must recompile with the new
             * flags instead of increffing.
             */
            AlreadyIncRefed<RegExp> clone = RegExp::create(cx, re->getSource(), origFlags | staticsFlags);
            if (!clone)
                return NULL;
            re = clone.get();
        } else {
            re->incref(cx);
        }
    }
    JS_ASSERT(re);
    if (!clone->initRegExp(cx, re))
        return NULL;
    return clone;
}

#ifdef JS_TRACER
JS_DEFINE_CALLINFO_3(extern, OBJECT, js_CloneRegExpObject, CONTEXT, OBJECT, OBJECT, 0,
                     ACCSET_STORE_ANY)
#endif

JSBool
js_ObjectIsRegExp(JSObject *obj)
{
    return obj->isRegExp();
}

/*
 * js::RegExp
 */

void
RegExp::handleYarrError(JSContext *cx, int error)
{
    switch (error) {
      case JSC::Yarr::NoError:
        JS_NOT_REACHED("Precondition violation: an error must have occurred.");
        return;
#define COMPILE_EMSG(__code, __msg) \
      case JSC::Yarr::__code: \
        JS_ReportErrorFlagsAndNumberUC(cx, JSREPORT_ERROR, js_GetErrorMessage, NULL, __msg); \
        return
      COMPILE_EMSG(PatternTooLarge, JSMSG_REGEXP_TOO_COMPLEX);
      COMPILE_EMSG(QuantifierOutOfOrder, JSMSG_BAD_QUANTIFIER);
      COMPILE_EMSG(QuantifierWithoutAtom, JSMSG_BAD_QUANTIFIER);
      COMPILE_EMSG(MissingParentheses, JSMSG_MISSING_PAREN);
      COMPILE_EMSG(ParenthesesUnmatched, JSMSG_UNMATCHED_RIGHT_PAREN);
      COMPILE_EMSG(ParenthesesTypeInvalid, JSMSG_BAD_QUANTIFIER); /* "(?" with bad next char */
      COMPILE_EMSG(CharacterClassUnmatched, JSMSG_BAD_CLASS_RANGE);
      COMPILE_EMSG(CharacterClassOutOfOrder, JSMSG_BAD_CLASS_RANGE);
      COMPILE_EMSG(CharacterClassRangeSingleChar, JSMSG_BAD_CLASS_RANGE);
      COMPILE_EMSG(EscapeUnterminated, JSMSG_TRAILING_SLASH);
      COMPILE_EMSG(QuantifierTooLarge, JSMSG_BAD_QUANTIFIER);
      COMPILE_EMSG(HitRecursionLimit, JSMSG_REGEXP_TOO_COMPLEX);
#undef COMPILE_EMSG
      default:
        JS_NOT_REACHED("Precondition violation: unknown Yarr error code.");
    }
}

void
RegExp::handlePCREError(JSContext *cx, int error)
{
#define REPORT(msg_) \
    JS_ReportErrorFlagsAndNumberUC(cx, JSREPORT_ERROR, js_GetErrorMessage, NULL, msg_); \
    return
    switch (error) {
      case -2: REPORT(JSMSG_REGEXP_TOO_COMPLEX);
      case 0: JS_NOT_REACHED("Precondition violation: an error must have occurred.");
      case 1: REPORT(JSMSG_TRAILING_SLASH);
      case 2: REPORT(JSMSG_TRAILING_SLASH);
      case 3: REPORT(JSMSG_REGEXP_TOO_COMPLEX);
      case 4: REPORT(JSMSG_BAD_QUANTIFIER);
      case 5: REPORT(JSMSG_BAD_QUANTIFIER);
      case 6: REPORT(JSMSG_BAD_CLASS_RANGE);
      case 7: REPORT(JSMSG_REGEXP_TOO_COMPLEX);
      case 8: REPORT(JSMSG_BAD_CLASS_RANGE);
      case 9: REPORT(JSMSG_BAD_QUANTIFIER);
      case 10: REPORT(JSMSG_UNMATCHED_RIGHT_PAREN);
      case 11: REPORT(JSMSG_REGEXP_TOO_COMPLEX);
      case 12: REPORT(JSMSG_UNMATCHED_RIGHT_PAREN);
      case 13: REPORT(JSMSG_REGEXP_TOO_COMPLEX);
      case 14: REPORT(JSMSG_MISSING_PAREN);
      case 15: REPORT(JSMSG_BAD_BACKREF);
      case 16: REPORT(JSMSG_REGEXP_TOO_COMPLEX);
      case 17: REPORT(JSMSG_REGEXP_TOO_COMPLEX);
      default:
        JS_NOT_REACHED("Precondition violation: unknown PCRE error code.");
    }
#undef REPORT
}

bool
RegExp::parseFlags(JSContext *cx, JSString *flagStr, uintN *flagsOut)
{
    size_t n = flagStr->length();
    const jschar *s = flagStr->getChars(cx);
    if (!s)
        return false;

    *flagsOut = 0;
    for (size_t i = 0; i < n; i++) {
#define HANDLE_FLAG(name_)                                                    \
        JS_BEGIN_MACRO                                                        \
            if (*flagsOut & (name_))                                          \
                goto bad_flag;                                                \
            *flagsOut |= (name_);                                             \
        JS_END_MACRO
        switch (s[i]) {
          case 'i': HANDLE_FLAG(JSREG_FOLD); break;
          case 'g': HANDLE_FLAG(JSREG_GLOB); break;
          case 'm': HANDLE_FLAG(JSREG_MULTILINE); break;
          case 'y': HANDLE_FLAG(JSREG_STICKY); break;
          default:
          bad_flag:
          {
            char charBuf[2];
            charBuf[0] = char(s[i]);
            charBuf[1] = '\0';
            JS_ReportErrorFlagsAndNumber(cx, JSREPORT_ERROR, js_GetErrorMessage, NULL,
                                         JSMSG_BAD_REGEXP_FLAG, charBuf);
            return false;
          }
        }
#undef HANDLE_FLAG
    }
    return true;
}

AlreadyIncRefed<RegExp>
RegExp::createFlagged(JSContext *cx, JSString *str, JSString *opt)
{
    if (!opt)
        return create(cx, str, 0);
    uintN flags = 0;
    if (!parseFlags(cx, opt, &flags))
        return AlreadyIncRefed<RegExp>(NULL);
    return create(cx, str, flags);
}

const Shape *
JSObject::assignInitialRegExpShape(JSContext *cx)
{
    JS_ASSERT(!cx->compartment->initialRegExpShape);
    JS_ASSERT(isRegExp());
    JS_ASSERT(nativeEmpty());

    JS_STATIC_ASSERT(JSSLOT_REGEXP_LAST_INDEX == 0);
    JS_STATIC_ASSERT(JSSLOT_REGEXP_SOURCE == JSSLOT_REGEXP_LAST_INDEX + 1);
    JS_STATIC_ASSERT(JSSLOT_REGEXP_GLOBAL == JSSLOT_REGEXP_SOURCE + 1);
    JS_STATIC_ASSERT(JSSLOT_REGEXP_IGNORE_CASE == JSSLOT_REGEXP_GLOBAL + 1);
    JS_STATIC_ASSERT(JSSLOT_REGEXP_MULTILINE == JSSLOT_REGEXP_IGNORE_CASE + 1);
    JS_STATIC_ASSERT(JSSLOT_REGEXP_STICKY == JSSLOT_REGEXP_MULTILINE + 1);

    /* The lastIndex property alone is writable but non-configurable. */
    if (!addDataProperty(cx, ATOM_TO_JSID(cx->runtime->atomState.lastIndexAtom),
                         JSSLOT_REGEXP_LAST_INDEX, JSPROP_PERMANENT))
    {
        return NULL;
    }

    /* Remaining instance properties are non-writable and non-configurable. */
    if (!addDataProperty(cx, ATOM_TO_JSID(cx->runtime->atomState.sourceAtom),
                         JSSLOT_REGEXP_SOURCE, JSPROP_PERMANENT | JSPROP_READONLY) ||
        !addDataProperty(cx, ATOM_TO_JSID(cx->runtime->atomState.globalAtom),
                         JSSLOT_REGEXP_GLOBAL, JSPROP_PERMANENT | JSPROP_READONLY) ||
        !addDataProperty(cx, ATOM_TO_JSID(cx->runtime->atomState.ignoreCaseAtom),
                         JSSLOT_REGEXP_IGNORE_CASE, JSPROP_PERMANENT | JSPROP_READONLY) ||
        !addDataProperty(cx, ATOM_TO_JSID(cx->runtime->atomState.multilineAtom),
                         JSSLOT_REGEXP_MULTILINE, JSPROP_PERMANENT | JSPROP_READONLY))
    {
        return NULL;
    }

    return addDataProperty(cx, ATOM_TO_JSID(cx->runtime->atomState.stickyAtom),
                           JSSLOT_REGEXP_STICKY, JSPROP_PERMANENT | JSPROP_READONLY);
}

/*
 * RegExp static properties.
 *
 * RegExp class static properties and their Perl counterparts:
 *
 *  RegExp.input                $_
 *  RegExp.multiline            $*
 *  RegExp.lastMatch            $&
 *  RegExp.lastParen            $+
 *  RegExp.leftContext          $`
 *  RegExp.rightContext         $'
 */

#define DEFINE_STATIC_GETTER(name, code)                                        \
    static JSBool                                                               \
    name(JSContext *cx, JSObject *obj, jsid id, jsval *vp)                      \
    {                                                                           \
        RegExpStatics *res = cx->regExpStatics();                               \
        code;                                                                   \
    }

DEFINE_STATIC_GETTER(static_input_getter,        return res->createPendingInput(cx, Valueify(vp)))
DEFINE_STATIC_GETTER(static_multiline_getter,    *vp = BOOLEAN_TO_JSVAL(res->multiline());
                                                 return true)
DEFINE_STATIC_GETTER(static_lastMatch_getter,    return res->createLastMatch(cx, Valueify(vp)))
DEFINE_STATIC_GETTER(static_lastParen_getter,    return res->createLastParen(cx, Valueify(vp)))
DEFINE_STATIC_GETTER(static_leftContext_getter,  return res->createLeftContext(cx, Valueify(vp)))
DEFINE_STATIC_GETTER(static_rightContext_getter, return res->createRightContext(cx, Valueify(vp)))

DEFINE_STATIC_GETTER(static_paren1_getter,       return res->createParen(cx, 1, Valueify(vp)))
DEFINE_STATIC_GETTER(static_paren2_getter,       return res->createParen(cx, 2, Valueify(vp)))
DEFINE_STATIC_GETTER(static_paren3_getter,       return res->createParen(cx, 3, Valueify(vp)))
DEFINE_STATIC_GETTER(static_paren4_getter,       return res->createParen(cx, 4, Valueify(vp)))
DEFINE_STATIC_GETTER(static_paren5_getter,       return res->createParen(cx, 5, Valueify(vp)))
DEFINE_STATIC_GETTER(static_paren6_getter,       return res->createParen(cx, 6, Valueify(vp)))
DEFINE_STATIC_GETTER(static_paren7_getter,       return res->createParen(cx, 7, Valueify(vp)))
DEFINE_STATIC_GETTER(static_paren8_getter,       return res->createParen(cx, 8, Valueify(vp)))
DEFINE_STATIC_GETTER(static_paren9_getter,       return res->createParen(cx, 9, Valueify(vp)))

#define DEFINE_STATIC_SETTER(name, code)                                        \
    static JSBool                                                               \
    name(JSContext *cx, JSObject *obj, jsid id, JSBool strict, jsval *vp)       \
    {                                                                           \
        RegExpStatics *res = cx->regExpStatics();                               \
        code;                                                                   \
        return true;                                                            \
    }

DEFINE_STATIC_SETTER(static_input_setter,
                     if (!JSVAL_IS_STRING(*vp) && !JS_ConvertValue(cx, *vp, JSTYPE_STRING, vp))
                         return false;
                     res->setPendingInput(JSVAL_TO_STRING(*vp)))
DEFINE_STATIC_SETTER(static_multiline_setter,
                     if (!JSVAL_IS_BOOLEAN(*vp) && !JS_ConvertValue(cx, *vp, JSTYPE_BOOLEAN, vp))
                         return false;
                     res->setMultiline(!!JSVAL_TO_BOOLEAN(*vp)))

const uint8 REGEXP_STATIC_PROP_ATTRS    = JSPROP_PERMANENT | JSPROP_SHARED | JSPROP_ENUMERATE;
const uint8 RO_REGEXP_STATIC_PROP_ATTRS = REGEXP_STATIC_PROP_ATTRS | JSPROP_READONLY;

static JSPropertySpec regexp_static_props[] = {
    {"input",        0, REGEXP_STATIC_PROP_ATTRS,    static_input_getter, static_input_setter},
    {"multiline",    0, REGEXP_STATIC_PROP_ATTRS,    static_multiline_getter,
                                                     static_multiline_setter},
    {"lastMatch",    0, RO_REGEXP_STATIC_PROP_ATTRS, static_lastMatch_getter,    NULL},
    {"lastParen",    0, RO_REGEXP_STATIC_PROP_ATTRS, static_lastParen_getter,    NULL},
    {"leftContext",  0, RO_REGEXP_STATIC_PROP_ATTRS, static_leftContext_getter,  NULL},
    {"rightContext", 0, RO_REGEXP_STATIC_PROP_ATTRS, static_rightContext_getter, NULL},
    {"$1",           0, RO_REGEXP_STATIC_PROP_ATTRS, static_paren1_getter,       NULL},
    {"$2",           0, RO_REGEXP_STATIC_PROP_ATTRS, static_paren2_getter,       NULL},
    {"$3",           0, RO_REGEXP_STATIC_PROP_ATTRS, static_paren3_getter,       NULL},
    {"$4",           0, RO_REGEXP_STATIC_PROP_ATTRS, static_paren4_getter,       NULL},
    {"$5",           0, RO_REGEXP_STATIC_PROP_ATTRS, static_paren5_getter,       NULL},
    {"$6",           0, RO_REGEXP_STATIC_PROP_ATTRS, static_paren6_getter,       NULL},
    {"$7",           0, RO_REGEXP_STATIC_PROP_ATTRS, static_paren7_getter,       NULL},
    {"$8",           0, RO_REGEXP_STATIC_PROP_ATTRS, static_paren8_getter,       NULL},
    {"$9",           0, RO_REGEXP_STATIC_PROP_ATTRS, static_paren9_getter,       NULL},
    {0,0,0,0,0}
};

static void
regexp_finalize(JSContext *cx, JSObject *obj)
{
    RegExp *re = RegExp::extractFrom(obj);
    if (!re)
        return;
    re->decref(cx);
}

#if JS_HAS_XDR

#include "jsxdrapi.h"

JSBool
js_XDRRegExpObject(JSXDRState *xdr, JSObject **objp)
{
    JSString *source = 0;
    uint32 flagsword = 0;

    if (xdr->mode == JSXDR_ENCODE) {
        JS_ASSERT(objp);
        RegExp *re = RegExp::extractFrom(*objp);
        if (!re)
            return false;
        source = re->getSource();
        flagsword = re->getFlags();
    }
    if (!JS_XDRString(xdr, &source) || !JS_XDRUint32(xdr, &flagsword))
        return false;
    if (xdr->mode == JSXDR_DECODE) {
        JSObject *obj = NewBuiltinClassInstance(xdr->cx, &js_RegExpClass);
        if (!obj)
            return false;
        obj->clearParent();
        obj->clearProto();

        /*
         * initRegExp can GC before storing re in the private field of the
         * object. At that point the only reference to the source string could
         * be from the malloc-allocated GC-invisible re. So we must anchor.
         */
        JS::Anchor<JSString *> anchor(source);
        AlreadyIncRefed<RegExp> re = RegExp::create(xdr->cx, source, flagsword);
        if (!re)
            return false;
        if (!obj->initRegExp(xdr->cx, re.get()))
            return false;
        *objp = obj;
    }
    return true;
}

#else  /* !JS_HAS_XDR */

#define js_XDRRegExpObject NULL

#endif /* !JS_HAS_XDR */

js::Class js_RegExpClass = {
    js_RegExp_str,
    JSCLASS_HAS_PRIVATE |
    JSCLASS_HAS_RESERVED_SLOTS(JSObject::REGEXP_CLASS_RESERVED_SLOTS) |
    JSCLASS_HAS_CACHED_PROTO(JSProto_RegExp),
    PropertyStub,         /* addProperty */
    PropertyStub,         /* delProperty */
    PropertyStub,         /* getProperty */
    StrictPropertyStub,   /* setProperty */
    EnumerateStub,        /* enumerate */
    ResolveStub,
    ConvertStub,
    regexp_finalize,
    NULL,                 /* reserved0 */
    NULL,                 /* checkAccess */
    NULL,                 /* call */
    NULL,                 /* construct */
    js_XDRRegExpObject,
    NULL,                 /* hasInstance */
    NULL                  /* trace */
};

/*
 * RegExp instance methods.
 */

JSBool
js_regexp_toString(JSContext *cx, JSObject *obj, Value *vp)
{
    if (!obj->isRegExp()) {
        ReportIncompatibleMethod(cx, vp, &js_RegExpClass);
        return false;
    }

    RegExp *re = RegExp::extractFrom(obj);
    if (!re) {
        *vp = StringValue(cx->runtime->emptyString);
        return true;
    }

    JSLinearString *src = re->getSource();
    StringBuffer sb(cx);
    if (size_t len = src->length()) {
        if (!sb.reserve(len + 2))
            return false;
        sb.infallibleAppend('/');
        sb.infallibleAppend(src->chars(), len);
        sb.infallibleAppend('/');
    } else {
        if (!sb.append("/(?:)/"))
            return false;
    }
    if (re->global() && !sb.append('g'))
        return false;
    if (re->ignoreCase() && !sb.append('i'))
        return false;
    if (re->multiline() && !sb.append('m'))
        return false;
    if (re->sticky() && !sb.append('y'))
        return false;

    JSFlatString *str = sb.finishString();
    if (!str)
        return false;
    *vp = StringValue(str);
    return true;
}

static JSBool
regexp_toString(JSContext *cx, uintN argc, Value *vp)
{
    JSObject *obj = ToObject(cx, &vp[1]);
    if (!obj)
        return false;
    return js_regexp_toString(cx, obj, vp);
}

/*
 * Return:
 * - The original if no escaping need be performed.
 * - A new string if escaping need be performed.
 * - NULL on error.
 */
static JSString *
EscapeNakedForwardSlashes(JSContext *cx, JSString *unescaped)
{
    size_t oldLen = unescaped->length();
    const jschar *oldChars = unescaped->getChars(cx);
    if (!oldChars)
        return NULL;
    JS::Anchor<JSString *> anchor(unescaped);

    js::Vector<jschar, 128> newChars(cx);
    for (const jschar *it = oldChars; it < oldChars + oldLen; ++it) {
        if (*it == '/' && (it == oldChars || it[-1] != '\\')) {
            if (!newChars.length()) {
                if (!newChars.reserve(oldLen + 1))
                    return NULL;
                newChars.infallibleAppend(oldChars, size_t(it - oldChars));
            }
            if (!newChars.append('\\'))
                return NULL;
        }

        if (!newChars.empty() && !newChars.append(*it))
            return NULL;
    }

    if (newChars.empty())
        return unescaped;

    size_t len = newChars.length();
    if (!newChars.append('\0'))
        return NULL;
    jschar *chars = newChars.extractRawBuffer();
    JSString *escaped = js_NewString(cx, chars, len);
    if (!escaped)
        cx->free_(chars);
    return escaped;
}

static bool
SwapRegExpInternals(JSContext *cx, JSObject *obj, Value *rval, JSString *str, uint32 flags = 0)
{
    flags |= cx->regExpStatics()->getFlags();
    AlreadyIncRefed<RegExp> re = RegExp::create(cx, str, flags);
    if (!re)
        return false;
    SwapObjectRegExp(cx, obj, re);
    *rval = ObjectValue(*obj);
    return true;
}

enum ExecType { RegExpExec, RegExpTest };

/*
 * ES5 15.10.6.2 (and 15.10.6.3, which calls 15.10.6.2).
 *
 * RegExp.prototype.test doesn't need to create a results array, and we use
 * |execType| to perform this optimization.
 */
static JSBool
ExecuteRegExp(JSContext *cx, ExecType execType, uintN argc, Value *vp)
{
    /* Step 1. */
    JSObject *obj = ToObject(cx, &vp[1]);
    if (!obj)
        return false;
    if (!obj->isRegExp()) {
        JSFunction *fun = vp[0].toObject().getFunctionPrivate();
        JSAutoByteString funNameBytes;
        if (const char *funName = GetFunctionNameBytes(cx, fun, &funNameBytes)) {
            JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_INCOMPATIBLE_PROTO,
                                 "RegExp", funName, obj->getClass()->name);
        }
        return false;
    }

    RegExp *re = RegExp::extractFrom(obj);
    if (!re)
        return true;

    /*
     * Code execution under this call could swap out the guts of |obj|, so we
     * have to take a defensive refcount here.
     */
    AutoRefCount<RegExp> arc(cx, NeedsIncRef<RegExp>(re));
    RegExpStatics *res = cx->regExpStatics();

    /* Step 2. */
    JSString *input;
    if (argc > 0) {
        input = js_ValueToString(cx, vp[2]);
        if (!input)
            return false;
        vp[2] = StringValue(input);
    } else {
        /* NON-STANDARD: Grab input from statics. */
        input = res->getPendingInput();
        if (!input) {
            JSAutoByteString sourceBytes(cx, re->getSource());
            if (!!sourceBytes) {
                JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_NO_INPUT,
                                     sourceBytes.ptr(),
                                     re->global() ? "g" : "",
                                     re->ignoreCase() ? "i" : "",
                                     re->multiline() ? "m" : "",
                                     re->sticky() ? "y" : "");
            }
            return false;
        }
    }

    /* Step 3. */
    size_t length = input->length();

    /* Step 4. */
    const Value &lastIndex = obj->getRegExpLastIndex();

    /* Step 5. */
    jsdouble i;
    if (!ToInteger(cx, lastIndex, &i))
        return false;

    /* Steps 6-7 (with sticky extension). */
    if (!re->global() && !re->sticky())
        i = 0;

    /* Step 9a. */
    if (i < 0 || i > length) {
        obj->zeroRegExpLastIndex();
        *vp = NullValue();
        return true;
    }

    /* Steps 8-21. */
    size_t lastIndexInt(i);
    if (!re->execute(cx, res, input, &lastIndexInt, execType == RegExpTest, vp))
        return false;

    /* Step 11 (with sticky extension). */
    if (re->global() || (!vp->isNull() && re->sticky())) {
        if (vp->isNull())
            obj->zeroRegExpLastIndex();
        else
            obj->setRegExpLastIndex(lastIndexInt);
    }

    return true;
}

/* ES5 15.10.6.2. */
JSBool
js_regexp_exec(JSContext *cx, uintN argc, Value *vp)
{
    return ExecuteRegExp(cx, RegExpExec, argc, vp);
}

/* ES5 15.10.6.3. */
JSBool
js_regexp_test(JSContext *cx, uintN argc, Value *vp)
{
    if (!ExecuteRegExp(cx, RegExpTest, argc, vp))
        return false;
    if (!vp->isTrue())
        vp->setBoolean(false);
    return true;
}

/*
 * Compile new js::RegExp guts for obj.
 *
 * Per ECMAv5 15.10.4.1, we act on combinations of (pattern, flags) as
 * arguments:
 *
 *  RegExp, undefined => flags := pattern.flags
 *  RegExp, _ => throw TypeError
 *  _ => pattern := ToString(pattern) if defined(pattern) else ''
 *       flags := ToString(flags) if defined(flags) else ''
 */
static bool
CompileRegExpAndSwap(JSContext *cx, JSObject *obj, uintN argc, Value *argv, Value *rval)
{
    if (argc == 0)
        return SwapRegExpInternals(cx, obj, rval, cx->runtime->emptyString);

    Value sourceValue = argv[0];
    if (sourceValue.isObject() && sourceValue.toObject().getClass() == &js_RegExpClass) {
        /*
         * If we get passed in a RegExp object we return a new object with the
         * same RegExp (internal matcher program) guts.
         * Note: the regexp static flags are not taken into consideration here.
         */
        JSObject &sourceObj = sourceValue.toObject();
        if (argc >= 2 && !argv[1].isUndefined()) {
            JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_NEWREGEXP_FLAGGED);
            return false;
        }
        RegExp *re = RegExp::extractFrom(&sourceObj);
        if (!re)
            return false;

        re->incref(cx);
        SwapObjectRegExp(cx, obj, AlreadyIncRefed<RegExp>(re));

        *rval = ObjectValue(*obj);
        return true;
    }

    JSString *sourceStr;
    if (sourceValue.isUndefined()) {
        sourceStr = cx->runtime->emptyString;
    } else {
        /* Coerce to string and compile. */
        sourceStr = js_ValueToString(cx, sourceValue);
        if (!sourceStr)
            return false;
    }

    uintN flags = 0;
    if (argc > 1 && !argv[1].isUndefined()) {
        JSString *flagStr = js_ValueToString(cx, argv[1]);
        if (!flagStr)
            return false;
        argv[1].setString(flagStr);
        if (!RegExp::parseFlags(cx, flagStr, &flags))
            return false;
    }

    JSString *escapedSourceStr = EscapeNakedForwardSlashes(cx, sourceStr);
    if (!escapedSourceStr)
        return false;

    return SwapRegExpInternals(cx, obj, rval, escapedSourceStr, flags);
}

static JSBool
regexp_compile(JSContext *cx, uintN argc, Value *vp)
{
    JSObject *obj = ToObject(cx, &vp[1]);
    if (!obj)
        return false;
    if (!obj->isRegExp()) {
        ReportIncompatibleMethod(cx, vp, &js_RegExpClass);
        return false;
    }

    return CompileRegExpAndSwap(cx, obj, argc, JS_ARGV(cx, vp), &JS_RVAL(cx, vp));
}

static JSBool
regexp_construct(JSContext *cx, uintN argc, Value *vp)
{
    Value *argv = JS_ARGV(cx, vp);

    if (!IsConstructing(vp)) {
        /*
         * If first arg is regexp and no flags are given, just return the arg.
         * Otherwise, delegate to the standard constructor.
         * See ECMAv5 15.10.3.1.
         */
        if (argc >= 1 && argv[0].isObject() && argv[0].toObject().isRegExp() &&
            (argc == 1 || argv[1].isUndefined())) {
            *vp = argv[0];
            return true;
        }
    }

    JSObject *obj = NewBuiltinClassInstance(cx, &js_RegExpClass);
    if (!obj)
        return false;

    return CompileRegExpAndSwap(cx, obj, argc, argv, &JS_RVAL(cx, vp));
}

static JSFunctionSpec regexp_methods[] = {
#if JS_HAS_TOSOURCE
    JS_FN(js_toSource_str,  regexp_toString,    0,0),
#endif
    JS_FN(js_toString_str,  regexp_toString,    0,0),
    JS_FN("compile",        regexp_compile,     2,0),
    JS_FN("exec",           js_regexp_exec,     1,0),
    JS_FN("test",           js_regexp_test,     1,0),
    JS_FS_END
};

JSObject *
js_InitRegExpClass(JSContext *cx, JSObject *global)
{
    JS_ASSERT(global->isGlobal());
    JS_ASSERT(global->isNative());

    /* Create and initialize RegExp.prototype. */
    JSObject *objectProto;
    if (!js_GetClassPrototype(cx, global, JSProto_Object, &objectProto))
        return NULL;
    JS_ASSERT(objectProto);

    JSObject *proto = NewObject<WithProto::Class>(cx, &js_RegExpClass, objectProto, global);
    if (!proto)
        return NULL;

    AlreadyIncRefed<RegExp> re = RegExp::create(cx, cx->runtime->emptyString, 0);
    if (!re)
        return NULL;
#ifdef DEBUG
    assertSameCompartment(cx, proto, re->compartment);
#endif

    /*
     * Associate the empty regular expression with RegExp.prototype, and define
     * the initial non-method properties of any regular expression instance.
     * These must be added before methods to preserve slot layout.
     */
    if (!proto->initRegExp(cx, re.get()))
        return NULL;

    /*
     * Now add the standard methods to RegExp.prototype, and pre-brand for
     * better shape-guarding code.
     */
    if (!JS_DefineFunctions(cx, proto, regexp_methods))
        return NULL;
    proto->brand(cx);

    /* Create the RegExp constructor. */
    JSAtom *regExpAtom = CLASS_ATOM(cx, RegExp);
    JSFunction *ctor =
        js_NewFunction(cx, NULL, regexp_construct, 2, JSFUN_CONSTRUCTOR, global, regExpAtom);
    if (!ctor)
        return NULL;

    /* RegExp creates regular expressions. */
    FUN_CLASP(ctor) = &js_RegExpClass;

    /* Define RegExp.prototype and RegExp.prototype.constructor. */
    if (!ctor->defineProperty(cx, ATOM_TO_JSID(cx->runtime->atomState.classPrototypeAtom),
                              ObjectValue(*proto), PropertyStub, StrictPropertyStub,
                              JSPROP_PERMANENT | JSPROP_READONLY) ||
        !proto->defineProperty(cx, ATOM_TO_JSID(cx->runtime->atomState.constructorAtom),
                               ObjectValue(*ctor), PropertyStub, StrictPropertyStub, 0)) {
        return NULL;
    }

    /* Add static properties to the RegExp constructor. */
    if (!JS_DefineProperties(cx, ctor, regexp_static_props) ||
        !JS_AliasProperty(cx, ctor, "input",        "$_") ||
        !JS_AliasProperty(cx, ctor, "multiline",    "$*") ||
        !JS_AliasProperty(cx, ctor, "lastMatch",    "$&") ||
        !JS_AliasProperty(cx, ctor, "lastParen",    "$+") ||
        !JS_AliasProperty(cx, ctor, "leftContext",  "$`") ||
        !JS_AliasProperty(cx, ctor, "rightContext", "$'")) {
        return NULL;
    }

    /*
     * Make sure proto's emptyShape is available to be shared by objects of
     * this class.  JSObject::emptyShape is a one-slot cache. If we omit this,
     * some other class could snap it up. (The risk is particularly great for
     * Object.prototype.)
     *
     * All callers of JSObject::initSharingEmptyShape depend on this.
     */
    if (!proto->getEmptyShape(cx, &js_RegExpClass, FINALIZE_OBJECT0))
        return NULL;

    /* Install the fully-constructed RegExp and RegExp.prototype in global. */
    if (!DefineConstructorAndPrototype(cx, global, JSProto_RegExp, ctor, proto))
        return NULL;

    return proto;
}
