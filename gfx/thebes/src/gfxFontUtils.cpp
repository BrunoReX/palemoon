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
 * The Original Code is thebes gfx code.
 *
 * The Initial Developer of the Original Code is Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2007-2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Stuart Parmenter <stuart@mozilla.com>
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

#include "gfxFontUtils.h"

#include "nsServiceManagerUtils.h"

#include "nsIPrefBranch.h"
#include "nsIPrefService.h"
#include "nsIPrefLocalizedString.h"
#include "nsISupportsPrimitives.h"
#include "nsIStreamBufferAccess.h"
#include "nsIUUIDGenerator.h"
#include "nsMemory.h"
#include "nsICharsetConverterManager.h"

#include "plbase64.h"

#include "woff.h"

#ifdef XP_MACOSX
#include <CoreFoundation/CoreFoundation.h>
#endif

#define NO_RANGE_FOUND 126 // bit 126 in the font unicode ranges is required to be 0

/* Unicode subrange table
 *   from: http://msdn.microsoft.com/library/default.asp?url=/library/en-us/intl/unicode_63ub.asp
 *
 * Use something like:
 * perl -pi -e 's/^(\d+)\s+([\dA-Fa-f]+)\s+-\s+([\dA-Fa-f]+)\s+\b(.*)/    { \1, 0x\2, 0x\3,\"\4\" },/' < unicoderanges.txt
 * to generate the below list.
 */
struct UnicodeRangeTableEntry
{
    PRUint8 bit;
    PRUint32 start;
    PRUint32 end;
    const char *info;
};

static const struct UnicodeRangeTableEntry gUnicodeRanges[] = {
    { 0, 0x0000, 0x007F, "Basic Latin" },
    { 1, 0x0080, 0x00FF, "Latin-1 Supplement" },
    { 2, 0x0100, 0x017F, "Latin Extended-A" },
    { 3, 0x0180, 0x024F, "Latin Extended-B" },
    { 4, 0x0250, 0x02AF, "IPA Extensions" },
    { 4, 0x1D00, 0x1D7F, "Phonetic Extensions" },
    { 4, 0x1D80, 0x1DBF, "Phonetic Extensions Supplement" },
    { 5, 0x02B0, 0x02FF, "Spacing Modifier Letters" },
    { 5, 0xA700, 0xA71F, "Modifier Tone Letters" },
    { 6, 0x0300, 0x036F, "Spacing Modifier Letters" },
    { 6, 0x1DC0, 0x1DFF, "Combining Diacritical Marks Supplement" },
    { 7, 0x0370, 0x03FF, "Greek and Coptic" },
    { 8, 0x2C80, 0x2CFF, "Coptic" },
    { 9, 0x0400, 0x04FF, "Cyrillic" },
    { 9, 0x0500, 0x052F, "Cyrillic Supplementary" },
    { 10, 0x0530, 0x058F, "Armenian" },
    { 11, 0x0590, 0x05FF, "Basic Hebrew" },
    /* 12 - reserved */
    { 13, 0x0600, 0x06FF, "Basic Arabic" },
    { 13, 0x0750, 0x077F, "Arabic Supplement" },
    { 14, 0x07C0, 0x07FF, "N'Ko" },
    { 15, 0x0900, 0x097F, "Devanagari" },
    { 16, 0x0980, 0x09FF, "Bengali" },
    { 17, 0x0A00, 0x0A7F, "Gurmukhi" },
    { 18, 0x0A80, 0x0AFF, "Gujarati" },
    { 19, 0x0B00, 0x0B7F, "Oriya" },
    { 20, 0x0B80, 0x0BFF, "Tamil" },
    { 21, 0x0C00, 0x0C7F, "Telugu" },
    { 22, 0x0C80, 0x0CFF, "Kannada" },
    { 23, 0x0D00, 0x0D7F, "Malayalam" },
    { 24, 0x0E00, 0x0E7F, "Thai" },
    { 25, 0x0E80, 0x0EFF, "Lao" },
    { 26, 0x10A0, 0x10FF, "Georgian" },
    { 26, 0x2D00, 0x2D2F, "Georgian Supplement" },
    { 27, 0x1B00, 0x1B7F, "Balinese" },
    { 28, 0x1100, 0x11FF, "Hangul Jamo" },
    { 29, 0x1E00, 0x1EFF, "Latin Extended Additional" },
    { 29, 0x2C60, 0x2C7F, "Latin Extended-C" },
    { 30, 0x1F00, 0x1FFF, "Greek Extended" },
    { 31, 0x2000, 0x206F, "General Punctuation" },
    { 31, 0x2E00, 0x2E7F, "Supplemental Punctuation" },
    { 32, 0x2070, 0x209F, "Subscripts and Superscripts" },
    { 33, 0x20A0, 0x20CF, "Currency Symbols" },
    { 34, 0x20D0, 0x20FF, "Combining Diacritical Marks for Symbols" },
    { 35, 0x2100, 0x214F, "Letter-like Symbols" },
    { 36, 0x2150, 0x218F, "Number Forms" },
    { 37, 0x2190, 0x21FF, "Arrows" },
    { 37, 0x27F0, 0x27FF, "Supplemental Arrows-A" },
    { 37, 0x2900, 0x297F, "Supplemental Arrows-B" },
    { 37, 0x2B00, 0x2BFF, "Miscellaneous Symbols and Arrows" },
    { 38, 0x2200, 0x22FF, "Mathematical Operators" },
    { 38, 0x27C0, 0x27EF, "Miscellaneous Mathematical Symbols-A" },
    { 38, 0x2980, 0x29FF, "Miscellaneous Mathematical Symbols-B" },
    { 38, 0x2A00, 0x2AFF, "Supplemental Mathematical Operators" },
    { 39, 0x2300, 0x23FF, "Miscellaneous Technical" },
    { 40, 0x2400, 0x243F, "Control Pictures" },
    { 41, 0x2440, 0x245F, "Optical Character Recognition" },
    { 42, 0x2460, 0x24FF, "Enclosed Alphanumerics" },
    { 43, 0x2500, 0x257F, "Box Drawing" },
    { 44, 0x2580, 0x259F, "Block Elements" },
    { 45, 0x25A0, 0x25FF, "Geometric Shapes" },
    { 46, 0x2600, 0x26FF, "Miscellaneous Symbols" },
    { 47, 0x2700, 0x27BF, "Dingbats" },
    { 48, 0x3000, 0x303F, "Chinese, Japanese, and Korean (CJK) Symbols and Punctuation" },
    { 49, 0x3040, 0x309F, "Hiragana" },
    { 50, 0x30A0, 0x30FF, "Katakana" },
    { 50, 0x31F0, 0x31FF, "Katakana Phonetic Extensions" },
    { 51, 0x3100, 0x312F, "Bopomofo" },
    { 51, 0x31A0, 0x31BF, "Extended Bopomofo" },
    { 52, 0x3130, 0x318F, "Hangul Compatibility Jamo" },
    { 53, 0xA840, 0xA87F, "Phags-pa" },
    { 54, 0x3200, 0x32FF, "Enclosed CJK Letters and Months" },
    { 55, 0x3300, 0x33FF, "CJK Compatibility" },
    { 56, 0xAC00, 0xD7A3, "Hangul" },
    { 57, 0xD800, 0xDFFF, "Surrogates. Note that setting this bit implies that there is at least one supplementary code point beyond the Basic Multilingual Plane (BMP) that is supported by this font. See Surrogates and Supplementary Characters." },
    { 58, 0x10900, 0x1091F, "Phoenician" },
    { 59, 0x2E80, 0x2EFF, "CJK Radicals Supplement" },
    { 59, 0x2F00, 0x2FDF, "Kangxi Radicals" },
    { 59, 0x2FF0, 0x2FFF, "Ideographic Description Characters" },
    { 59, 0x3190, 0x319F, "Kanbun" },
    { 59, 0x3400, 0x4DBF, "CJK Unified Ideographs Extension A" },
    { 59, 0x4E00, 0x9FFF, "CJK Unified Ideographs" },
    { 59, 0x20000, 0x2A6DF, "CJK Unified Ideographs Extension B" },
    { 60, 0xE000, 0xF8FF, "Private Use (Plane 0)" },
    { 61, 0x31C0, 0x31EF, "CJK Base Strokes" },
    { 61, 0xF900, 0xFAFF, "CJK Compatibility Ideographs" },
    { 61, 0x2F800, 0x2FA1F, "CJK Compatibility Ideographs Supplement" },
    { 62, 0xFB00, 0xFB4F, "Alphabetical Presentation Forms" },
    { 63, 0xFB50, 0xFDFF, "Arabic Presentation Forms-A" },
    { 64, 0xFE20, 0xFE2F, "Combining Half Marks" },
    { 65, 0xFE10, 0xFE1F, "Vertical Forms" },
    { 65, 0xFE30, 0xFE4F, "CJK Compatibility Forms" },
    { 66, 0xFE50, 0xFE6F, "Small Form Variants" },
    { 67, 0xFE70, 0xFEFE, "Arabic Presentation Forms-B" },
    { 68, 0xFF00, 0xFFEF, "Halfwidth and Fullwidth Forms" },
    { 69, 0xFFF0, 0xFFFF, "Specials" },
    { 70, 0x0F00, 0x0FFF, "Tibetan" },
    { 71, 0x0700, 0x074F, "Syriac" },
    { 72, 0x0780, 0x07BF, "Thaana" },
    { 73, 0x0D80, 0x0DFF, "Sinhala" },
    { 74, 0x1000, 0x109F, "Myanmar" },
    { 75, 0x1200, 0x137F, "Ethiopic" },
    { 75, 0x1380, 0x139F, "Ethiopic Supplement" },
    { 75, 0x2D80, 0x2DDF, "Ethiopic Extended" },
    { 76, 0x13A0, 0x13FF, "Cherokee" },
    { 77, 0x1400, 0x167F, "Canadian Aboriginal Syllabics" },
    { 78, 0x1680, 0x169F, "Ogham" },
    { 79, 0x16A0, 0x16FF, "Runic" },
    { 80, 0x1780, 0x17FF, "Khmer" },
    { 80, 0x19E0, 0x19FF, "Khmer Symbols" },
    { 81, 0x1800, 0x18AF, "Mongolian" },
    { 82, 0x2800, 0x28FF, "Braille" },
    { 83, 0xA000, 0xA48F, "Yi" },
    { 83, 0xA490, 0xA4CF, "Yi Radicals" },
    { 84, 0x1700, 0x171F, "Tagalog" },
    { 84, 0x1720, 0x173F, "Hanunoo" },
    { 84, 0x1740, 0x175F, "Buhid" },
    { 84, 0x1760, 0x177F, "Tagbanwa" },
    { 85, 0x10300, 0x1032F, "Old Italic" },
    { 86, 0x10330, 0x1034F, "Gothic" },
    { 87, 0x10440, 0x1044F, "Deseret" },
    { 88, 0x1D000, 0x1D0FF, "Byzantine Musical Symbols" },
    { 88, 0x1D100, 0x1D1FF, "Musical Symbols" },
    { 88, 0x1D200, 0x1D24F, "Ancient Greek Musical Notation" },
    { 89, 0x1D400, 0x1D7FF, "Mathematical Alphanumeric Symbols" },
    { 90, 0xFF000, 0xFFFFD, "Private Use (Plane 15)" },
    { 90, 0x100000, 0x10FFFD, "Private Use (Plane 16)" },
    { 91, 0xFE00, 0xFE0F, "Variation Selectors" },
    { 91, 0xE0100, 0xE01EF, "Variation Selectors Supplement" },
    { 92, 0xE0000, 0xE007F, "Tags" },
    { 93, 0x1900, 0x194F, "Limbu" },
    { 94, 0x1950, 0x197F, "Tai Le" },
    { 95, 0x1980, 0x19DF, "New Tai Lue" },
    { 96, 0x1A00, 0x1A1F, "Buginese" },
    { 97, 0x2C00, 0x2C5F, "Glagolitic" },
    { 98, 0x2D40, 0x2D7F, "Tifinagh" },
    { 99, 0x4DC0, 0x4DFF, "Yijing Hexagram Symbols" },
    { 100, 0xA800, 0xA82F, "Syloti Nagri" },
    { 101, 0x10000, 0x1007F, "Linear B Syllabary" },
    { 101, 0x10080, 0x100FF, "Linear B Ideograms" },
    { 101, 0x10100, 0x1013F, "Aegean Numbers" },
    { 102, 0x10140, 0x1018F, "Ancient Greek Numbers" },
    { 103, 0x10380, 0x1039F, "Ugaritic" },
    { 104, 0x103A0, 0x103DF, "Old Persian" },
    { 105, 0x10450, 0x1047F, "Shavian" },
    { 106, 0x10480, 0x104AF, "Osmanya" },
    { 107, 0x10800, 0x1083F, "Cypriot Syllabary" },
    { 108, 0x10A00, 0x10A5F, "Kharoshthi" },
    { 109, 0x1D300, 0x1D35F, "Tai Xuan Jing Symbols" },
    { 110, 0x12000, 0x123FF, "Cuneiform" },
    { 110, 0x12400, 0x1247F, "Cuneiform Numbers and Punctuation" },
    { 111, 0x1D360, 0x1D37F, "Counting Rod Numerals" }
};

