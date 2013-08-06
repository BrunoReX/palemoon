/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=99 ft=cpp:
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
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Chris Leary <cdleary@mozilla.com>
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

#ifndef ParseMaps_h__
#define ParseMaps_h__

#include "jsvector.h"

#include "mfbt/InlineMap.h"

namespace js {

/*
 * A pool that permits the reuse of the backing storage for the defn, index, or
 * defn-or-header (multi) maps.
 *
 * The pool owns all the maps that are given out, and is responsible for
 * relinquishing all resources when |purgeAll| is triggered.
 */
class ParseMapPool
{
    typedef Vector<void *, 32, SystemAllocPolicy> RecyclableMaps;

    RecyclableMaps      all;
    RecyclableMaps      recyclable;
    JSContext           *cx;

    void checkInvariants();

    void recycle(void *map) {
        JS_ASSERT(map);
#ifdef DEBUG
        bool ok = false;
        /* Make sure the map is in |all| but not already in |recyclable|. */
        for (void **it = all.begin(), **end = all.end(); it != end; ++it) {
            if (*it == map) {
                ok = true;
                break;
            }
        }
        JS_ASSERT(ok);
        for (void **it = recyclable.begin(), **end = recyclable.end(); it != end; ++it)
            JS_ASSERT(*it != map);
#endif
        JS_ASSERT(recyclable.length() < all.length());
        recyclable.infallibleAppend(map); /* Reserved in allocateFresh. */
    }

    void *allocateFresh();
    void *allocate();

    /* Arbitrary atom map type, that has keys and values of the same kind. */
    typedef AtomIndexMap AtomMapT;

    static AtomMapT *asAtomMap(void *ptr) {
        return reinterpret_cast<AtomMapT *>(ptr);
    }

  public:
    explicit ParseMapPool(JSContext *cx) : cx(cx) {}

    ~ParseMapPool() {
        purgeAll();
    }

    void purgeAll();

    bool empty() const {
        return all.empty();
    }

    /* Fallibly aquire one of the supported map types from the pool. */
    template <typename T>
    T *acquire();

    /* Release one of the supported map types back to the pool. */

    void release(AtomIndexMap *map) {
        recycle((void *) map);
    }

    void release(AtomDefnMap *map) {
        recycle((void *) map);
    }

    void release(AtomDOHMap *map) {
        recycle((void *) map);
    }
}; /* ParseMapPool */

/*
 * N.B. This is a POD-type so that it can be included in the JSParseNode union.
 * If possible, use the corresponding |OwnedAtomThingMapPtr| variant.
 */
template <class Map>
struct AtomThingMapPtr
{
    Map *map_;

    void init() { clearMap(); }

    bool ensureMap(JSContext *cx);
    void releaseMap(JSContext *cx);

    bool hasMap() const { return map_; }
    Map *getMap() { return map_; }
    void setMap(Map *newMap) { JS_ASSERT(!map_); map_ = newMap; }
    void clearMap() { map_ = NULL; }

    Map *operator->() { return map_; }
    const Map *operator->() const { return map_; }
    Map &operator*() const { return *map_; }
};

struct AtomDefnMapPtr : public AtomThingMapPtr<AtomDefnMap>
{
    JS_ALWAYS_INLINE
    JSDefinition *lookupDefn(JSAtom *atom) {
        AtomDefnMap::Ptr p = map_->lookup(atom);
        return p ? p.value() : NULL;
    }
};

typedef AtomThingMapPtr<AtomIndexMap> AtomIndexMapPtr;

/*
 * Wrapper around an AtomThingMapPtr (or its derivatives) that automatically
 * releases a map on destruction, if one has been acquired.
 */
template <typename AtomThingMapPtrT>
class OwnedAtomThingMapPtr : public AtomThingMapPtrT
{
    JSContext *cx;

  public:
    explicit OwnedAtomThingMapPtr(JSContext *cx) : cx(cx) {
        AtomThingMapPtrT::init();
    }

    ~OwnedAtomThingMapPtr() {
        AtomThingMapPtrT::releaseMap(cx);
    }
};

typedef OwnedAtomThingMapPtr<AtomDefnMapPtr> OwnedAtomDefnMapPtr;
typedef OwnedAtomThingMapPtr<AtomIndexMapPtr> OwnedAtomIndexMapPtr;

/* Node structure for chaining in AtomDecls. */
struct AtomDeclNode
{
    JSDefinition *defn;
    AtomDeclNode *next;

    explicit AtomDeclNode(JSDefinition *defn)
      : defn(defn), next(NULL)
    {}
};

/*
 * Tagged union of a JSDefinition and an AtomDeclNode, for use in AtomDecl's
 * internal map.
 */
class DefnOrHeader
{
    union {
        JSDefinition    *defn;
        AtomDeclNode    *head;
        uintptr_t       bits;
    } u;

  public:
    DefnOrHeader() {
        u.bits = 0;
    }

    explicit DefnOrHeader(JSDefinition *defn) {
        u.defn = defn;
        JS_ASSERT(!isHeader());
    }

    explicit DefnOrHeader(AtomDeclNode *node) {
        u.head = node;
        u.bits |= 0x1;
        JS_ASSERT(isHeader());
    }

    bool isHeader() const {
        return u.bits & 0x1;
    }

    JSDefinition *defn() const {
        JS_ASSERT(!isHeader());
        return u.defn;
    }

