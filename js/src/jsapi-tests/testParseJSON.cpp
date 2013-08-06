/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=99:
 */

#include <limits>
#include <math.h>

#include "tests.h"
#include "jsstr.h"
#include "vm/String.h"

using namespace js;

class AutoInflatedString {
    JSContext * const cx;
    jschar *chars_;
    size_t length_;

  public:
    AutoInflatedString(JSContext *cx) : cx(cx), chars_(NULL), length_(0) { }
    ~AutoInflatedString() {
        JS_free(cx, chars_);
    }

    template<size_t N> void operator=(const char (&str)[N]) {
        length_ = N - 1;
        chars_ = InflateString(cx, str, &length_);
        if (!chars_)
            abort();
    }

    const jschar *chars() const { return chars_; }
    size_t length() const { return length_; }
};

template<size_t N> JSFlatString *
NewString(JSContext *cx, const jschar (&chars)[N])
{
    return js_NewStringCopyN(cx, chars, N);
}

BEGIN_TEST(testParseJSON_success)
{
    // Primitives
    CHECK(TryParse(cx, "true", JSVAL_TRUE));
    CHECK(TryParse(cx, "false", JSVAL_FALSE));
    CHECK(TryParse(cx, "null", JSVAL_NULL));
    CHECK(TryParse(cx, "0", INT_TO_JSVAL(0)));
    CHECK(TryParse(cx, "1", INT_TO_JSVAL(1)));
    CHECK(TryParse(cx, "-1", INT_TO_JSVAL(-1)));
    CHECK(TryParse(cx, "1", DOUBLE_TO_JSVAL(1)));
    CHECK(TryParse(cx, "1.75", DOUBLE_TO_JSVAL(1.75)));
    CHECK(TryParse(cx, "9e9", DOUBLE_TO_JSVAL(9e9)));
    CHECK(TryParse(cx, "9e99999", DOUBLE_TO_JSVAL(std::numeric_limits<jsdouble>::infinity())));

    JSFlatString *str;

    const jschar emptystr[] = { '\0' };
    str = js_NewStringCopyN(cx, emptystr, 0);
    CHECK(str);
    CHECK(TryParse(cx, "\"\"", STRING_TO_JSVAL(str)));

    const jschar nullstr[] = { '\0' };
    str = NewString(cx, nullstr);
    CHECK(str);
    CHECK(TryParse(cx, "\"\\u0000\"", STRING_TO_JSVAL(str)));

    const jschar backstr[] = { '\b' };
    str = NewString(cx, backstr);
    CHECK(str);
    CHECK(TryParse(cx, "\"\\b\"", STRING_TO_JSVAL(str)));
    CHECK(TryParse(cx, "\"\\u0008\"", STRING_TO_JSVAL(str)));

    const jschar newlinestr[] = { '\n', };
    str = NewString(cx, newlinestr);
    CHECK(str);
    CHECK(TryParse(cx, "\"\\n\"", STRING_TO_JSVAL(str)));
    CHECK(TryParse(cx, "\"\\u000A\"", STRING_TO_JSVAL(str)));


    // Arrays
    jsval v, v2;
    JSObject *obj;

    CHECK(Parse(cx, "[]", &v));
    CHECK(!JSVAL_IS_PRIMITIVE(v));
    obj = JSVAL_TO_OBJECT(v);
    CHECK(JS_IsArrayObject(cx, obj));
    CHECK(JS_GetProperty(cx, obj, "length", &v2));
    CHECK_SAME(v2, JSVAL_ZERO);

    CHECK(Parse(cx, "[1]", &v));
    CHECK(!JSVAL_IS_PRIMITIVE(v));
    obj = JSVAL_TO_OBJECT(v);
    CHECK(JS_IsArrayObject(cx, obj));
    CHECK(JS_GetProperty(cx, obj, "0", &v2));
    CHECK_SAME(v2, JSVAL_ONE);
    CHECK(JS_GetProperty(cx, obj, "length", &v2));
    CHECK_SAME(v2, JSVAL_ONE);


    // Objects
    CHECK(Parse(cx, "{}", &v));
    CHECK(!JSVAL_IS_PRIMITIVE(v));
    obj = JSVAL_TO_OBJECT(v);
    CHECK(!JS_IsArrayObject(cx, obj));

    CHECK(Parse(cx, "{ \"f\": 17 }", &v));
    CHECK(!JSVAL_IS_PRIMITIVE(v));
    obj = JSVAL_TO_OBJECT(v);
    CHECK(!JS_IsArrayObject(cx, obj));
    CHECK(JS_GetProperty(cx, obj, "f", &v2));
    CHECK_SAME(v2, INT_TO_JSVAL(17));

    return true;
}

