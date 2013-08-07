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
 *   Vladimir Vukicevic <vladimir@pobox.com>
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

#ifdef MOZ_LOGGING
#define FORCE_PR_LOG /* Allow logging in the release build */
#endif
#include "prlog.h"

#include "gfxPlatform.h"

#if defined(XP_WIN)
#include "gfxWindowsPlatform.h"
#include "gfxD2DSurface.h"
#elif defined(XP_MACOSX)
#include "gfxPlatformMac.h"
#elif defined(MOZ_WIDGET_GTK2)
#include "gfxPlatformGtk.h"
#elif defined(MOZ_WIDGET_QT)
#include "gfxQtPlatform.h"
#elif defined(XP_OS2)
#include "gfxOS2Platform.h"
#elif defined(ANDROID)
#include "gfxAndroidPlatform.h"
#endif

#include "gfxAtoms.h"
#include "gfxPlatformFontList.h"
#include "gfxContext.h"
#include "gfxImageSurface.h"
#include "gfxTextRunCache.h"
#include "gfxTextRunWordCache.h"
#include "gfxUserFontSet.h"
#include "gfxUnicodeProperties.h"
#include "harfbuzz/hb-unicode.h"
#ifdef MOZ_GRAPHITE
#include "gfxGraphiteShaper.h"
#endif

#include "nsUnicodeRange.h"
#include "nsServiceManagerUtils.h"
#include "nsTArray.h"
#include "nsIUGenCategory.h"
#include "nsUnicharUtilCIID.h"
#include "nsILocaleService.h"

#include "nsWeakReference.h"

#include "cairo.h"
#include "qcms.h"

#include "plstr.h"
#include "nsCRT.h"
#include "GLContext.h"
#include "GLContextProvider.h"

#include "mozilla/FunctionTimer.h"
#include "mozilla/Preferences.h"

#include "nsIGfxInfo.h"

using namespace mozilla;

gfxPlatform *gPlatform = nsnull;
static bool gEverInitialized = false;

// These two may point to the same profile
static qcms_profile *gCMSOutputProfile = nsnull;
static qcms_profile *gCMSsRGBProfile = nsnull;

static qcms_transform *gCMSRGBTransform = nsnull;
static qcms_transform *gCMSInverseRGBTransform = nsnull;
static qcms_transform *gCMSRGBATransform = nsnull;

static bool gCMSInitialized = false;
static eCMSMode gCMSMode = eCMSMode_Off;
static int gCMSIntent = -2;

static void ShutdownCMS();
static void MigratePrefs();

#include "mozilla/gfx/2D.h"
using namespace mozilla::gfx;

// logs shared across gfx
#ifdef PR_LOGGING
static PRLogModuleInfo *sFontlistLog = nsnull;
static PRLogModuleInfo *sFontInitLog = nsnull;
static PRLogModuleInfo *sTextrunLog = nsnull;
static PRLogModuleInfo *sTextrunuiLog = nsnull;
#endif

/* Class to listen for pref changes so that chrome code can dynamically
   force sRGB as an output profile. See Bug #452125. */
class SRGBOverrideObserver : public nsIObserver,
                             public nsSupportsWeakReference
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIOBSERVER
};

NS_IMPL_ISUPPORTS2(SRGBOverrideObserver, nsIObserver, nsISupportsWeakReference)

NS_IMETHODIMP
SRGBOverrideObserver::Observe(nsISupports *aSubject,
                              const char *aTopic,
                              const PRUnichar *someData)
{
    NS_ASSERTION(NS_strcmp(someData,
                   NS_LITERAL_STRING("gfx.color_mangement.force_srgb").get()),
                 "Restarting CMS on wrong pref!");
    ShutdownCMS();
    return NS_OK;
}

#define GFX_DOWNLOADABLE_FONTS_ENABLED "gfx.downloadable_fonts.enabled"
#define GFX_DOWNLOADABLE_FONTS_SANITIZE "gfx.downloadable_fonts.sanitize"

#define GFX_PREF_HARFBUZZ_SCRIPTS "gfx.font_rendering.harfbuzz.scripts"
#define HARFBUZZ_SCRIPTS_DEFAULT  gfxUnicodeProperties::SHAPING_DEFAULT

#ifdef MOZ_GRAPHITE
#define GFX_PREF_GRAPHITE_SHAPING "gfx.font_rendering.graphite.enabled"
#endif

static const char* kObservedPrefs[] = {
    "gfx.downloadable_fonts.",
    "gfx.font_rendering.",
    nsnull
};

class FontPrefsObserver : public nsIObserver
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIOBSERVER
};

NS_IMPL_ISUPPORTS1(FontPrefsObserver, nsIObserver)

NS_IMETHODIMP
FontPrefsObserver::Observe(nsISupports *aSubject,
                           const char *aTopic,
                           const PRUnichar *someData)
{
    if (!someData) {
        NS_ERROR("font pref observer code broken");
        return NS_ERROR_UNEXPECTED;
    }
    NS_ASSERTION(gfxPlatform::GetPlatform(), "the singleton instance has gone");
    gfxPlatform::GetPlatform()->FontsPrefsChanged(NS_ConvertUTF16toUTF8(someData).get());

    return NS_OK;
}



// this needs to match the list of pref font.default.xx entries listed in all.js!
// the order *must* match the order in eFontPrefLang
static const char *gPrefLangNames[] = {
    "x-western",
    "x-central-euro",
    "ja",
    "zh-TW",
    "zh-CN",
    "zh-HK",
    "ko",
    "x-cyrillic",
    "x-baltic",
    "el",
    "tr",
    "th",
    "he",
    "ar",
    "x-devanagari",
    "x-tamil",
    "x-armn",
    "x-beng",
    "x-cans",
    "x-ethi",
    "x-geor",
    "x-gujr",
    "x-guru",
    "x-khmr",
    "x-mlym",
    "x-orya",
    "x-telu",
    "x-knda",
    "x-sinh",
    "x-tibt",
    "x-unicode",
    "x-user-def"
};

