/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageMetadata.h"

#include "RasterImage.h"
#include "nsComponentManagerUtils.h"
#include "nsSupportsPrimitives.h"

using namespace mozilla::image;

void
ImageMetadata::SetOnImage(RasterImage* image)
{
  if (mHotspotX != -1 && mHotspotY != -1) {
    nsCOMPtr<nsISupportsPRUint32> intwrapx = do_CreateInstance("@mozilla.org/supports-PRUint32;1");
    nsCOMPtr<nsISupportsPRUint32> intwrapy = do_CreateInstance("@mozilla.org/supports-PRUint32;1");
    intwrapx->SetData(mHotspotX);
    intwrapy->SetData(mHotspotY);
    image->Set("hotspotX", intwrapx);
    image->Set("hotspotY", intwrapy);
  }

  image->SetLoopCount(mLoopCount);

  for (uint32_t i = 0; i < image->GetNumFrames(); i++) {
    image->SetFrameAsNonPremult(i, mIsNonPremultiplied);
  }
}