nsresult
gfxFontUtils::ReadCMAPTableFormat12(PRUint8 *aBuf, PRUint32 aLength, gfxSparseBitSet& aCharacterMap) 
{
    enum {
        OffsetFormat = 0,
        OffsetReserved = 2,
        OffsetTableLength = 4,
        OffsetLanguage = 8,
        OffsetNumberGroups = 12,
        OffsetGroups = 16,

        SizeOfGroup = 12,

        GroupOffsetStartCode = 0,
        GroupOffsetEndCode = 4
    };
    NS_ENSURE_TRUE(aLength >= 16, NS_ERROR_GFX_CMAP_MALFORMED);

    NS_ENSURE_TRUE(ReadShortAt(aBuf, OffsetFormat) == 12, 
                   NS_ERROR_GFX_CMAP_MALFORMED);
    NS_ENSURE_TRUE(ReadShortAt(aBuf, OffsetReserved) == 0, 
                   NS_ERROR_GFX_CMAP_MALFORMED);

    PRUint32 tablelen = ReadLongAt(aBuf, OffsetTableLength);
    NS_ENSURE_TRUE(tablelen <= aLength, NS_ERROR_GFX_CMAP_MALFORMED);
    NS_ENSURE_TRUE(tablelen >= 16, NS_ERROR_GFX_CMAP_MALFORMED);

    NS_ENSURE_TRUE(ReadLongAt(aBuf, OffsetLanguage) == 0, 
                   NS_ERROR_GFX_CMAP_MALFORMED);

    const PRUint32 numGroups  = ReadLongAt(aBuf, OffsetNumberGroups);
    NS_ENSURE_TRUE(tablelen >= 16 + (12 * numGroups), 
                   NS_ERROR_GFX_CMAP_MALFORMED);

    const PRUint8 *groups = aBuf + OffsetGroups;
    PRUint32 prevEndCharCode = 0;
    for (PRUint32 i = 0; i < numGroups; i++, groups += SizeOfGroup) {
        const PRUint32 startCharCode = ReadLongAt(groups, GroupOffsetStartCode);
        const PRUint32 endCharCode = ReadLongAt(groups, GroupOffsetEndCode);
        NS_ENSURE_TRUE((prevEndCharCode < startCharCode || i == 0) &&
                       startCharCode <= endCharCode &&
                       endCharCode <= CMAP_MAX_CODEPOINT, 
                       NS_ERROR_GFX_CMAP_MALFORMED);
        aCharacterMap.SetRange(startCharCode, endCharCode);
        prevEndCharCode = endCharCode;
    }

    return NS_OK;
}

nsresult 
gfxFontUtils::ReadCMAPTableFormat4(PRUint8 *aBuf, PRUint32 aLength, gfxSparseBitSet& aCharacterMap)
{
    enum {
        OffsetFormat = 0,
        OffsetLength = 2,
        OffsetLanguage = 4,
        OffsetSegCountX2 = 6
    };

    NS_ENSURE_TRUE(ReadShortAt(aBuf, OffsetFormat) == 4, 
                   NS_ERROR_GFX_CMAP_MALFORMED);
    PRUint16 tablelen = ReadShortAt(aBuf, OffsetLength);
    NS_ENSURE_TRUE(tablelen <= aLength, NS_ERROR_GFX_CMAP_MALFORMED);
    NS_ENSURE_TRUE(tablelen > 16, NS_ERROR_GFX_CMAP_MALFORMED);
    
    // some buggy fonts on Mac OS report lang = English (e.g. Arial Narrow Bold, v. 1.1 (Tiger))
#if defined(XP_WIN)
    NS_ENSURE_TRUE(ReadShortAt(aBuf, OffsetLanguage) == 0, 
                   NS_ERROR_GFX_CMAP_MALFORMED);
#endif

    PRUint16 segCountX2 = ReadShortAt(aBuf, OffsetSegCountX2);
    NS_ENSURE_TRUE(tablelen >= 16 + (segCountX2 * 4), 
                   NS_ERROR_GFX_CMAP_MALFORMED);

    const PRUint16 segCount = segCountX2 / 2;

    const PRUint16 *endCounts = reinterpret_cast<const PRUint16*>(aBuf + 14);
    const PRUint16 *startCounts = endCounts + 1 /* skip one uint16 for reservedPad */ + segCount;
    const PRUint16 *idDeltas = startCounts + segCount;
    const PRUint16 *idRangeOffsets = idDeltas + segCount;
    PRUint16 prevEndCount = 0;
    for (PRUint16 i = 0; i < segCount; i++) {
        const PRUint16 endCount = ReadShortAt16(endCounts, i);
        const PRUint16 startCount = ReadShortAt16(startCounts, i);
        const PRUint16 idRangeOffset = ReadShortAt16(idRangeOffsets, i);
        
        // sanity-check range
        NS_ENSURE_TRUE((startCount > prevEndCount || i == 0) && 
                       startCount <= endCount,
                       NS_ERROR_GFX_CMAP_MALFORMED);
        prevEndCount = endCount;
        
        if (idRangeOffset == 0) {
            aCharacterMap.SetRange(startCount, endCount);
        } else {
            // const PRUint16 idDelta = ReadShortAt16(idDeltas, i); // Unused: self-documenting.
            for (PRUint32 c = startCount; c <= endCount; ++c) {
                if (c == 0xFFFF)
                    break;

                const PRUint16 *gdata = (idRangeOffset/2 
                                         + (c - startCount)
                                         + &idRangeOffsets[i]);

                NS_ENSURE_TRUE((PRUint8*)gdata > aBuf && 
                               (PRUint8*)gdata < aBuf + aLength, 
                               NS_ERROR_GFX_CMAP_MALFORMED);

                // make sure we have a glyph
                if (*gdata != 0) {
                    // The glyph index at this point is:
                    // glyph = (ReadShortAt16(idDeltas, i) + *gdata) % 65536;

                    aCharacterMap.set(c);
                }
            }
        }
    }

    return NS_OK;
}

// Windows requires fonts to have a format-4 cmap with a Microsoft ID (3).  On the Mac, fonts either have
// a format-4 cmap with Microsoft platform/encoding id or they have one with a platformID == Unicode (0)
// For fonts with two format-4 tables, the first one (Unicode platform) is preferred on the Mac.

#if defined(XP_MACOSX)
    #define acceptablePlatform(p)    ((p) == PLATFORM_ID_UNICODE || (p) == PLATFORM_ID_MICROSOFT)
    #define acceptableFormat4(p,e,k) ( ((p) == PLATFORM_ID_MICROSOFT && (e) == EncodingIDMicrosoft && (k) != 4) || \
                                       ((p) == PLATFORM_ID_UNICODE) )
    #define isSymbol(p,e)            ((p) == PLATFORM_ID_MICROSOFT && (e) == EncodingIDSymbol)
#else
    #define acceptablePlatform(p)    ((p) == PLATFORM_ID_MICROSOFT)
    #define acceptableFormat4(p,e,k) ((e) == EncodingIDMicrosoft)
    #define isSymbol(p,e)            ((e) == EncodingIDSymbol)
#endif

#define acceptableUCS4Encoding(p, e) \
    ((platformID == PLATFORM_ID_MICROSOFT && encodingID == EncodingIDUCS4ForMicrosoftPlatform) || \
     (platformID == PLATFORM_ID_UNICODE   && \
      (encodingID == EncodingIDDefaultForUnicodePlatform || encodingID >= EncodingIDUCS4ForUnicodePlatform)))

