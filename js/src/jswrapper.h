/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=99:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jswrapper_h___
#define jswrapper_h___

#include "mozilla/Attributes.h"

#include "jsapi.h"
#include "jsproxy.h"

namespace js {

class DummyFrameGuard;

/* Base class that just implements no-op forwarding methods for fundamental
 * traps. This is meant to be used as a base class for ProxyHandlers that
 * want transparent forwarding behavior but don't want to use the derived
 * traps and other baggage of js::Wrapper.
 */
class JS_FRIEND_API(AbstractWrapper) : public IndirectProxyHandler
{
    unsigned mFlags;
  public:
    unsigned flags() const { return mFlags; }

    explicit AbstractWrapper(unsigned flags);

    /* ES5 Harmony fundamental wrapper traps. */
    virtual bool getPropertyDescriptor(JSContext *cx, JSObject *wrapper, jsid id, bool set,
                                       PropertyDescriptor *desc) MOZ_OVERRIDE;
    virtual bool getOwnPropertyDescriptor(JSContext *cx, JSObject *wrapper, jsid id, bool set,
                                          PropertyDescriptor *desc) MOZ_OVERRIDE;
    virtual bool defineProperty(JSContext *cx, JSObject *wrapper, jsid id,
                                PropertyDescriptor *desc) MOZ_OVERRIDE;
    virtual bool getOwnPropertyNames(JSContext *cx, JSObject *wrapper, AutoIdVector &props) MOZ_OVERRIDE;
    virtual bool delete_(JSContext *cx, JSObject *wrapper, jsid id, bool *bp) MOZ_OVERRIDE;
    virtual bool enumerate(JSContext *cx, JSObject *wrapper, AutoIdVector &props) MOZ_OVERRIDE;

    /* Policy enforcement traps.
     *
     * enter() allows the policy to specify whether the caller may perform |act|
     * on the underlying object's |id| property. In the case when |act| is CALL,
     * |id| is generally JSID_VOID.
     *
     * leave() allows the policy to undo various scoped state changes taken in
     * enter(). If enter() succeeds, leave() must be called upon completion of
     * the approved action.
     *
     * The |act| parameter to enter() specifies the action being performed. GET,
     * SET, and CALL are self-explanatory, but PUNCTURE requires more explanation:
     *
     * GET and SET allow for a very fine-grained security membrane, through
     * which access can be granted or denied on a per-property, per-object, and
     * per-action basis. Sometimes though, we just want to asks if we can access
     * _everything_ behind the wrapper barrier. For example, when the structured
     * clone algorithm runs up against a cross-compartment wrapper, it needs to
     * know whether it can enter the compartment and keep cloning, or whether it
     * should throw. This is the role of PUNCTURE.
     *
     * PUNCTURE allows the policy to specify whether the wrapper barrier may
     * be lifted - that is to say, whether the caller is allowed to access
     * anything that the wrapped object could access. This is a very powerful
     * permission, and thus should generally be denied for security wrappers
     * except under very special circumstances. When |act| is PUNCTURE, |id|
     * should be JSID_VOID.
     * */
    enum Action { GET, SET, CALL, PUNCTURE };
    virtual bool enter(JSContext *cx, JSObject *wrapper, jsid id, Action act, bool *bp);
    virtual void leave(JSContext *cx, JSObject *wrapper);

    static JSObject *wrappedObject(const JSObject *wrapper);
    static AbstractWrapper *wrapperHandler(const JSObject *wrapper);
};

/* No-op wrapper handler base class. */
class JS_FRIEND_API(DirectWrapper) : public AbstractWrapper
{
  public:
    explicit DirectWrapper(unsigned flags);

    typedef enum { PermitObjectAccess, PermitPropertyAccess, DenyAccess } Permission;

    virtual ~DirectWrapper();

    /* ES5 Harmony derived wrapper traps. */
    virtual bool has(JSContext *cx, JSObject *wrapper, jsid id, bool *bp) MOZ_OVERRIDE;
    virtual bool hasOwn(JSContext *cx, JSObject *wrapper, jsid id, bool *bp) MOZ_OVERRIDE;
    virtual bool get(JSContext *cx, JSObject *wrapper, JSObject *receiver, jsid id, Value *vp) MOZ_OVERRIDE;
    virtual bool set(JSContext *cx, JSObject *wrapper, JSObject *receiver, jsid id, bool strict,
                     Value *vp) MOZ_OVERRIDE;
    virtual bool keys(JSContext *cx, JSObject *wrapper, AutoIdVector &props) MOZ_OVERRIDE;
    virtual bool iterate(JSContext *cx, JSObject *wrapper, unsigned flags, Value *vp) MOZ_OVERRIDE;

