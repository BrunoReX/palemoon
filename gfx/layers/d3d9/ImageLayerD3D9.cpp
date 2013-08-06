/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
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
 * The Original Code is Mozilla Corporation code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Bas Schouten <bschouten@mozilla.org>
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

#include "ImageLayerD3D9.h"
#include "gfxImageSurface.h"
#include "yuv_convert.h"
#include "nsIServiceManager.h" 
#include "nsIConsoleService.h" 
#include "nsPrintfCString.h" 
#include "Nv3DVUtils.h"

namespace mozilla {
namespace layers {

static already_AddRefed<IDirect3DTexture9>
SurfaceToTexture(IDirect3DDevice9 *aDevice,
                 gfxASurface *aSurface,
                 const gfxIntSize &aSize)
{

  nsRefPtr<gfxImageSurface> imageSurface = aSurface->GetAsImageSurface();

  if (!imageSurface) {
    imageSurface = new gfxImageSurface(aSize,
                                       gfxASurface::ImageFormatARGB32);
    
    nsRefPtr<gfxContext> context = new gfxContext(imageSurface);
    context->SetSource(aSurface);
    context->SetOperator(gfxContext::OPERATOR_SOURCE);
    context->Paint();
  }

  nsRefPtr<IDirect3DTexture9> texture;
  nsRefPtr<IDirect3DDevice9Ex> deviceEx;
  aDevice->QueryInterface(IID_IDirect3DDevice9Ex,
                          (void**)getter_AddRefs(deviceEx));

  if (deviceEx) {
    // D3D9Ex doesn't support managed textures. We could use dynamic textures
    // here but since Images are immutable that probably isn't such a great
    // idea.
    if (FAILED(aDevice->
               CreateTexture(aSize.width, aSize.height,
                             1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                             getter_AddRefs(texture), NULL)))
    {
      return NULL;
    }

    nsRefPtr<IDirect3DSurface9> surface;
    if (FAILED(aDevice->
               CreateOffscreenPlainSurface(aSize.width,
                                           aSize.height,
                                           D3DFMT_A8R8G8B8,
                                           D3DPOOL_SYSTEMMEM,
                                           getter_AddRefs(surface),
                                           NULL)))
    {
      return NULL;
    }

    D3DLOCKED_RECT lockedRect;
    surface->LockRect(&lockedRect, NULL, 0);
    for (int y = 0; y < aSize.height; y++) {
      memcpy((char*)lockedRect.pBits + lockedRect.Pitch * y,
             imageSurface->Data() + imageSurface->Stride() * y,
             aSize.width * 4);
    }
    surface->UnlockRect();
    nsRefPtr<IDirect3DSurface9> dstSurface;
    texture->GetSurfaceLevel(0, getter_AddRefs(dstSurface));
    aDevice->UpdateSurface(surface, NULL, dstSurface, NULL);
  } else {
    if (FAILED(aDevice->
               CreateTexture(aSize.width, aSize.height,
                             1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                             getter_AddRefs(texture), NULL)))
    {
      return NULL;
    }

    D3DLOCKED_RECT lockrect;
    /* lock the entire texture */
    texture->LockRect(0, &lockrect, NULL, 0);

    // copy over data. If we don't need to do any swaping we can
    // use memcpy
    for (int y = 0; y < aSize.height; y++) {
      memcpy((char*)lockrect.pBits + lockrect.Pitch * y,
             imageSurface->Data() + imageSurface->Stride() * y,
             aSize.width * 4);
    }

    texture->UnlockRect(0);
  }

  return texture.forget();
}

ImageContainerD3D9::ImageContainerD3D9(IDirect3DDevice9 *aDevice)
  : ImageContainer(nsnull)
  , mDevice(aDevice)
{
}

already_AddRefed<Image>
ImageContainerD3D9::CreateImage(const Image::Format *aFormats,
                               PRUint32 aNumFormats)
{
  if (!aNumFormats) {
    return nsnull;
  }
  nsRefPtr<Image> img;
  if (aFormats[0] == Image::PLANAR_YCBCR) {
    img = new PlanarYCbCrImageD3D9();
  } else if (aFormats[0] == Image::CAIRO_SURFACE) {
    img = new CairoImageD3D9(mDevice);
  }
  return img.forget();
}

void
ImageContainerD3D9::SetCurrentImage(Image *aImage)
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  mActiveImage = aImage;
  CurrentImageChanged();
}

already_AddRefed<Image>
ImageContainerD3D9::GetCurrentImage()
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  nsRefPtr<Image> retval = mActiveImage;
  return retval.forget();
}

