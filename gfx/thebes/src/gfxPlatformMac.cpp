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
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Vladimir Vukicevic <vladimir@pobox.com>
 *   Masayuki Nakano <masayuki@d-toybox.com>
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

#include "gfxPlatformMac.h"

#include "gfxImageSurface.h"
#include "gfxQuartzSurface.h"
#include "gfxQuartzImageSurface.h"

#include "gfxMacPlatformFontList.h"
#include "gfxAtsuiFonts.h"
#include "gfxUserFontSet.h"

#ifdef MOZ_CORETEXT
#include "gfxCoreTextFonts.h"
#endif

#include "nsIPrefBranch.h"
#include "nsIPrefService.h"
#include "nsIPrefLocalizedString.h"
#include "nsServiceManagerUtils.h"
#include "nsCRT.h"
#include "nsTArray.h"
#include "nsUnicodeRange.h"

#include "qcms.h"

gfxPlatformMac::gfxPlatformMac()
{
    mOSXVersion = 0;
    mFontAntiAliasingThreshold = ReadAntiAliasingThreshold();

#ifndef __LP64__
    // On 64-bit, we only have CoreText, no ATSUI;
    // for 32-bit, check whether we can and should use CoreText
    mUseCoreText = PR_FALSE;

#ifdef MOZ_CORETEXT
    if (&CTLineCreateWithAttributedString != NULL) {
        mUseCoreText = PR_TRUE;
        nsCOMPtr<nsIPrefBranch> prefbranch = do_GetService(NS_PREFSERVICE_CONTRACTID);
        if (prefbranch) {
            PRBool enabled;
            nsresult rv = prefbranch->GetBoolPref("gfx.force_atsui_text", &enabled);
            if (NS_SUCCEEDED(rv) && enabled)
                mUseCoreText = PR_FALSE;
        }
    }
#ifdef DEBUG_jonathan
    printf("Using %s for font & glyph shaping support\n",
           mUseCoreText ? "CoreText" : "ATSUI");
#endif
#endif /* MOZ_CORETEXT */

#endif /* not __LP64__ */
}

gfxPlatformMac::~gfxPlatformMac()
{
#ifdef MOZ_CORETEXT
#ifndef __LP64__
    if (mUseCoreText)
#endif
        gfxCoreTextFont::Shutdown();
#endif
}

gfxPlatformFontList*
gfxPlatformMac::CreatePlatformFontList()
{
    return new gfxMacPlatformFontList();
}

already_AddRefed<gfxASurface>
gfxPlatformMac::CreateOffscreenSurface(const gfxIntSize& size,
                                       gfxASurface::gfxImageFormat imageFormat)
{
    gfxASurface *newSurface = nsnull;

    newSurface = new gfxQuartzSurface(size, imageFormat);

    NS_IF_ADDREF(newSurface);
    return newSurface;
}

already_AddRefed<gfxASurface>
gfxPlatformMac::OptimizeImage(gfxImageSurface *aSurface,
                              gfxASurface::gfxImageFormat format)
{
    const gfxIntSize& surfaceSize = aSurface->GetSize();
    nsRefPtr<gfxImageSurface> isurf = aSurface;

    if (format != aSurface->Format()) {
        isurf = new gfxImageSurface (surfaceSize, format);
        if (!isurf->CopyFrom (aSurface)) {
            // don't even bother doing anything more
            NS_ADDREF(aSurface);
            return aSurface;
        }
    }

    nsRefPtr<gfxASurface> ret = new gfxQuartzImageSurface(isurf);
    return ret.forget();
}

nsresult
gfxPlatformMac::ResolveFontName(const nsAString& aFontName,
                                FontResolverCallback aCallback,
                                void *aClosure, PRBool& aAborted)
{
    nsAutoString resolvedName;
    if (!gfxPlatformFontList::PlatformFontList()->
             ResolveFontName(aFontName, resolvedName)) {
        aAborted = PR_FALSE;
        return NS_OK;
    }
    aAborted = !(*aCallback)(resolvedName, aClosure);
    return NS_OK;
}

nsresult
gfxPlatformMac::GetStandardFamilyName(const nsAString& aFontName, nsAString& aFamilyName)
{
    gfxPlatformFontList::PlatformFontList()->GetStandardFamilyName(aFontName, aFamilyName);
    return NS_OK;
}

gfxFontGroup *
gfxPlatformMac::CreateFontGroup(const nsAString &aFamilies,
                                const gfxFontStyle *aStyle,
                                gfxUserFontSet *aUserFontSet)
{
#ifdef __LP64__
    return new gfxCoreTextFontGroup(aFamilies, aStyle, aUserFontSet);
#else
#ifdef MOZ_CORETEXT
    if (mUseCoreText)
        return new gfxCoreTextFontGroup(aFamilies, aStyle, aUserFontSet);
#endif
    return new gfxAtsuiFontGroup(aFamilies, aStyle, aUserFontSet);
#endif
}

