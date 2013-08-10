/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/SVGAnimateMotionElement.h"
#include "mozilla/dom/SVGAnimateMotionElementBinding.h"

DOMCI_NODE_DATA(SVGAnimateMotionElement, mozilla::dom::SVGAnimateMotionElement)

NS_IMPL_NS_NEW_NAMESPACED_SVG_ELEMENT(AnimateMotion)

namespace mozilla {
namespace dom {

JSObject*
SVGAnimateMotionElement::WrapNode(JSContext *aCx, JSObject *aScope, bool *aTriedToWrap)
{
  return SVGAnimateMotionElementBinding::Wrap(aCx, aScope, this, aTriedToWrap);
}

//----------------------------------------------------------------------
// nsISupports methods

NS_IMPL_ADDREF_INHERITED(SVGAnimateMotionElement, SVGAnimationElement)
NS_IMPL_RELEASE_INHERITED(SVGAnimateMotionElement, SVGAnimationElement)

NS_INTERFACE_TABLE_HEAD(SVGAnimateMotionElement)
  NS_NODE_INTERFACE_TABLE5(SVGAnimateMotionElement, nsIDOMNode,
                           nsIDOMElement, nsIDOMSVGElement,
                           nsIDOMSVGAnimationElement,
                           nsIDOMSVGAnimateMotionElement)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(SVGAnimateMotionElement)
NS_INTERFACE_MAP_END_INHERITING(SVGAnimationElement)

//----------------------------------------------------------------------
// Implementation

SVGAnimateMotionElement::SVGAnimateMotionElement(already_AddRefed<nsINodeInfo> aNodeInfo)
  : SVGAnimationElement(aNodeInfo)
{
}

//----------------------------------------------------------------------
// nsIDOMNode methods

NS_IMPL_ELEMENT_CLONE_WITH_INIT(SVGAnimateMotionElement)

//----------------------------------------------------------------------
// nsISMILAnimationElement methods

nsSMILAnimationFunction&
SVGAnimateMotionElement::AnimationFunction()
{
  return mAnimationFunction;
}

bool
SVGAnimateMotionElement::GetTargetAttributeName(int32_t *aNamespaceID,
                                                nsIAtom **aLocalName) const
{
  // <animateMotion> doesn't take an attributeName, since it doesn't target an
  // 'attribute' per se.  We'll use a unique dummy attribute-name so that our
  // nsSMILTargetIdentifier logic (which requires a attribute name) still works.
  *aNamespaceID = kNameSpaceID_None;
  *aLocalName = nsGkAtoms::mozAnimateMotionDummyAttr;
  return true;
}

nsSMILTargetAttrType
SVGAnimateMotionElement::GetTargetAttributeType() const
{
  // <animateMotion> doesn't take an attributeType, since it doesn't target an
  // 'attribute' per se.  We'll just return 'XML' for simplicity.  (This just
  // needs to match what we expect in nsSVGElement::GetAnimAttr.)
  return eSMILTargetAttrType_XML;
}

} // namespace dom
} // namespace mozilla

