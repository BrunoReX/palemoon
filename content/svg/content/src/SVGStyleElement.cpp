/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/SVGStyleElement.h"
#include "nsContentUtils.h"
#include "nsStubMutationObserver.h"
#include "mozilla/dom/SVGStyleElementBinding.h"

DOMCI_NODE_DATA(SVGStyleElement, mozilla::dom::SVGStyleElement)

NS_IMPL_NS_NEW_NAMESPACED_SVG_ELEMENT(Style)

namespace mozilla {
namespace dom {

JSObject*
SVGStyleElement::WrapNode(JSContext *aCx, JSObject *aScope, bool *aTriedToWrap)
{
  return SVGStyleElementBinding::Wrap(aCx, aScope, this, aTriedToWrap);
}

//----------------------------------------------------------------------
// nsISupports methods

NS_IMPL_ADDREF_INHERITED(SVGStyleElement, SVGStyleElementBase)
NS_IMPL_RELEASE_INHERITED(SVGStyleElement, SVGStyleElementBase)

NS_INTERFACE_TABLE_HEAD_CYCLE_COLLECTION_INHERITED(SVGStyleElement)
  NS_NODE_INTERFACE_TABLE7(SVGStyleElement, nsIDOMNode, nsIDOMElement,
                           nsIDOMSVGElement, nsIDOMSVGStyleElement,
                           nsIDOMLinkStyle, nsIStyleSheetLinkingElement,
                           nsIMutationObserver)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(SVGStyleElement)
NS_INTERFACE_MAP_END_INHERITING(SVGStyleElementBase)

NS_IMPL_CYCLE_COLLECTION_CLASS(SVGStyleElement)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(SVGStyleElement,
                                                  SVGStyleElementBase)
  tmp->nsStyleLinkElement::Traverse(cb);
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(SVGStyleElement,
                                                SVGStyleElementBase)
  tmp->nsStyleLinkElement::Unlink();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

//----------------------------------------------------------------------
// Implementation

SVGStyleElement::SVGStyleElement(already_AddRefed<nsINodeInfo> aNodeInfo)
  : SVGStyleElementBase(aNodeInfo)
{
  SetIsDOMBinding();
  AddMutationObserver(this);
}


//----------------------------------------------------------------------
// nsIDOMNode methods


NS_IMPL_ELEMENT_CLONE_WITH_INIT(SVGStyleElement)


//----------------------------------------------------------------------
// nsIContent methods

nsresult
SVGStyleElement::BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                            nsIContent* aBindingParent,
                            bool aCompileEventHandlers)
{
  nsresult rv = SVGStyleElementBase::BindToTree(aDocument, aParent,
                                                aBindingParent,
                                                aCompileEventHandlers);
  NS_ENSURE_SUCCESS(rv, rv);

  void (SVGStyleElement::*update)() = &SVGStyleElement::UpdateStyleSheetInternal;
  nsContentUtils::AddScriptRunner(NS_NewRunnableMethod(this, update));

  return rv;
}

void
SVGStyleElement::UnbindFromTree(bool aDeep, bool aNullParent)
{
  nsCOMPtr<nsIDocument> oldDoc = GetCurrentDoc();

  SVGStyleElementBase::UnbindFromTree(aDeep, aNullParent);
  UpdateStyleSheetInternal(oldDoc);
}

nsresult
SVGStyleElement::SetAttr(int32_t aNameSpaceID, nsIAtom* aName,
                         nsIAtom* aPrefix, const nsAString& aValue,
                         bool aNotify)
{
  nsresult rv = SVGStyleElementBase::SetAttr(aNameSpaceID, aName, aPrefix,
                                             aValue, aNotify);
  if (NS_SUCCEEDED(rv)) {
    UpdateStyleSheetInternal(nullptr,
                             aNameSpaceID == kNameSpaceID_None &&
                             (aName == nsGkAtoms::title ||
                              aName == nsGkAtoms::media ||
                              aName == nsGkAtoms::type));
  }

  return rv;
}

nsresult
SVGStyleElement::UnsetAttr(int32_t aNameSpaceID, nsIAtom* aAttribute,
                           bool aNotify)
{
  nsresult rv = SVGStyleElementBase::UnsetAttr(aNameSpaceID, aAttribute,
                                               aNotify);
  if (NS_SUCCEEDED(rv)) {
    UpdateStyleSheetInternal(nullptr,
                             aNameSpaceID == kNameSpaceID_None &&
                             (aAttribute == nsGkAtoms::title ||
                              aAttribute == nsGkAtoms::media ||
                              aAttribute == nsGkAtoms::type));
  }

  return rv;
}

bool
SVGStyleElement::ParseAttribute(int32_t aNamespaceID,
                                nsIAtom* aAttribute,
                                const nsAString& aValue,
                                nsAttrValue& aResult)
{
  if (aNamespaceID == kNameSpaceID_None &&
      aAttribute == nsGkAtoms::crossorigin) {
    ParseCORSValue(aValue, aResult);
    return true;
  }

  return SVGStyleElementBase::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                             aResult);
}

//----------------------------------------------------------------------
// nsIMutationObserver methods