already_AddRefed<gfxASurface>
ImageContainerD3D9::GetCurrentAsSurface(gfxIntSize *aSize)
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);
  if (!mActiveImage) {
    return nsnull;
  }

  if (mActiveImage->GetFormat() == Image::PLANAR_YCBCR) {
    PlanarYCbCrImageD3D9 *yuvImage =
      static_cast<PlanarYCbCrImageD3D9*>(mActiveImage.get());
    if (yuvImage->HasData()) {
      *aSize = yuvImage->mSize;
    }
  } else if (mActiveImage->GetFormat() == Image::CAIRO_SURFACE) {
    CairoImageD3D9 *cairoImage =
      static_cast<CairoImageD3D9*>(mActiveImage.get());
    *aSize = cairoImage->GetSize();
  }

  return static_cast<ImageD3D9*>(mActiveImage->GetImplData())->GetAsSurface();
}

gfxIntSize
ImageContainerD3D9::GetCurrentSize()
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);
  if (!mActiveImage) {
    return gfxIntSize(0,0);
  }
  if (mActiveImage->GetFormat() == Image::PLANAR_YCBCR) {
    PlanarYCbCrImageD3D9 *yuvImage =
      static_cast<PlanarYCbCrImageD3D9*>(mActiveImage.get());
    if (!yuvImage->HasData()) {
      return gfxIntSize(0,0);
    }
    return yuvImage->mSize;

  } else if (mActiveImage->GetFormat() == Image::CAIRO_SURFACE) {
    CairoImageD3D9 *cairoImage =
      static_cast<CairoImageD3D9*>(mActiveImage.get());
    return cairoImage->GetSize();
  }

  return gfxIntSize(0,0);
}

PRBool
ImageContainerD3D9::SetLayerManager(LayerManager *aManager)
{
  if (aManager->GetBackendType() == LayerManager::LAYERS_D3D9) {
    return PR_TRUE;
  }

  return PR_FALSE;
}

Layer*
ImageLayerD3D9::GetLayer()
{
  return this;
}

