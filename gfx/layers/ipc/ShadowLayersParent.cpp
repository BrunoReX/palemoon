/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=8 et :
 */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at:
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Code.
 *
 * The Initial Developer of the Original Code is
 *   The Mozilla Foundation
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Chris Jones <jones.chris.g@gmail.com>
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

#include <vector>

#include "ShadowLayersParent.h"
#include "ShadowLayerParent.h"
#include "ShadowLayers.h"

#include "mozilla/unused.h"

#include "mozilla/layout/RenderFrameParent.h"

#include "gfxSharedImageSurface.h"

#include "ImageLayers.h"

typedef std::vector<mozilla::layers::EditReply> EditReplyVector;

using mozilla::layout::RenderFrameParent;

namespace mozilla {
namespace layers {

//--------------------------------------------------
// Convenience accessors
static ShadowLayerParent*
cast(const PLayerParent* in)
{ 
  return const_cast<ShadowLayerParent*>(
    static_cast<const ShadowLayerParent*>(in));
}

template<class OpCreateT>
static ShadowLayerParent*
AsShadowLayer(const OpCreateT& op)
{
  return cast(op.layerParent());
}

static ShadowLayerParent*
AsShadowLayer(const OpSetRoot& op)
{
  return cast(op.rootParent());
}

static ShadowLayerParent*
ShadowContainer(const OpInsertAfter& op)
{
  return cast(op.containerParent());
}
static ShadowLayerParent*
ShadowChild(const OpInsertAfter& op)
{
  return cast(op.childLayerParent());
}
static ShadowLayerParent*
ShadowAfter(const OpInsertAfter& op)
{
  return cast(op.afterParent());
}

static ShadowLayerParent*
ShadowContainer(const OpAppendChild& op)
{
  return cast(op.containerParent());
}
static ShadowLayerParent*
ShadowChild(const OpAppendChild& op)
{
  return cast(op.childLayerParent());
}

static ShadowLayerParent*
ShadowContainer(const OpRemoveChild& op)
{
  return cast(op.containerParent());
}
static ShadowLayerParent*
ShadowChild(const OpRemoveChild& op)
{
  return cast(op.childLayerParent());
}

//--------------------------------------------------
// ShadowLayersParent
ShadowLayersParent::ShadowLayersParent(ShadowLayerManager* aManager)
  : mDestroyed(false)
{
  MOZ_COUNT_CTOR(ShadowLayersParent);
  mLayerManager = aManager;
}

ShadowLayersParent::~ShadowLayersParent()
{
  MOZ_COUNT_DTOR(ShadowLayersParent);
}

void
ShadowLayersParent::Destroy()
{
  mDestroyed = true;
  for (size_t i = 0; i < ManagedPLayerParent().Length(); ++i) {
    ShadowLayerParent* slp =
      static_cast<ShadowLayerParent*>(ManagedPLayerParent()[i]);
    slp->Destroy();
  }
}

bool
ShadowLayersParent::RecvUpdate(const InfallibleTArray<Edit>& cset,
                               InfallibleTArray<EditReply>* reply)
{
  MOZ_LAYERS_LOG(("[ParentSide] received txn with %d edits", cset.Length()));

  if (mDestroyed || layer_manager()->IsDestroyed()) {
    return true;
  }

  EditReplyVector replyv;

  layer_manager()->BeginTransactionWithTarget(NULL);

  for (EditArray::index_type i = 0; i < cset.Length(); ++i) {
    const Edit& edit = cset[i];

    switch (edit.type()) {
      // Create* ops
    case Edit::TOpCreateThebesLayer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateThebesLayer"));

      nsRefPtr<ShadowThebesLayer> layer =
        layer_manager()->CreateShadowThebesLayer();
      layer->SetAllocator(this);
      AsShadowLayer(edit.get_OpCreateThebesLayer())->Bind(layer);
      break;
    }
    case Edit::TOpCreateContainerLayer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateContainerLayer"));

      nsRefPtr<ContainerLayer> layer = layer_manager()->CreateShadowContainerLayer();
      AsShadowLayer(edit.get_OpCreateContainerLayer())->Bind(layer);
      break;
    }
    case Edit::TOpCreateImageLayer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateImageLayer"));

      nsRefPtr<ShadowImageLayer> layer =
        layer_manager()->CreateShadowImageLayer();
      AsShadowLayer(edit.get_OpCreateImageLayer())->Bind(layer);
      break;
    }
    case Edit::TOpCreateColorLayer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateColorLayer"));