gfxPlatform::gfxPlatform()
  : mAzureBackendCollector(this, &gfxPlatform::GetAzureBackendInfo)
{
    mUseHarfBuzzScripts = UNINITIALIZED_VALUE;
    mAllowDownloadableFonts = UNINITIALIZED_VALUE;
    mDownloadableFontsSanitize = UNINITIALIZED_VALUE;
#ifdef MOZ_GRAPHITE
    mGraphiteShapingEnabled = UNINITIALIZED_VALUE;
#endif
}

gfxPlatform*
gfxPlatform::GetPlatform()
{
    if (!gPlatform) {
        Init();
    }
    return gPlatform;
}

void
gfxPlatform::Init()
{
    if (gEverInitialized) {
        NS_RUNTIMEABORT("Already started???");
    }
    gEverInitialized = true;

    gfxAtoms::RegisterAtoms();

#ifdef PR_LOGGING
    sFontlistLog = PR_NewLogModule("fontlist");;
    sFontInitLog = PR_NewLogModule("fontinit");;
    sTextrunLog = PR_NewLogModule("textrun");;
    sTextrunuiLog = PR_NewLogModule("textrunui");;
#endif


    /* Initialize the GfxInfo service.
     * Note: we can't call functions on GfxInfo that depend
     * on gPlatform until after it has been initialized
     * below. GfxInfo initialization annotates our
     * crash reports so we want to do it before
     * we try to load any drivers and do device detection
     * incase that code crashes. See bug #591561. */
    nsCOMPtr<nsIGfxInfo> gfxInfo;
    /* this currently will only succeed on Windows */
    gfxInfo = do_GetService("@mozilla.org/gfx/info;1");

#if defined(XP_WIN)
    gPlatform = new gfxWindowsPlatform;
#elif defined(XP_MACOSX)
    gPlatform = new gfxPlatformMac;
#elif defined(MOZ_WIDGET_GTK2)
    gPlatform = new gfxPlatformGtk;
#elif defined(MOZ_WIDGET_QT)
    gPlatform = new gfxQtPlatform;
#elif defined(XP_OS2)
    gPlatform = new gfxOS2Platform;
#elif defined(ANDROID)
    gPlatform = new gfxAndroidPlatform;
#else
    #error "No gfxPlatform implementation available"
#endif

    nsresult rv;

#if defined(XP_MACOSX) || defined(XP_WIN) || defined(ANDROID) // temporary, until this is implemented on others
    rv = gfxPlatformFontList::Init();
    if (NS_FAILED(rv)) {
        NS_RUNTIMEABORT("Could not initialize gfxPlatformFontList");
    }
#endif

    gPlatform->mScreenReferenceSurface =
        gPlatform->CreateOffscreenSurface(gfxIntSize(1,1),
                                          gfxASurface::CONTENT_COLOR_ALPHA);
    if (!gPlatform->mScreenReferenceSurface) {
        NS_RUNTIMEABORT("Could not initialize mScreenReferenceSurface");
    }

    rv = gfxFontCache::Init();
    if (NS_FAILED(rv)) {
        NS_RUNTIMEABORT("Could not initialize gfxFontCache");
    }

    rv = gfxTextRunWordCache::Init();
    if (NS_FAILED(rv)) {
        NS_RUNTIMEABORT("Could not initialize gfxTextRunWordCache");
    }

    rv = gfxTextRunCache::Init();
    if (NS_FAILED(rv)) {
        NS_RUNTIMEABORT("Could not initialize gfxTextRunCache");
    }

    /* Pref migration hook. */
    MigratePrefs();

    /* Create and register our CMS Override observer. */
    gPlatform->mSRGBOverrideObserver = new SRGBOverrideObserver();
    Preferences::AddWeakObserver(gPlatform->mSRGBOverrideObserver, "gfx.color_management.force_srgb");

    gPlatform->mFontPrefsObserver = new FontPrefsObserver();
    Preferences::AddStrongObservers(gPlatform->mFontPrefsObserver, kObservedPrefs);

    // Force registration of the gfx component, thus arranging for
    // ::Shutdown to be called.
    nsCOMPtr<nsISupports> forceReg
        = do_CreateInstance("@mozilla.org/gfx/init;1");
}

void
gfxPlatform::Shutdown()
{
    // These may be called before the corresponding subsystems have actually
    // started up. That's OK, they can handle it.
    gfxTextRunCache::Shutdown();
    gfxTextRunWordCache::Shutdown();
    gfxFontCache::Shutdown();
    gfxFontGroup::Shutdown();
#ifdef MOZ_GRAPHITE
    gfxGraphiteShaper::Shutdown();
#endif
#if defined(XP_MACOSX) || defined(XP_WIN) // temporary, until this is implemented on others
    gfxPlatformFontList::Shutdown();
#endif

    // Free the various non-null transforms and loaded profiles
    ShutdownCMS();

    // In some cases, gPlatform may not be created but Shutdown() called,
    // e.g., during xpcshell tests.
    if (gPlatform) {
        /* Unregister our CMS Override callback. */
        NS_ASSERTION(gPlatform->mSRGBOverrideObserver, "mSRGBOverrideObserver has alreay gone");
        Preferences::RemoveObserver(gPlatform->mSRGBOverrideObserver, "gfx.color_management.force_srgb");
        gPlatform->mSRGBOverrideObserver = nsnull;

        NS_ASSERTION(gPlatform->mFontPrefsObserver, "mFontPrefsObserver has alreay gone");
        Preferences::RemoveObservers(gPlatform->mFontPrefsObserver, kObservedPrefs);
        gPlatform->mFontPrefsObserver = nsnull;
    }

    mozilla::gl::GLContextProvider::Shutdown();

    delete gPlatform;
    gPlatform = nsnull;
}

