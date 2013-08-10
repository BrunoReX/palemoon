/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_SVGAnimateMotionElement_h
#define mozilla_dom_SVGAnimateMotionElement_h

#include "mozilla/dom/SVGAnimationElement.h"
#include "nsIDOMSVGAnimateMotionElement.h"
#include "SVGMotionSMILAnimationFunction.h"

nsresult NS_NewSVGAnimateMotionElement(nsIContent **aResult,
                                       already_AddRefed<nsINodeInfo> aNodeInfo);

namespace mozilla {
namespace dom {

class SVGAnimateMotionElement MOZ_FINAL : public SVGAnimationElement,
                                          public nsIDOMSVGAnimateMotionElement
{
protected:
  SVGAnimateMotionElement(already_AddRefed<nsINodeInfo> aNodeInfo);

  SVGMotionSMILAnimationFunction mAnimationFunction;
  friend nsresult
    (::NS_NewSVGAnimateMotionElement(nsIContent **aResult,
                                     already_AddRefed<nsINodeInfo> aNodeInfo));

  virtual JSObject* WrapNode(JSContext *aCx, JSObject *aScope, bool *aTriedToWrap) MOZ_OVERRIDE;

public:
  // interfaces:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIDOMSVGANIMATEMOTIONELEMENT

  NS_FORWARD_NSIDOMNODE_TO_NSINODE
  NS_FORWARD_NSIDOMELEMENT_TO_GENERIC
  NS_FORWARD_NSIDOMSVGELEMENT(SVGAnimationElement::)
  NS_FORWARD_NSIDOMSVGANIMATIONELEMENT(SVGAnimationElement::)

  // nsIDOMNode specializations
  virtual nsresult Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const;

  // nsISMILAnimationElement
  virtual nsSMILAnimationFunction& AnimationFunction();
  virtual bool GetTargetAttributeName(int32_t *aNamespaceID,
                                        nsIAtom **aLocalName) const;
  virtual nsSMILTargetAttrType GetTargetAttributeType() const;

  // nsSVGElement
  virtual nsIAtom* GetPathDataAttrName() const {
    return nsGkAtoms::path;
  }

  // Utility method to let our <mpath> children tell us when they've changed,
  // so we can make sure our mAnimationFunction is marked as having changed.
  void MpathChanged() { mAnimationFunction.MpathChanged(); }

  virtual nsXPCClassInfo* GetClassInfo();

  virtual nsIDOMNode* AsDOMNode() { return this; }
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_SVGAnimateMotionElement_h
