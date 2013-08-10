/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SVGLocatableElement_h
#define SVGLocatableElement_h

#include "nsSVGElement.h"
#include "nsIDOMSVGLocatable.h"

#define MOZILLA_SVGLOCATABLEELEMENT_IID \
  { 0xe20176ba, 0xc48d, 0x4704, \
    {0x89, 0xec, 0xe6, 0x69, 0x6c, 0xb7, 0xb8, 0xb3} }

namespace mozilla {
class DOMSVGMatrix;

namespace dom {
class SVGLocatableElement : public nsSVGElement,
                            public nsIDOMSVGLocatable
{
public:
  SVGLocatableElement(already_AddRefed<nsINodeInfo> aNodeInfo)
    : nsSVGElement(aNodeInfo) {}
  virtual ~SVGLocatableElement() {}

  NS_DECLARE_STATIC_IID_ACCESSOR(MOZILLA_SVGLOCATABLEELEMENT_IID)
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIDOMSVGLOCATABLE

  // WebIDL
  nsSVGElement* GetNearestViewportElement();
  nsSVGElement* GetFarthestViewportElement();
  already_AddRefed<nsIDOMSVGRect> GetBBox(ErrorResult& rv);
  already_AddRefed<DOMSVGMatrix> GetCTM();
  already_AddRefed<DOMSVGMatrix> GetScreenCTM();
  already_AddRefed<DOMSVGMatrix> GetTransformToElement(nsSVGElement& aElement,
                                                       ErrorResult& rv);
};

NS_DEFINE_STATIC_IID_ACCESSOR(SVGLocatableElement,
                              MOZILLA_SVGLOCATABLEELEMENT_IID)

} // namespace dom
} // namespace mozilla

#endif // SVGLocatableElement_h

