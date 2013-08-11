/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2001
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Stuart Parmenter <pavlov@netscape.com>
 *   Chris Saari <saari@netscape.com>
 *   Federico Mena-Quintero <federico@novell.com>
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

/** @file
 * This file declares the imgContainer class, which
 * handles static and animated image containers.
 *
 * @author  Stuart Parmenter <pavlov@netscape.com>
 * @author  Chris Saari <saari@netscape.com>
 * @author  Arron Mogge <paper@animecity.nu>
 * @author  Andrew Smith <asmith15@learn.senecac.on.ca>
 */

#ifndef __imgContainer_h__
#define __imgContainer_h__

#include "nsCOMArray.h"
#include "nsCOMPtr.h"
#include "imgIContainer.h"
#include "nsIProperties.h"
#include "nsITimer.h"
#include "nsWeakReference.h"
#include "nsTArray.h"
#include "imgFrame.h"

#define NS_IMGCONTAINER_CID \
{ /* c76ff2c1-9bf6-418a-b143-3340c00112f7 */         \
     0x376ff2c1,                                     \
     0x9bf6,                                         \
     0x418a,                                         \
    {0xb1, 0x43, 0x33, 0x40, 0xc0, 0x01, 0x12, 0xf7} \
}

/**
 * Handles static and animated image containers.
 *
 *
 * @par A Quick Walk Through
 * The decoder initializes this class and calls AppendFrame() to add a frame.
 * Once imgContainer detects more than one frame, it starts the animation
 * with StartAnimation().
 *
 * @par
 * StartAnimation() checks if animating is allowed, and creates a timer.  The
 * timer calls Notify when the specified frame delay time is up.
 *
 * @par
 * Notify() moves on to the next frame, sets up the new timer delay, destroys
 * the old frame, and forces a redraw via observer->FrameChanged().
 *
 * @par
 * Each frame can have a different method of removing itself. These are
 * listed as imgIContainer::cDispose... constants.  Notify() calls 
 * DoComposite() to handle any special frame destruction.
 *
 * @par
 * The basic path through DoComposite() is:
 * 1) Calculate Area that needs updating, which is at least the area of
 *    aNextFrame.
 * 2) Dispose of previous frame.
 * 3) Draw new image onto compositingFrame.
 * See comments in DoComposite() for more information and optimizations.
 *
 * @par
 * The rest of the imgContainer specific functions are used by DoComposite to
 * destroy the old frame and build the new one.
 *
 * @note
 * <li> "Mask", "Alpha", and "Alpha Level" are interchangable phrases in
 * respects to imgContainer.
 *
 * @par
 * <li> GIFs never have more than a 1 bit alpha.
 * <li> APNGs may have a full alpha channel.
 *
 * @par
 * <li> Background color specified in GIF is ignored by web browsers.
 *
 * @par
 * <li> If Frame 3 wants to dispose by restoring previous, what it wants is to
 * restore the composition up to and including Frame 2, as well as Frame 2s
 * disposal.  So, in the middle of DoComposite when composing Frame 3, right
 * after destroying Frame 2's area, we copy compositingFrame to
 * prevCompositingFrame.  When DoComposite gets called to do Frame 4, we
 * copy prevCompositingFrame back, and then draw Frame 4 on top.
 *
 * @par
 * The mAnim structure has members only needed for animated images, so
 * it's not allocated until the second frame is added.
 *
 * @note
 * mAnimationMode, mLoopCount and mObserver are not in the mAnim structure
 * because the first two have public setters and the observer we only get
 * in Init().
 */
class imgContainer : public imgIContainer, 
                     public nsITimerCallback, 
                     public nsIProperties
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_IMGICONTAINER
  NS_DECL_NSITIMERCALLBACK
  NS_DECL_NSIPROPERTIES

  imgContainer();
  virtual ~imgContainer();

