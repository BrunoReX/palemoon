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
 *   Robert O'Callahan <robert@ocallahan.org>
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

#include "FrameLayerBuilder.h"

#include "nsDisplayList.h"
#include "nsPresContext.h"
#include "nsLayoutUtils.h"
#include "Layers.h"
#include "BasicLayers.h"
#include "nsSubDocumentFrame.h"
#include "nsCSSRendering.h"
#include "nsCSSFrameConstructor.h"
#include "gfxUtils.h"
#include "nsImageFrame.h"

#ifdef DEBUG
#include <stdio.h>
#endif

using namespace mozilla::layers;

namespace mozilla {

namespace {

/**
 * This is the userdata we associate with a layer manager.
 */
class LayerManagerData : public LayerUserData {
public:
  LayerManagerData(LayerManager *aManager) :
    mInvalidateAllLayers(PR_FALSE),
    mLayerManager(aManager)
  {
    MOZ_COUNT_CTOR(LayerManagerData);
    mFramesWithLayers.Init();
  }
  ~LayerManagerData() {
    // Remove display item data properties now, since we won't be able
    // to find these frames again without mFramesWithLayers.
    mFramesWithLayers.EnumerateEntries(
        FrameLayerBuilder::RemoveDisplayItemDataForFrame, nsnull);
    MOZ_COUNT_DTOR(LayerManagerData);
  }

  /**
   * Tracks which frames have layers associated with them.
   */
  nsTHashtable<nsPtrHashKey<nsIFrame> > mFramesWithLayers;
  PRPackedBool mInvalidateAllLayers;
  /** Layer manager we belong to, we hold a reference to this object. */
  nsRefPtr<LayerManager> mLayerManager;
};

static void DestroyRegion(void* aPropertyValue)
{
  delete static_cast<nsRegion*>(aPropertyValue);
}

/**
 * This property represents a region that should be invalidated in every
 * ThebesLayer child whose parent ContainerLayer is associated with the
 * frame. This is an nsRegion*; the coordinates of the region are
 * relative to the top-left of the border-box of the frame the property
 * is attached to (which is the frame for the ContainerLayer).
 * 
 * We add to this region in InvalidateThebesLayerContents. The region
 * is propagated to ContainerState in BuildContainerLayerFor, and then
 * the region(s) are actually invalidated in CreateOrRecycleThebesLayer.
 *
 * When the property value is null, the region is infinite --- i.e. all
 * areas of the ThebesLayers should be invalidated.
 */
NS_DECLARE_FRAME_PROPERTY(ThebesLayerInvalidRegionProperty, DestroyRegion)

static void DestroyPoint(void* aPropertyValue)
{
  delete static_cast<nsPoint*>(aPropertyValue);
}

/**
 * The valid content in our child ThebesLayers is defined relative to
 * the offset from this frame to its active scroll root, mapped back
 * by the ThebesLayer's inverse transform.  Since we accumulate the
 * region invalidated between last-paint and next-paint, and because
 * the offset of this frame to its active root may change during that
 * period, we save the offset at last-paint in this property and use
 * it to invalidate at next-paint.
 */
NS_DECLARE_FRAME_PROPERTY(ThebesLayerLastPaintOffsetProperty, DestroyPoint)

/**
 * This is a helper object used to build up the layer children for
 * a ContainerLayer.
 */
class ContainerState {
public:
  ContainerState(nsDisplayListBuilder* aBuilder,
                 LayerManager* aManager,
                 nsIFrame* aContainerFrame,
                 ContainerLayer* aContainerLayer) :
    mBuilder(aBuilder), mManager(aManager),
    mContainerFrame(aContainerFrame), mContainerLayer(aContainerLayer),
    mNextFreeRecycledThebesLayer(0), mNextFreeRecycledColorLayer(0),
    mNextFreeRecycledImageLayer(0), mInvalidateAllThebesContent(PR_FALSE)
  {
    CollectOldLayers();
  }

  void SetInvalidThebesContent(const nsIntRegion& aRegion)
  {
    mInvalidThebesContent = aRegion;
  }
  void SetInvalidateAllThebesContent()
  {
    mInvalidateAllThebesContent = PR_TRUE;
  }
  /**
   * This is the method that actually walks a display list and builds
   * the child layers. We invoke it recursively to process clipped sublists.
   * @param aClipRect the clip rect to apply to the list items, or null
   * if no clipping is required
   */
  void ProcessDisplayItems(const nsDisplayList& aList,
                           FrameLayerBuilder::Clip& aClip);
  /**
   * This finalizes all the open ThebesLayers by popping every element off
   * mThebesLayerDataStack, then sets the children of the container layer
   * to be all the layers in mNewChildLayers in that order and removes any
   * layers as children of the container that aren't in mNewChildLayers.
   * @param aTextContentFlags if any child layer has CONTENT_COMPONENT_ALPHA,
   * set *aTextContentFlags to CONTENT_COMPONENT_ALPHA
   */
  void Finish(PRUint32 *aTextContentFlags);

  nsRect GetChildrenBounds() { return mBounds; }

protected:
  /**
   * We keep a stack of these to represent the ThebesLayers that are
   * currently available to have display items added to.
   * We use a stack here because as much as possible we want to
   * assign display items to existing ThebesLayers, and to the lowest
   * ThebesLayer in z-order. This reduces the number of layers and
   * makes it more likely a display item will be rendered to an opaque
   * layer, giving us the best chance of getting subpixel AA.
   */
  class ThebesLayerData {
  public:
    ThebesLayerData() :
      mActiveScrolledRoot(nsnull), mLayer(nsnull),
      mIsSolidColorInVisibleRegion(PR_FALSE),
      mNeedComponentAlpha(PR_FALSE),
      mForceTransparentSurface(PR_FALSE),
      mImage(nsnull) {}
    /**
     * Record that an item has been added to the ThebesLayer, so we
     * need to update our regions.
     * @param aVisibleRect the area of the item that's visible
     * @param aDrawRect the area of the item that would be drawn if it
     * was completely visible
     * @param aOpaqueRect if non-null, the area of the item that's opaque.
     * We pass in a separate opaque rect because the opaque rect can be
     * bigger than the visible rect, and we want to have the biggest
     * opaque rect that we can.
     * @param aSolidColor if non-null, the visible area of the item is
     * a constant color given by *aSolidColor
     */
    void Accumulate(nsDisplayListBuilder* aBuilder,
                    nsDisplayItem* aItem,
                    const nsIntRect& aVisibleRect,
                    const nsIntRect& aDrawRect,
                    const FrameLayerBuilder::Clip& aClip);
    nsIFrame* GetActiveScrolledRoot() { return mActiveScrolledRoot; }

    /**
     * If this represents only a nsDisplayImage, and the image type
     * supports being optimized to an ImageLayer (TYPE_RASTER only) returns
     * an ImageContainer for the image.
     */
    nsRefPtr<ImageContainer> CanOptimizeImageLayer(LayerManager* aManager);

    /**
     * The region of visible content in the layer, relative to the
     * container layer (which is at the snapped top-left of the display
     * list reference frame).
     */
    nsIntRegion  mVisibleRegion;
    /**
     * The region of visible content above the layer and below the
     * next ThebesLayerData currently in the stack, if any. Note that not
     * all ThebesLayers for the container are in the ThebesLayerData stack.
     * Same coordinate system as mVisibleRegion.
     */
    nsIntRegion  mVisibleAboveRegion;
    /**
     * The region containing the bounds of all display items in the layer,
     * regardless of visbility.
     * Same coordinate system as mVisibleRegion.
     */
    nsIntRegion  mDrawRegion;
    /**
     * The region containing the bounds of all display items (regardless
     * of visibility) in the layer and below the next ThebesLayerData
     * currently in the stack, if any.
     * Note that not all ThebesLayers for the container are in the
     * ThebesLayerData stack.
     * Same coordinate system as mVisibleRegion.
     */
    nsIntRegion  mDrawAboveRegion;
    /**
     * The region of visible content in the layer that is opaque.
     * Same coordinate system as mVisibleRegion.
     */
    nsIntRegion  mOpaqueRegion;
    /**
     * The "active scrolled root" for all content in the layer. Must
     * be non-null; all content in a ThebesLayer must have the same
     * active scrolled root.
     */
    nsIFrame*    mActiveScrolledRoot;
    ThebesLayer* mLayer;
    /**
     * If mIsSolidColorInVisibleRegion is true, this is the color of the visible
     * region.
     */
    nscolor      mSolidColor;
    /**
     * True if every pixel in mVisibleRegion will have color mSolidColor.
     */
    PRPackedBool mIsSolidColorInVisibleRegion;
    /**
     * True if there is any text visible in the layer that's over
     * transparent pixels in the layer.
     */
    PRPackedBool mNeedComponentAlpha;
    /**
     * Set if the layer should be treated as transparent, even if its entire
     * area is covered by opaque display items. For example, this needs to
     * be set if something is going to "punch holes" in the layer by clearing
     * part of its surface.
     */
    PRPackedBool mForceTransparentSurface;

    /**
     * Stores the pointer to the nsDisplayImage if we want to
     * convert this to an ImageLayer.
     */
    nsDisplayImage* mImage;
    /**
     * Stores the clip that we need to apply to the image.
     */
    FrameLayerBuilder::Clip mImageClip;
  };

  /**
   * Grab the next recyclable ThebesLayer, or create one if there are no
   * more recyclable ThebesLayers. Does any necessary invalidation of
   * a recycled ThebesLayer, and sets up the transform on the ThebesLayer
   * to account for scrolling.
   */
  already_AddRefed<ThebesLayer> CreateOrRecycleThebesLayer(nsIFrame* aActiveScrolledRoot);
  /**
   * Grab the next recyclable ColorLayer, or create one if there are no
   * more recyclable ColorLayers.
   */
  already_AddRefed<ColorLayer> CreateOrRecycleColorLayer();
  /**
   * Grab the next recyclable ImageLayer, or create one if there are no
   * more recyclable ImageLayers.
   */
  already_AddRefed<ImageLayer> CreateOrRecycleImageLayer();
  /**
   * Grabs all ThebesLayers and ColorLayers from the ContainerLayer and makes them
   * available for recycling.
   */
  void CollectOldLayers();
  /**
   * If aItem used to belong to a ThebesLayer, invalidates the area of
   * aItem in that layer. If aNewLayer is a ThebesLayer, invalidates the area of
   * aItem in that layer.
   */
  void InvalidateForLayerChange(nsDisplayItem* aItem, Layer* aNewLayer);
  /**
   * Try to determine whether the ThebesLayer at aThebesLayerIndex
   * has a single opaque color behind it, over the entire bounds of its visible
   * region.
   * If successful, return that color, otherwise return NS_RGBA(0,0,0,0).
   */
  nscolor FindOpaqueBackgroundColorFor(PRInt32 aThebesLayerIndex);
  /**
   * Indicate that we are done adding items to the ThebesLayer at the top of
   * mThebesLayerDataStack. Set the final visible region and opaque-content
   * flag, and pop it off the stack.
   */
  void PopThebesLayerData();
  /**
   * Find the ThebesLayer to which we should assign the next display item.
   * We scan the ThebesLayerData stack to find the topmost ThebesLayer
   * that is compatible with the display item (i.e., has the same
   * active scrolled root), and that has no content from other layers above
   * it and intersecting the aVisibleRect.
   * Returns the layer, and also updates the ThebesLayerData. Will
   * push a new ThebesLayerData onto the stack if no suitable existing
   * layer is found. If we choose a ThebesLayer that's already on the
   * ThebesLayerData stack, later elements on the stack will be popped off.
   * @param aVisibleRect the area of the next display item that's visible
   * @param aActiveScrolledRoot the active scrolled root for the next
   * display item
   * @param aOpaqueRect if non-null, a region of the display item that is opaque
   * @param aSolidColor if non-null, indicates that every pixel in aVisibleRect
   * will be painted with aSolidColor by the item
   */
  already_AddRefed<ThebesLayer> FindThebesLayerFor(nsDisplayItem* aItem,
                                                   const nsIntRect& aVisibleRect,
                                                   const nsIntRect& aDrawRect,
                                                   const FrameLayerBuilder::Clip& aClip,
                                                   nsIFrame* aActiveScrolledRoot);
  ThebesLayerData* GetTopThebesLayerData()
  {
    return mThebesLayerDataStack.IsEmpty() ? nsnull
        : mThebesLayerDataStack[mThebesLayerDataStack.Length() - 1].get();
  }