    /* Spidermonkey extensions. */
    virtual bool call(JSContext *cx, JSObject *wrapper, unsigned argc, Value *vp) MOZ_OVERRIDE;
    virtual bool construct(JSContext *cx, JSObject *wrapper, unsigned argc, Value *argv, Value *rval) MOZ_OVERRIDE;
    virtual bool nativeCall(JSContext *cx, JSObject *wrapper, Class *clasp, Native native, CallArgs args) MOZ_OVERRIDE;
    virtual bool hasInstance(JSContext *cx, JSObject *wrapper, const Value *vp, bool *bp) MOZ_OVERRIDE;
    virtual JSString *obj_toString(JSContext *cx, JSObject *wrapper) MOZ_OVERRIDE;
    virtual JSString *fun_toString(JSContext *cx, JSObject *wrapper, unsigned indent) MOZ_OVERRIDE;

    using AbstractWrapper::Action;

    static DirectWrapper singleton;

    static JSObject *New(JSContext *cx, JSObject *obj, JSObject *proto, JSObject *parent,
                         DirectWrapper *handler);

    using AbstractWrapper::wrappedObject;
    using AbstractWrapper::wrapperHandler;

    enum {
        CROSS_COMPARTMENT = 1 << 0,
        LAST_USED_FLAG = CROSS_COMPARTMENT
    };

    static void *getWrapperFamily();
};

/* 
 * This typedef is only here to avoid code churn in xpconnect. It will be
 * removed as soon as the Wrapper base class lands.
 */
typedef DirectWrapper Wrapper;

/* Base class for all cross compartment wrapper handlers. */
class JS_FRIEND_API(CrossCompartmentWrapper) : public DirectWrapper
{
  public:
    CrossCompartmentWrapper(unsigned flags);

    virtual ~CrossCompartmentWrapper();

    /* ES5 Harmony fundamental wrapper traps. */
    virtual bool getPropertyDescriptor(JSContext *cx, JSObject *wrapper, jsid id, bool set,
                                       PropertyDescriptor *desc) MOZ_OVERRIDE;
    virtual bool getOwnPropertyDescriptor(JSContext *cx, JSObject *wrapper, jsid id, bool set,
                                          PropertyDescriptor *desc) MOZ_OVERRIDE;
    virtual bool defineProperty(JSContext *cx, JSObject *wrapper, jsid id,
                                PropertyDescriptor *desc) MOZ_OVERRIDE;
    virtual bool getOwnPropertyNames(JSContext *cx, JSObject *wrapper, AutoIdVector &props) MOZ_OVERRIDE;
    virtual bool delete_(JSContext *cx, JSObject *wrapper, jsid id, bool *bp) MOZ_OVERRIDE;
    virtual bool enumerate(JSContext *cx, JSObject *wrapper, AutoIdVector &props) MOZ_OVERRIDE;

    /* ES5 Harmony derived wrapper traps. */
    virtual bool has(JSContext *cx, JSObject *wrapper, jsid id, bool *bp) MOZ_OVERRIDE;
    virtual bool hasOwn(JSContext *cx, JSObject *wrapper, jsid id, bool *bp) MOZ_OVERRIDE;
    virtual bool get(JSContext *cx, JSObject *wrapper, JSObject *receiver, jsid id, Value *vp) MOZ_OVERRIDE;
    virtual bool set(JSContext *cx, JSObject *wrapper, JSObject *receiver, jsid id, bool strict,
                     Value *vp) MOZ_OVERRIDE;
    virtual bool keys(JSContext *cx, JSObject *wrapper, AutoIdVector &props) MOZ_OVERRIDE;
    virtual bool iterate(JSContext *cx, JSObject *wrapper, unsigned flags, Value *vp) MOZ_OVERRIDE;

    /* Spidermonkey extensions. */
    virtual bool call(JSContext *cx, JSObject *wrapper, unsigned argc, Value *vp) MOZ_OVERRIDE;
    virtual bool construct(JSContext *cx, JSObject *wrapper, unsigned argc, Value *argv, Value *rval) MOZ_OVERRIDE;
    virtual bool nativeCall(JSContext *cx, JSObject *wrapper, Class *clasp, Native native, CallArgs args) MOZ_OVERRIDE;
    virtual bool hasInstance(JSContext *cx, JSObject *wrapper, const Value *vp, bool *bp) MOZ_OVERRIDE;
    virtual JSString *obj_toString(JSContext *cx, JSObject *wrapper) MOZ_OVERRIDE;
    virtual JSString *fun_toString(JSContext *cx, JSObject *wrapper, unsigned indent) MOZ_OVERRIDE;
    virtual bool regexp_toShared(JSContext *cx, JSObject *proxy, RegExpGuard *g) MOZ_OVERRIDE;
    virtual bool defaultValue(JSContext *cx, JSObject *wrapper, JSType hint, Value *vp) MOZ_OVERRIDE;
    virtual bool iteratorNext(JSContext *cx, JSObject *wrapper, Value *vp);

    static CrossCompartmentWrapper singleton;
};

/*
 * Base class for security wrappers. A security wrapper is potentially hiding
 * all or part of some wrapped object thus SecurityWrapper defaults to denying
 * access to the wrappee. This is the opposite of Wrapper which tries to be
 * completely transparent.
 *
 * NB: Currently, only a few ProxyHandler operations are overridden to deny
 * access, relying on derived SecurityWrapper to block access when necessary.
 */
template <class Base>
class JS_FRIEND_API(SecurityWrapper) : public Base
{
  public:
    SecurityWrapper(unsigned flags);

