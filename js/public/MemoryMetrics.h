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

#ifndef js_MemoryMetrics_h
#define js_MemoryMetrics_h

/*
 * These declarations are not within jsapi.h because they are highly likely
 * to change in the future. Depend on them at your own risk.
 */

#include <string.h>

#include "jsalloc.h"
#include "jspubtd.h"

#include "js/Utility.h"
#include "js/Vector.h"

namespace JS {

/* Data for tracking analysis/inference memory usage. */
struct TypeInferenceSizes
{
    size_t scripts;
    size_t objects;
    size_t tables;
    size_t temporary;
};

typedef void* (* GetNameCallback)(JSContext *cx, JSCompartment *c);
typedef void (* DestroyNameCallback)(void *string);

struct CompartmentStats
{
    CompartmentStats()
    {
        memset(this, 0, sizeof(*this));
    }

    void init(void *name_, DestroyNameCallback destroyName)
    {
        name = name_;
        destroyNameCb = destroyName;
    }

    ~CompartmentStats()
    {
        destroyNameCb(name);
    }

    // Pointer to an nsCString, which we can't use here.
    void *name;
    DestroyNameCallback destroyNameCb;

    size_t gcHeapArenaHeaders;
    size_t gcHeapArenaPadding;
    size_t gcHeapArenaUnused;

    size_t gcHeapObjectsNonFunction;
    size_t gcHeapObjectsFunction;
    size_t gcHeapStrings;
    size_t gcHeapShapesTree;
    size_t gcHeapShapesDict;
    size_t gcHeapShapesBase;
    size_t gcHeapScripts;
    size_t gcHeapTypeObjects;
    size_t gcHeapXML;

    size_t objectSlots;
    size_t objectElements;
    size_t stringChars;
    size_t shapesExtraTreeTables;
    size_t shapesExtraDictTables;
    size_t shapesExtraTreeShapeKids;
    size_t shapesCompartmentTables;
    size_t scriptData;

#ifdef JS_METHODJIT
    size_t mjitCode;
    size_t mjitData;
#endif
    TypeInferenceSizes typeInferenceSizes;
};

struct RuntimeStats
{
    RuntimeStats(JSMallocSizeOfFun mallocSizeOf, GetNameCallback getNameCb,
                 DestroyNameCallback destroyNameCb)
      : runtimeObject(0)
      , runtimeAtomsTable(0)
      , runtimeContexts(0)
      , runtimeNormal(0)
      , runtimeTemporary(0)
      , runtimeRegexpCode(0)
      , runtimeStackCommitted(0)
      , gcHeapChunkTotal(0)
      , gcHeapChunkCleanUnused(0)
      , gcHeapChunkDirtyUnused(0)
      , gcHeapChunkCleanDecommitted(0)
      , gcHeapChunkDirtyDecommitted(0)
      , gcHeapArenaUnused(0)
      , gcHeapChunkAdmin(0)
      , gcHeapUnusedPercentage(0)
      , totalObjects(0)
      , totalShapes(0)
      , totalScripts(0)
      , totalStrings(0)
#ifdef JS_METHODJIT
      , totalMjit(0)
#endif
      , totalTypeInference(0)
      , totalAnalysisTemp(0)
      , compartmentStatsVector()
      , currCompartmentStats(NULL)
      , mallocSizeOf(mallocSizeOf)
      , getNameCb(getNameCb)
      , destroyNameCb(destroyNameCb)
    {}

    size_t runtimeObject;
    size_t runtimeAtomsTable;
    size_t runtimeContexts;
    size_t runtimeNormal;
    size_t runtimeTemporary;
    size_t runtimeRegexpCode;
    size_t runtimeStackCommitted;
    size_t gcHeapChunkTotal;
    size_t gcHeapChunkCleanUnused;
    size_t gcHeapChunkDirtyUnused;
    size_t gcHeapChunkCleanDecommitted;
    size_t gcHeapChunkDirtyDecommitted;
    size_t gcHeapArenaUnused;
    size_t gcHeapChunkAdmin;
    size_t gcHeapUnusedPercentage;
    size_t totalObjects;
    size_t totalShapes;
    size_t totalScripts;
    size_t totalStrings;
#ifdef JS_METHODJIT
    size_t totalMjit;
#endif
    size_t totalTypeInference;
    size_t totalAnalysisTemp;

    js::Vector<CompartmentStats, 0, js::SystemAllocPolicy> compartmentStatsVector;
    CompartmentStats *currCompartmentStats;

    JSMallocSizeOfFun mallocSizeOf;
    GetNameCallback getNameCb;
    DestroyNameCallback destroyNameCb;
};

#ifdef JS_THREADSAFE

extern JS_PUBLIC_API(bool)
CollectRuntimeStats(JSRuntime *rt, RuntimeStats *rtStats);

extern JS_PUBLIC_API(bool)
GetExplicitNonHeapForRuntime(JSRuntime *rt, int64_t *amount,
                             JSMallocSizeOfFun mallocSizeOf);

#endif /* JS_THREADSAFE */

extern JS_PUBLIC_API(size_t)
SystemCompartmentCount(const JSRuntime *rt);

extern JS_PUBLIC_API(size_t)
UserCompartmentCount(const JSRuntime *rt);

} // namespace JS

#endif // js_MemoryMetrics_h