// these will move to gfxPlatform once all platforms support the fontlist
gfxFontEntry* 
gfxPlatformMac::LookupLocalFont(const gfxProxyFontEntry *aProxyEntry,
                                const nsAString& aFontName)
{
    return gfxPlatformFontList::PlatformFontList()->LookupLocalFont(aProxyEntry, 
                                                                    aFontName);
}

gfxFontEntry* 
gfxPlatformMac::MakePlatformFont(const gfxProxyFontEntry *aProxyEntry,
                                 const PRUint8 *aFontData, PRUint32 aLength)
{
    // Ownership of aFontData is passed in here.
    // After activating the font via ATS, we can discard the data.
    gfxFontEntry *fe =
        gfxPlatformFontList::PlatformFontList()->MakePlatformFont(aProxyEntry,
                                                                  aFontData,
                                                                  aLength);
    NS_Free((void*)aFontData);
    return fe;
}

PRBool
gfxPlatformMac::IsFontFormatSupported(nsIURI *aFontURI, PRUint32 aFormatFlags)
{
    // check for strange format flags
    NS_ASSERTION(!(aFormatFlags & gfxUserFontSet::FLAG_FORMAT_NOT_USED),
                 "strange font format hint set");

    // accept supported formats
    if (aFormatFlags & (gfxUserFontSet::FLAG_FORMAT_WOFF     |
                        gfxUserFontSet::FLAG_FORMAT_OPENTYPE | 
                        gfxUserFontSet::FLAG_FORMAT_TRUETYPE | 
                        gfxUserFontSet::FLAG_FORMAT_TRUETYPE_AAT)) {
        return PR_TRUE;
    }

    // reject all other formats, known and unknown
    if (aFormatFlags != 0) {
        return PR_FALSE;
    }

    // no format hint set, need to look at data
    return PR_TRUE;
}

// these will also move to gfxPlatform once all platforms support the fontlist
nsresult
gfxPlatformMac::GetFontList(const nsACString& aLangGroup,
                            const nsACString& aGenericFamily,
                            nsTArray<nsString>& aListOfFonts)
{
    gfxPlatformFontList::PlatformFontList()->GetFontList(aLangGroup, aGenericFamily, aListOfFonts);
    return NS_OK;
}

nsresult
gfxPlatformMac::UpdateFontList()
{
    gfxPlatformFontList::PlatformFontList()->UpdateFontList();
    return NS_OK;
}

PRInt32 
gfxPlatformMac::OSXVersion()
{
    if (!mOSXVersion) {
        // minor version is not accurate, use gestaltSystemVersionMajor, gestaltSystemVersionMinor, gestaltSystemVersionBugFix for these
        OSErr err = ::Gestalt(gestaltSystemVersion, reinterpret_cast<SInt32*>(&mOSXVersion));
        if (err != noErr) {
            //This should probably be changed when our minimum version changes
            NS_ERROR("Couldn't determine OS X version, assuming 10.4");
            mOSXVersion = MAC_OS_X_VERSION_10_4_HEX;
        }
    }
    return mOSXVersion;
}

void 
gfxPlatformMac::GetLangPrefs(eFontPrefLang aPrefLangs[], PRUint32 &aLen, eFontPrefLang aCharLang, eFontPrefLang aPageLang)
{
    if (IsLangCJK(aCharLang)) {
        AppendCJKPrefLangs(aPrefLangs, aLen, aCharLang, aPageLang);
    } else {
        AppendPrefLang(aPrefLangs, aLen, aCharLang);
    }

    AppendPrefLang(aPrefLangs, aLen, eFontPrefLang_Others);
}

