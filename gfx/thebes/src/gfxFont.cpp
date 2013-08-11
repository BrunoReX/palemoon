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
 * Portions created by the Initial Developer are Copyright (C) 2005-2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Stuart Parmenter <stuart@mozilla.com>
 *   Masayuki Nakano <masayuki@d-toybox.com>
 *   John Daggett <jdaggett@mozilla.com>
 *   Jonathan Kew <jfkthame@gmail.com>
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

#include "nsIPrefService.h"
#include "nsServiceManagerUtils.h"
#include "nsReadableUtils.h"
#include "nsExpirationTracker.h"

#include "gfxFont.h"
#include "gfxPlatform.h"

#include "prtypes.h"
#include "gfxTypes.h"
#include "gfxContext.h"
#include "gfxFontMissingGlyphs.h"
#include "gfxUserFontSet.h"
#include "gfxPlatformFontList.h"
#include "nsMathUtils.h"
#include "nsBidiUtils.h"

#include "cairo.h"
#include "gfxFontTest.h"

#include "nsCRT.h"

gfxFontCache *gfxFontCache::gGlobalCache = nsnull;

static PRLogModuleInfo *gFontSelection = PR_NewLogModule("fontSelectionLog");

#ifdef DEBUG_roc
#define DEBUG_TEXT_RUN_STORAGE_METRICS
#endif

#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
static PRUint32 gTextRunStorageHighWaterMark = 0;
static PRUint32 gTextRunStorage = 0;
static PRUint32 gFontCount = 0;
static PRUint32 gGlyphExtentsCount = 0;
static PRUint32 gGlyphExtentsWidthsTotalSize = 0;
static PRUint32 gGlyphExtentsSetupEagerSimple = 0;
static PRUint32 gGlyphExtentsSetupEagerTight = 0;
static PRUint32 gGlyphExtentsSetupLazyTight = 0;
static PRUint32 gGlyphExtentsSetupFallBackToTight = 0;
#endif

gfxFontEntry::~gfxFontEntry() 
{
    if (mUserFontData)
        delete mUserFontData;
}


PRBool gfxFontEntry::TestCharacterMap(PRUint32 aCh)
{
    if (!mCmapInitialized) {
        ReadCMAP();
    }
    return mCharacterMap.test(aCh);
}


nsresult gfxFontEntry::ReadCMAP()
{
    mCmapInitialized = PR_TRUE;
    return NS_OK;
}


const nsString& gfxFontEntry::FamilyName()
{
    NS_ASSERTION(mFamily, "gfxFontEntry is not a member of a family");
    return mFamily->Name();
}

// we consider faces with mStandardFace == PR_TRUE to be "greater than" those with PR_FALSE,
// because during style matching, later entries will replace earlier ones
class FontEntryStandardFaceComparator {
  public:
    PRBool Equals(const nsRefPtr<gfxFontEntry>& a, const nsRefPtr<gfxFontEntry>& b) const {
        return a->mStandardFace == b->mStandardFace;
    }
    PRBool LessThan(const nsRefPtr<gfxFontEntry>& a, const nsRefPtr<gfxFontEntry>& b) const {
        return (a->mStandardFace == PR_FALSE && b->mStandardFace == PR_TRUE);
    }
};

void
gfxFontFamily::SortAvailableFonts()
{
    mAvailableFonts.Sort(FontEntryStandardFaceComparator());
}

PRBool
gfxFontFamily::HasOtherFamilyNames()
{
    // need to read in other family names to determine this
    if (!mOtherFamilyNamesInitialized) {
        AddOtherFamilyNameFunctor addOtherNames(gfxPlatformFontList::PlatformFontList());
        ReadOtherFamilyNames(addOtherNames);  // sets mHasOtherFamilyNames
    }
    return mHasOtherFamilyNames;
}

gfxFontEntry*
gfxFontFamily::FindFontForStyle(const gfxFontStyle& aFontStyle, PRBool& aNeedsBold)
{
    if (!mHasStyles)
        FindStyleVariations(); // collect faces for the family, if not already done

    NS_ASSERTION(mAvailableFonts.Length() > 0, "font family with no faces!");

    aNeedsBold = PR_FALSE;

    PRInt8 baseWeight, weightDistance;
    aFontStyle.ComputeWeightAndOffset(&baseWeight, &weightDistance);
    PRBool wantBold = baseWeight >= 6;
    if ((wantBold && weightDistance < 0) || (!wantBold && weightDistance > 0)) {
        wantBold = !wantBold;
    }

    // If the family has only one face, we simply return it; no further checking needed
    if (mAvailableFonts.Length() == 1) {
        gfxFontEntry *fe = mAvailableFonts[0];
        aNeedsBold = wantBold && !fe->IsBold();
        return fe;
    }

    PRBool wantItalic = (aFontStyle.style & (FONT_STYLE_ITALIC | FONT_STYLE_OBLIQUE)) != 0;

    // Most families are "simple", having just Regular/Bold/Italic/BoldItalic,
    // or some subset of these. In this case, we have exactly 4 entries in mAvailableFonts,
    // stored in the above order; note that some of the entries may be NULL.
    // We can then pick the required entry based on whether the request is for
    // bold or non-bold, italic or non-italic, without running the more complex
    // matching algorithm used for larger families with many weights and/or widths.

    if (mIsSimpleFamily) {
        // Family has no more than the "standard" 4 faces, at fixed indexes;
        // calculate which one we want.
        // Note that we cannot simply return it as not all 4 faces are necessarily present.
        PRUint8 faceIndex = (wantItalic ? kItalicMask : 0) |
                            (wantBold ? kBoldMask : 0);

        // if the desired style is available, return it directly
        gfxFontEntry *fe = mAvailableFonts[faceIndex];
        if (fe) {
            // no need to set aNeedsBold here as we matched the boldness request
            return fe;
        }

        // order to check fallback faces in a simple family, depending on requested style
        static const PRUint8 simpleFallbacks[4][3] = {
            { kBoldFaceIndex, kItalicFaceIndex, kBoldItalicFaceIndex },   // fallbacks for Regular
            { kRegularFaceIndex, kBoldItalicFaceIndex, kItalicFaceIndex },// Bold
            { kBoldItalicFaceIndex, kRegularFaceIndex, kBoldFaceIndex },  // Italic
            { kItalicFaceIndex, kBoldFaceIndex, kRegularFaceIndex }       // BoldItalic
        };
        const PRUint8 *order = simpleFallbacks[faceIndex];

        for (PRUint8 trial = 0; trial < 3; ++trial) {
            // check remaining faces in order of preference to find the first that actually exists
            fe = mAvailableFonts[order[trial]];
            if (fe) {
                PR_LOG(gFontSelection, PR_LOG_DEBUG,
                       ("(FindFontForStyle) name: %s, sty: %02x, wt: %d, sz: %.1f -> %s (trial %d)\n", 
                        NS_ConvertUTF16toUTF8(mName).get(),
                        aFontStyle.style, aFontStyle.weight, aFontStyle.size,
                        NS_ConvertUTF16toUTF8(fe->Name()).get(), trial));
                aNeedsBold = wantBold && !fe->IsBold();
                return fe;
            }
        }

        // this can't happen unless we have totally broken the font-list manager!
        NS_NOTREACHED("no face found in simple font family!");
        return nsnull;
    }

    // This is a large/rich font family, so we do full style- and weight-matching:
    // first collect a list of weights that are the best match for the requested
    // font-stretch and font-style, then pick the best weight match among those
    // available.

    gfxFontEntry *weightList[10] = { 0 };
    PRBool foundWeights = FindWeightsForStyle(weightList, wantItalic, aFontStyle.stretch);
    if (!foundWeights) {
        PR_LOG(gFontSelection, PR_LOG_DEBUG,
               ("(FindFontForStyle) name: %s, sty: %02x, wt: %d, sz: %.1f -> null\n", 
                NS_ConvertUTF16toUTF8(mName).get(),
                aFontStyle.style, aFontStyle.weight, aFontStyle.size));
        return nsnull;
    }

    // 500 isn't quite bold so we want to treat it as 400 if we don't
    // have a 500 weight
    if (baseWeight == 5 && weightDistance == 0) {
        // If we have a 500 weight then use it
        if (weightList[5]) {
            PR_LOG(gFontSelection, PR_LOG_DEBUG,
                   ("(FindFontForStyle) name: %s, sty: %02x, wt: %d, sz: %.1f -> %s using wt 500\n", 
                    NS_ConvertUTF16toUTF8(mName).get(),
                    aFontStyle.style, aFontStyle.weight, aFontStyle.size,
                    NS_ConvertUTF16toUTF8(weightList[5]->Name()).get()));
            return weightList[5];
        }

        // Otherwise treat as 400
        baseWeight = 4;
    }

    PRInt8 matchBaseWeight = 0;
    PRInt8 direction = (baseWeight > 5) ? 1 : -1;
    for (PRInt8 i = baseWeight; ; i += direction) {
        if (weightList[i]) {
            matchBaseWeight = i;
            break;
        }

        // if we've reached one side without finding a font,
        // go the other direction until we find a match
        if (i == 1 || i == 9) {
            direction = -direction;
        }
    }

    gfxFontEntry *matchFE;
    const PRInt8 absDistance = abs(weightDistance);
    direction = (weightDistance >= 0) ? 1 : -1;
    PRInt8 i, wghtSteps = 0;

    // account for synthetic bold in lighter case
    // if lighter is applied with an inherited bold weight,
    // and no actual bold faces exist, synthetic bold is used
    // so the matched weight above is actually one step down already
    if (weightDistance < 0 && baseWeight > 5 && matchBaseWeight < 6) {
        wghtSteps = 1; // if no faces [600, 900] then synthetic bold at 700
    }

    for (i = matchBaseWeight; i < 10 && i > 0; i += direction) {
        if (weightList[i]) {
            matchFE = weightList[i];
            wghtSteps++;
        }
        if (wghtSteps > absDistance)
            break;
    }

    if (weightDistance > 0 && wghtSteps <= absDistance) {
        aNeedsBold = PR_TRUE;
    }

    if (!matchFE) {
        matchFE = weightList[matchBaseWeight];
    }

    PR_LOG(gFontSelection, PR_LOG_DEBUG,
           ("(FindFontForStyle) name: %s, sty: %02x, wt: %d, sz: %.1f -> %s\n", 
            NS_ConvertUTF16toUTF8(mName).get(),
            aFontStyle.style, aFontStyle.weight, aFontStyle.size,
            NS_ConvertUTF16toUTF8(matchFE->Name()).get()));
    NS_ASSERTION(matchFE, "we should always be able to return something here");
    return matchFE;
}

void
gfxFontFamily::CheckForSimpleFamily()
{
    if (mAvailableFonts.Length() > 4) {
        return; // can't be "simple" if there are >4 faces
    }

    PRInt16 firstStretch = mAvailableFonts[0]->Stretch();

    gfxFontEntry *faces[4] = { 0 };
    for (PRUint8 i = 0; i < mAvailableFonts.Length(); ++i) {
        gfxFontEntry *fe = mAvailableFonts[i];
        if (fe->Stretch() != firstStretch) {
            return; // font-stretch doesn't match, don't treat as simple family
        }
        PRUint8 faceIndex = (fe->IsItalic() ? kItalicMask : 0) |
                            (fe->Weight() >= 600 ? kBoldMask : 0);
        if (faces[faceIndex]) {
            return; // two faces resolve to the same slot; family isn't "simple"
        }
        faces[faceIndex] = fe;
    }

    // we have successfully slotted the available faces into the standard
    // 4-face framework
    mAvailableFonts.SetLength(4);
    for (PRUint8 i = 0; i < 4; ++i) {
        if (mAvailableFonts[i].get() != faces[i]) {
            mAvailableFonts[i].swap(faces[i]);
        }
    }

    mIsSimpleFamily = PR_TRUE;
}

static inline PRUint32
StyleDistance(gfxFontEntry *aFontEntry,
              PRBool anItalic, PRInt16 aStretch)
{
    // Compute a measure of the "distance" between the requested style
    // and the given fontEntry,
    // considering italicness and font-stretch but not weight.

    // TODO (refine CSS spec...): discuss priority of italic vs stretch;
    // whether penalty for stretch mismatch should depend on actual difference in values;
    // whether a sign mismatch in stretch should increase the effective distance

    return (aFontEntry->IsItalic() != anItalic ? 1 : 0) +
           (aFontEntry->mStretch != aStretch ? 10 : 0);
}

PRBool
gfxFontFamily::FindWeightsForStyle(gfxFontEntry* aFontsForWeights[],
                                   PRBool anItalic, PRInt16 aStretch)
{
    PRUint32 foundWeights = 0;
    PRUint32 bestMatchDistance = 0xffffffff;

    for (PRUint32 i = 0; i < mAvailableFonts.Length(); i++) {
        // this is not called for "simple" families, and therefore it does not
        // need to check the mAvailableFonts entries for NULL
        gfxFontEntry *fe = mAvailableFonts[i];
        PRUint32 distance = StyleDistance(fe, anItalic, aStretch);
        if (distance <= bestMatchDistance) {
            PRInt8 wt = fe->mWeight / 100;
            NS_ASSERTION(wt >= 1 && wt < 10, "invalid weight in fontEntry");
            if (!aFontsForWeights[wt]) {
                // record this as a possible candidate for weight matching
                aFontsForWeights[wt] = fe;
                ++foundWeights;
            } else {
                PRUint32 prevDistance = StyleDistance(aFontsForWeights[wt], anItalic, aStretch);
                if (prevDistance >= distance) {
                    // replacing a weight we already found, so don't increment foundWeights
                    aFontsForWeights[wt] = fe;
                }
            }
            bestMatchDistance = distance;
        }
    }

    NS_ASSERTION(foundWeights > 0, "Font family containing no faces?");

    if (foundWeights == 1) {
        // no need to cull entries if we only found one weight
        return PR_TRUE;
    }

    // we might have recorded some faces that were a partial style match, but later found
    // others that were closer; in this case, we need to cull the poorer matches from the
    // weight list we'll return
    for (PRUint32 i = 0; i < 10; ++i) {
        if (aFontsForWeights[i] &&
            StyleDistance(aFontsForWeights[i], anItalic, aStretch) > bestMatchDistance)
        {
            aFontsForWeights[i] = 0;
        }
    }

    return (foundWeights > 0);
}


void gfxFontFamily::LocalizedName(nsAString& aLocalizedName)
{
    // just return the primary name; subclasses should override
    aLocalizedName = mName;
}


