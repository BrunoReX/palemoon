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
 * The Original Code is the Mozilla SVG project.
 *
 * The Initial Developer of the Original Code is Robert Longson.
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#ifndef __NS_SVGINTEGERPAIR_H__
#define __NS_SVGINTEGERPAIR_H__

#include "nsAutoPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsError.h"
#include "nsIDOMSVGAnimatedInteger.h"
#include "nsISMILAttr.h"
#include "nsSVGElement.h"

class nsISMILAnimationElement;
class nsSMILValue;

class nsSVGIntegerPair
{

public:
  enum PairIndex {
    eFirst,
    eSecond
  };

  void Init(PRUint8 aAttrEnum = 0xff, PRInt32 aValue1 = 0, PRInt32 aValue2 = 0) {
    mAnimVal[0] = mBaseVal[0] = aValue1;
    mAnimVal[1] = mBaseVal[1] = aValue2;
    mAttrEnum = aAttrEnum;
    mIsAnimated = false;
    mIsBaseSet = false;
  }

  nsresult SetBaseValueString(const nsAString& aValue,
                              nsSVGElement *aSVGElement);
  void GetBaseValueString(nsAString& aValue);

  void SetBaseValue(PRInt32 aValue, PairIndex aIndex, nsSVGElement *aSVGElement);
  void SetBaseValues(PRInt32 aValue1, PRInt32 aValue2, nsSVGElement *aSVGElement);
  PRInt32 GetBaseValue(PairIndex aIndex) const
    { return mBaseVal[aIndex == eFirst ? 0 : 1]; }
  void SetAnimValue(const PRInt32 aValue[2], nsSVGElement *aSVGElement);
  PRInt32 GetAnimValue(PairIndex aIndex) const
    { return mAnimVal[aIndex == eFirst ? 0 : 1]; }

  // Returns true if the animated value of this integer has been explicitly
  // set (either by animation, or by taking on the base value which has been
  // explicitly set by markup or a DOM call), false otherwise.
  // If this returns false, the animated value is still valid, that is,
  // useable, and represents the default base value of the attribute.
  bool IsExplicitlySet() const
    { return mIsAnimated || mIsBaseSet; }

  nsresult ToDOMAnimatedInteger(nsIDOMSVGAnimatedInteger **aResult,
                                PairIndex aIndex,
                                nsSVGElement* aSVGElement);
  // Returns a new nsISMILAttr object that the caller must delete
  nsISMILAttr* ToSMILAttr(nsSVGElement* aSVGElement);

private:

  PRInt32 mAnimVal[2];
  PRInt32 mBaseVal[2];
  PRUint8 mAttrEnum; // element specified tracking for attribute
  bool mIsAnimated;
  bool mIsBaseSet;

public:
  struct DOMAnimatedInteger : public nsIDOMSVGAnimatedInteger
  {
    NS_DECL_CYCLE_COLLECTING_ISUPPORTS
    NS_DECL_CYCLE_COLLECTION_CLASS(DOMAnimatedInteger)

    DOMAnimatedInteger(nsSVGIntegerPair* aVal, PairIndex aIndex, nsSVGElement *aSVGElement)
      : mVal(aVal), mSVGElement(aSVGElement), mIndex(aIndex) {}

    nsSVGIntegerPair* mVal; // kept alive because it belongs to content
    nsRefPtr<nsSVGElement> mSVGElement;
    PairIndex mIndex; // are we the first or second integer

    NS_IMETHOD GetBaseVal(PRInt32* aResult)
      { *aResult = mVal->GetBaseValue(mIndex); return NS_OK; }
    NS_IMETHOD SetBaseVal(PRInt32 aValue)
      {
        mVal->SetBaseValue(aValue, mIndex, mSVGElement);
        return NS_OK;
      }

    // Script may have modified animation parameters or timeline -- DOM getters
    // need to flush any resample requests to reflect these modifications.
    NS_IMETHOD GetAnimVal(PRInt32* aResult)
    {
      mSVGElement->FlushAnimations();
      *aResult = mVal->GetAnimValue(mIndex);
      return NS_OK;
    }
  };

  struct SMILIntegerPair : public nsISMILAttr
  {
  public:
    SMILIntegerPair(nsSVGIntegerPair* aVal, nsSVGElement* aSVGElement)
      : mVal(aVal), mSVGElement(aSVGElement) {}

    // These will stay alive because a nsISMILAttr only lives as long
    // as the Compositing step, and DOM elements don't get a chance to
    // die during that.
    nsSVGIntegerPair* mVal;
    nsSVGElement* mSVGElement;

    // nsISMILAttr methods
    virtual nsresult ValueFromString(const nsAString& aStr,
                                     const nsISMILAnimationElement* aSrcElement,
                                     nsSMILValue& aValue,
                                     bool& aPreventCachingOfSandwich) const;
    virtual nsSMILValue GetBaseValue() const;
    virtual void ClearAnimValue();
    virtual nsresult SetAnimValue(const nsSMILValue& aValue);
  };
};

#endif //__NS_SVGINTEGERPAIR_H__
