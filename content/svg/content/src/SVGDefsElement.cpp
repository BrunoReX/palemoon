/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/SVGDefsElement.h"
#include "mozilla/dom/SVGDefsElementBinding.h"

DOMCI_NODE_DATA(SVGDefsElement, mozilla::dom::SVGDefsElement)

NS_IMPL_NS_NEW_NAMESPACED_SVG_ELEMENT(Defs)

namespace mozilla {
namespace dom {

JSObject*
SVGDefsElement::WrapNode(JSContext *aCx, JSObject *aScope, bool *aTriedToWrap)
{
  return SVGDefsElementBinding::Wrap(aCx, aScope, this, aTriedToWrap);
}

//----------------------------------------------------------------------
// nsISupports methods

NS_IMPL_ADDREF_INHERITED(SVGDefsElement, SVGGraphicsElement)
NS_IMPL_RELEASE_INHERITED(SVGDefsElement, SVGGraphicsElement)

NS_INTERFACE_TABLE_HEAD(SVGDefsElement)
  NS_NODE_INTERFACE_TABLE4(SVGDefsElement, nsIDOMNode, nsIDOMElement,
                           nsIDOMSVGElement,
                           nsIDOMSVGDefsElement)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(SVGDefsElement)
NS_INTERFACE_MAP_END_INHERITING(SVGGraphicsElement)

//----------------------------------------------------------------------
// Implementation

SVGDefsElement::SVGDefsElement(already_AddRefed<nsINodeInfo> aNodeInfo)
  : SVGGraphicsElement(aNodeInfo)
{
  SetIsDOMBinding();
}

//----------------------------------------------------------------------
// nsIDOMNode methods


NS_IMPL_ELEMENT_CLONE_WITH_INIT(SVGDefsElement)


//----------------------------------------------------------------------
// nsIContent methods

NS_IMETHODIMP_(bool)
SVGDefsElement::IsAttributeMapped(const nsIAtom* name) const
{
  static const MappedAttributeEntry* const map[] = {
    sFEFloodMap,
    sFiltersMap,
    sFontSpecificationMap,
    sGradientStopMap,
    sLightingEffectsMap,
    sMarkersMap,
    sTextContentElementsMap,
    sViewportsMap
  };

  return FindAttributeDependence(name, map) ||
    SVGGraphicsElement::IsAttributeMapped(name);
}

} // namespace dom
} // namespace mozilla

