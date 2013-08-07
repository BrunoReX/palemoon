/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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
 * The Original Code is Mozilla Graphite integration code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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

#include "prtypes.h"
#include "prmem.h"
#include "nsString.h"
#include "nsBidiUtils.h"
#include "nsMathUtils.h"
#include "nsHashSets.h"

#include "gfxTypes.h"

#include "gfxContext.h"
#include "gfxPlatform.h"
#include "gfxGraphiteShaper.h"
#include "gfxFontUtils.h"
#include "gfxUnicodeProperties.h"

#include "graphite2/Font.h"
#include "graphite2/Segment.h"

#include "harfbuzz/hb-blob.h"

#include "cairo.h"

#include "nsUnicodeRange.h"
#include "nsCRT.h"

#define FloatToFixed(f) (65536 * (f))
#define FixedToFloat(f) ((f) * (1.0 / 65536.0))
// Right shifts of negative (signed) integers are undefined, as are overflows
// when converting unsigned to negative signed integers.
// (If speed were an issue we could make some 2's complement assumptions.)
#define FixedToIntRound(f) ((f) > 0 ?  ((32768 + (f)) >> 16) \
                                    : -((32767 - (f)) >> 16))

using namespace mozilla; // for AutoSwap_* types

/*
 * Creation and destruction; on deletion, release any font tables we're holding
 */

gfxGraphiteShaper::gfxGraphiteShaper(gfxFont *aFont)
    : gfxFontShaper(aFont),
      mGrFace(nsnull),
      mGrFont(nsnull),
      mUseFontGlyphWidths(false)
{
    mTables.Init();
    mCallbackData.mFont = aFont;
    mCallbackData.mShaper = this;
}

PLDHashOperator
ReleaseTableFunc(const PRUint32& /* aKey */,
                 gfxGraphiteShaper::TableRec& aData,
                 void* /* aUserArg */)
{
    hb_blob_unlock(aData.mBlob);
    hb_blob_destroy(aData.mBlob);
    return PL_DHASH_REMOVE;
}

gfxGraphiteShaper::~gfxGraphiteShaper()
{
    if (mGrFont) {
        gr_font_destroy(mGrFont);
    }
    if (mGrFace) {
        gr_face_destroy(mGrFace);
    }
    mTables.Enumerate(ReleaseTableFunc, nsnull);
}

static const void*
GrGetTable(const void* appFaceHandle, unsigned int name, size_t *len)
{
    const gfxGraphiteShaper::CallbackData *cb =
        static_cast<const gfxGraphiteShaper::CallbackData*>(appFaceHandle);
    return cb->mShaper->GetTable(name, len);
}

const void*
gfxGraphiteShaper::GetTable(PRUint32 aTag, size_t *aLength)
{
    TableRec tableRec;

    if (!mTables.Get(aTag, &tableRec)) {
        hb_blob_t *blob = mFont->GetFontTable(aTag);
        if (blob) {
            // mFont->GetFontTable() gives us a reference to the blob.
            // We will destroy (release) it in our destructor.
            // Meanwhile, we hold the blob locked because Graphite expects
            // the table pointer to remain valid for the life of the face.
            tableRec.mBlob = blob;
            tableRec.mData = hb_blob_lock(blob);
            tableRec.mLength = hb_blob_get_length(blob);
            mTables.Put(aTag, tableRec);
        } else {
            return nsnull;
        }
    }

    *aLength = tableRec.mLength;
    return tableRec.mData;
}

static float
GrGetAdvance(const void* appFontHandle, gr_uint16 glyphid)
{
    const gfxGraphiteShaper::CallbackData *cb =
        static_cast<const gfxGraphiteShaper::CallbackData*>(appFontHandle);
    return FixedToFloat(cb->mFont->GetGlyphWidth(cb->mContext, glyphid));
}

static inline PRUint32
MakeGraphiteLangTag(PRUint32 aTag)
{
    PRUint32 grLangTag = aTag;
    // replace trailing space-padding with NULs for graphite
    PRUint32 mask = 0x000000FF;
    while ((grLangTag & mask) == ' ') {
        grLangTag &= ~mask;
        mask <<= 8;
    }
    return grLangTag;
}