void
ImageLayerD3D9::RenderLayer()
{
  if (!GetContainer()) {
    return;
  }

  nsRefPtr<Image> image = GetContainer()->GetCurrentImage();
  if (!image) {
    return;
  }

  SetShaderTransformAndOpacity();

  if (GetContainer()->GetBackendType() != LayerManager::LAYERS_D3D9)
  {
    gfxIntSize size;
    nsRefPtr<gfxASurface> surface =
      GetContainer()->GetCurrentAsSurface(&size);
    nsRefPtr<IDirect3DTexture9> texture =
      SurfaceToTexture(device(), surface, size);

    device()->SetVertexShaderConstantF(CBvLayerQuad,
                                       ShaderConstantRect(0,
                                                          0,
                                                          size.width,
                                                          size.height),
                                       1);

    if (surface->GetContentType() == gfxASurface::CONTENT_COLOR_ALPHA) {
      mD3DManager->SetShaderMode(DeviceManagerD3D9::RGBALAYER);
    } else {
      mD3DManager->SetShaderMode(DeviceManagerD3D9::RGBLAYER);
    }

    device()->SetTexture(0, texture);
    device()->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
  } else if (image->GetFormat() == Image::PLANAR_YCBCR) {
    PlanarYCbCrImageD3D9 *yuvImage =
      static_cast<PlanarYCbCrImageD3D9*>(image.get());

    if (!yuvImage->HasData()) {
      return;
    }
    yuvImage->AllocateTextures(device());

    device()->SetVertexShaderConstantF(CBvLayerQuad,
                                       ShaderConstantRect(0,
                                                          0,
                                                          yuvImage->mSize.width,
                                                          yuvImage->mSize.height),
                                       1);

    mD3DManager->SetShaderMode(DeviceManagerD3D9::YCBCRLAYER);

    /*
     * Send 3d control data and metadata
     */
    if (mD3DManager->GetNv3DVUtils()) {
      Nv_Stereo_Mode mode;
      switch (yuvImage->mData.mStereoMode) {
      case STEREO_MODE_LEFT_RIGHT:
        mode = NV_STEREO_MODE_LEFT_RIGHT;
        break;
      case STEREO_MODE_RIGHT_LEFT:
        mode = NV_STEREO_MODE_RIGHT_LEFT;
        break;
      case STEREO_MODE_BOTTOM_TOP:
        mode = NV_STEREO_MODE_BOTTOM_TOP;
        break;
      case STEREO_MODE_TOP_BOTTOM:
        mode = NV_STEREO_MODE_TOP_BOTTOM;
        break;
      case STEREO_MODE_MONO:
        mode = NV_STEREO_MODE_MONO;
        break;
      }

      // Send control data even in mono case so driver knows to leave stereo mode.
      mD3DManager->GetNv3DVUtils()->SendNv3DVControl(mode, true, FIREFOX_3DV_APP_HANDLE);

      if (yuvImage->mData.mStereoMode != STEREO_MODE_MONO) {
        mD3DManager->GetNv3DVUtils()->SendNv3DVControl(mode, true, FIREFOX_3DV_APP_HANDLE);

        nsRefPtr<IDirect3DSurface9> renderTarget;
        device()->GetRenderTarget(0, getter_AddRefs(renderTarget));
        mD3DManager->GetNv3DVUtils()->SendNv3DVMetaData((unsigned int)yuvImage->mSize.width,
                                                        (unsigned int)yuvImage->mSize.height, (HANDLE)(yuvImage->mYTexture), (HANDLE)(renderTarget));
      }
    }

    // Linear scaling is default here, adhering to mFilter is difficult since
    // presumably even with point filtering we'll still want chroma upsampling
    // to be linear. In the current approach we can't.
    device()->SetTexture(0, yuvImage->mYTexture);
    device()->SetTexture(1, yuvImage->mCbTexture);
    device()->SetTexture(2, yuvImage->mCrTexture);

    device()->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);

  } else if (image->GetFormat() == Image::CAIRO_SURFACE) {
    CairoImageD3D9 *cairoImage =
      static_cast<CairoImageD3D9*>(image.get());
    ImageContainerD3D9 *container =
      static_cast<ImageContainerD3D9*>(GetContainer());

    if (container->device() != device()) {
      // Ensure future images get created with the right device.
      container->SetDevice(device());
    }

    if (cairoImage->device() != device()) {
      cairoImage->SetDevice(device());
    }

    device()->SetVertexShaderConstantF(CBvLayerQuad,
                                       ShaderConstantRect(0,
                                                          0,
                                                          cairoImage->GetSize().width,
                                                          cairoImage->GetSize().height),
                                       1);

    if (cairoImage->HasAlpha()) {
      mD3DManager->SetShaderMode(DeviceManagerD3D9::RGBALAYER);
    } else {
      mD3DManager->SetShaderMode(DeviceManagerD3D9::RGBLAYER);
    }

    if (mFilter == gfxPattern::FILTER_NEAREST) {
      device()->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
      device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    }
    device()->SetTexture(0, cairoImage->GetOrCreateTexture());
    device()->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
    if (mFilter == gfxPattern::FILTER_NEAREST) {
      device()->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
      device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    }
  }

  GetContainer()->NotifyPaintedImage(image);
}

