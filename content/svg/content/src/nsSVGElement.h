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
 * The Initial Developer of the Original Code is
 * Crocodile Clips Ltd..
 * Portions created by the Initial Developer are Copyright (C) 2001
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Alex Fritze <alex.fritze@crocodile-clips.com> (original author)
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

#ifndef __NS_SVGELEMENT_H__
#define __NS_SVGELEMENT_H__

/*
  nsSVGElement is the base class for all SVG content elements.
  It implements all the common DOM interfaces and handles attributes.
*/

#include "mozilla/css/StyleRule.h"
#include "nsAutoPtr.h"
#include "nsChangeHint.h"
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsDOMMemoryReporter.h"
#include "nsError.h"
#include "nsGenericElement.h"
#include "nsISupportsImpl.h"
#include "nsStyledElement.h"

class nsIDOMSVGElement;
class nsIDOMSVGSVGElement;
class nsSVGAngle;
class nsSVGBoolean;
class nsSVGEnum;
class nsSVGInteger;
class nsSVGIntegerPair;
class nsSVGLength2;
class nsSVGNumber2;
class nsSVGNumberPair;
class nsSVGString;
class nsSVGSVGElement;
class nsSVGViewBox;

namespace mozilla {
class SVGAnimatedNumberList;
class SVGNumberList;
class SVGAnimatedLengthList;
class SVGUserUnitList;
class SVGAnimatedPointList;
class SVGAnimatedPathSegList;
class SVGAnimatedPreserveAspectRatio;
class SVGAnimatedTransformList;
class SVGStringList;
class DOMSVGStringList;
}

struct gfxMatrix;
struct nsSVGEnumMapping;

typedef nsStyledElementNotElementCSSInlineStyle nsSVGElementBase;

class nsSVGElement : public nsSVGElementBase    // nsIContent
{
protected:
  nsSVGElement(already_AddRefed<nsINodeInfo> aNodeInfo);
  nsresult Init();
  virtual ~nsSVGElement(){}

public:
  typedef mozilla::SVGNumberList SVGNumberList;
  typedef mozilla::SVGAnimatedNumberList SVGAnimatedNumberList;
  typedef mozilla::SVGUserUnitList SVGUserUnitList;
  typedef mozilla::SVGAnimatedLengthList SVGAnimatedLengthList;
  typedef mozilla::SVGAnimatedPointList SVGAnimatedPointList;
  typedef mozilla::SVGAnimatedPathSegList SVGAnimatedPathSegList;
  typedef mozilla::SVGAnimatedPreserveAspectRatio SVGAnimatedPreserveAspectRatio;
  typedef mozilla::SVGAnimatedTransformList SVGAnimatedTransformList;
  typedef mozilla::SVGStringList SVGStringList;

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  // nsIContent interface methods

