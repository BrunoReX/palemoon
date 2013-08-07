/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is Mozilla Foundation code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#if defined(MOZ_WIDGET_GTK2)
#include "gfxPlatformGtk.h"
#define gfxToolkitPlatform gfxPlatformGtk
#elif defined(MOZ_WIDGET_QT)
#include <qfontinfo.h>
#include "gfxQtPlatform.h"
#define gfxToolkitPlatform gfxQtPlatform
#elif defined(XP_WIN)
#include "gfxWindowsPlatform.h"
#define gfxToolkitPlatform gfxWindowsPlatform
#elif defined(ANDROID)
#include "gfxAndroidPlatform.h"
#define gfxToolkitPlatform gfxAndroidPlatform
#endif

#include "gfxTypes.h"
#include "gfxFT2Fonts.h"
#include "gfxFT2FontBase.h"
#include "gfxFT2Utils.h"
#include "gfxFT2FontList.h"
#include <locale.h>
#include "gfxHarfBuzzShaper.h"
#include "gfxUnicodeProperties.h"
#include "gfxAtoms.h"
#include "nsTArray.h"
#include "nsUnicodeRange.h"
#include "nsCRT.h"

#include "prlog.h"
#include "prinit.h"

#include "mozilla/Preferences.h"

static PRLogModuleInfo *gFontLog = PR_NewLogModule("ft2fonts");

// rounding and truncation functions for a Freetype floating point number
// (FT26Dot6) stored in a 32bit integer with high 26 bits for the integer
// part and low 6 bits for the fractional part.
#define MOZ_FT_ROUND(x) (((x) + 32) & ~63) // 63 = 2^6 - 1
#define MOZ_FT_TRUNC(x) ((x) >> 6)
#define CONVERT_DESIGN_UNITS_TO_PIXELS(v, s) \
        MOZ_FT_TRUNC(MOZ_FT_ROUND(FT_MulFix((v) , (s))))

#ifndef ANDROID // not needed on Android, we use the generic gfxFontGroup
/**
 * gfxFT2FontGroup
 */

PRBool
gfxFT2FontGroup::FontCallback(const nsAString& fontName,
                              const nsACString& genericName,
                              PRBool aUseFontSet,
                              void *closure)
{
    nsTArray<nsString> *sa = static_cast<nsTArray<nsString>*>(closure);

    if (!fontName.IsEmpty() && !sa->Contains(fontName)) {
        sa->AppendElement(fontName);
#ifdef DEBUG_pavlov
        printf(" - %s\n", NS_ConvertUTF16toUTF8(fontName).get());
#endif
    }

    return PR_TRUE;
}

gfxFT2FontGroup::gfxFT2FontGroup(const nsAString& families,
                                 const gfxFontStyle *aStyle,
                                 gfxUserFontSet *aUserFontSet)
    : gfxFontGroup(families, aStyle, aUserFontSet)
{
#ifdef DEBUG_pavlov
    printf("Looking for %s\n", NS_ConvertUTF16toUTF8(families).get());
#endif
    nsTArray<nsString> familyArray;
    ForEachFont(FontCallback, &familyArray);

    if (familyArray.Length() == 0) {
        nsAutoString prefFamilies;
        gfxToolkitPlatform::GetPlatform()->GetPrefFonts(aStyle->language, prefFamilies, nsnull);
        if (!prefFamilies.IsEmpty()) {
            ForEachFont(prefFamilies, aStyle->language, FontCallback, &familyArray);
        }
    }
    if (familyArray.Length() == 0) {
#if defined(MOZ_WIDGET_QT) /* FIXME DFB */
        printf("failde to find a font. sadface\n");
        // We want to get rid of this entirely at some point, but first we need real lists of fonts.
        QFont defaultFont;
        QFontInfo fi (defaultFont);
        familyArray.AppendElement(nsDependentString(static_cast<const PRUnichar *>(fi.family().utf16())));
#elif defined(MOZ_WIDGET_GTK2)
        FcResult result;
        FcChar8 *family = nsnull;
        FcPattern* pat = FcPatternCreate();
        FcPattern *match = FcFontMatch(nsnull, pat, &result);
        if (match)
            FcPatternGetString(match, FC_FAMILY, 0, &family);
        if (family)
            familyArray.AppendElement(NS_ConvertUTF8toUTF16((char*)family));
#elif defined(XP_WIN)
        HGDIOBJ hGDI = ::GetStockObject(SYSTEM_FONT);
        LOGFONTW logFont;
        if (hGDI && ::GetObjectW(hGDI, sizeof(logFont), &logFont))
            familyArray.AppendElement(nsDependentString(logFont.lfFaceName));
#elif defined(ANDROID)
        familyArray.AppendElement(NS_LITERAL_STRING("Droid Sans"));
#else
#error "Platform not supported"
#endif
    }

    for (PRUint32 i = 0; i < familyArray.Length(); i++) {
        nsRefPtr<gfxFT2Font> font = gfxFT2Font::GetOrMakeFont(familyArray[i], &mStyle);
        if (font) {
            mFonts.AppendElement(font);
        }
    }
    NS_ASSERTION(mFonts.Length() > 0, "We need at least one font in a fontgroup");
}

