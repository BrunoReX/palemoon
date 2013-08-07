/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * The Original Code is Mozilla SVG Project code.
 *
 * The Initial Developer of the Original Code is the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
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

#include "SVGPathSegUtils.h"
#include "nsSVGElement.h"
#include "nsSVGSVGElement.h"
#include "nsSVGPathDataParser.h"
#include "nsString.h"
#include "nsSVGUtils.h"
#include "nsContentUtils.h"
#include "nsTextFormatter.h"
#include "prdtoa.h"
#include <limits>
#include "nsMathUtils.h"
#include "prtypes.h"

using namespace mozilla;

static const float PATH_SEG_LENGTH_TOLERANCE = 0.0000001f;
static const PRUint32 MAX_RECURSION = 10;


/* static */ void
SVGPathSegUtils::GetValueAsString(const float* aSeg, nsAString& aValue)
{
  // Adding new seg type? Is the formatting below acceptable for the new types?
  PR_STATIC_ASSERT(NS_SVG_PATH_SEG_LAST_VALID_TYPE ==
                     nsIDOMSVGPathSeg::PATHSEG_CURVETO_QUADRATIC_SMOOTH_REL);
  PR_STATIC_ASSERT(NS_SVG_PATH_SEG_MAX_ARGS == 7);

  PRUint32 type = DecodeType(aSeg[0]);
  PRUnichar typeAsChar = GetPathSegTypeAsLetter(type);

  // Special case arcs:
  if (IsArcType(type)) {
    bool largeArcFlag = aSeg[4] != 0.0f;
    bool sweepFlag = aSeg[5] != 0.0f;
    nsTextFormatter::ssprintf(aValue,
                              NS_LITERAL_STRING("%c%g,%g %g %d,%d %g,%g").get(),
                              typeAsChar, aSeg[1], aSeg[2], aSeg[3],
                              largeArcFlag, sweepFlag, aSeg[6], aSeg[7]);
  } else {

    switch (ArgCountForType(type)) {
    case 0:
      aValue = typeAsChar;
      break;

    case 1:
      nsTextFormatter::ssprintf(aValue, NS_LITERAL_STRING("%c%g").get(),
                                typeAsChar, aSeg[1]);
      break;

    case 2:
      nsTextFormatter::ssprintf(aValue, NS_LITERAL_STRING("%c%g,%g").get(),
                                typeAsChar, aSeg[1], aSeg[2]);
      break;

    case 4:
      nsTextFormatter::ssprintf(aValue, NS_LITERAL_STRING("%c%g,%g %g,%g").get(),
                                typeAsChar, aSeg[1], aSeg[2], aSeg[3], aSeg[4]);
      break;

    case 6:
      nsTextFormatter::ssprintf(aValue,
                                NS_LITERAL_STRING("%c%g,%g %g,%g %g,%g").get(),
                                typeAsChar, aSeg[1], aSeg[2], aSeg[3], aSeg[4],
                                aSeg[5], aSeg[6]);
      break;

    default:
      NS_ABORT_IF_FALSE(false, "Unknown segment type");
      aValue = NS_LITERAL_STRING("<unknown-segment-type>").get();
      return;
    }
  }
  
  // nsTextFormatter::ssprintf is one of the nsTextFormatter methods that
  // randomly appends '\0' to its output string, which means that the length
  // of the output string is one too long. We need to manually remove that '\0'
  // until nsTextFormatter is fixed.
  //
  if (aValue[aValue.Length() - 1] == PRUnichar('\0')) {
    aValue.SetLength(aValue.Length() - 1);
  }
}


static float
CalcDistanceBetweenPoints(const gfxPoint& aP1, const gfxPoint& aP2)
{
  return NS_hypot(aP2.x - aP1.x, aP2.y - aP1.y);
}