void
gfxFontFamily::FindFontForChar(FontSearch *aMatchData)
{
    // xxx - optimization point - keep a bit vector with the union of supported unicode ranges
    // by all fonts for this family and bail immediately if the character is not in any of
    // this family's cmaps

    // iterate over fonts
    PRUint32 numFonts = mAvailableFonts.Length();
    for (PRUint32 i = 0; i < numFonts; i++) {
        gfxFontEntry *fe = mAvailableFonts[i];
        if (!fe)
            continue;

        PRInt32 rank = 0;

        if (fe->TestCharacterMap(aMatchData->mCh)) {
            rank += 20;
        }

        // if we didn't match any characters don't bother wasting more time with this face.
        if (rank == 0)
            continue;
            
        // omitting from original windows code -- family name, lang group, pitch
        // not available in current FontEntry implementation

        if (aMatchData->mFontToMatch) { 
            const gfxFontStyle *style = aMatchData->mFontToMatch->GetStyle();
            
            // italics
            if (fe->IsItalic() && 
                    (style->style & (FONT_STYLE_ITALIC | FONT_STYLE_OBLIQUE)) != 0) {
                rank += 5;
            }
            
            // weight
            PRInt8 baseWeight, weightDistance;
            style->ComputeWeightAndOffset(&baseWeight, &weightDistance);

            // xxx - not entirely correct, the one unit of weight distance reflects 
            // the "next bolder/lighter face"
            PRInt32 targetWeight = (baseWeight * 100) + (weightDistance * 100);

            PRInt32 entryWeight = fe->Weight();
            if (entryWeight == targetWeight) {
                rank += 5;
            } else {
                PRUint32 diffWeight = abs(entryWeight - targetWeight);
                if (diffWeight <= 100)  // favor faces close in weight
                    rank += 2;
            }
        } else {
            // if no font to match, prefer non-bold, non-italic fonts
            if (!fe->IsItalic() && !fe->IsBold())
                rank += 5;
        }
        
        // xxx - add whether AAT font with morphing info for specific lang groups
        
        if (rank > aMatchData->mMatchRank
            || (rank == aMatchData->mMatchRank &&
                Compare(fe->Name(), aMatchData->mBestMatch->Name()) > 0)) 
        {
            aMatchData->mBestMatch = fe;
            aMatchData->mMatchRank = rank;
        }
    }
}


// returns true if other names were found, false otherwise
PRBool
gfxFontFamily::ReadOtherFamilyNamesForFace(AddOtherFamilyNameFunctor& aOtherFamilyFunctor,
                                           gfxFontEntry *aFontEntry,
                                           PRBool useFullName)
{
    const PRUint32 kNAME = TRUETYPE_TAG('n','a','m','e');

    nsAutoTArray<PRUint8,8192> buffer;
    if (aFontEntry->GetFontTable(kNAME, buffer) != NS_OK)
        return PR_FALSE;

    const PRUint8 *nameData = buffer.Elements();
    PRUint32 dataLength = buffer.Length();
    const gfxFontUtils::NameHeader *nameHeader =
        reinterpret_cast<const gfxFontUtils::NameHeader*>(nameData);

    PRUint32 nameCount = nameHeader->count;
    if (nameCount * sizeof(gfxFontUtils::NameRecord) > dataLength) {
        NS_WARNING("invalid font (name records)");
        return PR_FALSE;
    }
    
    const gfxFontUtils::NameRecord *nameRecord =
        reinterpret_cast<const gfxFontUtils::NameRecord*>(nameData + sizeof(gfxFontUtils::NameHeader));
    PRUint32 stringsBase = PRUint32(nameHeader->stringOffset);

    PRBool foundNames = PR_FALSE;
    for (PRUint32 i = 0; i < nameCount; i++, nameRecord++) {
        PRUint32 nameLen = nameRecord->length;
        PRUint32 nameOff = nameRecord->offset;  // offset from base of string storage

        if (stringsBase + nameOff + nameLen > dataLength) {
            NS_WARNING("invalid font (name table strings)");
            return PR_FALSE;
        }

        PRUint16 nameID = nameRecord->nameID;
        if ((useFullName && nameID == gfxFontUtils::NAME_ID_FULL) ||
            (!useFullName && (nameID == gfxFontUtils::NAME_ID_FAMILY ||
                              nameID == gfxFontUtils::NAME_ID_PREFERRED_FAMILY))) {
            nsAutoString otherFamilyName;
            PRBool ok = gfxFontUtils::DecodeFontName(nameData + stringsBase + nameOff,
                                                     nameLen,
                                                     PRUint32(nameRecord->platformID),
                                                     PRUint32(nameRecord->encodingID),
                                                     PRUint32(nameRecord->languageID),
                                                     otherFamilyName);
            // add if not same as canonical family name
            if (ok && otherFamilyName != mName) {
                aOtherFamilyFunctor(this, otherFamilyName);
                foundNames = PR_TRUE;
            }
        }
    }

    return foundNames;
}


void
gfxFontFamily::ReadOtherFamilyNames(AddOtherFamilyNameFunctor& aOtherFamilyFunctor)
{
    if (mOtherFamilyNamesInitialized) 
        return;
    mOtherFamilyNamesInitialized = PR_TRUE;

    // read in other family names for the first face in the list
    PRUint32 numFonts = mAvailableFonts.Length();
    PRUint32 i;
    for (i = 0; i < numFonts; ++i) {
        if (!mAvailableFonts[i])
            continue;
        mHasOtherFamilyNames = ReadOtherFamilyNamesForFace(aOtherFamilyFunctor,
                                                           mAvailableFonts[i].get());
        break;
    }

    // read in other names for the first face in the list with the assumption
    // that if extra names don't exist in that face then they don't exist in
    // other faces for the same font
    if (mHasOtherFamilyNames) {
        // read in names for all faces, needed to catch cases where fonts have
        // family names for individual weights (e.g. Hiragino Kaku Gothic Pro W6)
        for ( ; i < numFonts; i++) {
            if (!mAvailableFonts[i])
                continue;
            ReadOtherFamilyNamesForFace(aOtherFamilyFunctor, mAvailableFonts[i].get());
        }
    }
}


gfxFontEntry*
gfxFontFamily::FindFont(const nsAString& aPostscriptName)
{
    // find the font using a simple linear search
    PRUint32 numFonts = mAvailableFonts.Length();
    for (PRUint32 i = 0; i < numFonts; i++) {
        gfxFontEntry *fe = mAvailableFonts[i].get();
        if (fe && fe->Name() == aPostscriptName)
            return fe;
    }
    return nsnull;
}


