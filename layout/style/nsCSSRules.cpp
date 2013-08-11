/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1999
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Boris Zbarsky <bzbarsky@mit.edu>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

/* rules in a CSS stylesheet other than style rules (e.g., @import rules) */

#include "nsCSSRules.h"
#include "nsCSSValue.h"
#include "nsICSSImportRule.h"
#include "nsICSSNameSpaceRule.h"

#include "nsString.h"
#include "nsIAtom.h"
#include "nsIURL.h"

#include "nsCSSRule.h"
#include "nsCSSProps.h"
#include "nsICSSStyleSheet.h"

#include "nsCOMPtr.h"
#include "nsIDOMCSSStyleSheet.h"
#include "nsIDOMCSSRule.h"
#include "nsIDOMCSSImportRule.h"
#include "nsIDOMCSSMediaRule.h"
#include "nsIDOMCSSMozDocumentRule.h"
#include "nsIDOMCSSCharsetRule.h"
#include "nsIDOMCSSStyleDeclaration.h"
#include "nsIMediaList.h"
#include "nsIDOMMediaList.h"
#include "nsICSSRuleList.h"
#include "nsIDOMStyleSheet.h"
#include "nsIDocument.h"
#include "nsPresContext.h"

#include "nsContentUtils.h"
#include "nsStyleConsts.h"
#include "nsDOMError.h"
#include "nsStyleUtil.h"
#include "nsCSSDeclaration.h"

#define IMPL_STYLE_RULE_INHERIT(_class, super) \
NS_IMETHODIMP _class::GetStyleSheet(nsIStyleSheet*& aSheet) const { return super::GetStyleSheet(aSheet); }  \
NS_IMETHODIMP _class::SetStyleSheet(nsICSSStyleSheet* aSheet) { return super::SetStyleSheet(aSheet); }  \
NS_IMETHODIMP _class::SetParentRule(nsICSSGroupRule* aRule) { return super::SetParentRule(aRule); }  \
nsIDOMCSSRule* _class::GetDOMRuleWeak(nsresult *aResult) { *aResult = NS_OK; return this; }  \
NS_IMETHODIMP _class::MapRuleInfoInto(nsRuleData* aRuleData) { return NS_OK; } 

#define IMPL_STYLE_RULE_INHERIT2(_class, super) \
NS_IMETHODIMP _class::GetStyleSheet(nsIStyleSheet*& aSheet) const { return super::GetStyleSheet(aSheet); }  \
NS_IMETHODIMP _class::SetParentRule(nsICSSGroupRule* aRule) { return super::SetParentRule(aRule); }  \
NS_IMETHODIMP _class::MapRuleInfoInto(nsRuleData* aRuleData) { return NS_OK; } 

// -------------------------------
// Style Rule List for group rules
//
class CSSGroupRuleRuleListImpl : public nsICSSRuleList
{
public:
  CSSGroupRuleRuleListImpl(nsICSSGroupRule *aGroupRule);

  NS_DECL_ISUPPORTS

  NS_DECL_NSIDOMCSSRULELIST

  virtual nsIDOMCSSRule* GetItemAt(PRUint32 aIndex, nsresult* aResult);

  void DropReference() { mGroupRule = nsnull; }

protected:
  virtual ~CSSGroupRuleRuleListImpl(void);

private:
  nsICSSGroupRule* mGroupRule;
};

CSSGroupRuleRuleListImpl::CSSGroupRuleRuleListImpl(nsICSSGroupRule *aGroupRule)
{
  // Not reference counted to avoid circular references.
  // The rule will tell us when its going away.
  mGroupRule = aGroupRule;
}

CSSGroupRuleRuleListImpl::~CSSGroupRuleRuleListImpl()
{
}

// QueryInterface implementation for CSSGroupRuleRuleList
NS_INTERFACE_MAP_BEGIN(CSSGroupRuleRuleListImpl)
  NS_INTERFACE_MAP_ENTRY(nsICSSRuleList)
  NS_INTERFACE_MAP_ENTRY(nsIDOMCSSRuleList)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY_CONTENT_CLASSINFO(CSSGroupRuleRuleList)
NS_INTERFACE_MAP_END


NS_IMPL_ADDREF(CSSGroupRuleRuleListImpl)
NS_IMPL_RELEASE(CSSGroupRuleRuleListImpl)

NS_IMETHODIMP    
CSSGroupRuleRuleListImpl::GetLength(PRUint32* aLength)
{
  if (mGroupRule) {
    PRInt32 count;
    mGroupRule->StyleRuleCount(count);
    *aLength = (PRUint32)count;
  } else {
    *aLength = 0;
  }

  return NS_OK;
}

nsIDOMCSSRule*    
CSSGroupRuleRuleListImpl::GetItemAt(PRUint32 aIndex, nsresult* aResult)
{
  nsresult result = NS_OK;

  if (mGroupRule) {
    nsCOMPtr<nsICSSRule> rule;

    result = mGroupRule->GetStyleRuleAt(aIndex, *getter_AddRefs(rule));
    if (rule) {
      return rule->GetDOMRuleWeak(aResult);
    }
    if (result == NS_ERROR_ILLEGAL_VALUE) {
      result = NS_OK; // per spec: "Return Value ... null if ... not a valid index."
    }
  }

  *aResult = result;

  return nsnull;
}

NS_IMETHODIMP    
CSSGroupRuleRuleListImpl::Item(PRUint32 aIndex, nsIDOMCSSRule** aReturn)
{
  nsresult rv;
  nsIDOMCSSRule* rule = GetItemAt(aIndex, &rv);
  if (!rule) {
    *aReturn = nsnull;
    return rv;
  }

  return CallQueryInterface(rule, aReturn);
}

// -------------------------------------------
// CharsetRule
//
class CSSCharsetRuleImpl : public nsCSSRule,
                           public nsICSSRule,
                           public nsIDOMCSSCharsetRule
{
public:
  CSSCharsetRuleImpl(const nsAString& aEncoding);
  CSSCharsetRuleImpl(const CSSCharsetRuleImpl& aCopy);
  virtual ~CSSCharsetRuleImpl(void);

  NS_DECL_ISUPPORTS_INHERITED

  DECL_STYLE_RULE_INHERIT

  // nsIStyleRule methods
#ifdef DEBUG
  NS_IMETHOD List(FILE* out = stdout, PRInt32 aIndent = 0) const;
#endif

  // nsICSSRule methods
  NS_IMETHOD GetType(PRInt32& aType) const;
  NS_IMETHOD Clone(nsICSSRule*& aClone) const;

  // nsIDOMCSSRule interface
  NS_DECL_NSIDOMCSSRULE
  
  // nsIDOMCSSCharsetRule methods
  NS_IMETHOD GetEncoding(nsAString& aEncoding);
  NS_IMETHOD SetEncoding(const nsAString& aEncoding);

protected:
  nsString  mEncoding;
};

CSSCharsetRuleImpl::CSSCharsetRuleImpl(const nsAString& aEncoding)
  : nsCSSRule(),
    mEncoding(aEncoding)
{
}

CSSCharsetRuleImpl::CSSCharsetRuleImpl(const CSSCharsetRuleImpl& aCopy)
  : nsCSSRule(aCopy),
    mEncoding(aCopy.mEncoding)
{
}

CSSCharsetRuleImpl::~CSSCharsetRuleImpl(void)
{
}

NS_IMPL_ADDREF_INHERITED(CSSCharsetRuleImpl, nsCSSRule)
NS_IMPL_RELEASE_INHERITED(CSSCharsetRuleImpl, nsCSSRule)

// QueryInterface implementation for CSSCharsetRuleImpl
NS_INTERFACE_MAP_BEGIN(CSSCharsetRuleImpl)
  NS_INTERFACE_MAP_ENTRY(nsICSSRule)
  NS_INTERFACE_MAP_ENTRY(nsIStyleRule)
  NS_INTERFACE_MAP_ENTRY(nsIDOMCSSRule)
  NS_INTERFACE_MAP_ENTRY(nsIDOMCSSCharsetRule)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsICSSRule)
  NS_INTERFACE_MAP_ENTRY_CONTENT_CLASSINFO(CSSCharsetRule)
