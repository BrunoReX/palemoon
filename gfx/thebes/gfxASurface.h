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
 * The Original Code is Oracle Corporation code.
 *
 * The Initial Developer of the Original Code is Oracle Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Stuart Parmenter <pavlov@pavlov.net>
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

#ifndef GFX_ASURFACE_H
#define GFX_ASURFACE_H

#include "gfxTypes.h"
#include "gfxRect.h"
#include "nsAutoPtr.h"

typedef struct _cairo_surface cairo_surface_t;
typedef struct _cairo_user_data_key cairo_user_data_key_t;

typedef void (*thebes_destroy_func_t) (void *data);

class gfxImageSurface;
struct nsIntPoint;
struct nsIntRect;

/**
 * A surface is something you can draw on. Instantiate a subclass of this
 * abstract class, and use gfxContext to draw on this surface.
 */
class THEBES_API gfxASurface {
public:
    nsrefcnt AddRef(void);
    nsrefcnt Release(void);

public:
    /**
     * The format for an image surface. For all formats with alpha data, 0
     * means transparent, 1 or 255 means fully opaque.
     */
    typedef enum {
        ImageFormatARGB32, ///< ARGB data in native endianness, using premultiplied alpha
        ImageFormatRGB24,  ///< xRGB data in native endianness
        ImageFormatA8,     ///< Only an alpha channel
        ImageFormatA1,     ///< Packed transparency information (one byte refers to 8 pixels)
        ImageFormatRGB16_565,  ///< RGB_565 data in native endianness
        ImageFormatUnknown
    } gfxImageFormat;

    typedef enum {
        SurfaceTypeImage,
        SurfaceTypePDF,
        SurfaceTypePS,
        SurfaceTypeXlib,
        SurfaceTypeXcb,
        SurfaceTypeGlitz,
        SurfaceTypeQuartz,
        SurfaceTypeWin32,
        SurfaceTypeBeOS,
        SurfaceTypeDirectFB,
        SurfaceTypeSVG,
        SurfaceTypeOS2,
        SurfaceTypeWin32Printing,
        SurfaceTypeQuartzImage,
        SurfaceTypeScript,
        SurfaceTypeQPainter,
        SurfaceTypeRecording,
        SurfaceTypeVG,
        SurfaceTypeGL,
        SurfaceTypeDRM,
        SurfaceTypeTee,
        SurfaceTypeXML,
        SurfaceTypeSkia,
        SurfaceTypeSubsurface,
        SurfaceTypeD2D,
        SurfaceTypeMax
    } gfxSurfaceType;

    typedef enum {
        CONTENT_COLOR       = 0x1000,
        CONTENT_ALPHA       = 0x2000,
        CONTENT_COLOR_ALPHA = 0x3000
    } gfxContentType;

    /** Wrap the given cairo surface and return a gfxASurface for it.
     * This adds a reference to csurf (owned by the returned gfxASurface).
     */
    static already_AddRefed<gfxASurface> Wrap(cairo_surface_t *csurf);

    /*** this DOES NOT addref the surface */
    cairo_surface_t *CairoSurface() {
        NS_ASSERTION(mSurface != nsnull, "gfxASurface::CairoSurface called with mSurface == nsnull!");
        return mSurface;
    }

    gfxSurfaceType GetType() const;

    gfxContentType GetContentType() const;

    void SetDeviceOffset(const gfxPoint& offset);
    gfxPoint GetDeviceOffset() const;

    virtual PRBool GetRotateForLandscape() { return PR_FALSE; }

    void Flush() const;
    void MarkDirty();
    void MarkDirty(const gfxRect& r);

    /* Printing backend functions */
    virtual nsresult BeginPrinting(const nsAString& aTitle, const nsAString& aPrintToFileName);
    virtual nsresult EndPrinting();
    virtual nsresult AbortPrinting();
    virtual nsresult BeginPage();
    virtual nsresult EndPage();

    void SetData(const cairo_user_data_key_t *key,
                 void *user_data,
                 thebes_destroy_func_t destroy);
    void *GetData(const cairo_user_data_key_t *key);

    virtual void Finish();

    /**
     * Create an offscreen surface that can be efficiently copied into
     * this surface (at least if tiling is not involved).
     * Returns null on error.
     */
    virtual already_AddRefed<gfxASurface> CreateSimilarSurface(gfxContentType aType,
                                                               const gfxIntSize& aSize);

    /**
     * Returns an image surface for this surface, or nsnull if not supported.
     * This will not copy image data, just wraps an image surface around
     * pixel data already available in memory.
     */
    virtual already_AddRefed<gfxImageSurface> GetAsImageSurface()
    {
      return nsnull;
    }

    int CairoStatus();

    /* Make sure that the given dimensions don't overflow a 32-bit signed int
     * using 4 bytes per pixel; optionally, make sure that either dimension
     * doesn't exceed the given limit.
     */
    static PRBool CheckSurfaceSize(const gfxIntSize& sz, PRInt32 limit = 0);

    /* Provide a stride value that will respect all alignment requirements of
     * the accelerated image-rendering code.
     */
    static PRInt32 FormatStrideForWidth(gfxImageFormat format, PRInt32 width);