PRUint32
gfxFontUtils::FindPreferredSubtable(PRUint8 *aBuf, PRUint32 aBufLength,
                                    PRUint32 *aTableOffset, PRBool *aSymbolEncoding)
{
    enum {
        OffsetVersion = 0,
        OffsetNumTables = 2,
        SizeOfHeader = 4,

        TableOffsetPlatformID = 0,
        TableOffsetEncodingID = 2,
        TableOffsetOffset = 4,
        SizeOfTable = 8,

        SubtableOffsetFormat = 0
    };
    enum {
        EncodingIDSymbol = 0,
        EncodingIDMicrosoft = 1,
        EncodingIDDefaultForUnicodePlatform = 0,
        EncodingIDUCS4ForUnicodePlatform = 3,
        EncodingIDUCS4ForMicrosoftPlatform = 10
    };

    // PRUint16 version = ReadShortAt(aBuf, OffsetVersion); // Unused: self-documenting.
    PRUint16 numTables = ReadShortAt(aBuf, OffsetNumTables);

    // save the format we want here
    PRUint32 keepFormat = 0;

    PRUint8 *table = aBuf + SizeOfHeader;
    for (PRUint16 i = 0; i < numTables; ++i, table += SizeOfTable) {
        const PRUint16 platformID = ReadShortAt(table, TableOffsetPlatformID);
        if (!acceptablePlatform(platformID))
            continue;

        const PRUint16 encodingID = ReadShortAt(table, TableOffsetEncodingID);
        const PRUint32 offset = ReadLongAt(table, TableOffsetOffset);

        NS_ASSERTION(offset < aBufLength, "cmap table offset is longer than table size");
        NS_ENSURE_TRUE(offset < aBufLength, NS_ERROR_GFX_CMAP_MALFORMED);

        const PRUint8 *subtable = aBuf + offset;
        const PRUint16 format = ReadShortAt(subtable, SubtableOffsetFormat);

        if (isSymbol(platformID, encodingID)) {
            keepFormat = format;
            *aTableOffset = offset;
            *aSymbolEncoding = PR_TRUE;
            break;
        } else if (format == 4 && acceptableFormat4(platformID, encodingID, keepFormat)) {
            keepFormat = format;
            *aTableOffset = offset;
            *aSymbolEncoding = PR_FALSE;
        } else if (format == 12 && acceptableUCS4Encoding(platformID, encodingID)) {
            keepFormat = format;
            *aTableOffset = offset;
            *aSymbolEncoding = PR_FALSE;
            break; // we don't want to try anything else when this format is available.
        }
    }

    return keepFormat;
}

nsresult
gfxFontUtils::ReadCMAP(PRUint8 *aBuf, PRUint32 aBufLength, gfxSparseBitSet& aCharacterMap, 
                       PRPackedBool& aUnicodeFont, PRPackedBool& aSymbolFont)
{
    PRUint32 offset;
    PRBool   symbol;
    PRUint32 format = FindPreferredSubtable(aBuf, aBufLength, &offset, &symbol);

    if (format == 4) {
        if (symbol) {
            aUnicodeFont = PR_FALSE;
            aSymbolFont = PR_TRUE;
        } else {
            aUnicodeFont = PR_TRUE;
            aSymbolFont = PR_FALSE;
        }
        return ReadCMAPTableFormat4(aBuf + offset, aBufLength - offset, aCharacterMap);
    }

    if (format == 12) {
        aUnicodeFont = PR_TRUE;
        aSymbolFont = PR_FALSE;
        return ReadCMAPTableFormat12(aBuf + offset, aBufLength - offset, aCharacterMap);
    }

    return NS_ERROR_FAILURE;
}

using namespace mozilla; // for the AutoSwap_* types

#pragma pack(1)

typedef struct {
    AutoSwap_PRUint16 format;
    AutoSwap_PRUint16 length;
    AutoSwap_PRUint16 language;
    AutoSwap_PRUint16 segCountX2;
    AutoSwap_PRUint16 searchRange;
    AutoSwap_PRUint16 entrySelector;
    AutoSwap_PRUint16 rangeShift;

    AutoSwap_PRUint16 arrays[1];
} Format4Cmap;

PRUint32
gfxFontUtils::MapCharToGlyphFormat4(const PRUint8 *aBuf, PRUnichar aCh)
{
    const Format4Cmap *cmap4 = reinterpret_cast<const Format4Cmap*>(aBuf);
    PRUint16 segCount;
    const AutoSwap_PRUint16 *endCodes;
    const AutoSwap_PRUint16 *startCodes;
    const AutoSwap_PRUint16 *idDelta;
    const AutoSwap_PRUint16 *idRangeOffset;
    PRUint16 probe;
    PRUint16 rangeShiftOver2;
    PRUint16 index;

// not needed because PRUnichar cannot exceed 0xFFFF
//    if (aCh >= 0x10000) {
//        return 0;
//    }

    segCount = (PRUint16)(cmap4->segCountX2) / 2;

    endCodes = &cmap4->arrays[0];
    startCodes = &cmap4->arrays[segCount + 1]; // +1 for reserved word between arrays
    idDelta = &startCodes[segCount];
    idRangeOffset = &idDelta[segCount];

    probe = 1 << (PRUint16)(cmap4->entrySelector);
    rangeShiftOver2 = (PRUint16)(cmap4->rangeShift) / 2;

    if ((PRUint16)(startCodes[rangeShiftOver2]) <= aCh) {
        index = rangeShiftOver2;
    } else {
        index = 0;
    }

    while (probe > 1) {
        probe >>= 1;
        if ((PRUint16)(startCodes[index + probe]) <= aCh) {
            index += probe;
        }
    }

    if (aCh >= (PRUint16)(startCodes[index]) && aCh <= (PRUint16)(endCodes[index])) {
        PRUint16 result;
        if ((PRUint16)(idRangeOffset[index]) == 0) {
            result = aCh;
        } else {
            PRUint16 offset = aCh - (PRUint16)(startCodes[index]);
            const AutoSwap_PRUint16 *glyphIndexTable =
                (const AutoSwap_PRUint16*)((const char*)&idRangeOffset[index] +
                                           (PRUint16)(idRangeOffset[index]));
            result = glyphIndexTable[offset];
        }

        // note that this is unsigned 16-bit arithmetic, and may wrap around
        result += (PRUint16)(idDelta[index]);
        return result;
    }

    return 0;
}

PRUint32
gfxFontUtils::MapCharToGlyph(PRUint8 *aBuf, PRUint32 aBufLength, PRUnichar aCh)
{
    PRUint32 offset;
    PRBool   symbol;
    PRUint32 format = FindPreferredSubtable(aBuf, aBufLength, &offset, &symbol);

    if (format == 4)
        return MapCharToGlyphFormat4(aBuf + offset, aCh);

    // other formats not currently supported; this is used only for the
    // Mac OS X 10.6 LiGothic font hack (bug 532346)

    return 0;
}

PRUint8 gfxFontUtils::CharRangeBit(PRUint32 ch) {
    const PRUint32 n = sizeof(gUnicodeRanges) / sizeof(struct UnicodeRangeTableEntry);

    for (PRUint32 i = 0; i < n; ++i)
        if (ch >= gUnicodeRanges[i].start && ch <= gUnicodeRanges[i].end)
            return gUnicodeRanges[i].bit;

    return NO_RANGE_FOUND;
}

void gfxFontUtils::GetPrefsFontList(const char *aPrefName, nsTArray<nsString>& aFontList)
{
    const PRUnichar kComma = PRUnichar(',');
    
    aFontList.Clear();
    
    // get the list of single-face font families
    nsCOMPtr<nsIPrefBranch> prefs(do_GetService(NS_PREFSERVICE_CONTRACTID));

    nsAutoString fontlistValue;
    if (prefs) {
        nsCOMPtr<nsISupportsString> prefString;
        prefs->GetComplexValue(aPrefName, NS_GET_IID(nsISupportsString), getter_AddRefs(prefString));
        if (!prefString) 
            return;
        prefString->GetData(fontlistValue);
    }
    
    // append each font name to the list
    nsAutoString fontname;
    nsPromiseFlatString fonts(fontlistValue);
    const PRUnichar *p, *p_end;
    fonts.BeginReading(p);
    fonts.EndReading(p_end);

     while (p < p_end) {
        const PRUnichar *nameStart = p;
        while (++p != p_end && *p != kComma)
        /* nothing */ ;

        // pull out a single name and clean out leading/trailing whitespace        
        fontname = Substring(nameStart, p);
        fontname.CompressWhitespace(PR_TRUE, PR_TRUE);
        
        // append it to the list
        aFontList.AppendElement(fontname);
        ++p;
    }

}

// produce a unique font name that is (1) a valid Postscript name and (2) less
// than 31 characters in length.  Using AddFontMemResourceEx on Windows fails 
// for names longer than 30 characters in length.

#define MAX_B64_LEN 32

nsresult gfxFontUtils::MakeUniqueUserFontName(nsAString& aName)
{
    nsCOMPtr<nsIUUIDGenerator> uuidgen =
      do_GetService("@mozilla.org/uuid-generator;1");
    NS_ENSURE_TRUE(uuidgen, NS_ERROR_OUT_OF_MEMORY);

    nsID guid;

    NS_ASSERTION(sizeof(guid) * 2 <= MAX_B64_LEN, "size of nsID has changed!");

    nsresult rv = uuidgen->GenerateUUIDInPlace(&guid);
    NS_ENSURE_SUCCESS(rv, rv);

    char guidB64[MAX_B64_LEN] = {0};

    if (!PL_Base64Encode(reinterpret_cast<char*>(&guid), sizeof(guid), guidB64))
        return NS_ERROR_FAILURE;

    // all b64 characters except for '/' are allowed in Postscript names, so convert / ==> -
    char *p;
    for (p = guidB64; *p; p++) {
        if (*p == '/')
            *p = '-';
    }

    aName.Assign(NS_LITERAL_STRING("uf"));
    aName.AppendASCII(guidB64);
    return NS_OK;
}


// TrueType/OpenType table handling code

// need byte aligned structs
#pragma pack(1)

struct SFNTHeader {
    AutoSwap_PRUint32    sfntVersion;            // Fixed, 0x00010000 for version 1.0.
    AutoSwap_PRUint16    numTables;              // Number of tables.
    AutoSwap_PRUint16    searchRange;            // (Maximum power of 2 <= numTables) x 16.
    AutoSwap_PRUint16    entrySelector;          // Log2(maximum power of 2 <= numTables).
    AutoSwap_PRUint16    rangeShift;             // NumTables x 16-searchRange.        
};

struct TableDirEntry {
    AutoSwap_PRUint32    tag;                    // 4 -byte identifier.
    AutoSwap_PRUint32    checkSum;               // CheckSum for this table.
    AutoSwap_PRUint32    offset;                 // Offset from beginning of TrueType font file.
    AutoSwap_PRUint32    length;                 // Length of this table.        
};

struct HeadTable {
    enum {
        HEAD_MAGIC_NUMBER = 0x5F0F3CF5,
        HEAD_CHECKSUM_CALC_CONST = 0xB1B0AFBA
    };

