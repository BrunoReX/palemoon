/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsFont.h"
#include "nsString.h"
#include "nsUnicharUtils.h"
#include "nsCRT.h"
#include "gfxFont.h"

nsFont::nsFont(const char* aName, uint8_t aStyle, uint8_t aVariant,
               uint16_t aWeight, int16_t aStretch, uint8_t aDecoration,
               nscoord aSize)
{
  NS_ASSERTION(aName && IsASCII(nsDependentCString(aName)),
               "Must only pass ASCII names here");
  name.AssignASCII(aName);
  style = aStyle;
  systemFont = false;
  variant = aVariant;
  weight = aWeight;
  stretch = aStretch;
  decorations = aDecoration;
  size = aSize;
  sizeAdjust = 0.0;
  kerning = NS_FONT_KERNING_AUTO;
  synthesis = NS_FONT_SYNTHESIS_WEIGHT | NS_FONT_SYNTHESIS_STYLE;

  variantAlternates = 0;
  variantCaps = NS_FONT_VARIANT_CAPS_NORMAL;
  variantEastAsian = 0;
  variantLigatures = 0;
  variantNumeric = 0;
  variantPosition = NS_FONT_VARIANT_POSITION_NORMAL;
}

nsFont::nsFont(const nsSubstring& aName, uint8_t aStyle, uint8_t aVariant,
               uint16_t aWeight, int16_t aStretch, uint8_t aDecoration,
               nscoord aSize)
  : name(aName)
{
  style = aStyle;
  systemFont = false;
  variant = aVariant;
  weight = aWeight;
  stretch = aStretch;
  decorations = aDecoration;
  size = aSize;
  sizeAdjust = 0.0;
  kerning = NS_FONT_KERNING_AUTO;
  synthesis = NS_FONT_SYNTHESIS_WEIGHT | NS_FONT_SYNTHESIS_STYLE;

  variantAlternates = 0;
  variantCaps = NS_FONT_VARIANT_CAPS_NORMAL;
  variantEastAsian = 0;
  variantLigatures = 0;
  variantNumeric = 0;
  variantPosition = NS_FONT_VARIANT_POSITION_NORMAL;
}

nsFont::nsFont(const nsFont& aOther)
  : name(aOther.name)
{
  style = aOther.style;
  systemFont = aOther.systemFont;
  variant = aOther.variant;
  weight = aOther.weight;
  stretch = aOther.stretch;
  decorations = aOther.decorations;
  size = aOther.size;
  sizeAdjust = aOther.sizeAdjust;
  kerning = aOther.kerning;
  synthesis = aOther.synthesis;
  fontFeatureSettings = aOther.fontFeatureSettings;
  languageOverride = aOther.languageOverride;
  variantAlternates = aOther.variantAlternates;
  variantCaps = aOther.variantCaps;
  variantEastAsian = aOther.variantEastAsian;
  variantLigatures = aOther.variantLigatures;
  variantNumeric = aOther.variantNumeric;
  variantPosition = aOther.variantPosition;
  alternateValues = aOther.alternateValues;
  featureValueLookup = aOther.featureValueLookup;
}

nsFont::nsFont()
{
}

nsFont::~nsFont()
{
}

bool nsFont::BaseEquals(const nsFont& aOther) const
{
  if ((style == aOther.style) &&
      (systemFont == aOther.systemFont) &&
      (weight == aOther.weight) &&
      (stretch == aOther.stretch) &&
      (size == aOther.size) &&
      (sizeAdjust == aOther.sizeAdjust) &&
      name.Equals(aOther.name, nsCaseInsensitiveStringComparator()) &&
      (kerning == aOther.kerning) &&
      (synthesis == aOther.synthesis) &&
      (fontFeatureSettings == aOther.fontFeatureSettings) &&
      (languageOverride == aOther.languageOverride) &&
      (variantAlternates == aOther.variantAlternates) &&
      (variantCaps == aOther.variantCaps) &&
      (variantEastAsian == aOther.variantEastAsian) &&
      (variantLigatures == aOther.variantLigatures) &&
      (variantNumeric == aOther.variantNumeric) &&
      (variantPosition == aOther.variantPosition) &&
      (alternateValues == aOther.alternateValues) &&
      (featureValueLookup == aOther.featureValueLookup)) {
    return true;
  }
  return false;
}

