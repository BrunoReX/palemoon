/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=2 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CompositorChild.h"
#include "CompositorParent.h"
#include "LayerManagerOGL.h"
#include "mozilla/layers/LayerTransactionChild.h"

using mozilla::layers::LayerTransactionChild;

namespace mozilla {
namespace layers {

/*static*/ CompositorChild* CompositorChild::sCompositor;

CompositorChild::CompositorChild(LayerManager *aLayerManager)
  : mLayerManager(aLayerManager)
{
  MOZ_COUNT_CTOR(CompositorChild);
}

CompositorChild::~CompositorChild()
{
  MOZ_COUNT_DTOR(CompositorChild);
}

void
CompositorChild::Destroy()
{
  mLayerManager->Destroy();
  mLayerManager = NULL;
  while (size_t len = ManagedPLayerTransactionChild().Length()) {
    LayerTransactionChild* layers =
      static_cast<LayerTransactionChild*>(ManagedPLayerTransactionChild()[len - 1]);
    layers->Destroy();
  }
  SendStop();
}

/*static*/ PCompositorChild*
CompositorChild::Create(Transport* aTransport, ProcessId aOtherProcess)
{
  // There's only one compositor per child process.
  MOZ_ASSERT(!sCompositor);

  nsRefPtr<CompositorChild> child(new CompositorChild(nullptr));
  ProcessHandle handle;
  if (!base::OpenProcessHandle(aOtherProcess, &handle)) {
    // We can't go on without a compositor.
    NS_RUNTIMEABORT("Couldn't OpenProcessHandle() to parent process.");
    return nullptr;
  }
  if (!child->Open(aTransport, handle, XRE_GetIOMessageLoop(),
                AsyncChannel::Child)) {
    NS_RUNTIMEABORT("Couldn't Open() Compositor channel.");
    return nullptr;
  }
  // We release this ref in ActorDestroy().
  return sCompositor = child.forget().get();
}

/*static*/ PCompositorChild*
CompositorChild::Get()
{
  // This is only expected to be used in child processes.
  MOZ_ASSERT(XRE_GetProcessType() != GeckoProcessType_Default);
  return sCompositor;
}

PLayerTransactionChild*
CompositorChild::AllocPLayerTransaction(const LayersBackend& aBackendHint,
                                        const uint64_t& aId,
                                        TextureFactoryIdentifier*)
{
  return new LayerTransactionChild();
}

bool
CompositorChild::DeallocPLayerTransaction(PLayerTransactionChild* actor)
{
  delete actor;
  return true;
}

void
CompositorChild::ActorDestroy(ActorDestroyReason aWhy)
{
  MOZ_ASSERT(sCompositor == this);

  if (aWhy == AbnormalShutdown) {
    NS_RUNTIMEABORT("ActorDestroy by IPC channel failure at CompositorChild");
  }

  sCompositor = NULL;
  // We don't want to release the ref to sCompositor here, during
  // cleanup, because that will cause it to be deleted while it's
  // still being used.  So defer the deletion to after it's not in
  // use.
  MessageLoop::current()->PostTask(
    FROM_HERE,
    NewRunnableMethod(this, &CompositorChild::Release));
}


} // namespace layers
} // namespace mozilla

