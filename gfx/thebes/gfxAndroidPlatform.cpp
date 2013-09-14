/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"

#include "gfxAndroidPlatform.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/Preferences.h"

#include "gfxFT2FontList.h"
#include "gfxImageSurface.h"
#include "mozilla/dom/ContentChild.h"
#include "nsXULAppAPI.h"
#include "nsIScreen.h"
#include "nsIScreenManager.h"
#include "nsILocaleService.h"

#include "cairo.h"

#include "ft2build.h"
#include FT_FREETYPE_H
#include FT_MODULE_H

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::gfx;

static FT_Library gPlatformFTLibrary = NULL;

static int64_t sFreetypeMemoryUsed;
static FT_MemoryRec_ sFreetypeMemoryRecord;

static int64_t
GetFreetypeSize()
{
    return sFreetypeMemoryUsed;
}

NS_MEMORY_REPORTER_IMPLEMENT(Freetype,
    "explicit/freetype",
    KIND_HEAP,
    UNITS_BYTES,
    GetFreetypeSize,
    "Memory used by Freetype."
)

NS_MEMORY_REPORTER_MALLOC_SIZEOF_ON_ALLOC_FUN(FreetypeMallocSizeOfOnAlloc)
NS_MEMORY_REPORTER_MALLOC_SIZEOF_ON_FREE_FUN(FreetypeMallocSizeOfOnFree)

static void*
CountingAlloc(FT_Memory memory, long size)
{
    void *p = malloc(size);
    sFreetypeMemoryUsed += FreetypeMallocSizeOfOnAlloc(p);
    return p;
}

static void
CountingFree(FT_Memory memory, void* p)
{
    sFreetypeMemoryUsed -= FreetypeMallocSizeOfOnFree(p);
    free(p);
}

static void*
CountingRealloc(FT_Memory memory, long cur_size, long new_size, void* p)
{
    sFreetypeMemoryUsed -= FreetypeMallocSizeOfOnFree(p);
    void *pnew = realloc(p, new_size);
    if (pnew) {
        sFreetypeMemoryUsed += FreetypeMallocSizeOfOnAlloc(pnew);
    } else {
        // realloc failed;  undo the decrement from above
        sFreetypeMemoryUsed += FreetypeMallocSizeOfOnAlloc(p);
    }
    return pnew;
}

gfxAndroidPlatform::gfxAndroidPlatform()
{
    // A custom allocator.  It counts allocations, enabling memory reporting.
    sFreetypeMemoryRecord.user    = nullptr;
    sFreetypeMemoryRecord.alloc   = CountingAlloc;
    sFreetypeMemoryRecord.free    = CountingFree;
    sFreetypeMemoryRecord.realloc = CountingRealloc;

    // These two calls are equivalent to FT_Init_FreeType(), but allow us to
    // provide a custom memory allocator.
    FT_New_Library(&sFreetypeMemoryRecord, &gPlatformFTLibrary);
    FT_Add_Default_Modules(gPlatformFTLibrary);

    NS_RegisterMemoryReporter(new NS_MEMORY_REPORTER_NAME(Freetype));

    nsCOMPtr<nsIScreenManager> screenMgr = do_GetService("@mozilla.org/gfx/screenmanager;1");
    nsCOMPtr<nsIScreen> screen;
    screenMgr->GetPrimaryScreen(getter_AddRefs(screen));
    mScreenDepth = 24;
    screen->GetColorDepth(&mScreenDepth);

    mOffscreenFormat = mScreenDepth == 16
                       ? gfxASurface::ImageFormatRGB16_565
                       : gfxASurface::ImageFormatRGB24;

    if (Preferences::GetBool("gfx.android.rgb16.force", false)) {
        mOffscreenFormat = gfxASurface::ImageFormatRGB16_565;
    }

}

gfxAndroidPlatform::~gfxAndroidPlatform()
{
    cairo_debug_reset_static_data();

    FT_Done_Library(gPlatformFTLibrary);
    gPlatformFTLibrary = NULL;
}

already_AddRefed<gfxASurface>
gfxAndroidPlatform::CreateOffscreenSurface(const gfxIntSize& size,
                                      gfxASurface::gfxContentType contentType)
{
    nsRefPtr<gfxASurface> newSurface;
    newSurface = new gfxImageSurface(size, OptimalFormatForContent(contentType));

    return newSurface.forget();
}