  nsDisplayListBuilder*            mBuilder;
  LayerManager*                    mManager;
  nsIFrame*                        mContainerFrame;
  ContainerLayer*                  mContainerLayer;
  /**
   * The region of ThebesLayers that should be invalidated every time
   * we recycle one.
   */
  nsIntRegion                      mInvalidThebesContent;
  nsRect                           mBounds;
  nsAutoTArray<nsAutoPtr<ThebesLayerData>,1>  mThebesLayerDataStack;
  /**
   * We collect the list of children in here. During ProcessDisplayItems,
   * the layers in this array either have mContainerLayer as their parent,
   * or no parent.
   */
  typedef nsAutoTArray<nsRefPtr<Layer>,1> AutoLayersArray;
  AutoLayersArray                  mNewChildLayers;
  nsTArray<nsRefPtr<ThebesLayer> > mRecycledThebesLayers;
  nsTArray<nsRefPtr<ColorLayer> >  mRecycledColorLayers;
  nsTArray<nsRefPtr<ImageLayer> >  mRecycledImageLayers;
  PRUint32                         mNextFreeRecycledThebesLayer;
  PRUint32                         mNextFreeRecycledColorLayer;
  PRUint32                         mNextFreeRecycledImageLayer;
  PRPackedBool                     mInvalidateAllThebesContent;
};

class ThebesDisplayItemLayerUserData : public LayerUserData
{
public:
  ThebesDisplayItemLayerUserData() :
    mForcedBackgroundColor(NS_RGBA(0,0,0,0)) {}

