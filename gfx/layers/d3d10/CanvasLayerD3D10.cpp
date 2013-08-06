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
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Vladimir Vukicevic <vladimir@pobox.com>
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

#include "CanvasLayerD3D10.h"

#include "../d3d9/Nv3DVUtils.h"
#include "gfxImageSurface.h"
#include "gfxWindowsSurface.h"
#include "gfxWindowsPlatform.h"

using namespace mozilla::gfx;

namespace mozilla {
namespace layers {

CanvasLayerD3D10::~CanvasLayerD3D10()
{
}

void
CanvasLayerD3D10::Initialize(const Data& aData)
{
  NS_ASSERTION(mSurface == nsnull, "BasicCanvasLayer::Initialize called twice!");

  if (aData.mSurface) {
    mSurface = aData.mSurface;
    NS_ASSERTION(aData.mGLContext == nsnull && !aData.mDrawTarget,
                 "CanvasLayer can't have both surface and GLContext/DrawTarget");
    mNeedsYFlip = PR_FALSE;
    mDataIsPremultiplied = PR_TRUE;
  } else if (aData.mGLContext) {
    NS_ASSERTION(aData.mGLContext->IsOffscreen(), "canvas gl context isn't offscreen");
    mGLContext = aData.mGLContext;
    mCanvasFramebuffer = mGLContext->GetOffscreenFBO();
    mDataIsPremultiplied = aData.mGLBufferIsPremultiplied;
    mNeedsYFlip = PR_TRUE;
  } else if (aData.mDrawTarget) {
    mDrawTarget = aData.mDrawTarget;
    void *texture = mDrawTarget->GetNativeSurface(NATIVE_SURFACE_D3D10_TEXTURE);

    if (!texture) {
      // XXX - Once we have non-D2D drawtargets we should do something more sensible here.
      NS_WARNING("Failed to get D3D10 texture from DrawTarget.");
      return;
    }

    mTexture = static_cast<ID3D10Texture2D*>(texture);

    NS_ASSERTION(aData.mGLContext == nsnull && aData.mSurface == nsnull,
                 "CanvasLayer can't have both surface and GLContext/Surface");

    mNeedsYFlip = PR_FALSE;
    mDataIsPremultiplied = PR_TRUE;

    mBounds.SetRect(0, 0, aData.mSize.width, aData.mSize.height);
    device()->CreateShaderResourceView(mTexture, NULL, getter_AddRefs(mSRView));
    return;
  } else {
    NS_ERROR("CanvasLayer created without mSurface, mDrawTarget or mGLContext?");
  }

  mBounds.SetRect(0, 0, aData.mSize.width, aData.mSize.height);

  if (mSurface && mSurface->GetType() == gfxASurface::SurfaceTypeD2D) {
    void *data = mSurface->GetData(&gKeyD3D10Texture);
    if (data) {
      mTexture = static_cast<ID3D10Texture2D*>(data);
      mIsD2DTexture = PR_TRUE;
      device()->CreateShaderResourceView(mTexture, NULL, getter_AddRefs(mSRView));
      mHasAlpha =
        mSurface->GetContentType() == gfxASurface::CONTENT_COLOR_ALPHA;
      return;
    }
  }

  mIsD2DTexture = PR_FALSE;
  mUsingSharedTexture = PR_FALSE;

  HANDLE shareHandle = mGLContext ? mGLContext->GetD3DShareHandle() : nsnull;
  if (shareHandle) {
    HRESULT hr = device()->OpenSharedResource(shareHandle, __uuidof(ID3D10Texture2D), getter_AddRefs(mTexture));
    if (SUCCEEDED(hr))
      mUsingSharedTexture = PR_TRUE;
  }

  if (mUsingSharedTexture) {
    mNeedsYFlip = PR_FALSE;
  } else {
    CD3D10_TEXTURE2D_DESC desc(DXGI_FORMAT_B8G8R8A8_UNORM, mBounds.width, mBounds.height, 1, 1);
    desc.Usage = D3D10_USAGE_DYNAMIC;
    desc.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;

    HRESULT hr = device()->CreateTexture2D(&desc, NULL, getter_AddRefs(mTexture));
    if (FAILED(hr)) {
      NS_WARNING("Failed to create texture for CanvasLayer!");
      return;
    }
  }

  device()->CreateShaderResourceView(mTexture, NULL, getter_AddRefs(mSRView));
}

void
CanvasLayerD3D10::UpdateSurface()
{
  if (!mDirty)
    return;
  mDirty = PR_FALSE;

  if (mDrawTarget) {
    mDrawTarget->Flush();
    return;
  }

  if (mIsD2DTexture) {
    mSurface->Flush();
    return;
  }

  if (mUsingSharedTexture) {
    // need to sync on the d3d9 device
    if (mGLContext) {
      mGLContext->MakeCurrent();
      mGLContext->fFinish();
    }
    return;
  }

  if (mGLContext) {
    // WebGL reads entire surface.
    D3D10_MAPPED_TEXTURE2D map;
    
    HRESULT hr = mTexture->Map(0, D3D10_MAP_WRITE_DISCARD, 0, &map);

    if (FAILED(hr)) {
      NS_WARNING("Failed to map CanvasLayer texture.");
      return;
    }

    PRUint8 *destination;
    if (map.RowPitch != mBounds.width * 4) {
      destination = new PRUint8[mBounds.width * mBounds.height * 4];
    } else {
      destination = (PRUint8*)map.pData;
    }

    // We have to flush to ensure that any buffered GL operations are
    // in the framebuffer before we read.
    mGLContext->fFlush();

    PRUint32 currentFramebuffer = 0;

    mGLContext->fGetIntegerv(LOCAL_GL_FRAMEBUFFER_BINDING, (GLint*)&currentFramebuffer);

    // Make sure that we read pixels from the correct framebuffer, regardless
    // of what's currently bound.
    if (currentFramebuffer != mCanvasFramebuffer)
      mGLContext->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, mCanvasFramebuffer);

    nsRefPtr<gfxImageSurface> tmpSurface =
      new gfxImageSurface(destination,
                          gfxIntSize(mBounds.width, mBounds.height),
                          mBounds.width * 4,
                          gfxASurface::ImageFormatARGB32);
    mGLContext->ReadPixelsIntoImageSurface(0, 0,
                                           mBounds.width, mBounds.height,
                                           tmpSurface);
    tmpSurface = nsnull;

    // Put back the previous framebuffer binding.
    if (currentFramebuffer != mCanvasFramebuffer)
      mGLContext->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, currentFramebuffer);