  virtual nsresult BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                              nsIContent* aBindingParent,
                              bool aCompileEventHandlers);

  virtual nsresult UnsetAttr(PRInt32 aNameSpaceID, nsIAtom* aAttribute,
                             bool aNotify);

  virtual nsChangeHint GetAttributeChangeHint(const nsIAtom* aAttribute,
                                              PRInt32 aModType) const;

  virtual bool IsNodeOfType(PRUint32 aFlags) const;

  NS_IMETHOD WalkContentStyleRules(nsRuleWalker* aRuleWalker);

  static const MappedAttributeEntry sFillStrokeMap[];
  static const MappedAttributeEntry sGraphicsMap[];
  static const MappedAttributeEntry sTextContentElementsMap[];
  static const MappedAttributeEntry sFontSpecificationMap[];
  static const MappedAttributeEntry sGradientStopMap[];
  static const MappedAttributeEntry sViewportsMap[];
  static const MappedAttributeEntry sMarkersMap[];
  static const MappedAttributeEntry sColorMap[];
  static const MappedAttributeEntry sFiltersMap[];
  static const MappedAttributeEntry sFEFloodMap[];
  static const MappedAttributeEntry sLightingEffectsMap[];

  // nsIDOMNode
  NS_IMETHOD IsSupported(const nsAString& aFeature, const nsAString& aVersion,
                         bool* aReturn);
  
  // nsIDOMSVGElement
  NS_IMETHOD GetId(nsAString & aId);
  NS_IMETHOD SetId(const nsAString & aId);
  NS_IMETHOD GetOwnerSVGElement(nsIDOMSVGSVGElement** aOwnerSVGElement);
  NS_IMETHOD GetViewportElement(nsIDOMSVGElement** aViewportElement);

  // Gets the element that establishes the rectangular viewport against which
  // we should resolve percentage lengths (our "coordinate context"). Returns
  // nsnull for outer <svg> or SVG without an <svg> parent (invalid SVG).
  nsSVGSVGElement* GetCtx() const;

  /**
   * Returns aMatrix post-multiplied by the transform from the userspace
   * established by this element to the userspace established by its parent.
   */
  virtual gfxMatrix PrependLocalTransformTo(const gfxMatrix &aMatrix) const;

  // Setter for to set the current <animateMotion> transformation
  // Only visible for nsSVGGraphicElement, so it's a no-op here, and that
  // subclass has the useful implementation.
  virtual void SetAnimateMotionTransform(const gfxMatrix* aMatrix) {/*no-op*/}

  bool IsStringAnimatable(PRUint8 aAttrEnum) {
    return GetStringInfo().mStringInfo[aAttrEnum].mIsAnimatable;
  }
  bool NumberAttrAllowsPercentage(PRUint8 aAttrEnum) {
    return GetNumberInfo().mNumberInfo[aAttrEnum].mPercentagesAllowed;
  }
  void SetLength(nsIAtom* aName, const nsSVGLength2 &aLength);
  virtual void DidChangeLength(PRUint8 aAttrEnum, bool aDoSetAttr);
  virtual void DidChangeNumber(PRUint8 aAttrEnum, bool aDoSetAttr);
  virtual void DidChangeNumberPair(PRUint8 aAttrEnum, bool aDoSetAttr);
  virtual void DidChangeInteger(PRUint8 aAttrEnum, bool aDoSetAttr);
  virtual void DidChangeIntegerPair(PRUint8 aAttrEnum, bool aDoSetAttr);
  virtual void DidChangeAngle(PRUint8 aAttrEnum, bool aDoSetAttr);
  virtual void DidChangeBoolean(PRUint8 aAttrEnum, bool aDoSetAttr);
  virtual void DidChangeEnum(PRUint8 aAttrEnum, bool aDoSetAttr);
  virtual void DidChangeViewBox(bool aDoSetAttr);
  virtual void DidChangePreserveAspectRatio(bool aDoSetAttr);
  virtual void DidChangeNumberList(PRUint8 aAttrEnum, bool aDoSetAttr);
  virtual void DidChangeLengthList(PRUint8 aAttrEnum, bool aDoSetAttr);
  virtual void DidChangePointList(bool aDoSetAttr);
  virtual void DidChangePathSegList(bool aDoSetAttr);
  virtual void DidChangeTransformList(bool aDoSetAttr);
  virtual void DidChangeString(PRUint8 aAttrEnum) {}
  void DidChangeStringList(bool aIsConditionalProcessingAttribute,
                           PRUint8 aAttrEnum);

  virtual void DidAnimateLength(PRUint8 aAttrEnum);
  virtual void DidAnimateNumber(PRUint8 aAttrEnum);
  virtual void DidAnimateNumberPair(PRUint8 aAttrEnum);
  virtual void DidAnimateInteger(PRUint8 aAttrEnum);
  virtual void DidAnimateIntegerPair(PRUint8 aAttrEnum);
  virtual void DidAnimateAngle(PRUint8 aAttrEnum);
  virtual void DidAnimateBoolean(PRUint8 aAttrEnum);
  virtual void DidAnimateEnum(PRUint8 aAttrEnum);
  virtual void DidAnimateViewBox();
  virtual void DidAnimatePreserveAspectRatio();
  virtual void DidAnimateNumberList(PRUint8 aAttrEnum);
  virtual void DidAnimateLengthList(PRUint8 aAttrEnum);
  virtual void DidAnimatePointList();
  virtual void DidAnimatePathSegList();
  virtual void DidAnimateTransformList();
  virtual void DidAnimateString(PRUint8 aAttrEnum);

  nsSVGLength2* GetAnimatedLength(const nsIAtom *aAttrName);
  void GetAnimatedLengthValues(float *aFirst, ...);
  void GetAnimatedNumberValues(float *aFirst, ...);
  void GetAnimatedIntegerValues(PRInt32 *aFirst, ...);
  SVGAnimatedNumberList* GetAnimatedNumberList(PRUint8 aAttrEnum);
  SVGAnimatedNumberList* GetAnimatedNumberList(nsIAtom *aAttrName);
  void GetAnimatedLengthListValues(SVGUserUnitList *aFirst, ...);
  SVGAnimatedLengthList* GetAnimatedLengthList(PRUint8 aAttrEnum);
  virtual SVGAnimatedPointList* GetAnimatedPointList() {
    return nsnull;
  }
  virtual SVGAnimatedPathSegList* GetAnimPathSegList() {
    // DOM interface 'SVGAnimatedPathData' (*inherited* by nsSVGPathElement)
    // has a member called 'animatedPathSegList' member, so we have a shorter
    // name so we don't get hidden by the GetAnimatedPathSegList declared by
    // NS_DECL_NSIDOMSVGANIMATEDPATHDATA.
    return nsnull;
  }
  // Despite the fact that animated transform lists are used for a variety of
  // attributes, no SVG element uses more than one.
  virtual SVGAnimatedTransformList* GetAnimatedTransformList() {
    return nsnull;
  }

  virtual nsISMILAttr* GetAnimatedAttr(PRInt32 aNamespaceID, nsIAtom* aName);
  void AnimationNeedsResample();
  void FlushAnimations();

  virtual void RecompileScriptEventListeners();

  void GetStringBaseValue(PRUint8 aAttrEnum, nsAString& aResult) const;
  void SetStringBaseValue(PRUint8 aAttrEnum, const nsAString& aValue);

  virtual nsIAtom* GetPointListAttrName() const {
    return nsnull;
  }
  virtual nsIAtom* GetPathDataAttrName() const {
    return nsnull;
  }
  virtual nsIAtom* GetTransformListAttrName() const {
    return nsnull;
  }

