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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
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

/* representation of simple property values within CSS declarations */

#ifndef nsCSSValue_h___
#define nsCSSValue_h___

#include "nsColor.h"
#include "nsString.h"
#include "nsCoord.h"
#include "nsCSSProperty.h"
#include "nsIURI.h"
#include "nsCOMPtr.h"
#include "nsAutoPtr.h"
#include "nsCRTGlue.h"
#include "nsStringBuffer.h"
#include "nsTArray.h"

class imgIRequest;
class nsIDocument;
class nsIPrincipal;

// Deletes a linked list iteratively to avoid blowing up the stack (bug 456196).
#define NS_CSS_DELETE_LIST_MEMBER(type_, ptr_, member_)                        \
  {                                                                            \
    type_ *cur = (ptr_)->member_;                                              \
    (ptr_)->member_ = nsnull;                                                  \
    while (cur) {                                                              \
      type_ *next = cur->member_;                                              \
      cur->member_ = nsnull;                                                   \
      delete cur;                                                              \
      cur = next;                                                              \
    }                                                                          \
  }

// Clones a linked list iteratively to avoid blowing up the stack.
// If it fails to clone the entire list then 'to_' is deleted and
// we return null.
#define NS_CSS_CLONE_LIST_MEMBER(type_, from_, member_, to_, args_)            \
  {                                                                            \
    type_ *dest = (to_);                                                       \
    (to_)->member_ = nsnull;                                                   \
    for (const type_ *src = (from_)->member_; src; src = src->member_) {       \
      type_ *clone = src->Clone args_;                                         \
      if (!clone) {                                                            \
        delete (to_);                                                          \
        return nsnull;                                                         \
      }                                                                        \
      dest->member_ = clone;                                                   \
      dest = clone;                                                            \
    }                                                                          \
  }

enum nsCSSUnit {
  eCSSUnit_Null         = 0,      // (n/a) null unit, value is not specified
  eCSSUnit_Auto         = 1,      // (n/a) value is algorithmic
  eCSSUnit_Inherit      = 2,      // (n/a) value is inherited
  eCSSUnit_Initial      = 3,      // (n/a) value is default UA value
  eCSSUnit_None         = 4,      // (n/a) value is none
  eCSSUnit_Normal       = 5,      // (n/a) value is normal (algorithmic, different than auto)
  eCSSUnit_System_Font  = 6,      // (n/a) value is -moz-use-system-font
  eCSSUnit_Dummy        = 7,      // (n/a) a fake but specified value, used
                                  //       only in temporary values
  eCSSUnit_DummyInherit = 8,      // (n/a) a fake but specified value, used
                                  //       only in temporary values
  eCSSUnit_RectIsAuto   = 9,      // (n/a) 'auto' for an entire rect()
  eCSSUnit_String       = 10,     // (PRUnichar*) a string value
  eCSSUnit_Ident        = 11,     // (PRUnichar*) a string value
  eCSSUnit_Families     = 12,     // (PRUnichar*) a string value
  eCSSUnit_Attr         = 13,     // (PRUnichar*) a attr(string) value
  eCSSUnit_Local_Font   = 14,     // (PRUnichar*) a local font name
  eCSSUnit_Font_Format  = 15,     // (PRUnichar*) a font format name
  eCSSUnit_Array        = 20,     // (nsCSSValue::Array*) a list of values
  eCSSUnit_Counter      = 21,     // (nsCSSValue::Array*) a counter(string,[string]) value
  eCSSUnit_Counters     = 22,     // (nsCSSValue::Array*) a counters(string,string[,string]) value
  eCSSUnit_Function     = 23,     // (nsCSSValue::Array*) a function with parameters.  First elem of array is name,
                                  //  the rest of the values are arguments.