bool nsFont::Equals(const nsFont& aOther) const
{
  if (BaseEquals(aOther) &&
      (variant == aOther.variant) &&
      (decorations == aOther.decorations)) {
    return true;
  }
  return false;
}

nsFont& nsFont::operator=(const nsFont& aOther)
{
  name = aOther.name;
  style = aOther.style;
  systemFont = aOther.systemFont;
  variant = aOther.variant;
  weight = aOther.weight;
  stretch = aOther.stretch;
  decorations = aOther.decorations;
  size = aOther.size;
  sizeAdjust = aOther.sizeAdjust;
  kerning = aOther.kerning;
  synthesis = aOther.synthesis;
  fontFeatureSettings = aOther.fontFeatureSettings;
  languageOverride = aOther.languageOverride;
  variantAlternates = aOther.variantAlternates;
  variantCaps = aOther.variantCaps;
  variantEastAsian = aOther.variantEastAsian;
  variantLigatures = aOther.variantLigatures;
  variantNumeric = aOther.variantNumeric;
  variantPosition = aOther.variantPosition;
  alternateValues = aOther.alternateValues;
  featureValueLookup = aOther.featureValueLookup;
  return *this;
}

void
nsFont::CopyAlternates(const nsFont& aOther)
{
  variantAlternates = aOther.variantAlternates;
  alternateValues = aOther.alternateValues;
  featureValueLookup = aOther.featureValueLookup;
}

static bool IsGenericFontFamily(const nsString& aFamily)
{
  uint8_t generic;
  nsFont::GetGenericID(aFamily, &generic);
  return generic != kGenericFont_NONE;
}

const PRUnichar kNullCh       = PRUnichar('\0');
const PRUnichar kSingleQuote  = PRUnichar('\'');
const PRUnichar kDoubleQuote  = PRUnichar('\"');
const PRUnichar kComma        = PRUnichar(',');

bool nsFont::EnumerateFamilies(nsFontFamilyEnumFunc aFunc, void* aData) const
{
  const PRUnichar *p, *p_end;
  name.BeginReading(p);
  name.EndReading(p_end);
  nsAutoString family;

  while (p < p_end) {
    while (nsCRT::IsAsciiSpace(*p))
      if (++p == p_end)
        return true;

    bool generic;
    if (*p == kSingleQuote || *p == kDoubleQuote) {
      // quoted font family
      PRUnichar quoteMark = *p;
      if (++p == p_end)
        return true;
      const PRUnichar *nameStart = p;

      // XXX What about CSS character escapes?
      while (*p != quoteMark)
        if (++p == p_end)
          return true;

      family = Substring(nameStart, p);
      generic = false;

      while (++p != p_end && *p != kComma)
        /* nothing */ ;

    } else {
      // unquoted font family
      const PRUnichar *nameStart = p;
      while (++p != p_end && *p != kComma)
        /* nothing */ ;

      family = Substring(nameStart, p);
      family.CompressWhitespace(false, true);
      generic = IsGenericFontFamily(family);
    }

    if (!family.IsEmpty() && !(*aFunc)(family, generic, aData))
      return false;

    ++p; // may advance past p_end
  }

  return true;
}

// mapping from bitflag to font feature tag/value pair
//
// these need to be kept in sync with the constants listed
// in gfxFontConstants.h (e.g. NS_FONT_VARIANT_EAST_ASIAN_JIS78)

// NS_FONT_VARIANT_EAST_ASIAN_xxx values
const gfxFontFeature eastAsianDefaults[] = {
  { TRUETYPE_TAG('j','p','7','8'), 1 },
  { TRUETYPE_TAG('j','p','8','3'), 1 },
  { TRUETYPE_TAG('j','p','9','0'), 1 },
  { TRUETYPE_TAG('j','p','0','4'), 1 },
  { TRUETYPE_TAG('s','m','p','l'), 1 },
  { TRUETYPE_TAG('t','r','a','d'), 1 },
  { TRUETYPE_TAG('f','w','i','d'), 1 },
  { TRUETYPE_TAG('p','w','i','d'), 1 },
  { TRUETYPE_TAG('r','u','b','y'), 1 }
};