gfxPlatform::~gfxPlatform()
{
    // The cairo folks think we should only clean up in debug builds,
    // but we're generally in the habit of trying to shut down as
    // cleanly as possible even in production code, so call this
    // cairo_debug_* function unconditionally.
    //
    // because cairo can assert and thus crash on shutdown, don't do this in release builds
#if MOZ_TREE_CAIRO && (defined(DEBUG) || defined(NS_BUILD_REFCNT_LOGGING) || defined(NS_TRACE_MALLOC))
    cairo_debug_reset_static_data();
#endif

#if 0
    // It would be nice to do this (although it might need to be after
    // the cairo shutdown that happens in ~gfxPlatform).  It even looks
    // idempotent.  But it has fatal assertions that fire if stuff is
    // leaked, and we hit them.
    FcFini();
#endif
}

already_AddRefed<gfxASurface>
gfxPlatform::OptimizeImage(gfxImageSurface *aSurface,
                           gfxASurface::gfxImageFormat format)
{
    const gfxIntSize& surfaceSize = aSurface->GetSize();

#ifdef XP_WIN
    if (gfxWindowsPlatform::GetPlatform()->GetRenderMode() == 
        gfxWindowsPlatform::RENDER_DIRECT2D) {
        return nsnull;
    }
#endif
    nsRefPtr<gfxASurface> optSurface = CreateOffscreenSurface(surfaceSize, gfxASurface::ContentFromFormat(format));
    if (!optSurface || optSurface->CairoStatus() != 0)
        return nsnull;

    gfxContext tmpCtx(optSurface);
    tmpCtx.SetOperator(gfxContext::OPERATOR_SOURCE);
    tmpCtx.SetSource(aSurface);
    tmpCtx.Paint();

    gfxASurface *ret = optSurface;
    NS_ADDREF(ret);
    return ret;
}

cairo_user_data_key_t kDrawTarget;

RefPtr<DrawTarget>
gfxPlatform::CreateDrawTargetForSurface(gfxASurface *aSurface)
{
#ifdef XP_WIN
  if (aSurface->GetType() == gfxASurface::SurfaceTypeD2D) {
    RefPtr<DrawTarget> drawTarget =
      Factory::CreateDrawTargetForD3D10Texture(static_cast<gfxD2DSurface*>(aSurface)->GetTexture(), FORMAT_B8G8R8A8);
    aSurface->SetData(&kDrawTarget, drawTarget, NULL);
    return drawTarget;
  }
#endif

  // Can't create a draw target for general cairo surfaces yet.
  return NULL;
}

cairo_user_data_key_t kSourceSurface;

void SourceBufferDestroy(void *srcBuffer)
{
  static_cast<SourceSurface*>(srcBuffer)->Release();
}

RefPtr<SourceSurface>
gfxPlatform::GetSourceSurfaceForSurface(DrawTarget *aTarget, gfxASurface *aSurface)
{
  void *userData = aSurface->GetData(&kSourceSurface);

  if (userData) {
    return static_cast<SourceSurface*>(userData);
  }

  SurfaceFormat format;
  if (aSurface->GetContentType() == gfxASurface::CONTENT_ALPHA) {
    format = FORMAT_A8;
  } else if (aSurface->GetContentType() == gfxASurface::CONTENT_COLOR) {
    format = FORMAT_B8G8R8X8;
  } else {
    format = FORMAT_B8G8R8A8;
  }

  RefPtr<SourceSurface> srcBuffer;

#ifdef XP_WIN
  if (aSurface->GetType() == gfxASurface::SurfaceTypeD2D) {
    NativeSurface surf;
    surf.mFormat = format;
    surf.mType = NATIVE_SURFACE_D3D10_TEXTURE;
    surf.mSurface = static_cast<gfxD2DSurface*>(aSurface)->GetTexture();
    mozilla::gfx::DrawTarget *dt = static_cast<mozilla::gfx::DrawTarget*>(aSurface->GetData(&kDrawTarget));
    if (dt) {
      dt->Flush();
    }
    srcBuffer = aTarget->CreateSourceSurfaceFromNativeSurface(surf);
  }
#endif

  if (!srcBuffer) {
    nsRefPtr<gfxImageSurface> imgSurface = aSurface->GetAsImageSurface();

    if (!imgSurface) {
      imgSurface = new gfxImageSurface(aSurface->GetSize(), gfxASurface::FormatFromContent(aSurface->GetContentType()));
      nsRefPtr<gfxContext> ctx = new gfxContext(imgSurface);
      ctx->SetSource(aSurface);
      ctx->SetOperator(gfxContext::OPERATOR_SOURCE);
      ctx->Paint();
    }

    gfxImageFormat cairoFormat = imgSurface->Format();
    switch(cairoFormat) {
      case gfxASurface::ImageFormatARGB32:
        format = FORMAT_B8G8R8A8;
        break;
      case gfxASurface::ImageFormatRGB24:
        format = FORMAT_B8G8R8X8;
        break;
      case gfxASurface::ImageFormatA8:
        format = FORMAT_A8;
        break;
      case gfxASurface::ImageFormatRGB16_565:
        format = FORMAT_R5G6B5;
        break;
      default:
        NS_RUNTIMEABORT("Invalid surface format!");
    }

    srcBuffer = aTarget->CreateSourceSurfaceFromData(imgSurface->Data(),
                                                     IntSize(imgSurface->GetSize().width, imgSurface->GetSize().height),
                                                     imgSurface->Stride(),
                                                     format);
  }

  srcBuffer->AddRef();
  aSurface->SetData(&kSourceSurface, srcBuffer, SourceBufferDestroy);

  return srcBuffer;
}

