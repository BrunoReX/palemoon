/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_CanvasGradient_h
#define mozilla_dom_CanvasGradient_h

#include "mozilla/Attributes.h"
#include "nsTArray.h"
#include "mozilla/RefPtr.h"
#include "mozilla/dom/CanvasRenderingContext2DBinding.h"
#include "mozilla/dom/CanvasRenderingContext2D.h"
#include "mozilla/gfx/2D.h"
#include "nsWrapperCache.h"

#define NS_CANVASGRADIENTAZURE_PRIVATE_IID \
    {0x28425a6a, 0x90e0, 0x4d42, {0x9c, 0x75, 0xff, 0x60, 0x09, 0xb3, 0x10, 0xa8}}

namespace mozilla {
namespace dom {

class CanvasGradient : public nsISupports,
                       public nsWrapperCache
{
public:
  NS_DECLARE_STATIC_IID_ACCESSOR(NS_CANVASGRADIENTAZURE_PRIVATE_IID)
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(CanvasGradient)

  enum Type
  {
    LINEAR = 0,
    RADIAL
  };

  Type GetType()
  {
    return mType;
  }


  mozilla::gfx::GradientStops *
  GetGradientStopsForTarget(mozilla::gfx::DrawTarget *aRT)
  {
    if (mStops && mStops->GetBackendType() == aRT->GetType()) {
      return mStops;
    }

    mStops = aRT->CreateGradientStops(mRawStops.Elements(), mRawStops.Length());

    return mStops;
  }

  // WebIDL
  void AddColorStop(float offset, const nsAString& colorstr, ErrorResult& rv);

  JSObject* WrapObject(JSContext* aCx, JS::Handle<JSObject*> aScope) MOZ_OVERRIDE
  {
    return CanvasGradientBinding::Wrap(aCx, aScope, this);
  }

  CanvasRenderingContext2D* GetParentObject()
  {
    return mContext;
  }

protected:
  CanvasGradient(CanvasRenderingContext2D* aContext, Type aType)
    : mContext(aContext)
    , mType(aType)
  {
    SetIsDOMBinding();
  }

  nsRefPtr<CanvasRenderingContext2D> mContext;
  nsTArray<mozilla::gfx::GradientStop> mRawStops;
  mozilla::RefPtr<mozilla::gfx::GradientStops> mStops;
  Type mType;
  virtual ~CanvasGradient() {}
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_CanvasGradient_h
