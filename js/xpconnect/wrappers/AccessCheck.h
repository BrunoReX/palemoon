/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=99 ft=cpp:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __AccessCheck_h__
#define __AccessCheck_h__

#include "jsapi.h"
#include "jswrapper.h"
#include "WrapperFactory.h"

class nsIPrincipal;

namespace xpc {

class AccessCheck {
  public:
    static bool subsumes(JSCompartment *a, JSCompartment *b);
    static bool subsumes(JSObject *a, JSObject *b);
    static bool wrapperSubsumes(JSObject *wrapper);
    static bool subsumesIgnoringDomain(JSCompartment *a, JSCompartment *b);
    static bool isChrome(JSCompartment *compartment);
    static bool isChrome(JSObject *obj);
    static bool callerIsChrome();
    static nsIPrincipal *getPrincipal(JSCompartment *compartment);
    static bool isCrossOriginAccessPermitted(JSContext *cx, JSObject *obj, jsid id,
                                             js::Wrapper::Action act);
    static bool callerIsXBL(JSContext *cx);
    static bool isSystemOnlyAccessPermitted(JSContext *cx);
    static bool isLocationObjectSameOrigin(JSContext *cx, JSObject *wrapper);

    static bool needsSystemOnlyWrapper(JSObject *obj);

    static bool isScriptAccessOnly(JSContext *cx, JSObject *wrapper);

    static void deny(JSContext *cx, jsid id);
};

struct Policy {
};

// This policy only permits access to the object if the subject can touch
// system objects.
struct OnlyIfSubjectIsSystem : public Policy {
    static bool check(JSContext *cx, JSObject *wrapper, jsid id, js::Wrapper::Action act) {
        return AccessCheck::isSystemOnlyAccessPermitted(cx);
    }

    static bool deny(JSContext *cx, jsid id, js::Wrapper::Action act) {
        AccessCheck::deny(cx, id);
        return false;
    }

    static bool allowNativeCall(JSContext *cx, JS::IsAcceptableThis test, JS::NativeImpl impl)
    {
        return AccessCheck::isSystemOnlyAccessPermitted(cx);
    }
};

// This policy only permits access to properties that are safe to be used
// across origins.
struct CrossOriginAccessiblePropertiesOnly : public Policy {
    static bool check(JSContext *cx, JSObject *wrapper, jsid id, js::Wrapper::Action act) {
        // Location objects should always use LocationPolicy.
        MOZ_ASSERT(!WrapperFactory::IsLocationObject(js::UnwrapObject(wrapper)));
        return AccessCheck::isCrossOriginAccessPermitted(cx, wrapper, id, act);
    }
    static bool deny(JSContext *cx, jsid id, js::Wrapper::Action act) {
        AccessCheck::deny(cx, id);
        return false;
    }
    static bool allowNativeCall(JSContext *cx, JS::IsAcceptableThis test, JS::NativeImpl impl)
    {
        return false;
    }
};

// We need a special security policy for Location objects.
//
// Location objects are special because their effective principal is that of
// the outer window, not the inner window. So while the security characteristics
// of most objects can be inferred from their compartments, those of the Location
// object cannot. This has two implications:
//
// 1 - Same-compartment access of Location objects is not necessarily allowed.
//     This means that objects must see a security wrapper around Location objects
//     in their own compartment.
// 2 - Cross-origin access of Location objects is not necessarily forbidden.
//     Since the security decision depends on the current state of the outer window,
//     we can't make it at wrap time. Instead, we need to make it at the time of
//     access.
//
// So for any Location object access, be it same-compartment or cross-compartment,
// we need to do a dynamic security check to determine whether the outer window is
// same-origin with the caller.
//
// So this policy first checks whether the access is something that any code,
// same-origin or not, is allowed to make. If it isn't, it _also_ checks the
// state of the outer window to determine whether we happen to be same-origin
// at the moment.
struct LocationPolicy : public Policy {
    static bool check(JSContext *cx, JSObject *wrapper, jsid id, js::Wrapper::Action act) {
        // We should only be dealing with Location objects here.
        MOZ_ASSERT(WrapperFactory::IsLocationObject(js::UnwrapObject(wrapper)));

        if ((AccessCheck::isCrossOriginAccessPermitted(cx, wrapper, id, act) ||
             AccessCheck::isLocationObjectSameOrigin(cx, wrapper))) {
            return true;
        }
        return false;
    }
    static bool deny(JSContext *cx, jsid id, js::Wrapper::Action act) {
        AccessCheck::deny(cx, id);
        return false;
    }

    static bool allowNativeCall(JSContext *cx, JS::IsAcceptableThis test, JS::NativeImpl impl)
    {
        return false;
    }
};

// This policy only permits access to properties if they appear in the
// objects exposed properties list.
struct ExposedPropertiesOnly : public Policy {
    static bool check(JSContext *cx, JSObject *wrapper, jsid id, js::Wrapper::Action act);

    static bool deny(JSContext *cx, jsid id, js::Wrapper::Action act) {
        // For gets, silently fail.
        if (act == js::Wrapper::GET)
            return true;
        // For sets,throw an exception.
        AccessCheck::deny(cx, id);
        return false;
    }
    static bool allowNativeCall(JSContext *cx, JS::IsAcceptableThis test, JS::NativeImpl impl);
};

// Components specific policy
struct ComponentsObjectPolicy : public Policy {
    static bool check(JSContext *cx, JSObject *wrapper, jsid id, js::Wrapper::Action act);

    static bool deny(JSContext *cx, jsid id, js::Wrapper::Action act) {
        AccessCheck::deny(cx, id);
        return false;
    }
    static bool allowNativeCall(JSContext *cx, JS::IsAcceptableThis test, JS::NativeImpl impl) {
        return false;
    }
};

}

#endif /* __AccessCheck_h__ */