NS_INTERFACE_MAP_END

IMPL_STYLE_RULE_INHERIT(CSSCharsetRuleImpl, nsCSSRule)

#ifdef DEBUG
NS_IMETHODIMP
CSSCharsetRuleImpl::List(FILE* out, PRInt32 aIndent) const
{
  // Indent
  for (PRInt32 indent = aIndent; --indent >= 0; ) fputs("  ", out);

  fputs("@charset \"", out);
  fputs(NS_LossyConvertUTF16toASCII(mEncoding).get(), out);
  fputs("\"\n", out);

  return NS_OK;
}
#endif

NS_IMETHODIMP
CSSCharsetRuleImpl::GetType(PRInt32& aType) const
{
  aType = nsICSSRule::CHARSET_RULE;
  return NS_OK;
}

NS_IMETHODIMP
CSSCharsetRuleImpl::Clone(nsICSSRule*& aClone) const
{
  CSSCharsetRuleImpl* clone = new CSSCharsetRuleImpl(*this);
  if (clone) {
    return CallQueryInterface(clone, &aClone);
  }
  aClone = nsnull;
  return NS_ERROR_OUT_OF_MEMORY;
}

NS_IMETHODIMP
CSSCharsetRuleImpl::GetEncoding(nsAString& aEncoding)
{
  aEncoding = mEncoding;
  return NS_OK;
}

NS_IMETHODIMP
CSSCharsetRuleImpl::SetEncoding(const nsAString& aEncoding)
{
  mEncoding = aEncoding;
  return NS_OK;
}


nsresult
NS_NewCSSCharsetRule(nsICSSRule** aInstancePtrResult, const nsAString& aEncoding)
{
  if (! aInstancePtrResult) {
    return NS_ERROR_NULL_POINTER;
  }

  CSSCharsetRuleImpl* it = new CSSCharsetRuleImpl(aEncoding);

  if (! it) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  return CallQueryInterface(it, aInstancePtrResult);
}

NS_IMETHODIMP
CSSCharsetRuleImpl::GetType(PRUint16* aType)
{
  *aType = nsIDOMCSSRule::CHARSET_RULE;
  return NS_OK;
}

NS_IMETHODIMP
CSSCharsetRuleImpl::GetCssText(nsAString& aCssText)
{
  aCssText.AssignLiteral("@charset \"");
  aCssText.Append(mEncoding);
  aCssText.AppendLiteral("\";");
  return NS_OK;
}

NS_IMETHODIMP
CSSCharsetRuleImpl::SetCssText(const nsAString& aCssText)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
CSSCharsetRuleImpl::GetParentStyleSheet(nsIDOMCSSStyleSheet** aSheet)
{
  NS_ENSURE_ARG_POINTER(aSheet);

  if (mSheet) {
    return CallQueryInterface(mSheet, aSheet);
  }
  *aSheet = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
CSSCharsetRuleImpl::GetParentRule(nsIDOMCSSRule** aParentRule)
{
  if (mParentRule) {
    return mParentRule->GetDOMRule(aParentRule);
  }
  *aParentRule = nsnull;
  return NS_OK;
}



// -------------------------------------------
// nsICSSImportRule
//
class CSSImportRuleImpl : public nsCSSRule,
                          public nsICSSImportRule,
                          public nsIDOMCSSImportRule
{
public:
  CSSImportRuleImpl(nsMediaList* aMedia);
  CSSImportRuleImpl(const CSSImportRuleImpl& aCopy);
  virtual ~CSSImportRuleImpl(void);

  NS_DECL_ISUPPORTS_INHERITED

  DECL_STYLE_RULE_INHERIT

  // nsIStyleRule methods
#ifdef DEBUG
  NS_IMETHOD List(FILE* out = stdout, PRInt32 aIndent = 0) const;
#endif

  // nsICSSRule methods
  NS_IMETHOD GetType(PRInt32& aType) const;
  NS_IMETHOD Clone(nsICSSRule*& aClone) const;

  // nsICSSImportRule methods
  NS_IMETHOD SetURLSpec(const nsString& aURLSpec);
  NS_IMETHOD GetURLSpec(nsString& aURLSpec) const;

  NS_IMETHOD SetMedia(const nsString& aMedia);
  NS_IMETHOD GetMedia(nsString& aMedia) const;

  NS_IMETHOD SetSheet(nsICSSStyleSheet*);
  
  // nsIDOMCSSRule interface
  NS_DECL_NSIDOMCSSRULE

  // nsIDOMCSSImportRule interface
  NS_DECL_NSIDOMCSSIMPORTRULE

protected:
  nsString  mURLSpec;
  nsRefPtr<nsMediaList> mMedia;
  nsCOMPtr<nsICSSStyleSheet> mChildSheet;
};

CSSImportRuleImpl::CSSImportRuleImpl(nsMediaList* aMedia)
  : nsCSSRule()
  , mURLSpec()
  , mMedia(aMedia)
{
  // XXXbz This is really silly.... the mMedia here will be replaced
  // with itself if we manage to load a sheet.  Which should really
  // never fail nowadays, in sane cases.
}

CSSImportRuleImpl::CSSImportRuleImpl(const CSSImportRuleImpl& aCopy)
  : nsCSSRule(aCopy),
    mURLSpec(aCopy.mURLSpec)
{
  nsCOMPtr<nsICSSStyleSheet> sheet;
  if (aCopy.mChildSheet) {
    aCopy.mChildSheet->Clone(nsnull, this, nsnull, nsnull,
                             getter_AddRefs(sheet));
  }
  SetSheet(sheet);
  // SetSheet sets mMedia appropriately
}

CSSImportRuleImpl::~CSSImportRuleImpl(void)
{
  if (mChildSheet) {
    mChildSheet->SetOwnerRule(nsnull);
  }
}

NS_IMPL_ADDREF_INHERITED(CSSImportRuleImpl, nsCSSRule)
NS_IMPL_RELEASE_INHERITED(CSSImportRuleImpl, nsCSSRule)

// QueryInterface implementation for CSSImportRuleImpl
NS_INTERFACE_MAP_BEGIN(CSSImportRuleImpl)
  NS_INTERFACE_MAP_ENTRY(nsICSSImportRule)
  NS_INTERFACE_MAP_ENTRY(nsICSSRule)
  NS_INTERFACE_MAP_ENTRY(nsIStyleRule)
  NS_INTERFACE_MAP_ENTRY(nsIDOMCSSRule)
  NS_INTERFACE_MAP_ENTRY(nsIDOMCSSImportRule)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsICSSImportRule)
  NS_INTERFACE_MAP_ENTRY_CONTENT_CLASSINFO(CSSImportRule)
NS_INTERFACE_MAP_END

IMPL_STYLE_RULE_INHERIT(CSSImportRuleImpl, nsCSSRule)

#ifdef DEBUG
NS_IMETHODIMP
CSSImportRuleImpl::List(FILE* out, PRInt32 aIndent) const
{
  // Indent
  for (PRInt32 indent = aIndent; --indent >= 0; ) fputs("  ", out);

  fputs("@import \"", out);
  fputs(NS_LossyConvertUTF16toASCII(mURLSpec).get(), out);
  fputs("\" ", out);

  nsAutoString mediaText;
  mMedia->GetText(mediaText);
  fputs(NS_LossyConvertUTF16toASCII(mediaText).get(), out);
  fputs("\n", out);

  return NS_OK;
}
#endif

NS_IMETHODIMP
CSSImportRuleImpl::GetType(PRInt32& aType) const
{
  aType = nsICSSRule::IMPORT_RULE;
  return NS_OK;
}

NS_IMETHODIMP
CSSImportRuleImpl::Clone(nsICSSRule*& aClone) const
{
  CSSImportRuleImpl* clone = new CSSImportRuleImpl(*this);
  if (clone) {
    return CallQueryInterface(clone, &aClone);
  }
  aClone = nsnull;
  return NS_ERROR_OUT_OF_MEMORY;
}

