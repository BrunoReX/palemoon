/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=78:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef JSGC_GENERATIONAL

#include "jscompartment.h"
#include "jsgc.h"
#include "jstypedarray.h"
#include "jsutil.h"

#include "gc/GCInternals.h"
#include "gc/Memory.h"
#include "vm/Debugger.h"

#include "jscompartmentinlines.h"
#include "jsobjinlines.h"

#include "gc/Barrier-inl.h"
#include "gc/Nursery-inl.h"

using namespace js;
using namespace gc;
using namespace mozilla;

bool
js::Nursery::init()
{
    JS_ASSERT(start() == 0);

    if (!hugeSlots.init())
        return false;

    fallbackBitmap.clear(false);

    void *heap = MapAlignedPages(runtime(), NurserySize, Alignment);
#ifdef JSGC_ROOT_ANALYSIS
    // Our poison pointers are not guaranteed to be invalid on 64-bit
    // architectures, and often are valid. We can't just reserve the full
    // poison range, because it might already have been taken up by something
    // else (shared library, previous allocation). So we'll just loop and
    // discard poison pointers until we get something valid.
    //
    // This leaks all of these poisoned pointers. It would be better if they
    // were marked as uncommitted, but it's a little complicated to avoid
    // clobbering pre-existing unrelated mappings.
    while (IsPoisonedPtr(heap) || IsPoisonedPtr((void*)(uintptr_t(heap) + NurserySize)))
        heap = MapAlignedPages(NurserySize, Alignment);
#endif
    if (!heap)
        return false;

    JSRuntime *rt = runtime();
    rt->gcNurseryStart_ = uintptr_t(heap);
    rt->gcNurseryEnd_ = chunk(LastNurseryChunk).end();
    numActiveChunks_ = 1;
    setCurrentChunk(0);
    JS_POISON(heap, FreshNursery, NurserySize);
    for (int i = 0; i < NumNurseryChunks; ++i)
        chunk(i).runtime = rt;

    JS_ASSERT(isEnabled());
    return true;
}

js::Nursery::~Nursery()
{
    if (start())
        UnmapPages(runtime(), (void *)start(), NurserySize);
}

void
js::Nursery::enable()
{
    if (isEnabled())
        return;
    JS_ASSERT(position_ == start());
    numActiveChunks_ = 1;
    setCurrentChunk(0);
}

void
js::Nursery::disable()
{
    if (!isEnabled())
        return;
    JS_ASSERT(position_ == start());
    numActiveChunks_ = 0;
    currentEnd_ = 0;
}

void *
js::Nursery::allocate(size_t size)
{
    JS_ASSERT(size % ThingAlignment == 0);
    JS_ASSERT(position() % ThingAlignment == 0);
    JS_ASSERT(!runtime()->isHeapBusy());

    if (position() + size > currentEnd()) {
        if (currentChunk_ + 1 == numActiveChunks_)
            return NULL;
        setCurrentChunk(currentChunk_ + 1);
    }

    void *thing = (void *)position();
    position_ = position() + size;

    JS_POISON(thing, AllocatedThing, size);
    return thing;
}

/* Internally, this function is used to allocate elements as well as slots. */
HeapSlot *
js::Nursery::allocateSlots(JSContext *cx, JSObject *obj, uint32_t nslots)
{
    JS_ASSERT(obj);
    JS_ASSERT(nslots > 0);

    if (!isInside(obj))
        return cx->pod_malloc<HeapSlot>(nslots);

    if (nslots > MaxNurserySlots)
        return allocateHugeSlots(cx, nslots);

    size_t size = sizeof(HeapSlot) * nslots;
    HeapSlot *slots = static_cast<HeapSlot *>(allocate(size));
    if (slots)
        return slots;

    return allocateHugeSlots(cx, nslots);
}

ObjectElements *
js::Nursery::allocateElements(JSContext *cx, JSObject *obj, uint32_t nelems)
{
    return reinterpret_cast<ObjectElements *>(allocateSlots(cx, obj, nelems));
}