PlanarYCbCrImageD3D9::PlanarYCbCrImageD3D9()
  : PlanarYCbCrImage(static_cast<ImageD3D9*>(this))
  , mHasData(PR_FALSE)
{
}

void
PlanarYCbCrImageD3D9::SetData(const PlanarYCbCrImage::Data &aData)
{
  // XXX - For D3D9Ex we really should just copy to systemmem surfaces here.
  // For now, we copy the data
  int width_shift = 0;
  int height_shift = 0;
  if (aData.mYSize.width == aData.mCbCrSize.width &&
      aData.mYSize.height == aData.mCbCrSize.height) {
     // YV24 format
     width_shift = 0;
     height_shift = 0;
     mType = gfx::YV24;
  } else if (aData.mYSize.width / 2 == aData.mCbCrSize.width &&
             aData.mYSize.height == aData.mCbCrSize.height) {
    // YV16 format
    width_shift = 1;
    height_shift = 0;
    mType = gfx::YV16;
  } else if (aData.mYSize.width / 2 == aData.mCbCrSize.width &&
             aData.mYSize.height / 2 == aData.mCbCrSize.height ) {
      // YV12 format
    width_shift = 1;
    height_shift = 1;
    mType = gfx::YV12;
  } else {
    NS_ERROR("YCbCr format not supported");
  }

  mData = aData;
  mData.mCbCrStride = mData.mCbCrSize.width = aData.mPicSize.width >> width_shift;
  // Round up the values for width and height to make sure we sample enough data
  // for the last pixel - See bug 590735
  if (width_shift && (aData.mPicSize.width & 1)) {
    mData.mCbCrStride++;
    mData.mCbCrSize.width++;
  }
  mData.mCbCrSize.height = aData.mPicSize.height >> height_shift;
  if (height_shift && (aData.mPicSize.height & 1)) {
      mData.mCbCrSize.height++;
  }
  mData.mYSize = aData.mPicSize;
  mData.mYStride = mData.mYSize.width;

  mBuffer = new PRUint8[mData.mCbCrStride * mData.mCbCrSize.height * 2 +
                        mData.mYStride * mData.mYSize.height];
  mData.mYChannel = mBuffer;
  mData.mCbChannel = mData.mYChannel + mData.mYStride * mData.mYSize.height;
  mData.mCrChannel = mData.mCbChannel + mData.mCbCrStride * mData.mCbCrSize.height;

  int cbcr_x = aData.mPicX >> width_shift;
  int cbcr_y = aData.mPicY >> height_shift;

  for (int i = 0; i < mData.mYSize.height; i++) {
    memcpy(mData.mYChannel + i * mData.mYStride,
           aData.mYChannel + ((aData.mPicY + i) * aData.mYStride) + aData.mPicX,
           mData.mYStride);
  }
  for (int i = 0; i < mData.mCbCrSize.height; i++) {
    memcpy(mData.mCbChannel + i * mData.mCbCrStride,
           aData.mCbChannel + ((cbcr_y + i) * aData.mCbCrStride) + cbcr_x,
           mData.mCbCrStride);
  }
  for (int i = 0; i < mData.mCbCrSize.height; i++) {
    memcpy(mData.mCrChannel + i * mData.mCbCrStride,
           aData.mCrChannel + ((cbcr_y + i) * aData.mCbCrStride) + cbcr_x,
           mData.mCbCrStride);
  }

  // Fix picture rect to be correct
  mData.mPicX = mData.mPicY = 0;
  mSize = aData.mPicSize;

  mHasData = PR_TRUE;
}