gfxFT2FontGroup::~gfxFT2FontGroup()
{
}

gfxFontGroup *
gfxFT2FontGroup::Copy(const gfxFontStyle *aStyle)
{
    return new gfxFT2FontGroup(mFamilies, aStyle, nsnull);
}

// Helper function to return the leading UTF-8 character in a char pointer
// as 32bit number. Also sets the length of the current character (i.e. the
// offset to the next one) in the second argument
PRUint32 getUTF8CharAndNext(const PRUint8 *aString, PRUint8 *aLength)
{
    *aLength = 1;
    if (aString[0] < 0x80) { // normal 7bit ASCII char
        return aString[0];
    }
    if ((aString[0] >> 5) == 6) { // two leading ones -> two bytes
        *aLength = 2;
        return ((aString[0] & 0x1F) << 6) + (aString[1] & 0x3F);
    }
    if ((aString[0] >> 4) == 14) { // three leading ones -> three bytes
        *aLength = 3;
        return ((aString[0] & 0x0F) << 12) + ((aString[1] & 0x3F) << 6) +
               (aString[2] & 0x3F);
    }
    if ((aString[0] >> 4) == 15) { // four leading ones -> four bytes
        *aLength = 4;
        return ((aString[0] & 0x07) << 18) + ((aString[1] & 0x3F) << 12) +
               ((aString[2] & 0x3F) <<  6) + (aString[3] & 0x3F);
    }
    return aString[0];
}


static PRBool
AddFontNameToArray(const nsAString& aName,
                   const nsACString& aGenericName,
                   PRBool aUseFontSet,
                   void *aClosure)
{
    if (!aName.IsEmpty()) {
        nsTArray<nsString> *list = static_cast<nsTArray<nsString> *>(aClosure);

        if (list->IndexOf(aName) == list->NoIndex)
            list->AppendElement(aName);
    }

    return PR_TRUE;
}

void
gfxFT2FontGroup::FamilyListToArrayList(const nsString& aFamilies,
                                       nsIAtom *aLangGroup,
                                       nsTArray<nsRefPtr<gfxFontEntry> > *aFontEntryList)
{
    nsAutoTArray<nsString, 15> fonts;
    ForEachFont(aFamilies, aLangGroup, AddFontNameToArray, &fonts);

    PRUint32 len = fonts.Length();
    for (PRUint32 i = 0; i < len; ++i) {
        const nsString& str = fonts[i];
        nsRefPtr<gfxFontEntry> fe = (gfxToolkitPlatform::GetPlatform()->FindFontEntry(str, mStyle));
        aFontEntryList->AppendElement(fe);
    }
}

void gfxFT2FontGroup::GetPrefFonts(nsIAtom *aLangGroup, nsTArray<nsRefPtr<gfxFontEntry> >& aFontEntryList)
{
    NS_ASSERTION(aLangGroup, "aLangGroup is null");
    gfxToolkitPlatform *platform = gfxToolkitPlatform::GetPlatform();
    nsAutoTArray<nsRefPtr<gfxFontEntry>, 5> fonts;
    nsCAutoString key;
    aLangGroup->ToUTF8String(key);
    key.Append("-");
    key.AppendInt(GetStyle()->style);
    key.Append("-");
    key.AppendInt(GetStyle()->weight);
    if (!platform->GetPrefFontEntries(key, &fonts)) {
        nsString fontString;
        platform->GetPrefFonts(aLangGroup, fontString);
        if (fontString.IsEmpty())
            return;

        FamilyListToArrayList(fontString, aLangGroup, &fonts);

        platform->SetPrefFontEntries(key, fonts);
    }
    aFontEntryList.AppendElements(fonts);
}