PR_STATIC_ASSERT(NS_ARRAY_LENGTH(eastAsianDefaults) ==
                 eFeatureEastAsian_numFeatures);

// NS_FONT_VARIANT_LIGATURES_xxx values
const gfxFontFeature ligDefaults[] = {
  { TRUETYPE_TAG('l','i','g','a'), 1 },
  { TRUETYPE_TAG('l','i','g','a'), 0 },
  { TRUETYPE_TAG('d','l','i','g'), 1 },
  { TRUETYPE_TAG('d','l','i','g'), 0 },
  { TRUETYPE_TAG('h','l','i','g'), 1 },
  { TRUETYPE_TAG('h','l','i','g'), 0 },
  { TRUETYPE_TAG('c','a','l','t'), 1 },
  { TRUETYPE_TAG('c','a','l','t'), 0 }
};

PR_STATIC_ASSERT(NS_ARRAY_LENGTH(ligDefaults) ==
                 eFeatureLigatures_numFeatures);

// NS_FONT_VARIANT_NUMERIC_xxx values
const gfxFontFeature numericDefaults[] = {
  { TRUETYPE_TAG('l','n','u','m'), 1 },
  { TRUETYPE_TAG('o','n','u','m'), 1 },
  { TRUETYPE_TAG('p','n','u','m'), 1 },
  { TRUETYPE_TAG('t','n','u','m'), 1 },
  { TRUETYPE_TAG('f','r','a','c'), 1 },
  { TRUETYPE_TAG('a','f','r','c'), 1 },
  { TRUETYPE_TAG('z','e','r','o'), 1 },
  { TRUETYPE_TAG('o','r','d','n'), 1 }
};

PR_STATIC_ASSERT(NS_ARRAY_LENGTH(numericDefaults) ==
                 eFeatureNumeric_numFeatures);

static void
AddFontFeaturesBitmask(uint32_t aValue, uint32_t aMin, uint32_t aMax,
                      const gfxFontFeature aFeatureDefaults[],
                      nsTArray<gfxFontFeature>& aFeaturesOut)

{
  uint32_t i, m;

  for (i = 0, m = aMin; m <= aMax; i++, m <<= 1) {
    if (m & aValue) {
      const gfxFontFeature& feature = aFeatureDefaults[i];
      aFeaturesOut.AppendElement(feature);
    }
  }
}

