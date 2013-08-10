/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_IMAGELIB_IMAGE_H_
#define MOZILLA_IMAGELIB_IMAGE_H_

#include "imgIContainer.h"
#include "imgStatusTracker.h"

namespace mozilla {
namespace image {

class Image : public imgIContainer
{
public:
  imgStatusTracker& GetStatusTracker() { return *mStatusTracker; }

  /**
   * Flags for Image initialization.
   *
   * Meanings:
   *
   * INIT_FLAG_NONE: Lack of flags
   *
   * INIT_FLAG_DISCARDABLE: The container should be discardable
   *
   * INIT_FLAG_DECODE_ON_DRAW: The container should decode on draw rather than
   * decoding on load.
   *
   * INIT_FLAG_MULTIPART: The container will be used to display a stream of
   * images in a multipart channel. If this flag is set, INIT_FLAG_DISCARDABLE
   * and INIT_FLAG_DECODE_ON_DRAW must not be set.
   */
  static const uint32_t INIT_FLAG_NONE           = 0x0;
  static const uint32_t INIT_FLAG_DISCARDABLE    = 0x1;
  static const uint32_t INIT_FLAG_DECODE_ON_DRAW = 0x2;
  static const uint32_t INIT_FLAG_MULTIPART      = 0x4;

  /**
   * Creates a new image container.
   *
   * @param aObserver Observer to send decoder and animation notifications to.
   * @param aMimeType The mimetype of the image.
   * @param aFlags Initialization flags of the INIT_FLAG_* variety.
   */
  virtual nsresult Init(imgIDecoderObserver* aObserver,
                        const char* aMimeType,
                        const char* aURIString,
                        uint32_t aFlags) = 0;

  /**
   * The rectangle defining the location and size of the currently displayed
   * frame.
   */
  virtual void GetCurrentFrameRect(nsIntRect& aRect) = 0;

  /**
   * The size, in bytes, occupied by the significant data portions of the image.
   * This includes both compressed source data and decoded frames.
   */
  uint32_t SizeOfData();

  /**
   * The components that make up SizeOfData().
   */      
  virtual size_t HeapSizeOfSourceWithComputedFallback(nsMallocSizeOfFun aMallocSizeOf) const = 0;
  virtual size_t HeapSizeOfDecodedWithComputedFallback(nsMallocSizeOfFun aMallocSizeOf) const = 0;
  virtual size_t NonHeapSizeOfDecoded() const = 0;
  virtual size_t OutOfProcessSizeOfDecoded() const = 0;

  // Mimetype translation
  enum eDecoderType {
    eDecoderType_png     = 0,
    eDecoderType_gif     = 1,
    eDecoderType_jpeg    = 2,
    eDecoderType_bmp     = 3,
    eDecoderType_ico     = 4,
    eDecoderType_icon    = 5,
    eDecoderType_unknown = 6
  };
  static eDecoderType GetDecoderType(const char *aMimeType);

  void IncrementAnimationConsumers();
  void DecrementAnimationConsumers();
#ifdef DEBUG
  uint32_t GetAnimationConsumers() { return mAnimationConsumers; }
#endif

  void SetInnerWindowID(uint64_t aInnerWindowId) {
    mInnerWindowId = aInnerWindowId;
  }
  uint64_t InnerWindowID() const { return mInnerWindowId; }

  bool HasError() { return mError; }

protected:
  Image(imgStatusTracker* aStatusTracker);

  // Shared functionality for implementors of imgIContainer. Every
  // implementation of attribute animationMode should forward here.
  nsresult GetAnimationModeInternal(uint16_t *aAnimationMode);
  nsresult SetAnimationModeInternal(uint16_t aAnimationMode);

  /**
   * Decides whether animation should or should not be happening,
   * and makes sure the right thing is being done.
   */
  virtual void EvaluateAnimation();

  virtual nsresult StartAnimation() = 0;
  virtual nsresult StopAnimation() = 0;

  uint64_t mInnerWindowId;

  // Member data shared by all implementations of this abstract class
  nsAutoPtr<imgStatusTracker> mStatusTracker;
  uint32_t                    mAnimationConsumers;
  uint16_t                    mAnimationMode;   // Enum values in imgIContainer
  bool                        mInitialized:1;   // Have we been initalized?
  bool                        mAnimating:1;     // Are we currently animating?
  bool                        mError:1;         // Error handling

  /**
   * Extended by child classes, if they have additional
   * conditions for being able to animate
   */
  virtual bool ShouldAnimate() {
    return mAnimationConsumers > 0 && mAnimationMode != kDontAnimMode;
  }
};

} // namespace image
} // namespace mozilla

#endif // MOZILLA_IMAGELIB_IMAGE_H_