    AutoSwap_PRUint32    tableVersionNumber;    // Fixed, 0x00010000 for version 1.0.
    AutoSwap_PRUint32    fontRevision;          // Set by font manufacturer.
    AutoSwap_PRUint32    checkSumAdjustment;    // To compute: set it to 0, sum the entire font as ULONG, then store 0xB1B0AFBA - sum.
    AutoSwap_PRUint32    magicNumber;           // Set to 0x5F0F3CF5.
    AutoSwap_PRUint16    flags;
    AutoSwap_PRUint16    unitsPerEm;            // Valid range is from 16 to 16384. This value should be a power of 2 for fonts that have TrueType outlines.
    AutoSwap_PRUint64    created;               // Number of seconds since 12:00 midnight, January 1, 1904. 64-bit integer
    AutoSwap_PRUint64    modified;              // Number of seconds since 12:00 midnight, January 1, 1904. 64-bit integer
    AutoSwap_PRInt16     xMin;                  // For all glyph bounding boxes.
    AutoSwap_PRInt16     yMin;                  // For all glyph bounding boxes.
    AutoSwap_PRInt16     xMax;                  // For all glyph bounding boxes.
    AutoSwap_PRInt16     yMax;                  // For all glyph bounding boxes.
    AutoSwap_PRUint16    macStyle;              // Bit 0: Bold (if set to 1);
    AutoSwap_PRUint16    lowestRecPPEM;         // Smallest readable size in pixels.
    AutoSwap_PRInt16     fontDirectionHint;
    AutoSwap_PRInt16     indexToLocFormat;
    AutoSwap_PRInt16     glyphDataFormat;
};

// name table stores set of name record structures, followed by
// large block containing all the strings.  name record offset and length
// indicates the offset and length within that block.
// http://www.microsoft.com/typography/otspec/name.htm
struct NameRecordData {
    PRUint32  offset;
    PRUint32  length;
};

struct OS2Table {
    AutoSwap_PRUint16    version;                // 0004 = OpenType 1.5
    AutoSwap_PRInt16     xAvgCharWidth;
    AutoSwap_PRUint16    usWeightClass;
    AutoSwap_PRUint16    usWidthClass;
    AutoSwap_PRUint16    fsType;
    AutoSwap_PRInt16     ySubscriptXSize;
    AutoSwap_PRInt16     ySubscriptYSize;
    AutoSwap_PRInt16     ySubscriptXOffset;
    AutoSwap_PRInt16     ySubscriptYOffset;
    AutoSwap_PRInt16     ySuperscriptXSize;
    AutoSwap_PRInt16     ySuperscriptYSize;
    AutoSwap_PRInt16     ySuperscriptXOffset;
    AutoSwap_PRInt16     ySuperscriptYOffset;
    AutoSwap_PRInt16     yStrikeoutSize;
    AutoSwap_PRInt16     yStrikeoutPosition;
    AutoSwap_PRInt16     sFamilyClass;
    PRUint8              panose[10];
    AutoSwap_PRUint32    unicodeRange1;
    AutoSwap_PRUint32    unicodeRange2;
    AutoSwap_PRUint32    unicodeRange3;
    AutoSwap_PRUint32    unicodeRange4;
    PRUint8              achVendID[4];
    AutoSwap_PRUint16    fsSelection;
    AutoSwap_PRUint16    usFirstCharIndex;
    AutoSwap_PRUint16    usLastCharIndex;
    AutoSwap_PRInt16     sTypoAscender;
    AutoSwap_PRInt16     sTypoDescender;
    AutoSwap_PRInt16     sTypoLineGap;
    AutoSwap_PRUint16    usWinAscent;
    AutoSwap_PRUint16    usWinDescent;
    AutoSwap_PRUint32    codePageRange1;
    AutoSwap_PRUint32    codePageRange2;
    AutoSwap_PRInt16     sxHeight;
    AutoSwap_PRInt16     sCapHeight;
    AutoSwap_PRUint16    usDefaultChar;
    AutoSwap_PRUint16    usBreakChar;
    AutoSwap_PRUint16    usMaxContext;
};

// old 'kern' table, supported on Windows
// see http://www.microsoft.com/typography/otspec/kern.htm
struct KernTableVersion0 {
    AutoSwap_PRUint16    version; // 0x0000
    AutoSwap_PRUint16    nTables;
};

struct KernTableSubtableHeaderVersion0 {
    AutoSwap_PRUint16    version;
    AutoSwap_PRUint16    length;
    AutoSwap_PRUint16    coverage;
};

// newer Mac-only 'kern' table, ignored by Windows
// see http://developer.apple.com/textfonts/TTRefMan/RM06/Chap6kern.html
struct KernTableVersion1 {
    AutoSwap_PRUint32    version; // 0x00010000
    AutoSwap_PRUint32    nTables;
};

struct KernTableSubtableHeaderVersion1 {
    AutoSwap_PRUint32    length;
    AutoSwap_PRUint16    coverage;
    AutoSwap_PRUint16    tupleIndex;
};

static PRBool
IsValidSFNTVersion(PRUint32 version)
{
    // normally 0x00010000, CFF-style OT fonts == 'OTTO' and Apple TT fonts = 'true'
    // 'typ1' is also possible for old Type 1 fonts in a SFNT container but not supported
    return version == 0x10000 ||
           version == TRUETYPE_TAG('O','T','T','O') ||
           version == TRUETYPE_TAG('t','r','u','e');
}

// copy and swap UTF-16 values, assume no surrogate pairs, can be in place
static void
CopySwapUTF16(const PRUint16 *aInBuf, PRUint16 *aOutBuf, PRUint32 aLen)
{
    const PRUint16 *end = aInBuf + aLen;
    while (aInBuf < end) {
        PRUint16 value = *aInBuf;
        *aOutBuf = (value >> 8) | (value & 0xff) << 8;
        aOutBuf++;
        aInBuf++;
    }
}

static PRBool
ValidateKernTable(const PRUint8 *aKernTable, PRUint32 aKernLength)
{
    // -- kern table can cause crashes if invalid, so do some basic sanity-checking
    const KernTableVersion0 *kernTable0 = reinterpret_cast<const KernTableVersion0*>(aKernTable);
    if (aKernLength < sizeof(KernTableVersion0)) {
        return PR_FALSE;
    }
    if (PRUint16(kernTable0->version) == 0) {
        if (aKernLength < sizeof(KernTableVersion0) +
                            PRUint16(kernTable0->nTables) * sizeof(KernTableSubtableHeaderVersion0)) {
            return PR_FALSE;
        }
        // at least the table is big enough to contain the subtable headers;
        // we could go further and check the actual subtable sizes....
        // for now, assume this is OK
        return PR_TRUE;
    }

    const KernTableVersion1 *kernTable1 = reinterpret_cast<const KernTableVersion1*>(aKernTable);
    if (aKernLength < sizeof(KernTableVersion1)) {
        return PR_FALSE;
    }
    if (kernTable1->version == 0x00010000) {
        if (aKernLength < sizeof(KernTableVersion1) +
                            kernTable1->nTables * sizeof(KernTableSubtableHeaderVersion1)) {
            return PR_FALSE;
        }
        // at least the table is big enough to contain the subtable headers;
        // we could go further and check the actual subtable sizes....
        // for now, assume this is OK
        return PR_TRUE;
    }

    // neither the old Windows version nor the newer Apple one; refuse to use it
    return PR_FALSE;
}

gfxUserFontType
gfxFontUtils::DetermineFontDataType(const PRUint8 *aFontData, PRUint32 aFontDataLength)
{
    // test for OpenType font data
    // problem: EOT-Lite with 0x10000 length will look like TrueType!
    if (aFontDataLength >= sizeof(SFNTHeader)) {
        const SFNTHeader *sfntHeader = reinterpret_cast<const SFNTHeader*>(aFontData);
        PRUint32 sfntVersion = sfntHeader->sfntVersion;
        if (IsValidSFNTVersion(sfntVersion)) {
            return GFX_USERFONT_OPENTYPE;
        }
    }
    
    // test for WOFF
    if (aFontDataLength >= sizeof(AutoSwap_PRUint32)) {
        const AutoSwap_PRUint32 *version = 
            reinterpret_cast<const AutoSwap_PRUint32*>(aFontData);
        if (PRUint32(*version) == TRUETYPE_TAG('w','O','F','F')) {
            return GFX_USERFONT_WOFF;
        }
    }
    
    // tests for other formats here
    
    return GFX_USERFONT_UNKNOWN;
}