nsresult
gfxFontCache::Init()
{
    NS_ASSERTION(!gGlobalCache, "Where did this come from?");
    gGlobalCache = new gfxFontCache();
    return gGlobalCache ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

void
gfxFontCache::Shutdown()
{
    delete gGlobalCache;
    gGlobalCache = nsnull;

#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
    printf("Textrun storage high water mark=%d\n", gTextRunStorageHighWaterMark);
    printf("Total number of fonts=%d\n", gFontCount);
    printf("Total glyph extents allocated=%d (size %d)\n", gGlyphExtentsCount,
            int(gGlyphExtentsCount*sizeof(gfxGlyphExtents)));
    printf("Total glyph extents width-storage size allocated=%d\n", gGlyphExtentsWidthsTotalSize);
    printf("Number of simple glyph extents eagerly requested=%d\n", gGlyphExtentsSetupEagerSimple);
    printf("Number of tight glyph extents eagerly requested=%d\n", gGlyphExtentsSetupEagerTight);
    printf("Number of tight glyph extents lazily requested=%d\n", gGlyphExtentsSetupLazyTight);
    printf("Number of simple glyph extent setups that fell back to tight=%d\n", gGlyphExtentsSetupFallBackToTight);
#endif
}

PRBool
gfxFontCache::HashEntry::KeyEquals(const KeyTypePointer aKey) const
{
    return aKey->mString.Equals(mFont->GetName()) &&
           aKey->mStyle->Equals(*mFont->GetStyle());
}

already_AddRefed<gfxFont>
gfxFontCache::Lookup(const nsAString &aName,
                     const gfxFontStyle *aStyle)
{
    Key key(aName, aStyle);
    HashEntry *entry = mFonts.GetEntry(key);
    if (!entry)
        return nsnull;

    gfxFont *font = entry->mFont;
    NS_ADDREF(font);
    return font;
}

void
gfxFontCache::AddNew(gfxFont *aFont)
{
    Key key(aFont->GetName(), aFont->GetStyle());
    HashEntry *entry = mFonts.PutEntry(key);
    if (!entry)
        return;
    gfxFont *oldFont = entry->mFont;
    entry->mFont = aFont;
    // If someone's asked us to replace an existing font entry, then that's a
    // bit weird, but let it happen, and expire the old font if it's not used.
    if (oldFont && oldFont->GetExpirationState()->IsTracked()) {
        // if oldFont == aFont, recount should be > 0,
        // so we shouldn't be here.
        NS_ASSERTION(aFont != oldFont, "new font is tracked for expiry!");
        NotifyExpired(oldFont);
    }
}

void
gfxFontCache::NotifyReleased(gfxFont *aFont)
{
    nsresult rv = AddObject(aFont);
    if (NS_FAILED(rv)) {
        // We couldn't track it for some reason. Kill it now.
        DestroyFont(aFont);
    }
    // Note that we might have fonts that aren't in the hashtable, perhaps because
    // of OOM adding to the hashtable or because someone did an AddNew where
    // we already had a font. These fonts are added to the expiration tracker
    // anyway, even though Lookup can't resurrect them. Eventually they will
    // expire and be deleted.
}

void
gfxFontCache::NotifyExpired(gfxFont *aFont)
{
    RemoveObject(aFont);
    DestroyFont(aFont);
}

void
gfxFontCache::DestroyFont(gfxFont *aFont)
{
    Key key(aFont->GetName(), aFont->GetStyle());
    HashEntry *entry = mFonts.GetEntry(key);
    if (entry && entry->mFont == aFont)
        mFonts.RemoveEntry(key);
    NS_ASSERTION(aFont->GetRefCount() == 0,
                 "Destroying with non-zero ref count!");
    delete aFont;
}

void
gfxFont::RunMetrics::CombineWith(const RunMetrics& aOther, PRBool aOtherIsOnLeft)
{
    mAscent = PR_MAX(mAscent, aOther.mAscent);
    mDescent = PR_MAX(mDescent, aOther.mDescent);
    if (aOtherIsOnLeft) {
        mBoundingBox =
            (mBoundingBox + gfxPoint(aOther.mAdvanceWidth, 0)).Union(aOther.mBoundingBox);
    } else {
        mBoundingBox =
            mBoundingBox.Union(aOther.mBoundingBox + gfxPoint(mAdvanceWidth, 0));
    }
    mAdvanceWidth += aOther.mAdvanceWidth;
}

gfxFont::gfxFont(gfxFontEntry *aFontEntry, const gfxFontStyle *aFontStyle) :
    mFontEntry(aFontEntry), mIsValid(PR_TRUE), mStyle(*aFontStyle), mSyntheticBoldOffset(0)
{
#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
    ++gFontCount;
#endif
}

gfxFont::~gfxFont()
{
    PRUint32 i;
    // We destroy the contents of mGlyphExtentsArray explicitly instead of
    // using nsAutoPtr because VC++ can't deal with nsTArrays of nsAutoPtrs
    // of classes that lack a proper copy constructor
    for (i = 0; i < mGlyphExtentsArray.Length(); ++i) {
        delete mGlyphExtentsArray[i];
    }
}

/**
 * A helper function in case we need to do any rounding or other
 * processing here.
 */
#define ToDeviceUnits(aAppUnits, aDevUnitsPerAppUnit) \
    (double(aAppUnits)*double(aDevUnitsPerAppUnit))

struct GlyphBuffer {
#define GLYPH_BUFFER_SIZE (2048/sizeof(cairo_glyph_t))
    cairo_glyph_t mGlyphBuffer[GLYPH_BUFFER_SIZE];
    unsigned int mNumGlyphs;

    GlyphBuffer()
        : mNumGlyphs(0) { }

    cairo_glyph_t *AppendGlyph() {
        return &mGlyphBuffer[mNumGlyphs++];
    }

    void Flush(cairo_t *aCR, PRBool aDrawToPath, PRBool aReverse,
               PRBool aFinish = PR_FALSE) {
        // Ensure there's enough room for at least two glyphs in the
        // buffer (because we may allocate two glyphs between flushes)
        if (!aFinish && mNumGlyphs + 2 <= GLYPH_BUFFER_SIZE)
            return;

        if (aReverse) {
            for (PRUint32 i = 0; i < mNumGlyphs/2; ++i) {
                cairo_glyph_t tmp = mGlyphBuffer[i];
                mGlyphBuffer[i] = mGlyphBuffer[mNumGlyphs - 1 - i];
                mGlyphBuffer[mNumGlyphs - 1 - i] = tmp;
            }
        }
        if (aDrawToPath)
            cairo_glyph_path(aCR, mGlyphBuffer, mNumGlyphs);
        else
            cairo_show_glyphs(aCR, mGlyphBuffer, mNumGlyphs);

        mNumGlyphs = 0;
    }
#undef GLYPH_BUFFER_SIZE
};

void
gfxFont::Draw(gfxTextRun *aTextRun, PRUint32 aStart, PRUint32 aEnd,
              gfxContext *aContext, PRBool aDrawToPath, gfxPoint *aPt,
              Spacing *aSpacing)
{
    if (aStart >= aEnd)
        return;

    const gfxTextRun::CompressedGlyph *charGlyphs = aTextRun->GetCharacterGlyphs();
    const PRUint32 appUnitsPerDevUnit = aTextRun->GetAppUnitsPerDevUnit();
    const double devUnitsPerAppUnit = 1.0/double(appUnitsPerDevUnit);
    PRBool isRTL = aTextRun->IsRightToLeft();
    double direction = aTextRun->GetDirection();
    // double-strike in direction of run
    double synBoldDevUnitOffsetAppUnits =
      direction * (double) mSyntheticBoldOffset * appUnitsPerDevUnit;
    PRUint32 i;
    // Current position in appunits
    double x = aPt->x;
    double y = aPt->y;

    PRBool success = SetupCairoFont(aContext);
    if (NS_UNLIKELY(!success))
        return;

    GlyphBuffer glyphs;
    cairo_glyph_t *glyph;
    cairo_t *cr = aContext->GetCairo();

    if (aSpacing) {
        x += direction*aSpacing[0].mBefore;
    }
    for (i = aStart; i < aEnd; ++i) {
        const gfxTextRun::CompressedGlyph *glyphData = &charGlyphs[i];
        if (glyphData->IsSimpleGlyph()) {
            glyph = glyphs.AppendGlyph();
            glyph->index = glyphData->GetSimpleGlyph();
            double advance = glyphData->GetSimpleAdvance();
            // Perhaps we should put a scale in the cairo context instead of
            // doing this scaling here...
            // Multiplying by the reciprocal may introduce tiny error here,
            // but we assume cairo is going to round coordinates at some stage
            // and this is faster
            double glyphX;
            if (isRTL) {
                x -= advance;
                glyphX = x;
            } else {
                glyphX = x;
                x += advance;
            }
            glyph->x = ToDeviceUnits(glyphX, devUnitsPerAppUnit);
            glyph->y = ToDeviceUnits(y, devUnitsPerAppUnit);
            
            // synthetic bolding by drawing with a one-pixel offset
            if (mSyntheticBoldOffset) {
                cairo_glyph_t *doubleglyph;
                doubleglyph = glyphs.AppendGlyph();
                doubleglyph->index = glyph->index;
                doubleglyph->x =
                  ToDeviceUnits(glyphX + synBoldDevUnitOffsetAppUnits,
                                devUnitsPerAppUnit);
                doubleglyph->y = glyph->y;
            }
            
            glyphs.Flush(cr, aDrawToPath, isRTL);
        } else {
            PRUint32 j;
            PRUint32 glyphCount = glyphData->GetGlyphCount();
            const gfxTextRun::DetailedGlyph *details = aTextRun->GetDetailedGlyphs(i);
            for (j = 0; j < glyphCount; ++j, ++details) {
                double advance = details->mAdvance;
                if (glyphData->IsMissing()) {
                    if (!aDrawToPath) {
                        double glyphX = x;
                        if (isRTL) {
                            glyphX -= advance;
                        }
                        gfxPoint pt(ToDeviceUnits(glyphX, devUnitsPerAppUnit),
                                    ToDeviceUnits(y, devUnitsPerAppUnit));
                        gfxFloat advanceDevUnits = ToDeviceUnits(advance, devUnitsPerAppUnit);
                        gfxFloat height = GetMetrics().maxAscent;
                        gfxRect glyphRect(pt.x, pt.y - height, advanceDevUnits, height);
                        gfxFontMissingGlyphs::DrawMissingGlyph(aContext, glyphRect, details->mGlyphID);
                    }
                } else {
                    glyph = glyphs.AppendGlyph();
                    glyph->index = details->mGlyphID;
                    double glyphX = x + details->mXOffset;
                    if (isRTL) {
                        glyphX -= advance;
                    }
                    glyph->x = ToDeviceUnits(glyphX, devUnitsPerAppUnit);
                    glyph->y = ToDeviceUnits(y + details->mYOffset, devUnitsPerAppUnit);

                    // synthetic bolding by drawing with a one-pixel offset
                    if (mSyntheticBoldOffset) {
                        cairo_glyph_t *doubleglyph;
                        doubleglyph = glyphs.AppendGlyph();
                        doubleglyph->index = glyph->index;
                        doubleglyph->x =
                            ToDeviceUnits(glyphX + synBoldDevUnitOffsetAppUnits,
                                          devUnitsPerAppUnit);
                        doubleglyph->y = glyph->y;
                    }

                    glyphs.Flush(cr, aDrawToPath, isRTL);
                }
                x += direction*advance;
            }
        }

        if (aSpacing) {
            double space = aSpacing[i - aStart].mAfter;
            if (i + 1 < aEnd) {
                space += aSpacing[i + 1 - aStart].mBefore;
            }
            x += direction*space;
        }
    }

    if (gfxFontTestStore::CurrentStore()) {
        /* This assumes that the tests won't have anything that results
         * in more than GLYPH_BUFFER_SIZE glyphs.  Do this before we
         * flush, since that'll blow away the num_glyphs.
         */
        gfxFontTestStore::CurrentStore()->AddItem(GetUniqueName(),
                                                  glyphs.mGlyphBuffer, glyphs.mNumGlyphs);
    }

    // draw any remaining glyphs
    glyphs.Flush(cr, aDrawToPath, isRTL, PR_TRUE);

    *aPt = gfxPoint(x, y);
}

static PRInt32
GetAdvanceForGlyphs(gfxTextRun *aTextRun, PRUint32 aStart, PRUint32 aEnd)
{
    const gfxTextRun::CompressedGlyph *glyphData = aTextRun->GetCharacterGlyphs() + aStart;
    PRInt32 advance = 0;
    PRUint32 i;
    for (i = aStart; i < aEnd; ++i, ++glyphData) {
        if (glyphData->IsSimpleGlyph()) {
            advance += glyphData->GetSimpleAdvance();   
        } else {
            PRUint32 glyphCount = glyphData->GetGlyphCount();
            const gfxTextRun::DetailedGlyph *details = aTextRun->GetDetailedGlyphs(i);
            PRUint32 j;
            for (j = 0; j < glyphCount; ++j, ++details) {
                advance += details->mAdvance;
            }
        }
    }
    return advance;
}

static void
UnionRange(gfxFloat aX, gfxFloat* aDestMin, gfxFloat* aDestMax)
{
    *aDestMin = PR_MIN(*aDestMin, aX);
    *aDestMax = PR_MAX(*aDestMax, aX);
}

// We get precise glyph extents if the textrun creator requested them, or
// if the font is a user font --- in which case the author may be relying
// on overflowing glyphs.
static PRBool
NeedsGlyphExtents(gfxFont *aFont, gfxTextRun *aTextRun)
{
    return (aTextRun->GetFlags() & gfxTextRunFactory::TEXT_NEED_BOUNDING_BOX) ||
        aFont->GetFontEntry()->IsUserFont();
}

static PRBool
NeedsGlyphExtents(gfxTextRun *aTextRun)
{
    if (aTextRun->GetFlags() & gfxTextRunFactory::TEXT_NEED_BOUNDING_BOX)
        return PR_TRUE;
    PRUint32 numRuns;
    const gfxTextRun::GlyphRun *glyphRuns = aTextRun->GetGlyphRuns(&numRuns);
    for (PRUint32 i = 0; i < numRuns; ++i) {
        if (glyphRuns[i].mFont->GetFontEntry()->IsUserFont())
            return PR_TRUE;
    }
    return PR_FALSE;
}

gfxFont::RunMetrics
gfxFont::Measure(gfxTextRun *aTextRun,
                 PRUint32 aStart, PRUint32 aEnd,
                 BoundingBoxType aBoundingBoxType, gfxContext *aRefContext,
                 Spacing *aSpacing)
{
    const PRUint32 appUnitsPerDevUnit = aTextRun->GetAppUnitsPerDevUnit();
    // Current position in appunits
    const gfxFont::Metrics& fontMetrics = GetMetrics();

    RunMetrics metrics;
    metrics.mAscent = fontMetrics.maxAscent*appUnitsPerDevUnit;
    metrics.mDescent = fontMetrics.maxDescent*appUnitsPerDevUnit;
    if (aStart == aEnd) {
        // exit now before we look at aSpacing[0], which is undefined
        metrics.mBoundingBox = gfxRect(0, -metrics.mAscent, 0, metrics.mAscent + metrics.mDescent);
        return metrics;
    }

    gfxFloat advanceMin = 0, advanceMax = 0;
    const gfxTextRun::CompressedGlyph *charGlyphs = aTextRun->GetCharacterGlyphs();
    PRBool isRTL = aTextRun->IsRightToLeft();
    double direction = aTextRun->GetDirection();
    PRBool needsGlyphExtents = NeedsGlyphExtents(this, aTextRun);
    gfxGlyphExtents *extents =
        (aBoundingBoxType == LOOSE_INK_EXTENTS &&
            !needsGlyphExtents &&
            !aTextRun->HasDetailedGlyphs()) ? nsnull
        : GetOrCreateGlyphExtents(aTextRun->GetAppUnitsPerDevUnit());
    double x = 0;
    if (aSpacing) {
        x += direction*aSpacing[0].mBefore;
    }
    PRUint32 i;
    for (i = aStart; i < aEnd; ++i) {
        const gfxTextRun::CompressedGlyph *glyphData = &charGlyphs[i];
        if (glyphData->IsSimpleGlyph()) {
            double advance = glyphData->GetSimpleAdvance();
            // Only get the real glyph horizontal extent if we were asked
            // for the tight bounding box or we're in quality mode
            if ((aBoundingBoxType != LOOSE_INK_EXTENTS || needsGlyphExtents) &&
                extents) {
                PRUint32 glyphIndex = glyphData->GetSimpleGlyph();
                PRUint16 extentsWidth = extents->GetContainedGlyphWidthAppUnits(glyphIndex);
                if (extentsWidth != gfxGlyphExtents::INVALID_WIDTH &&
                    aBoundingBoxType == LOOSE_INK_EXTENTS) {
                    UnionRange(x, &advanceMin, &advanceMax);
                    UnionRange(x + direction*extentsWidth, &advanceMin, &advanceMax);
                } else {
                    gfxRect glyphRect;
                    if (!extents->GetTightGlyphExtentsAppUnits(this,
                            aRefContext, glyphIndex, &glyphRect)) {
                        glyphRect = gfxRect(0, metrics.mBoundingBox.Y(),
                            advance, metrics.mBoundingBox.Height());
                    }
                    if (isRTL) {
                        glyphRect.pos.x -= advance;
                    }
                    glyphRect.pos.x += x;
                    metrics.mBoundingBox = metrics.mBoundingBox.Union(glyphRect);
                }
            }
            x += direction*advance;
        } else {
            PRUint32 glyphCount = glyphData->GetGlyphCount();
            const gfxTextRun::DetailedGlyph *details = aTextRun->GetDetailedGlyphs(i);
            PRUint32 j;
            for (j = 0; j < glyphCount; ++j, ++details) {
                PRUint32 glyphIndex = details->mGlyphID;
                gfxPoint glyphPt(x + details->mXOffset, details->mYOffset);
                double advance = details->mAdvance;
                gfxRect glyphRect;
                if (glyphData->IsMissing() || !extents ||
                    !extents->GetTightGlyphExtentsAppUnits(this,
                            aRefContext, glyphIndex, &glyphRect)) {
                    // We might have failed to get glyph extents due to
                    // OOM or something
                    glyphRect = gfxRect(0, -metrics.mAscent,
                        advance, metrics.mAscent + metrics.mDescent);
                }
                if (isRTL) {
                    glyphRect.pos.x -= advance;
                }
                glyphRect.pos.x += x;
                metrics.mBoundingBox = metrics.mBoundingBox.Union(glyphRect);
                x += direction*advance;
            }
        }
        // Every other glyph type is ignored
        if (aSpacing) {
            double space = aSpacing[i - aStart].mAfter;
            if (i + 1 < aEnd) {
                space += aSpacing[i + 1 - aStart].mBefore;
            }
            x += direction*space;
        }
    }

    if (aBoundingBoxType == LOOSE_INK_EXTENTS) {
        UnionRange(x, &advanceMin, &advanceMax);
        gfxRect fontBox(advanceMin, -metrics.mAscent,
                        advanceMax - advanceMin, metrics.mAscent + metrics.mDescent);
        metrics.mBoundingBox = metrics.mBoundingBox.Union(fontBox);
    }
    if (isRTL) {
        metrics.mBoundingBox.pos.x -= x;
    }

    metrics.mAdvanceWidth = x*direction;
    return metrics;
}

gfxGlyphExtents *
gfxFont::GetOrCreateGlyphExtents(PRUint32 aAppUnitsPerDevUnit) {
    PRUint32 i;
    for (i = 0; i < mGlyphExtentsArray.Length(); ++i) {
        if (mGlyphExtentsArray[i]->GetAppUnitsPerDevUnit() == aAppUnitsPerDevUnit)
            return mGlyphExtentsArray[i];
    }
    gfxGlyphExtents *glyphExtents = new gfxGlyphExtents(aAppUnitsPerDevUnit);
    if (glyphExtents) {
        mGlyphExtentsArray.AppendElement(glyphExtents);
        // Initialize the extents of a space glyph, assuming that spaces don't
        // render anything!
        glyphExtents->SetContainedGlyphWidthAppUnits(GetSpaceGlyph(), 0);
    }
    return glyphExtents;
}

void
gfxFont::SetupGlyphExtents(gfxContext *aContext, PRUint32 aGlyphID, PRBool aNeedTight,
                           gfxGlyphExtents *aExtents)
{
    gfxMatrix matrix = aContext->CurrentMatrix();
    aContext->IdentityMatrix();
    cairo_glyph_t glyph;
    glyph.index = aGlyphID;
    glyph.x = 0;
    glyph.y = 0;
    cairo_text_extents_t extents;
    cairo_glyph_extents(aContext->GetCairo(), &glyph, 1, &extents);
    aContext->SetMatrix(matrix);

    const Metrics& fontMetrics = GetMetrics();
    PRUint32 appUnitsPerDevUnit = aExtents->GetAppUnitsPerDevUnit();
    if (!aNeedTight && extents.x_bearing >= 0 &&
        extents.y_bearing >= -fontMetrics.maxAscent &&
        extents.height + extents.y_bearing <= fontMetrics.maxDescent) {
        PRUint32 appUnitsWidth =
            PRUint32(NS_ceil((extents.x_bearing + extents.width)*appUnitsPerDevUnit));
        if (appUnitsWidth < gfxGlyphExtents::INVALID_WIDTH) {
            aExtents->SetContainedGlyphWidthAppUnits(aGlyphID, PRUint16(appUnitsWidth));
            return;
        }
    }
#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
    if (!aNeedTight) {
        ++gGlyphExtentsSetupFallBackToTight;
    }
#endif

    double d2a = appUnitsPerDevUnit;
    gfxRect bounds(extents.x_bearing*d2a, extents.y_bearing*d2a,
                   extents.width*d2a, extents.height*d2a);
    aExtents->SetTightGlyphExtents(aGlyphID, bounds);
}

void
gfxFont::SanitizeMetrics(gfxFont::Metrics *aMetrics, PRBool aIsBadUnderlineFont)
{
    // Even if this font size is zero, this font is created with non-zero size.
    // However, for layout and others, we should return the metrics of zero size font.
    if (mStyle.size == 0) {
        memset(aMetrics, 0, sizeof(gfxFont::Metrics));
        return;
    }

    // MS (P)Gothic and MS (P)Mincho are not having suitable values in their super script offset.
    // If the values are not suitable, we should use x-height instead of them.
    // See https://bugzilla.mozilla.org/show_bug.cgi?id=353632
    if (aMetrics->superscriptOffset == 0 ||
        aMetrics->superscriptOffset >= aMetrics->maxAscent) {
        aMetrics->superscriptOffset = aMetrics->xHeight;
    }
    // And also checking the case of sub script offset. The old gfx for win has checked this too.
    if (aMetrics->subscriptOffset == 0 ||
        aMetrics->subscriptOffset >= aMetrics->maxAscent) {
        aMetrics->subscriptOffset = aMetrics->xHeight;
    }

    aMetrics->underlineSize = PR_MAX(1.0, aMetrics->underlineSize);
    aMetrics->strikeoutSize = PR_MAX(1.0, aMetrics->strikeoutSize);

    aMetrics->underlineOffset = PR_MIN(aMetrics->underlineOffset, -1.0);

    if (aMetrics->maxAscent < 1.0) {
        // We cannot draw strikeout line and overline in the ascent...
        aMetrics->underlineSize = 0;
        aMetrics->underlineOffset = 0;
        aMetrics->strikeoutSize = 0;
        aMetrics->strikeoutOffset = 0;
        return;
    }

    /**
     * Some CJK fonts have bad underline offset. Therefore, if this is such font,
     * we need to lower the underline offset to bottom of *em* descent.
     * However, if this is system font, we should not do this for the rendering compatibility with
     * another application's UI on the platform.
     * XXX Should not use this hack if the font size is too small?
     *     Such text cannot be read, this might be used for tight CSS rendering? (E.g., Acid2)
     */
    if (!mStyle.systemFont && aIsBadUnderlineFont) {
        // First, we need 2 pixels between baseline and underline at least. Because many CJK characters
        // put their glyphs on the baseline, so, 1 pixel is too close for CJK characters.
        aMetrics->underlineOffset = PR_MIN(aMetrics->underlineOffset, -2.0);

        // Next, we put the underline to bottom of below of the descent space.
        if (aMetrics->internalLeading + aMetrics->externalLeading > aMetrics->underlineSize) {
            aMetrics->underlineOffset = PR_MIN(aMetrics->underlineOffset, -aMetrics->emDescent);
        } else {
            aMetrics->underlineOffset = PR_MIN(aMetrics->underlineOffset,
                                               aMetrics->underlineSize - aMetrics->emDescent);
        }
    }
    // If underline positioned is too far from the text, descent position is preferred so that underline
    // will stay within the boundary.
    else if (aMetrics->underlineSize - aMetrics->underlineOffset > aMetrics->maxDescent) {
        if (aMetrics->underlineSize > aMetrics->maxDescent)
            aMetrics->underlineSize = PR_MAX(aMetrics->maxDescent, 1.0);
        // The max underlineOffset is 1px (the min underlineSize is 1px, and min maxDescent is 0px.)
        aMetrics->underlineOffset = aMetrics->underlineSize - aMetrics->maxDescent;
    }

    // If strikeout line is overflowed from the ascent, the line should be resized and moved for
    // that being in the ascent space.
    // Note that the strikeoutOffset is *middle* of the strikeout line position.
    gfxFloat halfOfStrikeoutSize = NS_floor(aMetrics->strikeoutSize / 2.0 + 0.5);
    if (halfOfStrikeoutSize + aMetrics->strikeoutOffset > aMetrics->maxAscent) {
        if (aMetrics->strikeoutSize > aMetrics->maxAscent) {
            aMetrics->strikeoutSize = PR_MAX(aMetrics->maxAscent, 1.0);
            halfOfStrikeoutSize = NS_floor(aMetrics->strikeoutSize / 2.0 + 0.5);
        }
        gfxFloat ascent = NS_floor(aMetrics->maxAscent + 0.5);
        aMetrics->strikeoutOffset = PR_MAX(halfOfStrikeoutSize, ascent / 2.0);
    }

    // If overline is larger than the ascent, the line should be resized.
    if (aMetrics->underlineSize > aMetrics->maxAscent) {
        aMetrics->underlineSize = aMetrics->maxAscent;
    }
}



gfxGlyphExtents::~gfxGlyphExtents()
{
#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
    gGlyphExtentsWidthsTotalSize += mContainedGlyphWidths.ComputeSize();
    gGlyphExtentsCount++;
#endif
    MOZ_COUNT_DTOR(gfxGlyphExtents);
}

PRBool
gfxGlyphExtents::GetTightGlyphExtentsAppUnits(gfxFont *aFont,
    gfxContext *aContext, PRUint32 aGlyphID, gfxRect *aExtents)
{
    HashEntry *entry = mTightGlyphExtents.GetEntry(aGlyphID);
    if (!entry) {
        if (!aContext) {
            NS_WARNING("Could not get glyph extents (no aContext)");
            return PR_FALSE;
        }

        aFont->SetupCairoFont(aContext);
#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
        ++gGlyphExtentsSetupLazyTight;
#endif
        aFont->SetupGlyphExtents(aContext, aGlyphID, PR_TRUE, this);
        entry = mTightGlyphExtents.GetEntry(aGlyphID);
        if (!entry) {
            NS_WARNING("Could not get glyph extents");
            return PR_FALSE;
        }
    }

    *aExtents = gfxRect(entry->x, entry->y, entry->width, entry->height);
    return PR_TRUE;
}

gfxGlyphExtents::GlyphWidths::~GlyphWidths()
{
    PRUint32 i;
    for (i = 0; i < mBlocks.Length(); ++i) {
        PtrBits bits = mBlocks[i];
        if (bits && !(bits & 0x1)) {
            delete[] reinterpret_cast<PRUint16 *>(bits);
        }
    }
}

#ifdef DEBUG
PRUint32
gfxGlyphExtents::GlyphWidths::ComputeSize()
{
    PRUint32 i;
    PRUint32 size = mBlocks.Capacity()*sizeof(PtrBits);
    for (i = 0; i < mBlocks.Length(); ++i) {
        PtrBits bits = mBlocks[i];
        if (bits && !(bits & 0x1)) {
            size += BLOCK_SIZE*sizeof(PRUint16);
        }
    }
    return size;
}
#endif

void
gfxGlyphExtents::GlyphWidths::Set(PRUint32 aGlyphID, PRUint16 aWidth)
{
    PRUint32 block = aGlyphID >> BLOCK_SIZE_BITS;
    PRUint32 len = mBlocks.Length();
    if (block >= len) {
        PtrBits *elems = mBlocks.AppendElements(block + 1 - len);
        if (!elems)
            return;
        memset(elems, 0, sizeof(PtrBits)*(block + 1 - len));
    }

    PtrBits bits = mBlocks[block];
    PRUint32 glyphOffset = aGlyphID & (BLOCK_SIZE - 1);
    if (!bits) {
        mBlocks[block] = MakeSingle(glyphOffset, aWidth);
        return;
    }

    PRUint16 *newBlock;
    if (bits & 0x1) {
        // Expand the block to a real block. We could avoid this by checking
        // glyphOffset == GetGlyphOffset(bits), but that never happens so don't bother
        newBlock = new PRUint16[BLOCK_SIZE];
        if (!newBlock)
            return;
        PRUint32 i;
        for (i = 0; i < BLOCK_SIZE; ++i) {
            newBlock[i] = INVALID_WIDTH;
        }
        newBlock[GetGlyphOffset(bits)] = GetWidth(bits);
        mBlocks[block] = reinterpret_cast<PtrBits>(newBlock);
    } else {
        newBlock = reinterpret_cast<PRUint16 *>(bits);
    }
    newBlock[glyphOffset] = aWidth;
}

void
gfxGlyphExtents::SetTightGlyphExtents(PRUint32 aGlyphID, const gfxRect& aExtentsAppUnits)
{
    HashEntry *entry = mTightGlyphExtents.PutEntry(aGlyphID);
    if (!entry)
        return;
    entry->x = aExtentsAppUnits.pos.x;
    entry->y = aExtentsAppUnits.pos.y;
    entry->width = aExtentsAppUnits.size.width;
    entry->height = aExtentsAppUnits.size.height;
}

gfxFontGroup::gfxFontGroup(const nsAString& aFamilies, const gfxFontStyle *aStyle, gfxUserFontSet *aUserFontSet)
    : mFamilies(aFamilies), mStyle(*aStyle), mUnderlineOffset(UNDERLINE_OFFSET_NOT_SET)
{
    mUserFontSet = nsnull;
    SetUserFontSet(aUserFontSet);
}

gfxFontGroup::~gfxFontGroup() {
    mFonts.Clear();
    SetUserFontSet(nsnull);
}


PRBool 
gfxFontGroup::IsInvalidChar(PRUnichar ch) {
    if (ch >= 32) {
        return ch == 0x0085/*NEL*/ ||
            ((ch & 0xFF00) == 0x2000 /* Unicode control character */ &&
             (ch == 0x200B/*ZWSP*/ || ch == 0x2028/*LSEP*/ || ch == 0x2029/*PSEP*/ ||
              IS_BIDI_CONTROL_CHAR(ch)));
    }
    // We could just blacklist all control characters, but it seems better
    // to only blacklist the ones we know cause problems for native font
    // engines.
    return ch == 0x0B || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' ||
        (ch >= 0x1c && ch <= 0x1f);
}

PRBool
gfxFontGroup::ForEachFont(FontCreationCallback fc,
                          void *closure)
{
    return ForEachFontInternal(mFamilies, mStyle.langGroup,
                               PR_TRUE, PR_TRUE, fc, closure);
}

PRBool
gfxFontGroup::ForEachFont(const nsAString& aFamilies,
                          const nsACString& aLangGroup,
                          FontCreationCallback fc,
                          void *closure)
{
    return ForEachFontInternal(aFamilies, aLangGroup,
                               PR_FALSE, PR_TRUE, fc, closure);
}

struct ResolveData {
    ResolveData(gfxFontGroup::FontCreationCallback aCallback,
                nsACString& aGenericFamily,
                void *aClosure) :
        mCallback(aCallback),
        mGenericFamily(aGenericFamily),
        mClosure(aClosure) {
    }
    gfxFontGroup::FontCreationCallback mCallback;
    nsCString mGenericFamily;
    void *mClosure;
};

PRBool
gfxFontGroup::ForEachFontInternal(const nsAString& aFamilies,
                                  const nsACString& aLangGroup,
                                  PRBool aResolveGeneric,
                                  PRBool aResolveFontName,
                                  FontCreationCallback fc,
                                  void *closure)
{
    const PRUnichar kSingleQuote  = PRUnichar('\'');
    const PRUnichar kDoubleQuote  = PRUnichar('\"');
    const PRUnichar kComma        = PRUnichar(',');

    nsCOMPtr<nsIPrefBranch> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID);

    nsPromiseFlatString families(aFamilies);
    const PRUnichar *p, *p_end;
    families.BeginReading(p);
    families.EndReading(p_end);
    nsAutoString family;
    nsCAutoString lcFamily;
    nsAutoString genericFamily;
    nsXPIDLCString value;
    nsCAutoString lang(aLangGroup);
    if (lang.IsEmpty())
        lang.Assign("x-unicode"); // XXX or should use "x-user-def"?

    while (p < p_end) {
        while (nsCRT::IsAsciiSpace(*p))
            if (++p == p_end)
                return PR_TRUE;

        PRBool generic;
        if (*p == kSingleQuote || *p == kDoubleQuote) {
            // quoted font family
            PRUnichar quoteMark = *p;
            if (++p == p_end)
                return PR_TRUE;
            const PRUnichar *nameStart = p;

            // XXX What about CSS character escapes?
            while (*p != quoteMark)
                if (++p == p_end)
                    return PR_TRUE;

            family = Substring(nameStart, p);
            generic = PR_FALSE;
            genericFamily.SetIsVoid(PR_TRUE);

            while (++p != p_end && *p != kComma)
                /* nothing */ ;

        } else {
            // unquoted font family
            const PRUnichar *nameStart = p;
            while (++p != p_end && *p != kComma)
                /* nothing */ ;

            family = Substring(nameStart, p);
            family.CompressWhitespace(PR_FALSE, PR_TRUE);

            if (aResolveGeneric &&
                (family.LowerCaseEqualsLiteral("serif") ||
                 family.LowerCaseEqualsLiteral("sans-serif") ||
                 family.LowerCaseEqualsLiteral("monospace") ||
                 family.LowerCaseEqualsLiteral("cursive") ||
                 family.LowerCaseEqualsLiteral("fantasy")))
            {
                generic = PR_TRUE;

                ToLowerCase(NS_LossyConvertUTF16toASCII(family), lcFamily);

                nsCAutoString prefName("font.name.");
                prefName.Append(lcFamily);
                prefName.AppendLiteral(".");
                prefName.Append(lang);

                // prefs file always uses (must use) UTF-8 so that we can use
                // |GetCharPref| and treat the result as a UTF-8 string.
                nsresult rv = prefs->GetCharPref(prefName.get(), getter_Copies(value));
                if (NS_SUCCEEDED(rv)) {
                    CopyASCIItoUTF16(lcFamily, genericFamily);
                    CopyUTF8toUTF16(value, family);
                }
            } else {
                generic = PR_FALSE;
                genericFamily.SetIsVoid(PR_TRUE);
            }
        }

        if (generic) {
            ForEachFontInternal(family, lang, PR_FALSE, aResolveFontName, fc, closure);
        } else if (!family.IsEmpty()) {
            NS_LossyConvertUTF16toASCII gf(genericFamily);
            if (aResolveFontName) {
                ResolveData data(fc, gf, closure);
                PRBool aborted = PR_FALSE, needsBold;
                nsresult rv;

                if (mUserFontSet && mUserFontSet->FindFontEntry(family, mStyle, needsBold)) {
                    gfxFontGroup::FontResolverProc(family, &data);
                    rv = NS_OK;
                } else {
                    gfxPlatform *pf = gfxPlatform::GetPlatform();
                    rv = pf->ResolveFontName(family,
                                                  gfxFontGroup::FontResolverProc,
                                                  &data, aborted);
                }
                if (NS_FAILED(rv) || aborted)
                    return PR_FALSE;
            }
            else {
                if (!fc(family, gf, closure))
                    return PR_FALSE;
            }
        }

        if (generic && aResolveGeneric) {
            nsCAutoString prefName("font.name-list.");
            prefName.Append(lcFamily);
            prefName.AppendLiteral(".");
            prefName.Append(aLangGroup);
            nsresult rv = prefs->GetCharPref(prefName.get(), getter_Copies(value));
            if (NS_SUCCEEDED(rv)) {
                ForEachFontInternal(NS_ConvertUTF8toUTF16(value),
                                    lang, PR_FALSE, aResolveFontName,
                                    fc, closure);
            }
        }

        ++p; // may advance past p_end
    }

    return PR_TRUE;
}