static PRInt32 GetCJKLangGroupIndex(const char *aLangGroup) {
    PRInt32 i;
    for (i = 0; i < COUNT_OF_CJK_LANG_GROUP; i++) {
        if (!PL_strcasecmp(aLangGroup, sCJKLangGroup[i]))
            return i;
    }
    return -1;
}

// this function assigns to the array passed in.
void gfxFT2FontGroup::GetCJKPrefFonts(nsTArray<nsRefPtr<gfxFontEntry> >& aFontEntryList) {
    gfxToolkitPlatform *platform = gfxToolkitPlatform::GetPlatform();

    nsCAutoString key("x-internal-cjk-");
    key.AppendInt(mStyle.style);
    key.Append("-");
    key.AppendInt(mStyle.weight);

    if (!platform->GetPrefFontEntries(key, &aFontEntryList)) {
        NS_ENSURE_TRUE(Preferences::GetRootBranch(), );
        // Add the CJK pref fonts from accept languages, the order should be same order
        nsAdoptingCString list = Preferences::GetLocalizedCString("intl.accept_languages");
        if (!list.IsEmpty()) {
            const char kComma = ',';
            const char *p, *p_end;
            list.BeginReading(p);
            list.EndReading(p_end);
            while (p < p_end) {
                while (nsCRT::IsAsciiSpace(*p)) {
                    if (++p == p_end)
                        break;
                }
                if (p == p_end)
                    break;
                const char *start = p;
                while (++p != p_end && *p != kComma)
                    /* nothing */ ;
                nsCAutoString lang(Substring(start, p));
                lang.CompressWhitespace(PR_FALSE, PR_TRUE);
                PRInt32 index = GetCJKLangGroupIndex(lang.get());
                if (index >= 0) {
                    nsCOMPtr<nsIAtom> atom = do_GetAtom(sCJKLangGroup[index]);
                    GetPrefFonts(atom, aFontEntryList);
                }
                p++;
            }
        }

        // Add the system locale
#ifdef XP_WIN
        switch (::GetACP()) {
            case 932: GetPrefFonts(gfxAtoms::ja, aFontEntryList); break;
            case 936: GetPrefFonts(gfxAtoms::zh_cn, aFontEntryList); break;
            case 949: GetPrefFonts(gfxAtoms::ko, aFontEntryList); break;
            // XXX Don't we need to append gfxAtoms::zh_hk if the codepage is 950?
            case 950: GetPrefFonts(gfxAtoms::zh_tw, aFontEntryList); break;
        }
#else
        const char *ctype = setlocale(LC_CTYPE, NULL);
        if (ctype) {
            if (!PL_strncasecmp(ctype, "ja", 2)) {
                GetPrefFonts(gfxAtoms::ja, aFontEntryList);
            } else if (!PL_strncasecmp(ctype, "zh_cn", 5)) {
                GetPrefFonts(gfxAtoms::zh_cn, aFontEntryList);
            } else if (!PL_strncasecmp(ctype, "zh_hk", 5)) {
                GetPrefFonts(gfxAtoms::zh_hk, aFontEntryList);
            } else if (!PL_strncasecmp(ctype, "zh_tw", 5)) {
                GetPrefFonts(gfxAtoms::zh_tw, aFontEntryList);
            } else if (!PL_strncasecmp(ctype, "ko", 2)) {
                GetPrefFonts(gfxAtoms::ko, aFontEntryList);
            }
        }
#endif

        // last resort...
        GetPrefFonts(gfxAtoms::ja, aFontEntryList);
        GetPrefFonts(gfxAtoms::ko, aFontEntryList);
        GetPrefFonts(gfxAtoms::zh_cn, aFontEntryList);
        GetPrefFonts(gfxAtoms::zh_hk, aFontEntryList);
        GetPrefFonts(gfxAtoms::zh_tw, aFontEntryList);

        platform->SetPrefFontEntries(key, aFontEntryList);
    }
}