PRBool
gfxFontUtils::ValidateSFNTHeaders(const PRUint8 *aFontData, 
                                  PRUint32 aFontDataLength)
{
    NS_ASSERTION(aFontData, "null font data");

    PRUint64 dataLength(aFontDataLength);
    
    // read in the sfnt header
    if (sizeof(SFNTHeader) > aFontDataLength) {
        NS_WARNING("invalid font (insufficient data)");
        return PR_FALSE;
    }
    
    const SFNTHeader *sfntHeader = reinterpret_cast<const SFNTHeader*>(aFontData);
    PRUint32 sfntVersion = sfntHeader->sfntVersion;
    if (!IsValidSFNTVersion(sfntVersion)) {
        NS_WARNING("invalid font (SFNT version)");
        return PR_FALSE;
    }
    
    // iterate through the table headers to find the head, name and OS/2 tables
    PRBool foundHead = PR_FALSE, foundOS2 = PR_FALSE, foundName = PR_FALSE;
    PRBool foundGlyphs = PR_FALSE, foundCFF = PR_FALSE, foundKern = PR_FALSE;
    PRUint32 headOffset, headLen, nameOffset, nameLen, kernOffset, kernLen;
    PRUint32 i, numTables;

    numTables = sfntHeader->numTables;
    PRUint32 headerLen = sizeof(SFNTHeader) + sizeof(TableDirEntry) * numTables;
    if (headerLen > aFontDataLength) {
        NS_WARNING("invalid font (table directory)");
        return PR_FALSE;
    }
    
    // table directory entries begin immediately following SFNT header
    const TableDirEntry *dirEntry = 
        reinterpret_cast<const TableDirEntry*>(aFontData + sizeof(SFNTHeader));
    PRUint32 checksum = 0;
    
    // checksum for font = (checksum of header) + (checksum of tables)
    const AutoSwap_PRUint32 *headerData = 
        reinterpret_cast<const AutoSwap_PRUint32*>(aFontData);

    // header length is in bytes, checksum calculated in longwords
    for (i = 0; i < (headerLen >> 2); i++, headerData++) {
        checksum += *headerData;
    }
    
    for (i = 0; i < numTables; i++, dirEntry++) {
    
        // sanity check on offset, length values
        if (PRUint64(dirEntry->offset) + PRUint64(dirEntry->length) > dataLength) {
            NS_WARNING("invalid font (table directory entry)");
            return PR_FALSE;
        }

        checksum += dirEntry->checkSum;
        
        switch (dirEntry->tag) {

        case TRUETYPE_TAG('h','e','a','d'):
            foundHead = PR_TRUE;
            headOffset = dirEntry->offset;
            headLen = dirEntry->length;
            if (headLen < sizeof(HeadTable)) {
                NS_WARNING("invalid font (head table length)");
                return PR_FALSE;
            }
            break;

        case TRUETYPE_TAG('k','e','r','n'):
            foundKern = PR_TRUE;
            kernOffset = dirEntry->offset;
            kernLen = dirEntry->length;
            break;

        case TRUETYPE_TAG('n','a','m','e'):
            foundName = PR_TRUE;
            nameOffset = dirEntry->offset;
            nameLen = dirEntry->length;
            break;

        case TRUETYPE_TAG('O','S','/','2'):
            foundOS2 = PR_TRUE;
            break;

        case TRUETYPE_TAG('g','l','y','f'):  // TrueType-style quadratic glyph table
            foundGlyphs = PR_TRUE;
            break;

        case TRUETYPE_TAG('C','F','F',' '):  // PS-style cubic glyph table
            foundCFF = PR_TRUE;
            break;

        default:
            break;
        }

    }

    // simple sanity checks
    
    // -- fonts need head, name tables
    if (!foundHead || !foundName) {
        NS_WARNING("invalid font (missing head/name table)");
        return PR_FALSE;
    }
    
    // -- on Windows need OS/2 table
#ifdef XP_WIN
    if (!foundOS2) {
        NS_WARNING("invalid font (missing OS/2 table)");
        return PR_FALSE;
    }
#endif

    // -- head table data
    const HeadTable *headData = reinterpret_cast<const HeadTable*>(aFontData + headOffset);

    if (headData->magicNumber != HeadTable::HEAD_MAGIC_NUMBER) {
        NS_WARNING("invalid font (head magic number)");
        return PR_FALSE;
    }

    if (headData->checkSumAdjustment != (HeadTable::HEAD_CHECKSUM_CALC_CONST - checksum)) {
        NS_WARNING("invalid font (bad checksum)");
        // Bug 483459 - warn about a bad checksum but allow the font to be 
        // used, since a small percentage of fonts don't calculate this 
        // correctly and font systems aren't fussy about this
        // return PR_FALSE;
    }
    
    // need glyf or CFF table based on sfnt version
    if (sfntVersion == TRUETYPE_TAG('O','T','T','O')) {
        if (!foundCFF) {
            NS_WARNING("invalid font (missing CFF table)");
            return PR_FALSE;
        }
    } else {
        if (!foundGlyphs) {
            NS_WARNING("invalid font (missing glyf table)");
            return PR_FALSE;
        }
    }
    
    // -- name table data
    const NameHeader *nameHeader = reinterpret_cast<const NameHeader*>(aFontData + nameOffset);

    PRUint32 nameCount = nameHeader->count;

    // -- sanity check the number of name records
    if (PRUint64(nameCount) * sizeof(NameRecord) + PRUint64(nameOffset) > dataLength) {
        NS_WARNING("invalid font (name records)");
        return PR_FALSE;
    }
    
    // -- iterate through name records
    const NameRecord *nameRecord = reinterpret_cast<const NameRecord*>
                                       (aFontData + nameOffset + sizeof(NameHeader));
    PRUint64 nameStringsBase = PRUint64(nameOffset) + PRUint64(nameHeader->stringOffset);

    for (i = 0; i < nameCount; i++, nameRecord++) {
        PRUint32 namelen = nameRecord->length;
        PRUint32 nameoff = nameRecord->offset;  // offset from base of string storage

        if (nameStringsBase + PRUint64(nameoff) + PRUint64(namelen) > dataLength) {
            NS_WARNING("invalid font (name table strings)");
            return PR_FALSE;
        }
    }

    // -- sanity-check the kern table, if present (see bug 487549)
    if (foundKern) {
        if (!ValidateKernTable(aFontData + kernOffset, kernLen)) {
            NS_WARNING("invalid font (kern table)");
            return PR_FALSE;
        }
    }

    // everything seems consistent
    return PR_TRUE;
}

nsresult
gfxFontUtils::RenameFont(const nsAString& aName, const PRUint8 *aFontData, 
                         PRUint32 aFontDataLength, nsTArray<PRUint8> *aNewFont)
{
    NS_ASSERTION(aNewFont, "null font data array");
    
    PRUint64 dataLength(aFontDataLength);

    // new name table
    static const PRUint32 neededNameIDs[] = {NAME_ID_FAMILY, 
                                             NAME_ID_STYLE,
                                             NAME_ID_UNIQUE,
                                             NAME_ID_FULL,
                                             NAME_ID_POSTSCRIPT};

    // calculate new name table size
    PRUint16 nameCount = NS_ARRAY_LENGTH(neededNameIDs);

    // leave room for null-terminator
    PRUint16 nameStrLength = (aName.Length() + 1) * sizeof(PRUnichar); 

    // round name table size up to 4-byte multiple
    PRUint32 nameTableSize = (sizeof(NameHeader) +
                              sizeof(NameRecord) * nameCount +
                              nameStrLength +
                              3) & ~3;
                              
    if (dataLength + nameTableSize > PR_UINT32_MAX)
        return NS_ERROR_FAILURE;
        
    PRUint32 adjFontDataSize = aFontDataLength + nameTableSize;
    
    // create new buffer: old font data plus new name table
    if (!aNewFont->AppendElements(adjFontDataSize))
        return NS_ERROR_OUT_OF_MEMORY;

    // copy the old font data
    PRUint8 *newFontData = reinterpret_cast<PRUint8*>(aNewFont->Elements());
    
    memcpy(newFontData, aFontData, aFontDataLength);
    
    // null out the last 4 bytes for checksum calculations
    memset(newFontData + adjFontDataSize - 4, 0, 4);
    
    NameHeader *nameHeader = reinterpret_cast<NameHeader*>(newFontData +
                                                            aFontDataLength);
    
    // -- name header
    nameHeader->format = 0;
    nameHeader->count = nameCount;
    nameHeader->stringOffset = sizeof(NameHeader) + nameCount * sizeof(NameRecord);
    
    // -- name records
    PRUint32 i;
    NameRecord *nameRecord = reinterpret_cast<NameRecord*>(nameHeader + 1);
    
    for (i = 0; i < nameCount; i++, nameRecord++) {
        nameRecord->platformID = PLATFORM_ID_MICROSOFT;
        nameRecord->encodingID = ENCODING_ID_MICROSOFT_UNICODEBMP;
        nameRecord->languageID = LANG_ID_MICROSOFT_EN_US;
        nameRecord->nameID = neededNameIDs[i];
        nameRecord->offset = 0;
        nameRecord->length = nameStrLength;
    }
    
    // -- string data, located after the name records, stored in big-endian form
    PRUnichar *strData = reinterpret_cast<PRUnichar*>(nameRecord);

    const PRUnichar *nameStr = aName.BeginReading();
    const PRUnichar *nameStrEnd = aName.EndReading();
    while (nameStr < nameStrEnd) {
        PRUnichar ch = *nameStr++;
        *strData++ = NS_SWAP16(ch);
    }
    *strData = 0; // add null termination
    
    // adjust name table header to point to the new name table
    SFNTHeader *sfntHeader = reinterpret_cast<SFNTHeader*>(newFontData);

    // table directory entries begin immediately following SFNT header
    TableDirEntry *dirEntry = 
        reinterpret_cast<TableDirEntry*>(newFontData + sizeof(SFNTHeader));

    PRUint32 numTables = sfntHeader->numTables;
    PRBool foundName = PR_FALSE;
    
    for (i = 0; i < numTables; i++, dirEntry++) {
        if (dirEntry->tag == TRUETYPE_TAG('n','a','m','e')) {
            foundName = PR_TRUE;
            break;
        }
    }
    
    // function only called if font validates, so this should always be true
    NS_ASSERTION(foundName, "attempt to rename font with no name table");

    // note: dirEntry now points to name record
    
    // recalculate name table checksum
    PRUint32 checkSum = 0;
    AutoSwap_PRUint32 *nameData = reinterpret_cast<AutoSwap_PRUint32*> (nameHeader);
    AutoSwap_PRUint32 *nameDataEnd = nameData + (nameTableSize >> 2);
    
    while (nameData < nameDataEnd)
        checkSum = checkSum + *nameData++;
    
    // adjust name table entry to point to new name table
    dirEntry->offset = aFontDataLength;
    dirEntry->length = nameTableSize;
    dirEntry->checkSum = checkSum;
    
    // fix up checksums
    PRUint32 checksum = 0;
    
    // checksum for font = (checksum of header) + (checksum of tables)
    PRUint32 headerLen = sizeof(SFNTHeader) + sizeof(TableDirEntry) * numTables;
    const AutoSwap_PRUint32 *headerData = 
        reinterpret_cast<const AutoSwap_PRUint32*>(newFontData);

    // header length is in bytes, checksum calculated in longwords
    for (i = 0; i < (headerLen >> 2); i++, headerData++) {
        checksum += *headerData;
    }
    
    PRUint32 headOffset = 0;
    dirEntry = reinterpret_cast<TableDirEntry*>(newFontData + sizeof(SFNTHeader));

    for (i = 0; i < numTables; i++, dirEntry++) {
        if (dirEntry->tag == TRUETYPE_TAG('h','e','a','d')) {
            headOffset = dirEntry->offset;
        }
        checksum += dirEntry->checkSum;
    }
    
    NS_ASSERTION(headOffset != 0, "no head table for font");
    
    HeadTable *headData = reinterpret_cast<HeadTable*>(newFontData + headOffset);

    headData->checkSumAdjustment = HeadTable::HEAD_CHECKSUM_CALC_CONST - checksum;

    return NS_OK;
}

enum {
#if defined(XP_MACOSX)
    CANONICAL_LANG_ID = gfxFontUtils::LANG_ID_MAC_ENGLISH,
    PLATFORM_ID       = gfxFontUtils::PLATFORM_ID_MAC
#else
    CANONICAL_LANG_ID = gfxFontUtils::LANG_ID_MICROSOFT_EN_US,
    PLATFORM_ID       = gfxFontUtils::PLATFORM_ID_MICROSOFT
#endif
};    

nsresult
gfxFontUtils::ReadNames(nsTArray<PRUint8>& aNameTable, PRUint32 aNameID, 
                        PRInt32 aPlatformID, nsTArray<nsString>& aNames)
{
    return ReadNames(aNameTable, aNameID, LANG_ALL, aPlatformID, aNames);
}