PRBool
gfxFontGroup::FontResolverProc(const nsAString& aName, void *aClosure)
{
    ResolveData *data = reinterpret_cast<ResolveData*>(aClosure);
    return (data->mCallback)(aName, data->mGenericFamily, data->mClosure);
}

gfxTextRun *
gfxFontGroup::MakeEmptyTextRun(const Parameters *aParams, PRUint32 aFlags)
{
    aFlags |= TEXT_IS_8BIT | TEXT_IS_ASCII | TEXT_IS_PERSISTENT;
    return gfxTextRun::Create(aParams, nsnull, 0, this, aFlags);
}

gfxTextRun *
gfxFontGroup::MakeSpaceTextRun(const Parameters *aParams, PRUint32 aFlags)
{
    aFlags |= TEXT_IS_8BIT | TEXT_IS_ASCII | TEXT_IS_PERSISTENT;
    static const PRUint8 space = ' ';

    nsAutoPtr<gfxTextRun> textRun;
    textRun = gfxTextRun::Create(aParams, &space, 1, this, aFlags);
    if (!textRun)
        return nsnull;

    gfxFont *font = GetFontAt(0);
    if (NS_UNLIKELY(GetStyle()->size == 0)) {
        // Short-circuit for size-0 fonts, as Windows and ATSUI can't handle
        // them, and always create at least size 1 fonts, i.e. they still
        // render something for size 0 fonts.
        textRun->AddGlyphRun(font, 0);
    }
    else {
        textRun->SetSpaceGlyph(font, aParams->mContext, 0);
    }
    // Note that the gfxGlyphExtents glyph bounds storage for the font will
    // always contain an entry for the font's space glyph, so we don't have
    // to call FetchGlyphExtents here.
    return textRun.forget();
}