bool
gfxGraphiteShaper::InitTextRun(gfxContext *aContext,
                               gfxTextRun *aTextRun,
                               const PRUnichar *aString,
                               PRUint32 aRunStart,
                               PRUint32 aRunLength,
                               PRInt32 aRunScript)
{
    // some font back-ends require this in order to get proper hinted metrics
    mFont->SetupCairoFont(aContext);

    mCallbackData.mContext = aContext;

    if (!mGrFont) {
        mGrFace = gr_make_face(&mCallbackData, GrGetTable, gr_face_default);
        if (!mGrFace) {
            return false;
        }
        mGrFont = mUseFontGlyphWidths ?
            gr_make_font_with_advance_fn(mFont->GetAdjustedSize(),
                                         &mCallbackData, GrGetAdvance,
                                         mGrFace) :
            gr_make_font(mFont->GetAdjustedSize(), mGrFace);
        if (!mGrFont) {
            gr_face_destroy(mGrFace);
            mGrFace = nsnull;
            return false;
        }
    }

    const gfxFontStyle *style = aTextRun->GetFontGroup()->GetStyle();
    PRUint32 grLang = 0;
    if (style->languageOverride) {
        grLang = MakeGraphiteLangTag(style->languageOverride);
    } else if (mFont->GetFontEntry()->mLanguageOverride) {
        grLang = MakeGraphiteLangTag(mFont->GetFontEntry()->mLanguageOverride);
    } else {
        nsCAutoString langString;
        style->language->ToUTF8String(langString);
        grLang = GetGraphiteTagForLang(langString);
    }
    gr_feature_val *grFeatures = gr_face_featureval_for_lang(mGrFace, grLang);

    bool disableLigatures =
        (aTextRun->GetFlags() &
         gfxTextRunFactory::TEXT_DISABLE_OPTIONAL_LIGATURES) != 0;
    if (disableLigatures) {
        const gr_feature_ref* fref =
            gr_face_find_fref(mGrFace, TRUETYPE_TAG('l','i','g','a'));
        if (fref) {
            gr_fref_set_feature_value(fref, 0, grFeatures);
        }
    }

    const nsTArray<gfxFontFeature> *features = &style->featureSettings;
    if (features->IsEmpty()) {
        features = &mFont->GetFontEntry()->mFeatureSettings;
    }
    for (PRUint32 i = 0; i < features->Length(); ++i) {
        const gr_feature_ref* fref =
            gr_face_find_fref(mGrFace, (*features)[i].mTag);
        if (fref) {
            gr_fref_set_feature_value(fref, (*features)[i].mValue, grFeatures);
        }
    }

    const PRUnichar *textStart = aString + aRunStart;
    const PRUnichar *textEnd = textStart + aRunLength;
    const void *pError;
    size_t nChars = gr_count_unicode_characters(gr_utf16,
                                                textStart, textEnd,
                                                &pError);
    if (pError != nsnull) {
        return false;
    }

    gr_segment *seg = gr_make_seg(mGrFont, mGrFace, 0, grFeatures,
                                  gr_utf16, textStart, nChars,
                                  aTextRun->IsRightToLeft());
    if (features) {
        gr_featureval_destroy(grFeatures);
    }
    if (!seg) {
        return false;
    }

    nsresult rv = SetGlyphsFromSegment(aTextRun, aRunStart, aRunLength, seg);

    gr_seg_destroy(seg);

    return NS_SUCCEEDED(rv);
}

#define SMALL_GLYPH_RUN 256 // avoid heap allocation of per-glyph data arrays
                            // for short (typical) runs up to this length

struct Cluster {
    PRUint32 baseChar;
    PRUint32 baseGlyph;
    PRUint32 nChars;
    PRUint32 nGlyphs;
    Cluster() : baseChar(0), baseGlyph(0), nChars(0), nGlyphs(0) { }
};

