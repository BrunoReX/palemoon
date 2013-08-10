/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef mozilla_dom_HTMLTableElement_h
#define mozilla_dom_HTMLTableElement_h

#include "nsGenericHTMLElement.h"
#include "nsIDOMHTMLTableElement.h"
#include "mozilla/dom/HTMLTableCaptionElement.h"
#include "mozilla/dom/HTMLTableSectionElement.h"
#include "nsMappedAttributes.h"

namespace mozilla {
namespace dom {

#define TABLE_ATTRS_DIRTY ((nsMappedAttributes*)0x1)

class TableRowsCollection;

class HTMLTableElement : public nsGenericHTMLElement,
                         public nsIDOMHTMLTableElement
{
public:
  HTMLTableElement(already_AddRefed<nsINodeInfo> aNodeInfo);
  virtual ~HTMLTableElement();

  NS_IMPL_FROMCONTENT_HTML_WITH_TAG(HTMLTableElement, table)

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  // nsIDOMNode
  NS_FORWARD_NSIDOMNODE_TO_NSINODE

  // nsIDOMElement
  NS_FORWARD_NSIDOMELEMENT_TO_GENERIC

  // nsIDOMHTMLElement
  NS_FORWARD_NSIDOMHTMLELEMENT_TO_GENERIC

  // nsIDOMHTMLTableElement
  NS_DECL_NSIDOMHTMLTABLEELEMENT

  HTMLTableCaptionElement* GetCaption() const
  {
    return static_cast<HTMLTableCaptionElement*>(GetChild(nsGkAtoms::caption));
  }
  void SetCaption(HTMLTableCaptionElement* aCaption)
  {
    DeleteCaption();
    if (aCaption) {
      mozilla::ErrorResult rv;
      nsINode::AppendChild(*aCaption, rv);
    }
  }
  already_AddRefed<nsGenericHTMLElement> CreateCaption();

  HTMLTableSectionElement* GetTHead() const
  {
    return static_cast<HTMLTableSectionElement*>(GetChild(nsGkAtoms::thead));
  }
  void SetTHead(HTMLTableSectionElement* aTHead, ErrorResult& aError)
  {
    if (aTHead && !aTHead->IsHTML(nsGkAtoms::thead)) {
      aError.Throw(NS_ERROR_DOM_HIERARCHY_REQUEST_ERR);
      return;
    }

    DeleteTHead();
    if (aTHead) {
      nsINode::InsertBefore(*aTHead, nsINode::GetFirstChild(), aError);
    }
  }
  already_AddRefed<nsGenericHTMLElement> CreateTHead();

  HTMLTableSectionElement* GetTFoot() const
  {
    return static_cast<HTMLTableSectionElement*>(GetChild(nsGkAtoms::tfoot));
  }
  void SetTFoot(HTMLTableSectionElement* aTFoot, ErrorResult& aError)
  {
    if (aTFoot && !aTFoot->IsHTML(nsGkAtoms::tfoot)) {
      aError.Throw(NS_ERROR_DOM_HIERARCHY_REQUEST_ERR);
      return;
    }

    DeleteTFoot();
    if (aTFoot) {
      nsINode::AppendChild(*aTFoot, aError);
    }
  }
  already_AddRefed<nsGenericHTMLElement> CreateTFoot();

  nsIHTMLCollection* TBodies();
  nsIHTMLCollection* Rows();

  already_AddRefed<nsGenericHTMLElement> InsertRow(int32_t aIndex,
                                                   ErrorResult& aError);
  void DeleteRow(int32_t aIndex, ErrorResult& aError);

  void GetAlign(nsString& aAlign)
  {
    GetHTMLAttr(nsGkAtoms::align, aAlign);
  }
  void SetAlign(const nsAString& aAlign, ErrorResult& aError)
  {
    SetHTMLAttr(nsGkAtoms::align, aAlign, aError);
  }
  void GetBorder(nsString& aBorder)
  {
    GetHTMLAttr(nsGkAtoms::border, aBorder);
  }
  void SetBorder(const nsAString& aBorder, ErrorResult& aError)
  {
    SetHTMLAttr(nsGkAtoms::border, aBorder, aError);
  }
  void GetFrame(nsString& aFrame)
  {
    GetHTMLAttr(nsGkAtoms::frame, aFrame);
  }
  void SetFrame(const nsAString& aFrame, ErrorResult& aError)
  {
    SetHTMLAttr(nsGkAtoms::frame, aFrame, aError);
  }
  void GetRules(nsString& aRules)
  {
    GetHTMLAttr(nsGkAtoms::rules, aRules);
  }
  void SetRules(const nsAString& aRules, ErrorResult& aError)
  {
    SetHTMLAttr(nsGkAtoms::rules, aRules, aError);
  }
  void GetSummary(nsString& aSummary)
  {
    GetHTMLAttr(nsGkAtoms::summary, aSummary);
  }
  void SetSummary(const nsAString& aSummary, ErrorResult& aError)
  {
    SetHTMLAttr(nsGkAtoms::summary, aSummary, aError);
  }
  void GetWidth(nsString& aWidth)
  {
    GetHTMLAttr(nsGkAtoms::width, aWidth);
  }
  void SetWidth(const nsAString& aWidth, ErrorResult& aError)
  {
    SetHTMLAttr(nsGkAtoms::width, aWidth, aError);
  }
  void GetBgColor(nsString& aBgColor)
  {
    GetHTMLAttr(nsGkAtoms::bgcolor, aBgColor);
  }
  void SetBgColor(const nsAString& aBgColor, ErrorResult& aError)
  {
    SetHTMLAttr(nsGkAtoms::bgcolor, aBgColor, aError);
  }
  void GetCellPadding(nsString& aCellPadding)
  {
    GetHTMLAttr(nsGkAtoms::cellpadding, aCellPadding);
  }
  void SetCellPadding(const nsAString& aCellPadding, ErrorResult& aError)
  {
    SetHTMLAttr(nsGkAtoms::cellpadding, aCellPadding, aError);
  }
  void GetCellSpacing(nsString& aCellSpacing)
  {
    GetHTMLAttr(nsGkAtoms::cellspacing, aCellSpacing);
  }
  void SetCellSpacing(const nsAString& aCellSpacing, ErrorResult& aError)
  {
    SetHTMLAttr(nsGkAtoms::cellspacing, aCellSpacing, aError);
  }

  virtual bool ParseAttribute(int32_t aNamespaceID,
                                nsIAtom* aAttribute,
                                const nsAString& aValue,
                                nsAttrValue& aResult);
  virtual nsMapRuleToAttributesFunc GetAttributeMappingFunction() const;
  NS_IMETHOD_(bool) IsAttributeMapped(const nsIAtom* aAttribute) const;

  virtual nsresult Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const;

  virtual nsXPCClassInfo* GetClassInfo();
  virtual nsIDOMNode* AsDOMNode() { return this; }
  virtual nsresult BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                              nsIContent* aBindingParent,
                              bool aCompileEventHandlers);
  virtual void UnbindFromTree(bool aDeep = true,
                              bool aNullParent = true);
  /**
   * Called when an attribute is about to be changed
   */
  virtual nsresult BeforeSetAttr(int32_t aNameSpaceID, nsIAtom* aName,
                                 const nsAttrValueOrString* aValue,
                                 bool aNotify);
  /**
   * Called when an attribute has just been changed
   */
  virtual nsresult AfterSetAttr(int32_t aNameSpaceID, nsIAtom* aName,
                                const nsAttrValue* aValue, bool aNotify);

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(HTMLTableElement,
                                           nsGenericHTMLElement)
  nsMappedAttributes* GetAttributesMappedForCell();

protected:
  virtual JSObject* WrapNode(JSContext *aCx, JSObject *aScope,
                             bool *aTriedToWrap) MOZ_OVERRIDE;

  nsIContent* GetChild(nsIAtom *aTag) const
  {
    for (nsIContent* cur = nsINode::GetFirstChild(); cur;
         cur = cur->GetNextSibling()) {
      if (cur->IsHTML(aTag)) {
        return cur;
      }
    }
    return nullptr;
  }

  nsRefPtr<nsContentList> mTBodies;
  nsRefPtr<TableRowsCollection> mRows;
  // Sentinel value of TABLE_ATTRS_DIRTY indicates that this is dirty and needs
  // to be recalculated.
  nsMappedAttributes *mTableInheritedAttributes;
  void BuildInheritedAttributes();
  void ReleaseInheritedAttributes() {
    if (mTableInheritedAttributes &&
        mTableInheritedAttributes != TABLE_ATTRS_DIRTY)
      NS_RELEASE(mTableInheritedAttributes);
      mTableInheritedAttributes = TABLE_ATTRS_DIRTY;
  }
};

} // namespace dom
} // namespace mozilla

#endif /* mozilla_dom_HTMLTableElement_h */