void nsFont::AddFontFeaturesToStyle(gfxFontStyle *aStyle) const
{
  // add in font-variant features
  gfxFontFeature setting;

  // -- kerning
  setting.mTag = TRUETYPE_TAG('k','e','r','n');
  switch (kerning) {
    case NS_FONT_KERNING_NONE:
      setting.mValue = 0;
      aStyle->featureSettings.AppendElement(setting);
      break;
    case NS_FONT_KERNING_NORMAL:
      setting.mValue = 1;
      aStyle->featureSettings.AppendElement(setting);
      break;
    default:
      // auto case implies use user agent default
      break;
  }

  // -- alternates
  if (variantAlternates & NS_FONT_VARIANT_ALTERNATES_HISTORICAL) {
    setting.mValue = 1;
    setting.mTag = TRUETYPE_TAG('h','i','s','t');
    aStyle->featureSettings.AppendElement(setting);
  }


  // -- copy font-specific alternate info into style
  //    (this will be resolved after font-matching occurs)
  aStyle->alternateValues.AppendElements(alternateValues);
  aStyle->featureValueLookup = featureValueLookup;

  // -- caps
  setting.mValue = 1;
  switch (variantCaps) {
    case NS_FONT_VARIANT_CAPS_ALLSMALL:
      setting.mTag = TRUETYPE_TAG('c','2','s','c');
      aStyle->featureSettings.AppendElement(setting);
      // fall through to the small-caps case
    case NS_FONT_VARIANT_CAPS_SMALLCAPS:
      setting.mTag = TRUETYPE_TAG('s','m','c','p');
      aStyle->featureSettings.AppendElement(setting);
      break;

    case NS_FONT_VARIANT_CAPS_ALLPETITE:
      setting.mTag = TRUETYPE_TAG('c','2','p','c');
      aStyle->featureSettings.AppendElement(setting);
      // fall through to the petite-caps case
    case NS_FONT_VARIANT_CAPS_PETITECAPS:
      setting.mTag = TRUETYPE_TAG('p','c','a','p');
      aStyle->featureSettings.AppendElement(setting);
      break;

    case NS_FONT_VARIANT_CAPS_TITLING:
      setting.mTag = TRUETYPE_TAG('t','i','t','l');
      aStyle->featureSettings.AppendElement(setting);
      break;

    case NS_FONT_VARIANT_CAPS_UNICASE:
      setting.mTag = TRUETYPE_TAG('u','n','i','c');
      aStyle->featureSettings.AppendElement(setting);
      break;

    default:
      break;
  }

  // -- east-asian
  if (variantEastAsian) {
    AddFontFeaturesBitmask(variantEastAsian,
                           NS_FONT_VARIANT_EAST_ASIAN_JIS78,
                           NS_FONT_VARIANT_EAST_ASIAN_RUBY,
                           eastAsianDefaults, aStyle->featureSettings);
  }

  // -- ligatures
  if (variantLigatures) {
    AddFontFeaturesBitmask(variantLigatures,
                           NS_FONT_VARIANT_LIGATURES_COMMON,
                           NS_FONT_VARIANT_LIGATURES_NO_CONTEXTUAL,
                           ligDefaults, aStyle->featureSettings);

    // special case common ligs, which also enable/disable clig
    if (variantLigatures & NS_FONT_VARIANT_LIGATURES_COMMON) {
      setting.mTag = TRUETYPE_TAG('c','l','i','g');
      setting.mValue = 1;
      aStyle->featureSettings.AppendElement(setting);
    } else if (variantLigatures & NS_FONT_VARIANT_LIGATURES_NO_COMMON) {
      setting.mTag = TRUETYPE_TAG('c','l','i','g');
      setting.mValue = 0;
      aStyle->featureSettings.AppendElement(setting);
    }
  }

  // -- numeric
  if (variantNumeric) {
    AddFontFeaturesBitmask(variantNumeric,
                           NS_FONT_VARIANT_NUMERIC_LINING,
                           NS_FONT_VARIANT_NUMERIC_ORDINAL,
                           numericDefaults, aStyle->featureSettings);
  }

  // -- position
  setting.mTag = 0;
  setting.mValue = 1;
  switch (variantPosition) {
    case NS_FONT_VARIANT_POSITION_SUPER:
      setting.mTag = TRUETYPE_TAG('s','u','p','s');
      aStyle->featureSettings.AppendElement(setting);
      break;

    case NS_FONT_VARIANT_POSITION_SUB:
      setting.mTag = TRUETYPE_TAG('s','u','b','s');
      aStyle->featureSettings.AppendElement(setting);
      break;

    default:
      break;
  }

  // add in features from font-feature-settings
  aStyle->featureSettings.AppendElements(fontFeatureSettings);
}

static bool FontEnumCallback(const nsString& aFamily, bool aGeneric, void *aData)
{
  *((nsString*)aData) = aFamily;
  return false;
}

void nsFont::GetFirstFamily(nsString& aFamily) const
{
  EnumerateFamilies(FontEnumCallback, &aFamily);
}

/*static*/
void nsFont::GetGenericID(const nsString& aGeneric, uint8_t* aID)
{
  *aID = kGenericFont_NONE;
  if (aGeneric.LowerCaseEqualsLiteral("-moz-fixed"))      *aID = kGenericFont_moz_fixed;
  else if (aGeneric.LowerCaseEqualsLiteral("serif"))      *aID = kGenericFont_serif;
  else if (aGeneric.LowerCaseEqualsLiteral("sans-serif")) *aID = kGenericFont_sans_serif;
  else if (aGeneric.LowerCaseEqualsLiteral("cursive"))    *aID = kGenericFont_cursive;
  else if (aGeneric.LowerCaseEqualsLiteral("fantasy"))    *aID = kGenericFont_fantasy;
  else if (aGeneric.LowerCaseEqualsLiteral("monospace"))  *aID = kGenericFont_monospace;
}