private:
  friend class imgRequest;

  struct Anim
  {
    //! Area of the first frame that needs to be redrawn on subsequent loops.
    nsIntRect                  firstFrameRefreshArea;
    // Note this doesn't hold a proper value until frame 2 finished decoding.
    PRInt32                    currentDecodingFrameIndex; // 0 to numFrames-1
    PRInt32                    currentAnimationFrameIndex; // 0 to numFrames-1
    //! Track the last composited frame for Optimizations (See DoComposite code)
    PRInt32                    lastCompositedFrameIndex;
    //! Whether we can assume there will be no more frames
    //! (and thus loop the animation)
    PRBool                     doneDecoding;
    //! Are we currently animating the image?
    PRBool                     animating;
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
    //! Timer to animate multiframed images
    nsCOMPtr<nsITimer>         timer;
    
    Anim() :
      firstFrameRefreshArea(),
      currentDecodingFrameIndex(0),
      currentAnimationFrameIndex(0),
      lastCompositedFrameIndex(-1),
      doneDecoding(PR_FALSE),
      animating(PR_FALSE)
    {
      ;
    }
    ~Anim()
    {
      if (timer)
        timer->Cancel();
    }
  };

  /**
   * Deletes and nulls out the frame in mFrames[framenum].
   *
   * Does not change the size of mFrames.
   *
   * @param framenum The index of the frame to be deleted. 
   *                 Must lie in [0, mFrames.Length() )
   */
  void DeleteImgFrame(PRUint32 framenum);

  imgFrame* GetImgFrame(PRUint32 framenum);
  imgFrame* GetDrawableImgFrame(PRUint32 framenum);
  imgFrame* GetCurrentImgFrame();
  imgFrame* GetCurrentDrawableImgFrame();
  PRInt32 GetCurrentImgFrameIndex() const;
  
  inline Anim* ensureAnimExists() {
    if (!mAnim)
      mAnim = new Anim();
    return mAnim;
  }
  
  /** Function for doing the frame compositing of animations
   *
   * @param aFrameToUse Set by DoComposite
   *                   (aNextFrame, compositingFrame, or compositingPrevFrame)
   * @param aDirtyRect  Area that the display will need to update
   * @param aPrevFrame  Last Frame seen/processed
   * @param aNextFrame  Frame we need to incorperate/display
   * @param aNextFrameIndex Position of aNextFrame in mFrames list
   */
  nsresult DoComposite(imgFrame** aFrameToUse, nsIntRect* aDirtyRect,
                       imgFrame* aPrevFrame,
                       imgFrame* aNextFrame,
                       PRInt32 aNextFrameIndex);
  
  /** Clears an area of <aFrame> with transparent black.
   *
   * @param aFrame Target Frame
   *
   * @note Does also clears the transparancy mask
   */
  static void ClearFrame(imgFrame* aFrame);
  
  //! @overload
  static void ClearFrame(imgFrame* aFrame, nsIntRect &aRect);
  
  //! Copy one frames's image and mask into another
  static PRBool CopyFrameImage(imgFrame *aSrcFrame,
                               imgFrame *aDstFrame);
  
  /** Draws one frames's image to into another,
   * at the position specified by aRect
   *
   * @param aSrcFrame  Frame providing the source image
   * @param aDstFrame  Frame where the image is drawn into
   * @param aRect      The position and size to draw the image
   */
  static nsresult DrawFrameTo(imgFrame *aSrcFrame,
                              imgFrame *aDstFrame,
                              nsIntRect& aRect);

  nsresult InternalAddFrameHelper(PRUint32 framenum, imgFrame *frame,
                                  PRUint8 **imageData, PRUint32 *imageLength,
                                  PRUint32 **paletteData, PRUint32 *paletteLength);
  nsresult InternalAddFrame(PRUint32 framenum, PRInt32 aX, PRInt32 aY, PRInt32 aWidth, PRInt32 aHeight,
                            gfxASurface::gfxImageFormat aFormat, PRUint8 aPaletteDepth,
                            PRUint8 **imageData, PRUint32 *imageLength,
                            PRUint32 **paletteData, PRUint32 *paletteLength);

private: // data

  nsIntSize                  mSize;
  
  //! All the frames of the image
  // *** IMPORTANT: if you use mFrames in a method, call RestoreDiscardedData() first to ensure
  //     that the frames actually exist (they may have been discarded to save memory).
  nsTArray<imgFrame *>       mFrames;
  int                        mNumFrames; /* stored separately from mFrames.Count() to support discarded images */
  
  nsCOMPtr<nsIProperties>    mProperties;

  // *** IMPORTANT: if you use mAnim in a method, call RestoreDiscardedData() first to ensure
  //     that the frames actually exist (they may have been discarded to save memory).
  imgContainer::Anim*        mAnim;
  
  //! See imgIContainer for mode constants
  PRUint16                   mAnimationMode;
  
  //! # loops remaining before animation stops (-1 no stop)
  PRInt32                    mLoopCount;
  
  //! imgIContainerObserver
  nsWeakPtr                  mObserver;

  PRBool                     mDiscardable;
  PRBool                     mDiscarded;
  nsCString                  mDiscardableMimeType;

  nsTArray<char>             mRestoreData;
  PRBool                     mRestoreDataDone;
  nsCOMPtr<nsITimer>         mDiscardTimer;

  nsresult ResetDiscardTimer (void);
  nsresult RestoreDiscardedData (void);
  nsresult ReloadImages (void);
  static void sDiscardTimerCallback (nsITimer *aTimer, void *aClosure);
};

#endif /* __imgContainer_h__ */