HeapSlot *
js::Nursery::reallocateSlots(JSContext *cx, JSObject *obj, HeapSlot *oldSlots,
                             uint32_t oldCount, uint32_t newCount)
{
    size_t oldSize = oldCount * sizeof(HeapSlot);
    size_t newSize = newCount * sizeof(HeapSlot);

    if (!isInside(obj))
        return static_cast<HeapSlot *>(cx->realloc_(oldSlots, oldSize, newSize));

    if (!isInside(oldSlots)) {
        HeapSlot *newSlots = static_cast<HeapSlot *>(cx->realloc_(oldSlots, oldSize, newSize));
        if (oldSlots != newSlots) {
            hugeSlots.remove(oldSlots);
            /* If this put fails, we will only leak the slots. */
            (void)hugeSlots.put(newSlots);
        }
        return newSlots;
    }

    /* The nursery cannot make use of the returned slots data. */
    if (newCount < oldCount)
        return oldSlots;

    HeapSlot *newSlots = allocateSlots(cx, obj, newCount);
    PodCopy(newSlots, oldSlots, oldCount);
    return newSlots;
}

ObjectElements *
js::Nursery::reallocateElements(JSContext *cx, JSObject *obj, ObjectElements *oldHeader,
                                uint32_t oldCount, uint32_t newCount)
{
    HeapSlot *slots = reallocateSlots(cx, obj, reinterpret_cast<HeapSlot *>(oldHeader),
                                      oldCount, newCount);
    return reinterpret_cast<ObjectElements *>(slots);
}

HeapSlot *
js::Nursery::allocateHugeSlots(JSContext *cx, size_t nslots)
{
    HeapSlot *slots = cx->pod_malloc<HeapSlot>(nslots);
    /* If this put fails, we will only leak the slots. */
    (void)hugeSlots.put(slots);
    return slots;
}

void
js::Nursery::notifyInitialSlots(Cell *cell, HeapSlot *slots)
{
    if (isInside(cell) && !isInside(slots)) {
        /* If this put fails, we will only leak the slots. */
        (void)hugeSlots.put(slots);
    }
}

namespace js {
namespace gc {

class MinorCollectionTracer : public JSTracer
{
  public:
    Nursery *nursery;
    AutoTraceSession session;

    /* Amount of data moved to the tenured generation during collection. */
    size_t tenuredSize;

    /*
     * This list is threaded through the Nursery using the space from already
     * moved things. The list is used to fix up the moved things and to find
     * things held live by intra-Nursery pointers.
     */
    RelocationOverlay *head;
    RelocationOverlay **tail;

    /* Save and restore all of the runtime state we use during MinorGC. */
    AutoDisableProxyCheck disableStrictProxyChecking;

    /* Insert the given relocation entry into the list of things to visit. */
    JS_ALWAYS_INLINE void insertIntoFixupList(RelocationOverlay *entry) {
        *tail = entry;
        tail = &entry->next_;
        *tail = NULL;
    }

    MinorCollectionTracer(JSRuntime *rt, Nursery *nursery)
      : JSTracer(),
        nursery(nursery),
        session(rt, MinorCollecting),
        tenuredSize(0),
        head(NULL),
        tail(&head),
        disableStrictProxyChecking(rt)
    {
        JS_TracerInit(this, rt, Nursery::MinorGCCallback);
        eagerlyTraceWeakMaps = TraceWeakMapKeysValues;
        rt->gcNumber++;
    }
};

} /* namespace gc */
} /* namespace js */

static AllocKind
GetObjectAllocKindForCopy(JSRuntime *rt, JSObject *obj)
{
    if (obj->isArray()) {
        JS_ASSERT(obj->numFixedSlots() == 0);

        /* Use minimal size object if we are just going to copy the pointer. */
        if (!IsInsideNursery(rt, (void *)obj->getElementsHeader()))
            return FINALIZE_OBJECT0_BACKGROUND;

        size_t nelements = obj->getDenseCapacity();
        return GetBackgroundAllocKind(GetGCArrayKind(nelements));
    }

    if (obj->is<JSFunction>())
        return obj->as<JSFunction>().getAllocKind();

    AllocKind kind = GetGCObjectFixedSlotsKind(obj->numFixedSlots());
    if (CanBeFinalizedInBackground(kind, obj->getClass()))
        kind = GetBackgroundAllocKind(kind);
    return kind;
}