protected:
  virtual nsresult AfterSetAttr(PRInt32 aNamespaceID, nsIAtom* aName,
                                const nsAString* aValue, bool aNotify);
  virtual bool ParseAttribute(PRInt32 aNamespaceID, nsIAtom* aAttribute,
                                const nsAString& aValue, nsAttrValue& aResult);
  static nsresult ReportAttributeParseFailure(nsIDocument* aDocument,
                                              nsIAtom* aAttribute,
                                              const nsAString& aValue);

  // Hooks for subclasses
  virtual bool IsEventName(nsIAtom* aName);

  void UpdateContentStyleRule();
  void UpdateAnimatedContentStyleRule();
  mozilla::css::StyleRule* GetAnimatedContentStyleRule();

  static nsIAtom* GetEventNameForAttr(nsIAtom* aAttr);

  struct LengthInfo {
    nsIAtom** mName;
    float     mDefaultValue;
    PRUint8   mDefaultUnitType;
    PRUint8   mCtxType;
  };

  struct LengthAttributesInfo {
    nsSVGLength2* mLengths;
    LengthInfo*   mLengthInfo;
    PRUint32      mLengthCount;

    LengthAttributesInfo(nsSVGLength2 *aLengths,
                         LengthInfo *aLengthInfo,
                         PRUint32 aLengthCount) :
      mLengths(aLengths), mLengthInfo(aLengthInfo), mLengthCount(aLengthCount)
      {}

    void Reset(PRUint8 aAttrEnum);
  };

  struct NumberInfo {
    nsIAtom** mName;
    float     mDefaultValue;
    bool mPercentagesAllowed;
  };

  struct NumberAttributesInfo {
    nsSVGNumber2* mNumbers;
    NumberInfo*   mNumberInfo;
    PRUint32      mNumberCount;

    NumberAttributesInfo(nsSVGNumber2 *aNumbers,
                         NumberInfo *aNumberInfo,
                         PRUint32 aNumberCount) :
      mNumbers(aNumbers), mNumberInfo(aNumberInfo), mNumberCount(aNumberCount)
      {}

    void Reset(PRUint8 aAttrEnum);
  };

  struct NumberPairInfo {
    nsIAtom** mName;
    float     mDefaultValue1;
    float     mDefaultValue2;
  };

  struct NumberPairAttributesInfo {
    nsSVGNumberPair* mNumberPairs;
    NumberPairInfo*  mNumberPairInfo;
    PRUint32         mNumberPairCount;

    NumberPairAttributesInfo(nsSVGNumberPair *aNumberPairs,
                             NumberPairInfo *aNumberPairInfo,
                             PRUint32 aNumberPairCount) :
      mNumberPairs(aNumberPairs), mNumberPairInfo(aNumberPairInfo),
      mNumberPairCount(aNumberPairCount)
      {}

    void Reset(PRUint8 aAttrEnum);
  };

  struct IntegerInfo {
    nsIAtom** mName;
    PRInt32   mDefaultValue;
  };

  struct IntegerAttributesInfo {
    nsSVGInteger* mIntegers;
    IntegerInfo*  mIntegerInfo;
    PRUint32      mIntegerCount;

    IntegerAttributesInfo(nsSVGInteger *aIntegers,
                          IntegerInfo *aIntegerInfo,
                          PRUint32 aIntegerCount) :
      mIntegers(aIntegers), mIntegerInfo(aIntegerInfo), mIntegerCount(aIntegerCount)
      {}

    void Reset(PRUint8 aAttrEnum);
  };

  struct IntegerPairInfo {
    nsIAtom** mName;
    PRInt32   mDefaultValue1;
    PRInt32   mDefaultValue2;
  };

  struct IntegerPairAttributesInfo {
    nsSVGIntegerPair* mIntegerPairs;
    IntegerPairInfo*  mIntegerPairInfo;
    PRUint32          mIntegerPairCount;

    IntegerPairAttributesInfo(nsSVGIntegerPair *aIntegerPairs,
                              IntegerPairInfo *aIntegerPairInfo,
                              PRUint32 aIntegerPairCount) :
      mIntegerPairs(aIntegerPairs), mIntegerPairInfo(aIntegerPairInfo),
      mIntegerPairCount(aIntegerPairCount)
      {}

    void Reset(PRUint8 aAttrEnum);
  };

  struct AngleInfo {
    nsIAtom** mName;
    float     mDefaultValue;
    PRUint8   mDefaultUnitType;
  };

  struct AngleAttributesInfo {
    nsSVGAngle* mAngles;
    AngleInfo*  mAngleInfo;
    PRUint32    mAngleCount;

    AngleAttributesInfo(nsSVGAngle *aAngles,
                        AngleInfo *aAngleInfo,
                        PRUint32 aAngleCount) :
      mAngles(aAngles), mAngleInfo(aAngleInfo), mAngleCount(aAngleCount)
      {}

    void Reset(PRUint8 aAttrEnum);
  };

  struct BooleanInfo {
    nsIAtom**    mName;
    bool mDefaultValue;
  };

  struct BooleanAttributesInfo {
    nsSVGBoolean* mBooleans;
    BooleanInfo*  mBooleanInfo;
    PRUint32      mBooleanCount;

    BooleanAttributesInfo(nsSVGBoolean *aBooleans,
                          BooleanInfo *aBooleanInfo,
                          PRUint32 aBooleanCount) :
      mBooleans(aBooleans), mBooleanInfo(aBooleanInfo), mBooleanCount(aBooleanCount)
      {}

    void Reset(PRUint8 aAttrEnum);
  };

  friend class nsSVGEnum;

  struct EnumInfo {
    nsIAtom**         mName;
    nsSVGEnumMapping* mMapping;
    PRUint16          mDefaultValue;
  };

  struct EnumAttributesInfo {
    nsSVGEnum* mEnums;
    EnumInfo*  mEnumInfo;
    PRUint32   mEnumCount;

    EnumAttributesInfo(nsSVGEnum *aEnums,
                       EnumInfo *aEnumInfo,
                       PRUint32 aEnumCount) :
      mEnums(aEnums), mEnumInfo(aEnumInfo), mEnumCount(aEnumCount)
      {}

    void Reset(PRUint8 aAttrEnum);
  };

  struct NumberListInfo {
    nsIAtom** mName;
  };

  struct NumberListAttributesInfo {
    SVGAnimatedNumberList* mNumberLists;
    NumberListInfo*        mNumberListInfo;
    PRUint32               mNumberListCount;

    NumberListAttributesInfo(SVGAnimatedNumberList *aNumberLists,
                             NumberListInfo *aNumberListInfo,
                             PRUint32 aNumberListCount)
      : mNumberLists(aNumberLists)
      , mNumberListInfo(aNumberListInfo)
      , mNumberListCount(aNumberListCount)
    {}

    void Reset(PRUint8 aAttrEnum);
  };

  struct LengthListInfo {
    nsIAtom** mName;
    PRUint8   mAxis;
    /**
     * Flag to indicate whether appending zeros to the end of the list would
     * change the rendering of the SVG for the attribute in question. For x and
     * y on the <text> element this is true, but for dx and dy on <text> this
     * is false. This flag is fed down to SVGLengthListSMILType so it can
     * determine if it can sensibly animate from-to lists of different lengths,
     * which is desirable in the case of dx and dy.
     */
    bool mCouldZeroPadList;
  };

  struct LengthListAttributesInfo {
    SVGAnimatedLengthList* mLengthLists;
    LengthListInfo*        mLengthListInfo;
    PRUint32               mLengthListCount;

    LengthListAttributesInfo(SVGAnimatedLengthList *aLengthLists,
                             LengthListInfo *aLengthListInfo,
                             PRUint32 aLengthListCount)
      : mLengthLists(aLengthLists)
      , mLengthListInfo(aLengthListInfo)
      , mLengthListCount(aLengthListCount)
    {}

    void Reset(PRUint8 aAttrEnum);
  };

  struct StringInfo {
    nsIAtom**    mName;
    PRInt32      mNamespaceID;
    bool mIsAnimatable;
  };

  struct StringAttributesInfo {
    nsSVGString*  mStrings;
    StringInfo*   mStringInfo;
    PRUint32      mStringCount;

    StringAttributesInfo(nsSVGString *aStrings,
                         StringInfo *aStringInfo,
                         PRUint32 aStringCount) :
      mStrings(aStrings), mStringInfo(aStringInfo), mStringCount(aStringCount)
      {}

    void Reset(PRUint8 aAttrEnum);
  };

  friend class mozilla::DOMSVGStringList;

  struct StringListInfo {
    nsIAtom**    mName;
  };

  struct StringListAttributesInfo {
    SVGStringList*    mStringLists;
    StringListInfo*   mStringListInfo;
    PRUint32          mStringListCount;

    StringListAttributesInfo(SVGStringList  *aStringLists,
                             StringListInfo *aStringListInfo,
                             PRUint32 aStringListCount) :
      mStringLists(aStringLists), mStringListInfo(aStringListInfo),
      mStringListCount(aStringListCount)
      {}

    void Reset(PRUint8 aAttrEnum);
  };

  virtual LengthAttributesInfo GetLengthInfo();
  virtual NumberAttributesInfo GetNumberInfo();
  virtual NumberPairAttributesInfo GetNumberPairInfo();
  virtual IntegerAttributesInfo GetIntegerInfo();
  virtual IntegerPairAttributesInfo GetIntegerPairInfo();
  virtual AngleAttributesInfo GetAngleInfo();
  virtual BooleanAttributesInfo GetBooleanInfo();
  virtual EnumAttributesInfo GetEnumInfo();
  // We assume all viewboxes and preserveAspectRatios are alike
  // so we don't need to wrap the class
  virtual nsSVGViewBox *GetViewBox();
  virtual SVGAnimatedPreserveAspectRatio *GetPreserveAspectRatio();
  virtual NumberListAttributesInfo GetNumberListInfo();
  virtual LengthListAttributesInfo GetLengthListInfo();
  virtual StringAttributesInfo GetStringInfo();
  virtual StringListAttributesInfo GetStringListInfo();

  static nsSVGEnumMapping sSVGUnitTypesMap[];

