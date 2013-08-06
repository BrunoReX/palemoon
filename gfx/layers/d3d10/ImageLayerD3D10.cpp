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
 *   Bas Schouten <bschouten@mozilla.com>
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

#include "ImageLayerD3D10.h"
#include "gfxImageSurface.h"
#include "gfxD2DSurface.h"
#include "gfxWindowsSurface.h"
#include "yuv_convert.h"
#include "../d3d9/Nv3DVUtils.h"

namespace mozilla {
namespace layers {

static already_AddRefed<ID3D10Texture2D>
SurfaceToTexture(ID3D10Device *aDevice,
                 gfxASurface *aSurface,
                 const gfxIntSize &aSize)
{
  if (aSurface && aSurface->GetType() == gfxASurface::SurfaceTypeD2D) {
    void *data = aSurface->GetData(&gKeyD3D10Texture);
    if (data) {
      nsRefPtr<ID3D10Texture2D> texture = static_cast<ID3D10Texture2D*>(data);
      ID3D10Device *dev;
      texture->GetDevice(&dev);
      if (dev == aDevice) {
        return texture.forget();
      }
    }
  }

  nsRefPtr<gfxImageSurface> imageSurface = aSurface->GetAsImageSurface();

  if (!imageSurface) {
    imageSurface = new gfxImageSurface(aSize,
                                       gfxASurface::ImageFormatARGB32);
    
    nsRefPtr<gfxContext> context = new gfxContext(imageSurface);
    context->SetSource(aSurface);
    context->SetOperator(gfxContext::OPERATOR_SOURCE);
    context->Paint();
  }

  D3D10_SUBRESOURCE_DATA data;
  
  CD3D10_TEXTURE2D_DESC desc(DXGI_FORMAT_B8G8R8A8_UNORM,
                             imageSurface->GetSize().width,
                             imageSurface->GetSize().height,
                             1, 1);
  desc.Usage = D3D10_USAGE_IMMUTABLE;
  
  data.pSysMem = imageSurface->Data();
  data.SysMemPitch = imageSurface->Stride();

  nsRefPtr<ID3D10Texture2D> texture;
  HRESULT hr = aDevice->CreateTexture2D(&desc, &data, getter_AddRefs(texture));

  if (FAILED(hr)) {
    LayerManagerD3D10::ReportFailure(NS_LITERAL_CSTRING("Failed to create texture for image surface"),
                                     hr);
  }

  return texture.forget();
}

ImageContainerD3D10::ImageContainerD3D10(ID3D10Device1 *aDevice)
  : ImageContainer(nsnull)
  , mDevice(aDevice)
{
}

already_AddRefed<Image>
ImageContainerD3D10::CreateImage(const Image::Format *aFormats,
                                 PRUint32 aNumFormats)
{
  if (!aNumFormats) {
    return nsnull;
  }
  nsRefPtr<Image> img;
  if (aFormats[0] == Image::PLANAR_YCBCR) {
    img = new PlanarYCbCrImageD3D10(mDevice);
  } else if (aFormats[0] == Image::CAIRO_SURFACE) {
    img = new CairoImageD3D10(mDevice);
  }
  return img.forget();
}

void
ImageContainerD3D10::SetCurrentImage(Image *aImage)
{
  MonitorAutoEnter mon(mMonitor);

  mActiveImage = aImage;
  CurrentImageChanged();
}

already_AddRefed<Image>
ImageContainerD3D10::GetCurrentImage()
{
  MonitorAutoEnter mon(mMonitor);

  nsRefPtr<Image> retval = mActiveImage;
  return retval.forget();
}

already_AddRefed<gfxASurface>
ImageContainerD3D10::GetCurrentAsSurface(gfxIntSize *aSize)
{
  MonitorAutoEnter mon(mMonitor);
  if (!mActiveImage) {
    return nsnull;
  }

  if (mActiveImage->GetFormat() == Image::PLANAR_YCBCR) {
    PlanarYCbCrImageD3D10 *yuvImage =
      static_cast<PlanarYCbCrImageD3D10*>(mActiveImage.get());
    if (yuvImage->HasData()) {
      *aSize = yuvImage->mSize;
    }
  } else if (mActiveImage->GetFormat() == Image::CAIRO_SURFACE) {
    CairoImageD3D10 *cairoImage =
      static_cast<CairoImageD3D10*>(mActiveImage.get());
    *aSize = cairoImage->mSize;
  }

  return static_cast<ImageD3D10*>(mActiveImage->GetImplData())->GetAsSurface();
}

gfxIntSize
ImageContainerD3D10::GetCurrentSize()
{
  MonitorAutoEnter mon(mMonitor);
  if (!mActiveImage) {
    return gfxIntSize(0,0);
  }
  if (mActiveImage->GetFormat() == Image::PLANAR_YCBCR) {
    PlanarYCbCrImageD3D10 *yuvImage =
      static_cast<PlanarYCbCrImageD3D10*>(mActiveImage.get());
    if (!yuvImage->HasData()) {
      return gfxIntSize(0,0);
    }
    return yuvImage->mSize;

  } else if (mActiveImage->GetFormat() == Image::CAIRO_SURFACE) {
    CairoImageD3D10 *cairoImage =
      static_cast<CairoImageD3D10*>(mActiveImage.get());
    return cairoImage->mSize;
  }

  return gfxIntSize(0,0);
}

PRBool
ImageContainerD3D10::SetLayerManager(LayerManager *aManager)
{
  if (aManager->GetBackendType() == LayerManager::LAYERS_D3D10) {
    return PR_TRUE;
  }
  return PR_FALSE;
}

Layer*
ImageLayerD3D10::GetLayer()
{
  return this;
}

void
ImageLayerD3D10::RenderLayer()
{
  if (!GetContainer()) {
    return;
  }

  nsRefPtr<Image> image = GetContainer()->GetCurrentImage();
  if (!image) {
    return;
  }

  SetEffectTransformAndOpacity();

  ID3D10EffectTechnique *technique;

  if (GetContainer()->GetBackendType() != LayerManager::LAYERS_D3D10 ||
      image->GetFormat() == Image::CAIRO_SURFACE)
  {
    gfxIntSize size;
    bool hasAlpha;
    nsRefPtr<ID3D10ShaderResourceView> srView;

    if (GetContainer()->GetBackendType() != LayerManager::LAYERS_D3D10)
    {
      nsRefPtr<gfxASurface> surf = GetContainer()->GetCurrentAsSurface(&size);
      
      nsRefPtr<ID3D10Texture2D> texture = SurfaceToTexture(device(), surf, size);

      if (!texture) {
        NS_WARNING("Failed to create texture for surface.");
        return;
      }
      
      hasAlpha = surf->GetContentType() == gfxASurface::CONTENT_COLOR_ALPHA;
      
      device()->CreateShaderResourceView(texture, NULL, getter_AddRefs(srView));
    } else {
      ImageContainerD3D10 *container =
        static_cast<ImageContainerD3D10*>(GetContainer());

      if (container->device() != device()) {
        container->SetDevice(device());
      }

      // image->GetFormat() == Image::CAIRO_SURFACE
      CairoImageD3D10 *cairoImage =
        static_cast<CairoImageD3D10*>(image.get());
      
      if (cairoImage->mDevice != device()) {
        // This shader resource view was for an old device! Can't draw that
        // now.
        return;
      }

      srView = cairoImage->mSRView;
      hasAlpha = cairoImage->mHasAlpha;
      size = cairoImage->mSize;
    }

    if (hasAlpha) {
      if (mFilter == gfxPattern::FILTER_NEAREST) {
        technique = effect()->GetTechniqueByName("RenderRGBALayerPremulPoint");
      } else {
        technique = effect()->GetTechniqueByName("RenderRGBALayerPremul");
      }
    } else {
      if (mFilter == gfxPattern::FILTER_NEAREST) {
        technique = effect()->GetTechniqueByName("RenderRGBLayerPremulPoint");
      } else {
        technique = effect()->GetTechniqueByName("RenderRGBLayerPremul");
      }
    }

    if (srView) {
      effect()->GetVariableByName("tRGB")->AsShaderResource()->SetResource(srView);
    }

    effect()->GetVariableByName("vLayerQuad")->AsVector()->SetFloatVector(
      ShaderConstantRectD3D10(
        (float)0,
        (float)0,
        (float)size.width,
        (float)size.height)
      );
  } else if (image->GetFormat() == Image::PLANAR_YCBCR) {
    PlanarYCbCrImageD3D10 *yuvImage =
      static_cast<PlanarYCbCrImageD3D10*>(image.get());

    if (!yuvImage->HasData()) {
      return;
    }

    if (yuvImage->mDevice != device()) {
        // These shader resources were created for an old device! Can't draw
        // that here.
        return;
    }

    // TODO: At some point we should try to deal with mFilter here, you don't
    // really want to use point filtering in the case of NEAREST, since that
    // would also use point filtering for Chroma upsampling. Where most likely
    // the user would only want point filtering for final RGB image upsampling.

    technique = effect()->GetTechniqueByName("RenderYCbCrLayer");

    effect()->GetVariableByName("tY")->AsShaderResource()->SetResource(yuvImage->mYView);
    effect()->GetVariableByName("tCb")->AsShaderResource()->SetResource(yuvImage->mCbView);
    effect()->GetVariableByName("tCr")->AsShaderResource()->SetResource(yuvImage->mCrView);

    /*
     * Send 3d control data and metadata to NV3DVUtils
     */
    if (GetNv3DVUtils()) {
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
      GetNv3DVUtils()->SendNv3DVControl(mode, true, FIREFOX_3DV_APP_HANDLE);

      if (yuvImage->mData.mStereoMode != STEREO_MODE_MONO) {
        // Dst resource is optional
        GetNv3DVUtils()->SendNv3DVMetaData((unsigned int)yuvImage->mSize.width, 
                                           (unsigned int)yuvImage->mSize.height, (HANDLE)(yuvImage->mYTexture), (HANDLE)(NULL));
      }
    }

    effect()->GetVariableByName("vLayerQuad")->AsVector()->SetFloatVector(
      ShaderConstantRectD3D10(
        (float)0,
        (float)0,
        (float)yuvImage->mSize.width,
        (float)yuvImage->mSize.height)
      );
  }

  technique->GetPassByIndex(0)->Apply(0);
  device()->Draw(4, 0);

  GetContainer()->NotifyPaintedImage(image);
}

PlanarYCbCrImageD3D10::PlanarYCbCrImageD3D10(ID3D10Device1 *aDevice)
  : PlanarYCbCrImage(static_cast<ImageD3D10*>(this))
  , mDevice(aDevice)
  , mHasData(PR_FALSE)
{
}

void
PlanarYCbCrImageD3D10::SetData(const PlanarYCbCrImage::Data &aData)
{
  // XXX - For D3D10Ex we really should just copy to systemmem surfaces here.
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

  AllocateTextures();

  mHasData = PR_TRUE;
}

void
PlanarYCbCrImageD3D10::AllocateTextures()
{
  D3D10_SUBRESOURCE_DATA dataY;
  D3D10_SUBRESOURCE_DATA dataCb;
  D3D10_SUBRESOURCE_DATA dataCr;
  CD3D10_TEXTURE2D_DESC descY(DXGI_FORMAT_R8_UNORM,
                              mData.mYSize.width,
                              mData.mYSize.height, 1, 1);
  CD3D10_TEXTURE2D_DESC descCbCr(DXGI_FORMAT_R8_UNORM,
                                 mData.mCbCrSize.width,
                                 mData.mCbCrSize.height, 1, 1);

  descY.Usage = descCbCr.Usage = D3D10_USAGE_IMMUTABLE;

  dataY.pSysMem = mData.mYChannel;
  dataY.SysMemPitch = mData.mYStride;
  dataCb.pSysMem = mData.mCbChannel;
  dataCb.SysMemPitch = mData.mCbCrStride;
  dataCr.pSysMem = mData.mCrChannel;
  dataCr.SysMemPitch = mData.mCbCrStride;

  mDevice->CreateTexture2D(&descY, &dataY, getter_AddRefs(mYTexture));
  mDevice->CreateTexture2D(&descCbCr, &dataCb, getter_AddRefs(mCbTexture));
  mDevice->CreateTexture2D(&descCbCr, &dataCr, getter_AddRefs(mCrTexture));
  mDevice->CreateShaderResourceView(mYTexture, NULL, getter_AddRefs(mYView));
  mDevice->CreateShaderResourceView(mCbTexture, NULL, getter_AddRefs(mCbView));
  mDevice->CreateShaderResourceView(mCrTexture, NULL, getter_AddRefs(mCrView));
}

already_AddRefed<gfxASurface>
PlanarYCbCrImageD3D10::GetAsSurface()
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

CairoImageD3D10::~CairoImageD3D10()
{
}

void
CairoImageD3D10::SetData(const CairoImage::Data &aData)
{
  mSize = aData.mSize;
  NS_ASSERTION(aData.mSurface->GetContentType() != gfxASurface::CONTENT_ALPHA,
               "Invalid content type passed to CairoImageD3D10.");

  mTexture = SurfaceToTexture(mDevice, aData.mSurface, mSize);

  if (!mTexture) {
    NS_WARNING("Failed to create texture for CairoImage.");
    return;
  }

  if (aData.mSurface->GetContentType() == gfxASurface::CONTENT_COLOR) {
    mHasAlpha = false;
  } else {
    mHasAlpha = true;
  }

  mDevice->CreateShaderResourceView(mTexture, NULL, getter_AddRefs(mSRView));
}

already_AddRefed<gfxASurface>
CairoImageD3D10::GetAsSurface()
{
  nsRefPtr<ID3D10Texture2D> surfTexture;

  // Make a copy of the texture since our current texture is not suitable for
  // drawing with Direct2D because it is immutable and cannot be bound as a
  // render target.
  D3D10_TEXTURE2D_DESC texDesc;
  mTexture->GetDesc(&texDesc);
  texDesc.Usage = D3D10_USAGE_DEFAULT;
  texDesc.BindFlags = D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE;
  texDesc.MiscFlags = D3D10_RESOURCE_MISC_GDI_COMPATIBLE;

  mDevice->CreateTexture2D(&texDesc, NULL, getter_AddRefs(surfTexture));

  mDevice->CopyResource(surfTexture, mTexture);

  nsRefPtr<gfxASurface> surf =
    new gfxD2DSurface(surfTexture, mHasAlpha ? gfxASurface::CONTENT_COLOR_ALPHA :
                                               gfxASurface::CONTENT_COLOR);
  return surf.forget();
}

} /* layers */
} /* mozilla */
