/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=79 ft=cpp:
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
 * The Original Code is SpiderMonkey JavaScript engine.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Luke Wagner <luke@mozilla.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#include "String.h"
#include "String-inl.h"

using namespace js;

bool
JSString::isShort() const
{
    bool is_short = arenaHeader()->getThingKind() == gc::FINALIZE_SHORT_STRING;
    JS_ASSERT_IF(is_short, isFlat());
    return is_short;
}

bool
JSString::isFixed() const
{
    return isFlat() && !isExtensible();
}

bool
JSString::isInline() const
{
    return isFixed() && (d.u1.chars == d.inlineStorage || isShort());
}

bool
JSString::isExternal() const
{
    bool is_external = arenaHeader()->getThingKind() == gc::FINALIZE_EXTERNAL_STRING;
    JS_ASSERT_IF(is_external, isFixed());
    return is_external;
}

void
JSLinearString::mark(JSTracer *)
{
    JSLinearString *str = this;
    while (!str->isStaticAtom() && str->markIfUnmarked() && str->isDependent())
        str = str->asDependent().base();
}

size_t
JSString::charsHeapSize()
{
    /* JSRope: do nothing, we'll count all children chars when we hit the leaf strings. */
    if (isRope())
        return 0;

    JS_ASSERT(isLinear());

    /* JSDependentString: do nothing, we'll count the chars when we hit the base string. */
    if (isDependent())
        return 0;

    JS_ASSERT(isFlat());

    /* JSExtensibleString: count the full capacity, not just the used space. */
    if (isExtensible())
        return asExtensible().capacity() * sizeof(jschar);

    JS_ASSERT(isFixed());

    /* JSExternalString: don't count, the chars could be stored anywhere. */
    if (isExternal())
        return 0;

    /* JSInlineString, JSShortString, JSInlineAtom, JSShortAtom: the chars are inline. */
    if (isInline())
        return 0;

    /* JSStaticAtom: the chars are static and so not part of the heap. */
    if (isStaticAtom())
        return 0;

    /* JSAtom, JSFixedString: count the chars. */
    return length() * sizeof(jschar);
}

static JS_ALWAYS_INLINE size_t
RopeCapacityFor(size_t length)
{
    static const size_t ROPE_DOUBLING_MAX = 1024 * 1024;

    /*
     * Grow by 12.5% if the buffer is very large. Otherwise, round up to the
     * next power of 2. This is similar to what we do with arrays; see
     * JSObject::ensureDenseArrayElements.
     */
    if (length > ROPE_DOUBLING_MAX)
        return length + (length / 8);
    return RoundUpPow2(length);
}

static JS_ALWAYS_INLINE jschar *
AllocChars(JSContext *maybecx, size_t wholeCapacity)
{
    /* +1 for the null char at the end. */
    JS_STATIC_ASSERT(JSString::MAX_LENGTH * sizeof(jschar) < UINT32_MAX);
    size_t bytes = (wholeCapacity + 1) * sizeof(jschar);
    if (maybecx)
        return (jschar *)maybecx->malloc_(bytes);
    return (jschar *)OffTheBooks::malloc_(bytes);
}