void *
js::Nursery::allocateFromTenured(Zone *zone, AllocKind thingKind)
{
    void *t = zone->allocator.arenas.allocateFromFreeList(thingKind, Arena::thingSize(thingKind));
    if (t)
        return t;
    zone->allocator.arenas.checkEmptyFreeList(thingKind);
    return zone->allocator.arenas.allocateFromArena(zone, thingKind);
}

void *
js::Nursery::moveToTenured(MinorCollectionTracer *trc, JSObject *src)
{
    Zone *zone = src->zone();
    AllocKind dstKind = GetObjectAllocKindForCopy(trc->runtime, src);
    JSObject *dst = static_cast<JSObject *>(allocateFromTenured(zone, dstKind));
    if (!dst)
        MOZ_CRASH();

    trc->tenuredSize += moveObjectToTenured(dst, src, dstKind);

    RelocationOverlay *overlay = reinterpret_cast<RelocationOverlay *>(src);
    overlay->forwardTo(dst);
    trc->insertIntoFixupList(overlay);

    return static_cast<void *>(dst);
}

size_t
js::Nursery::moveObjectToTenured(JSObject *dst, JSObject *src, AllocKind dstKind)
{
    size_t srcSize = Arena::thingSize(dstKind);
    size_t tenuredSize = srcSize;

    /*
     * Arrays do not necessarily have the same AllocKind between src and dst.
     * We deal with this by copying elements manually, possibly re-inlining
     * them if there is adequate room inline in dst.
     */
    if (src->isArray())
        srcSize = sizeof(ObjectImpl);

    js_memcpy(dst, src, srcSize);
    tenuredSize += moveSlotsToTenured(dst, src, dstKind);
    tenuredSize += moveElementsToTenured(dst, src, dstKind);

    /* The shape's list head may point into the old object. */
    if (&src->shape_ == dst->shape_->listp)
        dst->shape_->listp = &dst->shape_;

    return tenuredSize;
}

size_t
js::Nursery::moveSlotsToTenured(JSObject *dst, JSObject *src, AllocKind dstKind)
{
    /* Fixed slots have already been copied over. */
    if (!src->hasDynamicSlots())
        return 0;

    if (!isInside(src->slots)) {
        hugeSlots.remove(src->slots);
        return 0;
    }

    Zone *zone = src->zone();
    size_t count = src->numDynamicSlots();
    dst->slots = zone->pod_malloc<HeapSlot>(count);
    PodCopy(dst->slots, src->slots, count);
    return count * sizeof(HeapSlot);
}