RefPtr<ScaledFont>
gfxPlatform::GetScaledFontForFont(gfxFont *aFont)
{
  return NULL;
}

cairo_user_data_key_t kDrawSourceSurface;
static void
DataSourceSurfaceDestroy(void *dataSourceSurface)
{
      static_cast<DataSourceSurface*>(dataSourceSurface)->Release();
}

already_AddRefed<gfxASurface>
gfxPlatform::GetThebesSurfaceForDrawTarget(DrawTarget *aTarget)
{
  RefPtr<SourceSurface> source = aTarget->Snapshot();
  RefPtr<DataSourceSurface> data = source->GetDataSurface();

  if (!data) {
    return NULL;
  }

  IntSize size = data->GetSize();
  gfxASurface::gfxImageFormat format = gfxASurface::FormatFromContent(ContentForFormat(data->GetFormat()));
  
  nsRefPtr<gfxImageSurface> image =
    new gfxImageSurface(data->GetData(), gfxIntSize(size.width, size.height),
                        data->Stride(), format);

  image->SetData(&kDrawSourceSurface, data.forget().drop(), DataSourceSurfaceDestroy);
  return image.forget();
}

RefPtr<DrawTarget>
gfxPlatform::CreateOffscreenDrawTarget(const IntSize& aSize, SurfaceFormat aFormat)
{
  BackendType backend;
  if (!SupportsAzure(backend)) {
    return NULL;
  }
  return Factory::CreateDrawTarget(backend, aSize, aFormat); 
}

nsresult
gfxPlatform::GetFontList(nsIAtom *aLangGroup,
                         const nsACString& aGenericFamily,
                         nsTArray<nsString>& aListOfFonts)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult
gfxPlatform::UpdateFontList()
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

bool
gfxPlatform::DownloadableFontsEnabled()
{
    if (mAllowDownloadableFonts == UNINITIALIZED_VALUE) {
        mAllowDownloadableFonts =
            Preferences::GetBool(GFX_DOWNLOADABLE_FONTS_ENABLED, false);
    }

    return mAllowDownloadableFonts;
}

bool
gfxPlatform::SanitizeDownloadedFonts()
{
    if (mDownloadableFontsSanitize == UNINITIALIZED_VALUE) {
        mDownloadableFontsSanitize =
            Preferences::GetBool(GFX_DOWNLOADABLE_FONTS_SANITIZE, true);
    }

    return mDownloadableFontsSanitize;
}

#ifdef MOZ_GRAPHITE
bool
gfxPlatform::UseGraphiteShaping()
{
    if (mGraphiteShapingEnabled == UNINITIALIZED_VALUE) {
        mGraphiteShapingEnabled =
            Preferences::GetBool(GFX_PREF_GRAPHITE_SHAPING, false);
    }

    return mGraphiteShapingEnabled;
}
#endif

bool
gfxPlatform::UseHarfBuzzForScript(PRInt32 aScriptCode)
{
    if (mUseHarfBuzzScripts == UNINITIALIZED_VALUE) {
        mUseHarfBuzzScripts = Preferences::GetInt(GFX_PREF_HARFBUZZ_SCRIPTS, HARFBUZZ_SCRIPTS_DEFAULT);
    }

    PRInt32 shapingType = gfxUnicodeProperties::ScriptShapingType(aScriptCode);

    return (mUseHarfBuzzScripts & shapingType) != 0;
}

gfxFontEntry*
gfxPlatform::MakePlatformFont(const gfxProxyFontEntry *aProxyEntry,
                              const PRUint8 *aFontData,
                              PRUint32 aLength)
{
    // Default implementation does not handle activating downloaded fonts;
    // just free the data and return.
    // Platforms that support @font-face must override this,
    // using the data to instantiate the font, and taking responsibility
    // for freeing it when no longer required.
    if (aFontData) {
        NS_Free((void*)aFontData);
    }
    return nsnull;
}

static void
AppendGenericFontFromPref(nsString& aFonts, nsIAtom *aLangGroup, const char *aGenericName)
{
    NS_ENSURE_TRUE(Preferences::GetRootBranch(), );

    nsCAutoString prefName, langGroupString;

    aLangGroup->ToUTF8String(langGroupString);

    nsCAutoString genericDotLang;
    if (aGenericName) {
        genericDotLang.Assign(aGenericName);
    } else {
        prefName.AssignLiteral("font.default.");
        prefName.Append(langGroupString);
        genericDotLang = Preferences::GetCString(prefName.get());
    }

    genericDotLang.AppendLiteral(".");
    genericDotLang.Append(langGroupString);

    // fetch font.name.xxx value                   
    prefName.AssignLiteral("font.name.");
    prefName.Append(genericDotLang);
    nsAdoptingString nameValue = Preferences::GetString(prefName.get());
    if (nameValue) {
        if (!aFonts.IsEmpty())
            aFonts.AppendLiteral(", ");
        aFonts += nameValue;
    }

    // fetch font.name-list.xxx value                   
    prefName.AssignLiteral("font.name-list.");
    prefName.Append(genericDotLang);
    nsAdoptingString nameListValue = Preferences::GetString(prefName.get());
    if (nameListValue && !nameListValue.Equals(nameValue)) {
        if (!aFonts.IsEmpty())
            aFonts.AppendLiteral(", ");
        aFonts += nameListValue;
    }
}