    /* Return the default set of context flags for this surface; these are
     * hints to the context about any special rendering considerations.  See
     * gfxContext::SetFlag for documentation.
     */
    virtual PRInt32 GetDefaultContextFlags() const { return 0; }

    static gfxContentType ContentFromFormat(gfxImageFormat format);
    static gfxImageFormat FormatFromContent(gfxContentType format);

    void SetSubpixelAntialiasingEnabled(PRBool aEnabled);
    PRBool GetSubpixelAntialiasingEnabled();

    /**
     * Record number of bytes for given surface type.  Use positive bytes
     * for allocations and negative bytes for deallocations.
     */
    static void RecordMemoryUsedForSurfaceType(gfxASurface::gfxSurfaceType aType,
                                               PRInt32 aBytes);

    /**
     * Same as above, but use current surface type as returned by GetType().
     * The bytes will be accumulated until RecordMemoryFreed is called,
     * in which case the value that was recorded for this surface will
     * be freed.
     */
    void RecordMemoryUsed(PRInt32 aBytes);
    void RecordMemoryFreed();

    virtual PRInt32 KnownMemoryUsed() { return mBytesRecorded; }

    /**
     * The memory used by this surface (as reported by KnownMemoryUsed()) can
     * either live in this process's heap, in this process but outside the
     * heap, or in another process altogether.
     */
    enum MemoryLocation {
      MEMORY_IN_PROCESS_HEAP,
      MEMORY_IN_PROCESS_NONHEAP,
      MEMORY_OUT_OF_PROCESS
    };

    /**
     * Where does this surface's memory live?  By default, we say it's in this
     * process's heap.
     */
    virtual MemoryLocation GetMemoryLocation() const;

    static PRInt32 BytePerPixelFromFormat(gfxImageFormat format);

    virtual const gfxIntSize GetSize() const { return gfxIntSize(-1, -1); }

    void DumpAsDataURL();

    void SetOpaqueRect(const gfxRect& aRect) {
        if (aRect.IsEmpty()) {
            mOpaqueRect = nsnull;
        } else if (mOpaqueRect) {
            *mOpaqueRect = aRect;
        } else {
            mOpaqueRect = new gfxRect(aRect);
        }
    }
    const gfxRect& GetOpaqueRect() {
        if (mOpaqueRect)
            return *mOpaqueRect;
        static const gfxRect empty(0, 0, 0, 0);
        return empty;
    }

    /**
     * Move the pixels in |aSourceRect| to |aDestTopLeft|.  Like with
     * memmove(), |aSourceRect| and the rectangle defined by
     * |aDestTopLeft| are allowed to overlap, and the effect is
     * equivalent to copying |aSourceRect| to a scratch surface and
     * then back to |aDestTopLeft|.
     *
     * |aSourceRect| and the destination rectangle defined by
     * |aDestTopLeft| are clipped to this surface's bounds.
     */
    virtual void MovePixels(const nsIntRect& aSourceRect,
                            const nsIntPoint& aDestTopLeft);

    /**
     * Mark the surface as being allowed/not allowed to be used as a source.
     */
    void SetAllowUseAsSource(PRBool aAllow) { mAllowUseAsSource = aAllow; }
    PRBool GetAllowUseAsSource() { return mAllowUseAsSource; }

protected:
    gfxASurface() : mSurface(nsnull), mFloatingRefs(0), mBytesRecorded(0),
                    mSurfaceValid(PR_FALSE), mAllowUseAsSource(PR_TRUE)
    {
        MOZ_COUNT_CTOR(gfxASurface);
    }

    static gfxASurface* GetSurfaceWrapper(cairo_surface_t *csurf);
    static void SetSurfaceWrapper(cairo_surface_t *csurf, gfxASurface *asurf);

    /**
     * An implementation of MovePixels that assumes the backend can
     * internally handle this operation and doesn't allocate any
     * temporary surfaces.
     */
    void FastMovePixels(const nsIntRect& aSourceRect,
                        const nsIntPoint& aDestTopLeft);

    // NB: Init() *must* be called from within subclass's
    // constructors.  It's unsafe to call it after the ctor finishes;
    // leaks and use-after-frees are possible.
    void Init(cairo_surface_t *surface, PRBool existingSurface = PR_FALSE);

    virtual ~gfxASurface()
    {
        RecordMemoryFreed();

        MOZ_COUNT_DTOR(gfxASurface);
    }

    cairo_surface_t *mSurface;
    nsAutoPtr<gfxRect> mOpaqueRect;

private:
    static void SurfaceDestroyFunc(void *data);

    PRInt32 mFloatingRefs;
    PRInt32 mBytesRecorded;

protected:
    PRPackedBool mSurfaceValid;
    PRPackedBool mAllowUseAsSource;
};

/**
 * An Unknown surface; used to wrap unknown cairo_surface_t returns from cairo
 */
class THEBES_API gfxUnknownSurface : public gfxASurface {
public:
    gfxUnknownSurface(cairo_surface_t *surf) {
        Init(surf, PR_TRUE);
    }

    virtual ~gfxUnknownSurface() { }
};

#endif /* GFX_ASURFACE_H */