size_t
js::Nursery::moveElementsToTenured(JSObject *dst, JSObject *src, AllocKind dstKind)
{
    if (src->hasEmptyElements())
        return 0;

    Zone *zone = src->zone();
    ObjectElements *srcHeader = src->getElementsHeader();
    ObjectElements *dstHeader;

    /* TODO Bug 874151: Prefer to put element data inline if we have space. */
    if (!isInside(srcHeader)) {
        JS_ASSERT(src->elements == dst->elements);
        hugeSlots.remove(reinterpret_cast<HeapSlot*>(srcHeader));
        return 0;
    }

    /* ArrayBuffer stores byte-length, not Value count. */
    if (src->is<ArrayBufferObject>()) {
        size_t nbytes = sizeof(ObjectElements) + srcHeader->initializedLength;
        if (src->hasDynamicElements()) {
            dstHeader = static_cast<ObjectElements *>(zone->malloc_(nbytes));
            if (!dstHeader)
                MOZ_CRASH();
        } else {
            dst->setFixedElements();
            dstHeader = dst->getElementsHeader();
        }
        js_memcpy(dstHeader, srcHeader, nbytes);
        dst->elements = dstHeader->elements();
        return src->hasDynamicElements() ? nbytes : 0;
    }

    size_t nslots = ObjectElements::VALUES_PER_HEADER + srcHeader->capacity;

    /* Unlike other objects, Arrays can have fixed elements. */
    if (src->isArray() && nslots <= GetGCKindSlots(dstKind)) {
        dst->setFixedElements();
        dstHeader = dst->getElementsHeader();
        js_memcpy(dstHeader, srcHeader, nslots * sizeof(HeapSlot));
        return nslots * sizeof(HeapSlot);
    }

    size_t nbytes = nslots * sizeof(HeapValue);
    dstHeader = static_cast<ObjectElements *>(zone->malloc_(nbytes));
    if (!dstHeader)
        MOZ_CRASH();
    js_memcpy(dstHeader, srcHeader, nslots * sizeof(HeapSlot));
    dst->elements = dstHeader->elements();
    return nslots * sizeof(HeapSlot);
}

static bool
ShouldMoveToTenured(MinorCollectionTracer *trc, void **thingp)
{
    Cell *cell = static_cast<Cell *>(*thingp);
    Nursery &nursery = *trc->nursery;
    return !nursery.isInside(thingp) && nursery.isInside(cell) &&
           !nursery.getForwardedPointer(thingp);
}

/* static */ void
js::Nursery::MinorGCCallback(JSTracer *jstrc, void **thingp, JSGCTraceKind kind)
{
    MinorCollectionTracer *trc = static_cast<MinorCollectionTracer *>(jstrc);
    if (ShouldMoveToTenured(trc, thingp))
        *thingp = trc->nursery->moveToTenured(trc, static_cast<JSObject *>(*thingp));
}

void
js::Nursery::markFallback(Cell *cell)
{
    JS_ASSERT(uintptr_t(cell) >= start());
    size_t offset = uintptr_t(cell) - start();
    JS_ASSERT(offset < heapEnd() - start());
    JS_ASSERT(offset % ThingAlignment == 0);
    fallbackBitmap.set(offset / ThingAlignment);
}

void
js::Nursery::moveFallbackToTenured(gc::MinorCollectionTracer *trc)
{
    for (size_t i = 0; i < FallbackBitmapBits; ++i) {
        if (fallbackBitmap.get(i)) {
            JSObject *src = reinterpret_cast<JSObject *>(start() + i * ThingAlignment);
            moveToTenured(trc, src);
        }
    }
    fallbackBitmap.clear(false);
}

/* static */ void
js::Nursery::MinorFallbackMarkingCallback(JSTracer *jstrc, void **thingp, JSGCTraceKind kind)
{
    MinorCollectionTracer *trc = static_cast<MinorCollectionTracer *>(jstrc);
    if (ShouldMoveToTenured(trc, thingp))
        trc->nursery->markFallback(static_cast<JSObject *>(*thingp));
}

/* static */ void
js::Nursery::MinorFallbackFixupCallback(JSTracer *jstrc, void **thingp, JSGCTraceKind kind)
{
    MinorCollectionTracer *trc = static_cast<MinorCollectionTracer *>(jstrc);
    if (trc->nursery->isInside(*thingp))
        trc->nursery->getForwardedPointer(thingp);
}

static void
TraceHeapWithCallback(JSTracer *trc, JSTraceCallback callback)
{
    JSTraceCallback prior = trc->callback;

    AutoCopyFreeListToArenas copy(trc->runtime);
    trc->callback = callback;
    for (ZonesIter zone(trc->runtime); !zone.done(); zone.next()) {
        for (size_t i = 0; i < FINALIZE_LIMIT; ++i) {
            AllocKind kind = AllocKind(i);
            for (CellIterUnderGC cells(zone, kind); !cells.done(); cells.next())
                JS_TraceChildren(trc, cells.getCell(), MapAllocToTraceKind(kind));
        }
    }

    trc->callback = prior;
}

