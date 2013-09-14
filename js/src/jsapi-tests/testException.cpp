/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


#include "tests.h"

BEGIN_TEST(testException_bug860435)
{
    JS::RootedValue fun(cx);

    EVAL("ReferenceError", fun.address());
    CHECK(fun.isObject());

    JS::RootedValue v(cx);
    JS_CallFunctionValue(cx, global, fun, 0, v.address(), v.address());
    CHECK(v.isObject());

    JS_GetProperty(cx, &v.toObject(), "stack", v.address());
    CHECK(v.isString());
    return true;
}
END_TEST(testException_bug860435)
