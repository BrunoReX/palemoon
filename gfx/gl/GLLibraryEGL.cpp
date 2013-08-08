/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GLLibraryEGL.h"

#include "gfxCrashReporterUtils.h"
#include "mozilla/Preferences.h"
#include "nsDirectoryServiceDefs.h"

namespace mozilla {
namespace gl {

// should match the order of EGLExtensions, and be null-terminated.
static const char *sExtensionNames[] = {
    "EGL_KHR_image_base",
    "EGL_KHR_image_pixmap",
    "EGL_KHR_gl_texture_2D_image",
    "EGL_KHR_lock_surface",
    "EGL_ANGLE_surface_d3d_texture_2d_share_handle",
    "EGL_EXT_create_context_robustness",
    "EGL_KHR_image",
    nsnull
};

#if defined(ANDROID)

static PRLibrary* LoadApitraceLibrary()
{
    static PRLibrary* sApitraceLibrary = NULL;

    if (sApitraceLibrary)
        return sApitraceLibrary;

    nsCString logFile = Preferences::GetCString("gfx.apitrace.logfile");

    if (logFile.IsEmpty()) {
        logFile = "firefox.trace";
    }

    // The firefox process can't write to /data/local, but it can write
    // to $GRE_HOME/
    nsCAutoString logPath;
    logPath.AppendPrintf("%s/%s", getenv("GRE_HOME"), logFile.get());

    // apitrace uses the TRACE_FILE environment variable to determine where
    // to log trace output to
    printf_stderr("Logging GL tracing output to %s", logPath.get());
    setenv("TRACE_FILE", logPath.get(), false);

    printf_stderr("Attempting load of %s\n", APITRACE_LIB);

    sApitraceLibrary = PR_LoadLibrary(APITRACE_LIB);

    return sApitraceLibrary;
}

#endif // ANDROID

bool
GLLibraryEGL::EnsureInitialized()
{
    nsresult rv;

    if (mInitialized) {
        return true;
    }

    mozilla::ScopedGfxFeatureReporter reporter("EGL");

#ifdef XP_WIN
    if (!mEGLLibrary) {
        // On Windows, the GLESv2 and EGL libraries are shipped with libxul and
        // we should look for them there. We have to load the libs in this
        // order, because libEGL.dll depends on libGLESv2.dll.

        nsCOMPtr<nsIFile> libraryFile;

        nsCOMPtr<nsIProperties> dirService =
            do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID);
        if (!dirService)
            return false;

        rv = dirService->Get(NS_GRE_DIR, NS_GET_IID(nsIFile),
                             getter_AddRefs(libraryFile));
        if (NS_FAILED(rv))
            return false;

        libraryFile->Append(NS_LITERAL_STRING("libGLESv2.dll"));
        PRLibrary* glesv2lib = nsnull;

        libraryFile->Load(&glesv2lib);

        // Intentionally leak glesv2lib
    
        libraryFile->SetLeafName(NS_LITERAL_STRING("libEGL.dll"));
        rv = libraryFile->Load(&mEGLLibrary);
        if (NS_FAILED(rv)) {
            NS_WARNING("Couldn't load libEGL.dll, canvas3d will be disabled.");
            return false;
        }
    }
#else // !Windows

    // On non-Windows (Android) we use system copies of libEGL. We look for
    // the APITrace lib, libEGL.so, and libEGL.so.1 in that order.

#if defined(ANDROID)
    if (!mEGLLibrary)
        mEGLLibrary = LoadApitraceLibrary();
#endif

    if (!mEGLLibrary) {
        printf_stderr("Attempting load of libEGL.so\n");
        mEGLLibrary = PR_LoadLibrary("libEGL.so");
    }
#if defined(XP_UNIX)
    if (!mEGLLibrary) {
        mEGLLibrary = PR_LoadLibrary("libEGL.so.1");
    }
#endif

