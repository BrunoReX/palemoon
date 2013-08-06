//
// Copyright (c) 2002-2010 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// Surface.h: Defines the egl::Surface class, representing a drawing surface
// such as the client area of a window, including any back buffers.
// Implements EGLSurface and related functionality. [EGL 1.4] section 2.2 page 3.

#ifndef INCLUDE_SURFACE_H_
#define INCLUDE_SURFACE_H_

#define EGLAPI
#include <EGL/egl.h>
#include <d3d9.h>

#include "common/angleutils.h"

namespace egl
{
class Display;
class Config;

class Surface
{
  public:
    Surface(Display *display, const egl::Config *config, HWND window);
    Surface(Display *display, const egl::Config *config, EGLint width, EGLint height);

    ~Surface();

    void release();
    void resetSwapChain();

    HWND getWindowHandle();
    bool swap();

    virtual EGLint getWidth() const;
    virtual EGLint getHeight() const;

    virtual IDirect3DSurface9 *getRenderTarget();
    virtual IDirect3DSurface9 *getDepthStencil();

    HANDLE getShareHandle() { return mShareHandle; }

    void setSwapInterval(EGLint interval);
    bool checkForOutOfDateSwapChain();   // Returns true if swapchain changed due to resize or interval update

private:
    DISALLOW_COPY_AND_ASSIGN(Surface);

    Display *const mDisplay;
    IDirect3DSwapChain9 *mSwapChain;
    IDirect3DSurface9 *mDepthStencil;
    IDirect3DSurface9* mRenderTarget;
    IDirect3DTexture9* mOffscreenTexture;

    HANDLE mShareHandle;

    void subclassWindow();
    void unsubclassWindow();
    void resetSwapChain(int backbufferWidth, int backbufferHeight);
    static DWORD convertInterval(EGLint interval);

    const HWND mWindow;            // Window that the surface is created for.
    bool mWindowSubclassed;        // Indicates whether we successfully subclassed mWindow for WM_RESIZE hooking
    const egl::Config *mConfig;    // EGL config surface was created with
    EGLint mHeight;                // Height of surface
    EGLint mWidth;                 // Width of surface
//  EGLint horizontalResolution;   // Horizontal dot pitch
//  EGLint verticalResolution;     // Vertical dot pitch
//  EGLBoolean largestPBuffer;     // If true, create largest pbuffer possible
//  EGLBoolean mipmapTexture;      // True if texture has mipmaps
//  EGLint mipmapLevel;            // Mipmap level to render to
//  EGLenum multisampleResolve;    // Multisample resolve behavior
    EGLint mPixelAspectRatio;      // Display aspect ratio
    EGLenum mRenderBuffer;         // Render buffer
    EGLenum mSwapBehavior;         // Buffer swap behavior
//  EGLenum textureFormat;         // Format of texture: RGB, RGBA, or no texture
//  EGLenum textureTarget;         // Type of texture: 2D or no texture
//  EGLenum vgAlphaFormat;         // Alpha format for OpenVG
//  EGLenum vgColorSpace;          // Color space for OpenVG
    EGLint mSwapInterval;
    DWORD mPresentInterval;
    bool mPresentIntervalDirty;
};
}

#endif   // INCLUDE_SURFACE_H_