void
gfxPlatform::GetPrefFonts(nsIAtom *aLanguage, nsString& aFonts, bool aAppendUnicode)
{
    aFonts.Truncate();

    AppendGenericFontFromPref(aFonts, aLanguage, nsnull);
    if (aAppendUnicode)
        AppendGenericFontFromPref(aFonts, gfxAtoms::x_unicode, nsnull);
}

bool gfxPlatform::ForEachPrefFont(eFontPrefLang aLangArray[], PRUint32 aLangArrayLen, PrefFontCallback aCallback,
                                    void *aClosure)
{
    NS_ENSURE_TRUE(Preferences::GetRootBranch(), false);

    PRUint32    i;
    for (i = 0; i < aLangArrayLen; i++) {
        eFontPrefLang prefLang = aLangArray[i];
        const char *langGroup = GetPrefLangName(prefLang);
        
        nsCAutoString prefName;
    
        prefName.AssignLiteral("font.default.");
        prefName.Append(langGroup);
        nsAdoptingCString genericDotLang = Preferences::GetCString(prefName.get());
    
        genericDotLang.AppendLiteral(".");
        genericDotLang.Append(langGroup);
    
        // fetch font.name.xxx value                   
        prefName.AssignLiteral("font.name.");
        prefName.Append(genericDotLang);
        nsAdoptingCString nameValue = Preferences::GetCString(prefName.get());
        if (nameValue) {
            if (!aCallback(prefLang, NS_ConvertUTF8toUTF16(nameValue), aClosure))
                return false;
        }
    
        // fetch font.name-list.xxx value                   
        prefName.AssignLiteral("font.name-list.");
        prefName.Append(genericDotLang);
        nsAdoptingCString nameListValue = Preferences::GetCString(prefName.get());
        if (nameListValue && !nameListValue.Equals(nameValue)) {
            const char kComma = ',';
            const char *p, *p_end;
            nsCAutoString list(nameListValue);
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
                nsCAutoString fontName(Substring(start, p));
                fontName.CompressWhitespace(false, true);
                if (!aCallback(prefLang, NS_ConvertUTF8toUTF16(fontName), aClosure))
                    return false;
                p++;
            }
        }
    }

    return true;
}

eFontPrefLang
gfxPlatform::GetFontPrefLangFor(const char* aLang)
{
    if (!aLang || !aLang[0])
        return eFontPrefLang_Others;
    for (PRUint32 i = 0; i < PRUint32(eFontPrefLang_LangCount); ++i) {
        if (!PL_strcasecmp(gPrefLangNames[i], aLang))
            return eFontPrefLang(i);
    }
    return eFontPrefLang_Others;
}

eFontPrefLang
gfxPlatform::GetFontPrefLangFor(nsIAtom *aLang)
{
    if (!aLang)
        return eFontPrefLang_Others;
    nsCAutoString lang;
    aLang->ToUTF8String(lang);
    return GetFontPrefLangFor(lang.get());
}

const char*
gfxPlatform::GetPrefLangName(eFontPrefLang aLang)
{
    if (PRUint32(aLang) < PRUint32(eFontPrefLang_AllCount))
        return gPrefLangNames[PRUint32(aLang)];
    return nsnull;
}

eFontPrefLang
gfxPlatform::GetFontPrefLangFor(PRUint8 aUnicodeRange)
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
        case kRangeOriya:      return eFontPrefLang_Oriya;
        case kRangeTelugu:     return eFontPrefLang_Telugu;
        case kRangeKannada:    return eFontPrefLang_Kannada;
        case kRangeSinhala:    return eFontPrefLang_Sinhala;
        case kRangeTibetan:    return eFontPrefLang_Tibetan;
        case kRangeSetCJK:     return eFontPrefLang_CJKSet;
        default:               return eFontPrefLang_Others;
    }
}

bool 
gfxPlatform::IsLangCJK(eFontPrefLang aLang)
{
    switch (aLang) {
        case eFontPrefLang_Japanese:
        case eFontPrefLang_ChineseTW:
        case eFontPrefLang_ChineseCN:
        case eFontPrefLang_ChineseHK:
        case eFontPrefLang_Korean:
        case eFontPrefLang_CJKSet:
            return true;
        default:
            return false;
    }
}

void 
gfxPlatform::GetLangPrefs(eFontPrefLang aPrefLangs[], PRUint32 &aLen, eFontPrefLang aCharLang, eFontPrefLang aPageLang)
{
    if (IsLangCJK(aCharLang)) {
        AppendCJKPrefLangs(aPrefLangs, aLen, aCharLang, aPageLang);
    } else {
        AppendPrefLang(aPrefLangs, aLen, aCharLang);
    }

    AppendPrefLang(aPrefLangs, aLen, eFontPrefLang_Others);
}

