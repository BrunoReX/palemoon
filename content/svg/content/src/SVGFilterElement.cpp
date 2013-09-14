/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Util.h"

#include "nsGkAtoms.h"
#include "nsCOMPtr.h"
#include "mozilla/dom/SVGFilterElement.h"
#include "mozilla/dom/SVGFilterElementBinding.h"
#include "nsSVGUtils.h"

NS_IMPL_NS_NEW_NAMESPACED_SVG_ELEMENT(Filter)

namespace mozilla {
namespace dom {

JSObject*
SVGFilterElement::WrapNode(JSContext *aCx, JS::Handle<JSObject*> aScope)
{
  return SVGFilterElementBinding::Wrap(aCx, aScope, this);
}

nsSVGElement::LengthInfo SVGFilterElement::sLengthInfo[4] =
{
  { &nsGkAtoms::x, -10, nsIDOMSVGLength::SVG_LENGTHTYPE_PERCENTAGE, SVGContentUtils::X },
  { &nsGkAtoms::y, -10, nsIDOMSVGLength::SVG_LENGTHTYPE_PERCENTAGE, SVGContentUtils::Y },
  { &nsGkAtoms::width, 120, nsIDOMSVGLength::SVG_LENGTHTYPE_PERCENTAGE, SVGContentUtils::X },
  { &nsGkAtoms::height, 120, nsIDOMSVGLength::SVG_LENGTHTYPE_PERCENTAGE, SVGContentUtils::Y },
};

nsSVGElement::IntegerPairInfo SVGFilterElement::sIntegerPairInfo[1] =
{
  { &nsGkAtoms::filterRes, 0 }
};

nsSVGElement::EnumInfo SVGFilterElement::sEnumInfo[2] =
{
  { &nsGkAtoms::filterUnits,
    sSVGUnitTypesMap,
    SVG_UNIT_TYPE_OBJECTBOUNDINGBOX
  },
  { &nsGkAtoms::primitiveUnits,
    sSVGUnitTypesMap,
    SVG_UNIT_TYPE_USERSPACEONUSE
  }
};

nsSVGElement::StringInfo SVGFilterElement::sStringInfo[1] =
{
  { &nsGkAtoms::href, kNameSpaceID_XLink, true }
};

//----------------------------------------------------------------------
// Implementation

SVGFilterElement::SVGFilterElement(already_AddRefed<nsINodeInfo> aNodeInfo)
  : SVGFilterElementBase(aNodeInfo)
{
}

//----------------------------------------------------------------------
// nsIDOMNode methods


NS_IMPL_ELEMENT_CLONE_WITH_INIT(SVGFilterElement)


//----------------------------------------------------------------------

already_AddRefed<SVGAnimatedLength>
SVGFilterElement::X()
{
  return mLengthAttributes[ATTR_X].ToDOMAnimatedLength(this);
}

already_AddRefed<SVGAnimatedLength>
SVGFilterElement::Y()
{
   return mLengthAttributes[ATTR_Y].ToDOMAnimatedLength(this);
}

already_AddRefed<SVGAnimatedLength>
SVGFilterElement::Width()
{
  return mLengthAttributes[ATTR_WIDTH].ToDOMAnimatedLength(this);
}

already_AddRefed<SVGAnimatedLength>
SVGFilterElement::Height()
{
  return mLengthAttributes[ATTR_HEIGHT].ToDOMAnimatedLength(this);
}

already_AddRefed<nsIDOMSVGAnimatedEnumeration>
SVGFilterElement::FilterUnits()
{
  return mEnumAttributes[FILTERUNITS].ToDOMAnimatedEnum(this);
}

already_AddRefed<nsIDOMSVGAnimatedEnumeration>
SVGFilterElement::PrimitiveUnits()
{
  return mEnumAttributes[PRIMITIVEUNITS].ToDOMAnimatedEnum(this);
}

already_AddRefed<nsIDOMSVGAnimatedInteger>
SVGFilterElement::FilterResX()
{
  return mIntegerPairAttributes[FILTERRES].ToDOMAnimatedInteger(nsSVGIntegerPair::eFirst,
                                                                this);
}

already_AddRefed<nsIDOMSVGAnimatedInteger>
SVGFilterElement::FilterResY()
{
  return mIntegerPairAttributes[FILTERRES].ToDOMAnimatedInteger(nsSVGIntegerPair::eSecond,
                                                                this);
}

void
SVGFilterElement::SetFilterRes(uint32_t filterResX, uint32_t filterResY)
{
  mIntegerPairAttributes[FILTERRES].SetBaseValues(filterResX, filterResY, this);
}

already_AddRefed<SVGAnimatedString>
SVGFilterElement::Href()
{
  return mStringAttributes[HREF].ToDOMAnimatedString(this);
}

//----------------------------------------------------------------------
// nsIContent methods

NS_IMETHODIMP_(bool)
SVGFilterElement::IsAttributeMapped(const nsIAtom* name) const
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
    SVGFilterElementBase::IsAttributeMapped(name);
}

void
SVGFilterElement::Invalidate()
{
  nsTObserverArray<nsIMutationObserver*> *observers = GetMutationObservers();

  if (observers && !observers->IsEmpty()) {
    nsTObserverArray<nsIMutationObserver*>::ForwardIterator iter(*observers);
    while (iter.HasMore()) {
      nsCOMPtr<nsIMutationObserver> obs(iter.GetNext());
      nsCOMPtr<nsISVGFilterProperty> filter = do_QueryInterface(obs);
      if (filter)
        filter->Invalidate();
    }
  }
}

//----------------------------------------------------------------------
// nsSVGElement methods

/* virtual */ bool
SVGFilterElement::HasValidDimensions() const
{
  return (!mLengthAttributes[ATTR_WIDTH].IsExplicitlySet() ||
           mLengthAttributes[ATTR_WIDTH].GetAnimValInSpecifiedUnits() > 0) &&
         (!mLengthAttributes[ATTR_HEIGHT].IsExplicitlySet() ||
           mLengthAttributes[ATTR_HEIGHT].GetAnimValInSpecifiedUnits() > 0);
}

nsSVGElement::LengthAttributesInfo
SVGFilterElement::GetLengthInfo()
{
  return LengthAttributesInfo(mLengthAttributes, sLengthInfo,
                              ArrayLength(sLengthInfo));
}

nsSVGElement::IntegerPairAttributesInfo
SVGFilterElement::GetIntegerPairInfo()
{
  return IntegerPairAttributesInfo(mIntegerPairAttributes, sIntegerPairInfo,
                                   ArrayLength(sIntegerPairInfo));
}

nsSVGElement::EnumAttributesInfo
SVGFilterElement::GetEnumInfo()
{
  return EnumAttributesInfo(mEnumAttributes, sEnumInfo,
                            ArrayLength(sEnumInfo));
}

nsSVGElement::StringAttributesInfo
SVGFilterElement::GetStringInfo()
{
  return StringAttributesInfo(mStringAttributes, sStringInfo,
                              ArrayLength(sStringInfo));
}

} // namespace dom
} // namespace mozilla