void
PlanarYCbCrImageD3D9::AllocateTextures(IDirect3DDevice9 *aDevice)
{
  D3DLOCKED_RECT lockrectY;
  D3DLOCKED_RECT lockrectCb;
  D3DLOCKED_RECT lockrectCr;
  PRUint8* src;
  PRUint8* dest;

  nsRefPtr<IDirect3DSurface9> tmpSurfaceY;
  nsRefPtr<IDirect3DSurface9> tmpSurfaceCb;
  nsRefPtr<IDirect3DSurface9> tmpSurfaceCr;

  nsRefPtr<IDirect3DDevice9Ex> deviceEx;
  aDevice->QueryInterface(IID_IDirect3DDevice9Ex,
                          getter_AddRefs(deviceEx));

  bool isD3D9Ex = deviceEx;

  if (isD3D9Ex) {
    nsRefPtr<IDirect3DTexture9> tmpYTexture;
    nsRefPtr<IDirect3DTexture9> tmpCbTexture;
    nsRefPtr<IDirect3DTexture9> tmpCrTexture;
    // D3D9Ex does not support the managed pool, could use dynamic textures
    // here. But since an Image is immutable static textures are probably a
    // better idea.
    aDevice->CreateTexture(mData.mYSize.width, mData.mYSize.height,
                            1, 0, D3DFMT_L8, D3DPOOL_DEFAULT,
                            getter_AddRefs(mYTexture), NULL);
    aDevice->CreateTexture(mData.mCbCrSize.width, mData.mCbCrSize.height,
                            1, 0, D3DFMT_L8, D3DPOOL_DEFAULT,
                            getter_AddRefs(mCbTexture), NULL);
    aDevice->CreateTexture(mData.mCbCrSize.width, mData.mCbCrSize.height,
                            1, 0, D3DFMT_L8, D3DPOOL_DEFAULT,
                            getter_AddRefs(mCrTexture), NULL);
    aDevice->CreateTexture(mData.mYSize.width, mData.mYSize.height,
                            1, 0, D3DFMT_L8, D3DPOOL_SYSTEMMEM,
                            getter_AddRefs(tmpYTexture), NULL);
    aDevice->CreateTexture(mData.mCbCrSize.width, mData.mCbCrSize.height,
                            1, 0, D3DFMT_L8, D3DPOOL_SYSTEMMEM,
                            getter_AddRefs(tmpCbTexture), NULL);
    aDevice->CreateTexture(mData.mCbCrSize.width, mData.mCbCrSize.height,
                            1, 0, D3DFMT_L8, D3DPOOL_SYSTEMMEM,
                            getter_AddRefs(tmpCrTexture), NULL);
    tmpYTexture->GetSurfaceLevel(0, getter_AddRefs(tmpSurfaceY));
    tmpCbTexture->GetSurfaceLevel(0, getter_AddRefs(tmpSurfaceCb));
    tmpCrTexture->GetSurfaceLevel(0, getter_AddRefs(tmpSurfaceCr));
    tmpSurfaceY->LockRect(&lockrectY, NULL, 0);
    tmpSurfaceCb->LockRect(&lockrectCb, NULL, 0);
    tmpSurfaceCr->LockRect(&lockrectCr, NULL, 0);
  } else {
    aDevice->CreateTexture(mData.mYSize.width, mData.mYSize.height,
                            1, 0, D3DFMT_L8, D3DPOOL_MANAGED,
                            getter_AddRefs(mYTexture), NULL);
    aDevice->CreateTexture(mData.mCbCrSize.width, mData.mCbCrSize.height,
                            1, 0, D3DFMT_L8, D3DPOOL_MANAGED,
                            getter_AddRefs(mCbTexture), NULL);
    aDevice->CreateTexture(mData.mCbCrSize.width, mData.mCbCrSize.height,
                            1, 0, D3DFMT_L8, D3DPOOL_MANAGED,
                            getter_AddRefs(mCrTexture), NULL);

    /* lock the entire texture */
    mYTexture->LockRect(0, &lockrectY, NULL, 0);
    mCbTexture->LockRect(0, &lockrectCb, NULL, 0);
    mCrTexture->LockRect(0, &lockrectCr, NULL, 0);
  }

  src  = mData.mYChannel;
  //FIX cast
  dest = (PRUint8*)lockrectY.pBits;

  // copy over data
  for (int h=0; h<mData.mYSize.height; h++) {
    memcpy(dest, src, mData.mYSize.width);
    dest += lockrectY.Pitch;
    src += mData.mYStride;
  }

  src  = mData.mCbChannel;
  //FIX cast
  dest = (PRUint8*)lockrectCb.pBits;

  // copy over data
  for (int h=0; h<mData.mCbCrSize.height; h++) {
    memcpy(dest, src, mData.mCbCrSize.width);
    dest += lockrectCb.Pitch;
    src += mData.mCbCrStride;
  }

  src  = mData.mCrChannel;
  //FIX cast
  dest = (PRUint8*)lockrectCr.pBits;

  // copy over data
  for (int h=0; h<mData.mCbCrSize.height; h++) {
    memcpy(dest, src, mData.mCbCrSize.width);
    dest += lockrectCr.Pitch;
    src += mData.mCbCrStride;
  }

  if (isD3D9Ex) {
    tmpSurfaceY->UnlockRect();
    tmpSurfaceCb->UnlockRect();
    tmpSurfaceCr->UnlockRect();
    nsRefPtr<IDirect3DSurface9> dstSurface;
    mYTexture->GetSurfaceLevel(0, getter_AddRefs(dstSurface));
    aDevice->UpdateSurface(tmpSurfaceY, NULL, dstSurface, NULL);
    mCbTexture->GetSurfaceLevel(0, getter_AddRefs(dstSurface));
    aDevice->UpdateSurface(tmpSurfaceCb, NULL, dstSurface, NULL);
    mCrTexture->GetSurfaceLevel(0, getter_AddRefs(dstSurface));
    aDevice->UpdateSurface(tmpSurfaceCr, NULL, dstSurface, NULL);
  } else {
    mYTexture->UnlockRect(0);
    mCbTexture->UnlockRect(0);
    mCrTexture->UnlockRect(0);
  }
}