static void
SplitQuadraticBezier(const gfxPoint* aCurve, gfxPoint* aLeft, gfxPoint* aRight)
{
  aLeft[0].x = aCurve[0].x;
  aLeft[0].y = aCurve[0].y;
  aRight[2].x = aCurve[2].x;
  aRight[2].y = aCurve[2].y;
  aLeft[1].x = (aCurve[0].x + aCurve[1].x) / 2;
  aLeft[1].y = (aCurve[0].y + aCurve[1].y) / 2;
  aRight[1].x = (aCurve[1].x + aCurve[2].x) / 2;
  aRight[1].y = (aCurve[1].y + aCurve[2].y) / 2;
  aLeft[2].x = aRight[0].x = (aLeft[1].x + aRight[1].x) / 2;
  aLeft[2].y = aRight[0].y = (aLeft[1].y + aRight[1].y) / 2;
}

static void
SplitCubicBezier(const gfxPoint* aCurve, gfxPoint* aLeft, gfxPoint* aRight)
{
  gfxPoint tmp;
  tmp.x = (aCurve[1].x + aCurve[2].x) / 4;
  tmp.y = (aCurve[1].y + aCurve[2].y) / 4;
  aLeft[0].x = aCurve[0].x;
  aLeft[0].y = aCurve[0].y;
  aRight[3].x = aCurve[3].x;
  aRight[3].y = aCurve[3].y;
  aLeft[1].x = (aCurve[0].x + aCurve[1].x) / 2;
  aLeft[1].y = (aCurve[0].y + aCurve[1].y) / 2;
  aRight[2].x = (aCurve[2].x + aCurve[3].x) / 2;
  aRight[2].y = (aCurve[2].y + aCurve[3].y) / 2;
  aLeft[2].x = aLeft[1].x / 2 + tmp.x;
  aLeft[2].y = aLeft[1].y / 2 + tmp.y;
  aRight[1].x = aRight[2].x / 2 + tmp.x;
  aRight[1].y = aRight[2].y / 2 + tmp.y;
  aLeft[3].x = aRight[0].x = (aLeft[2].x + aRight[1].x) / 2;
  aLeft[3].y = aRight[0].y = (aLeft[2].y + aRight[1].y) / 2;
}

static gfxFloat
CalcBezLengthHelper(gfxPoint* aCurve, PRUint32 aNumPts,
                    PRUint32 aRecursionCount,
                    void (*aSplit)(const gfxPoint*, gfxPoint*, gfxPoint*))
{
  gfxPoint left[4];
  gfxPoint right[4];
  gfxFloat length = 0, dist;
  for (PRUint32 i = 0; i < aNumPts - 1; i++) {
    length += CalcDistanceBetweenPoints(aCurve[i], aCurve[i+1]);
  }
  dist = CalcDistanceBetweenPoints(aCurve[0], aCurve[aNumPts - 1]);
  if (length - dist > PATH_SEG_LENGTH_TOLERANCE &&
      aRecursionCount < MAX_RECURSION) {
    aSplit(aCurve, left, right);
    ++aRecursionCount;
    return CalcBezLengthHelper(left, aNumPts, aRecursionCount, aSplit) +
           CalcBezLengthHelper(right, aNumPts, aRecursionCount, aSplit);
  }
  return length;
}

static inline gfxFloat
CalcLengthOfCubicBezier(const gfxPoint& aPos, const gfxPoint &aCP1,
                        const gfxPoint& aCP2, const gfxPoint &aTo)
{
  gfxPoint curve[4] = { aPos, aCP1, aCP2, aTo };
  return CalcBezLengthHelper(curve, 4, 0, SplitCubicBezier);
}

static inline gfxFloat
CalcLengthOfQuadraticBezier(const gfxPoint& aPos, const gfxPoint& aCP,
                            const gfxPoint& aTo)
{
  gfxPoint curve[3] = { aPos, aCP, aTo };
  return CalcBezLengthHelper(curve, 3, 0, SplitQuadraticBezier);
}


