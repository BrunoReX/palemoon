/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef nsHTMLDivElement_h___
#define nsHTMLDivElement_h___

#include "nsGenericHTMLElement.h"
#include "nsIDOMHTMLDivElement.h"

class nsHTMLDivElement : public nsGenericHTMLElement,
                         public nsIDOMHTMLDivElement
{
public:
  nsHTMLDivElement(already_AddRefed<nsINodeInfo> aNodeInfo);
  virtual ~nsHTMLDivElement();

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  // nsIDOMNode
  NS_FORWARD_NSIDOMNODE_TO_NSINODE

  // nsIDOMElement
  NS_FORWARD_NSIDOMELEMENT_TO_GENERIC

  // nsIDOMHTMLElement
  NS_FORWARD_NSIDOMHTMLELEMENT_TO_GENERIC

  // nsIDOMHTMLDivElement
  NS_IMETHOD GetAlign(nsAString& aAlign)
  {
    nsString align;
    GetAlign(align);
    aAlign = align;
    return NS_OK;
  }
  NS_IMETHOD SetAlign(const nsAString& aAlign)
  {
    mozilla::ErrorResult rv;
    SetAlign(aAlign, rv);
    return rv.ErrorCode();
  }

  void GetAlign(nsString& aAlign)
  {
    GetHTMLAttr(nsGkAtoms::align, aAlign);
  }
  void SetAlign(const nsAString& aAlign, mozilla::ErrorResult& aError)
  {
    SetHTMLAttr(nsGkAtoms::align, aAlign, aError);
  }

  virtual bool ParseAttribute(int32_t aNamespaceID,
                                nsIAtom* aAttribute,
                                const nsAString& aValue,
                                nsAttrValue& aResult);
  NS_IMETHOD_(bool) IsAttributeMapped(const nsIAtom* aAttribute) const;
  virtual nsMapRuleToAttributesFunc GetAttributeMappingFunction() const;
  virtual nsresult Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const;

  virtual nsXPCClassInfo* GetClassInfo();
  virtual nsIDOMNode* AsDOMNode() { return this; }
};

#endif /* nsHTMLDivElement_h___ */