    AtomDeclNode *header() const {
        JS_ASSERT(isHeader());
        return (AtomDeclNode *) (u.bits & ~0x1);
    }

#ifdef DEBUG
    void dump();
#endif
};

namespace tl {

template <> struct IsPodType<DefnOrHeader> {
    static const bool result = true;
};

} /* namespace tl */

/*
 * Multimap for function-scope atom declarations.
 *
 * Wraps an internal DeclOrHeader map with multi-map functionality.
 *
 * In the common case, no block scoping is used, and atoms have a single
 * associated definition. In the uncommon (block scoping) case, we map the atom
 * to a chain of definition nodes.
 */
class AtomDecls
{
    /* AtomDeclsIter needs to get at the DOHMap directly. */
    friend class AtomDeclsIter;

    JSContext   *cx;
    AtomDOHMap  *map;

    AtomDecls(const AtomDecls &other);
    void operator=(const AtomDecls &other);

    AtomDeclNode *allocNode(JSDefinition *defn);

    /*
     * Fallibly return the value in |doh| as a node.
     * Update the defn currently occupying |doh| to a node if necessary.
     */
    AtomDeclNode *lastAsNode(DefnOrHeader *doh);

  public:
    explicit AtomDecls(JSContext *cx)
      : cx(cx), map(NULL)
    {}

    ~AtomDecls();

    bool init();

    void clear() {
        map->clear();
    }

    /* Return the definition at the head of the chain for |atom|. */
    inline JSDefinition *lookupFirst(JSAtom *atom);

    /* Perform a lookup that can iterate over the definitions associated with |atom|. */
    inline MultiDeclRange lookupMulti(JSAtom *atom);

    /* Add-or-update a known-unique definition for |atom|. */
    inline bool addUnique(JSAtom *atom, JSDefinition *defn);
    bool addShadow(JSAtom *atom, JSDefinition *defn);
    bool addHoist(JSAtom *atom, JSDefinition *defn);

    /* Updating the definition for an entry that is known to exist is infallible. */
    void updateFirst(JSAtom *atom, JSDefinition *defn) {
        JS_ASSERT(map);
        AtomDOHMap::Ptr p = map->lookup(atom);
        JS_ASSERT(p);
        if (p.value().isHeader())
            p.value().header()->defn = defn;
        else
            p.value() = DefnOrHeader(defn);
    }

    /* Remove the node at the head of the chain for |atom|. */
    void remove(JSAtom *atom) {
        JS_ASSERT(map);
        AtomDOHMap::Ptr p = map->lookup(atom);
        if (!p)
            return;

        DefnOrHeader &doh = p.value();
        if (!doh.isHeader()) {
            map->remove(p);
            return;
        }

        AtomDeclNode *node = doh.header();
        AtomDeclNode *newHead = node->next;
        if (newHead)
            p.value() = DefnOrHeader(newHead);
        else
            map->remove(p);
    }

    AtomDOHMap::Range all() {
        JS_ASSERT(map);
        return map->all();
    }

#ifdef DEBUG
    void dump();
#endif
};

/*
 * Lookup state tracker for those situations where the caller wants to traverse
 * multiple definitions associated with a single atom. This occurs due to block
 * scoping.
 */
class MultiDeclRange
{
    friend class AtomDecls;

    AtomDeclNode *node;
    JSDefinition *defn;

    explicit MultiDeclRange(JSDefinition *defn) : node(NULL), defn(defn) {}
    explicit MultiDeclRange(AtomDeclNode *node) : node(node), defn(node->defn) {}

  public:
    void popFront() {
        JS_ASSERT(!empty());
        if (!node) {
            defn = NULL;
            return;
        }
        node = node->next;
        defn = node ? node->defn : NULL;
    }

    JSDefinition *front() {
        JS_ASSERT(!empty());
        return defn;
    }

    bool empty() const {
        JS_ASSERT_IF(!defn, !node);
        return !defn;
    }
};

/* Iterates over all the definitions in an AtomDecls. */
class AtomDeclsIter
{
    AtomDOHMap::Range   r;     /* Range over the map. */
    AtomDeclNode        *link; /* Optional next node in the current atom's chain. */

  public:
    explicit AtomDeclsIter(AtomDecls *decls) : r(decls->all()), link(NULL) {}

    JSDefinition *operator()() {
        if (link) {
            JS_ASSERT(link != link->next);
            JSDefinition *result = link->defn;
            link = link->next;
            JS_ASSERT(result);
            return result;
        }

        if (r.empty())
            return NULL;

        const DefnOrHeader &doh = r.front().value();
        r.popFront();

        if (!doh.isHeader())
            return doh.defn();

        JS_ASSERT(!link);
        AtomDeclNode *node = doh.header();
        link = node->next;
        return node->defn;
    }
};

typedef AtomDefnMap::Range      AtomDefnRange;
typedef AtomDefnMap::AddPtr     AtomDefnAddPtr;
typedef AtomDefnMap::Ptr        AtomDefnPtr;
typedef AtomIndexMap::AddPtr    AtomIndexAddPtr;
typedef AtomIndexMap::Ptr       AtomIndexPtr;
typedef AtomDOHMap::Ptr         AtomDOHPtr;
typedef AtomDOHMap::AddPtr      AtomDOHAddPtr;
typedef AtomDOHMap::Range       AtomDOHRange;

} /* namepsace js */

#endif