NS_IMETHODIMP
CSSImportRuleImpl::SetURLSpec(const nsString& aURLSpec)
{
  mURLSpec = aURLSpec;
  return NS_OK;
}

NS_IMETHODIMP
CSSImportRuleImpl::GetURLSpec(nsString& aURLSpec) const
{
  aURLSpec = mURLSpec;
  return NS_OK;
}

NS_IMETHODIMP
CSSImportRuleImpl::SetMedia(const nsString& aMedia)
{
  if (mMedia) {
    return mMedia->SetText(aMedia);
  } else {
    return NS_OK;
  }
}

NS_IMETHODIMP
CSSImportRuleImpl::GetMedia(nsString& aMedia) const
{
  if (mMedia) {
    return mMedia->GetText(aMedia);
  } else {
    aMedia.Truncate();
    return NS_OK;
  }
}

NS_IMETHODIMP
CSSImportRuleImpl::SetSheet(nsICSSStyleSheet* aSheet)
{
  nsresult rv;
  NS_ENSURE_ARG_POINTER(aSheet);
  
  // set the new sheet
  mChildSheet = aSheet;
  aSheet->SetOwnerRule(this);

  // set our medialist to be the same as the sheet's medialist
  nsCOMPtr<nsIDOMStyleSheet> sheet(do_QueryInterface(mChildSheet, &rv));
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMPtr<nsIDOMMediaList> mediaList;
  rv = sheet->GetMedia(getter_AddRefs(mediaList));
  NS_ENSURE_SUCCESS(rv, rv);
  mMedia = static_cast<nsMediaList*>(mediaList.get());
  
  return NS_OK;
}

nsresult
NS_NewCSSImportRule(nsICSSImportRule** aInstancePtrResult, 
                    const nsString& aURLSpec,
                    nsMediaList* aMedia)
{
  NS_ENSURE_ARG_POINTER(aInstancePtrResult);

  CSSImportRuleImpl* it = new CSSImportRuleImpl(aMedia);

  if (! it) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  it->SetURLSpec(aURLSpec);
  return CallQueryInterface(it, aInstancePtrResult);
}

NS_IMETHODIMP
CSSImportRuleImpl::GetType(PRUint16* aType)
{
  NS_ENSURE_ARG_POINTER(aType);
  *aType = nsIDOMCSSRule::IMPORT_RULE;
  return NS_OK;
}

NS_IMETHODIMP
CSSImportRuleImpl::GetCssText(nsAString& aCssText)
{
  aCssText.AssignLiteral("@import url(");
  nsStyleUtil::AppendEscapedCSSString(mURLSpec, aCssText);
  aCssText.Append(NS_LITERAL_STRING(")"));
  if (mMedia) {
    nsAutoString mediaText;
    mMedia->GetText(mediaText);
    if (!mediaText.IsEmpty()) {
      aCssText.AppendLiteral(" ");
      aCssText.Append(mediaText);
    }
  }
  aCssText.AppendLiteral(";");
  return NS_OK;
}