JSFlatString *
JSRope::flatten(JSContext *maybecx)
{
    /*
     * Perform a depth-first dag traversal, splatting each node's characters
     * into a contiguous buffer. Visit each rope node three times:
     *   1. record position in the buffer and recurse into left child;
     *   2. recurse into the right child;
     *   3. transform the node into a dependent string.
     * To avoid maintaining a stack, tree nodes are mutated to indicate how many
     * times they have been visited. Since ropes can be dags, a node may be
     * encountered multiple times during traversal. However, step 3 above leaves
     * a valid dependent string, so everything works out. This algorithm is
     * homomorphic to marking code.
     *
     * While ropes avoid all sorts of quadratic cases with string
     * concatenation, they can't help when ropes are immediately flattened.
     * One idiomatic case that we'd like to keep linear (and has traditionally
     * been linear in SM and other JS engines) is:
     *
     *   while (...) {
     *     s += ...
     *     s.flatten
     *   }
     *
     * To do this, when the buffer for a to-be-flattened rope is allocated, the
     * allocation size is rounded up. Then, if the resulting flat string is the
     * left-hand side of a new rope that gets flattened and there is enough
     * capacity, the rope is flattened into the same buffer, thereby avoiding
     * copying the left-hand side. Clearing the 'extensible' bit turns off this
     * optimization. This is necessary, e.g., when the JSAPI hands out the raw
     * null-terminated char array of a flat string.
     *
     * N.B. This optimization can create chains of dependent strings.
     */
    const size_t wholeLength = length();
    size_t wholeCapacity;
    jschar *wholeChars;
    JSString *str = this;
    jschar *pos;

    if (this->leftChild()->isExtensible()) {
        JSExtensibleString &left = this->leftChild()->asExtensible();
        size_t capacity = left.capacity();
        if (capacity >= wholeLength) {
            wholeCapacity = capacity;
            wholeChars = const_cast<jschar *>(left.chars());
            size_t bits = left.d.lengthAndFlags;
            pos = wholeChars + (bits >> LENGTH_SHIFT);
            left.d.lengthAndFlags = bits ^ (EXTENSIBLE_FLAGS | DEPENDENT_BIT);
            left.d.s.u2.base = (JSLinearString *)this;  /* will be true on exit */
            goto visit_right_child;
        }
    }

    wholeCapacity = RopeCapacityFor(wholeLength);
    wholeChars = AllocChars(maybecx, wholeCapacity);
    if (!wholeChars)
        return NULL;

    pos = wholeChars;
    first_visit_node: {
        JSString &left = *str->d.u1.left;
        str->d.u1.chars = pos;
        if (left.isRope()) {
            left.d.s.u3.parent = str;          /* Return to this when 'left' done, */
            left.d.lengthAndFlags = 0x200;     /* but goto visit_right_child. */
            str = &left;
            goto first_visit_node;
        }
        size_t len = left.length();
        PodCopy(pos, left.d.u1.chars, len);
        pos += len;
    }
    visit_right_child: {
        JSString &right = *str->d.s.u2.right;
        if (right.isRope()) {
            right.d.s.u3.parent = str;         /* Return to this node when 'right' done, */
            right.d.lengthAndFlags = 0x300;    /* but goto finish_node. */
            str = &right;
            goto first_visit_node;
        }
        size_t len = right.length();
        PodCopy(pos, right.d.u1.chars, len);
        pos += len;
    }
    finish_node: {
        if (str == this) {
            JS_ASSERT(pos == wholeChars + wholeLength);
            *pos = '\0';
            str->d.lengthAndFlags = buildLengthAndFlags(wholeLength, EXTENSIBLE_FLAGS);
            str->d.u1.chars = wholeChars;
            str->d.s.u2.capacity = wholeCapacity;
            return &this->asFlat();
        }
        size_t progress = str->d.lengthAndFlags;
        str->d.lengthAndFlags = buildLengthAndFlags(pos - str->d.u1.chars, DEPENDENT_BIT);
        str->d.s.u2.base = (JSLinearString *)this;       /* will be true on exit */
        str = str->d.s.u3.parent;
        if (progress == 0x200)
            goto visit_right_child;
        JS_ASSERT(progress == 0x300);
        goto finish_node;
    }
}

JSString * JS_FASTCALL
js_ConcatStrings(JSContext *cx, JSString *left, JSString *right)
{
    JS_ASSERT_IF(!left->isAtom(), left->compartment() == cx->compartment);
    JS_ASSERT_IF(!right->isAtom(), right->compartment() == cx->compartment);

    size_t leftLen = left->length();
    if (leftLen == 0)
        return right;

    size_t rightLen = right->length();
    if (rightLen == 0)
        return left;

    size_t wholeLength = leftLen + rightLen;

    if (JSShortString::lengthFits(wholeLength)) {
        JSShortString *str = js_NewGCShortString(cx);
        if (!str)
            return NULL;
        const jschar *leftChars = left->getChars(cx);
        if (!leftChars)
            return NULL;
        const jschar *rightChars = right->getChars(cx);
        if (!rightChars)
            return NULL;

        jschar *buf = str->init(wholeLength);
        PodCopy(buf, leftChars, leftLen);
        PodCopy(buf + leftLen, rightChars, rightLen);
        buf[wholeLength] = 0;
        return str;
    }

    if (wholeLength > JSString::MAX_LENGTH) {
        if (JS_ON_TRACE(cx)) {
            if (!CanLeaveTrace(cx))
                return NULL;
            LeaveTrace(cx);
        }
        js_ReportAllocationOverflow(cx);
        return NULL;
    }

    return JSRope::new_(cx, left, right, wholeLength);
}

JSFixedString *
JSDependentString::undepend(JSContext *cx)
{
    JS_ASSERT(isDependent());

    size_t n = length();
    size_t size = (n + 1) * sizeof(jschar);
    jschar *s = (jschar *) cx->malloc_(size);
    if (!s)
        return NULL;

    PodCopy(s, chars(), n);
    s[n] = 0;

    d.lengthAndFlags = buildLengthAndFlags(n, FIXED_FLAGS);
    d.u1.chars = s;

    return &this->asFixed();
}

JSStringFinalizeOp JSExternalString::str_finalizers[JSExternalString::TYPE_LIMIT] = {
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