already_AddRefed<gfxFT2Font>
gfxFT2FontGroup::WhichFontSupportsChar(const nsTArray<nsRefPtr<gfxFontEntry> >& aFontEntryList, PRUint32 aCh)
{
    for (PRUint32 i = 0; i < aFontEntryList.Length(); i++) {
        gfxFontEntry *fe = aFontEntryList[i].get();
        if (fe->HasCharacter(aCh)) {
            nsRefPtr<gfxFT2Font> font =
                gfxFT2Font::GetOrMakeFont(static_cast<FontEntry*>(fe), &mStyle);
            return font.forget();
        }
    }
    return nsnull;
}

already_AddRefed<gfxFont>
gfxFT2FontGroup::WhichPrefFontSupportsChar(PRUint32 aCh)
{
    if (aCh > 0xFFFF)
        return nsnull;

    nsRefPtr<gfxFT2Font> selectedFont;

    // check out the style's language
    nsAutoTArray<nsRefPtr<gfxFontEntry>, 5> fonts;
    GetPrefFonts(mStyle.language, fonts);
    selectedFont = WhichFontSupportsChar(fonts, aCh);

    // otherwise search prefs
    if (!selectedFont) {
        PRUint32 unicodeRange = FindCharUnicodeRange(aCh);

        /* special case CJK */
        if (unicodeRange == kRangeSetCJK) {
            if (PR_LOG_TEST(gFontLog, PR_LOG_DEBUG)) {
                PR_LOG(gFontLog, PR_LOG_DEBUG, (" - Trying to find fonts for: CJK"));
            }

            nsAutoTArray<nsRefPtr<gfxFontEntry>, 15> fonts;
            GetCJKPrefFonts(fonts);
            selectedFont = WhichFontSupportsChar(fonts, aCh);
        } else {
            nsIAtom *langGroup = LangGroupFromUnicodeRange(unicodeRange);
            if (langGroup) {
                PR_LOG(gFontLog, PR_LOG_DEBUG, (" - Trying to find fonts for: %s", nsAtomCString(langGroup).get()));

                nsAutoTArray<nsRefPtr<gfxFontEntry>, 5> fonts;
                GetPrefFonts(langGroup, fonts);
                selectedFont = WhichFontSupportsChar(fonts, aCh);
            }
        }
    }

    if (selectedFont) {
        nsRefPtr<gfxFont> f = static_cast<gfxFont*>(selectedFont.get());
        return f.forget();
    }

    return nsnull;
}

already_AddRefed<gfxFont>
gfxFT2FontGroup::WhichSystemFontSupportsChar(PRUint32 aCh)
{
#if defined(XP_WIN) || defined(ANDROID)
    FontEntry *fe = static_cast<FontEntry*>
        (gfxPlatformFontList::PlatformFontList()->FindFontForChar(aCh, GetFontAt(0)));
    if (fe) {
        nsRefPtr<gfxFT2Font> f = gfxFT2Font::GetOrMakeFont(fe, &mStyle);
        nsRefPtr<gfxFont> font = f.get();
        return font.forget();
    }
#else
    nsRefPtr<gfxFont> selectedFont;
    nsRefPtr<gfxFont> refFont = GetFontAt(0);
    gfxToolkitPlatform *platform = gfxToolkitPlatform::GetPlatform();
    selectedFont = platform->FindFontForChar(aCh, refFont);
    if (selectedFont)
        return selectedFont.forget();
#endif
    return nsnull;
}

#endif // !ANDROID

/**
 * gfxFT2Font
 */

