/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLContext.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"

using namespace mozilla;

JSObject*
WebGLFramebuffer::WrapObject(JSContext *cx, JSObject *scope, bool *triedToWrap) {
    return dom::WebGLFramebufferBinding::Wrap(cx, scope, this, triedToWrap);
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_8(WebGLFramebuffer,
  mColorAttachment.mTexturePtr,
  mColorAttachment.mRenderbufferPtr,
  mDepthAttachment.mTexturePtr,
  mDepthAttachment.mRenderbufferPtr,
  mStencilAttachment.mTexturePtr,
  mStencilAttachment.mRenderbufferPtr,
  mDepthStencilAttachment.mTexturePtr,
  mDepthStencilAttachment.mRenderbufferPtr)

NS_IMPL_CYCLE_COLLECTING_ADDREF(WebGLFramebuffer)
NS_IMPL_CYCLE_COLLECTING_RELEASE(WebGLFramebuffer)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(WebGLFramebuffer)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END
