/* ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is about:memory glue.
 *
 * The Initial Developer of the Original Code is
 * Ms2ger <ms2ger@gmail.com>.
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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

#include "js/MemoryMetrics.h"

#include "mozilla/Assertions.h"

#include "jsapi.h"
#include "jscntxt.h"
#include "jscompartment.h"
#include "jsgc.h"
#include "jsobj.h"
#include "jsscope.h"
#include "jsscript.h"

#include "jsobjinlines.h"

#ifdef JS_THREADSAFE

namespace JS {

using namespace js;

static void
CompartmentStatsCallback(JSContext *cx, void *vdata, JSCompartment *compartment)
{
    // Append a new CompartmentStats to the vector.
    RuntimeStats *rtStats = static_cast<RuntimeStats *>(vdata);

    // CollectRuntimeStats reserves enough space.
    MOZ_ALWAYS_TRUE(rtStats->compartmentStatsVector.growBy(1));
    CompartmentStats &cStats = rtStats->compartmentStatsVector.back();
    cStats.init(rtStats->getNameCb(cx, compartment), rtStats->destroyNameCb);
    rtStats->currCompartmentStats = &cStats;

    // Get the compartment-level numbers.
#ifdef JS_METHODJIT
    cStats.mjitCode = compartment->sizeOfMjitCode();
#endif
    compartment->sizeOfTypeInferenceData(cx, &cStats.typeInferenceSizes, rtStats->mallocSizeOf);
    cStats.shapesCompartmentTables = compartment->sizeOfShapeTable(rtStats->mallocSizeOf);
}

static void
ExplicitNonHeapCompartmentCallback(JSContext *cx, void *data, JSCompartment *compartment)
{
#ifdef JS_METHODJIT
    size_t *n = static_cast<size_t *>(data);
    *n += compartment->sizeOfMjitCode();
#endif
}

static void
ChunkCallback(JSContext *cx, void *vdata, gc::Chunk *chunk)
{
    // Nb: This function is only called for dirty chunks, which is why we
    // increment gcHeapChunkDirtyDecommitted.
    RuntimeStats *rtStats = static_cast<RuntimeStats *>(vdata);
    for (size_t i = 0; i < gc::ArenasPerChunk; i++)
        if (chunk->decommittedArenas.get(i))
            rtStats->gcHeapChunkDirtyDecommitted += gc::ArenaSize;
}

static void
ArenaCallback(JSContext *cx, void *vdata, gc::Arena *arena,
              JSGCTraceKind traceKind, size_t thingSize)
{
    RuntimeStats *rtStats = static_cast<RuntimeStats *>(vdata);

    rtStats->currCompartmentStats->gcHeapArenaHeaders += sizeof(gc::ArenaHeader);
    size_t allocationSpace = arena->thingsSpan(thingSize);
    rtStats->currCompartmentStats->gcHeapArenaPadding +=
        gc::ArenaSize - allocationSpace - sizeof(gc::ArenaHeader);
    // We don't call the callback on unused things.  So we compute the
    // unused space like this:  arenaUnused = maxArenaUnused - arenaUsed.
    // We do this by setting arenaUnused to maxArenaUnused here, and then
    // subtracting thingSize for every used cell, in CellCallback().
    rtStats->currCompartmentStats->gcHeapArenaUnused += allocationSpace;
}

static void
CellCallback(JSContext *cx, void *vdata, void *thing, JSGCTraceKind traceKind,
             size_t thingSize)
{
    RuntimeStats *rtStats = static_cast<RuntimeStats *>(vdata);
    CompartmentStats *cStats = rtStats->currCompartmentStats;
    switch (traceKind) {
    case JSTRACE_OBJECT:
    {
        JSObject *obj = static_cast<JSObject *>(thing);
        if (obj->isFunction()) {
            cStats->gcHeapObjectsFunction += thingSize;
        } else {
            cStats->gcHeapObjectsNonFunction += thingSize;
        }
        size_t slotsSize, elementsSize;
        obj->sizeOfExcludingThis(rtStats->mallocSizeOf, &slotsSize, &elementsSize);
        cStats->objectSlots += slotsSize;
        cStats->objectElements += elementsSize;
        break;
    }
    case JSTRACE_STRING:
    {
        JSString *str = static_cast<JSString *>(thing);
        cStats->gcHeapStrings += thingSize;
        cStats->stringChars += str->sizeOfExcludingThis(rtStats->mallocSizeOf);
        break;
    }
    case JSTRACE_SHAPE:
    {
        Shape *shape = static_cast<Shape*>(thing);
        size_t propTableSize, kidsSize;
        shape->sizeOfExcludingThis(rtStats->mallocSizeOf, &propTableSize, &kidsSize);
        if (shape->inDictionary()) {
            cStats->gcHeapShapesDict += thingSize;
            cStats->shapesExtraDictTables += propTableSize;
            JS_ASSERT(kidsSize == 0);
        } else {
            cStats->gcHeapShapesTree += thingSize;
            cStats->shapesExtraTreeTables += propTableSize;
            cStats->shapesExtraTreeShapeKids += kidsSize;
        }
        break;
    }
    case JSTRACE_BASE_SHAPE:
    {
        cStats->gcHeapShapesBase += thingSize;
        break;
    }
    case JSTRACE_SCRIPT:
    {
        JSScript *script = static_cast<JSScript *>(thing);
        cStats->gcHeapScripts += thingSize;
        cStats->scriptData += script->sizeOfData(rtStats->mallocSizeOf);
#ifdef JS_METHODJIT
        cStats->mjitData += script->sizeOfJitScripts(rtStats->mallocSizeOf);
#endif
        break;
    }
    case JSTRACE_TYPE_OBJECT:
    {
        types::TypeObject *obj = static_cast<types::TypeObject *>(thing);
        cStats->gcHeapTypeObjects += thingSize;
        obj->sizeOfExcludingThis(&cStats->typeInferenceSizes, rtStats->mallocSizeOf);
        break;
    }
    case JSTRACE_XML:
    {
        cStats->gcHeapXML += thingSize;
        break;
    }
    }
    // Yes, this is a subtraction:  see ArenaCallback() for details.
    cStats->gcHeapArenaUnused -= thingSize;
}

JS_PUBLIC_API(bool)
CollectRuntimeStats(JSRuntime *rt, RuntimeStats *rtStats)
{
    JSContext *cx = JS_NewContext(rt, 0);
    if (!cx)
        return false;

    {
        JSAutoRequest ar(cx);

        if (!rtStats->compartmentStatsVector.reserve(rt->compartments.length()))
            return false;

        rtStats->gcHeapChunkCleanDecommitted =
            rt->gcChunkPool.countCleanDecommittedArenas(rt) * gc::ArenaSize;
        rtStats->gcHeapChunkCleanUnused =
            size_t(JS_GetGCParameter(rt, JSGC_UNUSED_CHUNKS)) * gc::ChunkSize -
            rtStats->gcHeapChunkCleanDecommitted;
        rtStats->gcHeapChunkTotal =
            size_t(JS_GetGCParameter(rt, JSGC_TOTAL_CHUNKS)) * gc::ChunkSize;

        IterateCompartmentsArenasCells(cx, rtStats, CompartmentStatsCallback,
                                       ArenaCallback, CellCallback);
        IterateChunks(cx, rtStats, ChunkCallback);

        rtStats->runtimeObject = rtStats->mallocSizeOf(rt);

        rt->sizeOfExcludingThis(rtStats->mallocSizeOf,
                                &rtStats->runtimeNormal,
                                &rtStats->runtimeTemporary,
                                &rtStats->runtimeRegexpCode,
                                &rtStats->runtimeStackCommitted);

        // Nb: we use sizeOfExcludingThis() because atomState.atoms is within
        // JSRuntime, and so counted when JSRuntime is counted.
        rtStats->runtimeAtomsTable =
            rt->atomState.atoms.sizeOfExcludingThis(rtStats->mallocSizeOf);

        JSContext *acx, *iter = NULL;
        while ((acx = JS_ContextIteratorUnlocked(rt, &iter)) != NULL)
            rtStats->runtimeContexts += acx->sizeOfIncludingThis(rtStats->mallocSizeOf);
    }

    JS_DestroyContextNoGC(cx);

    // This is initialized to all bytes stored in used chunks, and then we
    // subtract used space from it each time around the loop.
    rtStats->gcHeapChunkDirtyUnused = rtStats->gcHeapChunkTotal -
                                      rtStats->gcHeapChunkCleanUnused -
                                      rtStats->gcHeapChunkCleanDecommitted -
                                      rtStats->gcHeapChunkDirtyDecommitted;

    for (size_t index = 0;
         index < rtStats->compartmentStatsVector.length();
         index++) {
        CompartmentStats &cStats = rtStats->compartmentStatsVector[index];

        size_t used = cStats.gcHeapArenaHeaders +
                      cStats.gcHeapArenaPadding +
                      cStats.gcHeapArenaUnused +
                      cStats.gcHeapObjectsNonFunction +
                      cStats.gcHeapObjectsFunction +
                      cStats.gcHeapStrings +
                      cStats.gcHeapShapesTree +
                      cStats.gcHeapShapesDict +
                      cStats.gcHeapShapesBase +
                      cStats.gcHeapScripts +
                      cStats.gcHeapTypeObjects +
                      cStats.gcHeapXML;

        rtStats->gcHeapChunkDirtyUnused -= used;
        rtStats->gcHeapArenaUnused += cStats.gcHeapArenaUnused;
        rtStats->totalObjects += cStats.gcHeapObjectsNonFunction +
                                 cStats.gcHeapObjectsFunction +
                                 cStats.objectSlots +
                                 cStats.objectElements;
        rtStats->totalShapes  += cStats.gcHeapShapesTree +
                                 cStats.gcHeapShapesDict +
                                 cStats.gcHeapShapesBase +
                                 cStats.shapesExtraTreeTables +
                                 cStats.shapesExtraDictTables +
                                 cStats.shapesCompartmentTables;
        rtStats->totalScripts += cStats.gcHeapScripts +
                                 cStats.scriptData;
        rtStats->totalStrings += cStats.gcHeapStrings +
                                 cStats.stringChars;
#ifdef JS_METHODJIT
        rtStats->totalMjit    += cStats.mjitCode +
                                 cStats.mjitData;
#endif
        rtStats->totalTypeInference += cStats.gcHeapTypeObjects +
                                       cStats.typeInferenceSizes.objects +
                                       cStats.typeInferenceSizes.scripts +
                                       cStats.typeInferenceSizes.tables;
        rtStats->totalAnalysisTemp  += cStats.typeInferenceSizes.temporary;
    }

    size_t numDirtyChunks = (rtStats->gcHeapChunkTotal -
                             rtStats->gcHeapChunkCleanUnused) /
                            gc::ChunkSize;
    size_t perChunkAdmin =
        sizeof(gc::Chunk) - (sizeof(gc::Arena) * gc::ArenasPerChunk);
    rtStats->gcHeapChunkAdmin = numDirtyChunks * perChunkAdmin;
    rtStats->gcHeapChunkDirtyUnused -= rtStats->gcHeapChunkAdmin;

    // Why 10000x?  100x because it's a percentage, and another 100x
    // because nsIMemoryReporter requires that for percentage amounts so
    // they can be fractional.
    rtStats->gcHeapUnusedPercentage = (rtStats->gcHeapChunkCleanUnused +
                                       rtStats->gcHeapChunkDirtyUnused +
                                       rtStats->gcHeapChunkCleanDecommitted +
                                       rtStats->gcHeapChunkDirtyDecommitted +
                                       rtStats->gcHeapArenaUnused) * 10000 /
                                       rtStats->gcHeapChunkTotal;

    return true;
}

JS_PUBLIC_API(bool)
GetExplicitNonHeapForRuntime(JSRuntime *rt, int64_t *amount,
                             JSMallocSizeOfFun mallocSizeOf)
{
    JSContext *cx = JS_NewContext(rt, 0);
    if (!cx)
        return false;

    // explicit/<compartment>/gc-heap/*
    *amount = int64_t(JS_GetGCParameter(rt, JSGC_TOTAL_CHUNKS)) *
              gc::ChunkSize;

    {
        JSAutoRequest ar(cx);

        // explicit/<compartment>/mjit-code
        size_t n = 0;
        IterateCompartments(cx, &n, ExplicitNonHeapCompartmentCallback);
        *amount += n;

        // explicit/runtime/regexp-code
        // explicit/runtime/stack-committed
        size_t regexpCode, stackCommitted;
        rt->sizeOfExcludingThis(mallocSizeOf,
                                NULL,
                                NULL,
                                &regexpCode,
                                &stackCommitted);

        *amount += regexpCode;
        *amount += stackCommitted;
    }

    JS_DestroyContextNoGC(cx);

    return true;
}

} // namespace JS

#endif // JS_THREADSAFE