nsresult
gfxFontUtils::ReadCanonicalName(nsTArray<PRUint8>& aNameTable, PRUint32 aNameID, 
                                nsString& aName)
{
    nsresult rv;
    
    nsTArray<nsString> names;
    
    // first, look for the English name (this will succeed 99% of the time)
    rv = ReadNames(aNameTable, aNameID, CANONICAL_LANG_ID, PLATFORM_ID, names);
    NS_ENSURE_SUCCESS(rv, rv);
        
    // otherwise, grab names for all languages
    if (names.Length() == 0) {
        rv = ReadNames(aNameTable, aNameID, LANG_ALL, PLATFORM_ID, names);
        NS_ENSURE_SUCCESS(rv, rv);
    }
    
#if defined(XP_MACOSX)
    // may be dealing with font that only has Microsoft name entries
    if (names.Length() == 0) {
        rv = ReadNames(aNameTable, aNameID, LANG_ID_MICROSOFT_EN_US, 
                       PLATFORM_ID_MICROSOFT, names);
        NS_ENSURE_SUCCESS(rv, rv);
        
        // getting really desperate now, take anything!
        if (names.Length() == 0) {
            rv = ReadNames(aNameTable, aNameID, LANG_ALL, 
                           PLATFORM_ID_MICROSOFT, names);
            NS_ENSURE_SUCCESS(rv, rv);
        }
    }
#endif

    // return the first name (99.9% of the time names will
    // contain a single English name)
    if (names.Length()) {
        aName.Assign(names[0]);
        return NS_OK;
    }
        
    return NS_ERROR_FAILURE;
}

// Charsets to use for decoding Mac platform font names.
// This table is sorted by {encoding, language}, with the wildcard "ANY" being
// greater than any defined values for each field; we use a binary search on both
// fields, and fall back to matching only encoding if necessary

// Some "redundant" entries for specific combinations are included such as
// encoding=roman, lang=english, in order that common entries will be found
// on the first search.

#define ANY 0xffff
const gfxFontUtils::MacFontNameCharsetMapping gfxFontUtils::gMacFontNameCharsets[] =
{
    { ENCODING_ID_MAC_ROMAN,        LANG_ID_MAC_ENGLISH,      "x-mac-roman"     },
    { ENCODING_ID_MAC_ROMAN,        LANG_ID_MAC_ICELANDIC,    "x-mac-icelandic" },
    { ENCODING_ID_MAC_ROMAN,        LANG_ID_MAC_TURKISH,      "x-mac-turkish"   },
    { ENCODING_ID_MAC_ROMAN,        LANG_ID_MAC_POLISH,       "x-mac-ce"        },
    { ENCODING_ID_MAC_ROMAN,        LANG_ID_MAC_ROMANIAN,     "x-mac-romanian"  },
    { ENCODING_ID_MAC_ROMAN,        LANG_ID_MAC_CZECH,        "x-mac-ce"        },
    { ENCODING_ID_MAC_ROMAN,        LANG_ID_MAC_SLOVAK,       "x-mac-ce"        },
    { ENCODING_ID_MAC_ROMAN,        ANY,                      "x-mac-roman"     },
    { ENCODING_ID_MAC_JAPANESE,     LANG_ID_MAC_JAPANESE,     "Shift_JIS"       },
    { ENCODING_ID_MAC_JAPANESE,     ANY,                      "Shift_JIS"       },
    { ENCODING_ID_MAC_TRAD_CHINESE, LANG_ID_MAC_TRAD_CHINESE, "Big5"            },
    { ENCODING_ID_MAC_TRAD_CHINESE, ANY,                      "Big5"            },
    { ENCODING_ID_MAC_KOREAN,       LANG_ID_MAC_KOREAN,       "EUC-KR"          },
    { ENCODING_ID_MAC_KOREAN,       ANY,                      "EUC-KR"          },
    { ENCODING_ID_MAC_ARABIC,       LANG_ID_MAC_ARABIC,       "x-mac-arabic"    },
    { ENCODING_ID_MAC_ARABIC,       LANG_ID_MAC_URDU,         "x-mac-farsi"     },
    { ENCODING_ID_MAC_ARABIC,       LANG_ID_MAC_FARSI,        "x-mac-farsi"     },
    { ENCODING_ID_MAC_ARABIC,       ANY,                      "x-mac-arabic"    },
    { ENCODING_ID_MAC_HEBREW,       LANG_ID_MAC_HEBREW,       "x-mac-hebrew"    },
    { ENCODING_ID_MAC_HEBREW,       ANY,                      "x-mac-hebrew"    },
    { ENCODING_ID_MAC_GREEK,        ANY,                      "x-mac-greek"     },
    { ENCODING_ID_MAC_CYRILLIC,     ANY,                      "x-mac-cyrillic"  },
    { ENCODING_ID_MAC_DEVANAGARI,   ANY,                      "x-mac-devanagari"},
    { ENCODING_ID_MAC_GURMUKHI,     ANY,                      "x-mac-gurmukhi"  },
    { ENCODING_ID_MAC_GUJARATI,     ANY,                      "x-mac-gujarati"  },
    { ENCODING_ID_MAC_SIMP_CHINESE, LANG_ID_MAC_SIMP_CHINESE, "GB2312"          },
    { ENCODING_ID_MAC_SIMP_CHINESE, ANY,                      "GB2312"          }
};

const char* gfxFontUtils::gISOFontNameCharsets[] = 
{
    /* 0 */ "us-ascii"   ,
    /* 1 */ nsnull       , /* spec says "ISO 10646" but does not specify encoding form! */
    /* 2 */ "ISO-8859-1"
};

const char* gfxFontUtils::gMSFontNameCharsets[] =
{
    /* [0] ENCODING_ID_MICROSOFT_SYMBOL */      ""          ,
    /* [1] ENCODING_ID_MICROSOFT_UNICODEBMP */  ""          ,
    /* [2] ENCODING_ID_MICROSOFT_SHIFTJIS */    "Shift_JIS" ,
    /* [3] ENCODING_ID_MICROSOFT_PRC */         nsnull      ,
    /* [4] ENCODING_ID_MICROSOFT_BIG5 */        "Big5"      ,
    /* [5] ENCODING_ID_MICROSOFT_WANSUNG */     nsnull      ,
    /* [6] ENCODING_ID_MICROSOFT_JOHAB */       "x-johab"   ,
    /* [7] reserved */                          nsnull      ,
    /* [8] reserved */                          nsnull      ,
    /* [9] reserved */                          nsnull      ,
    /*[10] ENCODING_ID_MICROSOFT_UNICODEFULL */ ""
};

#define ARRAY_SIZE(A) (sizeof(A) / sizeof(A[0]))

// Return the name of the charset we should use to decode a font name
// given the name table attributes.
// Special return values:
//    ""       charset is UTF16BE, no need for a converter
//    nsnull   unknown charset, do not attempt conversion
const char*
gfxFontUtils::GetCharsetForFontName(PRUint16 aPlatform, PRUint16 aScript, PRUint16 aLanguage)
{
    switch (aPlatform)
    {
    case PLATFORM_ID_UNICODE:
        return "";

    case PLATFORM_ID_MAC:
        {
            PRUint32 lo = 0, hi = ARRAY_SIZE(gMacFontNameCharsets);
            MacFontNameCharsetMapping searchValue = { aScript, aLanguage, nsnull };
            for (PRUint32 i = 0; i < 2; ++i) {
                // binary search; if not found, set language to ANY and try again
                while (lo < hi) {
                    PRUint32 mid = (lo + hi) / 2;
                    const MacFontNameCharsetMapping& entry = gMacFontNameCharsets[mid];
                    if (entry < searchValue) {
                        lo = mid + 1;
                        continue;
                    }
                    if (searchValue < entry) {
                        hi = mid;
                        continue;
                    }
                    // found a match
                    return entry.mCharsetName;
                }

                // no match, so reset high bound for search and re-try
                hi = ARRAY_SIZE(gMacFontNameCharsets);
                searchValue.mLanguage = ANY;
            }
        }
        break;

    case PLATFORM_ID_ISO:
        if (aScript < ARRAY_SIZE(gISOFontNameCharsets)) {
            return gISOFontNameCharsets[aScript];
        }
        break;

    case PLATFORM_ID_MICROSOFT:
        if (aScript < ARRAY_SIZE(gMSFontNameCharsets)) {
            return gMSFontNameCharsets[aScript];
        }
        break;
    }

    return nsnull;
}

// convert a raw name from the name table to an nsString, if possible;
// return value indicates whether conversion succeeded
PRBool
gfxFontUtils::DecodeFontName(const PRUint8 *aNameData, PRInt32 aByteLen, 
                             PRUint32 aPlatformCode, PRUint32 aScriptCode,
                             PRUint32 aLangCode, nsAString& aName)
{
    NS_ASSERTION(aByteLen > 0, "bad length for font name data");

    const char *csName = GetCharsetForFontName(aPlatformCode, aScriptCode, aLangCode);

    if (!csName) {
        // nsnull -> unknown charset
#ifdef DEBUG
        char warnBuf[128];
        if (aByteLen > 64)
            aByteLen = 64;
        sprintf(warnBuf, "skipping font name, unknown charset %d:%d:%d for <%.*s>",
                aPlatformCode, aScriptCode, aLangCode, aByteLen, aNameData);
        NS_WARNING(warnBuf);
#endif
        return PR_FALSE;
    }

    if (csName[0] == 0) {
        // empty charset name: data is utf16be, no need to instantiate a converter
        PRUint32 strLen = aByteLen / 2;
#ifdef IS_LITTLE_ENDIAN
        aName.SetLength(strLen);
        CopySwapUTF16(reinterpret_cast<const PRUint16*>(aNameData),
                      reinterpret_cast<PRUint16*>(aName.BeginWriting()), strLen);
#else
        aName.Assign(reinterpret_cast<const PRUnichar*>(aNameData), strLen);
#endif    
        return PR_TRUE;
    }

    nsresult rv;
    nsCOMPtr<nsICharsetConverterManager_1_9_BRANCH> ccm =
        do_GetService(NS_CHARSETCONVERTERMANAGER_CONTRACTID, &rv);
    NS_ASSERTION(NS_SUCCEEDED(rv), "failed to get charset converter manager");
    if (NS_FAILED(rv)) {
        return PR_FALSE;
    }

    nsCOMPtr<nsIUnicodeDecoder> decoder;
    rv = ccm->GetUnicodeDecoderRawInternal(csName, getter_AddRefs(decoder));
    if (NS_FAILED(rv)) {
        NS_WARNING("failed to get the decoder for a font name string");
        return PR_FALSE;
    }

    PRInt32 destLength;
    rv = decoder->GetMaxLength(reinterpret_cast<const char*>(aNameData), aByteLen, &destLength);
    if (NS_FAILED(rv)) {
        NS_WARNING("decoder->GetMaxLength failed, invalid font name?");
        return PR_FALSE;
    }

    // make space for the converted string
    aName.SetLength(destLength);
    rv = decoder->Convert(reinterpret_cast<const char*>(aNameData), &aByteLen,
                          aName.BeginWriting(), &destLength);
    if (NS_FAILED(rv)) {
        NS_WARNING("decoder->Convert failed, invalid font name?");
        return PR_FALSE;
    }
    aName.Truncate(destLength); // set the actual length

    return PR_TRUE;
}