void
gfxPlatform::AppendCJKPrefLangs(eFontPrefLang aPrefLangs[], PRUint32 &aLen, eFontPrefLang aCharLang, eFontPrefLang aPageLang)
{
    // prefer the lang specified by the page *if* CJK
    if (IsLangCJK(aPageLang)) {
        AppendPrefLang(aPrefLangs, aLen, aPageLang);
    }
    
    // if not set up, set up the default CJK order, based on accept lang settings and locale
    if (mCJKPrefLangs.Length() == 0) {
    
        // temp array
        eFontPrefLang tempPrefLangs[kMaxLenPrefLangList];
        PRUint32 tempLen = 0;
        
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
                lang.CompressWhitespace(false, true);
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

        do { // to allow 'break' to abort this block if a call fails
            nsresult rv;
            nsCOMPtr<nsILocaleService> ls =
                do_GetService(NS_LOCALESERVICE_CONTRACTID, &rv);
            if (NS_FAILED(rv))
                break;

            nsCOMPtr<nsILocale> appLocale;
            rv = ls->GetApplicationLocale(getter_AddRefs(appLocale));
            if (NS_FAILED(rv))
                break;

            nsString localeStr;
            rv = appLocale->
                GetCategory(NS_LITERAL_STRING(NSILOCALE_MESSAGE), localeStr);
            if (NS_FAILED(rv))
                break;

            const nsAString& lang = Substring(localeStr, 0, 2);
            if (lang.EqualsLiteral("ja")) {
                AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_Japanese);
            } else if (lang.EqualsLiteral("zh")) {
                const nsAString& region = Substring(localeStr, 3, 2);
                if (region.EqualsLiteral("CN")) {
                    AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_ChineseCN);
                } else if (region.EqualsLiteral("TW")) {
                    AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_ChineseTW);
                } else if (region.EqualsLiteral("HK")) {
                    AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_ChineseHK);
                }
            } else if (lang.EqualsLiteral("ko")) {
                AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_Korean);
            }
        } while (0);

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

void 
gfxPlatform::AppendPrefLang(eFontPrefLang aPrefLangs[], PRUint32& aLen, eFontPrefLang aAddLang)
{
    if (aLen >= kMaxLenPrefLangList) return;
    
    // make sure
    PRUint32  i = 0;
    while (i < aLen && aPrefLangs[i] != aAddLang) {
        i++;
    }
    
    if (i == aLen) {
        aPrefLangs[aLen] = aAddLang;
        aLen++;
    }
}

eCMSMode
gfxPlatform::GetCMSMode()
{
    if (gCMSInitialized == false) {
        gCMSInitialized = true;
        nsresult rv;

        PRInt32 mode;
        rv = Preferences::GetInt("gfx.color_management.mode", &mode);
        if (NS_SUCCEEDED(rv) && (mode >= 0) && (mode < eCMSMode_AllCount)) {
            gCMSMode = static_cast<eCMSMode>(mode);
        }

        bool enableV4;
        rv = Preferences::GetBool("gfx.color_management.enablev4", &enableV4);
        if (NS_SUCCEEDED(rv) && enableV4) {
            qcms_enable_iccv4();
        }
    }
    return gCMSMode;
}

/* Chris Murphy (CM consultant) suggests this as a default in the event that we
cannot reproduce relative + Black Point Compensation.  BPC brings an
unacceptable performance overhead, so we go with perceptual. */
#define INTENT_DEFAULT QCMS_INTENT_PERCEPTUAL
#define INTENT_MIN 0
#define INTENT_MAX 3

int
gfxPlatform::GetRenderingIntent()
{
    if (gCMSIntent == -2) {

        /* Try to query the pref system for a rendering intent. */
        PRInt32 pIntent;
        if (NS_SUCCEEDED(Preferences::GetInt("gfx.color_management.rendering_intent", &pIntent))) {
            /* If the pref is within range, use it as an override. */
            if ((pIntent >= INTENT_MIN) && (pIntent <= INTENT_MAX)) {
                gCMSIntent = pIntent;
            }
            /* If the pref is out of range, use embedded profile. */
            else {
                gCMSIntent = -1;
            }
        }
        /* If we didn't get a valid intent from prefs, use the default. */
        else {
            gCMSIntent = INTENT_DEFAULT;
        }
    }
    return gCMSIntent;
}

void 
gfxPlatform::TransformPixel(const gfxRGBA& in, gfxRGBA& out, qcms_transform *transform)
{

    if (transform) {
        /* we want the bytes in RGB order */
#ifdef IS_LITTLE_ENDIAN
        /* ABGR puts the bytes in |RGBA| order on little endian */
        PRUint32 packed = in.Packed(gfxRGBA::PACKED_ABGR);
        qcms_transform_data(transform,
                       (PRUint8 *)&packed, (PRUint8 *)&packed,
                       1);
        out.~gfxRGBA();
        new (&out) gfxRGBA(packed, gfxRGBA::PACKED_ABGR);
#else
        /* ARGB puts the bytes in |ARGB| order on big endian */
        PRUint32 packed = in.Packed(gfxRGBA::PACKED_ARGB);
        /* add one to move past the alpha byte */
        qcms_transform_data(transform,
                       (PRUint8 *)&packed + 1, (PRUint8 *)&packed + 1,
                       1);
        out.~gfxRGBA();
        new (&out) gfxRGBA(packed, gfxRGBA::PACKED_ARGB);
#endif
    }

    else if (&out != &in)
        out = in;
}

qcms_profile *
gfxPlatform::GetPlatformCMSOutputProfile()
{
    return nsnull;
}