already_AddRefed<gfxFont>
gfxFontGroup::FindFontForChar(PRUint32 aCh, PRUint32 aPrevCh, PRUint32 aNextCh, gfxFont *aPrevMatchedFont)
{
    nsRefPtr<gfxFont>    selectedFont;

    // if this character or the previous one is a join-causer,
    // use the same font as the previous range if we can
    if (gfxFontUtils::IsJoinCauser(aCh) || gfxFontUtils::IsJoinCauser(aPrevCh)) {
        if (aPrevMatchedFont && aPrevMatchedFont->HasCharacter(aCh)) {
            selectedFont = aPrevMatchedFont;
            return selectedFont.forget();
        }
    }

    // 1. check fonts in the font group
    for (PRUint32 i = 0; i < FontListLength(); i++) {
        nsRefPtr<gfxFont> font = GetFontAt(i);
        if (font->HasCharacter(aCh))
            return font.forget();
    }

    // if character is in Private Use Area, don't do matching against pref or system fonts
    if ((aCh >= 0xE000  && aCh <= 0xF8FF) || (aCh >= 0xF0000 && aCh <= 0x10FFFD))
        return nsnull;

    // 2. search pref fonts
    if ((selectedFont = WhichPrefFontSupportsChar(aCh))) {
        return selectedFont.forget();
    }

    // 3. use fallback fonts
    // -- before searching for something else check the font used for the previous character
    if (!selectedFont && aPrevMatchedFont && aPrevMatchedFont->HasCharacter(aCh)) {
        selectedFont = aPrevMatchedFont;
        return selectedFont.forget();
    }

    // -- otherwise look for other stuff
    if (!selectedFont) {
        selectedFont = WhichSystemFontSupportsChar(aCh);
        return selectedFont.forget();
    }

    return nsnull;
}


void gfxFontGroup::ComputeRanges(nsTArray<gfxTextRange>& aRanges, const PRUnichar *aString, PRUint32 begin, PRUint32 end)
{
    const PRUnichar *str = aString + begin;
    PRUint32 len = end - begin;

    aRanges.Clear();

    if (len == 0) {
        return;
    }

    PRUint32 prevCh = 0;
    for (PRUint32 i = 0; i < len; i++) {

        const PRUint32 origI = i; // save off in case we increase for surrogate

        // set up current ch
        PRUint32 ch = str[i];
        if ((i+1 < len) && NS_IS_HIGH_SURROGATE(ch) && NS_IS_LOW_SURROGATE(str[i+1])) {
            i++;
            ch = SURROGATE_TO_UCS4(ch, str[i]);
        }

        // set up next ch
        PRUint32 nextCh = 0;
        if (i+1 < len) {
            nextCh = str[i+1];
            if ((i+2 < len) && NS_IS_HIGH_SURROGATE(nextCh) && NS_IS_LOW_SURROGATE(str[i+2]))
                nextCh = SURROGATE_TO_UCS4(nextCh, str[i+2]);
        }
        
        // find the font for this char
        nsRefPtr<gfxFont> font = FindFontForChar(ch, prevCh, nextCh, (aRanges.Length() == 0) ? nsnull : aRanges[aRanges.Length() - 1].font.get());

        prevCh = ch;

        if (aRanges.Length() == 0) {
            // first char ==> make a new range
            gfxTextRange r(0,1);
            r.font = font;
            aRanges.AppendElement(r);
        } else {
            // if font has changed, make a new range
            gfxTextRange& prevRange = aRanges[aRanges.Length() - 1];
            if (prevRange.font != font) {
                // close out the previous range
                prevRange.end = origI;

                gfxTextRange r(origI, i+1);
                r.font = font;
                aRanges.AppendElement(r);
            }
        }
    }
    aRanges[aRanges.Length()-1].end = len;
}

gfxUserFontSet* 
gfxFontGroup::GetUserFontSet()
{
    return mUserFontSet;
}

void 
gfxFontGroup::SetUserFontSet(gfxUserFontSet *aUserFontSet)
{
    NS_IF_RELEASE(mUserFontSet);
    mUserFontSet = aUserFontSet;
    NS_IF_ADDREF(mUserFontSet);
    mCurrGeneration = GetGeneration();
}

PRUint64
gfxFontGroup::GetGeneration()
{
    if (!mUserFontSet)
        return 0;
    return mUserFontSet->GetGeneration();
}


#define DEFAULT_PIXEL_FONT_SIZE 16.0f

gfxFontStyle::gfxFontStyle() :
    style(FONT_STYLE_NORMAL), systemFont(PR_TRUE), printerFont(PR_FALSE), 
    familyNameQuirks(PR_FALSE), weight(FONT_WEIGHT_NORMAL),
    stretch(NS_FONT_STRETCH_NORMAL), size(DEFAULT_PIXEL_FONT_SIZE),
    langGroup(NS_LITERAL_CSTRING("x-western")), sizeAdjust(0.0f)
{
}

gfxFontStyle::gfxFontStyle(PRUint8 aStyle, PRUint16 aWeight, PRInt16 aStretch,
                           gfxFloat aSize, const nsACString& aLangGroup,
                           float aSizeAdjust, PRPackedBool aSystemFont,
                           PRPackedBool aFamilyNameQuirks,
                           PRPackedBool aPrinterFont):
    style(aStyle), systemFont(aSystemFont), printerFont(aPrinterFont),
    familyNameQuirks(aFamilyNameQuirks), weight(aWeight), stretch(aStretch),
    size(aSize), langGroup(aLangGroup), sizeAdjust(aSizeAdjust)
{
    if (weight > 900)
        weight = 900;
    if (weight < 100)
        weight = 100;

    if (size >= FONT_MAX_SIZE) {
        size = FONT_MAX_SIZE;
        sizeAdjust = 0.0;
    } else if (size < 0.0) {
        NS_WARNING("negative font size");
        size = 0.0;
    }

    if (langGroup.IsEmpty()) {
        NS_WARNING("empty langgroup");
        langGroup.Assign("x-western");
    }
}

gfxFontStyle::gfxFontStyle(const gfxFontStyle& aStyle) :
    style(aStyle.style), systemFont(aStyle.systemFont), printerFont(aStyle.printerFont),
    familyNameQuirks(aStyle.familyNameQuirks), weight(aStyle.weight),
    stretch(aStyle.stretch), size(aStyle.size), langGroup(aStyle.langGroup),
    sizeAdjust(aStyle.sizeAdjust)
{
}

void
gfxFontStyle::ComputeWeightAndOffset(PRInt8 *outBaseWeight, PRInt8 *outOffset) const
{
    PRInt8 baseWeight = (weight + 50) / 100;
    PRInt8 offset = weight - baseWeight * 100;

    if (baseWeight < 0)
        baseWeight = 0;
    if (baseWeight > 9)
        baseWeight = 9;

    if (outBaseWeight)
        *outBaseWeight = baseWeight;
    if (outOffset)
        *outOffset = offset;
}

PRBool
gfxTextRun::GlyphRunIterator::NextRun()  {
    if (mNextIndex >= mTextRun->mGlyphRuns.Length())
        return PR_FALSE;
    mGlyphRun = &mTextRun->mGlyphRuns[mNextIndex];
    if (mGlyphRun->mCharacterOffset >= mEndOffset)
        return PR_FALSE;

    mStringStart = PR_MAX(mStartOffset, mGlyphRun->mCharacterOffset);
    PRUint32 last = mNextIndex + 1 < mTextRun->mGlyphRuns.Length()
        ? mTextRun->mGlyphRuns[mNextIndex + 1].mCharacterOffset : mTextRun->mCharacterCount;
    mStringEnd = PR_MIN(mEndOffset, last);

    ++mNextIndex;
    return PR_TRUE;
}

#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
static void
AccountStorageForTextRun(gfxTextRun *aTextRun, PRInt32 aSign)
{
    // Ignores detailed glyphs... we don't know when those have been constructed
    // Also ignores gfxSkipChars dynamic storage (which won't be anything
    // for preformatted text)
    // Also ignores GlyphRun array, again because it hasn't been constructed
    // by the time this gets called. If there's only one glyphrun that's stored
    // directly in the textrun anyway so no additional overhead.
    PRInt32 bytesPerChar = sizeof(gfxTextRun::CompressedGlyph);
    if (aTextRun->GetFlags() & gfxTextRunFactory::TEXT_IS_PERSISTENT) {
      bytesPerChar += (aTextRun->GetFlags() & gfxTextRunFactory::TEXT_IS_8BIT) ? 1 : 2;
    }
    PRInt32 bytes = sizeof(gfxTextRun) + aTextRun->GetLength()*bytesPerChar;
    gTextRunStorage += bytes*aSign;
    gTextRunStorageHighWaterMark = PR_MAX(gTextRunStorageHighWaterMark, gTextRunStorage);
}
#endif

gfxTextRun *
gfxTextRun::Create(const gfxTextRunFactory::Parameters *aParams, const void *aText,
                   PRUint32 aLength, gfxFontGroup *aFontGroup, PRUint32 aFlags)
{
    return new (aLength, aFlags)
        gfxTextRun(aParams, aText, aLength, aFontGroup, aFlags, sizeof(gfxTextRun));
}

void *
gfxTextRun::operator new(size_t aSize, PRUint32 aLength, PRUint32 aFlags)
{
    NS_ASSERTION(aSize % sizeof(CompressedGlyph) == 0, "Alignment broken!");
    aSize += sizeof(CompressedGlyph)*aLength;
    if (!(aFlags & gfxTextRunFactory::TEXT_IS_PERSISTENT)) {
        NS_ASSERTION(aSize % 2 == 0, "Alignment broken!");
        aSize += ((aFlags & gfxTextRunFactory::TEXT_IS_8BIT) ? 1 : 2)*aLength;
    }

    return new PRUint8[aSize];
}

void gfxTextRun::operator delete(void *p)
{
    delete[] static_cast<PRUint8*>(p);
}

gfxTextRun::gfxTextRun(const gfxTextRunFactory::Parameters *aParams, const void *aText,
                       PRUint32 aLength, gfxFontGroup *aFontGroup, PRUint32 aFlags,
                       PRUint32 aObjectSize)
  : mUserData(aParams->mUserData),
    mFontGroup(aFontGroup),
    mAppUnitsPerDevUnit(aParams->mAppUnitsPerDevUnit),
    mFlags(aFlags), mCharacterCount(aLength), mHashCode(0)
{
    NS_ASSERTION(mAppUnitsPerDevUnit != 0, "Invalid app unit scale");
    MOZ_COUNT_CTOR(gfxTextRun);
    NS_ADDREF(mFontGroup);
    if (aParams->mSkipChars) {
        mSkipChars.TakeFrom(aParams->mSkipChars);
    }

    mCharacterGlyphs = reinterpret_cast<CompressedGlyph*>
        (reinterpret_cast<PRUint8*>(this) + aObjectSize);
    memset(mCharacterGlyphs, 0, sizeof(CompressedGlyph)*aLength);

    if (mFlags & gfxTextRunFactory::TEXT_IS_8BIT) {
        mText.mSingle = static_cast<const PRUint8 *>(aText);
        if (!(mFlags & gfxTextRunFactory::TEXT_IS_PERSISTENT)) {
            PRUint8 *newText = reinterpret_cast<PRUint8*>(mCharacterGlyphs + aLength);
            memcpy(newText, aText, aLength);
            mText.mSingle = newText;    
        }
    } else {
        mText.mDouble = static_cast<const PRUnichar *>(aText);
        if (!(mFlags & gfxTextRunFactory::TEXT_IS_PERSISTENT)) {
            PRUnichar *newText = reinterpret_cast<PRUnichar*>(mCharacterGlyphs + aLength);
            memcpy(newText, aText, aLength*sizeof(PRUnichar));
            mText.mDouble = newText;    
        }
    }
#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
    AccountStorageForTextRun(this, 1);
#endif

    mUserFontSetGeneration = mFontGroup->GetGeneration();
}

gfxTextRun::~gfxTextRun()
{
#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
    AccountStorageForTextRun(this, -1);
#endif
#ifdef DEBUG
    // Make it easy to detect a dead text run
    mFlags = 0xFFFFFFFF;
#endif
    NS_RELEASE(mFontGroup);
    MOZ_COUNT_DTOR(gfxTextRun);
}

gfxTextRun *
gfxTextRun::Clone(const gfxTextRunFactory::Parameters *aParams, const void *aText,
                  PRUint32 aLength, gfxFontGroup *aFontGroup, PRUint32 aFlags)
{
    if (!mCharacterGlyphs)
        return nsnull;

    nsAutoPtr<gfxTextRun> textRun;
    textRun = gfxTextRun::Create(aParams, aText, aLength, aFontGroup, aFlags);
    if (!textRun)
        return nsnull;

    textRun->CopyGlyphDataFrom(this, 0, mCharacterCount, 0, PR_FALSE);
    return textRun.forget();
}

PRBool
gfxTextRun::SetPotentialLineBreaks(PRUint32 aStart, PRUint32 aLength,
                                   PRPackedBool *aBreakBefore,
                                   gfxContext *aRefContext)
{
    NS_ASSERTION(aStart + aLength <= mCharacterCount, "Overflow");

    if (!mCharacterGlyphs)
        return PR_TRUE;
    PRUint32 changed = 0;
    PRUint32 i;
    for (i = 0; i < aLength; ++i) {
        PRBool canBreak = aBreakBefore[i];
        if (canBreak && !mCharacterGlyphs[aStart + i].IsClusterStart()) {
            // This can happen ... there is no guarantee that our linebreaking rules
            // align with the platform's idea of what constitutes a cluster.
            NS_WARNING("Break suggested inside cluster!");
            canBreak = PR_FALSE;
        }
        changed |= mCharacterGlyphs[aStart + i].SetCanBreakBefore(canBreak);
    }
    return changed != 0;
}