nsresult
gfxFontUtils::ReadNames(nsTArray<PRUint8>& aNameTable, PRUint32 aNameID, 
                        PRInt32 aLangID, PRInt32 aPlatformID,
                        nsTArray<nsString>& aNames)
{
    PRUint32 nameTableLen = aNameTable.Length();
    NS_ASSERTION(nameTableLen != 0, "null name table");

    if (nameTableLen == 0)
        return NS_ERROR_FAILURE;

    PRUint8 *nameTable = aNameTable.Elements();

    // -- name table data
    const NameHeader *nameHeader = reinterpret_cast<const NameHeader*>(nameTable);

    PRUint32 nameCount = nameHeader->count;

    // -- sanity check the number of name records
    if (PRUint64(nameCount) * sizeof(NameRecord) > nameTableLen) {
        NS_WARNING("invalid font (name table data)");
        return NS_ERROR_FAILURE;
    }
    
    // -- iterate through name records
    const NameRecord *nameRecord 
        = reinterpret_cast<const NameRecord*>(nameTable + sizeof(NameHeader));
    PRUint64 nameStringsBase = PRUint64(nameHeader->stringOffset);

    PRUint32 i;
    for (i = 0; i < nameCount; i++, nameRecord++) {
        PRUint32 platformID;
        
        // skip over unwanted nameID's
        if (PRUint32(nameRecord->nameID) != aNameID)
            continue;

        // skip over unwanted platform data
        platformID = nameRecord->platformID;
        if (aPlatformID != PLATFORM_ALL 
            && PRUint32(nameRecord->platformID) != PLATFORM_ID)
            continue;
            
        // skip over unwanted languages
        if (aLangID != LANG_ALL 
              && PRUint32(nameRecord->languageID) != PRUint32(aLangID))
            continue;
        
        // add name to names array
        
        // -- calculate string location
        PRUint32 namelen = nameRecord->length;
        PRUint32 nameoff = nameRecord->offset;  // offset from base of string storage

        if (nameStringsBase + PRUint64(nameoff) + PRUint64(namelen) 
                > nameTableLen) {
            NS_WARNING("invalid font (name table strings)");
            return NS_ERROR_FAILURE;
        }
        
        // -- decode if necessary and make nsString
        nsAutoString name;
        nsresult rv;
        
        rv = DecodeFontName(nameTable + nameStringsBase + nameoff, namelen, 
                            platformID, PRUint32(nameRecord->encodingID),
                            PRUint32(nameRecord->languageID), name);
        
        if (NS_FAILED(rv))
            continue;
            
        PRUint32 k, numNames;
        PRBool foundName = PR_FALSE;
        
        numNames = aNames.Length();
        for (k = 0; k < numNames; k++) {
            if (name.Equals(aNames[k])) {
                foundName = PR_TRUE;
                break;
            }    
        }
        
        if (!foundName)
            aNames.AppendElement(name);                          

    }

    return NS_OK;
}

// Embedded OpenType (EOT) handling
// needed for dealing with downloadable fonts on Windows
//
// EOT version 0x00020001
// based on http://www.w3.org/Submission/2008/SUBM-EOT-20080305/
//
// EOT header consists of a fixed-size portion containing general font
// info, followed by a variable-sized portion containing name data,
// followed by the actual TT/OT font data (non-byte values are always
// stored in big-endian format)
//
// EOT header is stored in *little* endian order!!

struct EOTFixedHeader {

    PRUint32      eotSize;            // Total structure length in PRUint8s (including string and font data)
    PRUint32      fontDataSize;       // Length of the OpenType font (FontData) in PRUint8s
    PRUint32      version;            // Version number of this format - 0x00010000
    PRUint32      flags;              // Processing Flags
    PRUint8       panose[10];         // The PANOSE value for this font - See http://www.microsoft.com/typography/otspec/os2.htm#pan
    PRUint8       charset;            // In Windows this is derived from TEXTMETRIC.tmCharSet. This value specifies the character set of the font. DEFAULT_CHARSET (0x01) indicates no preference. - See http://msdn2.microsoft.com/en-us/library/ms534202.aspx
    PRUint8       italic;             // If the bit for ITALIC is set in OS/2.fsSelection, the value will be 0x01 - See http://www.microsoft.com/typography/otspec/os2.htm#fss
    PRUint32      weight;             // The weight value for this font - See http://www.microsoft.com/typography/otspec/os2.htm#wtc
    PRUint16      fsType;             // Type flags that provide information about embedding permissions - See http://www.microsoft.com/typography/otspec/os2.htm#fst
    PRUint16      magicNumber;        // Magic number for EOT file - 0x504C. Used to check for data corruption.
    PRUint32      unicodeRange1;      // OS/2.UnicodeRange1 (bits 0-31) - See http://www.microsoft.com/typography/otspec/os2.htm#ur
    PRUint32      unicodeRange2;      // OS/2.UnicodeRange2 (bits 32-63) - See http://www.microsoft.com/typography/otspec/os2.htm#ur
    PRUint32      unicodeRange3;      // OS/2.UnicodeRange3 (bits 64-95) - See http://www.microsoft.com/typography/otspec/os2.htm#ur
    PRUint32      unicodeRange4;      // OS/2.UnicodeRange4 (bits 96-127) - See http://www.microsoft.com/typography/otspec/os2.htm#ur
    PRUint32      codePageRange1;     // CodePageRange1 (bits 0-31) - See http://www.microsoft.com/typography/otspec/os2.htm#cpr
    PRUint32      codePageRange2;     // CodePageRange2 (bits 32-63) - See http://www.microsoft.com/typography/otspec/os2.htm#cpr
    PRUint32      checkSumAdjustment; // head.CheckSumAdjustment - See http://www.microsoft.com/typography/otspec/head.htm
    PRUint32      reserved[4];        // Reserved - must be 0
    PRUint16      padding1;           // Padding to maintain long alignment. Padding value must always be set to 0x0000.

    enum {
        EOT_VERSION = 0x00020001,
        EOT_MAGIC_NUMBER = 0x504c,
        EOT_DEFAULT_CHARSET = 0x01,
        EOT_EMBED_PRINT_PREVIEW = 0x0004,
        EOT_FAMILY_NAME_INDEX = 0,    // order of names in variable portion of EOT header
        EOT_STYLE_NAME_INDEX = 1,
        EOT_VERSION_NAME_INDEX = 2,
        EOT_FULL_NAME_INDEX = 3,
        EOT_NUM_NAMES = 4
    };

};

// EOT headers are only used on Windows

#ifdef XP_WIN

// EOT variable-sized header (version 0x00020001 - contains 4 name
// fields, each with the structure):
//
//   // number of bytes in the name array
//   PRUint16 size;
//   // array of UTF-16 chars, total length = <size> bytes
//   // note: english version of name record string
//   PRUint8  name[size]; 
//
// This structure is used for the following names, each separated by two
// bytes of padding (always 0 with no padding after the rootString):
//
//   familyName  - based on name ID = 1
//   styleName   - based on name ID = 2
//   versionName - based on name ID = 5
//   fullName    - based on name ID = 4
//   rootString  - used to restrict font usage to a specific domain
//

#if DEBUG
static void 
DumpEOTHeader(PRUint8 *aHeader, PRUint32 aHeaderLen)
{
    PRUint32 offset = 0;
    PRUint8 *ch = aHeader;

    printf("\n\nlen == %d\n\n", aHeaderLen);
    while (offset < aHeaderLen) {
        printf("%7.7x    ", offset);
        int i;
        for (i = 0; i < 16; i++) {
            printf("%2.2x  ", *ch++);
        }
        printf("\n");
        offset += 16;
    }
}
#endif