  /**
   * A color that should be painted over the bounds of the layer's visible
   * region before any other content is painted.
   */
  nscolor mForcedBackgroundColor;
};

/**
 * The address of gThebesDisplayItemLayerUserData is used as the user
 * data key for ThebesLayers created by FrameLayerBuilder.
 * It identifies ThebesLayers used to draw non-layer content, which are
 * therefore eligible for recycling. We want display items to be able to
 * create their own dedicated ThebesLayers in BuildLayer, if necessary,
 * and we wouldn't want to accidentally recycle those.
 * The user data is a ThebesDisplayItemLayerUserData.
 */
PRUint8 gThebesDisplayItemLayerUserData;
/**
 * The address of gColorLayerUserData is used as the user
 * data key for ColorLayers created by FrameLayerBuilder.
 * The user data is null.
 */
PRUint8 gColorLayerUserData;
/**
 * The address of gImageLayerUserData is used as the user
 * data key for ImageLayers created by FrameLayerBuilder.
 * The user data is null.
 */
PRUint8 gImageLayerUserData;
/**
 * The address of gLayerManagerUserData is used as the user
 * data key for retained LayerManagers managed by FrameLayerBuilder.
 * The user data is a LayerManagerData.
 */
PRUint8 gLayerManagerUserData;

} // anonymous namespace

void
FrameLayerBuilder::Init(nsDisplayListBuilder* aBuilder)
{
  mRootPresContext = aBuilder->ReferenceFrame()->PresContext()->GetRootPresContext();
  if (mRootPresContext) {
    mInitialDOMGeneration = mRootPresContext->GetDOMGeneration();
  }
}

PRBool
FrameLayerBuilder::DisplayItemDataEntry::HasNonEmptyContainerLayer()
{
  for (PRUint32 i = 0; i < mData.Length(); ++i) {
    if (mData[i].mLayer->GetType() == Layer::TYPE_CONTAINER &&
        mData[i].mLayerState != LAYER_ACTIVE_EMPTY)
      return PR_TRUE;
  }
  return PR_FALSE;
}

/* static */ void
FrameLayerBuilder::InternalDestroyDisplayItemData(nsIFrame* aFrame,
                                                  void* aPropertyValue,
                                                  PRBool aRemoveFromFramesWithLayers)
{
  nsRefPtr<LayerManager> managerRef;
  nsTArray<DisplayItemData>* array =
    reinterpret_cast<nsTArray<DisplayItemData>*>(&aPropertyValue);
  NS_ASSERTION(!array->IsEmpty(), "Empty arrays should not be stored");

  if (aRemoveFromFramesWithLayers) {
    LayerManager* manager = array->ElementAt(0).mLayer->Manager();
    LayerManagerData* data = static_cast<LayerManagerData*>
      (manager->GetUserData(&gLayerManagerUserData));
    NS_ASSERTION(data, "Frame with layer should have been recorded");
    data->mFramesWithLayers.RemoveEntry(aFrame);
    if (data->mFramesWithLayers.Count() == 0) {
      // Destroying our user data will consume a reference from the layer
      // manager. But don't actually release until we've released all the layers
      // in the DisplayItemData array below!
      managerRef = manager;
      manager->RemoveUserData(&gLayerManagerUserData);
    }
  }

  array->~nsTArray<DisplayItemData>();
}

/* static */ void
FrameLayerBuilder::DestroyDisplayItemData(nsIFrame* aFrame,
                                          void* aPropertyValue)
{
  InternalDestroyDisplayItemData(aFrame, aPropertyValue, PR_TRUE);
}

void
FrameLayerBuilder::DidBeginRetainedLayerTransaction(LayerManager* aManager)
{
  mRetainingManager = aManager;
  LayerManagerData* data = static_cast<LayerManagerData*>
    (aManager->GetUserData(&gLayerManagerUserData));
  if (data) {
    mInvalidateAllLayers = data->mInvalidateAllLayers;
  }
}

/**
 * A helper function to remove the mThebesLayerItems entries for every
 * layer in aLayer's subtree.
 */
void
FrameLayerBuilder::RemoveThebesItemsForLayerSubtree(Layer* aLayer)
{
  ThebesLayer* thebes = aLayer->AsThebesLayer();
  if (thebes) {
    mThebesLayerItems.RemoveEntry(thebes);
    return;
  }

  for (Layer* child = aLayer->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    RemoveThebesItemsForLayerSubtree(child);
  }
}

void
FrameLayerBuilder::DidEndTransaction(LayerManager* aManager)
{
  if (aManager != mRetainingManager) {
    Layer* root = aManager->GetRoot();
    if (root) {
      RemoveThebesItemsForLayerSubtree(root);
    }
  }
}

void
FrameLayerBuilder::WillEndTransaction(LayerManager* aManager)
{
  if (aManager != mRetainingManager)
    return;

  // We need to save the data we'll need to support retaining. We do this
  // before we paint so that invalidation triggered by painting will
  // be able to update the ThebesLayerInvalidRegionProperty values
  // correctly and the NS_FRAME_HAS_CONTAINER_LAYER bits will be set
  // correctly.
  LayerManagerData* data = static_cast<LayerManagerData*>
    (mRetainingManager->GetUserData(&gLayerManagerUserData));
  if (data) {
    // Update all the frames that used to have layers.
    data->mFramesWithLayers.EnumerateEntries(UpdateDisplayItemDataForFrame, this);
  } else {
    data = new LayerManagerData(mRetainingManager);
    mRetainingManager->SetUserData(&gLayerManagerUserData, data);
  }
  // Now go through all the frames that didn't have any retained
  // display items before, and record those retained display items.
  // This also empties mNewDisplayItemData.
  mNewDisplayItemData.EnumerateEntries(StoreNewDisplayItemData, data);
  data->mInvalidateAllLayers = PR_FALSE;

  NS_ASSERTION(data->mFramesWithLayers.Count() > 0,
               "Some frame must have a layer!");
}

static void
SetHasContainerLayer(nsIFrame* aFrame, nsPoint aOffsetToRoot)
{
  aFrame->AddStateBits(NS_FRAME_HAS_CONTAINER_LAYER);
  for (nsIFrame* f = aFrame;
       f && !(f->GetStateBits() & NS_FRAME_HAS_CONTAINER_LAYER_DESCENDANT);
       f = nsLayoutUtils::GetCrossDocParentFrame(f)) {
    f->AddStateBits(NS_FRAME_HAS_CONTAINER_LAYER_DESCENDANT);
  }

  FrameProperties props = aFrame->Properties();
  nsPoint* lastPaintOffset = static_cast<nsPoint*>
    (props.Get(ThebesLayerLastPaintOffsetProperty()));
  if (lastPaintOffset) {
    *lastPaintOffset = aOffsetToRoot;
  } else {
    props.Set(ThebesLayerLastPaintOffsetProperty(), new nsPoint(aOffsetToRoot));
  }
}

static void
SetNoContainerLayer(nsIFrame* aFrame)
{
  FrameProperties props = aFrame->Properties();
  props.Delete(ThebesLayerInvalidRegionProperty());
  props.Delete(ThebesLayerLastPaintOffsetProperty());
  aFrame->RemoveStateBits(NS_FRAME_HAS_CONTAINER_LAYER);
}

/* static */ PLDHashOperator
FrameLayerBuilder::UpdateDisplayItemDataForFrame(nsPtrHashKey<nsIFrame>* aEntry,
                                                 void* aUserArg)
{
  FrameLayerBuilder* builder = static_cast<FrameLayerBuilder*>(aUserArg);
  nsIFrame* f = aEntry->GetKey();
  FrameProperties props = f->Properties();
  DisplayItemDataEntry* newDisplayItems =
    builder ? builder->mNewDisplayItemData.GetEntry(f) : nsnull;
  if (!newDisplayItems) {
    // This frame was visible, but isn't anymore.
    PRBool found;
    void* prop = props.Remove(DisplayItemDataProperty(), &found);
    NS_ASSERTION(found, "How can the frame property be missing?");
    // Pass PR_FALSE to not remove from mFramesWithLayers, we'll remove it
    // by returning PL_DHASH_REMOVE below.
    // Note that DestroyDisplayItemData would delete the user data
    // for the retained layer manager if it removed the last entry from
    // mFramesWithLayers, but we won't. That's OK because our caller
    // is DidEndTransaction, which would recreate the user data
    // anyway.
    InternalDestroyDisplayItemData(f, prop, PR_FALSE);
    SetNoContainerLayer(f);
    return PL_DHASH_REMOVE;
  }

  if (newDisplayItems->HasNonEmptyContainerLayer()) {
    // Reset or create the invalid region now so we can start collecting
    // new dirty areas.
    // Note that the NS_FRAME_HAS_CONTAINER_LAYER bit is set in
    // BuildContainerLayerFor, so we don't need to set it here.
    nsRegion* invalidRegion = static_cast<nsRegion*>
      (props.Get(ThebesLayerInvalidRegionProperty()));
    if (invalidRegion) {
      invalidRegion->SetEmpty();
    } else {
      props.Set(ThebesLayerInvalidRegionProperty(), new nsRegion());
    }
  } else {
    SetNoContainerLayer(f);
  }

  // We need to remove and re-add the DisplayItemDataProperty in
  // case the nsTArray changes the value of its mHdr.
  void* propValue = props.Remove(DisplayItemDataProperty());
  NS_ASSERTION(propValue, "mFramesWithLayers out of sync");
  PR_STATIC_ASSERT(sizeof(nsTArray<DisplayItemData>) == sizeof(void*));
  nsTArray<DisplayItemData>* array =
    reinterpret_cast<nsTArray<DisplayItemData>*>(&propValue);
  // Steal the list of display item layers
  array->SwapElements(newDisplayItems->mData);
  props.Set(DisplayItemDataProperty(), propValue);
  // Don't need to process this frame again
  builder->mNewDisplayItemData.RawRemoveEntry(newDisplayItems);
  return PL_DHASH_NEXT;
}

/* static */ PLDHashOperator
FrameLayerBuilder::StoreNewDisplayItemData(DisplayItemDataEntry* aEntry,
                                           void* aUserArg)
{
  LayerManagerData* data = static_cast<LayerManagerData*>(aUserArg);
  nsIFrame* f = aEntry->GetKey();
  FrameProperties props = f->Properties();
  // Remember that this frame has display items in retained layers
  NS_ASSERTION(!data->mFramesWithLayers.GetEntry(f),
               "We shouldn't get here if we're already in mFramesWithLayers");
  data->mFramesWithLayers.PutEntry(f);
  NS_ASSERTION(!props.Get(DisplayItemDataProperty()),
               "mFramesWithLayers out of sync");

  void* propValue;
  nsTArray<DisplayItemData>* array =
    new (&propValue) nsTArray<DisplayItemData>();
  // Steal the list of display item layers
  array->SwapElements(aEntry->mData);
  // Save it
  props.Set(DisplayItemDataProperty(), propValue);

  if (f->GetStateBits() & NS_FRAME_HAS_CONTAINER_LAYER) {
    props.Set(ThebesLayerInvalidRegionProperty(), new nsRegion());
  }
  return PL_DHASH_REMOVE;
}

PRBool
FrameLayerBuilder::HasRetainedLayerFor(nsIFrame* aFrame, PRUint32 aDisplayItemKey)
{
  void* propValue = aFrame->Properties().Get(DisplayItemDataProperty());
  if (!propValue)
    return PR_FALSE;

  nsTArray<DisplayItemData>* array =
    (reinterpret_cast<nsTArray<DisplayItemData>*>(&propValue));
  for (PRUint32 i = 0; i < array->Length(); ++i) {
    if (array->ElementAt(i).mDisplayItemKey == aDisplayItemKey) {
      Layer* layer = array->ElementAt(i).mLayer;
      if (layer->Manager()->GetUserData(&gLayerManagerUserData)) {
        // All layer managers with our user data are retained layer managers
        return PR_TRUE;
      }
    }
  }
  return PR_FALSE;
}

Layer*
FrameLayerBuilder::GetOldLayerFor(nsIFrame* aFrame, PRUint32 aDisplayItemKey)
{
  // If we need to build a new layer tree, then just refuse to recycle
  // anything.
  if (!mRetainingManager || mInvalidateAllLayers)
    return nsnull;

  void* propValue = aFrame->Properties().Get(DisplayItemDataProperty());
  if (!propValue)
    return nsnull;

  nsTArray<DisplayItemData>* array =
    (reinterpret_cast<nsTArray<DisplayItemData>*>(&propValue));
  for (PRUint32 i = 0; i < array->Length(); ++i) {
    if (array->ElementAt(i).mDisplayItemKey == aDisplayItemKey) {
      Layer* layer = array->ElementAt(i).mLayer;
      if (layer->Manager() == mRetainingManager)
        return layer;
    }
  }
  return nsnull;
}

/**
 * Invalidate aRegion in aLayer. aLayer is in the coordinate system
 * *after* aLayer's transform has been applied, so we need to
 * apply the inverse of that transform before calling InvalidateRegion.
 * Currently we assume that the transform is just an integer translation,
 * since that's all we need for scrolling.
 */
static void
InvalidatePostTransformRegion(ThebesLayer* aLayer, const nsIntRegion& aRegion)
{
  gfxMatrix transform;
  if (aLayer->GetTransform().Is2D(&transform)) {
    NS_ASSERTION(!transform.HasNonIntegerTranslation(),
                 "Matrix not just an integer translation?");
    // Convert the region from the coordinates of the container layer
    // (relative to the snapped top-left of the display list reference frame)
    // to the ThebesLayer's own coordinates
    nsIntRegion rgn = aRegion;
    rgn.MoveBy(-nsIntPoint(PRInt32(transform.x0), PRInt32(transform.y0)));
    aLayer->InvalidateRegion(rgn);
  } else {
    NS_ERROR("Only 2D transformations currently supported");
  }
}

already_AddRefed<ColorLayer>
ContainerState::CreateOrRecycleColorLayer()
{
  nsRefPtr<ColorLayer> layer;
  if (mNextFreeRecycledColorLayer < mRecycledColorLayers.Length()) {
    // Recycle a layer
    layer = mRecycledColorLayers[mNextFreeRecycledColorLayer];
    ++mNextFreeRecycledColorLayer;
    // Clear clip rect so we don't accidentally stay clipped. We will
    // reapply any necessary clipping.
    layer->SetClipRect(nsnull);
  } else {
    // Create a new layer
    layer = mManager->CreateColorLayer();
    if (!layer)
      return nsnull;
    // Mark this layer as being used for Thebes-painting display items
    layer->SetUserData(&gColorLayerUserData, nsnull);
  }
  return layer.forget();
}

already_AddRefed<ImageLayer>
ContainerState::CreateOrRecycleImageLayer()
{
  nsRefPtr<ImageLayer> layer;
  if (mNextFreeRecycledImageLayer < mRecycledImageLayers.Length()) {
    // Recycle a layer
    layer = mRecycledImageLayers[mNextFreeRecycledImageLayer];
    ++mNextFreeRecycledImageLayer;
    // Clear clip rect so we don't accidentally stay clipped. We will
    // reapply any necessary clipping.
    layer->SetClipRect(nsnull);
  } else {
    // Create a new layer
    layer = mManager->CreateImageLayer();
    if (!layer)
      return nsnull;
    // Mark this layer as being used for Thebes-painting display items
    layer->SetUserData(&gImageLayerUserData, nsnull);
  }
  return layer.forget();
}

already_AddRefed<ThebesLayer>
ContainerState::CreateOrRecycleThebesLayer(nsIFrame* aActiveScrolledRoot)
{
  // We need a new thebes layer
  nsRefPtr<ThebesLayer> layer;
  if (mNextFreeRecycledThebesLayer < mRecycledThebesLayers.Length()) {
    // Recycle a layer
    layer = mRecycledThebesLayers[mNextFreeRecycledThebesLayer];
    ++mNextFreeRecycledThebesLayer;
    // Clear clip rect so we don't accidentally stay clipped. We will
    // reapply any necessary clipping.
    layer->SetClipRect(nsnull);

    // This gets called on recycled ThebesLayers that are going to be in the
    // final layer tree, so it's a convenient time to invalidate the
    // content that changed where we don't know what ThebesLayer it belonged
    // to, or if we need to invalidate the entire layer, we can do that.
    // This needs to be done before we update the ThebesLayer to its new
    // transform. See nsGfxScrollFrame::InvalidateInternal, where
    // we ensure that mInvalidThebesContent is updated according to the
    // scroll position as of the most recent paint.
    if (mInvalidateAllThebesContent) {
      nsIntRect invalidate = layer->GetValidRegion().GetBounds();
      layer->InvalidateRegion(invalidate);
    } else {
      InvalidatePostTransformRegion(layer, mInvalidThebesContent);
    }
    // We do not need to Invalidate these areas in the widget because we
    // assume the caller of InvalidateThebesLayerContents has ensured
    // the area is invalidated in the widget.
  } else {
    // Create a new thebes layer
    layer = mManager->CreateThebesLayer();
    if (!layer)
      return nsnull;
    // Mark this layer as being used for Thebes-painting display items
    layer->SetUserData(&gThebesDisplayItemLayerUserData,
        new ThebesDisplayItemLayerUserData());
  }

  // Set up transform so that 0,0 in the Thebes layer corresponds to the
  // (pixel-snapped) top-left of the aActiveScrolledRoot.
  nsPoint offset = mBuilder->ToReferenceFrame(aActiveScrolledRoot);
  nsIntPoint pixOffset = offset.ToNearestPixels(
      aActiveScrolledRoot->PresContext()->AppUnitsPerDevPixel());
  gfxMatrix matrix;
  matrix.Translate(gfxPoint(pixOffset.x, pixOffset.y));
  layer->SetTransform(gfx3DMatrix::From2D(matrix));

  return layer.forget();
}

/**
 * Returns the appunits per dev pixel for the item's frame. The item must
 * have a frame because only nsDisplayClip items don't have a frame,
 * and those items are flattened away by ProcessDisplayItems.
 */
static PRInt32
AppUnitsPerDevPixel(nsDisplayItem* aItem)
{
  // The underlying frame for zoom items is the root frame of the subdocument.
  // But zoom display items report their bounds etc using the parent document's
  // APD because zoom items act as a conversion layer between the two different
  // APDs.
  if (aItem->GetType() == nsDisplayItem::TYPE_ZOOM) {
    return static_cast<nsDisplayZoom*>(aItem)->GetParentAppUnitsPerDevPixel();
  }
  return aItem->GetUnderlyingFrame()->PresContext()->AppUnitsPerDevPixel();
}

/**
 * Restrict the visible region of aLayer to the region that is actually visible.
 * Because we only reduce the visible region here, we don't need to worry
 * about whether CONTENT_OPAQUE is set; if layer was opauqe in the old
 * visible region, it will still be opaque in the new one.
 * @param aItemVisible the visible region of the display item (that is,
 * after any layer transform has been applied)
 */
static void
RestrictVisibleRegionForLayer(Layer* aLayer, const nsIntRect& aItemVisible)
{
  gfxMatrix transform;
  if (!aLayer->GetTransform().Is2D(&transform))
    return;

  // if 'transform' is not invertible, then nothing will be displayed
  // for the layer, so it doesn't really matter what we do here
  gfxMatrix inverse = transform;
  inverse.Invert();
  gfxRect itemVisible(aItemVisible.x, aItemVisible.y, aItemVisible.width, aItemVisible.height);
  gfxRect layerVisible = inverse.TransformBounds(itemVisible);
  layerVisible.RoundOut();

  nsIntRect visibleRect;
  if (!gfxUtils::GfxRectToIntRect(layerVisible, &visibleRect))
    return;

  nsIntRegion rgn = aLayer->GetVisibleRegion();
  if (!visibleRect.Contains(rgn.GetBounds())) {
    rgn.And(rgn, visibleRect);
    aLayer->SetVisibleRegion(rgn);
  }
}

nscolor
ContainerState::FindOpaqueBackgroundColorFor(PRInt32 aThebesLayerIndex)
{
  ThebesLayerData* target = mThebesLayerDataStack[aThebesLayerIndex];
  for (PRInt32 i = aThebesLayerIndex - 1; i >= 0; --i) {
    ThebesLayerData* candidate = mThebesLayerDataStack[i];
    nsIntRegion visibleAboveIntersection;
    visibleAboveIntersection.And(candidate->mVisibleAboveRegion, target->mVisibleRegion);
    if (!visibleAboveIntersection.IsEmpty()) {
      // Some non-Thebes content between target and candidate; this is
      // hopeless
      break;
    }

    nsIntRegion intersection;
    intersection.And(candidate->mVisibleRegion, target->mVisibleRegion);
    if (intersection.IsEmpty()) {
      // The layer doesn't intersect our target, ignore it and move on
      continue;
    }
 
    // The candidate intersects our target. If any layer has a solid-color
    // area behind our target, this must be it. Scan its display items.
    nsPresContext* presContext = mContainerFrame->PresContext();
    nscoord appUnitsPerDevPixel = presContext->AppUnitsPerDevPixel();
    nsRect rect =
      target->mVisibleRegion.GetBounds().ToAppUnits(appUnitsPerDevPixel);
    return mBuilder->LayerBuilder()->
      FindOpaqueColorCovering(mBuilder, candidate->mLayer, rect);
  }
  return NS_RGBA(0,0,0,0);
}

nsRefPtr<ImageContainer>
ContainerState::ThebesLayerData::CanOptimizeImageLayer(LayerManager* aManager)
{
  if (!mImage || !mImageClip.mRoundedClipRects.IsEmpty()) {
    return nsnull;
  }

  return mImage->GetContainer(aManager);
}

void
ContainerState::PopThebesLayerData()
{
  NS_ASSERTION(!mThebesLayerDataStack.IsEmpty(), "Can't pop");

  PRInt32 lastIndex = mThebesLayerDataStack.Length() - 1;
  ThebesLayerData* data = mThebesLayerDataStack[lastIndex];

  nsRefPtr<Layer> layer;
  nsRefPtr<ImageContainer> imageContainer = data->CanOptimizeImageLayer(mManager); 

  if (data->mIsSolidColorInVisibleRegion || imageContainer) {
    NS_ASSERTION(!(data->mIsSolidColorInVisibleRegion && imageContainer),
                 "Can't be a solid color as well as an image!");
    if (imageContainer) {
      nsRefPtr<ImageLayer> imageLayer = CreateOrRecycleImageLayer();
      imageLayer->SetContainer(imageContainer);
      data->mImage->ConfigureLayer(imageLayer);
      NS_ASSERTION(data->mImageClip.mRoundedClipRects.IsEmpty(),
                   "How did we get rounded clip rects here?");
      if (data->mImageClip.mHaveClipRect) {
        nsPresContext* presContext = mContainerFrame->PresContext();
        nscoord appUnitsPerDevPixel = presContext->AppUnitsPerDevPixel();
        nsIntRect clip = data->mImageClip.mClipRect.ToNearestPixels(appUnitsPerDevPixel);
        imageLayer->IntersectClipRect(
          data->mImageClip.mClipRect.ToNearestPixels(appUnitsPerDevPixel));
      }
      layer = imageLayer;
    } else {
      nsRefPtr<ColorLayer> colorLayer = CreateOrRecycleColorLayer();
      colorLayer->SetIsFixedPosition(data->mLayer->GetIsFixedPosition());
      colorLayer->SetColor(data->mSolidColor);

      // Copy transform
      colorLayer->SetTransform(data->mLayer->GetTransform());
      
      // Clip colorLayer to its visible region, since ColorLayers are
      // allowed to paint outside the visible region. Here we rely on the
      // fact that uniform display items fill rectangles; obviously the
      // area to fill must contain the visible region, and because it's
      // a rectangle, it must therefore contain the visible region's GetBounds.
      // Note that the visible region is already clipped appropriately.
      nsIntRect visibleRect = data->mVisibleRegion.GetBounds();
      colorLayer->SetClipRect(&visibleRect);

      layer = colorLayer;
    }

    NS_ASSERTION(!mNewChildLayers.Contains(layer), "Layer already in list???");
    AutoLayersArray::index_type index = mNewChildLayers.IndexOf(data->mLayer);
    NS_ASSERTION(index != AutoLayersArray::NoIndex, "Thebes layer not found?");
    mNewChildLayers.InsertElementAt(index + 1, layer);

    // Hide the ThebesLayer. We leave it in the layer tree so that we
    // can find and recycle it later.
    data->mLayer->IntersectClipRect(nsIntRect());
    data->mLayer->SetVisibleRegion(nsIntRegion());
  } else {
    layer = data->mLayer;
  }

  gfxMatrix transform;
  if (!layer->GetTransform().Is2D(&transform)) {
    NS_ERROR("Only 2D transformations currently supported");
  }
  
  //ImageLayers are already configured with a visible region
  if (!imageContainer) {
    NS_ASSERTION(!transform.HasNonIntegerTranslation(),
                 "Matrix not just an integer translation?");
    // Convert from relative to the container to relative to the
    // ThebesLayer itself.
    nsIntRegion rgn = data->mVisibleRegion;
    rgn.MoveBy(-nsIntPoint(PRInt32(transform.x0), PRInt32(transform.y0)));
    layer->SetVisibleRegion(rgn);
  }

  nsIntRegion transparentRegion;
  transparentRegion.Sub(data->mVisibleRegion, data->mOpaqueRegion);
  PRBool isOpaque = transparentRegion.IsEmpty();
  // For translucent ThebesLayers, try to find an opaque background
  // color that covers the entire area beneath it so we can pull that
  // color into this layer to make it opaque.
  if (layer == data->mLayer) {
    nscolor backgroundColor = NS_RGBA(0,0,0,0);
    if (!isOpaque) {
      backgroundColor = FindOpaqueBackgroundColorFor(lastIndex);
      if (NS_GET_A(backgroundColor) == 255) {
        isOpaque = PR_TRUE;
      }
    }

    // Store the background color
    ThebesDisplayItemLayerUserData* userData =
      static_cast<ThebesDisplayItemLayerUserData*>
        (data->mLayer->GetUserData(&gThebesDisplayItemLayerUserData));
    NS_ASSERTION(userData, "where did our user data go?");
    if (userData->mForcedBackgroundColor != backgroundColor) {
      // Invalidate the entire target ThebesLayer since we're changing
      // the background color
      data->mLayer->InvalidateRegion(data->mLayer->GetValidRegion());
    }
    userData->mForcedBackgroundColor = backgroundColor;
  }
  PRUint32 flags;
  if (isOpaque && !data->mForceTransparentSurface) {
    flags = Layer::CONTENT_OPAQUE;
  } else if (data->mNeedComponentAlpha) {
    flags = Layer::CONTENT_COMPONENT_ALPHA;
  } else {
    flags = 0;
  }
  layer->SetContentFlags(flags);

  if (lastIndex > 0) {
    // Since we're going to pop off the last ThebesLayerData, the
    // mVisibleAboveRegion of the second-to-last item will need to include
    // the regions of the last item.
    ThebesLayerData* nextData = mThebesLayerDataStack[lastIndex - 1];
    nextData->mVisibleAboveRegion.Or(nextData->mVisibleAboveRegion,
                                     data->mVisibleAboveRegion);
    nextData->mVisibleAboveRegion.Or(nextData->mVisibleAboveRegion,
                                     data->mVisibleRegion);
    nextData->mDrawAboveRegion.Or(nextData->mDrawAboveRegion,
                                     data->mDrawAboveRegion);
    nextData->mDrawAboveRegion.Or(nextData->mDrawAboveRegion,
                                     data->mDrawRegion);
  }

  mThebesLayerDataStack.RemoveElementAt(lastIndex);
}

static PRBool
SuppressComponentAlpha(nsDisplayListBuilder* aBuilder,
                       nsDisplayItem* aItem,
                       const nsRect& aComponentAlphaBounds)
{
  const nsRegion* windowTransparentRegion = aBuilder->GetFinalTransparentRegion();
  if (!windowTransparentRegion || windowTransparentRegion->IsEmpty())
    return PR_FALSE;

  // Suppress component alpha for items in the toplevel window that are over
  // the window translucent area
  nsIFrame* f = aItem->GetUnderlyingFrame();
  nsIFrame* ref = aBuilder->ReferenceFrame();
  if (f->PresContext() != ref->PresContext())
    return PR_FALSE;

  for (nsIFrame* t = f; t; t = t->GetParent()) {
    if (t->IsTransformed())
      return PR_FALSE;
  }

  return windowTransparentRegion->Intersects(aComponentAlphaBounds);
}

static PRBool
WindowHasTransparency(nsDisplayListBuilder* aBuilder)
{
  const nsRegion* windowTransparentRegion = aBuilder->GetFinalTransparentRegion();
  return windowTransparentRegion && !windowTransparentRegion->IsEmpty();
}

void
ContainerState::ThebesLayerData::Accumulate(nsDisplayListBuilder* aBuilder,
                                            nsDisplayItem* aItem,
                                            const nsIntRect& aVisibleRect,
                                            const nsIntRect& aDrawRect,
                                            const FrameLayerBuilder::Clip& aClip)
{
  nscolor uniformColor;
  PRBool isUniform = aItem->IsUniform(aBuilder, &uniformColor);
  // Some display items have to exist (so they can set forceTransparentSurface
  // below) but don't draw anything. They'll return true for isUniform but
  // a color with opacity 0.
  if (!isUniform || NS_GET_A(uniformColor) > 0) {
    if (isUniform &&
        aItem->GetBounds(aBuilder).ToInsidePixels(AppUnitsPerDevPixel(aItem)).Contains(aVisibleRect)) {
      if (mVisibleRegion.IsEmpty()) {
        // This color is all we have
        mSolidColor = uniformColor;
        mIsSolidColorInVisibleRegion = PR_TRUE;
      } else if (mIsSolidColorInVisibleRegion &&
                 mVisibleRegion.IsEqual(nsIntRegion(aVisibleRect))) {
        // we can just blend the colors together
        mSolidColor = NS_ComposeColors(mSolidColor, uniformColor);
      } else {
        mIsSolidColorInVisibleRegion = PR_FALSE;
      }
    } else {
      mIsSolidColorInVisibleRegion = PR_FALSE;
    }

    mVisibleRegion.Or(mVisibleRegion, aVisibleRect);
    mVisibleRegion.SimplifyOutward(4);
    mDrawRegion.Or(mDrawRegion, aDrawRect);
    mDrawRegion.SimplifyOutward(4);
  }

  /* Mark as available for conversion to image layer if this is a nsDisplayImage and
   * we are the first visible item in the ThebesLayerData object.
   */
  if (aItem->GetType() == nsDisplayItem::TYPE_IMAGE && mVisibleRegion.IsEmpty()) {
    mImage = static_cast<nsDisplayImage*>(aItem);
    mImageClip = aClip;
  } else {
    mImage = nsnull;
  }
  
  PRBool forceTransparentSurface = PR_FALSE;
  nsRegion opaque = aItem->GetOpaqueRegion(aBuilder, &forceTransparentSurface);
  if (!opaque.IsEmpty()) {
    nsRegionRectIterator iter(opaque);
    nscoord appUnitsPerDevPixel = AppUnitsPerDevPixel(aItem);
    for (const nsRect* r = iter.Next(); r; r = iter.Next()) {
      // We don't use SimplifyInward here since it's not defined exactly
      // what it will discard. For our purposes the most important case
      // is a large opaque background at the bottom of z-order (e.g.,
      // a canvas background), so we need to make sure that the first rect
      // we see doesn't get discarded.
      nsIntRect rect = aClip.ApproximateIntersect(*r).ToInsidePixels(appUnitsPerDevPixel);
      nsIntRegion tmp;
      tmp.Or(mOpaqueRegion, rect);
       // Opaque display items in chrome documents whose window is partially
       // transparent are always added to the opaque region. This helps ensure
       // that we get as much subpixel-AA as possible in the chrome.
       if (tmp.GetNumRects() <= 4 ||
           (WindowHasTransparency(aBuilder) &&
            aItem->GetUnderlyingFrame()->PresContext()->IsChrome())) {
        mOpaqueRegion = tmp;
      }
    }
  }
  nsRect componentAlpha = aItem->GetComponentAlphaBounds(aBuilder);
  componentAlpha.IntersectRect(componentAlpha, aItem->GetVisibleRect());
  if (!componentAlpha.IsEmpty()) {
    nscoord appUnitsPerDevPixel = AppUnitsPerDevPixel(aItem);
    if (!mOpaqueRegion.Contains(componentAlpha.ToOutsidePixels(appUnitsPerDevPixel))) {
      if (SuppressComponentAlpha(aBuilder, aItem, componentAlpha)) {
        aItem->DisableComponentAlpha();
      } else {
        mNeedComponentAlpha = PR_TRUE;
      }
    }
  }
  mForceTransparentSurface = mForceTransparentSurface || forceTransparentSurface;
}

already_AddRefed<ThebesLayer>
ContainerState::FindThebesLayerFor(nsDisplayItem* aItem,
                                   const nsIntRect& aVisibleRect,
                                   const nsIntRect& aDrawRect,
                                   const FrameLayerBuilder::Clip& aClip,
                                   nsIFrame* aActiveScrolledRoot)
{
  PRInt32 i;
  PRInt32 lowestUsableLayerWithScrolledRoot = -1;
  PRInt32 topmostLayerWithScrolledRoot = -1;
  for (i = mThebesLayerDataStack.Length() - 1; i >= 0; --i) {
    ThebesLayerData* data = mThebesLayerDataStack[i];
    if (data->mDrawAboveRegion.Intersects(aVisibleRect)) {
      ++i;
      break;
    }
    if (data->mActiveScrolledRoot == aActiveScrolledRoot) {
      lowestUsableLayerWithScrolledRoot = i;
      if (topmostLayerWithScrolledRoot < 0) {
        topmostLayerWithScrolledRoot = i;
      }
    }
    if (data->mDrawRegion.Intersects(aVisibleRect))
      break;
  }
  if (topmostLayerWithScrolledRoot < 0) {
    --i;
    for (; i >= 0; --i) {
      ThebesLayerData* data = mThebesLayerDataStack[i];
      if (data->mActiveScrolledRoot == aActiveScrolledRoot) {
        topmostLayerWithScrolledRoot = i;
        break;
      }
    }
  }

  if (topmostLayerWithScrolledRoot >= 0) {
    while (PRUint32(topmostLayerWithScrolledRoot + 1) < mThebesLayerDataStack.Length()) {
      PopThebesLayerData();
    }
  }

  nsRefPtr<ThebesLayer> layer;
  ThebesLayerData* thebesLayerData = nsnull;
  if (lowestUsableLayerWithScrolledRoot < 0) {
    layer = CreateOrRecycleThebesLayer(aActiveScrolledRoot);

    NS_ASSERTION(!mNewChildLayers.Contains(layer), "Layer already in list???");
    mNewChildLayers.AppendElement(layer);

    thebesLayerData = new ThebesLayerData();
    mThebesLayerDataStack.AppendElement(thebesLayerData);
    thebesLayerData->mLayer = layer;
    thebesLayerData->mActiveScrolledRoot = aActiveScrolledRoot;
  } else {
    thebesLayerData = mThebesLayerDataStack[lowestUsableLayerWithScrolledRoot];
    layer = thebesLayerData->mLayer;
  }

  thebesLayerData->Accumulate(mBuilder, aItem, aVisibleRect, aDrawRect, aClip);
  return layer.forget();
}

static void
PaintInactiveLayer(nsDisplayListBuilder* aBuilder,
                   nsDisplayItem* aItem,
                   gfxContext* aContext)
{
  // This item has an inactive layer. Render it to a ThebesLayer
  // using a temporary BasicLayerManager.
  nsRefPtr<BasicLayerManager> tempManager = new BasicLayerManager();
  tempManager->BeginTransactionWithTarget(aContext);
  nsRefPtr<Layer> layer = aItem->BuildLayer(aBuilder, tempManager);
  if (!layer) {
    tempManager->EndTransaction(nsnull, nsnull);
    return;
  }
  PRInt32 appUnitsPerDevPixel = AppUnitsPerDevPixel(aItem);
  nsIntRect itemVisibleRect =
    aItem->GetVisibleRect().ToOutsidePixels(appUnitsPerDevPixel);
  RestrictVisibleRegionForLayer(layer, itemVisibleRect);

  tempManager->SetRoot(layer);
  aBuilder->LayerBuilder()->WillEndTransaction(tempManager);
  tempManager->EndTransaction(FrameLayerBuilder::DrawThebesLayer, aBuilder);
  aBuilder->LayerBuilder()->DidEndTransaction(tempManager);
}

/*
 * Iterate through the non-clip items in aList and its descendants.
 * For each item we compute the effective clip rect. Each item is assigned
 * to a layer. We invalidate the areas in ThebesLayers where an item
 * has moved from one ThebesLayer to another. Also,
 * aState->mInvalidThebesContent is invalidated in every ThebesLayer.
 * We set the clip rect for items that generated their own layer.
 * (ThebesLayers don't need a clip rect on the layer, we clip the items
 * individually when we draw them.)
 * If we have to clip to a rounded rect, we treat any active layer as
 * though it's inactive so that we draw it ourselves into the thebes layer.
 * We set the visible rect for all layers, although the actual setting
 * of visible rects for some ThebesLayers is deferred until the calling
 * of ContainerState::Finish.
 */
void
ContainerState::ProcessDisplayItems(const nsDisplayList& aList,
                                    FrameLayerBuilder::Clip& aClip)
{
  PRInt32 appUnitsPerDevPixel =
    mContainerFrame->PresContext()->AppUnitsPerDevPixel();

  for (nsDisplayItem* item = aList.GetBottom(); item; item = item->GetAbove()) {
    nsDisplayItem::Type type = item->GetType();
    if (type == nsDisplayItem::TYPE_CLIP ||
        type == nsDisplayItem::TYPE_CLIP_ROUNDED_RECT) {
      FrameLayerBuilder::Clip childClip(aClip, item);
      ProcessDisplayItems(*item->GetList(), childClip);
      continue;
    }

    NS_ASSERTION(appUnitsPerDevPixel == AppUnitsPerDevPixel(item),
      "items in a container layer should all have the same app units per dev pixel");

    nsIntRect itemVisibleRect =
      item->GetVisibleRect().ToOutsidePixels(appUnitsPerDevPixel);
    nsRect itemContent = item->GetBounds(mBuilder);
    if (aClip.mHaveClipRect) {
      itemContent.IntersectRect(aClip.mClipRect, itemContent);
    }
    mBounds.UnionRect(mBounds, itemContent);
    nsIntRect itemDrawRect = itemContent.ToOutsidePixels(appUnitsPerDevPixel);
    LayerState layerState = item->GetLayerState(mBuilder, mManager);

    nsIFrame* activeScrolledRoot =
      nsLayoutUtils::GetActiveScrolledRootFor(item, mBuilder);

    // Assign the item to a layer
    if (layerState == LAYER_ACTIVE_FORCE ||
        layerState == LAYER_ACTIVE_EMPTY ||
        layerState == LAYER_ACTIVE && (aClip.mRoundedClipRects.IsEmpty() ||
        // We can use the visible rect here only because the item has its own
        // layer, like the comment below.
        !aClip.IsRectClippedByRoundedCorner(item->GetVisibleRect()))) {

      // LAYER_ACTIVE_EMPTY means the layer is created just for its metadata.
      // We should never see an empty layer with any visible content!
      NS_ASSERTION(layerState != LAYER_ACTIVE_EMPTY ||
                   itemVisibleRect.IsEmpty(),
                   "State is LAYER_ACTIVE_EMPTY but visible rect is not.");

      // If the item would have its own layer but is invisible, just hide it.
      // Note that items without their own layers can't be skipped this
      // way, since their ThebesLayer may decide it wants to draw them
      // into its buffer even if they're currently covered.
      if (itemVisibleRect.IsEmpty() && layerState != LAYER_ACTIVE_EMPTY) {
        InvalidateForLayerChange(item, nsnull);
        continue;
      }

      aClip.RemoveRoundedCorners();

      // Just use its layer.
      nsRefPtr<Layer> ownLayer = item->BuildLayer(mBuilder, mManager);
      if (!ownLayer) {
        InvalidateForLayerChange(item, ownLayer);
        continue;
      }

      ownLayer->SetIsFixedPosition(!nsLayoutUtils::ScrolledByViewportScrolling(
                                      activeScrolledRoot, mBuilder));

      // Update that layer's clip and visible rects.
      NS_ASSERTION(ownLayer->Manager() == mManager, "Wrong manager");
      NS_ASSERTION(!ownLayer->HasUserData(&gLayerManagerUserData),
                   "We shouldn't have a FrameLayerBuilder-managed layer here!");
      // It has its own layer. Update that layer's clip and visible rects.
      if (aClip.mHaveClipRect) {
        ownLayer->IntersectClipRect(
            aClip.mClipRect.ToNearestPixels(appUnitsPerDevPixel));
      }
      ThebesLayerData* data = GetTopThebesLayerData();
      if (data) {
        data->mVisibleAboveRegion.Or(data->mVisibleAboveRegion, itemVisibleRect);
        // Add the entire bounds rect to the mDrawAboveRegion.
        // The visible region may be excluding opaque content above the
        // item, and we need to ensure that that content is not placed
        // in a ThebesLayer below the item!
        data->mDrawAboveRegion.Or(data->mDrawAboveRegion, itemDrawRect);
      }
      RestrictVisibleRegionForLayer(ownLayer, itemVisibleRect);
      ContainerLayer* oldContainer = ownLayer->GetParent();
      if (oldContainer && oldContainer != mContainerLayer) {
        oldContainer->RemoveChild(ownLayer);
      }
      NS_ASSERTION(!mNewChildLayers.Contains(ownLayer),
                   "Layer already in list???");

      InvalidateForLayerChange(item, ownLayer);

      mNewChildLayers.AppendElement(ownLayer);
      mBuilder->LayerBuilder()->AddLayerDisplayItem(ownLayer, item, layerState);
    } else {
      nsRefPtr<ThebesLayer> thebesLayer =
        FindThebesLayerFor(item, itemVisibleRect, itemDrawRect, aClip,
                           activeScrolledRoot);

      thebesLayer->SetIsFixedPosition(!nsLayoutUtils::ScrolledByViewportScrolling(
                                         activeScrolledRoot, mBuilder));

      InvalidateForLayerChange(item, thebesLayer);

      mBuilder->LayerBuilder()->
        AddThebesDisplayItem(thebesLayer, item, aClip, mContainerFrame,
                             layerState);
    }
  }
}

void
ContainerState::InvalidateForLayerChange(nsDisplayItem* aItem, Layer* aNewLayer)
{
  nsIFrame* f = aItem->GetUnderlyingFrame();
  NS_ASSERTION(f, "Display items that render using Thebes must have a frame");
  PRUint32 key = aItem->GetPerFrameKey();
  NS_ASSERTION(key, "Display items that render using Thebes must have a key");
  Layer* oldLayer = mBuilder->LayerBuilder()->GetOldLayerFor(f, key);
  if (!oldLayer) {
    // Nothing to do here, this item didn't have a layer before
    return;
  }
  if (aNewLayer != oldLayer) {
    // The item has changed layers.
    // Invalidate the bounds in the old layer and new layer.
    // The bounds might have changed, but we assume that any difference
    // in the bounds will have been invalidated for all Thebes layers
    // in the container via regular frame invalidation.
    nsRect bounds = aItem->GetBounds(mBuilder);
    PRInt32 appUnitsPerDevPixel = AppUnitsPerDevPixel(aItem);
    nsIntRect r = bounds.ToOutsidePixels(appUnitsPerDevPixel);

    ThebesLayer* t = oldLayer->AsThebesLayer();
    if (t) {
      InvalidatePostTransformRegion(t, r);
    }
    if (aNewLayer) {
      ThebesLayer* newLayer = aNewLayer->AsThebesLayer();
      if (newLayer) {
        InvalidatePostTransformRegion(newLayer, r);
      }
    }

    NS_ASSERTION(appUnitsPerDevPixel ==
                   mContainerFrame->PresContext()->AppUnitsPerDevPixel(),
                 "app units per dev pixel should be constant in a container");
    mContainerFrame->InvalidateWithFlags(
        bounds - mBuilder->ToReferenceFrame(mContainerFrame),
        nsIFrame::INVALIDATE_NO_THEBES_LAYERS |
        nsIFrame::INVALIDATE_EXCLUDE_CURRENT_PAINT);
  }
}

PRBool
FrameLayerBuilder::NeedToInvalidateFixedDisplayItem(nsDisplayListBuilder* aBuilder,
                                                    nsDisplayItem* aItem)
{
  return !aItem->ShouldFixToViewport(aBuilder) ||
      !HasRetainedLayerFor(aItem->GetUnderlyingFrame(), aItem->GetPerFrameKey());
}

void
FrameLayerBuilder::AddThebesDisplayItem(ThebesLayer* aLayer,
                                        nsDisplayItem* aItem,
                                        const Clip& aClip,
                                        nsIFrame* aContainerLayerFrame,
                                        LayerState aLayerState)
{
  AddLayerDisplayItem(aLayer, aItem);

  ThebesLayerItemsEntry* entry = mThebesLayerItems.PutEntry(aLayer);
  if (entry) {
    entry->mContainerLayerFrame = aContainerLayerFrame;
    NS_ASSERTION(aItem->GetUnderlyingFrame(), "Must have frame");
    ClippedDisplayItem* cdi =
      entry->mItems.AppendElement(ClippedDisplayItem(aItem, aClip));
    cdi->mInactiveLayer = aLayerState != LAYER_NONE;
  }
}

void
FrameLayerBuilder::AddLayerDisplayItem(Layer* aLayer,
                                       nsDisplayItem* aItem,
                                       LayerState aLayerState)
{
  if (aLayer->Manager() != mRetainingManager)
    return;

  nsIFrame* f = aItem->GetUnderlyingFrame();
  DisplayItemDataEntry* entry = mNewDisplayItemData.PutEntry(f);
  if (entry) {
    entry->mData.AppendElement(
      DisplayItemData(aLayer, aItem->GetPerFrameKey(), aLayerState));
  }
}

nscolor
FrameLayerBuilder::FindOpaqueColorCovering(nsDisplayListBuilder* aBuilder,
                                           ThebesLayer* aLayer,
                                           const nsRect& aRect)
{
  ThebesLayerItemsEntry* entry = mThebesLayerItems.GetEntry(aLayer);
  NS_ASSERTION(entry, "Must know about this layer!");
  for (PRInt32 i = entry->mItems.Length() - 1; i >= 0; --i) {
    nsDisplayItem* item = entry->mItems[i].mItem;
    const nsRect& visible = item->GetVisibleRect();
    if (!visible.Intersects(aRect))
      continue;

    nscolor color;
    if (visible.Contains(aRect) && item->IsUniform(aBuilder, &color) &&
        NS_GET_A(color) == 255)
      return color;
    break;
  }
  return NS_RGBA(0,0,0,0);
}

void
ContainerState::CollectOldLayers()
{
  for (Layer* layer = mContainerLayer->GetFirstChild(); layer;
       layer = layer->GetNextSibling()) {
    if (layer->HasUserData(&gColorLayerUserData)) {
      mRecycledColorLayers.AppendElement(static_cast<ColorLayer*>(layer));
    } else if (layer->HasUserData(&gImageLayerUserData)) {
      mRecycledImageLayers.AppendElement(static_cast<ImageLayer*>(layer));
    } else if (layer->HasUserData(&gThebesDisplayItemLayerUserData)) {
      NS_ASSERTION(layer->AsThebesLayer(), "Wrong layer type");
      mRecycledThebesLayers.AppendElement(static_cast<ThebesLayer*>(layer));
    }
  }
}

void
ContainerState::Finish(PRUint32* aTextContentFlags)
{
  while (!mThebesLayerDataStack.IsEmpty()) {
    PopThebesLayerData();
  }

  PRUint32 textContentFlags = 0;

  for (PRUint32 i = 0; i <= mNewChildLayers.Length(); ++i) {
    // An invariant of this loop is that the layers in mNewChildLayers
    // with index < i are the first i child layers of mContainerLayer.
    Layer* layer;
    if (i < mNewChildLayers.Length()) {
      layer = mNewChildLayers[i];
      if (!layer->GetVisibleRegion().IsEmpty()) {
        textContentFlags |= layer->GetContentFlags() & Layer::CONTENT_COMPONENT_ALPHA;
      }
      if (!layer->GetParent()) {
        // This is not currently a child of the container, so just add it
        // now.
        Layer* prevChild = i == 0 ? nsnull : mNewChildLayers[i - 1].get();
        mContainerLayer->InsertAfter(layer, prevChild);
        continue;
      }
      NS_ASSERTION(layer->GetParent() == mContainerLayer,
                   "Layer shouldn't be the child of some other container");
    } else {
      layer = nsnull;
    }

    // If layer is non-null, then it's already a child of the container,
    // so scan forward until we find it, removing the other layers we
    // don't want here.
    // If it's null, scan forward until we've removed all the leftover
    // children.
    Layer* nextOldChild = i == 0 ? mContainerLayer->GetFirstChild() :
      mNewChildLayers[i - 1]->GetNextSibling();
    while (nextOldChild != layer) {
      Layer* tmp = nextOldChild;
      nextOldChild = nextOldChild->GetNextSibling();
      mContainerLayer->RemoveChild(tmp);
    }
    // If non-null, 'layer' is now in the right place in the list, so we
    // can just move on to the next one.
  }

  *aTextContentFlags = textContentFlags;
}

already_AddRefed<ContainerLayer>
FrameLayerBuilder::BuildContainerLayerFor(nsDisplayListBuilder* aBuilder,
                                          LayerManager* aManager,
                                          nsIFrame* aContainerFrame,
                                          nsDisplayItem* aContainerItem,
                                          const nsDisplayList& aChildren)
{
  FrameProperties props = aContainerFrame->Properties();
  PRUint32 containerDisplayItemKey =
    aContainerItem ? aContainerItem->GetPerFrameKey() : 0;
  NS_ASSERTION(aContainerFrame, "Container display items here should have a frame");
  NS_ASSERTION(!aContainerItem ||
               aContainerItem->GetUnderlyingFrame() == aContainerFrame,
               "Container display item must match given frame");

  nsRefPtr<ContainerLayer> containerLayer;
  if (aManager == mRetainingManager) {
    Layer* oldLayer = GetOldLayerFor(aContainerFrame, containerDisplayItemKey);
    if (oldLayer) {
      NS_ASSERTION(oldLayer->Manager() == aManager, "Wrong manager");
      if (oldLayer->HasUserData(&gThebesDisplayItemLayerUserData)) {
        // The old layer for this item is actually our ThebesLayer
        // because we rendered its layer into that ThebesLayer. So we
        // don't actually have a retained container layer.
      } else {
        NS_ASSERTION(oldLayer->GetType() == Layer::TYPE_CONTAINER,
                     "Wrong layer type");
        containerLayer = static_cast<ContainerLayer*>(oldLayer);
        // Clear clip rect; the caller will set it if necessary.
        containerLayer->SetClipRect(nsnull);
      }
    }
  }
  if (!containerLayer) {
    // No suitable existing layer was found.
    containerLayer = aManager->CreateContainerLayer();
    if (!containerLayer)
      return nsnull;
  }

  if (aContainerItem &&
      aContainerItem->GetLayerState(aBuilder, aManager) == LAYER_ACTIVE_EMPTY) {
    // Empty layers only have metadata and should never have display items. We
    // early exit because later, invalidation will walk up the frame tree to
    // determine which thebes layer gets invalidated. Since an empty layer
    // should never have anything to paint, it should never be invalidated.
    NS_ASSERTION(aChildren.IsEmpty(), "Should have no children");
    return containerLayer.forget();
  }

  ContainerState state(aBuilder, aManager, aContainerFrame, containerLayer);
  nscoord appUnitsPerDevPixel = aContainerFrame->PresContext()->AppUnitsPerDevPixel();

  if (aManager == mRetainingManager) {
    DisplayItemDataEntry* entry = mNewDisplayItemData.PutEntry(aContainerFrame);
    if (entry) {
      entry->mData.AppendElement(
          DisplayItemData(containerLayer, containerDisplayItemKey, LAYER_ACTIVE));
    }

    nsPoint* offsetAtLastPaint = static_cast<nsPoint*>
      (props.Get(ThebesLayerLastPaintOffsetProperty()));
    nsPoint currentOffset = aBuilder->ToReferenceFrame(aContainerFrame);

    nsRegion* invalidThebesContent(static_cast<nsRegion*>
      (props.Get(ThebesLayerInvalidRegionProperty())));
    if (invalidThebesContent) {
      nsPoint offset = offsetAtLastPaint ? *offsetAtLastPaint : currentOffset;
      invalidThebesContent->MoveBy(offset);
      state.SetInvalidThebesContent(invalidThebesContent->
        ToOutsidePixels(appUnitsPerDevPixel));
      // We have to preserve the current contents of invalidThebesContent
      // because there might be multiple container layers for the same
      // frame and we need to invalidate the ThebesLayer children of all
      // of them.
      invalidThebesContent->MoveBy(-offset);
    } else {
      // The region was deleted to indicate that everything should be
      // invalidated.
      state.SetInvalidateAllThebesContent();
    }
    SetHasContainerLayer(aContainerFrame, currentOffset);
  }

  Clip clip;
  state.ProcessDisplayItems(aChildren, clip);

  // Set CONTENT_COMPONENT_ALPHA if any of our children have it.
  // This is suboptimal ... a child could have text that's over transparent
  // pixels in its own layer, but over opaque parts of previous siblings.
  PRUint32 flags;
  state.Finish(&flags);

  nsRect bounds = state.GetChildrenBounds();
  NS_ASSERTION(bounds == aChildren.GetBounds(aBuilder), "Wrong bounds");
  nsIntRect pixBounds = bounds.ToOutsidePixels(appUnitsPerDevPixel);
  containerLayer->SetVisibleRegion(pixBounds);
  // Make sure that rounding the visible region out didn't add any area
  // we won't paint
  if (aChildren.IsOpaque() && !aChildren.NeedsTransparentSurface() &&
      bounds.Contains(pixBounds.ToAppUnits(appUnitsPerDevPixel))) {
    // Clear CONTENT_COMPONENT_ALPHA
    flags = Layer::CONTENT_OPAQUE;
  }
  containerLayer->SetContentFlags(flags);

  return containerLayer.forget();
}

Layer*
FrameLayerBuilder::GetLeafLayerFor(nsDisplayListBuilder* aBuilder,
                                   LayerManager* aManager,
                                   nsDisplayItem* aItem)
{
  if (aManager != mRetainingManager)
    return nsnull;

  nsIFrame* f = aItem->GetUnderlyingFrame();
  NS_ASSERTION(f, "Can only call GetLeafLayerFor on items that have a frame");
  Layer* layer = GetOldLayerFor(f, aItem->GetPerFrameKey());
  if (!layer)
    return nsnull;
  if (layer->HasUserData(&gThebesDisplayItemLayerUserData)) {
    // This layer was created to render Thebes-rendered content for this
    // display item. The display item should not use it for its own
    // layer rendering.
    return nsnull;
  }
  // Clear clip rect; the caller is responsible for setting it.
  layer->SetClipRect(nsnull);
  return layer;
}

/* static */ void
FrameLayerBuilder::InvalidateThebesLayerContents(nsIFrame* aFrame,
                                                 const nsRect& aRect)
{
  nsRegion* invalidThebesContent = static_cast<nsRegion*>
    (aFrame->Properties().Get(ThebesLayerInvalidRegionProperty()));
  if (!invalidThebesContent)
    return;
  invalidThebesContent->Or(*invalidThebesContent, aRect);
  invalidThebesContent->SimplifyOutward(20);
}

/**
 * Returns true if we find a descendant with a container layer
 */
static PRBool
InternalInvalidateThebesLayersInSubtree(nsIFrame* aFrame)
{
  if (!(aFrame->GetStateBits() & NS_FRAME_HAS_CONTAINER_LAYER_DESCENDANT))
    return PR_FALSE;

  PRBool foundContainerLayer = PR_FALSE;
  if (aFrame->GetStateBits() & NS_FRAME_HAS_CONTAINER_LAYER) {
    // Delete the invalid region to indicate that all Thebes contents
    // need to be invalidated
    aFrame->Properties().Delete(ThebesLayerInvalidRegionProperty());
    foundContainerLayer = PR_TRUE;
  }

  PRInt32 listIndex = 0;
  nsIAtom* childList = nsnull;
  do {
    nsIFrame* child = aFrame->GetFirstChild(childList);
    if (!child && !childList) {
      nsSubDocumentFrame* subdocumentFrame = do_QueryFrame(aFrame);
      if (subdocumentFrame) {
        // Descend into the subdocument
        child = subdocumentFrame->GetSubdocumentRootFrame();
      }
    }
    while (child) {
      if (InternalInvalidateThebesLayersInSubtree(child)) {
        foundContainerLayer = PR_TRUE;
      }
      child = child->GetNextSibling();
    }
    childList = aFrame->GetAdditionalChildListName(listIndex++);
  } while (childList);

  if (!foundContainerLayer) {
    aFrame->RemoveStateBits(NS_FRAME_HAS_CONTAINER_LAYER_DESCENDANT);
  }
  return foundContainerLayer;
}

/* static */ void
FrameLayerBuilder::InvalidateThebesLayersInSubtree(nsIFrame* aFrame)
{
  InternalInvalidateThebesLayersInSubtree(aFrame);
}

/* static */ void
FrameLayerBuilder::InvalidateAllLayers(LayerManager* aManager)
{
  LayerManagerData* data = static_cast<LayerManagerData*>
    (aManager->GetUserData(&gLayerManagerUserData));
  if (data) {
    data->mInvalidateAllLayers = PR_TRUE;
  }
}

/* static */
Layer*
FrameLayerBuilder::GetDedicatedLayer(nsIFrame* aFrame, PRUint32 aDisplayItemKey)
{
  void* propValue = aFrame->Properties().Get(DisplayItemDataProperty());
  if (!propValue)
    return nsnull;

  nsTArray<DisplayItemData>* array =
    (reinterpret_cast<nsTArray<DisplayItemData>*>(&propValue));
  for (PRUint32 i = 0; i < array->Length(); ++i) {
    if (array->ElementAt(i).mDisplayItemKey == aDisplayItemKey) {
      Layer* layer = array->ElementAt(i).mLayer;
      if (!layer->HasUserData(&gColorLayerUserData) &&
          !layer->HasUserData(&gImageLayerUserData) &&
          !layer->HasUserData(&gThebesDisplayItemLayerUserData))
        return layer;
    }
  }
  return nsnull;
}

/* static */ void
FrameLayerBuilder::DrawThebesLayer(ThebesLayer* aLayer,
                                   gfxContext* aContext,
                                   const nsIntRegion& aRegionToDraw,
                                   const nsIntRegion& aRegionToInvalidate,
                                   void* aCallbackData)
{
  nsDisplayListBuilder* builder = static_cast<nsDisplayListBuilder*>
    (aCallbackData);

  if (builder->LayerBuilder()->CheckDOMModified())
    return;

  nsTArray<ClippedDisplayItem> items;
  nsIFrame* containerLayerFrame;
  {
    ThebesLayerItemsEntry* entry =
      builder->LayerBuilder()->mThebesLayerItems.GetEntry(aLayer);
    NS_ASSERTION(entry, "We shouldn't be drawing into a layer with no items!");
    items.SwapElements(entry->mItems);
    containerLayerFrame = entry->mContainerLayerFrame;
    // Later after this point, due to calls to DidEndTransaction
    // for temporary layer managers, mThebesLayerItems can change,
    // so 'entry' could become invalid.
  }

  ThebesDisplayItemLayerUserData* userData =
    static_cast<ThebesDisplayItemLayerUserData*>
      (aLayer->GetUserData(&gThebesDisplayItemLayerUserData));
  NS_ASSERTION(userData, "where did our user data go?");
  if (NS_GET_A(userData->mForcedBackgroundColor) > 0) {
    nsIntRect r = aLayer->GetVisibleRegion().GetBounds();
    aContext->NewPath();
    aContext->Rectangle(gfxRect(r.x, r.y, r.width, r.height));
    aContext->SetColor(gfxRGBA(userData->mForcedBackgroundColor));
    aContext->Fill();
  }

  gfxMatrix transform;
  if (!aLayer->GetTransform().Is2D(&transform)) {
    NS_ERROR("non-2D transform in our Thebes layer!");
    return;
  }
  NS_ASSERTION(!transform.HasNonIntegerTranslation(),
               "Matrix not just an integer translation?");
  // make the origin of the context coincide with the origin of the
  // ThebesLayer
  gfxContextMatrixAutoSaveRestore saveMatrix(aContext); 
  aContext->Translate(-gfxPoint(transform.x0, transform.y0));
  nsIntPoint offset(PRInt32(transform.x0), PRInt32(transform.y0));

  nsPresContext* presContext = containerLayerFrame->PresContext();
  PRInt32 appUnitsPerDevPixel = presContext->AppUnitsPerDevPixel();
  nsRect r = (aRegionToInvalidate.GetBounds() + offset).
    ToAppUnits(appUnitsPerDevPixel);
  containerLayerFrame->InvalidateWithFlags(r,
      nsIFrame::INVALIDATE_NO_THEBES_LAYERS |
      nsIFrame::INVALIDATE_EXCLUDE_CURRENT_PAINT);

  PRUint32 i;
  // Update visible regions. We need perform visibility analysis again
  // because we may be asked to draw into part of a ThebesLayer that
  // isn't actually visible in the window (e.g., because a ThebesLayer
  // expanded its visible region to a rectangle internally), in which
  // case the mVisibleRect stored in the display item may be wrong.
  nsRegion visible = aRegionToDraw.ToAppUnits(appUnitsPerDevPixel);
  visible.MoveBy(NSIntPixelsToAppUnits(offset.x, appUnitsPerDevPixel),
                 NSIntPixelsToAppUnits(offset.y, appUnitsPerDevPixel));

  for (i = items.Length(); i > 0; --i) {
    ClippedDisplayItem* cdi = &items[i - 1];

    NS_ASSERTION(AppUnitsPerDevPixel(cdi->mItem) == appUnitsPerDevPixel,
                 "a thebes layer should contain items only at the same zoom");

    NS_ABORT_IF_FALSE(cdi->mClip.mHaveClipRect ||
                      cdi->mClip.mRoundedClipRects.IsEmpty(),
                      "If we have rounded rects, we must have a clip rect");

    if (!cdi->mClip.mHaveClipRect ||
        (cdi->mClip.mRoundedClipRects.IsEmpty() &&
         cdi->mClip.mClipRect.Contains(visible.GetBounds()))) {
      cdi->mItem->RecomputeVisibility(builder, &visible);
      continue;
    }

    // Do a little dance to account for the fact that we're clipping
    // to cdi->mClipRect
    nsRegion clipped;
    clipped.And(visible, cdi->mClip.mClipRect);
    nsRegion finalClipped = clipped;
    cdi->mItem->RecomputeVisibility(builder, &finalClipped);
    // If we have rounded clip rects, don't subtract from the visible
    // region since we aren't displaying everything inside the rect.
    if (cdi->mClip.mRoundedClipRects.IsEmpty()) {
      nsRegion removed;
      removed.Sub(clipped, finalClipped);
      nsRegion newVisible;
      newVisible.Sub(visible, removed);
      // Don't let the visible region get too complex.
      if (newVisible.GetNumRects() <= 15) {
        visible = newVisible;
      }
    }
    if (!cdi->mClip.IsRectClippedByRoundedCorner(cdi->mItem->GetVisibleRect())) {
      cdi->mClip.RemoveRoundedCorners();
    }
  }

  nsRefPtr<nsIRenderingContext> rc;
  nsresult rv =
    presContext->DeviceContext()->CreateRenderingContextInstance(*getter_AddRefs(rc));
  if (NS_FAILED(rv))
    return;
  rc->Init(presContext->DeviceContext(), aContext);

  Clip currentClip;
  PRBool setClipRect = PR_FALSE;

  for (i = 0; i < items.Length(); ++i) {
    ClippedDisplayItem* cdi = &items[i];

    if (cdi->mItem->GetVisibleRect().IsEmpty())
      continue;

    // If the new desired clip state is different from the current state,
    // update the clip.
    if (setClipRect != cdi->mClip.mHaveClipRect ||
        (cdi->mClip.mHaveClipRect && cdi->mClip != currentClip)) {
      if (setClipRect) {
        aContext->Restore();
      }
      setClipRect = cdi->mClip.mHaveClipRect;
      if (setClipRect) {
        currentClip = cdi->mClip;
        aContext->Save();
        currentClip.ApplyTo(aContext, presContext);
      }
    }

    if (cdi->mInactiveLayer) {
      PaintInactiveLayer(builder, cdi->mItem, aContext);
    } else {
      cdi->mItem->Paint(builder, rc);
    }

    if (builder->LayerBuilder()->CheckDOMModified())
      break;
  }

  if (setClipRect) {
    aContext->Restore();
  }
}

PRBool
FrameLayerBuilder::CheckDOMModified()
{
  if (!mRootPresContext ||
      mInitialDOMGeneration == mRootPresContext->GetDOMGeneration())
    return PR_FALSE;
  if (mDetectedDOMModification) {
    // Don't spam the console with extra warnings
    return PR_TRUE;
  }
  mDetectedDOMModification = PR_TRUE;
  // Painting is not going to complete properly. There's not much
  // we can do here though. Invalidating the window to get another repaint
  // is likely to lead to an infinite repaint loop.
  NS_WARNING("Detected DOM modification during paint, bailing out!");
  return PR_TRUE;
}

#ifdef DEBUG
void
FrameLayerBuilder::DumpRetainedLayerTree()
{
  if (mRetainingManager) {
    mRetainingManager->Dump(stderr);
  }
}
#endif

FrameLayerBuilder::Clip::Clip(const Clip& aOther, nsDisplayItem* aClipItem)
  : mRoundedClipRects(aOther.mRoundedClipRects),
    mHaveClipRect(PR_TRUE)
{
  nsDisplayItem::Type type = aClipItem->GetType();
  NS_ABORT_IF_FALSE(type == nsDisplayItem::TYPE_CLIP ||
                    type == nsDisplayItem::TYPE_CLIP_ROUNDED_RECT,
                    "unexpected display item type");
  nsDisplayClip* item = static_cast<nsDisplayClip*>(aClipItem);
  // Always intersect with mClipRect, even if we're going to add a
  // rounded rect.
  if (aOther.mHaveClipRect) {
    mClipRect.IntersectRect(aOther.mClipRect, item->GetClipRect());
  } else {
    mClipRect = item->GetClipRect();
  }

  if (type == nsDisplayItem::TYPE_CLIP_ROUNDED_RECT) {
    RoundedRect *rr = mRoundedClipRects.AppendElement();
    if (rr) {
      rr->mRect = item->GetClipRect();
      static_cast<nsDisplayClipRoundedRect*>(item)->GetRadii(rr->mRadii);
    }
  }

  // FIXME: Optimize away excess rounded rectangles due to the new addition.
}

void
FrameLayerBuilder::Clip::ApplyTo(gfxContext* aContext,
                                 nsPresContext* aPresContext)
{
  aContext->NewPath();
  PRInt32 A2D = aPresContext->AppUnitsPerDevPixel();
  gfxRect clip = nsLayoutUtils::RectToGfxRect(mClipRect, A2D);
  aContext->Rectangle(clip, PR_TRUE);
  aContext->Clip();

  for (PRUint32 i = 0, iEnd = mRoundedClipRects.Length();
       i < iEnd; ++i) {
    const Clip::RoundedRect &rr = mRoundedClipRects[i];

    gfxCornerSizes pixelRadii;
    nsCSSRendering::ComputePixelRadii(rr.mRadii, A2D, &pixelRadii);

    clip = nsLayoutUtils::RectToGfxRect(rr.mRect, A2D);
    clip.Round();
    clip.Condition();
    // REVIEW: This might make clip empty.  Is that OK?

    aContext->NewPath();
    aContext->RoundedRectangle(clip, pixelRadii);
    aContext->Clip();
  }
}

nsRect
FrameLayerBuilder::Clip::ApproximateIntersect(const nsRect& aRect) const
{
  nsRect r = aRect;
  if (mHaveClipRect) {
    r.IntersectRect(r, mClipRect);
  }
  for (PRUint32 i = 0, iEnd = mRoundedClipRects.Length();
       i < iEnd; ++i) {
    const Clip::RoundedRect &rr = mRoundedClipRects[i];
    nsRegion rgn = nsLayoutUtils::RoundedRectIntersectRect(rr.mRect, rr.mRadii, r);
    r = rgn.GetLargestRectangle();
  }
  return r;
}

// Test if (aXPoint, aYPoint) is in the ellipse with center (aXCenter, aYCenter)
// and radii aXRadius, aYRadius.
bool IsInsideEllipse(nscoord aXRadius, nscoord aXCenter, nscoord aXPoint,
                     nscoord aYRadius, nscoord aYCenter, nscoord aYPoint)
{
  float scaledX = float(aXPoint - aXCenter) / float(aXRadius);
  float scaledY = float(aYPoint - aYCenter) / float(aYRadius);
  return scaledX * scaledX + scaledY * scaledY < 1.0f;
}

bool
FrameLayerBuilder::Clip::IsRectClippedByRoundedCorner(const nsRect& aRect) const
{
  if (mRoundedClipRects.IsEmpty())
    return false;

  nsRect rect;
  rect.IntersectRect(aRect, NonRoundedIntersection());
  for (PRUint32 i = 0, iEnd = mRoundedClipRects.Length();
       i < iEnd; ++i) {
    const Clip::RoundedRect &rr = mRoundedClipRects[i];
    // top left
    if (rect.x < rr.mRect.x + rr.mRadii[NS_CORNER_TOP_LEFT_X] &&
        rect.y < rr.mRect.y + rr.mRadii[NS_CORNER_TOP_LEFT_Y]) {
      if (!IsInsideEllipse(rr.mRadii[NS_CORNER_TOP_LEFT_X],
                           rr.mRect.x + rr.mRadii[NS_CORNER_TOP_LEFT_X],
                           rect.x,
                           rr.mRadii[NS_CORNER_TOP_LEFT_Y],
                           rr.mRect.y + rr.mRadii[NS_CORNER_TOP_LEFT_Y],
                           rect.y)) {
        return true;
      }
    }
    // top right
    if (rect.XMost() > rr.mRect.XMost() - rr.mRadii[NS_CORNER_TOP_RIGHT_X] &&
        rect.y < rr.mRect.y + rr.mRadii[NS_CORNER_TOP_RIGHT_Y]) {
      if (!IsInsideEllipse(rr.mRadii[NS_CORNER_TOP_RIGHT_X],
                           rr.mRect.XMost() - rr.mRadii[NS_CORNER_TOP_RIGHT_X],
                           rect.XMost(),
                           rr.mRadii[NS_CORNER_TOP_RIGHT_Y],
                           rr.mRect.y + rr.mRadii[NS_CORNER_TOP_RIGHT_Y],
                           rect.y)) {
        return true;
      }
    }
    // bottom left
    if (rect.x < rr.mRect.x + rr.mRadii[NS_CORNER_BOTTOM_LEFT_X] &&
        rect.YMost() > rr.mRect.YMost() - rr.mRadii[NS_CORNER_BOTTOM_LEFT_Y]) {
      if (!IsInsideEllipse(rr.mRadii[NS_CORNER_BOTTOM_LEFT_X],
                           rr.mRect.x + rr.mRadii[NS_CORNER_BOTTOM_LEFT_X],
                           rect.x,
                           rr.mRadii[NS_CORNER_BOTTOM_LEFT_Y],
                           rr.mRect.YMost() - rr.mRadii[NS_CORNER_BOTTOM_LEFT_Y],
                           rect.YMost())) {
        return true;
      }
    }
    // bottom right
    if (rect.XMost() > rr.mRect.XMost() - rr.mRadii[NS_CORNER_BOTTOM_RIGHT_X] &&
        rect.YMost() > rr.mRect.YMost() - rr.mRadii[NS_CORNER_BOTTOM_RIGHT_Y]) {
      if (!IsInsideEllipse(rr.mRadii[NS_CORNER_BOTTOM_RIGHT_X],
                           rr.mRect.XMost() - rr.mRadii[NS_CORNER_BOTTOM_RIGHT_X],
                           rect.XMost(),
                           rr.mRadii[NS_CORNER_BOTTOM_RIGHT_Y],
                           rr.mRect.YMost() - rr.mRadii[NS_CORNER_BOTTOM_RIGHT_Y],
                           rect.YMost())) {
        return true;
      }
    }
  }
  return false;
}

nsRect
FrameLayerBuilder::Clip::NonRoundedIntersection() const
{
  NS_ASSERTION(!mRoundedClipRects.IsEmpty(), "no rounded clip rects?");
  nsRect result = mClipRect;
  for (PRUint32 i = 0, iEnd = mRoundedClipRects.Length();
       i < iEnd; ++i) {
    result.IntersectRect(result, mRoundedClipRects[i].mRect);
  }
  return result;
}

void
FrameLayerBuilder::Clip::RemoveRoundedCorners()
{
  if (mRoundedClipRects.IsEmpty())
    return;

  mClipRect = NonRoundedIntersection();
  mRoundedClipRects.Clear();
}

} // namespace mozilla