  eCSSUnit_URL          = 30,     // (nsCSSValue::URL*) value
  eCSSUnit_Image        = 31,     // (nsCSSValue::Image*) value
  eCSSUnit_Gradient     = 32,     // (nsCSSValueGradient*) value
  eCSSUnit_Integer      = 50,     // (int) simple value
  eCSSUnit_Enumerated   = 51,     // (int) value has enumerated meaning
  eCSSUnit_EnumColor    = 80,     // (int) enumerated color (kColorKTable)
  eCSSUnit_Color        = 81,     // (nscolor) an RGBA value
  eCSSUnit_Percent      = 90,     // (float) 1.0 == 100%) value is percentage of something
  eCSSUnit_Number       = 91,     // (float) value is numeric (usually multiplier, different behavior that percent)

  // Length units - fixed
  // US English
  eCSSUnit_Inch         = 100,    // (float) 0.0254 meters

  // Metric
  eCSSUnit_Millimeter   = 207,    // (float) 1/1000 meter
  eCSSUnit_Centimeter   = 208,    // (float) 1/100 meter

  // US Typographic
  eCSSUnit_Point        = 300,    // (float) 1/72 inch
  eCSSUnit_Pica         = 301,    // (float) 12 points == 1/6 inch

  // Length units - relative
  // Font relative measure
  eCSSUnit_EM           = 800,    // (float) == current font size
  eCSSUnit_XHeight      = 801,    // (float) distance from top of lower case x to baseline
  eCSSUnit_Char         = 802,    // (float) number of characters, used for width with monospace font
  eCSSUnit_RootEM       = 803,    // (float) == root element font size

  // Screen relative measure
  eCSSUnit_Pixel        = 900,    // (float) CSS pixel unit

  // Angular units
  eCSSUnit_Degree       = 1000,    // (float) 360 per circle
  eCSSUnit_Grad         = 1001,    // (float) 400 per circle
  eCSSUnit_Radian       = 1002,    // (float) 2*pi per circle

  // Frequency units
  eCSSUnit_Hertz        = 2000,    // (float) 1/seconds
  eCSSUnit_Kilohertz    = 2001,    // (float) 1000 Hertz

  // Time units
  eCSSUnit_Seconds      = 3000,    // (float) Standard time
  eCSSUnit_Milliseconds = 3001     // (float) 1/1000 second
};

struct nsCSSValueGradient;

class nsCSSValue {
public:
  struct Array;
  friend struct Array;

  struct URL;
  friend struct URL;

  struct Image;
  friend struct Image;
  
  // for valueless units only (null, auto, inherit, none, normal)
  explicit nsCSSValue(nsCSSUnit aUnit = eCSSUnit_Null)
    : mUnit(aUnit)
  {
    NS_ASSERTION(aUnit <= eCSSUnit_RectIsAuto, "not a valueless unit");
  }

  nsCSSValue(PRInt32 aValue, nsCSSUnit aUnit) NS_HIDDEN;
  nsCSSValue(float aValue, nsCSSUnit aUnit) NS_HIDDEN;
  nsCSSValue(const nsString& aValue, nsCSSUnit aUnit) NS_HIDDEN;
  explicit nsCSSValue(nscolor aValue) NS_HIDDEN;
  nsCSSValue(Array* aArray, nsCSSUnit aUnit) NS_HIDDEN;
  explicit nsCSSValue(URL* aValue) NS_HIDDEN;
  explicit nsCSSValue(Image* aValue) NS_HIDDEN;
  explicit nsCSSValue(nsCSSValueGradient* aValue) NS_HIDDEN;
  nsCSSValue(const nsCSSValue& aCopy) NS_HIDDEN;
  ~nsCSSValue() { Reset(); }

  NS_HIDDEN_(nsCSSValue&)  operator=(const nsCSSValue& aCopy);
  NS_HIDDEN_(PRBool)      operator==(const nsCSSValue& aOther) const;

  PRBool operator!=(const nsCSSValue& aOther) const
  {
    return !(*this == aOther);
  }