void
gfxPlatformMac::AppendCJKPrefLangs(eFontPrefLang aPrefLangs[], PRUint32 &aLen, eFontPrefLang aCharLang, eFontPrefLang aPageLang)
{
    nsCOMPtr<nsIPrefBranch> prefs(do_GetService(NS_PREFSERVICE_CONTRACTID));

    // prefer the lang specified by the page *if* CJK
    if (IsLangCJK(aPageLang)) {
        AppendPrefLang(aPrefLangs, aLen, aPageLang);
    }
    
    // if not set up, set up the default CJK order, based on accept lang settings and system script
    if (mCJKPrefLangs.Length() == 0) {
    
        // temp array
        eFontPrefLang tempPrefLangs[kMaxLenPrefLangList];
        PRUint32 tempLen = 0;
        
        // Add the CJK pref fonts from accept languages, the order should be same order
        nsCAutoString list;
        nsresult rv;
        if (prefs) {
            nsCOMPtr<nsIPrefLocalizedString> prefString;
            rv = prefs->GetComplexValue("intl.accept_languages", NS_GET_IID(nsIPrefLocalizedString), getter_AddRefs(prefString));
            if (prefString) {
                nsAutoString temp;
                prefString->ToString(getter_Copies(temp));
                LossyCopyUTF16toASCII(temp, list);
            }
        }
        
        if (NS_SUCCEEDED(rv) && !list.IsEmpty()) {
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
                eFontPrefLang fpl = gfxPlatform::GetFontPrefLangFor(lang.get());
                switch (fpl) {
                    case eFontPrefLang_Japanese:
                    case eFontPrefLang_Korean:
                    case eFontPrefLang_ChineseCN:
                    case eFontPrefLang_ChineseHK:
                    case eFontPrefLang_ChineseTW:
                        AppendPrefLang(tempPrefLangs, tempLen, fpl);
                        break;
                    default:
                        break;
                }
                p++;
            }
        }

        // Prefer the system locale if it is CJK.
        TextEncoding sysScript = ::GetApplicationTextEncoding();
        // XXX Is not there the HK locale?
        switch (sysScript) {
            case kTextEncodingMacJapanese:    AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_Japanese); break;
            case kTextEncodingMacChineseTrad: AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_ChineseTW); break;
            case kTextEncodingMacKorean:      AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_Korean); break;
            case kTextEncodingMacChineseSimp: AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_ChineseCN); break;
            default:                          break;
        }

        // last resort... (the order is same as old gfx.)
        AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_Japanese);
        AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_Korean);
        AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_ChineseCN);
        AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_ChineseHK);
        AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_ChineseTW);
        
        // copy into the cached array
        PRUint32 j;
        for (j = 0; j < tempLen; j++) {
            mCJKPrefLangs.AppendElement(tempPrefLangs[j]);
        }
    }
    
    // append in cached CJK langs
    PRUint32  i, numCJKlangs = mCJKPrefLangs.Length();
    
    for (i = 0; i < numCJKlangs; i++) {
        AppendPrefLang(aPrefLangs, aLen, (eFontPrefLang) (mCJKPrefLangs[i]));
    }
        
}

PRUint32
gfxPlatformMac::ReadAntiAliasingThreshold()
{
    PRUint32 threshold = 0;  // default == no threshold
    
    // first read prefs flag to determine whether to use the setting or not
    PRBool useAntiAliasingThreshold = PR_FALSE;
    nsCOMPtr<nsIPrefBranch> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID);
    if (prefs) {
        PRBool enabled;
        nsresult rv =
            prefs->GetBoolPref("gfx.use_text_smoothing_setting", &enabled);
        if (NS_SUCCEEDED(rv)) {
            useAntiAliasingThreshold = enabled;
        }
    }
    
    // if the pref setting is disabled, return 0 which effectively disables this feature
    if (!useAntiAliasingThreshold)
        return threshold;
        
    // value set via Appearance pref panel, "Turn off text smoothing for font sizes xxx and smaller"
    CFNumberRef prefValue = (CFNumberRef)CFPreferencesCopyAppValue(CFSTR("AppleAntiAliasingThreshold"), kCFPreferencesCurrentApplication);

    if (prefValue) {
        if (!CFNumberGetValue(prefValue, kCFNumberIntType, &threshold)) {
            threshold = 0;
        }
        CFRelease(prefValue);
    }

    return threshold;
}