gfxTextRun::LigatureData
gfxTextRun::ComputeLigatureData(PRUint32 aPartStart, PRUint32 aPartEnd,
                                PropertyProvider *aProvider)
{
    NS_ASSERTION(aPartStart < aPartEnd, "Computing ligature data for empty range");
    NS_ASSERTION(aPartEnd <= mCharacterCount, "Character length overflow");
  
    LigatureData result;
    CompressedGlyph *charGlyphs = mCharacterGlyphs;

    PRUint32 i;
    for (i = aPartStart; !charGlyphs[i].IsLigatureGroupStart(); --i) {
        NS_ASSERTION(i > 0, "Ligature at the start of the run??");
    }
    result.mLigatureStart = i;
    for (i = aPartStart + 1; i < mCharacterCount && !charGlyphs[i].IsLigatureGroupStart(); ++i) {
    }
    result.mLigatureEnd = i;

    PRInt32 ligatureWidth =
        GetAdvanceForGlyphs(this, result.mLigatureStart, result.mLigatureEnd);
    // Count the number of started clusters we have seen
    PRUint32 totalClusterCount = 0;
    PRUint32 partClusterIndex = 0;
    PRUint32 partClusterCount = 0;
    for (i = result.mLigatureStart; i < result.mLigatureEnd; ++i) {
        // Treat the first character of the ligature as the start of a
        // cluster for our purposes of allocating ligature width to its
        // characters.
        if (i == result.mLigatureStart || charGlyphs[i].IsClusterStart()) {
            ++totalClusterCount;
            if (i < aPartStart) {
                ++partClusterIndex;
            } else if (i < aPartEnd) {
                ++partClusterCount;
            }
        }
    }
    NS_ASSERTION(totalClusterCount > 0, "Ligature involving no clusters??");
    result.mPartAdvance = ligatureWidth*partClusterIndex/totalClusterCount;
    result.mPartWidth = ligatureWidth*partClusterCount/totalClusterCount;

    if (partClusterCount == 0) {
        // nothing to draw
        result.mClipBeforePart = result.mClipAfterPart = PR_TRUE;
    } else {
        // Determine whether we should clip before or after this part when
        // drawing its slice of the ligature.
        // We need to clip before the part if any cluster is drawn before
        // this part.
        result.mClipBeforePart = partClusterIndex > 0;
        // We need to clip after the part if any cluster is drawn after
        // this part.
        result.mClipAfterPart = partClusterIndex + partClusterCount < totalClusterCount;
    }

    if (aProvider && (mFlags & gfxTextRunFactory::TEXT_ENABLE_SPACING)) {
        gfxFont::Spacing spacing;
        if (aPartStart == result.mLigatureStart) {
            aProvider->GetSpacing(aPartStart, 1, &spacing);
            result.mPartWidth += spacing.mBefore;
        }
        if (aPartEnd == result.mLigatureEnd) {
            aProvider->GetSpacing(aPartEnd - 1, 1, &spacing);
            result.mPartWidth += spacing.mAfter;
        }
    }

    return result;
}

gfxFloat
gfxTextRun::ComputePartialLigatureWidth(PRUint32 aPartStart, PRUint32 aPartEnd,
                                        PropertyProvider *aProvider)
{
    if (aPartStart >= aPartEnd)
        return 0;
    LigatureData data = ComputeLigatureData(aPartStart, aPartEnd, aProvider);
    return data.mPartWidth;
}

static void
GetAdjustedSpacing(gfxTextRun *aTextRun, PRUint32 aStart, PRUint32 aEnd,
                   gfxTextRun::PropertyProvider *aProvider,
                   gfxTextRun::PropertyProvider::Spacing *aSpacing)
{
    if (aStart >= aEnd)
        return;

    aProvider->GetSpacing(aStart, aEnd - aStart, aSpacing);

#ifdef DEBUG
    // Check to see if we have spacing inside ligatures

    const gfxTextRun::CompressedGlyph *charGlyphs = aTextRun->GetCharacterGlyphs();
    PRUint32 i;

    for (i = aStart; i < aEnd; ++i) {
        if (!charGlyphs[i].IsLigatureGroupStart()) {
            NS_ASSERTION(i == aStart || aSpacing[i - aStart].mBefore == 0,
                         "Before-spacing inside a ligature!");
            NS_ASSERTION(i - 1 <= aStart || aSpacing[i - 1 - aStart].mAfter == 0,
                         "After-spacing inside a ligature!");
        }
    }
#endif
}

PRBool
gfxTextRun::GetAdjustedSpacingArray(PRUint32 aStart, PRUint32 aEnd,
                                    PropertyProvider *aProvider,
                                    PRUint32 aSpacingStart, PRUint32 aSpacingEnd,
                                    nsTArray<PropertyProvider::Spacing> *aSpacing)
{
    if (!aProvider || !(mFlags & gfxTextRunFactory::TEXT_ENABLE_SPACING))
        return PR_FALSE;
    if (!aSpacing->AppendElements(aEnd - aStart))
        return PR_FALSE;
    memset(aSpacing->Elements(), 0, sizeof(gfxFont::Spacing)*(aSpacingStart - aStart));
    GetAdjustedSpacing(this, aSpacingStart, aSpacingEnd, aProvider,
                       aSpacing->Elements() + aSpacingStart - aStart);
    memset(aSpacing->Elements() + aSpacingEnd - aStart, 0, sizeof(gfxFont::Spacing)*(aEnd - aSpacingEnd));
    return PR_TRUE;
}

void
gfxTextRun::ShrinkToLigatureBoundaries(PRUint32 *aStart, PRUint32 *aEnd)
{
    if (*aStart >= *aEnd)
        return;
  
    CompressedGlyph *charGlyphs = mCharacterGlyphs;

    while (*aStart < *aEnd && !charGlyphs[*aStart].IsLigatureGroupStart()) {
        ++(*aStart);
    }
    if (*aEnd < mCharacterCount) {
        while (*aEnd > *aStart && !charGlyphs[*aEnd].IsLigatureGroupStart()) {
            --(*aEnd);
        }
    }
}

void
gfxTextRun::DrawGlyphs(gfxFont *aFont, gfxContext *aContext,
                       PRBool aDrawToPath, gfxPoint *aPt,
                       PRUint32 aStart, PRUint32 aEnd,
                       PropertyProvider *aProvider,
                       PRUint32 aSpacingStart, PRUint32 aSpacingEnd)
{
    nsAutoTArray<PropertyProvider::Spacing,200> spacingBuffer;
    PRBool haveSpacing = GetAdjustedSpacingArray(aStart, aEnd, aProvider,
        aSpacingStart, aSpacingEnd, &spacingBuffer);
    aFont->Draw(this, aStart, aEnd, aContext, aDrawToPath, aPt,
                haveSpacing ? spacingBuffer.Elements() : nsnull);
}

static void
ClipPartialLigature(gfxTextRun *aTextRun, gfxFloat *aLeft, gfxFloat *aRight,
                    gfxFloat aXOrigin, gfxTextRun::LigatureData *aLigature)
{
    if (aLigature->mClipBeforePart) {
        if (aTextRun->IsRightToLeft()) {
            *aRight = PR_MIN(*aRight, aXOrigin);
        } else {
            *aLeft = PR_MAX(*aLeft, aXOrigin);
        }
    }
    if (aLigature->mClipAfterPart) {
        gfxFloat endEdge = aXOrigin + aTextRun->GetDirection()*aLigature->mPartWidth;
        if (aTextRun->IsRightToLeft()) {
            *aLeft = PR_MAX(*aLeft, endEdge);
        } else {
            *aRight = PR_MIN(*aRight, endEdge);
        }
    }    
}

void
gfxTextRun::DrawPartialLigature(gfxFont *aFont, gfxContext *aCtx, PRUint32 aStart,
                                PRUint32 aEnd,
                                const gfxRect *aDirtyRect, gfxPoint *aPt,
                                PropertyProvider *aProvider)
{
    if (aStart >= aEnd)
        return;
    if (!aDirtyRect) {
        NS_ERROR("Cannot draw partial ligatures without a dirty rect");
        return;
    }

    // Draw partial ligature. We hack this by clipping the ligature.
    LigatureData data = ComputeLigatureData(aStart, aEnd, aProvider);
    gfxFloat left = aDirtyRect->X();
    gfxFloat right = aDirtyRect->XMost();
    ClipPartialLigature(this, &left, &right, aPt->x, &data);

    aCtx->Save();
    aCtx->NewPath();
    // use division here to ensure that when the rect is aligned on multiples
    // of mAppUnitsPerDevUnit, we clip to true device unit boundaries.
    // Also, make sure we snap the rectangle to device pixels.
    aCtx->Rectangle(gfxRect(left/mAppUnitsPerDevUnit,
                            aDirtyRect->Y()/mAppUnitsPerDevUnit,
                            (right - left)/mAppUnitsPerDevUnit,
                            aDirtyRect->Height()/mAppUnitsPerDevUnit), PR_TRUE);
    aCtx->Clip();
    gfxFloat direction = GetDirection();
    gfxPoint pt(aPt->x - direction*data.mPartAdvance, aPt->y);
    DrawGlyphs(aFont, aCtx, PR_FALSE, &pt, data.mLigatureStart,
               data.mLigatureEnd, aProvider, aStart, aEnd);
    aCtx->Restore();

    aPt->x += direction*data.mPartWidth;
}

// returns true if a glyph run is using a font with synthetic bolding enabled, false otherwise
static PRBool
HasSyntheticBold(gfxTextRun *aRun, PRUint32 aStart, PRUint32 aLength)
{
    gfxTextRun::GlyphRunIterator iter(aRun, aStart, aLength);
    while (iter.NextRun()) {
        gfxFont *font = iter.GetGlyphRun()->mFont;
        if (font && font->IsSyntheticBold()) {
            return PR_TRUE;
        }
    }

    return PR_FALSE;
}

// returns true if color is non-opaque (i.e. alpha != 1.0) or completely transparent, false otherwise
// if true, color is set on output
static PRBool
HasNonOpaqueColor(gfxContext *aContext, gfxRGBA& aCurrentColor)
{
    if (aContext->GetDeviceColor(aCurrentColor)) {
        if (aCurrentColor.a < 1.0 && aCurrentColor.a > 0.0) {
            return PR_TRUE;
        }
    }
        
    return PR_FALSE;
}

// helper class for double-buffering drawing with non-opaque color
struct BufferAlphaColor {
    BufferAlphaColor(gfxContext *aContext)
        : mContext(aContext)
    {

    }

    ~BufferAlphaColor() {}

    void PushSolidColor(const gfxRect& aBounds, const gfxRGBA& aAlphaColor, PRUint32 appsPerDevUnit)
    {
        mContext->Save();
        mContext->NewPath();
        mContext->Rectangle(gfxRect(aBounds.X() / appsPerDevUnit,
                    aBounds.Y() / appsPerDevUnit,
                    aBounds.Width() / appsPerDevUnit,
                    aBounds.Height() / appsPerDevUnit), PR_TRUE);
        mContext->Clip();
        mContext->SetColor(gfxRGBA(aAlphaColor.r, aAlphaColor.g, aAlphaColor.b));
        mContext->PushGroup(gfxASurface::CONTENT_COLOR_ALPHA);
        mAlpha = aAlphaColor.a;
    }

    void PopAlpha()
    {
        // pop the text, using the color alpha as the opacity
        mContext->PopGroupToSource();
        mContext->SetOperator(gfxContext::OPERATOR_OVER);
        mContext->Paint(mAlpha);
        mContext->Restore();
    }

    gfxContext *mContext;
    gfxFloat mAlpha;
};

void
gfxTextRun::AdjustAdvancesForSyntheticBold(PRUint32 aStart, PRUint32 aLength)
{
    const PRUint32 appUnitsPerDevUnit = GetAppUnitsPerDevUnit();
    PRBool isRTL = IsRightToLeft();

    GlyphRunIterator iter(this, aStart, aLength);
    while (iter.NextRun()) {
        gfxFont *font = iter.GetGlyphRun()->mFont;
        if (font->IsSyntheticBold()) {
            PRUint32 synAppUnitOffset = font->GetSyntheticBoldOffset() * appUnitsPerDevUnit;
            PRUint32 start = iter.GetStringStart();
            PRUint32 end = iter.GetStringEnd();
            PRUint32 i;
            
            // iterate over glyphs, start to end
            for (i = start; i < end; ++i) {
                gfxTextRun::CompressedGlyph *glyphData = &mCharacterGlyphs[i];
                
                if (glyphData->IsSimpleGlyph()) {
                    // simple glyphs ==> just add the advance
                    PRUint32 advance = glyphData->GetSimpleAdvance() + synAppUnitOffset;
                    if (CompressedGlyph::IsSimpleAdvance(advance)) {
                        glyphData->SetSimpleGlyph(advance, glyphData->GetSimpleGlyph());
                    } else {
                        // rare case, tested by making this the default
                        PRUint32 glyphIndex = glyphData->GetSimpleGlyph();
                        glyphData->SetComplex(PR_TRUE, PR_TRUE, 1);
                        DetailedGlyph detail = {glyphIndex, advance, 0, 0};
                        SetGlyphs(i, *glyphData, &detail);
                    }
                } else {
                    // complex glyphs ==> add offset at cluster/ligature boundaries
                    PRUint32 detailedLength = glyphData->GetGlyphCount();
                    if (detailedLength && mDetailedGlyphs) {
                        gfxTextRun::DetailedGlyph *details = mDetailedGlyphs[i].get();
                        if (!details) continue;
                        if (isRTL)
                            details[0].mAdvance += synAppUnitOffset;
                        else
                            details[detailedLength - 1].mAdvance += synAppUnitOffset;
                    }
                }
            }
        }
    }
}

void
gfxTextRun::Draw(gfxContext *aContext, gfxPoint aPt,
                 PRUint32 aStart, PRUint32 aLength, const gfxRect *aDirtyRect,
                 PropertyProvider *aProvider, gfxFloat *aAdvanceWidth)
{
    NS_ASSERTION(aStart + aLength <= mCharacterCount, "Substring out of range");

    gfxFloat direction = GetDirection();
    gfxPoint pt = aPt;

    // synthetic bolding draws glyphs twice ==> colors with opacity won't draw correctly unless first drawn without alpha
    BufferAlphaColor syntheticBoldBuffer(aContext);
    gfxRGBA currentColor;
    PRBool needToRestore = PR_FALSE;

    if (HasNonOpaqueColor(aContext, currentColor) && HasSyntheticBold(this, aStart, aLength)) {
        needToRestore = PR_TRUE;
        // measure text, use the bounding box
        gfxTextRun::Metrics metrics = MeasureText(aStart, aLength, gfxFont::LOOSE_INK_EXTENTS,
                                                  aContext, aProvider);
        metrics.mBoundingBox.MoveBy(aPt);
        syntheticBoldBuffer.PushSolidColor(metrics.mBoundingBox, currentColor, GetAppUnitsPerDevUnit());
    }

    GlyphRunIterator iter(this, aStart, aLength);
    while (iter.NextRun()) {
        gfxFont *font = iter.GetGlyphRun()->mFont;
        PRUint32 start = iter.GetStringStart();
        PRUint32 end = iter.GetStringEnd();
        PRUint32 ligatureRunStart = start;
        PRUint32 ligatureRunEnd = end;
        ShrinkToLigatureBoundaries(&ligatureRunStart, &ligatureRunEnd);
        
        DrawPartialLigature(font, aContext, start, ligatureRunStart, aDirtyRect, &pt, aProvider);
        DrawGlyphs(font, aContext, PR_FALSE, &pt, ligatureRunStart,
                   ligatureRunEnd, aProvider, ligatureRunStart, ligatureRunEnd);
        DrawPartialLigature(font, aContext, ligatureRunEnd, end, aDirtyRect, &pt, aProvider);
    }

    // composite result when synthetic bolding used
    if (needToRestore) {
        syntheticBoldBuffer.PopAlpha();
    }

    if (aAdvanceWidth) {
        *aAdvanceWidth = (pt.x - aPt.x)*direction;
    }
}