PRBool
gfxFT2Font::InitTextRun(gfxContext *aContext,
                        gfxTextRun *aTextRun,
                        const PRUnichar *aString,
                        PRUint32 aRunStart,
                        PRUint32 aRunLength,
                        PRInt32 aRunScript,
                        PRBool aPreferPlatformShaping)
{
    PRBool ok = PR_FALSE;

    if (gfxPlatform::GetPlatform()->UseHarfBuzzForScript(aRunScript)) {
        if (!mHarfBuzzShaper) {
            gfxFT2LockedFace face(this);
            mFUnitsConvFactor = face.XScale();

            mHarfBuzzShaper = new gfxHarfBuzzShaper(this);
        }
        ok = mHarfBuzzShaper->InitTextRun(aContext, aTextRun, aString,
                                          aRunStart, aRunLength, aRunScript);
    }

    if (!ok) {
        AddRange(aTextRun, aString, aRunStart, aRunLength);
    }

    aTextRun->AdjustAdvancesForSyntheticBold(aContext, aRunStart, aRunLength);

    return PR_TRUE;
}

void
gfxFT2Font::AddRange(gfxTextRun *aTextRun, const PRUnichar *str, PRUint32 offset, PRUint32 len)
{
    const PRUint32 appUnitsPerDevUnit = aTextRun->GetAppUnitsPerDevUnit();
    // we'll pass this in/figure it out dynamically, but at this point there can be only one face.
    gfxFT2LockedFace faceLock(this);
    FT_Face face = faceLock.get();

    gfxTextRun::CompressedGlyph g;

    const gfxFT2Font::CachedGlyphData *cgd = nsnull, *cgdNext = nsnull;

    FT_UInt spaceGlyph = GetSpaceGlyph();

    for (PRUint32 i = 0; i < len; i++) {
        PRUint32 ch = str[offset + i];

        if (ch == 0) {
            // treat this null byte as a missing glyph, don't create a glyph for it
            aTextRun->SetMissingGlyph(offset + i, 0);
            continue;
        }

        NS_ASSERTION(!gfxFontGroup::IsInvalidChar(ch), "Invalid char detected");

        if (cgdNext) {
            cgd = cgdNext;
            cgdNext = nsnull;
        } else {
            cgd = GetGlyphDataForChar(ch);
        }

        FT_UInt gid = cgd->glyphIndex;
        PRInt32 advance = 0;

        if (gid == 0) {
            advance = -1; // trigger the missing glyphs case below
        } else {
            // find next character and its glyph -- in case they exist
            // and exist in the current font face -- to compute kerning
            PRUint32 chNext = 0;
            FT_UInt gidNext = 0;
            FT_Pos lsbDeltaNext = 0;

            if (FT_HAS_KERNING(face) && i + 1 < len) {
                chNext = str[offset + i + 1];
                if (chNext != 0) {
                    cgdNext = GetGlyphDataForChar(chNext);
                    gidNext = cgdNext->glyphIndex;
                    if (gidNext && gidNext != spaceGlyph)
                        lsbDeltaNext = cgdNext->lsbDelta;
                }
            }

            advance = cgd->xAdvance;

            // now add kerning to the current glyph's advance
            if (chNext && gidNext) {
                FT_Vector kerning; kerning.x = 0;
                FT_Get_Kerning(face, gid, gidNext, FT_KERNING_DEFAULT, &kerning);
                advance += kerning.x;
                if (cgd->rsbDelta - lsbDeltaNext >= 32) {
                    advance -= 64;
                } else if (cgd->rsbDelta - lsbDeltaNext < -32) {
                    advance += 64;
                }
            }

            // convert 26.6 fixed point to app units
            // round rather than truncate to nearest pixel
            // because these advances are often scaled
            advance = ((advance * appUnitsPerDevUnit + 32) >> 6);
        }
#ifdef DEBUG_thebes_2
        printf(" gid=%d, advance=%d (%s)\n", gid, advance,
               NS_LossyConvertUTF16toASCII(font->GetName()).get());
#endif

        if (advance >= 0 &&
            gfxTextRun::CompressedGlyph::IsSimpleAdvance(advance) &&
            gfxTextRun::CompressedGlyph::IsSimpleGlyphID(gid)) {
            aTextRun->SetSimpleGlyph(offset + i, g.SetSimpleGlyph(advance, gid));
        } else if (gid == 0) {
            // gid = 0 only happens when the glyph is missing from the font
            aTextRun->SetMissingGlyph(offset + i, ch);
        } else {
            gfxTextRun::DetailedGlyph details;
            details.mGlyphID = gid;
            NS_ASSERTION(details.mGlyphID == gid, "Seriously weird glyph ID detected!");
            details.mAdvance = advance;
            details.mXOffset = 0;
            details.mYOffset = 0;
            g.SetComplex(aTextRun->IsClusterStart(offset + i), PR_TRUE, 1);
            aTextRun->SetGlyphs(offset + i, g, &details);
        }
    }
}