static bool
IsJapaneseLocale()
{
    static bool sInitialized = false;
    static bool sIsJapanese = false;

    if (!sInitialized) {
        sInitialized = true;

        do { // to allow 'break' to abandon this block if a call fails
            nsresult rv;
            nsCOMPtr<nsILocaleService> ls =
                do_GetService(NS_LOCALESERVICE_CONTRACTID, &rv);
            if (NS_FAILED(rv)) {
                break;
            }
            nsCOMPtr<nsILocale> appLocale;
            rv = ls->GetApplicationLocale(getter_AddRefs(appLocale));
            if (NS_FAILED(rv)) {
                break;
            }
            nsString localeStr;
            rv = appLocale->
                GetCategory(NS_LITERAL_STRING(NSILOCALE_MESSAGE), localeStr);
            if (NS_FAILED(rv)) {
                break;
            }
            const nsAString& lang = nsDependentSubstring(localeStr, 0, 2);
            if (lang.EqualsLiteral("ja")) {
                sIsJapanese = true;
            }
        } while (false);
    }

    return sIsJapanese;
}

void
gfxAndroidPlatform::GetCommonFallbackFonts(const uint32_t aCh,
                                           int32_t aRunScript,
                                           nsTArray<const char*>& aFontList)
{
    static const char kDroidSansJapanese[] = "Droid Sans Japanese";

    if (IS_IN_BMP(aCh)) {
        // try language-specific "Droid Sans *" fonts for certain blocks,
        // as most devices probably have these
        uint8_t block = (aCh >> 8) & 0xff;
        switch (block) {
        case 0x05:
            aFontList.AppendElement("Droid Sans Hebrew");
            aFontList.AppendElement("Droid Sans Armenian");
            break;
        case 0x06:
            aFontList.AppendElement("Droid Sans Arabic");
            break;
        case 0x09:
            aFontList.AppendElement("Droid Sans Devanagari");
            break;
        case 0x0b:
            aFontList.AppendElement("Droid Sans Tamil");
            break;
        case 0x0e:
            aFontList.AppendElement("Droid Sans Thai");
            break;
        case 0x10: case 0x2d:
            aFontList.AppendElement("Droid Sans Georgian");
            break;
        case 0x12: case 0x13:
            aFontList.AppendElement("Droid Sans Ethiopic");
            break;
        case 0xf9: case 0xfa:
            if (IsJapaneseLocale()) {
                aFontList.AppendElement(kDroidSansJapanese);
            }
            break;
        default:
            if (block >= 0x2e && block <= 0x9f && IsJapaneseLocale()) {
                aFontList.AppendElement(kDroidSansJapanese);
            }
            break;
        }
    }
    // and try Droid Sans Fallback as a last resort
    aFontList.AppendElement("Droid Sans Fallback");
}

nsresult
gfxAndroidPlatform::GetFontList(nsIAtom *aLangGroup,
                                const nsACString& aGenericFamily,
                                nsTArray<nsString>& aListOfFonts)
{
    gfxPlatformFontList::PlatformFontList()->GetFontList(aLangGroup,
                                                         aGenericFamily,
                                                         aListOfFonts);
    return NS_OK;
}

void
gfxAndroidPlatform::GetFontList(InfallibleTArray<FontListEntry>* retValue)
{
    gfxFT2FontList::PlatformFontList()->GetFontList(retValue);
}

nsresult
gfxAndroidPlatform::UpdateFontList()
{
    gfxPlatformFontList::PlatformFontList()->UpdateFontList();
    return NS_OK;
}

nsresult
gfxAndroidPlatform::ResolveFontName(const nsAString& aFontName,
                                    FontResolverCallback aCallback,
                                    void *aClosure,
                                    bool& aAborted)
{
    nsAutoString resolvedName;
    if (!gfxPlatformFontList::PlatformFontList()->
             ResolveFontName(aFontName, resolvedName)) {
        aAborted = false;
        return NS_OK;
    }
    aAborted = !(*aCallback)(resolvedName, aClosure);
    return NS_OK;
}

nsresult
gfxAndroidPlatform::GetStandardFamilyName(const nsAString& aFontName, nsAString& aFamilyName)
{
    gfxPlatformFontList::PlatformFontList()->GetStandardFamilyName(aFontName, aFamilyName);
    return NS_OK;
}

gfxPlatformFontList*
gfxAndroidPlatform::CreatePlatformFontList()
{
    gfxPlatformFontList* list = new gfxFT2FontList();
    if (NS_SUCCEEDED(list->InitFontList())) {
        return list;
    }
    gfxPlatformFontList::Shutdown();
    return nullptr;
}