template<size_t N> inline bool
Parse(JSContext *cx, const char (&input)[N], jsval *vp)
{
    AutoInflatedString str(cx);
    str = input;
    CHECK(JS_ParseJSON(cx, str.chars(), str.length(), vp));
    return true;
}

template<size_t N> inline bool
TryParse(JSContext *cx, const char (&input)[N], const jsval &expected)
{
    AutoInflatedString str(cx);
    jsval v;
    str = input;
    CHECK(JS_ParseJSON(cx, str.chars(), str.length(), &v));
    CHECK_SAME(v, expected);
    return true;
}
END_TEST(testParseJSON_success)

BEGIN_TEST(testParseJSON_error)
{
    CHECK(Error(cx, "["));
    CHECK(Error(cx, "[,]"));
    CHECK(Error(cx, "[1,]"));
    CHECK(Error(cx, "{a:2}"));
    CHECK(Error(cx, "{\"a\":2,}"));
    CHECK(Error(cx, "]"));
    CHECK(Error(cx, "'bad string'"));
    CHECK(Error(cx, "\""));
    CHECK(Error(cx, "{]"));
    CHECK(Error(cx, "[}"));
    return true;
}

template<size_t N> inline bool
Error(JSContext *cx, const char (&input)[N])
{
    AutoInflatedString str(cx);
    jsval dummy;
    str = input;
    CHECK(!JS_ParseJSON(cx, str.chars(), str.length(), &dummy));
    JS_ClearPendingException(cx);
    return true;
}
END_TEST(testParseJSON_error)

static JSBool
Censor(JSContext *cx, uintN argc, jsval *vp)
{
    JS_ASSERT(argc == 2);
#ifdef DEBUG
    jsval *argv = JS_ARGV(cx, vp);
    JS_ASSERT(JSVAL_IS_STRING(argv[0]));
#endif
    JS_SET_RVAL(cx, vp, JSVAL_NULL);
    return true;
}

BEGIN_TEST(testParseJSON_reviver)
{
    JSFunction *fun = JS_NewFunction(cx, Censor, 0, 0, global, "censor");
    CHECK(fun);

    jsval filter = OBJECT_TO_JSVAL(JS_GetFunctionObject(fun));

    CHECK(TryParse(cx, "true", filter));
    CHECK(TryParse(cx, "false", filter));
    CHECK(TryParse(cx, "null", filter));
    CHECK(TryParse(cx, "1", filter));
    CHECK(TryParse(cx, "1.75", filter));
    CHECK(TryParse(cx, "[]", filter));
    CHECK(TryParse(cx, "[1]", filter));
    CHECK(TryParse(cx, "{}", filter));
    return true;
}

template<size_t N> inline bool
TryParse(JSContext *cx, const char (&input)[N], jsval filter)
{
    AutoInflatedString str(cx);
    jsval v;
    str = input;
    CHECK(JS_ParseJSONWithReviver(cx, str.chars(), str.length(), filter, &v));
    CHECK_SAME(v, JSVAL_NULL);
    return true;
}
END_TEST(testParseJSON_reviver)