void
PlanarYCbCrImageD3D9::FreeTextures()
{
}

already_AddRefed<gfxASurface>
PlanarYCbCrImageD3D9::GetAsSurface()
{
  nsRefPtr<gfxImageSurface> imageSurface =
    new gfxImageSurface(mSize, gfxASurface::ImageFormatRGB24);

  // Convert from YCbCr to RGB now
  gfx::ConvertYCbCrToRGB32(mData.mYChannel,
                           mData.mCbChannel,
                           mData.mCrChannel,
                           imageSurface->Data(),
                           0,
                           0,
                           mSize.width,
                           mSize.height,
                           mData.mYStride,
                           mData.mCbCrStride,
                           imageSurface->Stride(),
                           mType);

  return imageSurface.forget().get();
}

CairoImageD3D9::~CairoImageD3D9()
{
}

void
CairoImageD3D9::SetDevice(IDirect3DDevice9 *aDevice)
{
  mTexture = NULL;
  mDevice = aDevice;
}

void
CairoImageD3D9::SetData(const CairoImage::Data &aData)
{
  mSize = aData.mSize;
  mCachedSurface = aData.mSurface;
  mTexture = NULL;
}

IDirect3DTexture9*
CairoImageD3D9::GetOrCreateTexture()
{
  if (mTexture)
    return mTexture;

  mTexture = SurfaceToTexture(mDevice, mCachedSurface, mSize);

  // We need to keep our cached surface around in case the device changes.
  return mTexture;
}

already_AddRefed<gfxASurface>
CairoImageD3D9::GetAsSurface()
{
  nsRefPtr<gfxASurface> surface = mCachedSurface;
  return surface.forget();
}

} /* layers */
} /* mozilla */