  nsCSSUnit GetUnit() const { return mUnit; }
  PRBool    IsLengthUnit() const
    { return eCSSUnit_Inch <= mUnit && mUnit <= eCSSUnit_Pixel; }
  PRBool    IsFixedLengthUnit() const  
    { return eCSSUnit_Inch <= mUnit && mUnit <= eCSSUnit_Pica; }
  PRBool    IsRelativeLengthUnit() const  
    { return eCSSUnit_EM <= mUnit && mUnit <= eCSSUnit_Pixel; }
  PRBool    IsAngularUnit() const  
    { return eCSSUnit_Degree <= mUnit && mUnit <= eCSSUnit_Radian; }
  PRBool    IsFrequencyUnit() const  
    { return eCSSUnit_Hertz <= mUnit && mUnit <= eCSSUnit_Kilohertz; }
  PRBool    IsTimeUnit() const  
    { return eCSSUnit_Seconds <= mUnit && mUnit <= eCSSUnit_Milliseconds; }

  PRBool    UnitHasStringValue() const
    { return eCSSUnit_String <= mUnit && mUnit <= eCSSUnit_Font_Format; }

  PRInt32 GetIntValue() const
  {
    NS_ASSERTION(mUnit == eCSSUnit_Integer || mUnit == eCSSUnit_Enumerated ||
                 mUnit == eCSSUnit_EnumColor,
                 "not an int value");
    return mValue.mInt;
  }

  float GetPercentValue() const
  {
    NS_ASSERTION(mUnit == eCSSUnit_Percent, "not a percent value");
    return mValue.mFloat;
  }

  float GetFloatValue() const
  {
    NS_ASSERTION(eCSSUnit_Number <= mUnit, "not a float value");
    return mValue.mFloat;
  }

  float GetAngleValue() const
  {
    NS_ASSERTION(eCSSUnit_Degree <= mUnit &&
                 mUnit <= eCSSUnit_Radian, "not an angle value");
    return mValue.mFloat;
  }

  // Converts any angle to radians.
  double GetAngleValueInRadians() const;

  nsAString& GetStringValue(nsAString& aBuffer) const
  {
    NS_ASSERTION(UnitHasStringValue(), "not a string value");
    aBuffer.Truncate();
    PRUint32 len = NS_strlen(GetBufferValue(mValue.mString));
    mValue.mString->ToString(len, aBuffer);
    return aBuffer;
  }

  const PRUnichar* GetStringBufferValue() const
  {
    NS_ASSERTION(UnitHasStringValue(), "not a string value");
    return GetBufferValue(mValue.mString);
  }

  nscolor GetColorValue() const
  {
    NS_ASSERTION((mUnit == eCSSUnit_Color), "not a color value");
    return mValue.mColor;
  }

  PRBool IsNonTransparentColor() const;

  Array* GetArrayValue() const
  {
    NS_ASSERTION(eCSSUnit_Array <= mUnit && mUnit <= eCSSUnit_Function,
                 "not an array value");
    return mValue.mArray;
  }

  nsIURI* GetURLValue() const
  {
    NS_ASSERTION(mUnit == eCSSUnit_URL || mUnit == eCSSUnit_Image,
                 "not a URL value");
    return mUnit == eCSSUnit_URL ?
      mValue.mURL->mURI : mValue.mImage->mURI;
  }

  nsCSSValueGradient* GetGradientValue() const
  {
    NS_ASSERTION(mUnit == eCSSUnit_Gradient, "not a gradient value");
    return mValue.mGradient;
  }

  URL* GetURLStructValue() const
  {
    // Not allowing this for Image values, because if the caller takes
    // a ref to them they won't be able to delete them properly.
    NS_ASSERTION(mUnit == eCSSUnit_URL, "not a URL value");
    return mValue.mURL;
  }

  const PRUnichar* GetOriginalURLValue() const
  {
    NS_ASSERTION(mUnit == eCSSUnit_URL || mUnit == eCSSUnit_Image,
                 "not a URL value");
    return GetBufferValue(mUnit == eCSSUnit_URL ?
                            mValue.mURL->mString :
                            mValue.mImage->mString);
  }

  // Not making this inline because that would force us to include
  // imgIRequest.h, which leads to REQUIRES hell, since this header is included
  // all over.
  NS_HIDDEN_(imgIRequest*) GetImageValue() const;

  NS_HIDDEN_(nscoord)   GetLengthTwips() const;