NS_IMETHODIMP
CSSImportRuleImpl::SetCssText(const nsAString& aCssText)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
CSSImportRuleImpl::GetParentStyleSheet(nsIDOMCSSStyleSheet** aSheet)
{
  NS_ENSURE_ARG_POINTER(aSheet);
  if (mSheet) {
    return CallQueryInterface(mSheet, aSheet);
  }
  *aSheet = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
CSSImportRuleImpl::GetParentRule(nsIDOMCSSRule** aParentRule)
{
  if (mParentRule) {
    return mParentRule->GetDOMRule(aParentRule);
  }
  *aParentRule = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
CSSImportRuleImpl::GetHref(nsAString & aHref)
{
  aHref = mURLSpec;
  return NS_OK;
}

NS_IMETHODIMP
CSSImportRuleImpl::GetMedia(nsIDOMMediaList * *aMedia)
{
  NS_ENSURE_ARG_POINTER(aMedia);
  if (!mMedia) {
    *aMedia = nsnull;
    return NS_OK;
  }

  return CallQueryInterface(mMedia.get(), aMedia);
}

NS_IMETHODIMP
CSSImportRuleImpl::GetStyleSheet(nsIDOMCSSStyleSheet * *aStyleSheet)
{
  NS_ENSURE_ARG_POINTER(aStyleSheet);
  if (!mChildSheet) {
    *aStyleSheet = nsnull;
    return NS_OK;
  }

  return CallQueryInterface(mChildSheet, aStyleSheet);
}

nsCSSGroupRule::nsCSSGroupRule()
  : nsCSSRule()
  , mRuleCollection(nsnull)
{
}

static PRBool
CloneRuleInto(nsICSSRule* aRule, void* aArray)
{
  nsICSSRule* clone = nsnull;
  aRule->Clone(clone);
  if (clone) {
    static_cast<nsCOMArray<nsICSSRule>*>(aArray)->AppendObject(clone);
    NS_RELEASE(clone);
  }
  return PR_TRUE;
}

static PRBool
SetParentRuleReference(nsICSSRule* aRule, void* aParentRule)
{
  nsCSSGroupRule* parentRule = static_cast<nsCSSGroupRule*>(aParentRule);
  aRule->SetParentRule(parentRule);
  return PR_TRUE;
}

nsCSSGroupRule::nsCSSGroupRule(const nsCSSGroupRule& aCopy)
  : nsCSSRule(aCopy)
  , mRuleCollection(nsnull) // lazily constructed
{
  const_cast<nsCSSGroupRule&>(aCopy).mRules.EnumerateForwards(CloneRuleInto, &mRules);
  mRules.EnumerateForwards(SetParentRuleReference, this);
}

nsCSSGroupRule::~nsCSSGroupRule()
{
  mRules.EnumerateForwards(SetParentRuleReference, nsnull);
  if (mRuleCollection) {
    mRuleCollection->DropReference();
    NS_RELEASE(mRuleCollection);
  }
}

IMPL_STYLE_RULE_INHERIT2(nsCSSGroupRule, nsCSSRule)

static PRBool
SetStyleSheetReference(nsICSSRule* aRule, void* aSheet)
{
  nsICSSStyleSheet* sheet = (nsICSSStyleSheet*)aSheet;
  aRule->SetStyleSheet(sheet);
  return PR_TRUE;
}

NS_IMETHODIMP
nsCSSGroupRule::SetStyleSheet(nsICSSStyleSheet* aSheet)
{
  mRules.EnumerateForwards(SetStyleSheetReference, aSheet);
  return nsCSSRule::SetStyleSheet(aSheet);
}

#ifdef DEBUG
NS_IMETHODIMP
nsCSSGroupRule::List(FILE* out, PRInt32 aIndent) const
{
  fputs(" {\n", out);

  for (PRInt32 index = 0, count = mRules.Count(); index < count; ++index) {
    mRules.ObjectAt(index)->List(out, aIndent + 1);
  }
  fputs("}\n", out);
  return NS_OK;
}
#endif

NS_IMETHODIMP
nsCSSGroupRule::AppendStyleRule(nsICSSRule* aRule)
{
  mRules.AppendObject(aRule);
  aRule->SetStyleSheet(mSheet);
  aRule->SetParentRule(this);
  if (mSheet) {
    // XXXldb Shouldn't we be using |WillDirty| and |DidDirty| (and
    // shouldn't |SetModified| be removed?
    mSheet->SetModified(PR_TRUE);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsCSSGroupRule::StyleRuleCount(PRInt32& aCount) const
{
  aCount = mRules.Count();
  return NS_OK;
}

NS_IMETHODIMP
nsCSSGroupRule::GetStyleRuleAt(PRInt32 aIndex, nsICSSRule*& aRule) const
{
  if (aIndex < 0 || aIndex >= mRules.Count()) {
    aRule = nsnull;
    return NS_ERROR_ILLEGAL_VALUE;
  }

  NS_ADDREF(aRule = mRules.ObjectAt(aIndex));
  return NS_OK;
}

NS_IMETHODIMP_(PRBool)
nsCSSGroupRule::EnumerateRulesForwards(RuleEnumFunc aFunc, void * aData) const
{
  return
    const_cast<nsCSSGroupRule*>(this)->mRules.EnumerateForwards(aFunc, aData);
}

/*
 * The next two methods (DeleteStyleRuleAt and InsertStyleRulesAt)
 * should never be called unless you have first called WillDirty() on
 * the parents tylesheet.  After they are called, DidDirty() needs to
 * be called on the sheet
 */
NS_IMETHODIMP
nsCSSGroupRule::DeleteStyleRuleAt(PRUint32 aIndex)
{
  nsICSSRule* rule = mRules.SafeObjectAt(aIndex);
  if (rule) {
    rule->SetStyleSheet(nsnull);
    rule->SetParentRule(nsnull);
  }
  return mRules.RemoveObjectAt(aIndex) ? NS_OK : NS_ERROR_ILLEGAL_VALUE;
}

NS_IMETHODIMP
nsCSSGroupRule::InsertStyleRulesAt(PRUint32 aIndex,
                                   nsCOMArray<nsICSSRule>& aRules)
{
  aRules.EnumerateForwards(SetStyleSheetReference, mSheet);
  aRules.EnumerateForwards(SetParentRuleReference, this);
  if (! mRules.InsertObjectsAt(aRules, aIndex)) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsCSSGroupRule::ReplaceStyleRule(nsICSSRule* aOld, nsICSSRule* aNew)
{
  PRInt32 index = mRules.IndexOf(aOld);
  NS_ENSURE_TRUE(index != -1, NS_ERROR_UNEXPECTED);
  mRules.ReplaceObjectAt(aNew, index);
  aNew->SetStyleSheet(mSheet);
  aNew->SetParentRule(this);
  aOld->SetStyleSheet(nsnull);
  aOld->SetParentRule(nsnull);
  return NS_OK;
}

nsresult
nsCSSGroupRule::AppendRulesToCssText(nsAString& aCssText)
{
  aCssText.AppendLiteral(" {\n");

  // get all the rules
  for (PRInt32 index = 0, count = mRules.Count(); index < count; ++index) {
    nsICSSRule* rule = mRules.ObjectAt(index);
    nsCOMPtr<nsIDOMCSSRule> domRule;
    rule->GetDOMRule(getter_AddRefs(domRule));
    if (domRule) {
      nsAutoString cssText;
      domRule->GetCssText(cssText);
      aCssText.Append(NS_LITERAL_STRING("  ") +
                      cssText +
                      NS_LITERAL_STRING("\n"));
    }
  }

  aCssText.AppendLiteral("}");
  
  return NS_OK;
}

nsresult
nsCSSGroupRule::GetParentStyleSheet(nsIDOMCSSStyleSheet** aSheet)
{
  if (mSheet) {
    return CallQueryInterface(mSheet, aSheet);
  }
  *aSheet = nsnull;
  return NS_OK;
}

nsresult
nsCSSGroupRule::GetParentRule(nsIDOMCSSRule** aParentRule)
{
  if (mParentRule) {
    return mParentRule->GetDOMRule(aParentRule);
  }
  *aParentRule = nsnull;
  return NS_OK;
}

// nsIDOMCSSMediaRule or nsIDOMCSSMozDocumentRule methods
nsresult
nsCSSGroupRule::GetCssRules(nsIDOMCSSRuleList* *aRuleList)
{
  if (!mRuleCollection) {
    mRuleCollection = new CSSGroupRuleRuleListImpl(this);
    if (!mRuleCollection) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
    NS_ADDREF(mRuleCollection);
  }

  return CallQueryInterface(mRuleCollection, aRuleList);
}

nsresult
nsCSSGroupRule::InsertRule(const nsAString & aRule, PRUint32 aIndex, PRUint32* _retval)
{
  NS_ENSURE_TRUE(mSheet, NS_ERROR_FAILURE);
  
  if (aIndex > PRUint32(mRules.Count()))
    return NS_ERROR_DOM_INDEX_SIZE_ERR;

  NS_ASSERTION(PRUint32(mRules.Count()) <= PR_INT32_MAX,
               "Too many style rules!");

  return mSheet->InsertRuleIntoGroup(aRule, this, aIndex, _retval);
}

nsresult
nsCSSGroupRule::DeleteRule(PRUint32 aIndex)
{
  NS_ENSURE_TRUE(mSheet, NS_ERROR_FAILURE);

  if (aIndex >= PRUint32(mRules.Count()))
    return NS_ERROR_DOM_INDEX_SIZE_ERR;

  NS_ASSERTION(PRUint32(mRules.Count()) <= PR_INT32_MAX,
               "Too many style rules!");

  return mSheet->DeleteRuleFromGroup(this, aIndex);
}

// -------------------------------------------
// nsICSSMediaRule
//
nsCSSMediaRule::nsCSSMediaRule()
{
}

nsCSSMediaRule::nsCSSMediaRule(const nsCSSMediaRule& aCopy)
  : nsCSSGroupRule(aCopy)
{
  if (aCopy.mMedia) {
    aCopy.mMedia->Clone(getter_AddRefs(mMedia));
    if (mMedia) {
      // XXXldb This doesn't really make sense.
      mMedia->SetStyleSheet(aCopy.mSheet);
    }
  }
}

nsCSSMediaRule::~nsCSSMediaRule()
{
  if (mMedia) {
    mMedia->SetStyleSheet(nsnull);
  }
}

NS_IMPL_ADDREF_INHERITED(nsCSSMediaRule, nsCSSRule)
NS_IMPL_RELEASE_INHERITED(nsCSSMediaRule, nsCSSRule)

// QueryInterface implementation for nsCSSMediaRule
NS_INTERFACE_MAP_BEGIN(nsCSSMediaRule)
  NS_INTERFACE_MAP_ENTRY(nsICSSGroupRule)
  NS_INTERFACE_MAP_ENTRY(nsICSSRule)
  NS_INTERFACE_MAP_ENTRY(nsIStyleRule)
  NS_INTERFACE_MAP_ENTRY(nsIDOMCSSRule)
  NS_INTERFACE_MAP_ENTRY(nsIDOMCSSMediaRule)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsCSSGroupRule)
  NS_INTERFACE_MAP_ENTRY_CONTENT_CLASSINFO(CSSMediaRule)
NS_INTERFACE_MAP_END

NS_IMETHODIMP
nsCSSMediaRule::SetStyleSheet(nsICSSStyleSheet* aSheet)
{
  if (mMedia) {
    // Set to null so it knows it's leaving one sheet and joining another.
    mMedia->SetStyleSheet(nsnull);
    mMedia->SetStyleSheet(aSheet);
  }

  return nsCSSGroupRule::SetStyleSheet(aSheet);
}

#ifdef DEBUG
NS_IMETHODIMP
nsCSSMediaRule::List(FILE* out, PRInt32 aIndent) const
{
  for (PRInt32 indent = aIndent; --indent >= 0; ) fputs("  ", out);

  nsAutoString  buffer;

  fputs("@media ", out);

  if (mMedia) {
    nsAutoString mediaText;
    mMedia->GetText(mediaText);
    fputs(NS_LossyConvertUTF16toASCII(mediaText).get(), out);
  }

  return nsCSSGroupRule::List(out, aIndent);
}
#endif

NS_IMETHODIMP
nsCSSMediaRule::GetType(PRInt32& aType) const
{
  aType = nsICSSRule::MEDIA_RULE;
  return NS_OK;
}

NS_IMETHODIMP
nsCSSMediaRule::Clone(nsICSSRule*& aClone) const
{
  nsCSSMediaRule* clone = new nsCSSMediaRule(*this);
  if (clone) {
    return CallQueryInterface(clone, &aClone);
  }
  aClone = nsnull;
  return NS_ERROR_OUT_OF_MEMORY;
}

nsresult
nsCSSMediaRule::SetMedia(nsMediaList* aMedia)
{
  mMedia = aMedia;
  if (aMedia)
    mMedia->SetStyleSheet(mSheet);
  return NS_OK;
}

// nsIDOMCSSRule methods
NS_IMETHODIMP
nsCSSMediaRule::GetType(PRUint16* aType)
{
  *aType = nsIDOMCSSRule::MEDIA_RULE;
  return NS_OK;
}

NS_IMETHODIMP
nsCSSMediaRule::GetCssText(nsAString& aCssText)
{
  aCssText.AssignLiteral("@media ");
  // get all the media
  if (mMedia) {
    nsAutoString mediaText;
    mMedia->GetText(mediaText);
    aCssText.Append(mediaText);
  }

  return nsCSSGroupRule::AppendRulesToCssText(aCssText);
}

NS_IMETHODIMP
nsCSSMediaRule::SetCssText(const nsAString& aCssText)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsCSSMediaRule::GetParentStyleSheet(nsIDOMCSSStyleSheet** aSheet)
{
  return nsCSSGroupRule::GetParentStyleSheet(aSheet);
}

NS_IMETHODIMP
nsCSSMediaRule::GetParentRule(nsIDOMCSSRule** aParentRule)
{
  return nsCSSGroupRule::GetParentRule(aParentRule);
}

// nsIDOMCSSMediaRule methods
NS_IMETHODIMP
nsCSSMediaRule::GetMedia(nsIDOMMediaList* *aMedia)
{
  NS_ENSURE_ARG_POINTER(aMedia);
  if (!mMedia) {
    *aMedia = nsnull;
    return NS_OK;
  }

  return CallQueryInterface(mMedia.get(), aMedia);
}

NS_IMETHODIMP
nsCSSMediaRule::GetCssRules(nsIDOMCSSRuleList* *aRuleList)
{
  return nsCSSGroupRule::GetCssRules(aRuleList);
}

NS_IMETHODIMP
nsCSSMediaRule::InsertRule(const nsAString & aRule, PRUint32 aIndex, PRUint32* _retval)
{
  return nsCSSGroupRule::InsertRule(aRule, aIndex, _retval);
}

NS_IMETHODIMP
nsCSSMediaRule::DeleteRule(PRUint32 aIndex)
{
  return nsCSSGroupRule::DeleteRule(aIndex);
}

// nsICSSGroupRule interface
NS_IMETHODIMP_(PRBool)
nsCSSMediaRule::UseForPresentation(nsPresContext* aPresContext,
                                   nsMediaQueryResultCacheKey& aKey)
{
  if (mMedia) {
    return mMedia->Matches(aPresContext, aKey);
  }
  return PR_TRUE;
}


nsCSSDocumentRule::nsCSSDocumentRule(void)
{
}

nsCSSDocumentRule::nsCSSDocumentRule(const nsCSSDocumentRule& aCopy)
  : nsCSSGroupRule(aCopy)
  , mURLs(new URL(*aCopy.mURLs))
{
}

nsCSSDocumentRule::~nsCSSDocumentRule(void)
{
}

NS_IMPL_ADDREF_INHERITED(nsCSSDocumentRule, nsCSSRule)
NS_IMPL_RELEASE_INHERITED(nsCSSDocumentRule, nsCSSRule)

// QueryInterface implementation for nsCSSDocumentRule
NS_INTERFACE_MAP_BEGIN(nsCSSDocumentRule)
  NS_INTERFACE_MAP_ENTRY(nsICSSGroupRule)
  NS_INTERFACE_MAP_ENTRY(nsICSSRule)
  NS_INTERFACE_MAP_ENTRY(nsIStyleRule)
  NS_INTERFACE_MAP_ENTRY(nsIDOMCSSRule)
  NS_INTERFACE_MAP_ENTRY(nsIDOMCSSMozDocumentRule)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsCSSGroupRule)
  NS_INTERFACE_MAP_ENTRY_CONTENT_CLASSINFO(CSSMozDocumentRule)
NS_INTERFACE_MAP_END

#ifdef DEBUG
NS_IMETHODIMP
nsCSSDocumentRule::List(FILE* out, PRInt32 aIndent) const
{
  for (PRInt32 indent = aIndent; --indent >= 0; ) fputs("  ", out);

  nsCAutoString str;
  str.AssignLiteral("@-moz-document ");
  for (URL *url = mURLs; url; url = url->next) {
    switch (url->func) {
      case eURL:
        str.AppendLiteral("url(\"");
        break;
      case eURLPrefix:
        str.AppendLiteral("url-prefix(\"");
        break;
      case eDomain:
        str.AppendLiteral("domain(\"");
        break;
    }
    nsCAutoString escapedURL(url->url);
    escapedURL.ReplaceSubstring("\"", "\\\""); // escape quotes
    str.Append(escapedURL);
    str.AppendLiteral("\"), ");
  }
  str.Cut(str.Length() - 2, 1); // remove last ,
  fputs(str.get(), out);

  return nsCSSGroupRule::List(out, aIndent);
}
#endif

NS_IMETHODIMP
nsCSSDocumentRule::GetType(PRInt32& aType) const
{
  aType = nsICSSRule::DOCUMENT_RULE;
  return NS_OK;
}

NS_IMETHODIMP
nsCSSDocumentRule::Clone(nsICSSRule*& aClone) const
{
  nsCSSDocumentRule* clone = new nsCSSDocumentRule(*this);
  if (clone) {
    return CallQueryInterface(clone, &aClone);
  }
  aClone = nsnull;
  return NS_ERROR_OUT_OF_MEMORY;
}

// nsIDOMCSSRule methods
NS_IMETHODIMP
nsCSSDocumentRule::GetType(PRUint16* aType)
{
  // XXX What should really happen here?
  *aType = nsIDOMCSSRule::UNKNOWN_RULE;
  return NS_OK;
}

NS_IMETHODIMP
nsCSSDocumentRule::GetCssText(nsAString& aCssText)
{
  aCssText.AssignLiteral("@-moz-document ");
  for (URL *url = mURLs; url; url = url->next) {
    switch (url->func) {
      case eURL:
        aCssText.AppendLiteral("url(");
        break;
      case eURLPrefix:
        aCssText.AppendLiteral("url-prefix(");
        break;
      case eDomain:
        aCssText.AppendLiteral("domain(");
        break;
    }
    nsStyleUtil::AppendEscapedCSSString(NS_ConvertUTF8toUTF16(url->url),
                                        aCssText);
    aCssText.AppendLiteral("), ");
  }
  aCssText.Cut(aCssText.Length() - 2, 1); // remove last ,

  return nsCSSGroupRule::AppendRulesToCssText(aCssText);
}

NS_IMETHODIMP
nsCSSDocumentRule::SetCssText(const nsAString& aCssText)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsCSSDocumentRule::GetParentStyleSheet(nsIDOMCSSStyleSheet** aSheet)
{
  return nsCSSGroupRule::GetParentStyleSheet(aSheet);
}

NS_IMETHODIMP
nsCSSDocumentRule::GetParentRule(nsIDOMCSSRule** aParentRule)
{
  return nsCSSGroupRule::GetParentRule(aParentRule);
}

NS_IMETHODIMP
nsCSSDocumentRule::GetCssRules(nsIDOMCSSRuleList* *aRuleList)
{
  return nsCSSGroupRule::GetCssRules(aRuleList);
}

NS_IMETHODIMP
nsCSSDocumentRule::InsertRule(const nsAString & aRule, PRUint32 aIndex, PRUint32* _retval)
{
  return nsCSSGroupRule::InsertRule(aRule, aIndex, _retval);
}

NS_IMETHODIMP
nsCSSDocumentRule::DeleteRule(PRUint32 aIndex)
{
  return nsCSSGroupRule::DeleteRule(aIndex);
}

// nsICSSGroupRule interface
NS_IMETHODIMP_(PRBool)
nsCSSDocumentRule::UseForPresentation(nsPresContext* aPresContext,
                                      nsMediaQueryResultCacheKey& aKey)
{
  nsIURI *docURI = aPresContext->Document()->GetDocumentURI();
  nsCAutoString docURISpec;
  if (docURI)
    docURI->GetSpec(docURISpec);

  for (URL *url = mURLs; url; url = url->next) {
    switch (url->func) {
      case eURL: {
        if (docURISpec == url->url)
          return PR_TRUE;
      } break;
      case eURLPrefix: {
        if (StringBeginsWith(docURISpec, url->url))
          return PR_TRUE;
      } break;
      case eDomain: {
        nsCAutoString host;
        if (docURI)
          docURI->GetHost(host);
        PRInt32 lenDiff = host.Length() - url->url.Length();
        if (lenDiff == 0) {
          if (host == url->url)
            return PR_TRUE;
        } else {
          if (StringEndsWith(host, url->url) &&
              host.CharAt(lenDiff - 1) == '.')
            return PR_TRUE;
        }
      } break;
    }
  }

  return PR_FALSE;
}

nsCSSDocumentRule::URL::~URL()
{
  NS_CSS_DELETE_LIST_MEMBER(nsCSSDocumentRule::URL, this, next);
}

// -------------------------------------------
// nsICSSNameSpaceRule
//
class CSSNameSpaceRuleImpl : public nsCSSRule,
                             public nsICSSNameSpaceRule,
                             public nsIDOMCSSRule
{
public:
  CSSNameSpaceRuleImpl(void);
  CSSNameSpaceRuleImpl(const CSSNameSpaceRuleImpl& aCopy);
  virtual ~CSSNameSpaceRuleImpl(void);

  NS_DECL_ISUPPORTS_INHERITED

  DECL_STYLE_RULE_INHERIT

  // nsIStyleRule methods
#ifdef DEBUG
  NS_IMETHOD List(FILE* out = stdout, PRInt32 aIndent = 0) const;
#endif

  // nsICSSRule methods
  NS_IMETHOD GetType(PRInt32& aType) const;
  NS_IMETHOD Clone(nsICSSRule*& aClone) const;

  // nsICSSNameSpaceRule methods
  NS_IMETHOD GetPrefix(nsIAtom*& aPrefix) const;
  NS_IMETHOD SetPrefix(nsIAtom* aPrefix);

  NS_IMETHOD GetURLSpec(nsString& aURLSpec) const;
  NS_IMETHOD SetURLSpec(const nsString& aURLSpec);

  // nsIDOMCSSRule interface
  NS_DECL_NSIDOMCSSRULE
  
protected:
  nsIAtom*  mPrefix;
  nsString  mURLSpec;
};

CSSNameSpaceRuleImpl::CSSNameSpaceRuleImpl(void)
  : nsCSSRule(),
    mPrefix(nsnull),
    mURLSpec()
{
}

CSSNameSpaceRuleImpl::CSSNameSpaceRuleImpl(const CSSNameSpaceRuleImpl& aCopy)
  : nsCSSRule(aCopy),
    mPrefix(aCopy.mPrefix),
    mURLSpec(aCopy.mURLSpec)
{
  NS_IF_ADDREF(mPrefix);
}

CSSNameSpaceRuleImpl::~CSSNameSpaceRuleImpl(void)
{
  NS_IF_RELEASE(mPrefix);
}

NS_IMPL_ADDREF_INHERITED(CSSNameSpaceRuleImpl, nsCSSRule)
NS_IMPL_RELEASE_INHERITED(CSSNameSpaceRuleImpl, nsCSSRule)

// QueryInterface implementation for CSSNameSpaceRuleImpl
NS_INTERFACE_MAP_BEGIN(CSSNameSpaceRuleImpl)
  NS_INTERFACE_MAP_ENTRY(nsICSSNameSpaceRule)
  NS_INTERFACE_MAP_ENTRY(nsICSSRule)
  NS_INTERFACE_MAP_ENTRY(nsIStyleRule)
  NS_INTERFACE_MAP_ENTRY(nsIDOMCSSRule)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsICSSNameSpaceRule)
  NS_INTERFACE_MAP_ENTRY_CONTENT_CLASSINFO(CSSNameSpaceRule)
NS_INTERFACE_MAP_END

IMPL_STYLE_RULE_INHERIT(CSSNameSpaceRuleImpl, nsCSSRule)

#ifdef DEBUG
NS_IMETHODIMP
CSSNameSpaceRuleImpl::List(FILE* out, PRInt32 aIndent) const
{
  for (PRInt32 indent = aIndent; --indent >= 0; ) fputs("  ", out);

  nsAutoString  buffer;

  fputs("@namespace ", out);

  if (mPrefix) {
    mPrefix->ToString(buffer);
    fputs(NS_LossyConvertUTF16toASCII(buffer).get(), out);
    fputs(" ", out);
  }

  fputs("url(", out);
  fputs(NS_LossyConvertUTF16toASCII(mURLSpec).get(), out);
  fputs(")\n", out);
  return NS_OK;
}
#endif

NS_IMETHODIMP
CSSNameSpaceRuleImpl::GetType(PRInt32& aType) const
{
  aType = nsICSSRule::NAMESPACE_RULE;
  return NS_OK;
}

NS_IMETHODIMP
CSSNameSpaceRuleImpl::Clone(nsICSSRule*& aClone) const
{
  CSSNameSpaceRuleImpl* clone = new CSSNameSpaceRuleImpl(*this);
  if (clone) {
    return CallQueryInterface(clone, &aClone);
  }
  aClone = nsnull;
  return NS_ERROR_OUT_OF_MEMORY;
}

NS_IMETHODIMP
CSSNameSpaceRuleImpl::GetPrefix(nsIAtom*& aPrefix) const
{
  aPrefix = mPrefix;
  NS_IF_ADDREF(aPrefix);
  return NS_OK;
}

NS_IMETHODIMP
CSSNameSpaceRuleImpl::SetPrefix(nsIAtom* aPrefix)
{
  NS_IF_RELEASE(mPrefix);
  mPrefix = aPrefix;
  NS_IF_ADDREF(mPrefix);
  return NS_OK;
}

NS_IMETHODIMP
CSSNameSpaceRuleImpl::GetURLSpec(nsString& aURLSpec) const
{
  aURLSpec = mURLSpec;
  return NS_OK;
}

NS_IMETHODIMP
CSSNameSpaceRuleImpl::SetURLSpec(const nsString& aURLSpec)
{
  mURLSpec = aURLSpec;
  return NS_OK;
}

nsresult
NS_NewCSSNameSpaceRule(nsICSSNameSpaceRule** aInstancePtrResult, 
                       nsIAtom* aPrefix, const nsString& aURLSpec)
{
  if (! aInstancePtrResult) {
    return NS_ERROR_NULL_POINTER;
  }

  CSSNameSpaceRuleImpl* it = new CSSNameSpaceRuleImpl();

  if (! it) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  it->SetPrefix(aPrefix);
  it->SetURLSpec(aURLSpec);
  return CallQueryInterface(it, aInstancePtrResult);
}

NS_IMETHODIMP
CSSNameSpaceRuleImpl::GetType(PRUint16* aType)
{
  // XXX What should really happen here?
  *aType = nsIDOMCSSRule::UNKNOWN_RULE;
  return NS_OK;
}

NS_IMETHODIMP
CSSNameSpaceRuleImpl::GetCssText(nsAString& aCssText)
{
  aCssText.AssignLiteral("@namespace ");
  if (mPrefix) {
    nsString atomStr;
    mPrefix->ToString(atomStr);
    aCssText.Append(atomStr);
    aCssText.AppendLiteral(" ");
  }
  aCssText.AppendLiteral("url(");
  nsStyleUtil::AppendEscapedCSSString(mURLSpec, aCssText);
  aCssText.Append(NS_LITERAL_STRING(");"));
  return NS_OK;
}

NS_IMETHODIMP
CSSNameSpaceRuleImpl::SetCssText(const nsAString& aCssText)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
CSSNameSpaceRuleImpl::GetParentStyleSheet(nsIDOMCSSStyleSheet** aSheet)
{
  if (mSheet) {
    return CallQueryInterface(mSheet, aSheet);
  }
  *aSheet = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
CSSNameSpaceRuleImpl::GetParentRule(nsIDOMCSSRule** aParentRule)
{
  if (mParentRule) {
    return mParentRule->GetDOMRule(aParentRule);
  }
  *aParentRule = nsnull;
  return NS_OK;
}

// -------------------------------------------
// nsCSSFontFaceStyleDecl and related routines
//

// A src: descriptor is represented as an array value; each entry in
// the array can be eCSSUnit_URL, eCSSUnit_Local_Font, or
// eCSSUnit_Font_Format.  Blocks of eCSSUnit_Font_Format may appear
// only after one of the first two.  (css3-fonts only contemplates
// annotating URLs with formats, but we handle the general case.)
static void
AppendSerializedFontSrc(const nsCSSValue& src, nsAString & aResult NS_OUTPARAM)
{
  NS_PRECONDITION(src.GetUnit() == eCSSUnit_Array,
                  "improper value unit for src:");

  const nsCSSValue::Array& sources = *src.GetArrayValue();
  size_t i = 0;

  while (i < sources.Count()) {
    nsAutoString formats;

    if (sources[i].GetUnit() == eCSSUnit_URL) {
      aResult.AppendLiteral("url(");
      nsDependentString url(sources[i].GetOriginalURLValue());
      nsStyleUtil::AppendEscapedCSSString(url, aResult);
      aResult.AppendLiteral(")");
    } else if (sources[i].GetUnit() == eCSSUnit_Local_Font) {
      aResult.AppendLiteral("local(");
      nsDependentString local(sources[i].GetStringBufferValue());
      nsStyleUtil::AppendEscapedCSSString(local, aResult);
      aResult.AppendLiteral(")");
    } else {
      NS_NOTREACHED("entry in src: descriptor with improper unit");
      i++;
      continue;
    }

    i++;
    formats.Truncate();
    while (i < sources.Count() &&
           sources[i].GetUnit() == eCSSUnit_Font_Format) {
      formats.Append('"');
      formats.Append(sources[i].GetStringBufferValue());
      formats.AppendLiteral("\", ");
      i++;
    }
    if (formats.Length() > 0) {
      formats.Truncate(formats.Length() - 2); // remove the last comma
      aResult.AppendLiteral(" format(");
      aResult.Append(formats);
      aResult.Append(')');
    }
    aResult.AppendLiteral(", ");
  }
  aResult.Truncate(aResult.Length() - 2); // remove the last comma-space
}

// Mapping from nsCSSFontDesc codes to nsCSSFontFaceStyleDecl fields.
// Keep this in sync with enum nsCSSFontDesc in nsCSSProperty.h.
nsCSSValue nsCSSFontFaceStyleDecl::* const
nsCSSFontFaceStyleDecl::Fields[] = {
    &nsCSSFontFaceStyleDecl::mFamily,
    &nsCSSFontFaceStyleDecl::mStyle,
    &nsCSSFontFaceStyleDecl::mWeight,
    &nsCSSFontFaceStyleDecl::mStretch,
    &nsCSSFontFaceStyleDecl::mSrc,
    &nsCSSFontFaceStyleDecl::mUnicodeRange
};

// QueryInterface implementation for nsCSSFontFaceStyleDecl
NS_INTERFACE_MAP_BEGIN(nsCSSFontFaceStyleDecl)
  NS_INTERFACE_MAP_ENTRY(nsIDOMCSSStyleDeclaration)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY_CONTENT_CLASSINFO(CSSFontFaceStyleDecl)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF_USING_AGGREGATOR(nsCSSFontFaceStyleDecl, ContainingRule())
NS_IMPL_RELEASE_USING_AGGREGATOR(nsCSSFontFaceStyleDecl, ContainingRule())

// helper for string GetPropertyValue and RemovePropertyValue
nsresult
nsCSSFontFaceStyleDecl::GetPropertyValue(nsCSSFontDesc aFontDescID,
                                         nsAString & aResult NS_OUTPARAM) const
{
  NS_ENSURE_ARG_RANGE(aFontDescID, eCSSFontDesc_UNKNOWN,
                      eCSSFontDesc_COUNT - 1);

  aResult.Truncate();
  if (aFontDescID == eCSSFontDesc_UNKNOWN)
    return NS_OK;

  const nsCSSValue& val = this->*nsCSSFontFaceStyleDecl::Fields[aFontDescID];

  if (val.GetUnit() == eCSSUnit_Null) {
    // Avoid having to check no-value in the Family and Src cases below.
    return NS_OK;
  }

  switch (aFontDescID) {
  case eCSSFontDesc_Family: {
      // we don't use AppendCSSValueToString here because it doesn't
      // canonicalize the way we want, and anyway it's overkill when
      // we know we have eCSSUnit_String
      NS_ASSERTION(val.GetUnit() == eCSSUnit_String, "unexpected unit");
      nsDependentString family(val.GetStringBufferValue());
      nsStyleUtil::AppendEscapedCSSString(family, aResult);
      return NS_OK;
    }

  case eCSSFontDesc_Style:
    nsCSSDeclaration::AppendCSSValueToString(eCSSProperty_font_style, val,
                                             aResult);
    return NS_OK;

  case eCSSFontDesc_Weight:
    nsCSSDeclaration::AppendCSSValueToString(eCSSProperty_font_weight, val,
                                             aResult);
    return NS_OK;
    
  case eCSSFontDesc_Stretch:
    nsCSSDeclaration::AppendCSSValueToString(eCSSProperty_font_stretch, val,
                                             aResult);
    return NS_OK;

  case eCSSFontDesc_Src:
    AppendSerializedFontSrc(val, aResult);
    return NS_OK;

  case eCSSFontDesc_UnicodeRange:
    // these are not implemented, so always return an empty string
    return NS_OK;

  case eCSSFontDesc_UNKNOWN:
  case eCSSFontDesc_COUNT:
    ;
  }
  NS_NOTREACHED("nsCSSFontFaceStyleDecl::GetPropertyValue: "
                "out-of-range value got to the switch");
  return NS_ERROR_INVALID_ARG;
}


// attribute DOMString cssText;
NS_IMETHODIMP
nsCSSFontFaceStyleDecl::GetCssText(nsAString & aCssText)
{
  nsAutoString descStr;

  aCssText.Truncate();
  for (nsCSSFontDesc id = nsCSSFontDesc(eCSSFontDesc_UNKNOWN + 1);
       id < eCSSFontDesc_COUNT;
       id = nsCSSFontDesc(id + 1)) {
    if ((this->*nsCSSFontFaceStyleDecl::Fields[id]).GetUnit()
          != eCSSUnit_Null &&
        NS_SUCCEEDED(GetPropertyValue(id, descStr))) {
      NS_ASSERTION(descStr.Length() > 0,
                   "GetCssText: non-null unit, empty property value");
      aCssText.AppendLiteral("  ");
      aCssText.AppendASCII(nsCSSProps::GetStringValue(id).get());
      aCssText.AppendLiteral(": ");
      aCssText.Append(descStr);
      aCssText.AppendLiteral(";\n");
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsCSSFontFaceStyleDecl::SetCssText(const nsAString & aCssText)
{
  return NS_ERROR_NOT_IMPLEMENTED; // bug 443978
}

// DOMString getPropertyValue (in DOMString propertyName);
NS_IMETHODIMP
nsCSSFontFaceStyleDecl::GetPropertyValue(const nsAString & propertyName,
                                         nsAString & aResult NS_OUTPARAM)
{
  return GetPropertyValue(nsCSSProps::LookupFontDesc(propertyName), aResult);
}

// nsIDOMCSSValue getPropertyCSSValue (in DOMString propertyName);
NS_IMETHODIMP
nsCSSFontFaceStyleDecl::GetPropertyCSSValue(const nsAString & propertyName,
                                            nsIDOMCSSValue **aResult NS_OUTPARAM)
{
  // ??? nsDOMCSSDeclaration returns null/NS_OK, but that seems wrong.
  return NS_ERROR_NOT_IMPLEMENTED;
}

// DOMString removeProperty (in DOMString propertyName) raises (DOMException);
NS_IMETHODIMP
nsCSSFontFaceStyleDecl::RemoveProperty(const nsAString & propertyName,
                                       nsAString & aResult NS_OUTPARAM)
{
  nsCSSFontDesc descID = nsCSSProps::LookupFontDesc(propertyName);
  NS_ASSERTION(descID >= eCSSFontDesc_UNKNOWN &&
               descID < eCSSFontDesc_COUNT,
               "LookupFontDesc returned value out of range");

  if (descID == eCSSFontDesc_UNKNOWN) {
    aResult.Truncate();
  } else {
    nsresult rv = GetPropertyValue(descID, aResult);
    NS_ENSURE_SUCCESS(rv, rv);
    (this->*nsCSSFontFaceStyleDecl::Fields[descID]).Reset();
  }
  return NS_OK;
}

// DOMString getPropertyPriority (in DOMString propertyName);
NS_IMETHODIMP
nsCSSFontFaceStyleDecl::GetPropertyPriority(const nsAString & propertyName,
                                            nsAString & aResult NS_OUTPARAM)
{
  // font descriptors do not have priorities at present
  aResult.Truncate();
  return NS_OK;
}

// void setProperty (in DOMString propertyName, in DOMString value,
//                   in DOMString priority)  raises (DOMException);
NS_IMETHODIMP
nsCSSFontFaceStyleDecl::SetProperty(const nsAString & propertyName,
                                    const nsAString & value,
                                    const nsAString & priority)
{
  return NS_ERROR_NOT_IMPLEMENTED; // bug 443978
}

// readonly attribute unsigned long length;
NS_IMETHODIMP
nsCSSFontFaceStyleDecl::GetLength(PRUint32 *aLength)
{
  PRUint32 len = 0;
  for (nsCSSFontDesc id = nsCSSFontDesc(eCSSFontDesc_UNKNOWN + 1);
       id < eCSSFontDesc_COUNT;
       id = nsCSSFontDesc(id + 1))
    if ((this->*nsCSSFontFaceStyleDecl::Fields[id]).GetUnit() != eCSSUnit_Null)
      len++;

  *aLength = len;
  return NS_OK;
}

// DOMString item (in unsigned long index);
NS_IMETHODIMP
nsCSSFontFaceStyleDecl::Item(PRUint32 index, nsAString & aResult NS_OUTPARAM)
 {
  PRInt32 nset = -1;
  for (nsCSSFontDesc id = nsCSSFontDesc(eCSSFontDesc_UNKNOWN + 1);
       id < eCSSFontDesc_COUNT;
       id = nsCSSFontDesc(id + 1)) {
    if ((this->*nsCSSFontFaceStyleDecl::Fields[id]).GetUnit()
        != eCSSUnit_Null) {
      nset++;
      if (nset == PRInt32(index)) {
        aResult.AssignASCII(nsCSSProps::GetStringValue(id).get());
        return NS_OK;
      }
    }
  }
  aResult.Truncate();
  return NS_OK;
}

// readonly attribute nsIDOMCSSRule parentRule;
NS_IMETHODIMP
nsCSSFontFaceStyleDecl::GetParentRule(nsIDOMCSSRule** aParentRule)
{
  return ContainingRule()->GetDOMRule(aParentRule);
}


// -------------------------------------------
// nsCSSFontFaceRule
// 

NS_IMETHODIMP
nsCSSFontFaceRule::Clone(nsICSSRule*& aClone) const
{
  nsCSSFontFaceRule* clone = new nsCSSFontFaceRule(*this);
  if (clone) {
    return CallQueryInterface(clone, &aClone);
  }
  aClone = nsnull;
  return NS_ERROR_OUT_OF_MEMORY;
}

NS_IMPL_ADDREF_INHERITED(nsCSSFontFaceRule, nsCSSRule)
NS_IMPL_RELEASE_INHERITED(nsCSSFontFaceRule, nsCSSRule)

// QueryInterface implementation for nsCSSFontFaceRule
NS_INTERFACE_MAP_BEGIN(nsCSSFontFaceRule)
  NS_INTERFACE_MAP_ENTRY(nsICSSRule)
  NS_INTERFACE_MAP_ENTRY(nsIStyleRule)
  NS_INTERFACE_MAP_ENTRY(nsIDOMCSSFontFaceRule)
  NS_INTERFACE_MAP_ENTRY(nsIDOMCSSRule)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsICSSRule)
  NS_INTERFACE_MAP_ENTRY_CONTENT_CLASSINFO(CSSFontFaceRule)
NS_INTERFACE_MAP_END

IMPL_STYLE_RULE_INHERIT(nsCSSFontFaceRule, nsCSSRule)

#ifdef DEBUG
NS_IMETHODIMP
nsCSSFontFaceRule::List(FILE* out, PRInt32 aIndent) const
{
  nsCString baseInd, descInd;
  for (PRInt32 indent = aIndent; --indent >= 0; ) {
    baseInd.AppendLiteral("  ");
    descInd.AppendLiteral("  ");
  }
  descInd.AppendLiteral("  ");

  nsString descStr;

  fprintf(out, "%s@font-face {\n", baseInd.get());
  for (nsCSSFontDesc id = nsCSSFontDesc(eCSSFontDesc_UNKNOWN + 1);
       id < eCSSFontDesc_COUNT;
       id = nsCSSFontDesc(id + 1))
    if ((mDecl.*nsCSSFontFaceStyleDecl::Fields[id]).GetUnit()
        != eCSSUnit_Null) {
      if (NS_FAILED(mDecl.GetPropertyValue(id, descStr)))
        descStr.AssignLiteral("#<serialization error>");
      else if (descStr.Length() == 0)
        descStr.AssignLiteral("#<serialization missing>");
      fprintf(out, "%s%s: %s\n",
              descInd.get(), nsCSSProps::GetStringValue(id).get(),
              NS_ConvertUTF16toUTF8(descStr).get());
    }
  fprintf(out, "%s}\n", baseInd.get());
  return NS_OK;
}
#endif

NS_IMETHODIMP
nsCSSFontFaceRule::GetType(PRInt32& aType) const
{
  aType = nsICSSRule::FONT_FACE_RULE;
  return NS_OK;
}

NS_IMETHODIMP
nsCSSFontFaceRule::GetType(PRUint16* aType)
{
  *aType = nsIDOMCSSRule::FONT_FACE_RULE;
  return NS_OK;
}

NS_IMETHODIMP
nsCSSFontFaceRule::GetCssText(nsAString& aCssText)
{
  nsAutoString propText;
  mDecl.GetCssText(propText);

  aCssText.AssignLiteral("@font-face {\n");
  aCssText.Append(propText);
  aCssText.Append('}');
  return NS_OK;
}

NS_IMETHODIMP
nsCSSFontFaceRule::SetCssText(const nsAString& aCssText)
{
  return NS_ERROR_NOT_IMPLEMENTED; // bug 443978
}

NS_IMETHODIMP
nsCSSFontFaceRule::GetParentStyleSheet(nsIDOMCSSStyleSheet** aSheet)
{
  if (mSheet) {
    return CallQueryInterface(mSheet, aSheet);
  }
  *aSheet = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
nsCSSFontFaceRule::GetParentRule(nsIDOMCSSRule** aParentRule)
{
  if (mParentRule) {
    return mParentRule->GetDOMRule(aParentRule);
  }
  *aParentRule = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
nsCSSFontFaceRule::GetStyle(nsIDOMCSSStyleDeclaration** aStyle)
{
  return CallQueryInterface(&mDecl, aStyle);
}

// Arguably these should forward to nsCSSFontFaceStyleDecl methods.
void
nsCSSFontFaceRule::SetDesc(nsCSSFontDesc aDescID, nsCSSValue const & aValue)
{
  NS_PRECONDITION(aDescID > eCSSFontDesc_UNKNOWN &&
                  aDescID < eCSSFontDesc_COUNT,
                  "aDescID out of range in nsCSSFontFaceRule::SetDesc");

  mDecl.*nsCSSFontFaceStyleDecl::Fields[aDescID] = aValue;
}

void
nsCSSFontFaceRule::GetDesc(nsCSSFontDesc aDescID, nsCSSValue & aValue)
{
  NS_PRECONDITION(aDescID > eCSSFontDesc_UNKNOWN &&
                  aDescID < eCSSFontDesc_COUNT,
                  "aDescID out of range in nsCSSFontFaceRule::GetDesc");

  aValue = mDecl.*nsCSSFontFaceStyleDecl::Fields[aDescID];
}