nsresult
gfxGraphiteShaper::SetGlyphsFromSegment(gfxTextRun *aTextRun,
                                        PRUint32 aRunStart,
                                        PRUint32 aRunLength,
                                        gr_segment *aSegment)
{
    PRInt32 dev2appUnits = aTextRun->GetAppUnitsPerDevUnit();
    bool rtl = aTextRun->IsRightToLeft();

    PRUint32 glyphCount = gr_seg_n_slots(aSegment);

    // identify clusters; graphite may have reordered/expanded/ligated glyphs.
    nsAutoTArray<Cluster,SMALL_GLYPH_RUN> clusters;
    nsAutoTArray<PRUint16,SMALL_GLYPH_RUN> gids;
    nsAutoTArray<float,SMALL_GLYPH_RUN> xLocs;
    nsAutoTArray<float,SMALL_GLYPH_RUN> yLocs;

    if (!clusters.SetLength(aRunLength) ||
        !gids.SetLength(glyphCount) ||
        !xLocs.SetLength(glyphCount) ||
        !yLocs.SetLength(glyphCount))
    {
        return NS_ERROR_OUT_OF_MEMORY;
    }

    // walk through the glyph slots and check which original character
    // each is associated with
    PRUint32 gIndex = 0; // glyph slot index
    PRUint32 cIndex = 0; // current cluster index
    for (const gr_slot *slot = gr_seg_first_slot(aSegment);
         slot != nsnull;
         slot = gr_slot_next_in_segment(slot), gIndex++)
    {
        PRUint32 before = gr_slot_before(slot);
        PRUint32 after = gr_slot_after(slot);
        gids[gIndex] = gr_slot_gid(slot);
        xLocs[gIndex] = gr_slot_origin_X(slot);
        yLocs[gIndex] = gr_slot_origin_Y(slot);

        // if this glyph has a "before" character index that precedes the
        // current cluster's char index, we need to merge preceding
        // clusters until it gets included
        while (before < clusters[cIndex].baseChar && cIndex > 0) {
            clusters[cIndex-1].nChars += clusters[cIndex].nChars;
            clusters[cIndex-1].nGlyphs += clusters[cIndex].nGlyphs;
            --cIndex;
        }

        // if there's a gap between the current cluster's base character and
        // this glyph's, extend the cluster to include the intervening chars
        if (gr_slot_can_insert_before(slot) && clusters[cIndex].nChars &&
            before >= clusters[cIndex].baseChar + clusters[cIndex].nChars)
        {
            NS_ASSERTION(cIndex < aRunLength - 1, "cIndex at end of run");
            Cluster& c = clusters[cIndex + 1];
            c.baseChar = clusters[cIndex].baseChar + clusters[cIndex].nChars;
            c.nChars = before - c.baseChar;
            c.baseGlyph = gIndex;
            c.nGlyphs = 0;
            ++cIndex;
        }

        // increment cluster's glyph count to include current slot
        NS_ASSERTION(cIndex < aRunLength, "cIndex beyond valid run length");
        ++clusters[cIndex].nGlyphs;

        // extend cluster if necessary to reach the glyph's "after" index
        if (clusters[cIndex].baseChar + clusters[cIndex].nChars < after + 1) {
            clusters[cIndex].nChars = after + 1 - clusters[cIndex].baseChar;
        }
    }

    // now put glyphs into the textrun, one cluster at a time
    for (PRUint32 i = 0; i <= cIndex; ++i) {
        const Cluster& c = clusters[i];

        float adv; // total advance of the cluster
        if (rtl) {
            if (i == 0) {
                adv = gr_seg_advance_X(aSegment) - xLocs[c.baseGlyph];
            } else {
                adv = xLocs[clusters[i-1].baseGlyph] - xLocs[c.baseGlyph];
            }
        } else {
            if (i == cIndex) {
                adv = gr_seg_advance_X(aSegment) - xLocs[c.baseGlyph];
            } else {
                adv = xLocs[clusters[i+1].baseGlyph] - xLocs[c.baseGlyph];
            }
        }

        // Check for default-ignorable char that didn't get filtered, combined,
        // etc by the shaping process, and skip it.
        PRUint32 offs = gr_cinfo_base(gr_seg_cinfo(aSegment, c.baseChar));
        NS_ASSERTION(offs >= c.baseChar && offs < aRunLength,
                     "unexpected offset");
        if (c.nGlyphs == 1 && c.nChars == 1 &&
            aTextRun->FilterIfIgnorable(aRunStart + offs))
        {
            continue;
        }

        PRUint32 appAdvance = adv * dev2appUnits;
        if (c.nGlyphs == 1 &&
            gfxTextRun::CompressedGlyph::IsSimpleGlyphID(gids[c.baseGlyph]) &&
            gfxTextRun::CompressedGlyph::IsSimpleAdvance(appAdvance) &&
            yLocs[c.baseGlyph] == 0)
        {
            gfxTextRun::CompressedGlyph g;
            aTextRun->SetSimpleGlyph(aRunStart + offs,
                                     g.SetSimpleGlyph(appAdvance,
                                                      gids[c.baseGlyph]));
        } else {
            // not a one-to-one mapping with simple metrics: use DetailedGlyph
            nsAutoTArray<gfxTextRun::DetailedGlyph,8> details;
            float clusterLoc;
            for (PRUint32 j = c.baseGlyph; j < c.baseGlyph + c.nGlyphs; ++j) {
                gfxTextRun::DetailedGlyph* d = details.AppendElement();
                d->mGlyphID = gids[j];
                d->mYOffset = -yLocs[j] * dev2appUnits;
                if (j == c.baseGlyph) {
                    d->mXOffset = 0;
                    d->mAdvance = appAdvance;
                    clusterLoc = xLocs[j];
                } else {
                    d->mXOffset = (xLocs[j] - clusterLoc - adv) * dev2appUnits;
                    d->mAdvance = 0;
                }
            }
            gfxTextRun::CompressedGlyph g;
            g.SetComplex(aTextRun->IsClusterStart(aRunStart + offs),
                         true, details.Length());
            aTextRun->SetGlyphs(aRunStart + offs, g, details.Elements());
        }

        for (PRUint32 j = c.baseChar + 1; j < c.baseChar + c.nChars; ++j) {
            offs = gr_cinfo_base(gr_seg_cinfo(aSegment, j));
            NS_ASSERTION(offs >= j && offs < aRunLength,
                         "unexpected offset");
            gfxTextRun::CompressedGlyph g;
            g.SetComplex(aTextRun->IsClusterStart(aRunStart + offs),
                         false, 0);
            aTextRun->SetGlyphs(aRunStart + offs, g, nsnull);
        }
    }

    return NS_OK;
}