  NS_HIDDEN_(void)  Reset()  // sets to null
  {
    if (mUnit != eCSSUnit_Null)
      DoReset();
  }
private:
  NS_HIDDEN_(void)  DoReset();

public:
  NS_HIDDEN_(void)  SetIntValue(PRInt32 aValue, nsCSSUnit aUnit);
  NS_HIDDEN_(void)  SetPercentValue(float aValue);
  NS_HIDDEN_(void)  SetFloatValue(float aValue, nsCSSUnit aUnit);
  NS_HIDDEN_(void)  SetStringValue(const nsString& aValue, nsCSSUnit aUnit);
  NS_HIDDEN_(void)  SetColorValue(nscolor aValue);
  NS_HIDDEN_(void)  SetArrayValue(nsCSSValue::Array* aArray, nsCSSUnit aUnit);
  NS_HIDDEN_(void)  SetURLValue(nsCSSValue::URL* aURI);
  NS_HIDDEN_(void)  SetImageValue(nsCSSValue::Image* aImage);
  NS_HIDDEN_(void)  SetGradientValue(nsCSSValueGradient* aGradient);
  NS_HIDDEN_(void)  SetAutoValue();
  NS_HIDDEN_(void)  SetInheritValue();
  NS_HIDDEN_(void)  SetInitialValue();
  NS_HIDDEN_(void)  SetNoneValue();
  NS_HIDDEN_(void)  SetNormalValue();
  NS_HIDDEN_(void)  SetSystemFontValue();
  NS_HIDDEN_(void)  SetDummyValue();
  NS_HIDDEN_(void)  SetDummyInheritValue();
  NS_HIDDEN_(void)  SetRectIsAutoValue();
  NS_HIDDEN_(void)  StartImageLoad(nsIDocument* aDocument)
                                   const;  // Not really const, but pretending

  // Returns an already addrefed buffer.  Can return null on allocation
  // failure.
  static nsStringBuffer* BufferFromString(const nsString& aValue);
  
  struct URL {
    // Methods are not inline because using an nsIPrincipal means requiring
    // caps, which leads to REQUIRES hell, since this header is included all
    // over.    

    // aString must not be null.
    // aOriginPrincipal must not be null.
    URL(nsIURI* aURI, nsStringBuffer* aString, nsIURI* aReferrer,
        nsIPrincipal* aOriginPrincipal) NS_HIDDEN;

    ~URL() NS_HIDDEN;

    NS_HIDDEN_(PRBool) operator==(const URL& aOther) const;

    // URIEquals only compares URIs and principals (unlike operator==, which
    // also compares the original strings).  URIEquals also assumes that the
    // mURI member of both URL objects is non-null.  Do NOT call this method
    // unless you're sure this is the case.
    NS_HIDDEN_(PRBool) URIEquals(const URL& aOther) const;

    nsCOMPtr<nsIURI> mURI; // null == invalid URL
    nsStringBuffer* mString; // Could use nsRefPtr, but it'd add useless
                             // null-checks; this is never null.
    nsCOMPtr<nsIURI> mReferrer;
    nsCOMPtr<nsIPrincipal> mOriginPrincipal;

    void AddRef() {
      if (mRefCnt == PR_UINT32_MAX) {
        NS_WARNING("refcount overflow, leaking nsCSSValue::URL");
        return;
      }
      ++mRefCnt;
      NS_LOG_ADDREF(this, mRefCnt, "nsCSSValue::URL", sizeof(*this));
    }
    void Release() {
      if (mRefCnt == PR_UINT32_MAX) {
        NS_WARNING("refcount overflow, leaking nsCSSValue::URL");
        return;
      }
      --mRefCnt;
      NS_LOG_RELEASE(this, mRefCnt, "nsCSSValue::URL");
      if (mRefCnt == 0)
        delete this;
    }
  protected:
    nsrefcnt mRefCnt;

    // not to be implemented
    URL(const URL& aOther);
    URL& operator=(const URL& aOther);
  };

  struct Image : public URL {
    // Not making the constructor and destructor inline because that would
    // force us to include imgIRequest.h, which leads to REQUIRES hell, since
    // this header is included all over.
    // aString must not be null.
    Image(nsIURI* aURI, nsStringBuffer* aString, nsIURI* aReferrer,
          nsIPrincipal* aOriginPrincipal, nsIDocument* aDocument) NS_HIDDEN;
    ~Image() NS_HIDDEN;