private:
  void UnsetAttrInternal(PRInt32 aNameSpaceID, nsIAtom* aAttribute,
                         bool aNotify);

  nsRefPtr<mozilla::css::StyleRule> mContentStyleRule;
};

/**
 * A macro to implement the NS_NewSVGXXXElement() functions.
 */
#define NS_IMPL_NS_NEW_SVG_ELEMENT(_elementName)                             \
nsresult                                                                     \
NS_NewSVG##_elementName##Element(nsIContent **aResult,                       \
                                 already_AddRefed<nsINodeInfo> aNodeInfo)    \
{                                                                            \
  nsRefPtr<nsSVG##_elementName##Element> it =                                \
    new nsSVG##_elementName##Element(aNodeInfo);                             \
  if (!it)                                                                   \
    return NS_ERROR_OUT_OF_MEMORY;                                           \
                                                                             \
  nsresult rv = it->Init();                                                  \
                                                                             \
  if (NS_FAILED(rv)) {                                                       \
    return rv;                                                               \
  }                                                                          \
                                                                             \
  *aResult = it.forget().get();                                              \
                                                                             \
  return rv;                                                                 \
}

#define NS_IMPL_NS_NEW_SVG_ELEMENT_CHECK_PARSER(_elementName)                \
nsresult                                                                     \
NS_NewSVG##_elementName##Element(nsIContent **aResult,                       \
                                 already_AddRefed<nsINodeInfo> aNodeInfo,    \
                                 FromParser aFromParser)                     \
{                                                                            \
  nsRefPtr<nsSVG##_elementName##Element> it =                                \
    new nsSVG##_elementName##Element(aNodeInfo, aFromParser);                \
  if (!it)                                                                   \
    return NS_ERROR_OUT_OF_MEMORY;                                           \
                                                                             \
  nsresult rv = it->Init();                                                  \
                                                                             \
  if (NS_FAILED(rv)) {                                                       \
    return rv;                                                               \
  }                                                                          \
                                                                             \
  *aResult = it.forget().get();                                              \
                                                                             \
  return rv;                                                                 \
}

 // No unlinking, we'd need to null out the value pointer (the object it
// points to is held by the element) and null-check it everywhere.
#define NS_SVG_VAL_IMPL_CYCLE_COLLECTION(_val, _element)                     \
NS_IMPL_CYCLE_COLLECTION_CLASS(_val)                                         \
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(_val)                                \
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR_AMBIGUOUS(_element, nsIContent) \
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END                                        \
NS_IMPL_CYCLE_COLLECTION_UNLINK_0(_val)


#endif // __NS_SVGELEMENT_H__