static void
TraverseClosePath(const float* aArgs, SVGPathTraversalState& aState)
{
  if (aState.ShouldUpdateLengthAndControlPoints()) {
    aState.length += CalcDistanceBetweenPoints(aState.pos, aState.start);
    aState.cp1 = aState.cp2 = aState.start;
  }
  aState.pos = aState.start;
}

static void
TraverseMovetoAbs(const float* aArgs, SVGPathTraversalState& aState)
{
  aState.start = aState.pos = gfxPoint(aArgs[0], aArgs[1]);
  if (aState.ShouldUpdateLengthAndControlPoints()) {
    // aState.length is unchanged, since move commands don't affect path length.
    aState.cp1 = aState.cp2 = aState.start;
  }
}

static void
TraverseMovetoRel(const float* aArgs, SVGPathTraversalState& aState)
{
  aState.start = aState.pos += gfxPoint(aArgs[0], aArgs[1]);
  if (aState.ShouldUpdateLengthAndControlPoints()) {
    // aState.length is unchanged, since move commands don't affect path length.
    aState.cp1 = aState.cp2 = aState.start;
  }
}

static void
TraverseLinetoAbs(const float* aArgs, SVGPathTraversalState& aState)
{
  gfxPoint to(aArgs[0], aArgs[1]);
  if (aState.ShouldUpdateLengthAndControlPoints()) {
    aState.length += CalcDistanceBetweenPoints(aState.pos, to);
    aState.cp1 = aState.cp2 = to;
  }
  aState.pos = to;
}

static void
TraverseLinetoRel(const float* aArgs, SVGPathTraversalState& aState)
{
  gfxPoint to = aState.pos + gfxPoint(aArgs[0], aArgs[1]);
  if (aState.ShouldUpdateLengthAndControlPoints()) {
    aState.length += CalcDistanceBetweenPoints(aState.pos, to);
    aState.cp1 = aState.cp2 = to;
  }
  aState.pos = to;
}

static void
TraverseLinetoHorizontalAbs(const float* aArgs, SVGPathTraversalState& aState)
{
  gfxPoint to(aArgs[0], aState.pos.y);
  if (aState.ShouldUpdateLengthAndControlPoints()) {
    aState.length += fabs(to.x - aState.pos.x);
    aState.cp1 = aState.cp2 = to;
  }
  aState.pos = to;
}

static void
TraverseLinetoHorizontalRel(const float* aArgs, SVGPathTraversalState& aState)
{
  aState.pos.x += aArgs[0];
  if (aState.ShouldUpdateLengthAndControlPoints()) {
    aState.length += fabs(aArgs[0]);
    aState.cp1 = aState.cp2 = aState.pos;
  }
}

static void
TraverseLinetoVerticalAbs(const float* aArgs, SVGPathTraversalState& aState)
{
  gfxPoint to(aState.pos.x, aArgs[0]);
  if (aState.ShouldUpdateLengthAndControlPoints()) {
    aState.length += fabs(to.y - aState.pos.y);
    aState.cp1 = aState.cp2 = to;
  }
  aState.pos = to;
}

static void
TraverseLinetoVerticalRel(const float* aArgs, SVGPathTraversalState& aState)
{
  aState.pos.y += aArgs[0];
  if (aState.ShouldUpdateLengthAndControlPoints()) {
    aState.length += fabs(aArgs[0]);
    aState.cp1 = aState.cp2 = aState.pos;
  }
}

static void
TraverseCurvetoCubicAbs(const float* aArgs, SVGPathTraversalState& aState)
{
  gfxPoint to(aArgs[4], aArgs[5]);
  if (aState.ShouldUpdateLengthAndControlPoints()) {
    gfxPoint cp1(aArgs[0], aArgs[1]);
    gfxPoint cp2(aArgs[2], aArgs[3]);
    aState.length += (float)CalcLengthOfCubicBezier(aState.pos, cp1, cp2, to);
    aState.cp2 = cp2;
    aState.cp1 = to;
  }
  aState.pos = to;
}