qcms_profile *
gfxPlatform::GetCMSOutputProfile()
{
    if (!gCMSOutputProfile) {
        NS_TIME_FUNCTION;

        /* Determine if we're using the internal override to force sRGB as
           an output profile for reftests. See Bug 452125.

           Note that we don't normally (outside of tests) set a
           default value of this preference, which means nsIPrefBranch::GetBoolPref
           will typically throw (and leave its out-param untouched).
         */
        if (Preferences::GetBool("gfx.color_management.force_srgb", false)) {
            gCMSOutputProfile = GetCMSsRGBProfile();
        }

        if (!gCMSOutputProfile) {
            nsAdoptingCString fname = Preferences::GetCString("gfx.color_management.display_profile");
            if (!fname.IsEmpty()) {
                gCMSOutputProfile = qcms_profile_from_path(fname);
            }
        }

        if (!gCMSOutputProfile) {
            gCMSOutputProfile =
                gfxPlatform::GetPlatform()->GetPlatformCMSOutputProfile();
        }

        /* Determine if the profile looks bogus. If so, close the profile
         * and use sRGB instead. See bug 460629, */
        if (gCMSOutputProfile && qcms_profile_is_bogus(gCMSOutputProfile)) {
            NS_ASSERTION(gCMSOutputProfile != GetCMSsRGBProfile(),
                         "Builtin sRGB profile tagged as bogus!!!");
            qcms_profile_release(gCMSOutputProfile);
            gCMSOutputProfile = nsnull;
        }

        if (!gCMSOutputProfile) {
            gCMSOutputProfile = GetCMSsRGBProfile();
        }
        /* Precache the LUT16 Interpolations for the output profile. See 
           bug 444661 for details. */
        qcms_profile_precache_output_transform(gCMSOutputProfile);
    }

    return gCMSOutputProfile;
}

qcms_profile *
gfxPlatform::GetCMSsRGBProfile()
{
    if (!gCMSsRGBProfile) {

        /* Create the profile using qcms. */
        gCMSsRGBProfile = qcms_profile_sRGB();
    }
    return gCMSsRGBProfile;
}

qcms_transform *
gfxPlatform::GetCMSRGBTransform()
{
    if (!gCMSRGBTransform) {
        qcms_profile *inProfile, *outProfile;
        outProfile = GetCMSOutputProfile();
        inProfile = GetCMSsRGBProfile();

        if (!inProfile || !outProfile)
            return nsnull;

        gCMSRGBTransform = qcms_transform_create(inProfile, QCMS_DATA_RGB_8,
                                              outProfile, QCMS_DATA_RGB_8,
                                             QCMS_INTENT_PERCEPTUAL);
    }

    return gCMSRGBTransform;
}

qcms_transform *
gfxPlatform::GetCMSInverseRGBTransform()
{
    if (!gCMSInverseRGBTransform) {
        qcms_profile *inProfile, *outProfile;
        inProfile = GetCMSOutputProfile();
        outProfile = GetCMSsRGBProfile();

        if (!inProfile || !outProfile)
            return nsnull;

        gCMSInverseRGBTransform = qcms_transform_create(inProfile, QCMS_DATA_RGB_8,
                                                     outProfile, QCMS_DATA_RGB_8,
                                                     QCMS_INTENT_PERCEPTUAL);
    }

    return gCMSInverseRGBTransform;
}

qcms_transform *
gfxPlatform::GetCMSRGBATransform()
{
    if (!gCMSRGBATransform) {
        qcms_profile *inProfile, *outProfile;
        outProfile = GetCMSOutputProfile();
        inProfile = GetCMSsRGBProfile();

        if (!inProfile || !outProfile)
            return nsnull;

        gCMSRGBATransform = qcms_transform_create(inProfile, QCMS_DATA_RGBA_8,
                                               outProfile, QCMS_DATA_RGBA_8,
                                               QCMS_INTENT_PERCEPTUAL);
    }

    return gCMSRGBATransform;
}

/* Shuts down various transforms and profiles for CMS. */
static void ShutdownCMS()
{

    if (gCMSRGBTransform) {
        qcms_transform_release(gCMSRGBTransform);
        gCMSRGBTransform = nsnull;
    }
    if (gCMSInverseRGBTransform) {
        qcms_transform_release(gCMSInverseRGBTransform);
        gCMSInverseRGBTransform = nsnull;
    }
    if (gCMSRGBATransform) {
        qcms_transform_release(gCMSRGBATransform);
        gCMSRGBATransform = nsnull;
    }
    if (gCMSOutputProfile) {
        qcms_profile_release(gCMSOutputProfile);

        // handle the aliased case
        if (gCMSsRGBProfile == gCMSOutputProfile)
            gCMSsRGBProfile = nsnull;
        gCMSOutputProfile = nsnull;
    }
    if (gCMSsRGBProfile) {
        qcms_profile_release(gCMSsRGBProfile);
        gCMSsRGBProfile = nsnull;
    }

    // Reset the state variables
    gCMSIntent = -2;
    gCMSMode = eCMSMode_Off;
    gCMSInitialized = false;
}

static void MigratePrefs()
{
    /* Migrate from the boolean color_management.enabled pref - we now use
       color_management.mode. */
    if (Preferences::HasUserValue("gfx.color_management.enabled")) {
        if (Preferences::GetBool("gfx.color_management.enabled", false)) {
            Preferences::SetInt("gfx.color_management.mode", static_cast<PRInt32>(eCMSMode_All));
        }
        Preferences::ClearUser("gfx.color_management.enabled");
    }
}

