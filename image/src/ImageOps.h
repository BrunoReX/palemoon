/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_IMAGELIB_IMAGEOPS_H_
#define MOZILLA_IMAGELIB_IMAGEOPS_H_

#include "nsCOMPtr.h"
#include "nsRect.h"

class imgIContainer;

namespace mozilla {
namespace image {

class Image;

class ImageOps
{
public:
  /**
   * Creates a version of an existing image which does not animate and is frozen
   * at the first frame.
   *
   * @param aImage         The existing image.
   */
  static already_AddRefed<Image> Freeze(Image* aImage);
  static already_AddRefed<imgIContainer> Freeze(imgIContainer* aImage);

  /**
   * Creates a clipped version of an existing image. Animation is unaffected.
   *
   * @param aImage         The existing image.
   * @param aClip          The rectangle to clip the image against.
   */
  static already_AddRefed<Image> Clip(Image* aImage, nsIntRect aClip);
  static already_AddRefed<imgIContainer> Clip(imgIContainer* aImage, nsIntRect aClip);

private:
  // This is a static utility class, so disallow instantiation.
  virtual ~ImageOps() = 0;
};

} // namespace image
} // namespace mozilla

#endif // MOZILLA_IMAGELIB_IMAGEOPS_H_
