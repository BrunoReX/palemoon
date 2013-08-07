/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
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
 * The Original Code is Mozilla Corporation code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Matt Woodrow <mwoodrow@mozilla.com>
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

#include "LayerSorter.h"
#include "DirectedGraph.h"
#include "limits.h"
#include "gfxLineSegment.h"

namespace mozilla {
namespace layers {

enum LayerSortOrder {
  Undefined,
  ABeforeB,
  BBeforeA,
};

/**
 * Recover the z component from a 2d transformed point by finding the intersection
 * of a line through the point in the z direction and the transformed plane.
 *
 * We want to solve:
 *
 * point = normal . (p0 - l0) / normal . l
 */
static gfxFloat RecoverZDepth(const gfx3DMatrix& aTransform, const gfxPoint& aPoint)
{
    const gfxPoint3D l(0, 0, 1);
    gfxPoint3D l0 = gfxPoint3D(aPoint.x, aPoint.y, 0);
    gfxPoint3D p0 = aTransform.Transform3D(gfxPoint3D(0, 0, 0));
    gfxPoint3D normal = aTransform.GetNormalVector();

    gfxFloat n = normal.DotProduct(p0 - l0); 
    gfxFloat d = normal.DotProduct(l);

    if (!d) {
        return 0;
    }

    return n/d;
}

/**
 * Determine if this transform layer should be drawn before another when they 
 * are both preserve-3d children.
 *
 * We want to find the relative z depths of the 2 layers at points where they
 * intersect when projected onto the 2d screen plane. Intersections are defined
 * as corners that are positioned within the other quad, as well as intersections
 * of the lines.
 *
 * We then choose the intersection point with the greatest difference in Z
 * depths and use this point to determine an ordering for the two layers.
 * For layers that are intersecting in 3d space, this essentially guesses an 
 * order. In a lot of cases we only intersect right at the edge point (3d cubes
 * in particular) and this generates the 'correct' looking ordering. For planes
 * that truely intersect, then there is no correct ordering and this remains
 * unsolved without changing our rendering code.
 */
static LayerSortOrder CompareDepth(Layer* aOne, Layer* aTwo) {
  gfxRect ourRect = aOne->GetEffectiveVisibleRegion().GetBounds();
  gfxRect otherRect = aTwo->GetEffectiveVisibleRegion().GetBounds();

  gfx3DMatrix ourTransform = aOne->GetTransform();
  gfx3DMatrix otherTransform = aTwo->GetTransform();

  // Transform both rectangles and project into 2d space.
  gfxQuad ourTransformedRect = ourTransform.TransformRect(ourRect);
  gfxQuad otherTransformedRect = otherTransform.TransformRect(otherRect);

  gfxRect ourBounds = ourTransformedRect.GetBounds();
  gfxRect otherBounds = otherTransformedRect.GetBounds();

  if (!ourBounds.Intersects(otherBounds)) {
    return Undefined;
  }

  // Make a list of all points that are within the other rect.
  // Could we just check Contains() on the bounds rects. ie, is it possible
  // for layers to overlap without intersections (in 2d space) and yet still
  // have their bounds rects not completely enclose each other?
  nsTArray<gfxPoint> points;
  for (PRUint32 i = 0; i < 4; i++) {
    if (ourTransformedRect.Contains(otherTransformedRect.mPoints[i])) {
      points.AppendElement(otherTransformedRect.mPoints[i]);
    }
    if (otherTransformedRect.Contains(ourTransformedRect.mPoints[i])) {
      points.AppendElement(ourTransformedRect.mPoints[i]);
    }
  }
  
  // Look for intersections between lines (in 2d space) and use these as
  // depth testing points.
  for (PRUint32 i = 0; i < 4; i++) {
    for (PRUint32 j = 0; j < 4; j++) {
      gfxPoint intersection;
      gfxLineSegment one(ourTransformedRect.mPoints[i],
                         ourTransformedRect.mPoints[(i + 1) % 4]);
      gfxLineSegment two(otherTransformedRect.mPoints[j],
                         otherTransformedRect.mPoints[(j + 1) % 4]);
      if (one.Intersects(two, intersection)) {
        points.AppendElement(intersection);
      }
    }
  }

  // No intersections, no defined order between these layers.
  if (points.IsEmpty()) {
    return Undefined;
  }

  // Find the relative Z depths of each intersection point and check that the layers are in the same order.
  gfxFloat highest = 0;
  for (PRUint32 i = 0; i < points.Length(); i++) {
    gfxFloat ourDepth = RecoverZDepth(ourTransform, points.ElementAt(i));
    gfxFloat otherDepth = RecoverZDepth(otherTransform, points.ElementAt(i));

    gfxFloat difference = otherDepth - ourDepth;

    if (fabs(difference) > fabs(highest)) {
      highest = difference;
    }
  }
  // If layers have the same depth keep the original order
  if (highest >= 0) {
    return ABeforeB;
  } else {
    return BBeforeA;
  }
}

#ifdef DEBUG
static bool gDumpLayerSortList = getenv("MOZ_DUMP_LAYER_SORT_LIST") != 0;

static void DumpLayerList(nsTArray<Layer*>& aLayers)
{
  for (PRUint32 i = 0; i < aLayers.Length(); i++) {
    fprintf(stderr, "%p, ", aLayers.ElementAt(i));
  }
  fprintf(stderr, "\n");
}

static void DumpEdgeList(DirectedGraph<Layer*>& aGraph)
{
  nsTArray<DirectedGraph<Layer*>::Edge> edges = aGraph.GetEdgeList();
  
  for (PRUint32 i = 0; i < edges.Length(); i++) {
    fprintf(stderr, "From: %p, To: %p\n", edges.ElementAt(i).mFrom, edges.ElementAt(i).mTo);
  }
}
#endif

// The maximum number of layers that we will attempt to sort. Anything
// greater than this will be left unsorted. We should consider enabling
// depth buffering for the scene in this case.
#define MAX_SORTABLE_LAYERS 100

void SortLayersBy3DZOrder(nsTArray<Layer*>& aLayers)
{
  PRUint32 nodeCount = aLayers.Length();
  if (nodeCount > MAX_SORTABLE_LAYERS) {
    return;
  }
  DirectedGraph<Layer*> graph;

#ifdef DEBUG
  if (gDumpLayerSortList) {
    fprintf(stderr, " --- Layers before sorting: --- \n");
    DumpLayerList(aLayers);
  }
#endif

  // Iterate layers and determine edges.
  for (PRUint32 i = 0; i < nodeCount; i++) {
    for (PRUint32 j = i + 1; j < nodeCount; j++) {
      Layer* a = aLayers.ElementAt(i);
      Layer* b = aLayers.ElementAt(j);
      LayerSortOrder order = CompareDepth(a, b);
      if (order == ABeforeB) {
        graph.AddEdge(a, b);
      } else if (order == BBeforeA) {
        graph.AddEdge(b, a);
      }
    }
  }

#ifdef DEBUG
  if (gDumpLayerSortList) {
    fprintf(stderr, " --- Edge List: --- \n");
    DumpEdgeList(graph);
  }
#endif

  // Build a new array using the graph.
  nsTArray<Layer*> noIncoming;
  nsTArray<Layer*> sortedList;

  // Make a list of all layers with no incoming edges.
  noIncoming.AppendElements(aLayers);
  const nsTArray<DirectedGraph<Layer*>::Edge>& edges = graph.GetEdgeList();
  for (PRUint32 i = 0; i < edges.Length(); i++) {
    noIncoming.RemoveElement(edges.ElementAt(i).mTo);
  }

  // Move each item without incoming edges into the sorted list,
  // and remove edges from it.
  do {
    if (!noIncoming.IsEmpty()) {
      PRUint32 last = noIncoming.Length() - 1;

      Layer* layer = noIncoming.ElementAt(last);

      noIncoming.RemoveElementAt(last);
      sortedList.AppendElement(layer);

      nsTArray<DirectedGraph<Layer*>::Edge> outgoing;
      graph.GetEdgesFrom(layer, outgoing);
      for (PRUint32 i = 0; i < outgoing.Length(); i++) {
        DirectedGraph<Layer*>::Edge edge = outgoing.ElementAt(i);
        graph.RemoveEdge(edge);
        if (!graph.NumEdgesTo(edge.mTo)) {
          // If this node also has no edges now, add it to the list
          noIncoming.AppendElement(edge.mTo);
        }
      }
    }

    // If there are no nodes without incoming edges, but there
    // are still edges, then we have a cycle.
    if (noIncoming.IsEmpty() && graph.GetEdgeCount()) {
      // Find the node with the least incoming edges.
      PRUint32 minEdges = UINT_MAX;
      Layer* minNode = nsnull;
      for (PRUint32 i = 0; i < aLayers.Length(); i++) {
        PRUint32 edgeCount = graph.NumEdgesTo(aLayers.ElementAt(i));
        if (edgeCount && edgeCount < minEdges) {
          minEdges = edgeCount;
          minNode = aLayers.ElementAt(i);
          if (minEdges == 1) {
            break;
          }
        }
      }

      // Remove all of them!
      graph.RemoveEdgesTo(minNode);
      noIncoming.AppendElement(minNode);
    }
  } while (!noIncoming.IsEmpty());
  NS_ASSERTION(!graph.GetEdgeCount(), "Cycles detected!");
#ifdef DEBUG
  if (gDumpLayerSortList) {
    fprintf(stderr, " --- Layers after sorting: --- \n");
    DumpLayerList(sortedList);
  }
#endif

  aLayers.Clear();
  aLayers.AppendElements(sortedList);
}

}
}