qcms_profile *
gfxPlatformMac::GetPlatformCMSOutputProfile()
{
    qcms_profile *profile = nsnull;
    CMProfileRef cmProfile;
    CMProfileLocation *location;
    UInt32 locationSize;

    /* There a number of different ways that we could try to get a color
       profile to use.  On 10.5 all of these methods seem to give the same
       results. On 10.6, the results are different and the following method,
       using CGMainDisplayID() seems to best match what we are looking for.
       Currently, both Google Chrome and Qt4 use a similar method.

       CMTypes.h describes CMDisplayIDType:
       "Data type for ColorSync DisplayID reference
        On 8 & 9 this is a AVIDType
	On X this is a CGSDisplayID"

       CGMainDisplayID gives us a CGDirectDisplayID which presumeably
       corresponds directly to a CGSDisplayID */
    CGDirectDisplayID displayID = CGMainDisplayID();

    /* On OS X 10.4 CGDirectDisplayID is of type 'struct _CGDirectDisplayID *' whereas it is of
       type 'uint32_t' on OS X 10.5. Therefore, we need to use a c-style cast instead of static_cast
       to cover converting from a pointer or from an integer */
    CMError err = CMGetProfileByAVID((CMDisplayIDType)displayID, &cmProfile);
    if (err != noErr)
        return nsnull;

    // get the size of location
    err = NCMGetProfileLocation(cmProfile, NULL, &locationSize);
    if (err != noErr)
        return nsnull;

    // allocate enough room for location
    location = static_cast<CMProfileLocation*>(malloc(locationSize));
    if (!location)
        goto fail_close;

    err = NCMGetProfileLocation(cmProfile, location, &locationSize);
    if (err != noErr)
        goto fail_location;

    switch (location->locType) {
#ifndef __LP64__
    case cmFileBasedProfile: {
        FSRef fsRef;
        if (!FSpMakeFSRef(&location->u.fileLoc.spec, &fsRef)) {
            char path[512];
            if (!FSRefMakePath(&fsRef, reinterpret_cast<UInt8*>(path), sizeof(path))) {
                profile = qcms_profile_from_path(path);
#ifdef DEBUG_tor
                if (profile)
                    fprintf(stderr,
                            "ICM profile read from %s fileLoc successfully\n", path);
#endif
            }
        }
        break;
    }
#endif
    case cmPathBasedProfile:
        profile = qcms_profile_from_path(location->u.pathLoc.path);
#ifdef DEBUG_tor
        if (profile)
            fprintf(stderr,
                    "ICM profile read from %s pathLoc successfully\n",
                    device.u.pathLoc.path);
#endif
        break;
    default:
#ifdef DEBUG_tor
        fprintf(stderr, "Unhandled ColorSync profile location\n");
#endif
        break;
    }

fail_location:
    free(location);
fail_close:
    CMCloseProfile(cmProfile);
    return profile;
}

void
gfxPlatformMac::SetupClusterBoundaries(gfxTextRun *aTextRun, const PRUnichar *aString)
{
    TextBreakLocatorRef locator;
    OSStatus status = UCCreateTextBreakLocator(NULL, 0, kUCTextBreakClusterMask,
                                               &locator);
    if (status != noErr)
        return;
    UniCharArrayOffset breakOffset = 0;
    UCTextBreakOptions options = kUCTextBreakLeadingEdgeMask;
    PRUint32 length = aTextRun->GetLength();
    while (breakOffset < length) {
        UniCharArrayOffset next;
        status = UCFindTextBreak(locator, kUCTextBreakClusterMask, options,
                                 aString, length, breakOffset, &next);
        if (status != noErr)
            break;
        options |= kUCTextBreakIterateMask;
        PRUint32 i;
        for (i = breakOffset + 1; i < next; ++i) {
            gfxTextRun::CompressedGlyph g;
            // Remember that this character is not the start of a cluster by
            // setting its glyph data to "not a cluster start", "is a
            // ligature start", with no glyphs.
            aTextRun->SetGlyphs(i, g.SetComplex(PR_FALSE, PR_TRUE, 0), nsnull);
        }
        breakOffset = next;
    }
    UCDisposeTextBreakLocator(&locator);
}


eFontPrefLang
gfxPlatformMac::GetFontPrefLangFor(PRUint8 aUnicodeRange)
{
    switch (aUnicodeRange) {
        case kRangeSetLatin:   return eFontPrefLang_Western;
        case kRangeCyrillic:   return eFontPrefLang_Cyrillic;
        case kRangeGreek:      return eFontPrefLang_Greek;
        case kRangeTurkish:    return eFontPrefLang_Turkish;
        case kRangeHebrew:     return eFontPrefLang_Hebrew;
        case kRangeArabic:     return eFontPrefLang_Arabic;
        case kRangeBaltic:     return eFontPrefLang_Baltic;
        case kRangeThai:       return eFontPrefLang_Thai;
        case kRangeKorean:     return eFontPrefLang_Korean;
        case kRangeJapanese:   return eFontPrefLang_Japanese;
        case kRangeSChinese:   return eFontPrefLang_ChineseCN;
        case kRangeTChinese:   return eFontPrefLang_ChineseTW;
        case kRangeDevanagari: return eFontPrefLang_Devanagari;
        case kRangeTamil:      return eFontPrefLang_Tamil;
        case kRangeArmenian:   return eFontPrefLang_Armenian;
        case kRangeBengali:    return eFontPrefLang_Bengali;
        case kRangeCanadian:   return eFontPrefLang_Canadian;
        case kRangeEthiopic:   return eFontPrefLang_Ethiopic;
        case kRangeGeorgian:   return eFontPrefLang_Georgian;
        case kRangeGujarati:   return eFontPrefLang_Gujarati;
        case kRangeGurmukhi:   return eFontPrefLang_Gurmukhi;
        case kRangeKhmer:      return eFontPrefLang_Khmer;
        case kRangeMalayalam:  return eFontPrefLang_Malayalam;
        case kRangeSetCJK:     return eFontPrefLang_CJKSet;
        default:               return eFontPrefLang_Others;
    }
}