bool
gfxAndroidPlatform::IsFontFormatSupported(nsIURI *aFontURI, uint32_t aFormatFlags)
{
    // check for strange format flags
    NS_ASSERTION(!(aFormatFlags & gfxUserFontSet::FLAG_FORMAT_NOT_USED),
                 "strange font format hint set");

    // accept supported formats
    if (aFormatFlags & (gfxUserFontSet::FLAG_FORMAT_OPENTYPE |
                        gfxUserFontSet::FLAG_FORMAT_WOFF |
                        gfxUserFontSet::FLAG_FORMAT_TRUETYPE)) {
        return true;
    }

    // reject all other formats, known and unknown
    if (aFormatFlags != 0) {
        return false;
    }

    // no format hint set, need to look at data
    return true;
}

gfxFontGroup *
gfxAndroidPlatform::CreateFontGroup(const nsAString &aFamilies,
                               const gfxFontStyle *aStyle,
                               gfxUserFontSet* aUserFontSet)
{
    return new gfxFontGroup(aFamilies, aStyle, aUserFontSet);
}

FT_Library
gfxAndroidPlatform::GetFTLibrary()
{
    return gPlatformFTLibrary;
}

gfxFontEntry* 
gfxAndroidPlatform::MakePlatformFont(const gfxProxyFontEntry *aProxyEntry,
                                     const uint8_t *aFontData, uint32_t aLength)
{
    return gfxPlatformFontList::PlatformFontList()->MakePlatformFont(aProxyEntry,
                                                                     aFontData,
                                                                     aLength);
}

TemporaryRef<ScaledFont>
gfxAndroidPlatform::GetScaledFontForFont(DrawTarget* aTarget, gfxFont *aFont)
{
    NativeFont nativeFont;
    if (aTarget->GetType() == BACKEND_CAIRO) {
        nativeFont.mType = NATIVE_FONT_CAIRO_FONT_FACE;
        nativeFont.mFont = NULL;
        return Factory::CreateScaledFontWithCairo(nativeFont, aFont->GetAdjustedSize(), aFont->GetCairoScaledFont());
    }
 
    NS_ASSERTION(aFont->GetType() == gfxFont::FONT_TYPE_FT2, "Expecting Freetype font");
    nativeFont.mType = NATIVE_FONT_SKIA_FONT_FACE;
    nativeFont.mFont = static_cast<gfxFT2FontBase*>(aFont)->GetFontOptions();
    return Factory::CreateScaledFontForNativeFont(nativeFont, aFont->GetAdjustedSize());
}

bool
gfxAndroidPlatform::FontHintingEnabled()
{
    // In "mobile" builds, we sometimes use non-reflow-zoom, so we
    // might not want hinting.  Let's see.

#ifdef MOZ_USING_ANDROID_JAVA_WIDGETS
    // On android-java, we currently only use gecko to render web
    // content that can always be be non-reflow-zoomed.  So turn off
    // hinting.
    // 
    // XXX when gecko-android-java is used as an "app runtime", we may
    // want to re-enable hinting for non-browser processes there.
    return false;
#endif //  MOZ_USING_ANDROID_JAVA_WIDGETS

#ifdef MOZ_WIDGET_GONK
    // On B2G, the UX preference is currently to keep hinting disabled
    // for all text (see bug 829523).
    return false;
#endif

    // Currently, we don't have any other targets, but if/when we do,
    // decide how to handle them here.

    NS_NOTREACHED("oops, what platform is this?");
    return gfxPlatform::FontHintingEnabled();
}

bool
gfxAndroidPlatform::RequiresLinearZoom()
{
#ifdef MOZ_USING_ANDROID_JAVA_WIDGETS
    // On android-java, we currently only use gecko to render web
    // content that can always be be non-reflow-zoomed.
    //
    // XXX when gecko-android-java is used as an "app runtime", we may
    // want to treat it like B2G and use linear zoom only for the web
    // browser process, not other apps.
    return true;
#endif

#ifdef MOZ_WIDGET_GONK
    // On B2G, we need linear zoom for the browser, but otherwise prefer
    // the improved glyph spacing that results from respecting the device
    // pixel resolution for glyph layout (see bug 816614).
    return XRE_GetProcessType() == GeckoProcessType_Content &&
           ContentChild::GetSingleton()->IsForBrowser();
#endif

    NS_NOTREACHED("oops, what platform is this?");
    return gfxPlatform::RequiresLinearZoom();
}

int
gfxAndroidPlatform::GetScreenDepth() const
{
    return mScreenDepth;
}