static void
TraverseCurvetoCubicSmoothAbs(const float* aArgs, SVGPathTraversalState& aState)
{
  gfxPoint to(aArgs[2], aArgs[3]);
  if (aState.ShouldUpdateLengthAndControlPoints()) {
    gfxPoint cp1 = aState.pos - (aState.cp2 - aState.pos);
    gfxPoint cp2(aArgs[0], aArgs[1]);
    aState.length += (float)CalcLengthOfCubicBezier(aState.pos, cp1, cp2, to);
    aState.cp2 = cp2;
    aState.cp1 = to;
  }
  aState.pos = to;
}

static void
TraverseCurvetoCubicRel(const float* aArgs, SVGPathTraversalState& aState)
{
  gfxPoint to = aState.pos + gfxPoint(aArgs[4], aArgs[5]);
  if (aState.ShouldUpdateLengthAndControlPoints()) {
    gfxPoint cp1 = aState.pos + gfxPoint(aArgs[0], aArgs[1]);
    gfxPoint cp2 = aState.pos + gfxPoint(aArgs[2], aArgs[3]);
    aState.length += (float)CalcLengthOfCubicBezier(aState.pos, cp1, cp2, to);
    aState.cp2 = cp2;
    aState.cp1 = to;
  }
  aState.pos = to;
}

static void
TraverseCurvetoCubicSmoothRel(const float* aArgs, SVGPathTraversalState& aState)
{
  gfxPoint to = aState.pos + gfxPoint(aArgs[2], aArgs[3]);
  if (aState.ShouldUpdateLengthAndControlPoints()) {
    gfxPoint cp1 = aState.pos - (aState.cp2 - aState.pos);
    gfxPoint cp2 = aState.pos + gfxPoint(aArgs[0], aArgs[1]);
    aState.length += (float)CalcLengthOfCubicBezier(aState.pos, cp1, cp2, to);
    aState.cp2 = cp2;
    aState.cp1 = to;
  }
  aState.pos = to;
}

static void
TraverseCurvetoQuadraticAbs(const float* aArgs, SVGPathTraversalState& aState)
{
  gfxPoint to(aArgs[2], aArgs[3]);
  if (aState.ShouldUpdateLengthAndControlPoints()) {
    gfxPoint cp(aArgs[0], aArgs[1]);
    aState.length += (float)CalcLengthOfQuadraticBezier(aState.pos, cp, to);
    aState.cp1 = cp;
    aState.cp2 = to;
  }
  aState.pos = to;
}

static void
TraverseCurvetoQuadraticSmoothAbs(const float* aArgs,
                                  SVGPathTraversalState& aState)
{
  gfxPoint to(aArgs[0], aArgs[1]);
  if (aState.ShouldUpdateLengthAndControlPoints()) {
    gfxPoint cp = aState.pos - (aState.cp1 - aState.pos);
    aState.length += (float)CalcLengthOfQuadraticBezier(aState.pos, cp, to);
    aState.cp1 = cp;
    aState.cp2 = to;
  }
  aState.pos = to;
}

static void
TraverseCurvetoQuadraticRel(const float* aArgs, SVGPathTraversalState& aState)
{
  gfxPoint to = aState.pos + gfxPoint(aArgs[2], aArgs[3]);
  if (aState.ShouldUpdateLengthAndControlPoints()) {
    gfxPoint cp = aState.pos + gfxPoint(aArgs[0], aArgs[1]);
    aState.length += (float)CalcLengthOfQuadraticBezier(aState.pos, cp, to);
    aState.cp1 = cp;
    aState.cp2 = to;
  }
  aState.pos = to;
}

