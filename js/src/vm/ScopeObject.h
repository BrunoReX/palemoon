/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=78:
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
 * The Original Code is SpiderMonkey call object code.
 *
 * The Initial Developer of the Original Code is
 * the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Paul Biggar <pbiggar@mozilla.com> (original author)
 *   Luke Wagner <luke@mozilla.com>
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

#ifndef ScopeObject_h___
#define ScopeObject_h___

#include "jsobj.h"

namespace js {

/*
 * Scope objects
 *
 * Scope objects are technically real JSObjects but only belong on the scope
 * chain (that is, fp->scopeChain() or fun->environment()). The hierarchy of
 * scope objects is:
 *
 *   JSObject                   Generic object
 *     \
 *   ScopeObject                Engine-internal scope
 *     \   \   \
 *      \   \  DeclEnvObject    Holds name of recursive/heavyweight named lambda
 *       \   \
 *        \  CallObject         Scope of entire function or strict eval
 *         \
 *   NestedScopeObject          Scope created for a statement
 *     \   \
 *      \  WithObject           with
 *       \
 *   BlockObject                Shared interface of cloned/static block objects
 *     \   \
 *      \  ClonedBlockObject    let, switch, catch, for
 *       \
 *       StaticBlockObject      See NB
 *
 * This hierarchy represents more than just the interface hierarchy: reserved
 * slots in base classes are fixed for all derived classes. Thus, for example,
 * ScopeObject::enclosingScope() can simply access a fixed slot without further
 * dynamic type information.
 *
 * NB: Static block objects are a special case: these objects are created at
 * compile time to hold the shape/binding information from which block objects
 * are cloned at runtime. These objects should never escape into the wild and
 * support a restricted set of ScopeObject operations.
 */

class ScopeObject : public JSObject
{
    /* Use maybeStackFrame() instead. */
    void *getPrivate() const;

  protected:
    static const uint32_t SCOPE_CHAIN_SLOT = 0;

  public:
    /*
     * Since every scope chain terminates with a global object and GlobalObject
     * does not derive ScopeObject (it has a completely different layout), the
     * enclosing scope of a ScopeObject is necessarily non-null.
     */
    inline JSObject &enclosingScope() const;
    inline bool setEnclosingScope(JSContext *cx, JSObject &obj);

    /*
     * The stack frame for this scope object, if the frame is still active.
     * Note: these members may not be called for a StaticBlockObject.
     */
    inline js::StackFrame *maybeStackFrame() const;
    inline void setStackFrame(StackFrame *frame);

    /* For jit access. */
    static inline size_t offsetOfEnclosingScope();
};

class CallObject : public ScopeObject
{
    static const uint32_t CALLEE_SLOT = 1;
    static const uint32_t ARGUMENTS_SLOT = 2;

  public:
    static const uint32_t RESERVED_SLOTS = 3;

    static CallObject *
    create(JSContext *cx, JSScript *script, JSObject &enclosing, JSObject *callee);

    /* True if this is for a strict mode eval frame or for a function call. */
    inline bool isForEval() const;

    /*
     * The callee function if this CallObject was created for a function
     * invocation, or null if it was created for a strict mode eval frame.
     */
    inline JSObject *getCallee() const;
    inline JSFunction *getCalleeFunction() const;
    inline void setCallee(JSObject *callee);

    /* Returns the callee's arguments object. */
    inline const js::Value &getArguments() const;
    inline void setArguments(const js::Value &v);
    inline void initArguments(const js::Value &v);

    /* Returns the formal argument at the given index. */
    inline const js::Value &arg(uintN i) const;
    inline void setArg(uintN i, const js::Value &v);
    inline void initArgUnchecked(uintN i, const js::Value &v);

    /* Returns the variable at the given index. */
    inline const js::Value &var(uintN i) const;
    inline void setVar(uintN i, const js::Value &v);
    inline void initVarUnchecked(uintN i, const js::Value &v);

    /*
     * Get the actual arrays of arguments and variables. Only call if type
     * inference is enabled, where we ensure that call object variables are in
     * contiguous slots (see NewCallObject).
     */
    inline js::HeapValueArray argArray();
    inline js::HeapValueArray varArray();

    inline void copyValues(uintN nargs, Value *argv, uintN nvars, Value *slots);
};

class DeclEnvObject : public ScopeObject
{
  public:
    static const uint32_t RESERVED_SLOTS = 1;
    static const gc::AllocKind FINALIZE_KIND = gc::FINALIZE_OBJECT2;

    static DeclEnvObject *create(JSContext *cx, StackFrame *fp);

};

class NestedScopeObject : public ScopeObject
{
  protected:
    static const unsigned DEPTH_SLOT = 1;

  public:
    /* Return the abstract stack depth right before entering this nested scope. */
    uint32_t stackDepth() const;
};

class WithObject : public NestedScopeObject
{
    static const unsigned THIS_SLOT = 2;

    /* Use WithObject::object() instead. */
    JSObject *getProto() const;

  public:
    static const unsigned RESERVED_SLOTS = 3;
    static const gc::AllocKind FINALIZE_KIND = gc::FINALIZE_OBJECT4;

    static WithObject *
    create(JSContext *cx, StackFrame *fp, JSObject &proto, JSObject &enclosing, uint32_t depth);

    /* Return object for the 'this' class hook. */
    JSObject &withThis() const;

    /* Return the 'o' in 'with (o)'. */
    JSObject &object() const;
};

class BlockObject : public NestedScopeObject
{
  public:
    static const unsigned RESERVED_SLOTS = 2;
    static const gc::AllocKind FINALIZE_KIND = gc::FINALIZE_OBJECT4;

    /* Return the number of variables associated with this block. */
    inline uint32_t slotCount() const;

  protected:
    /* Blocks contain an object slot for each slot i: 0 <= i < slotCount. */
    inline HeapValue &slotValue(unsigned i);
};

class StaticBlockObject : public BlockObject
{
    /* These ScopeObject operations are not valid on a static block object. */
    js::StackFrame *maybeStackFrame() const;
    void setStackFrame(StackFrame *frame);

  public:
    static StaticBlockObject *create(JSContext *cx);

    inline StaticBlockObject *enclosingBlock() const;
    inline void setEnclosingBlock(StaticBlockObject *blockObj);

    void setStackDepth(uint32_t depth);

    /*
     * Frontend compilation temporarily uses the object's slots to link
     * a let var to its associated Definition parse node.
     */
    void setDefinitionParseNode(unsigned i, Definition *def);
    Definition *maybeDefinitionParseNode(unsigned i);
    void poisonDefinitionParseNode(unsigned i);

    const js::Shape *addVar(JSContext *cx, jsid id, intN index, bool *redeclared);
};

class ClonedBlockObject : public BlockObject
{
  public:
    static ClonedBlockObject *create(JSContext *cx, StaticBlockObject &block, StackFrame *fp);

    /* The static block from which this block was cloned. */
    StaticBlockObject &staticBlock() const;

    /*
     * When this block's stack slots are about to be popped, 'put' must be
     * called to copy the slot values into this block's object slots.
     */
    void put(JSContext *cx);

    /* Assuming 'put' has been called, return the value of the ith let var. */
    const Value &closedSlot(unsigned i);
};

}  /* namespace js */

extern bool
js_XDRStaticBlockObject(JSXDRState *xdr, js::StaticBlockObject **objp);

#endif /* ScopeObject_h___ */