nsresult
gfxFontUtils::MakeEOTHeader(const PRUint8 *aFontData, PRUint32 aFontDataLength,
                            nsTArray<PRUint8> *aHeader, FontDataOverlay *aOverlay)
{
    NS_ASSERTION(aFontData && aFontDataLength != 0, "null font data");
    NS_ASSERTION(aHeader, "null header");
    NS_ASSERTION(aHeader->Length() == 0, "non-empty header passed in");
    NS_ASSERTION(aOverlay, "null font overlay struct passed in");

    aOverlay->overlaySrc = 0;
    
    if (!aHeader->AppendElements(sizeof(EOTFixedHeader)))
        return NS_ERROR_OUT_OF_MEMORY;

    EOTFixedHeader *eotHeader = reinterpret_cast<EOTFixedHeader*>(aHeader->Elements());
    memset(eotHeader, 0, sizeof(EOTFixedHeader));

    PRUint32 fontDataSize = aFontDataLength;

    // set up header fields
    eotHeader->fontDataSize = fontDataSize;
    eotHeader->version = EOTFixedHeader::EOT_VERSION;
    eotHeader->flags = 0;  // don't specify any special processing
    eotHeader->charset = EOTFixedHeader::EOT_DEFAULT_CHARSET;
    eotHeader->fsType = EOTFixedHeader::EOT_EMBED_PRINT_PREVIEW;
    eotHeader->magicNumber = EOTFixedHeader::EOT_MAGIC_NUMBER;

    // read in the sfnt header
    if (sizeof(SFNTHeader) > aFontDataLength)
        return NS_ERROR_FAILURE;
    
    const SFNTHeader *sfntHeader = reinterpret_cast<const SFNTHeader*>(aFontData);
    if (!IsValidSFNTVersion(sfntHeader->sfntVersion))
        return NS_ERROR_FAILURE;

    // iterate through the table headers to find the head, name and OS/2 tables
    PRBool foundHead = PR_FALSE, foundOS2 = PR_FALSE, foundName = PR_FALSE, foundGlyphs = PR_FALSE;
    PRUint32 headOffset, headLen, nameOffset, nameLen, os2Offset, os2Len;
    PRUint32 i, numTables;

    numTables = sfntHeader->numTables;
    if (sizeof(SFNTHeader) + sizeof(TableDirEntry) * numTables > aFontDataLength)
        return NS_ERROR_FAILURE;
    
    PRUint64 dataLength(aFontDataLength);
    
    // table directory entries begin immediately following SFNT header
    const TableDirEntry *dirEntry = reinterpret_cast<const TableDirEntry*>(aFontData + sizeof(SFNTHeader));
    
    for (i = 0; i < numTables; i++, dirEntry++) {
    
        // sanity check on offset, length values
        if (PRUint64(dirEntry->offset) + PRUint64(dirEntry->length) > dataLength)
            return NS_ERROR_FAILURE;

        switch (dirEntry->tag) {

        case TRUETYPE_TAG('h','e','a','d'):
            foundHead = PR_TRUE;
            headOffset = dirEntry->offset;
            headLen = dirEntry->length;
            if (headLen < sizeof(HeadTable))
                return NS_ERROR_FAILURE;
            break;

        case TRUETYPE_TAG('n','a','m','e'):
            foundName = PR_TRUE;
            nameOffset = dirEntry->offset;
            nameLen = dirEntry->length;
            break;

        case TRUETYPE_TAG('O','S','/','2'):
            foundOS2 = PR_TRUE;
            os2Offset = dirEntry->offset;
            os2Len = dirEntry->length;
            break;

        case TRUETYPE_TAG('g','l','y','f'):  // TrueType-style quadratic glyph table
            foundGlyphs = PR_TRUE;
            break;

        case TRUETYPE_TAG('C','F','F',' '):  // PS-style cubic glyph table
            foundGlyphs = PR_TRUE;
            break;

        default:
            break;
        }

        if (foundHead && foundName && foundOS2 && foundGlyphs)
            break;
    }

    // require these three tables on Windows
    if (!foundHead || !foundName || !foundOS2)
        return NS_ERROR_FAILURE;

    // at this point, all table offset/length values are within bounds
    
    // read in the data from those tables

    // -- head table data
    const HeadTable  *headData = reinterpret_cast<const HeadTable*>(aFontData + headOffset);

    if (headData->magicNumber != HeadTable::HEAD_MAGIC_NUMBER)
        return NS_ERROR_FAILURE;

    eotHeader->checkSumAdjustment = headData->checkSumAdjustment;

    // -- name table data

    // -- first, read name table header
    const NameHeader *nameHeader = reinterpret_cast<const NameHeader*>(aFontData + nameOffset);
    PRUint32 nameStringsBase = PRUint32(nameHeader->stringOffset);

    PRUint32 nameCount = nameHeader->count;

    // -- sanity check the number of name records
    if (PRUint64(nameCount) * sizeof(NameRecord) + PRUint64(nameOffset) > dataLength)
        return NS_ERROR_FAILURE;
    
    // -- iterate through name records, look for specific name ids with
    //    matching platform/encoding/etc. and store offset/lengths
    NameRecordData names[EOTFixedHeader::EOT_NUM_NAMES] = {0};
    const NameRecord *nameRecord = reinterpret_cast<const NameRecord*>(aFontData + nameOffset + sizeof(NameHeader));
    PRUint32 needNames = (1 << EOTFixedHeader::EOT_FAMILY_NAME_INDEX) | 
                         (1 << EOTFixedHeader::EOT_STYLE_NAME_INDEX) | 
                         (1 << EOTFixedHeader::EOT_FULL_NAME_INDEX) | 
                         (1 << EOTFixedHeader::EOT_VERSION_NAME_INDEX);

    for (i = 0; i < nameCount; i++, nameRecord++) {

        // looking for Microsoft English US name strings, skip others
        if (PRUint32(nameRecord->platformID) != PLATFORM_ID_MICROSOFT || 
                PRUint32(nameRecord->encodingID) != ENCODING_ID_MICROSOFT_UNICODEBMP || 
                PRUint32(nameRecord->languageID) != LANG_ID_MICROSOFT_EN_US)
            continue;

        switch ((PRUint32)nameRecord->nameID) {

        case NAME_ID_FAMILY:
            names[EOTFixedHeader::EOT_FAMILY_NAME_INDEX].offset = nameRecord->offset;
            names[EOTFixedHeader::EOT_FAMILY_NAME_INDEX].length = nameRecord->length;
            needNames &= ~(1 << EOTFixedHeader::EOT_FAMILY_NAME_INDEX);
            break;

        case NAME_ID_STYLE:
            names[EOTFixedHeader::EOT_STYLE_NAME_INDEX].offset = nameRecord->offset;
            names[EOTFixedHeader::EOT_STYLE_NAME_INDEX].length = nameRecord->length;
            needNames &= ~(1 << EOTFixedHeader::EOT_STYLE_NAME_INDEX);
            break;

        case NAME_ID_FULL:
            names[EOTFixedHeader::EOT_FULL_NAME_INDEX].offset = nameRecord->offset;
            names[EOTFixedHeader::EOT_FULL_NAME_INDEX].length = nameRecord->length;
            needNames &= ~(1 << EOTFixedHeader::EOT_FULL_NAME_INDEX);
            break;

        case NAME_ID_VERSION:
            names[EOTFixedHeader::EOT_VERSION_NAME_INDEX].offset = nameRecord->offset;
            names[EOTFixedHeader::EOT_VERSION_NAME_INDEX].length = nameRecord->length;
            needNames &= ~(1 << EOTFixedHeader::EOT_VERSION_NAME_INDEX);
            break;

        default:
            break;
        }

        if (needNames == 0)
            break;
    }

    // the Version name is allowed to be null
    if ((needNames & ~(1 << EOTFixedHeader::EOT_VERSION_NAME_INDEX)) != 0) {
        return NS_ERROR_FAILURE;
    }

    // -- expand buffer if needed to include variable-length portion
    PRUint32 eotVariableLength = 0;
    eotVariableLength = (names[EOTFixedHeader::EOT_FAMILY_NAME_INDEX].length & (~1)) +
                        (names[EOTFixedHeader::EOT_STYLE_NAME_INDEX].length & (~1)) +
                        (names[EOTFixedHeader::EOT_FULL_NAME_INDEX].length & (~1)) +
                        (names[EOTFixedHeader::EOT_VERSION_NAME_INDEX].length & (~1)) +
                        EOTFixedHeader::EOT_NUM_NAMES * (2 /* size */ 
                                                         + 2 /* padding */) +
                        2 /* null root string size */;

    if (!aHeader->AppendElements(eotVariableLength))
        return NS_ERROR_OUT_OF_MEMORY;

    // append the string data to the end of the EOT header
    PRUint8 *eotEnd = aHeader->Elements() + sizeof(EOTFixedHeader);
    PRUint32 strOffset, strLen;

    for (i = 0; i < EOTFixedHeader::EOT_NUM_NAMES; i++) {
        PRUint32 namelen = names[i].length;
        PRUint32 nameoff = names[i].offset;  // offset from base of string storage

        // sanity check the name string location
        if (PRUint64(nameOffset) + PRUint64(nameStringsBase) + PRUint64(nameoff) 
            + PRUint64(namelen) > dataLength) {
            return NS_ERROR_FAILURE;
        }
    
        strOffset = nameOffset + nameStringsBase + nameoff + namelen;

        // output 2-byte str size   
        strLen = namelen & (~1);  // UTF-16 string len must be even
        *((PRUint16*) eotEnd) = PRUint16(strLen);
        eotEnd += 2;

        // length is number of UTF-16 chars, not bytes    
        CopySwapUTF16(reinterpret_cast<const PRUint16*>(aFontData + strOffset), 
                      reinterpret_cast<PRUint16*>(eotEnd), 
                      (strLen >> 1));  
        eotEnd += strLen;

        // add 2-byte zero padding to the end of each string
        *eotEnd++ = 0;
        *eotEnd++ = 0;

       // Note: Microsoft's WEFT tool produces name strings which
       // include an extra null at the end of each string, in addition
       // to the 2-byte zero padding that separates the string fields. 
       // Don't think this is important to imitate...
    }

    // append null root string size
    *eotEnd++ = 0;
    *eotEnd++ = 0;

    NS_ASSERTION(eotEnd == aHeader->Elements() + aHeader->Length(), 
                 "header length calculation incorrect");
                 
    // bug 496573 -- fonts with a fullname that does not begin with the 
    // family name cause the EOT font loading API to hiccup
    PRUint32 famOff = names[EOTFixedHeader::EOT_FAMILY_NAME_INDEX].offset;
    PRUint32 famLen = names[EOTFixedHeader::EOT_FAMILY_NAME_INDEX].length;
    PRUint32 fullOff = names[EOTFixedHeader::EOT_FULL_NAME_INDEX].offset;
    PRUint32 fullLen = names[EOTFixedHeader::EOT_FULL_NAME_INDEX].length;
    
    const PRUint8 *nameStrings = aFontData + nameOffset + nameStringsBase;

    // assure that the start of the fullname matches the family name
    if (famLen <= fullLen 
        && memcmp(nameStrings + famOff, nameStrings + fullOff, famLen)) {
        aOverlay->overlaySrc = nameOffset + nameStringsBase + famOff;
        aOverlay->overlaySrcLen = famLen;
        aOverlay->overlayDest = nameOffset + nameStringsBase + fullOff;
    }

    // -- OS/2 table data
    const OS2Table *os2Data = reinterpret_cast<const OS2Table*>(aFontData + os2Offset);

    memcpy(eotHeader->panose, os2Data->panose, sizeof(eotHeader->panose));

    eotHeader->italic = (PRUint16) os2Data->fsSelection & 0x01;
    eotHeader->weight = os2Data->usWeightClass;
    eotHeader->unicodeRange1 = os2Data->unicodeRange1;
    eotHeader->unicodeRange2 = os2Data->unicodeRange2;
    eotHeader->unicodeRange3 = os2Data->unicodeRange3;
    eotHeader->unicodeRange4 = os2Data->unicodeRange4;
    eotHeader->codePageRange1 = os2Data->codePageRange1;
    eotHeader->codePageRange2 = os2Data->codePageRange2;

    eotHeader->eotSize = aHeader->Length() + fontDataSize;

    // DumpEOTHeader(aHeader->Elements(), aHeader->Length());

    return NS_OK;
}

/* static */
PRBool
gfxFontUtils::IsCffFont(const PRUint8* aFontData)
{
    // this is only called after aFontData has passed basic validation,
    // so we know there is enough data present to allow us to read the version!
    const SFNTHeader *sfntHeader = reinterpret_cast<const SFNTHeader*>(aFontData);
    return (sfntHeader->sfntVersion == TRUETYPE_TAG('O','T','T','O'));
}

#endif