// default SetupClusterBoundaries, based on Unicode properties;
// platform subclasses may override if they wish
void
gfxPlatform::SetupClusterBoundaries(gfxTextRun *aTextRun, const PRUnichar *aString)
{
    if (aTextRun->GetFlags() & gfxTextRunFactory::TEXT_IS_8BIT) {
        // 8-bit text doesn't have clusters.
        // XXX is this true in all languages???
        // behdad: don't think so.  Czech for example IIRC has a
        // 'ch' grapheme.
        // jfkthame: but that's not expected to behave as a grapheme cluster
        // for selection/editing/etc.
        return;
    }

    gfxTextRun::CompressedGlyph extendCluster;
    extendCluster.SetComplex(false, true, 0);

    PRUint32 i, length = aTextRun->GetLength();
    gfxUnicodeProperties::HSType hangulState = gfxUnicodeProperties::HST_NONE;

    for (i = 0; i < length; ++i) {
        bool surrogatePair = false;
        PRUint32 ch = aString[i];
        if (NS_IS_HIGH_SURROGATE(ch) &&
            i < length - 1 && NS_IS_LOW_SURROGATE(aString[i+1]))
        {
            ch = SURROGATE_TO_UCS4(ch, aString[i+1]);
            surrogatePair = true;
        }

        PRUint8 category = gfxUnicodeProperties::GetGeneralCategory(ch);
        gfxUnicodeProperties::HSType hangulType = gfxUnicodeProperties::HST_NONE;

        // combining marks extend the cluster
        if ((category >= HB_CATEGORY_COMBINING_MARK &&
             category <= HB_CATEGORY_NON_SPACING_MARK) ||
            (ch >= 0x200c && ch <= 0x200d) || // ZWJ, ZWNJ
            (ch >= 0xff9e && ch <= 0xff9f))   // katakana sound marks
        {
            if (i > 0) {
                aTextRun->SetGlyphs(i, extendCluster, nsnull);
            }
        } else if (category == HB_CATEGORY_OTHER_LETTER) {
            // handle special cases in Letter_Other category
#if 0
            // Currently disabled. This would follow the UAX#29 specification
            // for extended grapheme clusters, but this is not favored by
            // Thai users, at least for editing behavior.
            // See discussion of equivalent Pango issue in bug 474068 and
            // upstream at https://bugzilla.gnome.org/show_bug.cgi?id=576156.

            if ((ch & ~0xff) == 0x0e00) {
                // specific Thai & Lao (U+0Exx) chars that extend the cluster
                if ( ch == 0x0e30 ||
                    (ch >= 0x0e32 && ch <= 0x0e33) ||
                     ch == 0x0e45 ||
                     ch == 0x0eb0 ||
                    (ch >= 0x0eb2 && ch <= 0x0eb3))
                {
                    if (i > 0) {
                        aTextRun->SetGlyphs(i, extendCluster, nsnull);
                    }
                }
                else if ((ch >= 0x0e40 && ch <= 0x0e44) ||
                         (ch >= 0x0ec0 && ch <= 0x0ec4))
                {
                    // characters that are prepended to the following cluster
                    if (i < length - 1) {
                        aTextRun->SetGlyphs(i+1, extendCluster, nsnull);
                    }
                }
            } else
#endif
            if ((ch & ~0xff) == 0x1100 ||
                (ch >= 0xa960 && ch <= 0xa97f) ||
                (ch >= 0xac00 && ch <= 0xd7ff))
            {
                // no break within Hangul syllables
                hangulType = gfxUnicodeProperties::GetHangulSyllableType(ch);
                switch (hangulType) {
                case gfxUnicodeProperties::HST_L:
                case gfxUnicodeProperties::HST_LV:
                case gfxUnicodeProperties::HST_LVT:
                    if (hangulState == gfxUnicodeProperties::HST_L) {
                        aTextRun->SetGlyphs(i, extendCluster, nsnull);
                    }
                    break;
                case gfxUnicodeProperties::HST_V:
                    if ( (hangulState != gfxUnicodeProperties::HST_NONE) &&
                        !(hangulState & gfxUnicodeProperties::HST_T))
                    {
                        aTextRun->SetGlyphs(i, extendCluster, nsnull);
                    }
                    break;
                case gfxUnicodeProperties::HST_T:
                    if (hangulState & (gfxUnicodeProperties::HST_V |
                                       gfxUnicodeProperties::HST_T))
                    {
                        aTextRun->SetGlyphs(i, extendCluster, nsnull);
                    }
                    break;
                default:
                    break;
                }
            }
        }

        if (surrogatePair) {
            ++i;
            aTextRun->SetGlyphs(i, extendCluster, nsnull);
        }

        hangulState = hangulType;
    }
}

void
gfxPlatform::FontsPrefsChanged(const char *aPref)
{
    NS_ASSERTION(aPref != nsnull, "null preference");
    if (!strcmp(GFX_DOWNLOADABLE_FONTS_ENABLED, aPref)) {
        mAllowDownloadableFonts = UNINITIALIZED_VALUE;
    } else if (!strcmp(GFX_DOWNLOADABLE_FONTS_SANITIZE, aPref)) {
        mDownloadableFontsSanitize = UNINITIALIZED_VALUE;
#ifdef MOZ_GRAPHITE
    } else if (!strcmp(GFX_PREF_GRAPHITE_SHAPING, aPref)) {
        mGraphiteShapingEnabled = UNINITIALIZED_VALUE;
        gfxTextRunWordCache::Flush();
        gfxFontCache::GetCache()->AgeAllGenerations();
#endif
    } else if (!strcmp(GFX_PREF_HARFBUZZ_SCRIPTS, aPref)) {
        mUseHarfBuzzScripts = UNINITIALIZED_VALUE;
        gfxTextRunWordCache::Flush();
        gfxFontCache::GetCache()->AgeAllGenerations();
    }
}


PRLogModuleInfo*
gfxPlatform::GetLog(eGfxLog aWhichLog)
{
#ifdef PR_LOGGING
    switch (aWhichLog) {
    case eGfxLog_fontlist:
        return sFontlistLog;
        break;
    case eGfxLog_fontinit:
        return sFontInitLog;
        break;
    case eGfxLog_textrun:
        return sTextrunLog;
        break;
    case eGfxLog_textrunui:
        return sTextrunuiLog;
        break;
    default:
        break;
    }

    return nsnull;
#else
    return nsnull;
#endif
}
