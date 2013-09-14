/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BasicCompositor.h"
#include "ipc/AutoOpenSurface.h"
#include "mozilla/layers/Effects.h"
#include "nsIWidget.h"
#include "gfx2DGlue.h"
#include "gfxUtils.h"
#include <algorithm>

namespace mozilla {
using namespace mozilla::gfx;

namespace layers {

class TextureSourceBasic : public TextureHost
{
public:
  virtual IntSize GetSize() const MOZ_OVERRIDE { return mSize; }

  virtual TextureSourceBasic* AsSourceBasic() MOZ_OVERRIDE { return this; }

  SourceSurface *GetSurface() { return mSurface; }

  virtual void SetCompositor(Compositor* aCompositor)
  {
    mCompositor = static_cast<BasicCompositor*>(aCompositor);
  }

  virtual const char *Name() { return "TextureSourceBasic"; }

protected:
  virtual void UpdateImpl(const SurfaceDescriptor& aImage,
                          nsIntRegion *aRegion,
                          nsIntPoint*) MOZ_OVERRIDE
  {
    AutoOpenSurface surf(OPEN_READ_ONLY, aImage);
    mFormat =
      (surf.ContentType() == gfxASurface::CONTENT_COLOR_ALPHA) ? FORMAT_B8G8R8A8 :
                                                                 FORMAT_B8G8R8X8;
    mThebesSurface = ShadowLayerForwarder::OpenDescriptor(OPEN_READ_ONLY, aImage);
    mThebesImage = mThebesSurface->GetAsImageSurface();
    MOZ_ASSERT(mThebesImage);
    mSurface = nullptr;
    mSize = IntSize(mThebesImage->Width(), mThebesImage->Height());
  }

  virtual bool Lock() MOZ_OVERRIDE
  {
    if (!mSurface) {
      mSurface = mCompositor->GetDrawTarget()->CreateSourceSurfaceFromData(mThebesImage->Data(),
                                                                           mSize,
                                                                           mThebesImage->Stride(),
                                                                           mFormat);
    }
    return true;
  }

  virtual already_AddRefed<gfxImageSurface> GetAsSurface() MOZ_OVERRIDE {
    if (!mThebesImage) {
      mThebesImage = mThebesSurface->GetAsImageSurface();
    }
    nsRefPtr<gfxImageSurface> result = mThebesImage;
    return result.forget();
  }