    if (map.RowPitch != mBounds.width * 4) {
      for (int y = 0; y < mBounds.height; y++) {
        memcpy((PRUint8*)map.pData + map.RowPitch * y,
               destination + mBounds.width * 4 * y,
               mBounds.width * 4);
      }
      delete [] destination;
    }
    mTexture->Unmap(0);
  } else if (mSurface) {
    RECT r;
    r.left = 0;
    r.top = 0;
    r.right = mBounds.width;
    r.bottom = mBounds.height;

    D3D10_MAPPED_TEXTURE2D map;
    HRESULT hr = mTexture->Map(0, D3D10_MAP_WRITE_DISCARD, 0, &map);

    if (FAILED(hr)) {
      NS_WARNING("Failed to lock CanvasLayer texture.");
      return;
    }

    nsRefPtr<gfxImageSurface> dstSurface;

    dstSurface = new gfxImageSurface((unsigned char*)map.pData,
                                     gfxIntSize(mBounds.width, mBounds.height),
                                     map.RowPitch,
                                     gfxASurface::ImageFormatARGB32);
    nsRefPtr<gfxContext> ctx = new gfxContext(dstSurface);
    ctx->SetOperator(gfxContext::OPERATOR_SOURCE);
    ctx->SetSource(mSurface);
    ctx->Paint();
    
    mTexture->Unmap(0);
  }
}

Layer*
CanvasLayerD3D10::GetLayer()
{
  return this;
}

void
CanvasLayerD3D10::RenderLayer()
{
  UpdateSurface();
  FireDidTransactionCallback();

  if (!mTexture)
    return;

  nsIntRect visibleRect = mVisibleRegion.GetBounds();

  SetEffectTransformAndOpacity();

  ID3D10EffectTechnique *technique;

  if (mDataIsPremultiplied) {
    if (!mHasAlpha) {
      if (mFilter == gfxPattern::FILTER_NEAREST) {
        technique = effect()->GetTechniqueByName("RenderRGBLayerPremulPoint");
      } else {
        technique = effect()->GetTechniqueByName("RenderRGBLayerPremul");
      }
    } else {
      if (mFilter == gfxPattern::FILTER_NEAREST) {
        technique = effect()->GetTechniqueByName("RenderRGBALayerPremulPoint");
      } else {
        technique = effect()->GetTechniqueByName("RenderRGBALayerPremul");
      }
    }
  } else {
    if (mFilter == gfxPattern::FILTER_NEAREST) {
      technique = effect()->GetTechniqueByName("RenderRGBALayerNonPremulPoint");
    } else {
      technique = effect()->GetTechniqueByName("RenderRGBALayerNonPremul");
    }
  }

  if (mSRView) {
    effect()->GetVariableByName("tRGB")->AsShaderResource()->SetResource(mSRView);
  }

  effect()->GetVariableByName("vLayerQuad")->AsVector()->SetFloatVector(
    ShaderConstantRectD3D10(
      (float)mBounds.x,
      (float)mBounds.y,
      (float)mBounds.width,
      (float)mBounds.height)
    );

  if (mNeedsYFlip) {
    effect()->GetVariableByName("vTextureCoords")->AsVector()->SetFloatVector(
      ShaderConstantRectD3D10(
        0,
        1.0f,
        1.0f,
        -1.0f)
      );
  }

  technique->GetPassByIndex(0)->Apply(0);
  device()->Draw(4, 0);

  if (mNeedsYFlip) {
    effect()->GetVariableByName("vTextureCoords")->AsVector()->
      SetFloatVector(ShaderConstantRectD3D10(0, 0, 1.0f, 1.0f));
  }
}

} /* namespace layers */
} /* namespace mozilla */
