/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_HTMLOptGroupElement_h
#define mozilla_dom_HTMLOptGroupElement_h

#include "mozilla/Attributes.h"
#include "nsIDOMHTMLOptGroupElement.h"
#include "nsGenericHTMLElement.h"

namespace mozilla {
namespace dom {

class HTMLOptGroupElement : public nsGenericHTMLElement,
                            public nsIDOMHTMLOptGroupElement
{
public:
  HTMLOptGroupElement(already_AddRefed<nsINodeInfo> aNodeInfo);
  virtual ~HTMLOptGroupElement();

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  // nsIDOMNode
  NS_FORWARD_NSIDOMNODE_TO_NSINODE

  // nsIDOMElement
  NS_FORWARD_NSIDOMELEMENT_TO_GENERIC

  // nsIDOMHTMLElement
  NS_FORWARD_NSIDOMHTMLELEMENT_TO_GENERIC

  // nsIDOMHTMLOptGroupElement
  NS_DECL_NSIDOMHTMLOPTGROUPELEMENT

  // nsINode
  virtual nsresult InsertChildAt(nsIContent* aKid, uint32_t aIndex,
                                 bool aNotify) MOZ_OVERRIDE;
  virtual void RemoveChildAt(uint32_t aIndex, bool aNotify) MOZ_OVERRIDE;

  // nsIContent
  virtual nsresult PreHandleEvent(nsEventChainPreVisitor& aVisitor) MOZ_OVERRIDE;

  virtual nsEventStates IntrinsicState() const MOZ_OVERRIDE;
 
  virtual nsresult Clone(nsINodeInfo* aNodeInfo, nsINode** aResult) const MOZ_OVERRIDE;

  virtual nsresult AfterSetAttr(int32_t aNameSpaceID, nsIAtom* aName,
                                const nsAttrValue* aValue, bool aNotify) MOZ_OVERRIDE;

  virtual nsIDOMNode* AsDOMNode() MOZ_OVERRIDE { return this; }

  virtual bool IsDisabled() const MOZ_OVERRIDE {
    return HasAttr(kNameSpaceID_None, nsGkAtoms::disabled);
  }

  bool Disabled() const
  {
    return GetBoolAttr(nsGkAtoms::disabled);
  }
  void SetDisabled(bool aValue, ErrorResult& aError)
  {
     SetHTMLBoolAttr(nsGkAtoms::disabled, aValue, aError);
  }

  // The XPCOM GetLabel is OK for us
  void SetLabel(const nsAString& aLabel, ErrorResult& aError)
  {
    SetHTMLAttr(nsGkAtoms::label, aLabel, aError);
  }

protected:
  virtual JSObject* WrapNode(JSContext* aCx,
                             JS::Handle<JSObject*> aScope) MOZ_OVERRIDE;

protected:

  /**
   * Get the select content element that contains this option
   * @param aSelectElement the select element [OUT]
   */
  nsIContent* GetSelect();
};

} // namespace dom
} // namespace mozilla

#endif /* mozilla_dom_HTMLOptGroupElement_h */