void
gfxTextRun::DrawToPath(gfxContext *aContext, gfxPoint aPt,
                       PRUint32 aStart, PRUint32 aLength,
                       PropertyProvider *aProvider, gfxFloat *aAdvanceWidth)
{
    NS_ASSERTION(aStart + aLength <= mCharacterCount, "Substring out of range");

    gfxFloat direction = GetDirection();
    gfxPoint pt = aPt;

    GlyphRunIterator iter(this, aStart, aLength);
    while (iter.NextRun()) {
        gfxFont *font = iter.GetGlyphRun()->mFont;
        PRUint32 start = iter.GetStringStart();
        PRUint32 end = iter.GetStringEnd();
        PRUint32 ligatureRunStart = start;
        PRUint32 ligatureRunEnd = end;
        ShrinkToLigatureBoundaries(&ligatureRunStart, &ligatureRunEnd);
        NS_ASSERTION(ligatureRunStart == start,
                     "Can't draw path starting inside ligature");
        NS_ASSERTION(ligatureRunEnd == end,
                     "Can't end drawing path inside ligature");

        DrawGlyphs(font, aContext, PR_TRUE, &pt, ligatureRunStart, ligatureRunEnd, aProvider,
            ligatureRunStart, ligatureRunEnd);
    }

    if (aAdvanceWidth) {
        *aAdvanceWidth = (pt.x - aPt.x)*direction;
    }
}

void
gfxTextRun::AccumulateMetricsForRun(gfxFont *aFont,
                                    PRUint32 aStart, PRUint32 aEnd,
                                    gfxFont::BoundingBoxType aBoundingBoxType,
                                    gfxContext *aRefContext,
                                    PropertyProvider *aProvider,
                                    PRUint32 aSpacingStart, PRUint32 aSpacingEnd,
                                    Metrics *aMetrics)
{
    nsAutoTArray<PropertyProvider::Spacing,200> spacingBuffer;
    PRBool haveSpacing = GetAdjustedSpacingArray(aStart, aEnd, aProvider,
        aSpacingStart, aSpacingEnd, &spacingBuffer);
    Metrics metrics = aFont->Measure(this, aStart, aEnd, aBoundingBoxType, aRefContext,
                                     haveSpacing ? spacingBuffer.Elements() : nsnull);
    aMetrics->CombineWith(metrics, IsRightToLeft());
}

void
gfxTextRun::AccumulatePartialLigatureMetrics(gfxFont *aFont,
    PRUint32 aStart, PRUint32 aEnd,
    gfxFont::BoundingBoxType aBoundingBoxType, gfxContext *aRefContext,
    PropertyProvider *aProvider, Metrics *aMetrics)
{
    if (aStart >= aEnd)
        return;

    // Measure partial ligature. We hack this by clipping the metrics in the
    // same way we clip the drawing.
    LigatureData data = ComputeLigatureData(aStart, aEnd, aProvider);

    // First measure the complete ligature
    Metrics metrics;
    AccumulateMetricsForRun(aFont, data.mLigatureStart, data.mLigatureEnd,
                            aBoundingBoxType, aRefContext,
                            aProvider, aStart, aEnd, &metrics);

    // Clip the bounding box to the ligature part
    gfxFloat bboxLeft = metrics.mBoundingBox.X();
    gfxFloat bboxRight = metrics.mBoundingBox.XMost();
    // Where we are going to start "drawing" relative to our left baseline origin
    gfxFloat origin = IsRightToLeft() ? metrics.mAdvanceWidth - data.mPartAdvance : 0;
    ClipPartialLigature(this, &bboxLeft, &bboxRight, origin, &data);
    metrics.mBoundingBox.pos.x = bboxLeft;
    metrics.mBoundingBox.size.width = bboxRight - bboxLeft;

    // mBoundingBox is now relative to the left baseline origin for the entire
    // ligature. Shift it left.
    metrics.mBoundingBox.pos.x -=
        IsRightToLeft() ? metrics.mAdvanceWidth - (data.mPartAdvance + data.mPartWidth)
            : data.mPartAdvance;    
    metrics.mAdvanceWidth = data.mPartWidth;

    aMetrics->CombineWith(metrics, IsRightToLeft());
}

gfxTextRun::Metrics
gfxTextRun::MeasureText(PRUint32 aStart, PRUint32 aLength,
                        gfxFont::BoundingBoxType aBoundingBoxType,
                        gfxContext *aRefContext,
                        PropertyProvider *aProvider)
{
    NS_ASSERTION(aStart + aLength <= mCharacterCount, "Substring out of range");

    Metrics accumulatedMetrics;
    GlyphRunIterator iter(this, aStart, aLength);
    while (iter.NextRun()) {
        gfxFont *font = iter.GetGlyphRun()->mFont;
        PRUint32 start = iter.GetStringStart();
        PRUint32 end = iter.GetStringEnd();
        PRUint32 ligatureRunStart = start;
        PRUint32 ligatureRunEnd = end;
        ShrinkToLigatureBoundaries(&ligatureRunStart, &ligatureRunEnd);

        AccumulatePartialLigatureMetrics(font, start, ligatureRunStart,
            aBoundingBoxType, aRefContext, aProvider, &accumulatedMetrics);

        // XXX This sucks. We have to get glyph extents just so we can detect
        // glyphs outside the font box, even when aBoundingBoxType is LOOSE,
        // even though in almost all cases we could get correct results just
        // by getting some ascent/descent from the font and using our stored
        // advance widths.
        AccumulateMetricsForRun(font,
            ligatureRunStart, ligatureRunEnd, aBoundingBoxType,
            aRefContext, aProvider, ligatureRunStart, ligatureRunEnd,
            &accumulatedMetrics);

        AccumulatePartialLigatureMetrics(font, ligatureRunEnd, end,
            aBoundingBoxType, aRefContext, aProvider, &accumulatedMetrics);
    }

    return accumulatedMetrics;
}

#define MEASUREMENT_BUFFER_SIZE 100

PRUint32
gfxTextRun::BreakAndMeasureText(PRUint32 aStart, PRUint32 aMaxLength,
                                PRBool aLineBreakBefore, gfxFloat aWidth,
                                PropertyProvider *aProvider,
                                PRBool aSuppressInitialBreak,
                                gfxFloat *aTrimWhitespace,
                                Metrics *aMetrics,
                                gfxFont::BoundingBoxType aBoundingBoxType,
                                gfxContext *aRefContext,
                                PRBool *aUsedHyphenation,
                                PRUint32 *aLastBreak,
                                PRBool aCanWordWrap,
                                gfxBreakPriority *aBreakPriority)
{
    aMaxLength = PR_MIN(aMaxLength, mCharacterCount - aStart);

    NS_ASSERTION(aStart + aMaxLength <= mCharacterCount, "Substring out of range");

    PRUint32 bufferStart = aStart;
    PRUint32 bufferLength = PR_MIN(aMaxLength, MEASUREMENT_BUFFER_SIZE);
    PropertyProvider::Spacing spacingBuffer[MEASUREMENT_BUFFER_SIZE];
    PRBool haveSpacing = aProvider && (mFlags & gfxTextRunFactory::TEXT_ENABLE_SPACING) != 0;
    if (haveSpacing) {
        GetAdjustedSpacing(this, bufferStart, bufferStart + bufferLength, aProvider,
                           spacingBuffer);
    }
    PRPackedBool hyphenBuffer[MEASUREMENT_BUFFER_SIZE];
    PRBool haveHyphenation = (mFlags & gfxTextRunFactory::TEXT_ENABLE_HYPHEN_BREAKS) != 0;
    if (haveHyphenation) {
        aProvider->GetHyphenationBreaks(bufferStart, bufferLength,
                                        hyphenBuffer);
    }

    gfxFloat width = 0;
    gfxFloat advance = 0;
    // The number of space characters that can be trimmed
    PRUint32 trimmableChars = 0;
    // The amount of space removed by ignoring trimmableChars
    gfxFloat trimmableAdvance = 0;
    PRInt32 lastBreak = -1;
    PRInt32 lastBreakTrimmableChars = -1;
    gfxFloat lastBreakTrimmableAdvance = -1;
    PRBool aborted = PR_FALSE;
    PRUint32 end = aStart + aMaxLength;
    PRBool lastBreakUsedHyphenation = PR_FALSE;

    PRUint32 ligatureRunStart = aStart;
    PRUint32 ligatureRunEnd = end;
    ShrinkToLigatureBoundaries(&ligatureRunStart, &ligatureRunEnd);

    PRUint32 i;
    for (i = aStart; i < end; ++i) {
        if (i >= bufferStart + bufferLength) {
            // Fetch more spacing and hyphenation data
            bufferStart = i;
            bufferLength = PR_MIN(aStart + aMaxLength, i + MEASUREMENT_BUFFER_SIZE) - i;
            if (haveSpacing) {
                GetAdjustedSpacing(this, bufferStart, bufferStart + bufferLength, aProvider,
                                   spacingBuffer);
            }
            if (haveHyphenation) {
                aProvider->GetHyphenationBreaks(bufferStart, bufferLength,
                                                hyphenBuffer);
            }
        }

        // There can't be a word-wrap break opportunity at the beginning of the
        // line: if the width is too small for even one character to fit, it 
        // could be the first and last break opportunity on the line, and that
        // would trigger an infinite loop.
        if (!aSuppressInitialBreak || i > aStart) {
            PRBool lineBreakHere = mCharacterGlyphs[i].CanBreakBefore();
            PRBool hyphenation = haveHyphenation && hyphenBuffer[i - bufferStart];
            PRBool wordWrapping = aCanWordWrap && *aBreakPriority <= eWordWrapBreak;

            if (lineBreakHere || hyphenation || wordWrapping) {
                gfxFloat hyphenatedAdvance = advance;
                if (!lineBreakHere && !wordWrapping) {
                    hyphenatedAdvance += aProvider->GetHyphenWidth();
                }
            
                if (lastBreak < 0 || width + hyphenatedAdvance - trimmableAdvance <= aWidth) {
                    // We can break here.
                    lastBreak = i;
                    lastBreakTrimmableChars = trimmableChars;
                    lastBreakTrimmableAdvance = trimmableAdvance;
                    lastBreakUsedHyphenation = !lineBreakHere && !wordWrapping;
                    *aBreakPriority = hyphenation || lineBreakHere ?
                        eNormalBreak : eWordWrapBreak;
                }

                width += advance;
                advance = 0;
                if (width - trimmableAdvance > aWidth) {
                    // No more text fits. Abort
                    aborted = PR_TRUE;
                    break;
                }
            }
        }
        
        gfxFloat charAdvance;
        if (i >= ligatureRunStart && i < ligatureRunEnd) {
            charAdvance = GetAdvanceForGlyphs(this, i, i + 1);
            if (haveSpacing) {
                PropertyProvider::Spacing *space = &spacingBuffer[i - bufferStart];
                charAdvance += space->mBefore + space->mAfter;
            }
        } else {
            charAdvance = ComputePartialLigatureWidth(i, i + 1, aProvider);
        }
        
        advance += charAdvance;
        if (aTrimWhitespace) {
            if (GetChar(i) == ' ') {
                ++trimmableChars;
                trimmableAdvance += charAdvance;
            } else {
                trimmableAdvance = 0;
                trimmableChars = 0;
            }
        }
    }

    if (!aborted) {
        width += advance;
    }

    // There are three possibilities:
    // 1) all the text fit (width <= aWidth)
    // 2) some of the text fit up to a break opportunity (width > aWidth && lastBreak >= 0)
    // 3) none of the text fits before a break opportunity (width > aWidth && lastBreak < 0)
    PRUint32 charsFit;
    PRBool usedHyphenation = PR_FALSE;
    if (width - trimmableAdvance <= aWidth) {
        charsFit = aMaxLength;
    } else if (lastBreak >= 0) {
        charsFit = lastBreak - aStart;
        trimmableChars = lastBreakTrimmableChars;
        trimmableAdvance = lastBreakTrimmableAdvance;
        usedHyphenation = lastBreakUsedHyphenation;
    } else {
        charsFit = aMaxLength;
    }

    if (aMetrics) {
        *aMetrics = MeasureText(aStart, charsFit - trimmableChars,
            aBoundingBoxType, aRefContext, aProvider);
    }
    if (aTrimWhitespace) {
        *aTrimWhitespace = trimmableAdvance;
    }
    if (aUsedHyphenation) {
        *aUsedHyphenation = usedHyphenation;
    }
    if (aLastBreak && charsFit == aMaxLength) {
        if (lastBreak < 0) {
            *aLastBreak = PR_UINT32_MAX;
        } else {
            *aLastBreak = lastBreak - aStart;
        }
    }

    return charsFit;
}

gfxFloat
gfxTextRun::GetAdvanceWidth(PRUint32 aStart, PRUint32 aLength,
                            PropertyProvider *aProvider)
{
    NS_ASSERTION(aStart + aLength <= mCharacterCount, "Substring out of range");

    PRUint32 ligatureRunStart = aStart;
    PRUint32 ligatureRunEnd = aStart + aLength;
    ShrinkToLigatureBoundaries(&ligatureRunStart, &ligatureRunEnd);

    gfxFloat result = ComputePartialLigatureWidth(aStart, ligatureRunStart, aProvider) +
                      ComputePartialLigatureWidth(ligatureRunEnd, aStart + aLength, aProvider);

    // Account for all remaining spacing here. This is more efficient than
    // processing it along with the glyphs.
    if (aProvider && (mFlags & gfxTextRunFactory::TEXT_ENABLE_SPACING)) {
        PRUint32 i;
        nsAutoTArray<PropertyProvider::Spacing,200> spacingBuffer;
        if (spacingBuffer.AppendElements(aLength)) {
            GetAdjustedSpacing(this, ligatureRunStart, ligatureRunEnd, aProvider,
                               spacingBuffer.Elements());
            for (i = 0; i < ligatureRunEnd - ligatureRunStart; ++i) {
                PropertyProvider::Spacing *space = &spacingBuffer[i];
                result += space->mBefore + space->mAfter;
            }
        }
    }

    return result + GetAdvanceForGlyphs(this, ligatureRunStart, ligatureRunEnd);
}

PRBool
gfxTextRun::SetLineBreaks(PRUint32 aStart, PRUint32 aLength,
                          PRBool aLineBreakBefore, PRBool aLineBreakAfter,
                          gfxFloat *aAdvanceWidthDelta,
                          gfxContext *aRefContext)
{
    // Do nothing because our shaping does not currently take linebreaks into
    // account. There is no change in advance width.
    if (aAdvanceWidthDelta) {
        *aAdvanceWidthDelta = 0;
    }
    return PR_FALSE;
}

PRUint32
gfxTextRun::FindFirstGlyphRunContaining(PRUint32 aOffset)
{
    NS_ASSERTION(aOffset <= mCharacterCount, "Bad offset looking for glyphrun");
    if (aOffset == mCharacterCount)
        return mGlyphRuns.Length();
    PRUint32 start = 0;
    PRUint32 end = mGlyphRuns.Length();
    while (end - start > 1) {
        PRUint32 mid = (start + end)/2;
        if (mGlyphRuns[mid].mCharacterOffset <= aOffset) {
            start = mid;
        } else {
            end = mid;
        }
    }
    NS_ASSERTION(mGlyphRuns[start].mCharacterOffset <= aOffset,
                 "Hmm, something went wrong, aOffset should have been found");
    return start;
}

