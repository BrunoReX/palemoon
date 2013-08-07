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
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Stuart Parmenter <pavlov@pavlov.net>
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

#ifndef GFX_WINDOWS_PLATFORM_H
#define GFX_WINDOWS_PLATFORM_H


/**
 * XXX to get CAIRO_HAS_D2D_SURFACE, CAIRO_HAS_DWRITE_FONT
 * and cairo_win32_scaled_font_select_font
 */
#include "cairo-win32.h"

#include "gfxFontUtils.h"
#include "gfxWindowsSurface.h"
#include "gfxFont.h"
#ifdef CAIRO_HAS_DWRITE_FONT
#include "gfxDWriteFonts.h"
#endif
#include "gfxPlatform.h"
#include "gfxContext.h"

#include "nsTArray.h"
#include "nsDataHashtable.h"

#include <windows.h>
#include <objbase.h>

// Utility to get a Windows HDC from a thebes context,
// used by both GDI and Uniscribe font shapers
struct DCFromContext {
    DCFromContext(gfxContext *aContext) {
        dc = NULL;
        nsRefPtr<gfxASurface> aSurface = aContext->CurrentSurface();
        NS_ASSERTION(aSurface, "DCFromContext: null surface");
        if (aSurface &&
            (aSurface->GetType() == gfxASurface::SurfaceTypeWin32 ||
             aSurface->GetType() == gfxASurface::SurfaceTypeWin32Printing))
        {
            dc = static_cast<gfxWindowsSurface*>(aSurface.get())->GetDC();
            needsRelease = false;
            SaveDC(dc);
            cairo_scaled_font_t* scaled =
                cairo_get_scaled_font(aContext->GetCairo());
            cairo_win32_scaled_font_select_font(scaled, dc);
        }
        if (!dc) {
            dc = GetDC(NULL);
            SetGraphicsMode(dc, GM_ADVANCED);
            needsRelease = true;
        }
    }

    ~DCFromContext() {
        if (needsRelease) {
            ReleaseDC(NULL, dc);
        } else {
            RestoreDC(dc, -1);
        }
    }

    operator HDC () {
        return dc;
    }

    HDC dc;
    bool needsRelease;
};

// ClearType parameters set by running ClearType tuner
struct ClearTypeParameterInfo {
    ClearTypeParameterInfo() :
        gamma(-1), pixelStructure(-1), clearTypeLevel(-1), enhancedContrast(-1)
    { }

    nsString    displayName;  // typically just 'DISPLAY1'
    PRInt32     gamma;
    PRInt32     pixelStructure;
    PRInt32     clearTypeLevel;
    PRInt32     enhancedContrast;
};

class THEBES_API gfxWindowsPlatform : public gfxPlatform {
public:
    gfxWindowsPlatform();
    virtual ~gfxWindowsPlatform();
    static gfxWindowsPlatform *GetPlatform() {
        return (gfxWindowsPlatform*) gfxPlatform::GetPlatform();
    }

    virtual gfxPlatformFontList* CreatePlatformFontList();

    already_AddRefed<gfxASurface> CreateOffscreenSurface(const gfxIntSize& size,
                                                         gfxASurface::gfxContentType contentType);
    virtual mozilla::RefPtr<mozilla::gfx::ScaledFont>
      GetScaledFontForFont(gfxFont *aFont);
    virtual already_AddRefed<gfxASurface>
      GetThebesSurfaceForDrawTarget(mozilla::gfx::DrawTarget *aTarget);
    
    virtual bool SupportsAzure(mozilla::gfx::BackendType& aBackend);

    enum RenderMode {
        /* Use GDI and windows surfaces */
        RENDER_GDI = 0,

        /* Use 32bpp image surfaces and call StretchDIBits */
        RENDER_IMAGE_STRETCH32,

        /* Use 32bpp image surfaces, and do 32->24 conversion before calling StretchDIBits */
        RENDER_IMAGE_STRETCH24,

        /* Use Direct2D rendering */
        RENDER_DIRECT2D,

        /* max */
        RENDER_MODE_MAX
    };

    RenderMode GetRenderMode() { return mRenderMode; }
    void SetRenderMode(RenderMode rmode) { mRenderMode = rmode; }

    /**
     * Updates render mode with relation to the current preferences and
     * available devices.
     */
    void UpdateRenderMode();

    /**
     * Verifies a D2D device is present and working, will attempt to create one
     * it is non-functional or non-existant.
     *
     * \param aAttemptForce Attempt to force D2D cairo device creation by using
     * cairo device creation routines.
     */
    void VerifyD2DDevice(bool aAttemptForce);

    HDC GetScreenDC() { return mScreenDC; }