    if (!mEGLLibrary) {
        NS_WARNING("Couldn't load EGL LIB.");
        return false;
    }

#endif // !Windows

#define SYMBOL(name) \
{ (PRFuncPtr*) &mSymbols.f##name, { "egl" #name, NULL } }

    GLLibraryLoader::SymLoadStruct earlySymbols[] = {
        SYMBOL(GetDisplay),
        SYMBOL(GetCurrentSurface),
        SYMBOL(GetCurrentContext),
        SYMBOL(MakeCurrent),
        SYMBOL(DestroyContext),
        SYMBOL(CreateContext),
        SYMBOL(DestroySurface),
        SYMBOL(CreateWindowSurface),
        SYMBOL(CreatePbufferSurface),
        SYMBOL(CreatePixmapSurface),
        SYMBOL(BindAPI),
        SYMBOL(Initialize),
        SYMBOL(ChooseConfig),
        SYMBOL(GetError),
        SYMBOL(GetConfigs),
        SYMBOL(GetConfigAttrib),
        SYMBOL(WaitNative),
        SYMBOL(GetProcAddress),
        SYMBOL(SwapBuffers),
        SYMBOL(CopyBuffers),
        SYMBOL(QueryString),
        SYMBOL(QueryContext),
        SYMBOL(BindTexImage),
        SYMBOL(ReleaseTexImage),
        SYMBOL(QuerySurface),
        { NULL, { NULL } }
    };

    if (!GLLibraryLoader::LoadSymbols(mEGLLibrary, &earlySymbols[0])) {
        NS_WARNING("Couldn't find required entry points in EGL library (early init)");
        return false;
    }

#if defined(MOZ_X11) && defined(MOZ_EGL_XRENDER_COMPOSITE)
    mEGLDisplay = fGetDisplay((EGLNativeDisplayType) gdk_x11_get_default_xdisplay());
#else
    mEGLDisplay = fGetDisplay(EGL_DEFAULT_DISPLAY);
#endif
    if (!fInitialize(mEGLDisplay, NULL, NULL))
        return false;

    const char *vendor = (const char*) fQueryString(mEGLDisplay, LOCAL_EGL_VENDOR);
    if (vendor && (strstr(vendor, "TransGaming") != 0 || strstr(vendor, "Google Inc.") != 0)) {
        mIsANGLE = true;
    }
    
    InitExtensions();

    GLLibraryLoader::PlatformLookupFunction lookupFunction =
            (GLLibraryLoader::PlatformLookupFunction)mSymbols.fGetProcAddress;

    if (IsExtensionSupported(KHR_image) || IsExtensionSupported(KHR_image_base)) {
        GLLibraryLoader::SymLoadStruct imageSymbols[] = {
            { (PRFuncPtr*) &mSymbols.fCreateImage,  { "eglCreateImageKHR",  nsnull } },
            { (PRFuncPtr*) &mSymbols.fDestroyImage, { "eglDestroyImageKHR", nsnull } },
            { nsnull, { nsnull } }
        };

        bool success = GLLibraryLoader::LoadSymbols(mEGLLibrary,
                                                    &imageSymbols[0],
                                                    lookupFunction);
        if (!success) {
            NS_ERROR("EGL supports KHR_image(_base) without exposing its functions!");

            MarkExtensionUnsupported(KHR_image);
            MarkExtensionUnsupported(KHR_image_base);
            MarkExtensionUnsupported(KHR_image_pixmap);

            mSymbols.fCreateImage = nsnull;
            mSymbols.fDestroyImage = nsnull;
        }
    } else {
        MarkExtensionUnsupported(KHR_image_pixmap);
    }

    if (IsExtensionSupported(KHR_lock_surface)) {
        GLLibraryLoader::SymLoadStruct lockSymbols[] = {
            { (PRFuncPtr*) &mSymbols.fLockSurface,   { "eglLockSurfaceKHR",   nsnull } },
            { (PRFuncPtr*) &mSymbols.fUnlockSurface, { "eglUnlockSurfaceKHR", nsnull } },
            { nsnull, { nsnull } }
        };

        bool success = GLLibraryLoader::LoadSymbols(mEGLLibrary,
                                                    &lockSymbols[0],
                                                    lookupFunction);
        if (!success) {
            NS_ERROR("EGL supports KHR_lock_surface without exposing its functions!");

            MarkExtensionUnsupported(KHR_lock_surface);

            mSymbols.fLockSurface = nsnull;
            mSymbols.fUnlockSurface = nsnull;
        }
    }

    if (IsExtensionSupported(ANGLE_surface_d3d_texture_2d_share_handle)) {
        GLLibraryLoader::SymLoadStruct d3dSymbols[] = {
            { (PRFuncPtr*) &mSymbols.fQuerySurfacePointerANGLE, { "eglQuerySurfacePointerANGLE", nsnull } },
            { nsnull, { nsnull } }
        };

        bool success = GLLibraryLoader::LoadSymbols(mEGLLibrary,
                                                    &d3dSymbols[0],
                                                    lookupFunction);
        if (!success) {
            NS_ERROR("EGL supports ANGLE_surface_d3d_texture_2d_share_handle without exposing its functions!");

            MarkExtensionUnsupported(ANGLE_surface_d3d_texture_2d_share_handle);

            mSymbols.fQuerySurfacePointerANGLE = nsnull;
        }
    }

    mInitialized = true;
    reporter.SetSuccessful();
    return true;
}

void
GLLibraryEGL::InitExtensions()
{
    const char *extensions = (const char*)fQueryString(mEGLDisplay, LOCAL_EGL_EXTENSIONS);

    if (!extensions) {
        NS_WARNING("Failed to load EGL extension list!");
        return;
    }

#ifdef DEBUG
    // If DEBUG, then be verbose the first time we're run.
    static bool firstVerboseRun = true;
#else
    // Non-DEBUG, so never spew.
    const bool firstVerboseRun = false;
#endif

    if (firstVerboseRun) {
        printf_stderr("Extensions: %s 0x%02x\n", extensions, extensions[0]);
        printf_stderr("Extensions length: %d\n", strlen(extensions));
    }

    mAvailableExtensions.Load(extensions, sExtensionNames, firstVerboseRun);

#ifdef DEBUG
    firstVerboseRun = false;
#endif
}

void
GLLibraryEGL::DumpEGLConfig(EGLConfig cfg)
{
    int attrval;
    int err;

#define ATTR(_x) do {                                                   \
        fGetConfigAttrib(mEGLDisplay, cfg, LOCAL_EGL_##_x, &attrval);  \
        if ((err = fGetError()) != 0x3000) {                        \
            printf_stderr("  %s: ERROR (0x%04x)\n", #_x, err);        \
        } else {                                                    \
            printf_stderr("  %s: %d (0x%04x)\n", #_x, attrval, attrval); \
        }                                                           \
    } while(0)

    printf_stderr("EGL Config: %d [%p]\n", (int)(intptr_t)cfg, cfg);

    ATTR(BUFFER_SIZE);
    ATTR(ALPHA_SIZE);
    ATTR(BLUE_SIZE);
    ATTR(GREEN_SIZE);
    ATTR(RED_SIZE);
    ATTR(DEPTH_SIZE);
    ATTR(STENCIL_SIZE);
    ATTR(CONFIG_CAVEAT);
    ATTR(CONFIG_ID);
    ATTR(LEVEL);
    ATTR(MAX_PBUFFER_HEIGHT);
    ATTR(MAX_PBUFFER_PIXELS);
    ATTR(MAX_PBUFFER_WIDTH);
    ATTR(NATIVE_RENDERABLE);
    ATTR(NATIVE_VISUAL_ID);
    ATTR(NATIVE_VISUAL_TYPE);
    ATTR(PRESERVED_RESOURCES);
    ATTR(SAMPLES);
    ATTR(SAMPLE_BUFFERS);
    ATTR(SURFACE_TYPE);
    ATTR(TRANSPARENT_TYPE);
    ATTR(TRANSPARENT_RED_VALUE);
    ATTR(TRANSPARENT_GREEN_VALUE);
    ATTR(TRANSPARENT_BLUE_VALUE);
    ATTR(BIND_TO_TEXTURE_RGB);
    ATTR(BIND_TO_TEXTURE_RGBA);
    ATTR(MIN_SWAP_INTERVAL);
    ATTR(MAX_SWAP_INTERVAL);
    ATTR(LUMINANCE_SIZE);
    ATTR(ALPHA_MASK_SIZE);
    ATTR(COLOR_BUFFER_TYPE);
    ATTR(RENDERABLE_TYPE);
    ATTR(CONFORMANT);

#undef ATTR
}

void
GLLibraryEGL::DumpEGLConfigs()
{
    int nc = 0;
    fGetConfigs(mEGLDisplay, NULL, 0, &nc);
    EGLConfig *ec = new EGLConfig[nc];
    fGetConfigs(mEGLDisplay, ec, nc, &nc);

    for (int i = 0; i < nc; ++i) {
        printf_stderr ("========= EGL Config %d ========\n", i);
        DumpEGLConfig(ec[i]);
    }

    delete [] ec;
}

} /* namespace gl */
} /* namespace mozilla */