    virtual bool nativeCall(JSContext *cx, JSObject *wrapper, Class *clasp, Native native, CallArgs args) MOZ_OVERRIDE;
    virtual bool objectClassIs(JSObject *obj, ESClassValue classValue, JSContext *cx) MOZ_OVERRIDE;
    virtual bool regexp_toShared(JSContext *cx, JSObject *proxy, RegExpGuard *g) MOZ_OVERRIDE;
};

typedef SecurityWrapper<DirectWrapper> SameCompartmentSecurityWrapper;
typedef SecurityWrapper<CrossCompartmentWrapper> CrossCompartmentSecurityWrapper;

/*
 * A hacky class that lets a friend force a fake frame. We must already be
 * in the compartment of |target| when we enter the forced frame.
 */
class JS_FRIEND_API(ForceFrame)
{
  public:
    JSContext * const context;
    JSObject * const target;
  private:
    DummyFrameGuard *frame;

  public:
    ForceFrame(JSContext *cx, JSObject *target);
    ~ForceFrame();
    bool enter();
};


class JS_FRIEND_API(DeadObjectProxy) : public BaseProxyHandler
{
  public:
    static int sDeadObjectFamily;

    explicit DeadObjectProxy();

    /* ES5 Harmony fundamental wrapper traps. */
    virtual bool getPropertyDescriptor(JSContext *cx, JSObject *wrapper, jsid id, bool set,
                                       PropertyDescriptor *desc) MOZ_OVERRIDE;
    virtual bool getOwnPropertyDescriptor(JSContext *cx, JSObject *wrapper, jsid id, bool set,
                                          PropertyDescriptor *desc) MOZ_OVERRIDE;
    virtual bool defineProperty(JSContext *cx, JSObject *wrapper, jsid id,
                                PropertyDescriptor *desc) MOZ_OVERRIDE;
    virtual bool getOwnPropertyNames(JSContext *cx, JSObject *wrapper, AutoIdVector &props) MOZ_OVERRIDE;
    virtual bool delete_(JSContext *cx, JSObject *wrapper, jsid id, bool *bp) MOZ_OVERRIDE;
    virtual bool enumerate(JSContext *cx, JSObject *wrapper, AutoIdVector &props) MOZ_OVERRIDE;

    /* Spidermonkey extensions. */
    virtual bool call(JSContext *cx, JSObject *proxy, unsigned argc, Value *vp);
    virtual bool construct(JSContext *cx, JSObject *proxy, unsigned argc, Value *argv, Value *rval);
    virtual bool nativeCall(JSContext *cx, JSObject *proxy, Class *clasp, Native native, CallArgs args);
    virtual bool hasInstance(JSContext *cx, JSObject *proxy, const Value *vp, bool *bp);
    virtual bool objectClassIs(JSObject *obj, ESClassValue classValue, JSContext *cx);
    virtual JSString *obj_toString(JSContext *cx, JSObject *proxy);
    virtual JSString *fun_toString(JSContext *cx, JSObject *proxy, unsigned indent);
    virtual bool regexp_toShared(JSContext *cx, JSObject *proxy, RegExpGuard *g);
    virtual bool defaultValue(JSContext *cx, JSObject *obj, JSType hint, Value *vp);
    virtual bool iteratorNext(JSContext *cx, JSObject *proxy, Value *vp);
    virtual bool getElementIfPresent(JSContext *cx, JSObject *obj, JSObject *receiver,
                                     uint32_t index, Value *vp, bool *present);


    static DeadObjectProxy singleton;
};

extern JSObject *
TransparentObjectWrapper(JSContext *cx, JSObject *obj, JSObject *wrappedProto, JSObject *parent,
                         unsigned flags);

// Proxy family for wrappers. Public so that IsWrapper() can be fully inlined by
// jsfriendapi users.
extern JS_FRIEND_DATA(int) sWrapperFamily;

inline bool
IsWrapper(const JSObject *obj)
{
    return IsProxy(obj) && GetProxyHandler(obj)->family() == &sWrapperFamily;
}

// Given a JSObject, returns that object stripped of wrappers. If
// stopAtOuter is true, then this returns the outer window if it was
// previously wrapped. Otherwise, this returns the first object for
// which JSObject::isWrapper returns false.
JS_FRIEND_API(JSObject *)
UnwrapObject(JSObject *obj, bool stopAtOuter = true, unsigned *flagsp = NULL);

// Given a JSObject, returns that object stripped of wrappers. At each stage,
// the security wrapper has the opportunity to veto the unwrap. Since checked
// code should never be unwrapping outer window wrappers, we always stop at
// outer windows.
JS_FRIEND_API(JSObject *)
UnwrapObjectChecked(JSContext *cx, JSObject *obj);

JS_FRIEND_API(bool)
IsCrossCompartmentWrapper(const JSObject *obj);

JSObject *
NewDeadProxyObject(JSContext *cx, JSObject *parent);

void
NukeCrossCompartmentWrapper(JSObject *wrapper);

} /* namespace js */

#endif
