/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SVGLocatableElement.h"
#include "DOMSVGMatrix.h"
#include "nsIFrame.h"
#include "nsISVGChildFrame.h"
#include "nsSVGRect.h"
#include "nsSVGSVGElement.h"
#include "nsSVGUtils.h"
#include "SVGContentUtils.h"

namespace mozilla {
namespace dom {

//----------------------------------------------------------------------
// nsISupports methods

NS_IMPL_ADDREF_INHERITED(SVGLocatableElement, nsSVGElement)
NS_IMPL_RELEASE_INHERITED(SVGLocatableElement, nsSVGElement)

NS_INTERFACE_MAP_BEGIN(SVGLocatableElement)
  NS_INTERFACE_MAP_ENTRY(nsIDOMSVGLocatable)
NS_INTERFACE_MAP_END_INHERITING(nsSVGElement)


//----------------------------------------------------------------------
// nsIDOMSVGLocatable methods

/* readonly attribute nsIDOMSVGElement nearestViewportElement; */
NS_IMETHODIMP
SVGLocatableElement::GetNearestViewportElement(nsIDOMSVGElement * *aNearestViewportElement)
{
  nsCOMPtr<nsIDOMSVGElement> domElem = do_QueryInterface(GetNearestViewportElement());
  domElem.forget(aNearestViewportElement);
  return NS_OK;
}

nsSVGElement*
SVGLocatableElement::GetNearestViewportElement()
{
  return SVGContentUtils::GetNearestViewportElement(this);
}

/* readonly attribute nsIDOMSVGElement farthestViewportElement; */
NS_IMETHODIMP
SVGLocatableElement::GetFarthestViewportElement(nsIDOMSVGElement * *aFarthestViewportElement)
{
  NS_IF_ADDREF(*aFarthestViewportElement = SVGContentUtils::GetOuterSVGElement(this));
  return NS_OK;
}

nsSVGElement*
SVGLocatableElement::GetFarthestViewportElement()
{
  return SVGContentUtils::GetOuterSVGElement(this);
}

/* nsIDOMSVGRect getBBox (); */
NS_IMETHODIMP
SVGLocatableElement::GetBBox(nsIDOMSVGRect **_retval)
{
  ErrorResult rv;
  *_retval = GetBBox(rv).get();
  return rv.ErrorCode();
}

already_AddRefed<nsIDOMSVGRect>
SVGLocatableElement::GetBBox(ErrorResult& rv)
{
  nsIFrame* frame = GetPrimaryFrame(Flush_Layout);

  if (!frame || (frame->GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD)) {
    rv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  nsISVGChildFrame* svgframe = do_QueryFrame(frame);
  if (!svgframe) {
    rv.Throw(NS_ERROR_NOT_IMPLEMENTED); // XXX: outer svg
    return nullptr;
  }

  nsCOMPtr<nsIDOMSVGRect> rect;
  rv = NS_NewSVGRect(getter_AddRefs(rect), nsSVGUtils::GetBBox(frame));
  return rect.forget();
}

/* DOMSVGMatrix getCTM (); */
NS_IMETHODIMP
SVGLocatableElement::GetCTM(nsISupports * *aCTM)
{
  *aCTM = GetCTM().get();
  return NS_OK;
}

already_AddRefed<DOMSVGMatrix>
SVGLocatableElement::GetCTM()
{
  nsIDocument* currentDoc = GetCurrentDoc();
  if (currentDoc) {
    // Flush all pending notifications so that our frames are up to date
    currentDoc->FlushPendingNotifications(Flush_Layout);
  }
  gfxMatrix m = SVGContentUtils::GetCTM(this, false);
  nsCOMPtr<DOMSVGMatrix> mat = m.IsSingular() ? nullptr : new DOMSVGMatrix(m);
  return mat.forget();
}

/* DOMSVGMatrix getScreenCTM (); */
NS_IMETHODIMP
SVGLocatableElement::GetScreenCTM(nsISupports * *aCTM)
{
  *aCTM = GetScreenCTM().get();
  return NS_OK;
}

already_AddRefed<DOMSVGMatrix>
SVGLocatableElement::GetScreenCTM()
{
  nsIDocument* currentDoc = GetCurrentDoc();
  if (currentDoc) {
    // Flush all pending notifications so that our frames are up to date
    currentDoc->FlushPendingNotifications(Flush_Layout);
  }
  gfxMatrix m = SVGContentUtils::GetCTM(this, true);
  nsCOMPtr<DOMSVGMatrix> mat = m.IsSingular() ? nullptr : new DOMSVGMatrix(m);
  return mat.forget();
}

/* DOMSVGMatrix getTransformToElement (in nsIDOMSVGElement element); */
NS_IMETHODIMP
SVGLocatableElement::GetTransformToElement(nsIDOMSVGElement *element,
                                           nsISupports **_retval)
{
  nsCOMPtr<nsSVGElement> elem = do_QueryInterface(element);
  if (!elem)
    return NS_ERROR_DOM_SVG_WRONG_TYPE_ERR;
  ErrorResult rv;
  *_retval = GetTransformToElement(*elem, rv).get();
  return rv.ErrorCode();
}

already_AddRefed<DOMSVGMatrix>
SVGLocatableElement::GetTransformToElement(nsSVGElement& aElement,
                                           ErrorResult& rv)
{
  nsCOMPtr<nsIDOMSVGLocatable> target = do_QueryInterface(&aElement);
  if (!target) {
    rv.Throw(NS_NOINTERFACE);
    return nullptr;
  }

  // the easiest way to do this (if likely to increase rounding error):
  nsCOMPtr<DOMSVGMatrix> ourScreenCTM = GetScreenCTM();
  nsCOMPtr<DOMSVGMatrix> targetScreenCTM;
  target->GetScreenCTM(getter_AddRefs(targetScreenCTM));
  if (!ourScreenCTM || !targetScreenCTM) {
    rv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }
  nsCOMPtr<DOMSVGMatrix> tmp = targetScreenCTM->Inverse(rv);
  if (rv.Failed()) return nullptr;

  nsCOMPtr<DOMSVGMatrix> mat = tmp->Multiply(*ourScreenCTM);
  return mat.forget();
}

} // namespace dom
} // namespace mozilla