gfxFT2Font::gfxFT2Font(cairo_scaled_font_t *aCairoFont,
                       FT2FontEntry *aFontEntry,
                       const gfxFontStyle *aFontStyle,
                       PRBool aNeedsBold)
    : gfxFT2FontBase(aCairoFont, aFontEntry, aFontStyle)
{
    NS_ASSERTION(mFontEntry, "Unable to find font entry for font.  Something is whack.");
    mApplySyntheticBold = aNeedsBold;
    mCharGlyphCache.Init(64);
}

gfxFT2Font::~gfxFT2Font()
{
}

cairo_font_face_t *
gfxFT2Font::CairoFontFace()
{
    return GetFontEntry()->CairoFontFace();
}

/**
 * Look up the font in the gfxFont cache. If we don't find it, create one.
 * In either case, add a ref, append it to the aFonts array, and return it ---
 * except for OOM in which case we do nothing and return null.
 */
already_AddRefed<gfxFT2Font>
gfxFT2Font::GetOrMakeFont(const nsAString& aName, const gfxFontStyle *aStyle,
                          PRBool aNeedsBold)
{
#ifdef ANDROID
    FT2FontEntry *fe = static_cast<FT2FontEntry*>
        (gfxPlatformFontList::PlatformFontList()->
            FindFontForFamily(aName, aStyle, aNeedsBold));
#else
    FT2FontEntry *fe = static_cast<FT2FontEntry*>
        (gfxToolkitPlatform::GetPlatform()->FindFontEntry(aName, *aStyle));
#endif
    if (!fe) {
        NS_WARNING("Failed to find font entry for font!");
        return nsnull;
    }

    nsRefPtr<gfxFT2Font> font = GetOrMakeFont(fe, aStyle, aNeedsBold);
    return font.forget();
}

already_AddRefed<gfxFT2Font>
gfxFT2Font::GetOrMakeFont(FT2FontEntry *aFontEntry, const gfxFontStyle *aStyle,
                          PRBool aNeedsBold)
{
    nsRefPtr<gfxFont> font = gfxFontCache::GetCache()->Lookup(aFontEntry, aStyle);
    if (!font) {
        cairo_scaled_font_t *scaledFont = aFontEntry->CreateScaledFont(aStyle);
        font = new gfxFT2Font(scaledFont, aFontEntry, aStyle, aNeedsBold);
        cairo_scaled_font_destroy(scaledFont);
        if (!font)
            return nsnull;
        gfxFontCache::GetCache()->AddNew(font);
    }
    gfxFont *f = nsnull;
    font.swap(f);
    return static_cast<gfxFT2Font *>(f);
}

void
gfxFT2Font::FillGlyphDataForChar(PRUint32 ch, CachedGlyphData *gd)
{
    gfxFT2LockedFace faceLock(this);
    FT_Face face = faceLock.get();

    FT_UInt gid = FT_Get_Char_Index(face, ch);

    if (gid == 0) {
        // this font doesn't support this char!
        NS_ASSERTION(gid != 0, "We don't have a glyph, but font indicated that it supported this char in tables?");
        gd->glyphIndex = 0;
        return;
    }

#ifdef MOZ_GFX_OPTIMIZE_MOBILE
    FT_Error err = FT_Load_Glyph(face, gid, FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_HINTING);
#else
    FT_Error err = FT_Load_Glyph(face, gid, FT_LOAD_DEFAULT);
#endif

    if (err) {
        // hmm, this is weird, we failed to load a glyph that we had?
        NS_WARNING("Failed to load glyph that we got from Get_Char_index");

        gd->glyphIndex = 0;
        return;
    }

    gd->glyphIndex = gid;
    gd->lsbDelta = face->glyph->lsb_delta;
    gd->rsbDelta = face->glyph->rsb_delta;
    gd->xAdvance = face->glyph->advance.x;
}