nsresult
gfxTextRun::AddGlyphRun(gfxFont *aFont, PRUint32 aUTF16Offset, PRBool aForceNewRun)
{
    PRUint32 numGlyphRuns = mGlyphRuns.Length();
    if (!aForceNewRun &&
        numGlyphRuns > 0)
    {
        GlyphRun *lastGlyphRun = &mGlyphRuns[numGlyphRuns - 1];

        NS_ASSERTION(lastGlyphRun->mCharacterOffset <= aUTF16Offset,
                     "Glyph runs out of order (and run not forced)");

        if (lastGlyphRun->mFont == aFont)
            return NS_OK;
        if (lastGlyphRun->mCharacterOffset == aUTF16Offset) {
            lastGlyphRun->mFont = aFont;
            return NS_OK;
        }
    }

    NS_ASSERTION(aForceNewRun || numGlyphRuns > 0 || aUTF16Offset == 0,
                 "First run doesn't cover the first character (and run not forced)?");

    GlyphRun *glyphRun = mGlyphRuns.AppendElement();
    if (!glyphRun)
        return NS_ERROR_OUT_OF_MEMORY;
    glyphRun->mFont = aFont;
    glyphRun->mCharacterOffset = aUTF16Offset;
    return NS_OK;
}

void
gfxTextRun::SortGlyphRuns()
{
    if (mGlyphRuns.Length() <= 1)
        return;

    nsTArray<GlyphRun> runs(mGlyphRuns);
    GlyphRunOffsetComparator comp;
    runs.Sort(comp);

    // Now copy back, coalescing adjacent glyph runs that have the same font
    mGlyphRuns.Clear();
    PRUint32 i;
    for (i = 0; i < runs.Length(); ++i) {
        // a GlyphRun with the same font as the previous GlyphRun can just
        // be skipped; the last GlyphRun will cover its character range.
        if (i == 0 || runs[i].mFont != runs[i - 1].mFont) {
            mGlyphRuns.AppendElement(runs[i]);
            // If two fonts have the same character offset, Sort() will have
            // randomized the order.
            NS_ASSERTION(i == 0 ||
                         runs[i].mCharacterOffset !=
                         runs[i - 1].mCharacterOffset,
                         "Two fonts for the same run, glyph indices may not match the font");
        }
    }
}

void
gfxTextRun::SanitizeGlyphRuns()
{
    if (mGlyphRuns.Length() <= 1)
        return;

    // If any glyph run starts with ligature-continuation characters, we need to advance it
    // to the first "real" character to avoid drawing partial ligature glyphs from wrong font
    // (seen with U+FEFF in reftest 474417-1, as Core Text eliminates the glyph, which makes
    // it appear as if a ligature has been formed)
    PRInt32 i, lastRunIndex = mGlyphRuns.Length() - 1;
    for (i = lastRunIndex; i >= 0; --i) {
        GlyphRun& run = mGlyphRuns[i];
        while (mCharacterGlyphs[run.mCharacterOffset].IsLigatureContinuation() &&
               run.mCharacterOffset < mCharacterCount) {
            run.mCharacterOffset++;
        }
        // if the run has become empty, eliminate it
        if ((i < lastRunIndex &&
             run.mCharacterOffset >= mGlyphRuns[i+1].mCharacterOffset) ||
            (i == lastRunIndex && run.mCharacterOffset == mCharacterCount)) {
            mGlyphRuns.RemoveElementAt(i);
            --lastRunIndex;
        }
    }
}

PRUint32
gfxTextRun::CountMissingGlyphs()
{
    PRUint32 i;
    PRUint32 count = 0;
    for (i = 0; i < mCharacterCount; ++i) {
        if (mCharacterGlyphs[i].IsMissing()) {
            ++count;
        }
    }
    return count;
}

gfxTextRun::DetailedGlyph *
gfxTextRun::AllocateDetailedGlyphs(PRUint32 aIndex, PRUint32 aCount)
{
    NS_ASSERTION(aIndex < mCharacterCount, "Index out of range");

    if (!mCharacterGlyphs)
        return nsnull;

    if (!mDetailedGlyphs) {
        mDetailedGlyphs = new nsAutoArrayPtr<DetailedGlyph>[mCharacterCount];
        if (!mDetailedGlyphs) {
            mCharacterGlyphs[aIndex].SetMissing(0);
            return nsnull;
        }
    }
    DetailedGlyph *details = new DetailedGlyph[aCount];
    if (!details) {
        mCharacterGlyphs[aIndex].SetMissing(0);
        return nsnull;
    }
    mDetailedGlyphs[aIndex] = details;
    return details;
}

void
gfxTextRun::SetGlyphs(PRUint32 aIndex, CompressedGlyph aGlyph,
                      const DetailedGlyph *aGlyphs)
{
    NS_ASSERTION(!aGlyph.IsSimpleGlyph(), "Simple glyphs not handled here");
    NS_ASSERTION(aIndex > 0 ||
                 (aGlyph.IsClusterStart() && aGlyph.IsLigatureGroupStart()),
                 "First character must be the start of a cluster and can't be a ligature continuation!");

    PRUint32 glyphCount = aGlyph.GetGlyphCount();
    if (glyphCount > 0) {
        DetailedGlyph *details = AllocateDetailedGlyphs(aIndex, glyphCount);
        if (!details)
            return;
        memcpy(details, aGlyphs, sizeof(DetailedGlyph)*glyphCount);
    }
    mCharacterGlyphs[aIndex] = aGlyph;
}

void
gfxTextRun::SetMissingGlyph(PRUint32 aIndex, PRUint32 aChar)
{
    DetailedGlyph *details = AllocateDetailedGlyphs(aIndex, 1);
    if (!details)
        return;

    details->mGlyphID = aChar;
    GlyphRun *glyphRun = &mGlyphRuns[FindFirstGlyphRunContaining(aIndex)];
    gfxFloat width = PR_MAX(glyphRun->mFont->GetMetrics().aveCharWidth,
                            gfxFontMissingGlyphs::GetDesiredMinWidth(aChar));
    details->mAdvance = PRUint32(width*GetAppUnitsPerDevUnit());
    details->mXOffset = 0;
    details->mYOffset = 0;
    mCharacterGlyphs[aIndex].SetMissing(1);
}

static void
ClearCharacters(gfxTextRun::CompressedGlyph *aGlyphs, PRUint32 aLength)
{
    memset(aGlyphs, 0, sizeof(gfxTextRun::CompressedGlyph)*aLength);
}

void
gfxTextRun::CopyGlyphDataFrom(gfxTextRun *aSource, PRUint32 aStart,
                              PRUint32 aLength, PRUint32 aDest,
                              PRBool aStealData)
{
    NS_ASSERTION(aStart + aLength <= aSource->GetLength(),
                 "Source substring out of range");
    NS_ASSERTION(aDest + aLength <= GetLength(),
                 "Destination substring out of range");

    PRUint32 i;
    // Copy base character data
    for (i = 0; i < aLength; ++i) {
        CompressedGlyph g = aSource->mCharacterGlyphs[i + aStart];
        g.SetCanBreakBefore(mCharacterGlyphs[i + aDest].CanBreakBefore());
        mCharacterGlyphs[i + aDest] = g;
        if (aStealData) {
            aSource->mCharacterGlyphs[i + aStart].SetMissing(0);
        }
    }

    // Copy detailed glyphs
    if (aSource->mDetailedGlyphs) {
        for (i = 0; i < aLength; ++i) {
            DetailedGlyph *details = aSource->mDetailedGlyphs[i + aStart];
            if (details) {
                if (aStealData) {
                    if (!mDetailedGlyphs) {
                        mDetailedGlyphs = new nsAutoArrayPtr<DetailedGlyph>[mCharacterCount];
                        if (!mDetailedGlyphs) {
                            ClearCharacters(&mCharacterGlyphs[aDest], aLength);
                            return;
                        }
                    }        
                    mDetailedGlyphs[i + aDest] = details;
                    aSource->mDetailedGlyphs[i + aStart].forget();
                } else {
                    PRUint32 glyphCount = mCharacterGlyphs[i + aDest].GetGlyphCount();
                    DetailedGlyph *dest = AllocateDetailedGlyphs(i + aDest, glyphCount);
                    if (!dest) {
                        ClearCharacters(&mCharacterGlyphs[aDest], aLength);
                        return;
                    }
                    memcpy(dest, details, sizeof(DetailedGlyph)*glyphCount);
                }
            } else if (mDetailedGlyphs) {
                mDetailedGlyphs[i + aDest] = nsnull;
            }
        }
    } else if (mDetailedGlyphs) {
        for (i = 0; i < aLength; ++i) {
            mDetailedGlyphs[i + aDest] = nsnull;
        }
    }

    // Copy glyph runs
    GlyphRunIterator iter(aSource, aStart, aLength);
#ifdef DEBUG
    gfxFont *lastFont = nsnull;
#endif
    while (iter.NextRun()) {
        gfxFont *font = iter.GetGlyphRun()->mFont;
        NS_ASSERTION(font != lastFont, "Glyphruns not coalesced?");
#ifdef DEBUG
        lastFont = font;
        PRUint32 end = iter.GetStringEnd();
#endif
        PRUint32 start = iter.GetStringStart();
        // These assertions are probably not needed; it's possible for us to assign
        // different fonts to a base character and a following diacritic.
        // View http://www.alanwood.net/unicode/cyrillic.html on OS X 10.5 for an example.
        NS_ASSERTION(aSource->IsClusterStart(start),
                     "Started word in the middle of a cluster...");
        NS_ASSERTION(end == aSource->GetLength() || aSource->IsClusterStart(end),
                     "Ended word in the middle of a cluster...");

        nsresult rv = AddGlyphRun(font, start - aStart + aDest);
        if (NS_FAILED(rv))
            return;
    }
}

void
gfxTextRun::SetSpaceGlyph(gfxFont *aFont, gfxContext *aContext, PRUint32 aCharIndex)
{
    PRUint32 spaceGlyph = aFont->GetSpaceGlyph();
    float spaceWidth = aFont->GetMetrics().spaceWidth;
    PRUint32 spaceWidthAppUnits = NS_lroundf(spaceWidth*mAppUnitsPerDevUnit);
    if (!spaceGlyph ||
        !CompressedGlyph::IsSimpleGlyphID(spaceGlyph) ||
        !CompressedGlyph::IsSimpleAdvance(spaceWidthAppUnits)) {
        gfxTextRunFactory::Parameters params = {
            aContext, nsnull, nsnull, nsnull, 0, mAppUnitsPerDevUnit
        };
        static const PRUint8 space = ' ';
        nsAutoPtr<gfxTextRun> textRun;
        textRun = mFontGroup->MakeTextRun(&space, 1, &params,
            gfxTextRunFactory::TEXT_IS_8BIT | gfxTextRunFactory::TEXT_IS_ASCII |
            gfxTextRunFactory::TEXT_IS_PERSISTENT);
        if (!textRun || !textRun->mCharacterGlyphs)
            return;
        CopyGlyphDataFrom(textRun, 0, 1, aCharIndex, PR_TRUE);
        return;
    }

    AddGlyphRun(aFont, aCharIndex);
    CompressedGlyph g;
    g.SetSimpleGlyph(spaceWidthAppUnits, spaceGlyph);
    SetSimpleGlyph(aCharIndex, g);
}

void
gfxTextRun::FetchGlyphExtents(gfxContext *aRefContext)
{
    PRBool needsGlyphExtents = NeedsGlyphExtents(this);
    if (!needsGlyphExtents && !mDetailedGlyphs)
        return;

    PRUint32 i;
    CompressedGlyph *charGlyphs = mCharacterGlyphs;
    for (i = 0; i < mGlyphRuns.Length(); ++i) {
        gfxFont *font = mGlyphRuns[i].mFont;
        PRUint32 start = mGlyphRuns[i].mCharacterOffset;
        PRUint32 end = i + 1 < mGlyphRuns.Length()
            ? mGlyphRuns[i + 1].mCharacterOffset : GetLength();
        PRBool fontIsSetup = PR_FALSE;
        PRUint32 j;
        gfxGlyphExtents *extents = font->GetOrCreateGlyphExtents(mAppUnitsPerDevUnit);
  
        for (j = start; j < end; ++j) {
            const gfxTextRun::CompressedGlyph *glyphData = &charGlyphs[j];
            if (glyphData->IsSimpleGlyph()) {
                // If we're in speed mode, don't set up glyph extents here; we'll
                // just return "optimistic" glyph bounds later
                if (needsGlyphExtents) {
                    PRUint32 glyphIndex = glyphData->GetSimpleGlyph();
                    if (!extents->IsGlyphKnown(glyphIndex)) {
                        if (!fontIsSetup) {
                            font->SetupCairoFont(aRefContext);
                             fontIsSetup = PR_TRUE;
                        }
#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
                        ++gGlyphExtentsSetupEagerSimple;
#endif
                        font->SetupGlyphExtents(aRefContext, glyphIndex, PR_FALSE, extents);
                    }
                }
            } else if (!glyphData->IsMissing()) {
                PRUint32 k;
                PRUint32 glyphCount = glyphData->GetGlyphCount();
                const gfxTextRun::DetailedGlyph *details = GetDetailedGlyphs(j);
                for (k = 0; k < glyphCount; ++k, ++details) {
                    PRUint32 glyphIndex = details->mGlyphID;
                    if (!extents->IsGlyphKnownWithTightExtents(glyphIndex)) {
                        if (!fontIsSetup) {
                            font->SetupCairoFont(aRefContext);
                            fontIsSetup = PR_TRUE;
                        }
#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
                        ++gGlyphExtentsSetupEagerTight;
#endif
                        font->SetupGlyphExtents(aRefContext, glyphIndex, PR_TRUE, extents);
                    }
                }
            }
        }
    }
}

#ifdef DEBUG
void
gfxTextRun::Dump(FILE* aOutput) {
    if (!aOutput) {
        aOutput = stdout;
    }

    PRUint32 i;
    fputc('"', aOutput);
    for (i = 0; i < mCharacterCount; ++i) {
        PRUnichar ch = GetChar(i);
        if (ch >= 32 && ch < 128) {
            fputc(ch, aOutput);
        } else {
            fprintf(aOutput, "\\u%4x", ch);
        }
    }
    fputs("\" [", aOutput);
    for (i = 0; i < mGlyphRuns.Length(); ++i) {
        if (i > 0) {
            fputc(',', aOutput);
        }
        gfxFont* font = mGlyphRuns[i].mFont;
        const gfxFontStyle* style = font->GetStyle();
        NS_ConvertUTF16toUTF8 fontName(font->GetName());
        fprintf(aOutput, "%d: %s %f/%d/%d/%s", mGlyphRuns[i].mCharacterOffset,
                fontName.get(), style->size,
                style->weight, style->style, style->langGroup.get());
    }
    fputc(']', aOutput);
}
#endif