void
SVGStyleElement::CharacterDataChanged(nsIDocument* aDocument,
                                      nsIContent* aContent,
                                      CharacterDataChangeInfo* aInfo)
{
  ContentChanged(aContent);
}

void
SVGStyleElement::ContentAppended(nsIDocument* aDocument,
                                 nsIContent* aContainer,
                                 nsIContent* aFirstNewContent,
                                 int32_t aNewIndexInContainer)
{
  ContentChanged(aContainer);
}

void
SVGStyleElement::ContentInserted(nsIDocument* aDocument,
                                 nsIContent* aContainer,
                                 nsIContent* aChild,
                                 int32_t aIndexInContainer)
{
  ContentChanged(aChild);
}

void
SVGStyleElement::ContentRemoved(nsIDocument* aDocument,
                                nsIContent* aContainer,
                                nsIContent* aChild,
                                int32_t aIndexInContainer,
                                nsIContent* aPreviousSibling)
{
  ContentChanged(aChild);
}

void
SVGStyleElement::ContentChanged(nsIContent* aContent)
{
  if (nsContentUtils::IsInSameAnonymousTree(this, aContent)) {
    UpdateStyleSheetInternal(nullptr);
  }
}

//----------------------------------------------------------------------
// nsIDOMSVGStyleElement methods

/* attribute DOMString xmlspace; */
NS_IMETHODIMP SVGStyleElement::GetXmlspace(nsAString & aXmlspace)
{
  GetAttr(kNameSpaceID_XML, nsGkAtoms::space, aXmlspace);

  return NS_OK;
}
NS_IMETHODIMP SVGStyleElement::SetXmlspace(const nsAString & aXmlspace)
{
  return SetAttr(kNameSpaceID_XML, nsGkAtoms::space, aXmlspace, true);
}
void
SVGStyleElement::SetXmlspace(const nsAString & aXmlspace, ErrorResult& rv)
{
  rv = SetAttr(kNameSpaceID_XML, nsGkAtoms::space, aXmlspace, true);
}

/* attribute DOMString type; */
NS_IMETHODIMP SVGStyleElement::GetType(nsAString & aType)
{
  GetAttr(kNameSpaceID_None, nsGkAtoms::type, aType);

  return NS_OK;
}
NS_IMETHODIMP SVGStyleElement::SetType(const nsAString & aType)
{
  return SetAttr(kNameSpaceID_None, nsGkAtoms::type, aType, true);
}
void
SVGStyleElement::SetType(const nsAString & aType, ErrorResult& rv)
{
  rv = SetAttr(kNameSpaceID_None, nsGkAtoms::type, aType, true);
}

/* attribute DOMString media; */
NS_IMETHODIMP SVGStyleElement::GetMedia(nsAString & aMedia)
{
  GetAttr(kNameSpaceID_None, nsGkAtoms::media, aMedia);

  return NS_OK;
}
NS_IMETHODIMP SVGStyleElement::SetMedia(const nsAString & aMedia)
{
  return SetAttr(kNameSpaceID_None, nsGkAtoms::media, aMedia, true);
}
void
SVGStyleElement::SetMedia(const nsAString & aMedia, ErrorResult& rv)
{
  rv = SetAttr(kNameSpaceID_None, nsGkAtoms::media, aMedia, true);
}

/* attribute DOMString title; */
NS_IMETHODIMP SVGStyleElement::GetTitle(nsAString & aTitle)
{
  GetAttr(kNameSpaceID_None, nsGkAtoms::title, aTitle);

  return NS_OK;
}
NS_IMETHODIMP SVGStyleElement::SetTitle(const nsAString & aTitle)
{
  return SetAttr(kNameSpaceID_None, nsGkAtoms::title, aTitle, true);
}
void
SVGStyleElement::SetTitle(const nsAString & aTitle, ErrorResult& rv)
{
  rv = SetAttr(kNameSpaceID_None, nsGkAtoms::title, aTitle, true);
}

//----------------------------------------------------------------------
// nsStyleLinkElement methods

already_AddRefed<nsIURI>
SVGStyleElement::GetStyleSheetURL(bool* aIsInline)
{
  *aIsInline = true;
  return nullptr;
}

void
SVGStyleElement::GetStyleSheetInfo(nsAString& aTitle,
                                   nsAString& aType,
                                   nsAString& aMedia,
                                   bool* aIsAlternate)
{
  *aIsAlternate = false;

  nsAutoString title;
  GetAttr(kNameSpaceID_None, nsGkAtoms::title, title);
  title.CompressWhitespace();
  aTitle.Assign(title);

  GetAttr(kNameSpaceID_None, nsGkAtoms::media, aMedia);
  // The SVG spec is formulated in terms of the CSS2 spec,
  // which specifies that media queries are case insensitive.
  nsContentUtils::ASCIIToLower(aMedia);

  GetAttr(kNameSpaceID_None, nsGkAtoms::type, aType);
  if (aType.IsEmpty()) {
    aType.AssignLiteral("text/css");
  }

  return;
}

CORSMode
SVGStyleElement::GetCORSMode() const
{
  return AttrValueToCORSMode(GetParsedAttr(nsGkAtoms::crossorigin));
}

} // namespace dom
} // namespace mozilla

