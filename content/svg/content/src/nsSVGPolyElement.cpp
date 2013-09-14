/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsSVGPolyElement.h"
#include "DOMSVGPointList.h"
#include "gfxContext.h"
#include "SVGContentUtils.h"

using namespace mozilla;

//----------------------------------------------------------------------
// nsISupports methods

NS_IMPL_ADDREF_INHERITED(nsSVGPolyElement,nsSVGPolyElementBase)
NS_IMPL_RELEASE_INHERITED(nsSVGPolyElement,nsSVGPolyElementBase)

NS_INTERFACE_MAP_BEGIN(nsSVGPolyElement)
NS_INTERFACE_MAP_END_INHERITING(nsSVGPolyElementBase)

//----------------------------------------------------------------------
// Implementation

nsSVGPolyElement::nsSVGPolyElement(already_AddRefed<nsINodeInfo> aNodeInfo)
  : nsSVGPolyElementBase(aNodeInfo)
{
}

already_AddRefed<DOMSVGPointList>
nsSVGPolyElement::Points()
{
  void *key = mPoints.GetBaseValKey();
  nsRefPtr<DOMSVGPointList> points = DOMSVGPointList::GetDOMWrapper(key, this, false);
  return points.forget();
}

already_AddRefed<DOMSVGPointList>
nsSVGPolyElement::AnimatedPoints()
{
  void *key = mPoints.GetAnimValKey();
  nsRefPtr<DOMSVGPointList> points = DOMSVGPointList::GetDOMWrapper(key, this, true);
  return points.forget();
}


//----------------------------------------------------------------------
// nsIContent methods

NS_IMETHODIMP_(bool)
nsSVGPolyElement::IsAttributeMapped(const nsIAtom* name) const
{
  static const MappedAttributeEntry* const map[] = {
    sMarkersMap
  };
  
  return FindAttributeDependence(name, map) ||
    nsSVGPolyElementBase::IsAttributeMapped(name);
}

//----------------------------------------------------------------------
// nsSVGPathGeometryElement methods

bool
nsSVGPolyElement::AttributeDefinesGeometry(const nsIAtom *aName)
{
  if (aName == nsGkAtoms::points)
    return true;

  return false;
}

void
nsSVGPolyElement::GetMarkPoints(nsTArray<nsSVGMark> *aMarks)
{
  const SVGPointList &points = mPoints.GetAnimValue();

  if (!points.Length())
    return;

  float px = 0.0, py = 0.0, prevAngle = 0.0;

  for (uint32_t i = 0; i < points.Length(); ++i) {
    float x = points[i].mX;
    float y = points[i].mY;
    float angle = atan2(y-py, x-px);
    if (i == 1)
      aMarks->ElementAt(aMarks->Length() - 1).angle = angle;
    else if (i > 1)
      aMarks->ElementAt(aMarks->Length() - 1).angle =
        SVGContentUtils::AngleBisect(prevAngle, angle);

    aMarks->AppendElement(nsSVGMark(x, y, 0));

    prevAngle = angle;
    px = x;
    py = y;
  }

  aMarks->ElementAt(aMarks->Length() - 1).angle = prevAngle;
}

void
nsSVGPolyElement::ConstructPath(gfxContext *aCtx)
{
  const SVGPointList &points = mPoints.GetAnimValue();

  if (!points.Length())
    return;

  aCtx->MoveTo(points[0]);
  for (uint32_t i = 1; i < points.Length(); ++i) {
    aCtx->LineTo(points[i]);
  }
}

