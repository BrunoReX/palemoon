/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
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
 * the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2011
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

#ifndef jsnuminlines_h___
#define jsnuminlines_h___

#include "vm/Unicode.h"

#include "jsstrinlines.h"


namespace js {

template<typename T> struct NumberTraits { };
template<> struct NumberTraits<int32_t> {
  static JS_ALWAYS_INLINE int32_t NaN() { return 0; }
  static JS_ALWAYS_INLINE int32_t toSelfType(int32_t i) { return i; }
  static JS_ALWAYS_INLINE int32_t toSelfType(jsdouble d) { return js_DoubleToECMAUint32(d); }
};
template<> struct NumberTraits<jsdouble> {
  static JS_ALWAYS_INLINE jsdouble NaN() { return js_NaN; }
  static JS_ALWAYS_INLINE jsdouble toSelfType(int32_t i) { return i; }
  static JS_ALWAYS_INLINE jsdouble toSelfType(jsdouble d) { return d; }
};

template<typename T>
static JS_ALWAYS_INLINE bool
StringToNumberType(JSContext *cx, JSString *str, T *result)
{
    size_t length = str->length();
    const jschar *chars = str->getChars(NULL);
    if (!chars)
        return false;

    if (length == 1) {
        jschar c = chars[0];
        if ('0' <= c && c <= '9') {
            *result = NumberTraits<T>::toSelfType(T(c - '0'));
            return true;
        }
        if (unicode::IsSpace(c)) {
            *result = NumberTraits<T>::toSelfType(T(0));
            return true;
        }
        *result = NumberTraits<T>::NaN();
        return true;
    }

    const jschar *end = chars + length;
    const jschar *bp = SkipSpace(chars, end);

    /* ECMA doesn't allow signed hex numbers (bug 273467). */
    if (end - bp >= 2 && bp[0] == '0' && (bp[1] == 'x' || bp[1] == 'X')) {
        /* Looks like a hex number. */
        const jschar *endptr;
        double d;
        if (!GetPrefixInteger(cx, bp + 2, end, 16, &endptr, &d) || SkipSpace(endptr, end) != end) {
            *result = NumberTraits<T>::NaN();
            return true;
        }
        *result = NumberTraits<T>::toSelfType(d);
        return true;
    }

    /*
     * Note that ECMA doesn't treat a string beginning with a '0' as
     * an octal number here. This works because all such numbers will
     * be interpreted as decimal by js_strtod.  Also, any hex numbers
     * that have made it here (which can only be negative ones) will
     * be treated as 0 without consuming the 'x' by js_strtod.
     */
    const jschar *ep;
    double d;
    if (!js_strtod(cx, bp, end, &ep, &d) || SkipSpace(ep, end) != end) {
        *result = NumberTraits<T>::NaN();
        return true;
    }
    *result = NumberTraits<T>::toSelfType(d);
    return true;
}

} // namespace js

#endif /* jsnuminlines_h___ */
