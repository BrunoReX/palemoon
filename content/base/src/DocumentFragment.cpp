/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implementation of DOM Core's nsIDOMDocumentFragment.
 */

#include "mozilla/dom/DocumentFragment.h"
#include "mozilla/dom/Element.h" // for DOMCI_NODE_DATA
#include "nsINameSpaceManager.h"
#include "nsINodeInfo.h"
#include "nsNodeInfoManager.h"
#include "nsError.h"
#include "nsGkAtoms.h"
#include "nsDOMString.h"
#include "nsContentUtils.h"
#include "mozilla/dom/DocumentFragmentBinding.h"

namespace mozilla {
namespace dom {

JSObject*
DocumentFragment::WrapNode(JSContext *aCx, JS::Handle<JSObject*> aScope)
{
  return DocumentFragmentBinding::Wrap(aCx, aScope, this);
}

bool
DocumentFragment::IsNodeOfType(uint32_t aFlags) const
{
  return !(aFlags & ~(eCONTENT | eDOCUMENT_FRAGMENT));
}

nsIAtom*
DocumentFragment::DoGetID() const
{
  return nullptr;  
}

nsIAtom*
DocumentFragment::GetIDAttributeName() const
{
  return nullptr;
}

#ifdef DEBUG
void
DocumentFragment::List(FILE* out, int32_t aIndent) const
{
  int32_t indent;
  for (indent = aIndent; --indent >= 0; ) {
    fputs("  ", out);
  }

  fprintf(out, "DocumentFragment@%p", (void *)this);

  fprintf(out, " flags=[%08x]", static_cast<unsigned int>(GetFlags()));
  fprintf(out, " refcount=%d<", mRefCnt.get());

  nsIContent* child = GetFirstChild();
  if (child) {
    fputs("\n", out);

    for (; child; child = child->GetNextSibling()) {
      child->List(out, aIndent + 1);
    }

    for (indent = aIndent; --indent >= 0; ) {
      fputs("  ", out);
    }
  }

  fputs(">\n", out);
}

void
DocumentFragment::DumpContent(FILE* out, int32_t aIndent,
                              bool aDumpAll) const
{
  int32_t indent;
  for (indent = aIndent; --indent >= 0; ) {
    fputs("  ", out);
  }

  fputs("<DocumentFragment>", out);

  if(aIndent) {
    fputs("\n", out);
  }

  for (nsIContent* child = GetFirstChild();
       child;
       child = child->GetNextSibling()) {
    int32_t indent = aIndent ? aIndent + 1 : 0;
    child->DumpContent(out, indent, aDumpAll);
  }
  for (indent = aIndent; --indent >= 0; ) {
    fputs("  ", out);
  }
  fputs("</DocumentFragment>", out);

  if(aIndent) {
    fputs("\n", out);
  }
}
#endif

/* static */ already_AddRefed<DocumentFragment>
DocumentFragment::Constructor(const GlobalObject& aGlobal, ErrorResult& aRv)
{
  nsCOMPtr<nsPIDOMWindow> window = do_QueryInterface(aGlobal.Get());
  if (!window || !window->GetDoc()) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  return window->GetDoc()->CreateDocumentFragment();
}

// QueryInterface implementation for DocumentFragment
NS_INTERFACE_MAP_BEGIN(DocumentFragment)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRIES_CYCLE_COLLECTION(DocumentFragment)
  NS_INTERFACE_MAP_ENTRY(nsIContent)
  NS_INTERFACE_MAP_ENTRY(nsINode)
  NS_INTERFACE_MAP_ENTRY(nsIDOMDocumentFragment)
  NS_INTERFACE_MAP_ENTRY(nsIDOMNode)
  NS_INTERFACE_MAP_ENTRY(nsIDOMEventTarget)
  NS_INTERFACE_MAP_ENTRY(mozilla::dom::EventTarget)
  NS_INTERFACE_MAP_ENTRY_TEAROFF(nsISupportsWeakReference,
                                 new nsNodeSupportsWeakRefTearoff(this))
  NS_INTERFACE_MAP_ENTRY_TEAROFF(nsIDOMNodeSelector,
                                 new nsNodeSelectorTearoff(this))
  // nsNodeSH::PreCreate() depends on the identity pointer being the
  // same as nsINode (which nsIContent inherits), so if you change the
  // below line, make sure nsNodeSH::PreCreate() still does the right
  // thing!
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIContent)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF_INHERITED(DocumentFragment, FragmentOrElement)
NS_IMPL_RELEASE_INHERITED(DocumentFragment, FragmentOrElement)

NS_IMPL_ELEMENT_CLONE(DocumentFragment)

} // namespace dom
} // namespace mozilla