      nsRefPtr<ShadowColorLayer> layer = layer_manager()->CreateShadowColorLayer();
      AsShadowLayer(edit.get_OpCreateColorLayer())->Bind(layer);
      break;
    }
    case Edit::TOpCreateCanvasLayer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateCanvasLayer"));

      nsRefPtr<ShadowCanvasLayer> layer = 
        layer_manager()->CreateShadowCanvasLayer();
      layer->SetAllocator(this);
      AsShadowLayer(edit.get_OpCreateCanvasLayer())->Bind(layer);
      break;
    }

      // Attributes
    case Edit::TOpSetLayerAttributes: {
      MOZ_LAYERS_LOG(("[ParentSide] SetLayerAttributes"));

      const OpSetLayerAttributes& osla = edit.get_OpSetLayerAttributes();
      Layer* layer = AsShadowLayer(osla)->AsLayer();
      const LayerAttributes& attrs = osla.attrs();

      const CommonLayerAttributes& common = attrs.common();
      layer->SetVisibleRegion(common.visibleRegion());
      layer->SetContentFlags(common.contentFlags());
      layer->SetOpacity(common.opacity());
      layer->SetClipRect(common.useClipRect() ? &common.clipRect() : NULL);
      layer->SetTransform(common.transform());
      layer->SetTileSourceRect(common.useTileSourceRect() ? &common.tileSourceRect() : NULL);
      static bool fixedPositionLayersEnabled = getenv("MOZ_ENABLE_FIXED_POSITION_LAYERS") != 0;
      if (fixedPositionLayersEnabled) {
        layer->SetIsFixedPosition(common.isFixedPosition());
      }

      typedef SpecificLayerAttributes Specific;
      const SpecificLayerAttributes& specific = attrs.specific();
      switch (specific.type()) {
      case Specific::Tnull_t:
        break;

      case Specific::TThebesLayerAttributes: {
        MOZ_LAYERS_LOG(("[ParentSide]   thebes layer"));

        ShadowThebesLayer* thebesLayer =
          static_cast<ShadowThebesLayer*>(layer);
        const ThebesLayerAttributes& attrs =
          specific.get_ThebesLayerAttributes();

        thebesLayer->SetValidRegion(attrs.validRegion());

        break;
      }
      case Specific::TContainerLayerAttributes:
        MOZ_LAYERS_LOG(("[ParentSide]   container layer"));

        static_cast<ContainerLayer*>(layer)->SetFrameMetrics(
          specific.get_ContainerLayerAttributes().metrics());
        break;

      case Specific::TColorLayerAttributes:
        MOZ_LAYERS_LOG(("[ParentSide]   color layer"));

        static_cast<ColorLayer*>(layer)->SetColor(
          specific.get_ColorLayerAttributes().color());
        break;

      case Specific::TCanvasLayerAttributes:
        MOZ_LAYERS_LOG(("[ParentSide]   canvas layer"));

        static_cast<CanvasLayer*>(layer)->SetFilter(
          specific.get_CanvasLayerAttributes().filter());
        break;

      case Specific::TImageLayerAttributes:
        MOZ_LAYERS_LOG(("[ParentSide]   image layer"));

        static_cast<ImageLayer*>(layer)->SetFilter(
          specific.get_ImageLayerAttributes().filter());
        break;

      default:
        NS_RUNTIMEABORT("not reached");
      }
      break;
    }

      // Tree ops
    case Edit::TOpSetRoot: {
      MOZ_LAYERS_LOG(("[ParentSide] SetRoot"));

      mRoot = AsShadowLayer(edit.get_OpSetRoot())->AsContainer();
      break;
    }
    case Edit::TOpInsertAfter: {
      MOZ_LAYERS_LOG(("[ParentSide] InsertAfter"));

      const OpInsertAfter& oia = edit.get_OpInsertAfter();
      ShadowContainer(oia)->AsContainer()->InsertAfter(
        ShadowChild(oia)->AsLayer(), ShadowAfter(oia)->AsLayer());
      break;
    }
    case Edit::TOpAppendChild: {
      MOZ_LAYERS_LOG(("[ParentSide] AppendChild"));

      const OpAppendChild& oac = edit.get_OpAppendChild();
      ShadowContainer(oac)->AsContainer()->InsertAfter(
        ShadowChild(oac)->AsLayer(), NULL);
      break;
    }
    case Edit::TOpRemoveChild: {
      MOZ_LAYERS_LOG(("[ParentSide] RemoveChild"));

      const OpRemoveChild& orc = edit.get_OpRemoveChild();
      Layer* childLayer = ShadowChild(orc)->AsLayer();
      ShadowContainer(orc)->AsContainer()->RemoveChild(childLayer);
      break;
    }

    case Edit::TOpPaintThebesBuffer: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint ThebesLayer"));

      const OpPaintThebesBuffer& op = edit.get_OpPaintThebesBuffer();
      ShadowLayerParent* shadow = AsShadowLayer(op);
      ShadowThebesLayer* thebes =
        static_cast<ShadowThebesLayer*>(shadow->AsLayer());
      const ThebesBuffer& newFront = op.newFrontBuffer();

      OptionalThebesBuffer newBack;
      nsIntRegion newValidRegion;
      OptionalThebesBuffer readonlyFront;
      nsIntRegion frontUpdatedRegion;
      thebes->Swap(newFront, op.updatedRegion(),
                   &newBack, &newValidRegion,
                   &readonlyFront, &frontUpdatedRegion);
      replyv.push_back(
        OpThebesBufferSwap(
          shadow, NULL,
          newBack, newValidRegion,
          readonlyFront, frontUpdatedRegion));
      break;
    }
    case Edit::TOpPaintCanvas: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint CanvasLayer"));

      const OpPaintCanvas& op = edit.get_OpPaintCanvas();
      ShadowLayerParent* shadow = AsShadowLayer(op);
      ShadowCanvasLayer* canvas =
        static_cast<ShadowCanvasLayer*>(shadow->AsLayer());

      canvas->SetAllocator(this);
      CanvasSurface newBack;
      canvas->Swap(op.newFrontBuffer(), op.needYFlip(), &newBack);
      canvas->Updated();
      replyv.push_back(OpBufferSwap(shadow, NULL,
                                    newBack));

      break;
    }
    case Edit::TOpPaintImage: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint ImageLayer"));

      const OpPaintImage& op = edit.get_OpPaintImage();
      ShadowLayerParent* shadow = AsShadowLayer(op);
      ShadowImageLayer* image =
        static_cast<ShadowImageLayer*>(shadow->AsLayer());

      image->SetAllocator(this);
      SharedImage newBack;
      image->Swap(op.newFrontBuffer(), &newBack);
      replyv.push_back(OpImageSwap(shadow, NULL,
                                   newBack));

      break;
    }

    default:
      NS_RUNTIMEABORT("not reached");
    }
  }

  layer_manager()->EndTransaction(NULL, NULL, LayerManager::END_NO_IMMEDIATE_REDRAW);

  reply->SetCapacity(replyv.size());
  if (replyv.size() > 0) {
    reply->AppendElements(&replyv.front(), replyv.size());
  }

  // Ensure that any pending operations involving back and front
  // buffers have completed, so that neither process stomps on the
  // other's buffer contents.
  ShadowLayerManager::PlatformSyncBeforeReplyUpdate();

  Frame()->ShadowLayersUpdated();

  return true;
}

PLayerParent*
ShadowLayersParent::AllocPLayer()
{
  return new ShadowLayerParent();
}

bool
ShadowLayersParent::DeallocPLayer(PLayerParent* actor)
{
  delete actor;
  return true;
}

RenderFrameParent*
ShadowLayersParent::Frame()
{
  return static_cast<RenderFrameParent*>(Manager());
}

void
ShadowLayersParent::DestroySharedSurface(gfxSharedImageSurface* aSurface)
{
  layer_manager()->DestroySharedSurface(aSurface, this);
}

void
ShadowLayersParent::DestroySharedSurface(SurfaceDescriptor* aSurface)
{
  layer_manager()->DestroySharedSurface(aSurface, this);
}

} // namespace layers
} // namespace mozilla