    nsresult GetFontList(nsIAtom *aLangGroup,
                         const nsACString& aGenericFamily,
                         nsTArray<nsString>& aListOfFonts);

    nsresult UpdateFontList();

    nsresult ResolveFontName(const nsAString& aFontName,
                             FontResolverCallback aCallback,
                             void *aClosure, bool& aAborted);

    nsresult GetStandardFamilyName(const nsAString& aFontName, nsAString& aFamilyName);

    gfxFontGroup *CreateFontGroup(const nsAString &aFamilies,
                                  const gfxFontStyle *aStyle,
                                  gfxUserFontSet *aUserFontSet);

    /**
     * Look up a local platform font using the full font face name (needed to support @font-face src local() )
     */
    virtual gfxFontEntry* LookupLocalFont(const gfxProxyFontEntry *aProxyEntry,
                                          const nsAString& aFontName);

    /**
     * Activate a platform font (needed to support @font-face src url() )
     */
    virtual gfxFontEntry* MakePlatformFont(const gfxProxyFontEntry *aProxyEntry,
                                           const PRUint8 *aFontData,
                                           PRUint32 aLength);

    /**
     * Check whether format is supported on a platform or not (if unclear, returns true)
     */
    virtual bool IsFontFormatSupported(nsIURI *aFontURI, PRUint32 aFormatFlags);

    /* Find a FontFamily/FontEntry object that represents a font on your system given a name */
    gfxFontFamily *FindFontFamily(const nsAString& aName);
    gfxFontEntry *FindFontEntry(const nsAString& aName, const gfxFontStyle& aFontStyle);

    bool GetPrefFontEntries(const nsCString& aLangGroup, nsTArray<nsRefPtr<gfxFontEntry> > *array);
    void SetPrefFontEntries(const nsCString& aLangGroup, nsTArray<nsRefPtr<gfxFontEntry> >& array);

    void ClearPrefFonts() { mPrefFonts.Clear(); }

    // ClearType is not always enabled even when available (e.g. Windows XP)
    // if either of these prefs are enabled and apply, use ClearType rendering
    bool UseClearTypeForDownloadableFonts();
    bool UseClearTypeAlways();

    // OS version in 16.16 major/minor form
    // based on http://msdn.microsoft.com/en-us/library/ms724834(VS.85).aspx
    enum {
        kWindowsUnknown = 0,
        kWindows2000 = 0x50000,
        kWindowsXP = 0x50001,
        kWindowsServer2003 = 0x50002,
        kWindowsVista = 0x60000,
        kWindows7 = 0x60001
    };

    static PRInt32 WindowsOSVersion(PRInt32 *aBuildNum = nsnull);

    static void GetDLLVersion(const PRUnichar *aDLLPath, nsAString& aVersion);

    // returns ClearType tuning information for each display
    static void GetCleartypeParams(nsTArray<ClearTypeParameterInfo>& aParams);

    virtual void FontsPrefsChanged(const char *aPref);

    void SetupClearTypeParams();

#ifdef CAIRO_HAS_DWRITE_FONT
    IDWriteFactory *GetDWriteFactory() { return mDWriteFactory; }
    inline bool DWriteEnabled() { return mUseDirectWrite; }
    inline DWRITE_MEASURING_MODE DWriteMeasuringMode() { return mMeasuringMode; }
#else
    inline bool DWriteEnabled() { return false; }
#endif
#ifdef CAIRO_HAS_D2D_SURFACE
    cairo_device_t *GetD2DDevice() { return mD2DDevice; }
    ID3D10Device1 *GetD3D10Device() { return mD2DDevice ? cairo_d2d_device_get_device(mD2DDevice) : nsnull; }
#endif

    static bool IsOptimus();

protected:
    RenderMode mRenderMode;

    PRInt8 mUseClearTypeForDownloadableFonts;
    PRInt8 mUseClearTypeAlways;
    HDC mScreenDC;

private:
    void Init();

    bool mUseDirectWrite;
    bool mUsingGDIFonts;

#ifdef CAIRO_HAS_DWRITE_FONT
    nsRefPtr<IDWriteFactory> mDWriteFactory;
    DWRITE_MEASURING_MODE mMeasuringMode;
#endif
#ifdef CAIRO_HAS_D2D_SURFACE
    cairo_device_t *mD2DDevice;
#endif

    virtual qcms_profile* GetPlatformCMSOutputProfile();

    // TODO: unify this with mPrefFonts (NB: holds families, not fonts) in gfxPlatformFontList
    nsDataHashtable<nsCStringHashKey, nsTArray<nsRefPtr<gfxFontEntry> > > mPrefFonts;
};

#endif /* GFX_WINDOWS_PLATFORM_H */
