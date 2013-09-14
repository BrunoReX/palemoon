/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_imagelib_FrameBlender_h_
#define mozilla_imagelib_FrameBlender_h_

#include "nsTArray.h"
#include "mozilla/TimeStamp.h"
#include "gfxASurface.h"

class imgFrame;

namespace mozilla {
namespace image {

/**
 * FrameBlender stores and gives access to imgFrames. It also knows how to
 * blend frames from previous to next, looping if necessary.
 *
 * All logic about when and whether to blend are external to FrameBlender.
 */

class FrameBlender
{
public:

  FrameBlender();
  ~FrameBlender();

  bool DoBlend(nsIntRect* aDirtyRect, uint32_t aPrevFrameIndex,
               uint32_t aNextFrameIndex);

  /**
   * Get the @aIndex-th frame, including (if applicable) any results of
   * blending.
   */
  imgFrame* GetFrame(uint32_t aIndex) const;

  /**
   * Get the @aIndex-th frame in the frame index, ignoring results of blending.
   */
  imgFrame* RawGetFrame(uint32_t aIndex) const;

  void InsertFrame(uint32_t framenum, imgFrame* aFrame);
  void RemoveFrame(uint32_t framenum);
  imgFrame* SwapFrame(uint32_t framenum, imgFrame* aFrame);
  void ClearFrames();

  /* The total number of frames in this image. */
  uint32_t GetNumFrames() const;

  void Discard();

  void SetSize(nsIntSize aSize) { mSize = aSize; }

  size_t SizeOfDecodedWithComputedFallbackIfHeap(gfxASurface::MemoryLocation aLocation,
                                                 nsMallocSizeOfFun aMallocSizeOf) const;

  void ResetAnimation();

  // "Blend" method indicates how the current image is combined with the
  // previous image.
  enum FrameBlendMethod {
    // All color components of the frame, including alpha, overwrite the current
    // contents of the frame's output buffer region
    kBlendSource =  0,

    // The frame should be composited onto the output buffer based on its alpha,
    // using a simple OVER operation
    kBlendOver
  };

  enum FrameDisposalMethod {
    kDisposeClearAll         = -1, // Clear the whole image, revealing
                                   // what was there before the gif displayed
    kDisposeNotSpecified,   // Leave frame, let new frame draw on top
    kDisposeKeep,           // Leave frame, let new frame draw on top
    kDisposeClear,          // Clear the frame's area, revealing bg
    kDisposeRestorePrevious // Restore the previous (composited) frame
  };

  // A hint as to whether an individual frame is entirely opaque, or requires
  // alpha blending.
  enum FrameAlpha {
    kFrameHasAlpha,
    kFrameOpaque
  };

private:

  struct Anim
  {
    //! Track the last composited frame for Optimizations (See DoComposite code)
    int32_t                    lastCompositedFrameIndex;

    /** For managing blending of frames
     *
     * Some animations will use the compositingFrame to composite images
     * and just hand this back to the caller when it is time to draw the frame.
     * NOTE: When clearing compositingFrame, remember to set
     *       lastCompositedFrameIndex to -1.  Code assume that if
     *       lastCompositedFrameIndex >= 0 then compositingFrame exists.
     */
    nsAutoPtr<imgFrame>        compositingFrame;

    /** the previous composited frame, for DISPOSE_RESTORE_PREVIOUS
     *
     * The Previous Frame (all frames composited up to the current) needs to be
     * stored in cases where the image specifies it wants the last frame back
     * when it's done with the current frame.
     */
    nsAutoPtr<imgFrame>        compositingPrevFrame;

    Anim() :
      lastCompositedFrameIndex(-1)
    {}
  };

  inline void EnsureAnimExists()
  {
    if (!mAnim) {
      // Create the animation context
      mAnim = new Anim();
    }
  }

  /** Clears an area of <aFrame> with transparent black.
   *
   * @param aFrameData Target Frame data
   * @param aFrameRect The rectangle of the data pointed ot by aFrameData
   *
   * @note Does also clears the transparancy mask
   */
  static void ClearFrame(uint8_t* aFrameData, const nsIntRect& aFrameRect);
  static void ClearFrame(imgFrame* aFrame);

  //! @overload
  static void ClearFrame(uint8_t* aFrameData, const nsIntRect& aFrameRect, const nsIntRect &aRectToClear);
  static void ClearFrame(imgFrame* aFrame, const nsIntRect& aRectToClear);

  //! Copy one frames's image and mask into another
  static bool CopyFrameImage(uint8_t *aDataSrc, const nsIntRect& aRectSrc,
                             uint8_t *aDataDest, const nsIntRect& aRectDest);
  static bool CopyFrameImage(imgFrame* aSrc, imgFrame* aDst);

  /**
   * Draws one frames's image to into another, at the position specified by
   * aSrcRect.
   *
   * @aSrcData the raw data of the current frame being drawn
   * @aSrcRect the size of the source frame, and the position of that frame in
   *           the composition frame
   * @aSrcPaletteLength the length (in bytes) of the palette at the beginning
   *                    of the source data (0 if image is not paletted)
   * @aSrcHasAlpha whether the source data represents an image with alpha
   * @aDstPixels the raw data of the composition frame where the current frame
   *             is drawn into (32-bit ARGB)
   * @aDstRect the size of the composition frame
   * @aBlendMethod the blend method for how to blend src on the composition frame.
   */
  static nsresult DrawFrameTo(uint8_t *aSrcData, const nsIntRect& aSrcRect,
                              uint32_t aSrcPaletteLength, bool aSrcHasAlpha,
                              uint8_t *aDstPixels, const nsIntRect& aDstRect,
                              FrameBlendMethod aBlendMethod);
  static nsresult DrawFrameTo(imgFrame* aSrc, imgFrame* aDst, const nsIntRect& aSrcRect);

private: // data
  //! All the frames of the image
  // IMPORTANT: if you use mFrames in a method, call EnsureImageIsDecoded() first
  // to ensure that the frames actually exist (they may have been discarded to save
  // memory, or we may be decoding on draw).
  nsTArray<imgFrame*> mFrames;

  nsIntSize mSize;

  // IMPORTANT: if you use mAnim in a method, call EnsureImageIsDecoded() first to ensure
  // that the frames actually exist (they may have been discarded to save memory, or
  // we maybe decoding on draw).
  Anim* mAnim;
};

} // namespace image
} // namespace mozilla

#endif /* mozilla_imagelib_FrameBlender_h_ */