// for language tag validation - include list of tags from the IANA registry
#include "gfxLanguageTagList.cpp"

nsTHashtable<nsUint32HashKey> gfxGraphiteShaper::sLanguageTags;

/*static*/ PRUint32
gfxGraphiteShaper::GetGraphiteTagForLang(const nsCString& aLang)
{
    int len = aLang.Length();
    if (len < 2) {
        return 0;
    }

    // convert primary language subtag to a left-packed, NUL-padded integer
    // for the Graphite API
    PRUint32 grLang = 0;
    for (int i = 0; i < 4; ++i) {
        grLang <<= 8;
        if (i < len) {
            PRUint8 ch = aLang[i];
            if (ch == '-') {
                // found end of primary language subtag, truncate here
                len = i;
                continue;
            }
            if (ch < 'a' || ch > 'z') {
                // invalid character in tag, so ignore it completely
                return 0;
            }
            grLang += ch;
        }
    }

    // valid tags must have length = 2 or 3
    if (len < 2 || len > 3) {
        return 0;
    }

    if (!sLanguageTags.IsInitialized()) {
        // store the registered IANA tags in a hash for convenient validation
        sLanguageTags.Init(ArrayLength(sLanguageTagList));
        for (const PRUint32 *tag = sLanguageTagList; *tag != 0; ++tag) {
            sLanguageTags.PutEntry(*tag);
        }
    }

    // only accept tags known in the IANA registry
    if (sLanguageTags.GetEntry(grLang)) {
        return grLang;
    }

    return 0;
}

/*static*/ void
gfxGraphiteShaper::Shutdown()
{
#ifdef NS_FREE_PERMANENT_DATA
    if (sLanguageTags.IsInitialized()) {
        sLanguageTags.Clear();
    }
#endif
}