  BasicCompositor *mCompositor;
  RefPtr<SourceSurface> mSurface;
  nsRefPtr<gfxImageSurface> mThebesImage;
  nsRefPtr<gfxASurface> mThebesSurface;
  IntSize mSize;
};

TemporaryRef<TextureHost>
CreateBasicTextureHost(SurfaceDescriptorType aDescriptorType,
                       uint32_t aTextureHostFlags,
                       uint32_t aTextureFlags)
{
  MOZ_ASSERT(aDescriptorType == SurfaceDescriptor::TShmem ||
             aDescriptorType == SurfaceDescriptor::TMemoryImage,
             "We can only support Shmem currently");
  return new TextureSourceBasic();
}

BasicCompositor::BasicCompositor(nsIWidget *aWidget)
  : mWidget(aWidget)
  , mWidgetSize(-1, -1)
{
  MOZ_COUNT_CTOR(BasicCompositor);
  sBackend = LAYERS_BASIC;
}

BasicCompositor::~BasicCompositor()
{
  Destroy();
  MOZ_COUNT_DTOR(BasicCompositor);
}

TemporaryRef<CompositingRenderTarget>
BasicCompositor::CreateRenderTarget(const IntRect& aRect, SurfaceInitMode aInit)
{
  MOZ_ASSERT(aInit != INIT_MODE_COPY);
  RefPtr<DrawTarget> target = mDrawTarget->CreateSimilarDrawTarget(aRect.Size(), FORMAT_B8G8R8A8);

  RefPtr<BasicCompositingRenderTarget> rt = new BasicCompositingRenderTarget(target, aRect.Size());

  return rt.forget();
}

TemporaryRef<CompositingRenderTarget>
BasicCompositor::CreateRenderTargetFromSource(const IntRect &aRect,
                                              const CompositingRenderTarget *aSource)
{
  RefPtr<DrawTarget> target = mDrawTarget->CreateSimilarDrawTarget(aRect.Size(), FORMAT_B8G8R8A8);
  RefPtr<BasicCompositingRenderTarget> rt = new BasicCompositingRenderTarget(target, aRect.Size());

  DrawTarget *source;
  if (aSource) {
    const BasicCompositingRenderTarget* sourceSurface =
      static_cast<const BasicCompositingRenderTarget*>(aSource);
    source = sourceSurface->mDrawTarget;
  } else {
    source = mDrawTarget;
  }

  RefPtr<SourceSurface> snapshot = source->Snapshot();

  rt->mDrawTarget->CopySurface(snapshot, aRect, IntPoint(0, 0));
  return rt.forget();
}

static void
DrawSurfaceWithTextureCoords(DrawTarget *aDest,
                             const gfx::Rect& aDestRect,
                             SourceSurface *aSource,
                             const gfx::Rect& aTextureCoords,
                             float aOpacity,
                             SourceSurface *aMask,
                             const Matrix& aMaskTransform)
{
  // Convert aTextureCoords into aSource's coordinate space
  gfxRect sourceRect(aTextureCoords.x * aSource->GetSize().width,
                     aTextureCoords.y * aSource->GetSize().height,
                     aTextureCoords.width * aSource->GetSize().width,
                     aTextureCoords.height * aSource->GetSize().height);
  // Compute a transform that maps sourceRect to aDestRect.
  gfxMatrix transform =
    gfxUtils::TransformRectToRect(sourceRect,
                                  gfxPoint(aDestRect.x, aDestRect.y),
                                  gfxPoint(aDestRect.XMost(), aDestRect.y),
                                  gfxPoint(aDestRect.XMost(), aDestRect.YMost()));
  Matrix matrix = ToMatrix(transform);
  if (aMask) {
    aDest->Mask(SurfacePattern(aSource, EXTEND_REPEAT, matrix),
                SurfacePattern(aMask, EXTEND_CLAMP, aMaskTransform),
                DrawOptions(aOpacity));
  } else {
    aDest->FillRect(aDestRect,
                    SurfacePattern(aSource, EXTEND_REPEAT, matrix),
                    DrawOptions(aOpacity));
  }
}

void
BasicCompositor::DrawQuad(const gfx::Rect& aRect, const gfx::Rect& aClipRect,
                          const EffectChain &aEffectChain,
                          gfx::Float aOpacity, const gfx::Matrix4x4 &aTransform,
                          const gfx::Point& aOffset)
{
  DrawTarget *dest = mRenderTarget ? mRenderTarget->mDrawTarget : mDrawTarget;

  if (!aTransform.Is2D()) {
    NS_WARNING("Can't handle 3D transforms yet!");
    return;
  }

  dest->PushClipRect(aClipRect);

  Matrix oldTransform = dest->GetTransform();
  Matrix newTransform = aTransform.As2D();
  newTransform.Translate(-aOffset.x, -aOffset.y);
  dest->SetTransform(newTransform);

  RefPtr<SourceSurface> sourceMask;
  Matrix maskTransform;
  if (aEffectChain.mSecondaryEffects[EFFECT_MASK]) {
    EffectMask *effectMask = static_cast<EffectMask*>(aEffectChain.mSecondaryEffects[EFFECT_MASK].get());
    sourceMask = effectMask->mMaskTexture->AsSourceBasic()->GetSurface();
    MOZ_ASSERT(effectMask->mMaskTransform.Is2D(), "How did we end up with a 3D transform here?!");
    MOZ_ASSERT(!effectMask->mIs3D);
    maskTransform = effectMask->mMaskTransform.As2D();
  }

  switch (aEffectChain.mPrimaryEffect->mType) {
    case EFFECT_SOLID_COLOR: {
      EffectSolidColor* effectSolidColor =
        static_cast<EffectSolidColor*>(aEffectChain.mPrimaryEffect.get());

      dest->FillRect(aRect,
                     ColorPattern(effectSolidColor->mColor),
                     DrawOptions(aOpacity));
      break;
    }
    case EFFECT_BGRA:
    case EFFECT_BGRX:
    case EFFECT_RGBA:
    case EFFECT_RGBX: {
      TexturedEffect* texturedEffect =
          static_cast<TexturedEffect*>(aEffectChain.mPrimaryEffect.get());
      TextureSourceBasic* source = texturedEffect->mTexture->AsSourceBasic();

      DrawSurfaceWithTextureCoords(dest, aRect,
                                   source->GetSurface(),
                                   texturedEffect->mTextureCoords,
                                   aOpacity, sourceMask, maskTransform);
      break;
    }
    case EFFECT_YCBCR: {
      NS_RUNTIMEABORT("Can't (easily) support component alpha with BasicCompositor!");
      break;
    }
    case EFFECT_RENDER_TARGET: {
      EffectRenderTarget* effectRenderTarget =
        static_cast<EffectRenderTarget*>(aEffectChain.mPrimaryEffect.get());
      RefPtr<BasicCompositingRenderTarget> surface
        = static_cast<BasicCompositingRenderTarget*>(effectRenderTarget->mRenderTarget.get());
      RefPtr<SourceSurface> sourceSurf = surface->mDrawTarget->Snapshot();

      DrawSurfaceWithTextureCoords(dest, aRect,
                                   sourceSurf,
                                   effectRenderTarget->mTextureCoords,
                                   aOpacity, sourceMask, maskTransform);
      break;
    }
    case EFFECT_COMPONENT_ALPHA: {
      NS_RUNTIMEABORT("Can't (easily) support component alpha with BasicCompositor!");
      break;
    }
    default: {
      NS_RUNTIMEABORT("Invalid effect type!");
      break;
    }
  }

  dest->SetTransform(oldTransform);
  dest->PopClip();
}

void
BasicCompositor::BeginFrame(const gfx::Rect *aClipRectIn,
                            const gfxMatrix& aTransform,
                            const gfx::Rect& aRenderBounds,
                            gfx::Rect *aClipRectOut /* = nullptr */,
                            gfx::Rect *aRenderBoundsOut /* = nullptr */)
{
  nsIntRect intRect;
  mWidget->GetClientBounds(intRect);
  Rect rect = Rect(0, 0, intRect.width, intRect.height);
  mWidgetSize = intRect.Size();

  if (mCopyTarget) {
    // If we have a copy target, then we don't have a widget-provided mDrawTarget (currently). Create a dummy
    // placeholder so that CreateRenderTarget() works.
    mDrawTarget = gfxPlatform::GetPlatform()->CreateOffscreenDrawTarget(IntSize(1,1), FORMAT_B8G8R8A8);
  }
  if (!mDrawTarget) {
    if (aRenderBoundsOut) {
      *aRenderBoundsOut = Rect();
    }
    return;
  }

  // Setup an intermediate render target to buffer all compositing. We will
  // copy this into mDrawTarget (the widget), and/or mCopyTarget in EndFrame()
  RefPtr<CompositingRenderTarget> target = CreateRenderTarget(IntRect(0, 0, intRect.width, intRect.height), INIT_MODE_CLEAR);
  SetRenderTarget(target);

  if (aRenderBoundsOut) {
    *aRenderBoundsOut = rect;
  }

  if (aClipRectIn) {
    mRenderTarget->mDrawTarget->PushClipRect(*aClipRectIn);
  } else {
    mRenderTarget->mDrawTarget->PushClipRect(rect);
    if (aClipRectOut) {
      *aClipRectOut = rect;
    }
  }
}

void
BasicCompositor::EndFrame()
{
  mRenderTarget->mDrawTarget->PopClip();

  if (mCopyTarget) {
    nsRefPtr<gfxASurface> thebes = gfxPlatform::GetPlatform()->GetThebesSurfaceForDrawTarget(mRenderTarget->mDrawTarget);
    gfxContextAutoSaveRestore restore(mCopyTarget);
    mCopyTarget->SetOperator(gfxContext::OPERATOR_SOURCE);
    mCopyTarget->SetSource(thebes);
    mCopyTarget->Paint();
  }
}

void
BasicCompositor::AbortFrame()
{
  mDrawTarget->PopClip();
}

}
}