void
js::Nursery::markStoreBuffer(MinorCollectionTracer *trc)
{
    JSRuntime *rt = trc->runtime;
    if (!rt->gcStoreBuffer.hasOverflowed()) {
        rt->gcStoreBuffer.mark(trc);
        return;
    }

    /*
     * If the store buffer has overflowed, we need to walk the full heap to
     * discover cross-generation edges. Since we cannot easily walk the heap
     * while simultaneously allocating, we use a three pass algorithm:
     *   1) Walk the major heap and mark live things in the nursery in a
     *      pre-allocated bitmap.
     *   2) Use the bitmap to move all live nursery things to the tenured
     *      heap.
     *   3) Walk the heap a second time to find and update all of the moved
     *      references in the tenured heap.
     */
    TraceHeapWithCallback(trc, MinorFallbackMarkingCallback);
    moveFallbackToTenured(trc);
    TraceHeapWithCallback(trc, MinorFallbackFixupCallback);
}

void
js::Nursery::collect(JSRuntime *rt, JS::gcreason::Reason reason)
{
    JS_AbortIfWrongThread(rt);

    if (rt->mainThread.suppressGC)
        return;

    if (!isEnabled())
        return;

    if (position() == start())
        return;

    rt->gcHelperThread.waitBackgroundSweepEnd();

    AutoStopVerifyingBarriers av(rt, false);

    /* Move objects pointed to by roots from the nursery to the major heap. */
    MinorCollectionTracer trc(rt, this);
    MarkRuntime(&trc);
    Debugger::markAll(&trc);
    for (CompartmentsIter comp(rt); !comp.done(); comp.next()) {
        comp->markAllCrossCompartmentWrappers(&trc);
        comp->markAllInitialShapeTableEntries(&trc);
    }
    markStoreBuffer(&trc);
    rt->newObjectCache.clearNurseryObjects(rt);

    /*
     * Most of the work is done here. This loop iterates over objects that have
     * been moved to the major heap. If these objects have any outgoing pointers
     * to the nursery, then those nursery objects get moved as well, until no
     * objects are left to move. That is, we iterate to a fixed point.
     */
    for (RelocationOverlay *p = trc.head; p; p = p->next()) {
        JSObject *obj = static_cast<JSObject*>(p->forwardingAddress());
        JS_TraceChildren(&trc, obj, MapAllocToTraceKind(obj->tenuredGetAllocKind()));
    }

    /* Resize the nursery. */
    double promotionRate = trc.tenuredSize / double(allocationEnd() - start());
    if (promotionRate > 0.5)
        growAllocableSpace();
    else if (promotionRate < 0.1)
        shrinkAllocableSpace();

    /* Sweep. */
    sweep(rt->defaultFreeOp());
    rt->gcStoreBuffer.clear();

    /*
     * We ignore gcMaxBytes when allocating for minor collection. However, if we
     * overflowed, we disable the nursery. The next time we allocate, we'll fail
     * because gcBytes >= gcMaxBytes.
     */
    if (rt->gcBytes >= rt->gcMaxBytes)
        disable();
}


void
js::Nursery::sweep(FreeOp *fop)
{
    for (HugeSlotsSet::Range r = hugeSlots.all(); !r.empty(); r.popFront())
        fop->free_(r.front());
    hugeSlots.clear();

#ifdef DEBUG
    JS_POISON((void *)start(), SweptNursery, NurserySize - sizeof(JSRuntime *));
    for (int i = 0; i < NumNurseryChunks; ++i)
        chunk(i).runtime = runtime();
#endif

    setCurrentChunk(0);
}

void
js::Nursery::growAllocableSpace()
{
    numActiveChunks_ = Min(numActiveChunks_ * 2, NumNurseryChunks);
}

void
js::Nursery::shrinkAllocableSpace()
{
    numActiveChunks_ = Max(numActiveChunks_ - 1, 1);
}

#endif /* JSGC_GENERATIONAL */
