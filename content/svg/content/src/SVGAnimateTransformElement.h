/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_SVGAnimateTransformElement_h
#define mozilla_dom_SVGAnimateTransformElement_h

#include "mozilla/dom/SVGAnimationElement.h"
#include "nsIDOMSVGAnimateTransformElement.h"
#include "nsSMILAnimationFunction.h"

nsresult NS_NewSVGAnimateTransformElement(nsIContent **aResult,
                                          already_AddRefed<nsINodeInfo> aNodeInfo);

namespace mozilla {
namespace dom {

class SVGAnimateTransformElement MOZ_FINAL : public SVGAnimationElement,
                                             public nsIDOMSVGAnimateTransformElement
{
protected:
  SVGAnimateTransformElement(already_AddRefed<nsINodeInfo> aNodeInfo);

  nsSMILAnimationFunction mAnimationFunction;
  friend nsresult
    (::NS_NewSVGAnimateTransformElement(nsIContent **aResult,
                                        already_AddRefed<nsINodeInfo> aNodeInfo));

  virtual JSObject* WrapNode(JSContext *aCx, JSObject *aScope, bool *aTriedToWrap) MOZ_OVERRIDE;

public:
  // interfaces:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIDOMSVGANIMATETRANSFORMELEMENT

  NS_FORWARD_NSIDOMNODE_TO_NSINODE
  NS_FORWARD_NSIDOMELEMENT_TO_GENERIC
  NS_FORWARD_NSIDOMSVGELEMENT(SVGAnimationElement::)
  NS_FORWARD_NSIDOMSVGANIMATIONELEMENT(SVGAnimationElement::)

  // nsIDOMNode specializations
  virtual nsresult Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const;

  // Element specializations
  bool ParseAttribute(int32_t aNamespaceID,
                        nsIAtom* aAttribute,
                        const nsAString& aValue,
                        nsAttrValue& aResult);

  // nsISMILAnimationElement
  virtual nsSMILAnimationFunction& AnimationFunction();
  virtual bool GetTargetAttributeName(int32_t *aNamespaceID,
                                      nsIAtom **aLocalName) const;

  virtual nsXPCClassInfo* GetClassInfo();

  virtual nsIDOMNode* AsDOMNode() { return this; }
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_SVGAnimateTransformElement_h
