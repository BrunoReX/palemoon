/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set tw=80 expandtab softtabstop=2 ts=2 sw=2: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_HTMLAreaElement_h
#define mozilla_dom_HTMLAreaElement_h

#include "mozilla/Attributes.h"
#include "nsIDOMHTMLAreaElement.h"
#include "nsGenericHTMLElement.h"
#include "nsILink.h"
#include "nsGkAtoms.h"
#include "nsIURL.h"
#include "Link.h"

class nsIDocument;

namespace mozilla {
namespace dom {

class HTMLAreaElement : public nsGenericHTMLElement,
                        public nsIDOMHTMLAreaElement,
                        public nsILink,
                        public Link
{
public:
  HTMLAreaElement(already_AddRefed<nsINodeInfo> aNodeInfo);
  virtual ~HTMLAreaElement();

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  // DOM memory reporter participant
  NS_DECL_SIZEOF_EXCLUDING_THIS

  // nsIDOMNode
  NS_FORWARD_NSIDOMNODE_TO_NSINODE

  // nsIDOMElement
  NS_FORWARD_NSIDOMELEMENT_TO_GENERIC

  // nsIDOMHTMLElement
  NS_FORWARD_NSIDOMHTMLELEMENT_TO_GENERIC

  virtual int32_t TabIndexDefault() MOZ_OVERRIDE;

  // nsIDOMHTMLAreaElement
  NS_DECL_NSIDOMHTMLAREAELEMENT

  // nsILink
  NS_IMETHOD LinkAdded() MOZ_OVERRIDE { return NS_OK; }
  NS_IMETHOD LinkRemoved() MOZ_OVERRIDE { return NS_OK; }

  virtual nsresult PreHandleEvent(nsEventChainPreVisitor& aVisitor) MOZ_OVERRIDE;
  virtual nsresult PostHandleEvent(nsEventChainPostVisitor& aVisitor) MOZ_OVERRIDE;
  virtual bool IsLink(nsIURI** aURI) const MOZ_OVERRIDE;
  virtual void GetLinkTarget(nsAString& aTarget) MOZ_OVERRIDE;
  virtual already_AddRefed<nsIURI> GetHrefURI() const MOZ_OVERRIDE;

  virtual nsresult BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                              nsIContent* aBindingParent,
                              bool aCompileEventHandlers) MOZ_OVERRIDE;
  virtual void UnbindFromTree(bool aDeep = true,
                              bool aNullParent = true) MOZ_OVERRIDE;
  nsresult SetAttr(int32_t aNameSpaceID, nsIAtom* aName,
                   const nsAString& aValue, bool aNotify)
  {
    return SetAttr(aNameSpaceID, aName, nullptr, aValue, aNotify);
  }
  virtual nsresult SetAttr(int32_t aNameSpaceID, nsIAtom* aName,
                           nsIAtom* aPrefix, const nsAString& aValue,
                           bool aNotify) MOZ_OVERRIDE;
  virtual nsresult UnsetAttr(int32_t aNameSpaceID, nsIAtom* aAttribute,
                             bool aNotify) MOZ_OVERRIDE;

  virtual nsresult Clone(nsINodeInfo* aNodeInfo, nsINode** aResult) const MOZ_OVERRIDE;

  virtual nsEventStates IntrinsicState() const MOZ_OVERRIDE;

  virtual nsIDOMNode* AsDOMNode() MOZ_OVERRIDE { return this; }

  // WebIDL

  // The XPCOM GetAlt is OK for us
  void SetAlt(const nsAString& aAlt, ErrorResult& aError)
  {
    SetHTMLAttr(nsGkAtoms::alt, aAlt, aError);
  }

  // The XPCOM GetCoords is OK for us
  void SetCoords(const nsAString& aCoords, ErrorResult& aError)
  {
    SetHTMLAttr(nsGkAtoms::coords, aCoords, aError);
  }

  // The XPCOM GetShape is OK for us
  void SetShape(const nsAString& aShape, ErrorResult& aError)
  {
    SetHTMLAttr(nsGkAtoms::shape, aShape, aError);
  }

  // The XPCOM GetHref is OK for us
  void SetHref(const nsAString& aHref, ErrorResult& aError)
  {
    aError = SetHref(aHref);
  }

  // The XPCOM GetTarget is OK for us
  void SetTarget(const nsAString& aTarget, ErrorResult& aError)
  {
    SetHTMLAttr(nsGkAtoms::target, aTarget, aError);
  }

  // The XPCOM GetDownload is OK for us
  void SetDownload(const nsAString& aDownload, ErrorResult& aError)
  {
    SetHTMLAttr(nsGkAtoms::download, aDownload, aError);
  }

  // The XPCOM GetPing is OK for us
  void SetPing(const nsAString& aPing, ErrorResult& aError)
  {
    SetHTMLAttr(nsGkAtoms::ping, aPing, aError);
  }

  // The XPCOM GetProtocol is OK for us
  // The XPCOM SetProtocol is OK for us

  // The XPCOM GetHost is OK for us
  // The XPCOM SetHost is OK for us

  // The XPCOM GetHostname is OK for us
  // The XPCOM SetHostname is OK for us

  // The XPCOM GetPort is OK for us
  // The XPCOM SetPort is OK for us

  // The XPCOM GetPathname is OK for us
  // The XPCOM SetPathname is OK for us

  // The XPCOM GetSearch is OK for us
  // The XPCOM SetSearch is OK for us

  // The XPCOM GetHash is OK for us
  // The XPCOM SetHash is OK for us

  bool NoHref() const
  {
    return GetBoolAttr(nsGkAtoms::nohref);
  }

  void SetNoHref(bool aValue, ErrorResult& aError)
  {
    SetHTMLBoolAttr(nsGkAtoms::nohref, aValue, aError);
  }

  void Stringify(nsAString& aResult)
  {
    GetHref(aResult);
  }

protected:
  virtual JSObject* WrapNode(JSContext* aCx,
                             JS::Handle<JSObject*> aScope) MOZ_OVERRIDE;

  virtual void GetItemValueText(nsAString& text) MOZ_OVERRIDE;
  virtual void SetItemValueText(const nsAString& text) MOZ_OVERRIDE;
};

} // namespace dom
} // namespace mozilla

#endif /* mozilla_dom_HTMLAreaElement_h */
