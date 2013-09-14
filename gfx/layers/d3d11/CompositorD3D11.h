/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_COMPOSITORD3D11_H
#define MOZILLA_GFX_COMPOSITORD3D11_H

#include "mozilla/layers/Compositor.h"
#include "TextureD3D11.h"
#include <d3d11.h>

class nsWidget;

namespace mozilla {
namespace layers {

#define LOGD3D11(param)

struct VertexShaderConstants
{
  float layerTransform[4][4];
  float projection[4][4];
  float renderTargetOffset[4];
  gfx::Rect textureCoords;
  gfx::Rect layerQuad;
  gfx::Rect maskQuad;
};

struct PixelShaderConstants
{
  float layerColor[4];
  float layerOpacity[4];
};

struct DeviceAttachmentsD3D11;

class CompositorD3D11 : public Compositor
{
public:
  CompositorD3D11(nsIWidget *aWidget);
  ~CompositorD3D11();

  virtual bool Initialize();
  virtual void Destroy() { }

  virtual TextureFactoryIdentifier
    GetTextureFactoryIdentifier() MOZ_OVERRIDE;

  virtual bool CanUseCanvasLayerForSize(const gfxIntSize &aSize) MOZ_OVERRIDE;
  virtual int32_t GetMaxTextureSize() const MOZ_FINAL;

  virtual void SetTargetContext(gfxContext *aTarget) { mTarget = aTarget; }

  virtual void MakeCurrent(MakeCurrentFlags aFlags = 0) { }

  virtual TemporaryRef<CompositingRenderTarget>
    CreateRenderTarget(const gfx::IntRect &aRect,
                       SurfaceInitMode aInit) MOZ_OVERRIDE;

  virtual TemporaryRef<CompositingRenderTarget>
    CreateRenderTargetFromSource(const gfx::IntRect &aRect,
                                 const CompositingRenderTarget *aSource) MOZ_OVERRIDE;

  virtual void SetRenderTarget(CompositingRenderTarget *aSurface) MOZ_OVERRIDE;
  virtual CompositingRenderTarget* GetCurrentRenderTarget() MOZ_OVERRIDE
  {
    return mCurrentRT;
  }

  virtual void SetDestinationSurfaceSize(const gfx::IntSize& aSize) MOZ_OVERRIDE { }

  /**
   * Declare an offset to use when rendering layers. This will be ignored when
   * rendering to a target instead of the screen.
   */
  virtual void SetScreenRenderOffset(const ScreenPoint& aOffset) MOZ_OVERRIDE {
    if (aOffset.x || aOffset.y) {
      NS_RUNTIMEABORT("SetScreenRenderOffset not supported by CompositorD3D11.");
    }
    // If the offset is 0, 0 that's okay.
  }

  virtual void DrawQuad(const gfx::Rect &aRect, const gfx::Rect &aClipRect,
                        const EffectChain &aEffectChain,
                        gfx::Float aOpacity, const gfx::Matrix4x4 &aTransform,
                        const gfx::Point &aOffset) MOZ_OVERRIDE;

  /**
   * Start a new frame. If aClipRectIn is null, sets *aClipRectOut to the screen dimensions. 
   */
  virtual void BeginFrame(const gfx::Rect *aClipRectIn,
                          const gfxMatrix& aTransform,
                          const gfx::Rect& aRenderBounds,
                          gfx::Rect *aClipRectOut = nullptr,
                          gfx::Rect *aRenderBoundsOut = nullptr) MOZ_OVERRIDE;

  /**
   * Flush the current frame to the screen.
   */
  virtual void EndFrame() MOZ_OVERRIDE;

  /**
   * Post rendering stuff if the rendering is outside of this Compositor
   * e.g., by Composer2D
   */
  virtual void EndFrameForExternalComposition(const gfxMatrix& aTransform) MOZ_OVERRIDE { }

  /**
   * Tidy up if BeginFrame has been called, but EndFrame won't be
   */
  virtual void AbortFrame() MOZ_OVERRIDE { }

  /**
   * Setup the viewport and projection matrix for rendering
   * to a window of the given dimensions.
   */
  virtual void PrepareViewport(const gfx::IntSize& aSize,
                               const gfxMatrix& aWorldTransform) MOZ_OVERRIDE;

  virtual bool SupportsPartialTextureUpdate() MOZ_OVERRIDE { return true; }

#ifdef MOZ_DUMP_PAINTING
  virtual const char* Name() const MOZ_OVERRIDE { return "Direct3D 11"; }
#endif

  virtual void NotifyLayersTransaction() MOZ_OVERRIDE { }

  virtual nsIWidget* GetWidget() const MOZ_OVERRIDE { return mWidget; }
  virtual const nsIntSize& GetWidgetSize() MOZ_OVERRIDE;

  ID3D11Device *GetDevice() { return mDevice; }

private:
  enum MaskMode {
    UNMASKED = 0,
    MASKED = 1,
    MASKED3D
  };

  void VerifyBufferSize();
  void UpdateRenderTarget();
  bool CreateShaders();
  void UpdateConstantBuffers();
  void SetSamplerForFilter(gfx::Filter aFilter);
  void SetPSForEffect(Effect *aEffect, MaskMode aMaskMode);
  void PaintToTarget();

  RefPtr<ID3D11DeviceContext> mContext;
  RefPtr<ID3D11Device> mDevice;
  RefPtr<IDXGISwapChain> mSwapChain;
  RefPtr<CompositingRenderTargetD3D11> mDefaultRT;
  RefPtr<CompositingRenderTargetD3D11> mCurrentRT;

  DeviceAttachmentsD3D11 *mAttachments;

  nsRefPtr<gfxContext> mTarget;

  nsIWidget *mWidget;
  // XXX - Bas - wth?
  nsIntSize mSize;

  HWND mHwnd;

  D3D_FEATURE_LEVEL mFeatureLevel;

  VertexShaderConstants mVSConstants;
  PixelShaderConstants mPSConstants;
};

}
}

#endif