    // Inherit operator== from nsCSSValue::URL

    nsCOMPtr<imgIRequest> mRequest; // null == image load blocked or somehow failed

    // Override AddRef and Release to not only log ourselves correctly, but
    // also so that we delete correctly without a virtual destructor
    void AddRef() {
      if (mRefCnt == PR_UINT32_MAX) {
        NS_WARNING("refcount overflow, leaking nsCSSValue::Image");
        return;
      }
      ++mRefCnt;
      NS_LOG_ADDREF(this, mRefCnt, "nsCSSValue::Image", sizeof(*this));
    }

    void Release() {
      if (mRefCnt == PR_UINT32_MAX) {
        NS_WARNING("refcount overflow, leaking nsCSSValue::Image");
        return;
      }
      --mRefCnt;
      NS_LOG_RELEASE(this, mRefCnt, "nsCSSValue::Image");
      if (mRefCnt == 0)
        delete this;
    }
  };

private:
  static const PRUnichar* GetBufferValue(nsStringBuffer* aBuffer) {
    return static_cast<PRUnichar*>(aBuffer->Data());
  }

protected:
  nsCSSUnit mUnit;
  union {
    PRInt32    mInt;
    float      mFloat;
    // Note: the capacity of the buffer may exceed the length of the string.
    // If we're of a string type, mString is not null.
    nsStringBuffer* mString;
    nscolor    mColor;
    Array*     mArray;
    URL*       mURL;
    Image*     mImage;
    nsCSSValueGradient* mGradient;
  }         mValue;
};

struct nsCSSValueGradientStop {
public:
  nsCSSValueGradientStop() NS_HIDDEN;
  // needed to keep bloat logs happy when we use the nsTArray in nsCSSValueGradient
  nsCSSValueGradientStop(const nsCSSValueGradientStop& aOther) NS_HIDDEN;
  ~nsCSSValueGradientStop() NS_HIDDEN;

  nsCSSValue mLocation;
  nsCSSValue mColor;

  PRBool operator==(const nsCSSValueGradientStop& aOther) const
  {
    return (mLocation == aOther.mLocation &&
            mColor == aOther.mColor);
  }

  PRBool operator!=(const nsCSSValueGradientStop& aOther) const
  {
    return !(*this == aOther);
  }
};

struct nsCSSValueGradient {
  nsCSSValueGradient(PRBool aIsRadial,
                     PRBool aIsRepeating) NS_HIDDEN;

  // true if gradient is radial, false if it is linear
  PRPackedBool mIsRadial;
  PRPackedBool mIsRepeating;
  // line position and angle
  nsCSSValue mBgPosX;
  nsCSSValue mBgPosY;
  nsCSSValue mAngle;

  // Only meaningful if mIsRadial is true
  nsCSSValue mRadialShape;
  nsCSSValue mRadialSize;

  nsTArray<nsCSSValueGradientStop> mStops;

  PRBool operator==(const nsCSSValueGradient& aOther) const
  {
    if (mIsRadial != aOther.mIsRadial ||
        mIsRepeating != aOther.mIsRepeating ||
        mBgPosX != aOther.mBgPosX ||
        mBgPosY != aOther.mBgPosY ||
        mAngle != aOther.mAngle ||
        mRadialShape != aOther.mRadialShape ||
        mRadialSize != aOther.mRadialSize)
      return PR_FALSE;

    if (mStops.Length() != aOther.mStops.Length())
      return PR_FALSE;

    for (PRUint32 i = 0; i < mStops.Length(); i++) {
      if (mStops[i] != aOther.mStops[i])
        return PR_FALSE;
    }

    return PR_TRUE;
  }

  PRBool operator!=(const nsCSSValueGradient& aOther) const
  {
    return !(*this == aOther);
  }