static void
TraverseCurvetoQuadraticSmoothRel(const float* aArgs,
                                  SVGPathTraversalState& aState)
{
  gfxPoint to = aState.pos + gfxPoint(aArgs[0], aArgs[1]);
  if (aState.ShouldUpdateLengthAndControlPoints()) {
    gfxPoint cp = aState.pos - (aState.cp1 - aState.pos);
    aState.length += (float)CalcLengthOfQuadraticBezier(aState.pos, cp, to);
    aState.cp1 = cp;
    aState.cp2 = to;
  }
  aState.pos = to;
}

static void
TraverseArcAbs(const float* aArgs, SVGPathTraversalState& aState)
{
  gfxPoint to(aArgs[5], aArgs[6]);
  if (aState.ShouldUpdateLengthAndControlPoints()) {
    float dist = 0;
    gfxPoint radii(aArgs[0], aArgs[1]);
    gfxPoint bez[4] = { aState.pos, gfxPoint(0, 0),
                        gfxPoint(0, 0), gfxPoint(0, 0) };
    nsSVGArcConverter converter(aState.pos, to, radii, aArgs[2],
                                aArgs[3] != 0, aArgs[4] != 0);
    while (converter.GetNextSegment(&bez[1], &bez[2], &bez[3])) {
      dist += CalcBezLengthHelper(bez, 4, 0, SplitCubicBezier);
      bez[0] = bez[3];
    }
    aState.length += dist;
    aState.cp1 = aState.cp2 = to;
  }
  aState.pos = to;
}

static void
TraverseArcRel(const float* aArgs, SVGPathTraversalState& aState)
{
  gfxPoint to = aState.pos + gfxPoint(aArgs[5], aArgs[6]);
  if (aState.ShouldUpdateLengthAndControlPoints()) {
    float dist = 0;
    gfxPoint radii(aArgs[0], aArgs[1]);
    gfxPoint bez[4] = { aState.pos, gfxPoint(0, 0),
                        gfxPoint(0, 0), gfxPoint(0, 0) };
    nsSVGArcConverter converter(aState.pos, to, radii, aArgs[2],
                                aArgs[3] != 0, aArgs[4] != 0);
    while (converter.GetNextSegment(&bez[1], &bez[2], &bez[3])) {
      dist += CalcBezLengthHelper(bez, 4, 0, SplitCubicBezier);
      bez[0] = bez[3];
    }
    aState.length += dist;
    aState.cp1 = aState.cp2 = to;
  }
  aState.pos = to;
}


typedef void (*TraverseFunc)(const float*, SVGPathTraversalState&);

static TraverseFunc gTraverseFuncTable[NS_SVG_PATH_SEG_TYPE_COUNT] = {
  nsnull, //  0 == PATHSEG_UNKNOWN
  TraverseClosePath,
  TraverseMovetoAbs,
  TraverseMovetoRel,
  TraverseLinetoAbs,
  TraverseLinetoRel,
  TraverseCurvetoCubicAbs,
  TraverseCurvetoCubicRel,
  TraverseCurvetoQuadraticAbs,
  TraverseCurvetoQuadraticRel,
  TraverseArcAbs,
  TraverseArcRel,
  TraverseLinetoHorizontalAbs,
  TraverseLinetoHorizontalRel,
  TraverseLinetoVerticalAbs,
  TraverseLinetoVerticalRel,
  TraverseCurvetoCubicSmoothAbs,
  TraverseCurvetoCubicSmoothRel,
  TraverseCurvetoQuadraticSmoothAbs,
  TraverseCurvetoQuadraticSmoothRel
};

/* static */ void
SVGPathSegUtils::TraversePathSegment(const float* aData,
                                     SVGPathTraversalState& aState)
{
  PR_STATIC_ASSERT(NS_ARRAY_LENGTH(gTraverseFuncTable) ==
                     NS_SVG_PATH_SEG_TYPE_COUNT);
  PRUint32 type = DecodeType(aData[0]);
  gTraverseFuncTable[type](aData + 1, aState);
}
