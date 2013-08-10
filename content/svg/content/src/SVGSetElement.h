/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_SVGSetElement_h
#define mozilla_dom_SVGSetElement_h

#include "mozilla/dom/SVGAnimationElement.h"
#include "nsIDOMSVGSetElement.h"
#include "nsSMILSetAnimationFunction.h"

nsresult NS_NewSVGSetElement(nsIContent **aResult,
                             already_AddRefed<nsINodeInfo> aNodeInfo);

namespace mozilla {
namespace dom {

class SVGSetElement MOZ_FINAL : public SVGAnimationElement,
                                public nsIDOMSVGSetElement
{
protected:
  SVGSetElement(already_AddRefed<nsINodeInfo> aNodeInfo);

  nsSMILSetAnimationFunction mAnimationFunction;

  friend nsresult (::NS_NewSVGSetElement(nsIContent **aResult,
                                         already_AddRefed<nsINodeInfo> aNodeInfo));

  virtual JSObject* WrapNode(JSContext *aCx, JSObject *aScope, bool *aTriedToWrap) MOZ_OVERRIDE;

public:
  // interfaces:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIDOMSVGSETELEMENT

  NS_FORWARD_NSIDOMNODE_TO_NSINODE
  NS_FORWARD_NSIDOMELEMENT_TO_GENERIC
  NS_FORWARD_NSIDOMSVGELEMENT(SVGAnimationElement::)
  NS_FORWARD_NSIDOMSVGANIMATIONELEMENT(SVGAnimationElement::)

  // nsIDOMNode
  virtual nsresult Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const;

  // nsISMILAnimationElement
  virtual nsSMILAnimationFunction& AnimationFunction();

  virtual nsXPCClassInfo* GetClassInfo();

  virtual nsIDOMNode* AsDOMNode() { return this; }
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_SVGSetElement_h