  void AddRef() {
    if (mRefCnt == PR_UINT32_MAX) {
      NS_WARNING("refcount overflow, leaking nsCSSValue::Gradient");
      return;
    }
    ++mRefCnt;
    NS_LOG_ADDREF(this, mRefCnt, "nsCSSValue::Gradient", sizeof(*this));
  }
  void Release() {
    if (mRefCnt == PR_UINT32_MAX) {
      NS_WARNING("refcount overflow, leaking nsCSSValue::Gradient");
      return;
    }
    --mRefCnt;
    NS_LOG_RELEASE(this, mRefCnt, "nsCSSValue::Gradient");
    if (mRefCnt == 0)
      delete this;
  }

private:
  nsrefcnt mRefCnt;

  // not to be implemented
  nsCSSValueGradient(const nsCSSValueGradient& aOther);
  nsCSSValueGradient& operator=(const nsCSSValueGradient& aOther);
};

struct nsCSSValue::Array {

  // return |Array| with reference count of zero
  static Array* Create(size_t aItemCount) {
    return new (aItemCount) Array(aItemCount);
  }

  nsCSSValue& operator[](size_t aIndex) {
    NS_ASSERTION(aIndex < mCount, "out of range");
    return mArray[aIndex];
  }

  const nsCSSValue& operator[](size_t aIndex) const {
    NS_ASSERTION(aIndex < mCount, "out of range");
    return mArray[aIndex];
  }

  nsCSSValue& Item(size_t aIndex) { return (*this)[aIndex]; }
  const nsCSSValue& Item(size_t aIndex) const { return (*this)[aIndex]; }

  size_t Count() const { return mCount; }

  PRBool operator==(const Array& aOther) const
  {
    if (mCount != aOther.mCount)
      return PR_FALSE;
    for (size_t i = 0; i < mCount; ++i)
      if ((*this)[i] != aOther[i])
        return PR_FALSE;
    return PR_TRUE;
  }

  void AddRef() {
    if (mRefCnt == size_t(-1)) { // really want SIZE_MAX
      NS_WARNING("refcount overflow, leaking nsCSSValue::Array");
      return;
    }
    ++mRefCnt;
    NS_LOG_ADDREF(this, mRefCnt, "nsCSSValue::Array", sizeof(*this));
  }
  void Release() {
    if (mRefCnt == size_t(-1)) { // really want SIZE_MAX
      NS_WARNING("refcount overflow, leaking nsCSSValue::Array");
      return;
    }
    --mRefCnt;
    NS_LOG_RELEASE(this, mRefCnt, "nsCSSValue::Array");
    if (mRefCnt == 0)
      delete this;
  }

private:

  size_t mRefCnt;
  const size_t mCount;
  // This must be the last sub-object, since we extend this array to
  // be of size mCount; it needs to be a sub-object so it gets proper
  // alignment.
  nsCSSValue mArray[1];

  void* operator new(size_t aSelfSize, size_t aItemCount) CPP_THROW_NEW {
    NS_ABORT_IF_FALSE(aItemCount > 0, "cannot have a 0 item count");
    return ::operator new(aSelfSize + sizeof(nsCSSValue) * (aItemCount - 1));
  }

  void operator delete(void* aPtr) { ::operator delete(aPtr); }

  nsCSSValue* First() { return mArray; }

  const nsCSSValue* First() const { return mArray; }

#define CSSVALUE_LIST_FOR_EXTRA_VALUES(var)                                   \
  for (nsCSSValue *var = First() + 1, *var##_end = First() + mCount;          \
       var != var##_end; ++var)

  Array(size_t aItemCount)
    : mRefCnt(0)
    , mCount(aItemCount)
  {
    MOZ_COUNT_CTOR(nsCSSValue::Array);
    CSSVALUE_LIST_FOR_EXTRA_VALUES(val) {
      new (val) nsCSSValue();
    }
  }

  ~Array()
  {
    MOZ_COUNT_DTOR(nsCSSValue::Array);
    CSSVALUE_LIST_FOR_EXTRA_VALUES(val) {
      val->~nsCSSValue();
    }
  }

#undef CSSVALUE_LIST_FOR_EXTRA_VALUES

private:
  // not to be implemented
  Array(const Array& aOther);
  Array& operator=(const Array& aOther);
};

#endif /* nsCSSValue_h___ */

