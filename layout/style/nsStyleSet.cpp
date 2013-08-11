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
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Daniel Glazman <glazman@netscape.com>
 *   Brian Ryner    <bryner@brianryner.com>
 *   L. David Baron <dbaron@dbaron.org>, Mozilla Corporation
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

/*
 * the container for the style sheets that apply to a presentation, and
 * the internal API that the style system exposes for creating (and
 * potentially re-creating) style contexts
 */

#include "nsStyleSet.h"
#include "nsNetUtil.h"
#include "nsICSSStyleSheet.h"
#include "nsIDocument.h"
#include "nsRuleWalker.h"
#include "nsStyleContext.h"
#include "nsICSSStyleRule.h"
#include "nsCSSAnonBoxes.h"
#include "nsCSSPseudoElements.h"
#include "nsCSSRuleProcessor.h"
#include "nsIContent.h"
#include "nsIFrame.h"
#include "nsContentUtils.h"
#include "nsRuleProcessorData.h"

NS_IMPL_ISUPPORTS1(nsEmptyStyleRule, nsIStyleRule)

NS_IMETHODIMP
nsEmptyStyleRule::MapRuleInfoInto(nsRuleData* aRuleData)
{
  return NS_OK;
}

#ifdef DEBUG
NS_IMETHODIMP
nsEmptyStyleRule::List(FILE* out, PRInt32 aIndent) const
{
  return NS_OK;
}
#endif

static const nsStyleSet::sheetType gCSSSheetTypes[] = {
  nsStyleSet::eAgentSheet,
  nsStyleSet::eUserSheet,
  nsStyleSet::eDocSheet,
  nsStyleSet::eOverrideSheet
};

nsStyleSet::nsStyleSet()
  : mRuleTree(nsnull),
    mDestroyedCount(0),
    mBatching(0),
    mInShutdown(PR_FALSE),
    mAuthorStyleDisabled(PR_FALSE),
    mInReconstruct(PR_FALSE),
    mDirty(0)
{
}

nsresult
nsStyleSet::Init(nsPresContext *aPresContext)
{
  mFirstLineRule = new nsEmptyStyleRule;
  mFirstLetterRule = new nsEmptyStyleRule;
  if (!mFirstLineRule || !mFirstLetterRule) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  if (!BuildDefaultStyleData(aPresContext)) {
    mDefaultStyleData.Destroy(0, aPresContext);
    return NS_ERROR_OUT_OF_MEMORY;
  }

  mRuleTree = nsRuleNode::CreateRootNode(aPresContext);
  if (!mRuleTree) {
    mDefaultStyleData.Destroy(0, aPresContext);
    return NS_ERROR_OUT_OF_MEMORY;
  }

  return NS_OK;
}

nsresult
nsStyleSet::BeginReconstruct()
{
  NS_ASSERTION(!mInReconstruct, "Unmatched begin/end?");
  NS_ASSERTION(mRuleTree, "Reconstructing before first construction?");

  // Create a new rule tree root
  nsRuleNode* newTree =
    nsRuleNode::CreateRootNode(mRuleTree->GetPresContext());
  if (!newTree)
    return NS_ERROR_OUT_OF_MEMORY;

  // Save the old rule tree so we can destroy it later
  if (!mOldRuleTrees.AppendElement(mRuleTree)) {
    newTree->Destroy();
    return NS_ERROR_OUT_OF_MEMORY;
  }

  // We need to keep mRoots so that the rule tree GC will only free the
  // rule trees that really aren't referenced anymore (which should be
  // all of them, if there are no bugs in reresolution code).

  mInReconstruct = PR_TRUE;
  mRuleTree = newTree;

  return NS_OK;
}

void
nsStyleSet::EndReconstruct()
{
  NS_ASSERTION(mInReconstruct, "Unmatched begin/end?");
  mInReconstruct = PR_FALSE;
#ifdef DEBUG
  for (PRInt32 i = mRoots.Length() - 1; i >= 0; --i) {
    nsRuleNode *n = mRoots[i]->GetRuleNode();
    while (n->GetParent()) {
      n = n->GetParent();
    }
    // Since nsStyleContext's mParent and mRuleNode are immutable, and
    // style contexts own their parents, and nsStyleContext asserts in
    // its constructor that the style context and its parent are in the
    // same rule tree, we don't need to check any of the children of
    // mRoots; we only need to check the rule nodes of mRoots
    // themselves.

    NS_ASSERTION(n == mRuleTree, "style context has old rule node");
  }
#endif
  // This *should* destroy the only element of mOldRuleTrees, but in
  // case of some bugs (which would trigger the above assertions), it
  // won't.
  GCRuleTrees();
}

void
nsStyleSet::SetQuirkStyleSheet(nsIStyleSheet* aQuirkStyleSheet)
{
  NS_ASSERTION(aQuirkStyleSheet, "Must have quirk sheet if this is called");
  NS_ASSERTION(!mQuirkStyleSheet, "Multiple calls to SetQuirkStyleSheet?");
  NS_ASSERTION(mSheets[eAgentSheet].IndexOf(aQuirkStyleSheet) != -1,
               "Quirk style sheet not one of our agent sheets?");
  mQuirkStyleSheet = aQuirkStyleSheet;
}

nsresult
nsStyleSet::GatherRuleProcessors(sheetType aType)
{
  mRuleProcessors[aType] = nsnull;
  if (mAuthorStyleDisabled && (aType == eDocSheet || 
                               aType == ePresHintSheet ||
                               aType == eHTMLPresHintSheet ||
                               aType == eStyleAttrSheet)) {
    //don't regather if this level is disabled
    return NS_OK;
  }
  if (mSheets[aType].Count()) {
    switch (aType) {
      case eAgentSheet:
      case eUserSheet:
      case eDocSheet:
      case eOverrideSheet: {
        // levels containing CSS stylesheets
        nsCOMArray<nsIStyleSheet>& sheets = mSheets[aType];
        nsCOMArray<nsICSSStyleSheet> cssSheets(sheets.Count());
        for (PRInt32 i = 0, i_end = sheets.Count(); i < i_end; ++i) {
          nsCOMPtr<nsICSSStyleSheet> cssSheet = do_QueryInterface(sheets[i]);
          NS_ASSERTION(cssSheet, "not a CSS sheet");
          cssSheets.AppendObject(cssSheet);
        }
        mRuleProcessors[aType] = new nsCSSRuleProcessor(cssSheets, 
                                                        PRUint8(aType));
      } break;

      default:
        // levels containing non-CSS stylesheets
        NS_ASSERTION(mSheets[aType].Count() == 1, "only one sheet per level");
        mRuleProcessors[aType] = do_QueryInterface(mSheets[aType][0]);
        break;
    }
  }

  return NS_OK;
}

#ifdef DEBUG
#define CHECK_APPLICABLE \
PR_BEGIN_MACRO \
  PRBool applicable = PR_TRUE; \
  aSheet->GetApplicable(applicable); \
  NS_ASSERTION(applicable, "Inapplicable sheet being placed in style set"); \
PR_END_MACRO
#else
#define CHECK_APPLICABLE
#endif

nsresult
nsStyleSet::AppendStyleSheet(sheetType aType, nsIStyleSheet *aSheet)
{
  NS_PRECONDITION(aSheet, "null arg");
  CHECK_APPLICABLE;
  mSheets[aType].RemoveObject(aSheet);
  if (!mSheets[aType].AppendObject(aSheet))
    return NS_ERROR_OUT_OF_MEMORY;

  if (!mBatching)
    return GatherRuleProcessors(aType);

  mDirty |= 1 << aType;
  return NS_OK;
}

nsresult
nsStyleSet::PrependStyleSheet(sheetType aType, nsIStyleSheet *aSheet)
{
  NS_PRECONDITION(aSheet, "null arg");
  CHECK_APPLICABLE;
  mSheets[aType].RemoveObject(aSheet);
  if (!mSheets[aType].InsertObjectAt(aSheet, 0))
    return NS_ERROR_OUT_OF_MEMORY;

  if (!mBatching)
    return GatherRuleProcessors(aType);

  mDirty |= 1 << aType;
  return NS_OK;
}

nsresult
nsStyleSet::RemoveStyleSheet(sheetType aType, nsIStyleSheet *aSheet)
{
  NS_PRECONDITION(aSheet, "null arg");
#ifdef DEBUG
  PRBool complete = PR_TRUE;
  aSheet->GetComplete(complete);
  NS_ASSERTION(complete, "Incomplete sheet being removed from style set");
#endif
  mSheets[aType].RemoveObject(aSheet);
  if (!mBatching)
    return GatherRuleProcessors(aType);

  mDirty |= 1 << aType;
  return NS_OK;
}

nsresult
nsStyleSet::ReplaceSheets(sheetType aType,
                          const nsCOMArray<nsIStyleSheet> &aNewSheets)
{
  mSheets[aType].Clear();
  if (!mSheets[aType].AppendObjects(aNewSheets))
    return NS_ERROR_OUT_OF_MEMORY;

  if (!mBatching)
    return GatherRuleProcessors(aType);

  mDirty |= 1 << aType;
  return NS_OK;
}

PRBool
nsStyleSet::GetAuthorStyleDisabled()
{
  return mAuthorStyleDisabled;
}

nsresult
nsStyleSet::SetAuthorStyleDisabled(PRBool aStyleDisabled)
{
  if (aStyleDisabled == !mAuthorStyleDisabled) {
    mAuthorStyleDisabled = aStyleDisabled;
    BeginUpdate();
    mDirty |= 1 << eDocSheet |
              1 << ePresHintSheet |
              1 << eHTMLPresHintSheet |
              1 << eStyleAttrSheet;
    return EndUpdate();
  }
  return NS_OK;
}

// -------- Doc Sheets

nsresult
nsStyleSet::AddDocStyleSheet(nsIStyleSheet* aSheet, nsIDocument* aDocument)
{
  NS_PRECONDITION(aSheet && aDocument, "null arg");
  CHECK_APPLICABLE;

  nsCOMArray<nsIStyleSheet>& docSheets = mSheets[eDocSheet];

  docSheets.RemoveObject(aSheet);
  // lowest index first
  PRInt32 newDocIndex = aDocument->GetIndexOfStyleSheet(aSheet);
  PRInt32 count = docSheets.Count();
  PRInt32 index;
  for (index = 0; index < count; index++) {
    nsIStyleSheet* sheet = docSheets.ObjectAt(index);
    PRInt32 sheetDocIndex = aDocument->GetIndexOfStyleSheet(sheet);
    if (sheetDocIndex > newDocIndex)
      break;
  }
  if (!docSheets.InsertObjectAt(aSheet, index))
    return NS_ERROR_OUT_OF_MEMORY;
  if (!mBatching)
    return GatherRuleProcessors(eDocSheet);

  mDirty |= 1 << eDocSheet;
  return NS_OK;
}

#undef CHECK_APPLICABLE

// Batching
void
nsStyleSet::BeginUpdate()
{
  ++mBatching;
}

nsresult
nsStyleSet::EndUpdate()
{
  NS_ASSERTION(mBatching > 0, "Unbalanced EndUpdate");
  if (--mBatching) {
    // We're not completely done yet.
    return NS_OK;
  }

  for (int i = 0; i < eSheetTypeCount; ++i) {
    if (mDirty & (1 << i)) {
      nsresult rv = GatherRuleProcessors(sheetType(i));
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  mDirty = 0;
  return NS_OK;
}

void
nsStyleSet::EnableQuirkStyleSheet(PRBool aEnable)
{
#ifdef DEBUG
  PRBool oldEnabled;
  {
    nsCOMPtr<nsIDOMCSSStyleSheet> domSheet =
      do_QueryInterface(mQuirkStyleSheet);
    domSheet->GetDisabled(&oldEnabled);
    oldEnabled = !oldEnabled;
  }
#endif
  mQuirkStyleSheet->SetEnabled(aEnable);
#ifdef DEBUG
  // This should always be OK, since SetEnabled should call
  // ClearRuleCascades.
  // Note that we can hit this codepath multiple times when document.open()
  // (potentially implied) happens multiple times.
  if (mRuleProcessors[eAgentSheet] && aEnable != oldEnabled) {
    static_cast<nsCSSRuleProcessor*>(static_cast<nsIStyleRuleProcessor*>(
      mRuleProcessors[eAgentSheet]))->AssertQuirksChangeOK();
  }
#endif
}

static PRBool
EnumRulesMatching(nsIStyleRuleProcessor* aProcessor, void* aData)
{
  ElementRuleProcessorData* data =
    static_cast<ElementRuleProcessorData*>(aData);

  aProcessor->RulesMatching(data);
  return PR_TRUE;
}

/**
 * |GetContext| implements sharing of style contexts (not just the data
 * on the rule nodes) between siblings and cousins of the same
 * generation.  (It works for cousins of the same generation since
 * |aParentContext| could itself be a shared context.)
 */
already_AddRefed<nsStyleContext>
nsStyleSet::GetContext(nsPresContext* aPresContext, 
                       nsStyleContext* aParentContext, 
                       nsRuleNode* aRuleNode,
                       nsIAtom* aPseudoTag)
{
  nsStyleContext* result = nsnull;
      
  if (aParentContext)
    result = aParentContext->FindChildWithRules(aPseudoTag, aRuleNode).get();

#ifdef NOISY_DEBUG
  if (result)
    fprintf(stdout, "--- SharedSC %d ---\n", ++gSharedCount);
  else
    fprintf(stdout, "+++ NewSC %d +++\n", ++gNewCount);
#endif

  if (!result) {
    result = NS_NewStyleContext(aParentContext, aPseudoTag, aRuleNode,
                                aPresContext).get();
    if (!aParentContext && result)
      mRoots.AppendElement(result);
  }

  return result;
}

void
nsStyleSet::AddImportantRules(nsRuleNode* aCurrLevelNode,
                              nsRuleNode* aLastPrevLevelNode,
                              nsRuleWalker* aRuleWalker)
{
  if (!aCurrLevelNode)
    return;

  nsAutoTArray<nsCOMPtr<nsIStyleRule>, 16> importantRules;
  for (nsRuleNode *node = aCurrLevelNode; node != aLastPrevLevelNode;
       node = node->GetParent()) {
    nsIStyleRule *rule = node->GetRule();
    nsCOMPtr<nsICSSStyleRule> cssRule(do_QueryInterface(rule));
    if (cssRule) {
      nsCOMPtr<nsIStyleRule> impRule = cssRule->GetImportantRule();
      if (impRule)
        importantRules.AppendElement(impRule);
    }
  }

  for (PRUint32 i = importantRules.Length(); i-- != 0; ) {
    aRuleWalker->Forward(importantRules[i]);
  }
}

#ifdef DEBUG
void
nsStyleSet::AssertNoImportantRules(nsRuleNode* aCurrLevelNode,
                                   nsRuleNode* aLastPrevLevelNode)
{
  if (!aCurrLevelNode)
    return;

  for (nsRuleNode *node = aCurrLevelNode; node != aLastPrevLevelNode;
       node = node->GetParent()) {
    nsIStyleRule *rule = node->GetRule();
    nsCOMPtr<nsICSSStyleRule> cssRule(do_QueryInterface(rule));
    if (cssRule) {
      nsCOMPtr<nsIStyleRule> impRule = cssRule->GetImportantRule();
      NS_ASSERTION(!impRule, "Unexpected important rule");
    }
  }
}

void
nsStyleSet::AssertNoCSSRules(nsRuleNode* aCurrLevelNode,
                             nsRuleNode* aLastPrevLevelNode)
{
  if (!aCurrLevelNode)
    return;

  for (nsRuleNode *node = aCurrLevelNode; node != aLastPrevLevelNode;
       node = node->GetParent()) {
    nsIStyleRule *rule = node->GetRule();
    nsCOMPtr<nsICSSStyleRule> cssRule(do_QueryInterface(rule));
    NS_ASSERTION(!cssRule || !cssRule->Selector(), "Unexpected CSS rule");
  }
}
#endif

// Enumerate the rules in a way that cares about the order of the rules.
void
nsStyleSet::FileRules(nsIStyleRuleProcessor::EnumFunc aCollectorFunc, 
                      RuleProcessorData* aData, nsRuleWalker* aRuleWalker)
{
  // Cascading order:
  // [least important]
  //  1. UA normal rules                    = Agent        normal
  //  2. Presentation hints                 = PresHint     normal
  //  3. User normal rules                  = User         normal
  //  4. HTML Presentation hints            = HTMLPresHint normal
  //  5. Author normal rules                = Document     normal
  //  6. Override normal rules              = Override     normal
  //  7. Author !important rules            = Document     !important
  //  8. Override !important rules          = Override     !important
  //  9. User !important rules              = User         !important
  // 10. UA !important rules                = Agent        !important
  // [most important]

  NS_PRECONDITION(SheetCount(ePresHintSheet) == 0 ||
                  SheetCount(eHTMLPresHintSheet) == 0,
                  "Can't have both types of preshint sheets at once!");
  
  aRuleWalker->SetLevel(eAgentSheet, PR_FALSE);
  if (mRuleProcessors[eAgentSheet])
    (*aCollectorFunc)(mRuleProcessors[eAgentSheet], aData);
  nsRuleNode* lastAgentRN = aRuleWalker->GetCurrentNode();

  aRuleWalker->SetLevel(ePresHintSheet, PR_FALSE);
  if (mRuleProcessors[ePresHintSheet])
    (*aCollectorFunc)(mRuleProcessors[ePresHintSheet], aData);
  nsRuleNode* lastPresHintRN = aRuleWalker->GetCurrentNode();

  aRuleWalker->SetLevel(eUserSheet, PR_FALSE);
  PRBool skipUserStyles =
    aData->mContent && aData->mContent->IsInNativeAnonymousSubtree();
  if (!skipUserStyles && mRuleProcessors[eUserSheet]) // NOTE: different
    (*aCollectorFunc)(mRuleProcessors[eUserSheet], aData);
  nsRuleNode* lastUserRN = aRuleWalker->GetCurrentNode();

  aRuleWalker->SetLevel(eHTMLPresHintSheet, PR_FALSE);
  if (mRuleProcessors[eHTMLPresHintSheet])
    (*aCollectorFunc)(mRuleProcessors[eHTMLPresHintSheet], aData);
  nsRuleNode* lastHTMLPresHintRN = aRuleWalker->GetCurrentNode();
  
  aRuleWalker->SetLevel(eDocSheet, PR_FALSE);
  PRBool cutOffInheritance = PR_FALSE;
  if (mBindingManager) {
    // We can supply additional document-level sheets that should be walked.
    mBindingManager->WalkRules(aCollectorFunc, aData, &cutOffInheritance);
  }
  if (!skipUserStyles && !cutOffInheritance &&
      mRuleProcessors[eDocSheet]) // NOTE: different
    (*aCollectorFunc)(mRuleProcessors[eDocSheet], aData);
  aRuleWalker->SetLevel(eStyleAttrSheet, PR_FALSE);
  if (mRuleProcessors[eStyleAttrSheet])
    (*aCollectorFunc)(mRuleProcessors[eStyleAttrSheet], aData);
  nsRuleNode* lastDocRN = aRuleWalker->GetCurrentNode();

  aRuleWalker->SetLevel(eOverrideSheet, PR_FALSE);
  if (mRuleProcessors[eOverrideSheet])
    (*aCollectorFunc)(mRuleProcessors[eOverrideSheet], aData);
  nsRuleNode* lastOvrRN = aRuleWalker->GetCurrentNode();

  aRuleWalker->SetLevel(eDocSheet, PR_TRUE);
  AddImportantRules(lastDocRN, lastHTMLPresHintRN, aRuleWalker);  // doc
  aRuleWalker->SetLevel(eOverrideSheet, PR_TRUE);
  AddImportantRules(lastOvrRN, lastDocRN, aRuleWalker);  // override
#ifdef DEBUG
  AssertNoCSSRules(lastHTMLPresHintRN, lastUserRN);
  AssertNoImportantRules(lastHTMLPresHintRN, lastUserRN); // HTML preshints
#endif
  aRuleWalker->SetLevel(eUserSheet, PR_TRUE);
  AddImportantRules(lastUserRN, lastPresHintRN, aRuleWalker); //user
#ifdef DEBUG
  AssertNoCSSRules(lastPresHintRN, lastAgentRN);
  AssertNoImportantRules(lastPresHintRN, lastAgentRN); // preshints
#endif
  aRuleWalker->SetLevel(eAgentSheet, PR_TRUE);
  AddImportantRules(lastAgentRN, nsnull, aRuleWalker);     //agent

}

// Enumerate all the rules in a way that doesn't care about the order
// of the rules and doesn't walk !important-rules.
void
nsStyleSet::WalkRuleProcessors(nsIStyleRuleProcessor::EnumFunc aFunc,
                               RuleProcessorData* aData)
{
  NS_PRECONDITION(SheetCount(ePresHintSheet) == 0 ||
                  SheetCount(eHTMLPresHintSheet) == 0,
                  "Can't have both types of preshint sheets at once!");
  
  if (mRuleProcessors[eAgentSheet])
    (*aFunc)(mRuleProcessors[eAgentSheet], aData);
  if (mRuleProcessors[ePresHintSheet])
    (*aFunc)(mRuleProcessors[ePresHintSheet], aData);

  PRBool skipUserStyles =
    aData->mContent && aData->mContent->IsInNativeAnonymousSubtree();
  if (!skipUserStyles && mRuleProcessors[eUserSheet]) // NOTE: different
    (*aFunc)(mRuleProcessors[eUserSheet], aData);

  if (mRuleProcessors[eHTMLPresHintSheet])
    (*aFunc)(mRuleProcessors[eHTMLPresHintSheet], aData);
  
  PRBool cutOffInheritance = PR_FALSE;
  if (mBindingManager) {
    // We can supply additional document-level sheets that should be walked.
    mBindingManager->WalkRules(aFunc, aData, &cutOffInheritance);
  }
  if (!skipUserStyles && !cutOffInheritance &&
      mRuleProcessors[eDocSheet]) // NOTE: different
    (*aFunc)(mRuleProcessors[eDocSheet], aData);
  if (mRuleProcessors[eStyleAttrSheet])
    (*aFunc)(mRuleProcessors[eStyleAttrSheet], aData);
  if (mRuleProcessors[eOverrideSheet])
    (*aFunc)(mRuleProcessors[eOverrideSheet], aData);
}

PRBool nsStyleSet::BuildDefaultStyleData(nsPresContext* aPresContext)
{
  NS_ASSERTION(!mDefaultStyleData.mResetData &&
               !mDefaultStyleData.mInheritedData,
               "leaking default style data");
  mDefaultStyleData.mResetData = new (aPresContext) nsResetStyleData;
  if (!mDefaultStyleData.mResetData)
    return PR_FALSE;
  mDefaultStyleData.mInheritedData = new (aPresContext) nsInheritedStyleData;
  if (!mDefaultStyleData.mInheritedData)
    return PR_FALSE;

#define SSARG_PRESCONTEXT aPresContext

#define CREATE_DATA(name, type, args) \
  if (!(mDefaultStyleData.m##type##Data->m##name##Data = \
          new (aPresContext) nsStyle##name args)) \
    return PR_FALSE;

#define STYLE_STRUCT_INHERITED(name, checkdata_cb, ctor_args) \
  CREATE_DATA(name, Inherited, ctor_args)
#define STYLE_STRUCT_RESET(name, checkdata_cb, ctor_args) \
  CREATE_DATA(name, Reset, ctor_args)

#include "nsStyleStructList.h"

#undef STYLE_STRUCT_INHERITED
#undef STYLE_STRUCT_RESET
#undef SSARG_PRESCONTEXT

  return PR_TRUE;
}

already_AddRefed<nsStyleContext>
nsStyleSet::ResolveStyleFor(nsIContent* aContent,
                            nsStyleContext* aParentContext)
{
  NS_ENSURE_FALSE(mInShutdown, nsnull);
  
  nsStyleContext*  result = nsnull;
  nsPresContext* presContext = PresContext();

  NS_ASSERTION(aContent, "must have content");
  NS_ASSERTION(aContent->IsNodeOfType(nsINode::eELEMENT),
               "content must be element");

  if (aContent && presContext) {
    nsRuleWalker ruleWalker(mRuleTree);
    ElementRuleProcessorData data(presContext, aContent, &ruleWalker);
    FileRules(EnumRulesMatching, &data, &ruleWalker);
    result = GetContext(presContext, aParentContext,
                        ruleWalker.GetCurrentNode(), nsnull).get();
  }

  return result;
}

already_AddRefed<nsStyleContext>
nsStyleSet::ResolveStyleForRules(nsStyleContext* aParentContext,
                                 nsIAtom* aPseudoTag,
                                 nsRuleNode *aRuleNode,
                                 const nsCOMArray<nsIStyleRule> &aRules)
{
  NS_ENSURE_FALSE(mInShutdown, nsnull);
  nsStyleContext* result = nsnull;
  nsPresContext *presContext = PresContext();

  if (presContext) {
    nsRuleWalker ruleWalker(mRuleTree);
    if (aRuleNode)
      ruleWalker.SetCurrentNode(aRuleNode);
    // FIXME: Perhaps this should be passed in, but it probably doesn't
    // matter.
    ruleWalker.SetLevel(eDocSheet, PR_FALSE);
    for (PRInt32 i = 0; i < aRules.Count(); i++) {
      ruleWalker.Forward(aRules.ObjectAt(i));
    }
    result = GetContext(presContext, aParentContext,
                        ruleWalker.GetCurrentNode(), aPseudoTag).get();
  }
  return result;
}

already_AddRefed<nsStyleContext>
nsStyleSet::ResolveStyleForNonElement(nsStyleContext* aParentContext)
{
  nsStyleContext* result = nsnull;
  nsPresContext *presContext = PresContext();

  if (presContext) {
    result = GetContext(presContext, aParentContext, mRuleTree,
                        nsCSSAnonBoxes::mozNonElement).get();
  }

  return result;
}

void
nsStyleSet::WalkRestrictionRule(nsIAtom* aPseudoType,
                                nsRuleWalker* aRuleWalker)
{
  // This needs to match GetPseudoRestriction in nsRuleNode.cpp.
  if (aPseudoType) {
    aRuleWalker->SetLevel(eAgentSheet, PR_FALSE);
    if (aPseudoType == nsCSSPseudoElements::firstLetter)
      aRuleWalker->Forward(mFirstLetterRule);
    else if (aPseudoType == nsCSSPseudoElements::firstLine)
      aRuleWalker->Forward(mFirstLineRule);
  }
}

static PRBool
EnumPseudoRulesMatching(nsIStyleRuleProcessor* aProcessor, void* aData)
{
  PseudoRuleProcessorData* data =
    static_cast<PseudoRuleProcessorData*>(aData);

  aProcessor->RulesMatching(data);
  return PR_TRUE;
}

already_AddRefed<nsStyleContext>
nsStyleSet::ResolvePseudoStyleFor(nsIContent* aParentContent,
                                  nsIAtom* aPseudoTag,
                                  nsStyleContext* aParentContext,
                                  nsICSSPseudoComparator* aComparator)
{
  NS_ENSURE_FALSE(mInShutdown, nsnull);

  nsStyleContext*  result = nsnull;
  nsPresContext *presContext = PresContext();

  NS_ASSERTION(aPseudoTag, "must have pseudo tag");
  NS_ASSERTION(!aParentContent ||
               aParentContent->IsNodeOfType(nsINode::eELEMENT),
               "content (if non-null) must be element");
  NS_ASSERTION(aParentContent ||
               nsCSSAnonBoxes::IsAnonBox(aPseudoTag),
               "null content must correspond to anonymous box");
  NS_ASSERTION(nsCSSAnonBoxes::IsAnonBox(aPseudoTag) ||
               nsCSSPseudoElements::IsPseudoElement(aPseudoTag),
               "aPseudoTag must be pseudo-element or anonymous box");

  if (aPseudoTag && presContext) {
    nsRuleWalker ruleWalker(mRuleTree);
    PseudoRuleProcessorData data(presContext, aParentContent, aPseudoTag,
                                 aComparator, &ruleWalker);
    WalkRestrictionRule(aPseudoTag, &ruleWalker);
    FileRules(EnumPseudoRulesMatching, &data, &ruleWalker);

    result = GetContext(presContext, aParentContext,
                        ruleWalker.GetCurrentNode(), aPseudoTag).get();
  }

  return result;
}

already_AddRefed<nsStyleContext>
nsStyleSet::ProbePseudoStyleFor(nsIContent* aParentContent,
                                nsIAtom* aPseudoTag,
                                nsStyleContext* aParentContext)
{
  NS_ENSURE_FALSE(mInShutdown, nsnull);
  
  nsStyleContext*  result = nsnull;
  nsPresContext *presContext = PresContext();

  NS_ASSERTION(aPseudoTag, "must have pseudo tag");
  NS_ASSERTION(aParentContent &&
               aParentContent->IsNodeOfType(nsINode::eELEMENT),
               "aParentContent must be element");
  //NS_ASSERTION(nsCSSPseudoElements::IsPseudoElement(aPseudoTag),
  //             "aPseudoTag must be a pseudo-element");
  NS_ASSERTION(aParentContent ||
               nsCSSAnonBoxes::IsAnonBox(aPseudoTag),
               "null content must correspond to anonymous box");
  NS_ASSERTION(nsCSSAnonBoxes::IsAnonBox(aPseudoTag) ||
               nsCSSPseudoElements::IsPseudoElement(aPseudoTag),
               "aPseudoTag must be pseudo-element or anonymous box");

  if (aPseudoTag && presContext) {
    nsRuleWalker ruleWalker(mRuleTree);
    PseudoRuleProcessorData data(presContext, aParentContent, aPseudoTag,
                                 nsnull, &ruleWalker);
    WalkRestrictionRule(aPseudoTag, &ruleWalker);
    // not the root if there was a restriction rule
    nsRuleNode *adjustedRoot = ruleWalker.GetCurrentNode();
    FileRules(EnumPseudoRulesMatching, &data, &ruleWalker);

    nsRuleNode *ruleNode = ruleWalker.GetCurrentNode();
    if (ruleNode != adjustedRoot)
      result =
        GetContext(presContext, aParentContext, ruleNode, aPseudoTag).get();
  }

  // For :before and :after pseudo-elements, having display: none or no
  // 'content' property is equivalent to not having the pseudo-element
  // at all.
  if (result &&
      (aPseudoTag == nsCSSPseudoElements::before ||
       aPseudoTag == nsCSSPseudoElements::after)) {
    const nsStyleDisplay *display = result->GetStyleDisplay();
    const nsStyleContent *content = result->GetStyleContent();
    // XXXldb What is contentCount for |content: ""|?
    if (display->mDisplay == NS_STYLE_DISPLAY_NONE ||
        content->ContentCount() == 0) {
      result->Release();
      result = nsnull;
    }
  }
  
  return result;
}

PRBool
nsStyleSet::AppendFontFaceRules(nsPresContext* aPresContext,
                                nsTArray<nsFontFaceRuleContainer>& aArray)
{
  NS_ENSURE_FALSE(mInShutdown, PR_FALSE);

  for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(gCSSSheetTypes); ++i) {
    nsCSSRuleProcessor *ruleProc = static_cast<nsCSSRuleProcessor*>
                                    (mRuleProcessors[gCSSSheetTypes[i]].get());
    if (ruleProc && !ruleProc->AppendFontFaceRules(aPresContext, aArray))
      return PR_FALSE;
  }
  return PR_TRUE;
}

void
nsStyleSet::BeginShutdown(nsPresContext* aPresContext)
{
  mInShutdown = 1;
  mRoots.Clear(); // no longer valid, since we won't keep it up to date
}

void
nsStyleSet::Shutdown(nsPresContext* aPresContext)
{
  mRuleTree->Destroy();
  mRuleTree = nsnull;

  // We can have old rule trees either because:
  //   (1) we failed the assertions in EndReconstruct, or
  //   (2) we're shutting down within a reconstruct (see bug 462392)
  for (PRUint32 i = mOldRuleTrees.Length(); i > 0; ) {
    --i;
    mOldRuleTrees[i]->Destroy();
  }
  mOldRuleTrees.Clear();

  mDefaultStyleData.Destroy(0, aPresContext);
}

static const PRInt32 kGCInterval = 1000;

void
nsStyleSet::NotifyStyleContextDestroyed(nsPresContext* aPresContext,
                                        nsStyleContext* aStyleContext)
{
  if (mInShutdown)
    return;

  // Remove style contexts from mRoots even if mOldRuleTree is non-null.  This
  // could be a style context from the new ruletree!
  if (!aStyleContext->GetParent()) {
    mRoots.RemoveElement(aStyleContext);
  }

  if (mInReconstruct)
    return;

  if (++mDestroyedCount == kGCInterval) {
    GCRuleTrees();
  }
}

void
nsStyleSet::GCRuleTrees()
{
  mDestroyedCount = 0;

  // Mark the style context tree by marking all style contexts which
  // have no parent, which will mark all descendants.  This will reach
  // style contexts in the undisplayed map and "additional style
  // contexts" since they are descendants of the roots.
  for (PRInt32 i = mRoots.Length() - 1; i >= 0; --i) {
    mRoots[i]->Mark();
  }

  // Sweep the rule tree.
#ifdef DEBUG
  PRBool deleted =
#endif
    mRuleTree->Sweep();
  NS_ASSERTION(!deleted, "Root node must not be gc'd");

  // Sweep the old rule trees.
  for (PRUint32 i = mOldRuleTrees.Length(); i > 0; ) {
    --i;
    if (mOldRuleTrees[i]->Sweep()) {
      // It was deleted, as it should be.
      mOldRuleTrees.RemoveElementAt(i);
    } else {
      NS_NOTREACHED("old rule tree still referenced");
    }
  }
}

already_AddRefed<nsStyleContext>
nsStyleSet::ReParentStyleContext(nsPresContext* aPresContext,
                                 nsStyleContext* aStyleContext, 
                                 nsStyleContext* aNewParentContext)
{
  NS_ASSERTION(aPresContext, "must have pres context");
  NS_ASSERTION(aStyleContext, "must have style context");

  if (aPresContext && aStyleContext) {
    if (aStyleContext->GetParent() == aNewParentContext) {
      aStyleContext->AddRef();
      return aStyleContext;
    }
    else {  // really a new parent
      nsIAtom* pseudoTag = aStyleContext->GetPseudoType();
      nsRuleNode* ruleNode = aStyleContext->GetRuleNode();

      already_AddRefed<nsStyleContext> result =
          GetContext(aPresContext, aNewParentContext, ruleNode, pseudoTag);
      return result;
    }
  }
  return nsnull;
}

struct StatefulData : public StateRuleProcessorData {
  StatefulData(nsPresContext* aPresContext,
               nsIContent* aContent, PRInt32 aStateMask)
    : StateRuleProcessorData(aPresContext, aContent, aStateMask),
      mHint(nsReStyleHint(0))
  {}
  nsReStyleHint   mHint;
}; 

static PRBool SheetHasStatefulStyle(nsIStyleRuleProcessor* aProcessor,
                                    void *aData)
{
  StatefulData* data = (StatefulData*)aData;
  nsReStyleHint hint;
  aProcessor->HasStateDependentStyle(data, &hint);
  data->mHint = nsReStyleHint(data->mHint | hint);
  return PR_TRUE; // continue
}

// Test if style is dependent on content state
nsReStyleHint
nsStyleSet::HasStateDependentStyle(nsPresContext* aPresContext,
                                   nsIContent*     aContent,
                                   PRInt32         aStateMask)
{
  nsReStyleHint result = nsReStyleHint(0);

  if (aContent->IsNodeOfType(nsINode::eELEMENT)) {
    StatefulData data(aPresContext, aContent, aStateMask);
    WalkRuleProcessors(SheetHasStatefulStyle, &data);
    result = data.mHint;
  }

  return result;
}

struct AttributeData : public AttributeRuleProcessorData {
  AttributeData(nsPresContext* aPresContext,
                nsIContent* aContent, nsIAtom* aAttribute, PRInt32 aModType,
                PRUint32 aStateMask)
    : AttributeRuleProcessorData(aPresContext, aContent, aAttribute, aModType,
                                 aStateMask),
      mHint(nsReStyleHint(0))
  {}
  nsReStyleHint   mHint;
}; 

static PRBool
SheetHasAttributeStyle(nsIStyleRuleProcessor* aProcessor, void *aData)
{
  AttributeData* data = (AttributeData*)aData;
  nsReStyleHint hint;
  aProcessor->HasAttributeDependentStyle(data, &hint);
  data->mHint = nsReStyleHint(data->mHint | hint);
  return PR_TRUE; // continue
}

// Test if style is dependent on content state
nsReStyleHint
nsStyleSet::HasAttributeDependentStyle(nsPresContext* aPresContext,
                                       nsIContent*    aContent,
                                       nsIAtom*       aAttribute,
                                       PRInt32        aModType,
                                       PRUint32       aStateMask)
{
  nsReStyleHint result = nsReStyleHint(0);

  if (aContent->IsNodeOfType(nsINode::eELEMENT)) {
    AttributeData data(aPresContext, aContent, aAttribute, aModType,
                       aStateMask);
    WalkRuleProcessors(SheetHasAttributeStyle, &data);
    result = data.mHint;
  }

  return result;
}

PRBool
nsStyleSet::MediumFeaturesChanged(nsPresContext* aPresContext)
{
  // We can't use WalkRuleProcessors without a content node.
  PRBool stylesChanged = PR_FALSE;
  for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(mRuleProcessors); ++i) {
    nsIStyleRuleProcessor *processor = mRuleProcessors[i];
    if (!processor) {
      continue;
    }
    PRBool thisChanged = PR_FALSE;
    processor->MediumFeaturesChanged(aPresContext, &thisChanged);
    stylesChanged = stylesChanged || thisChanged;
  }

  if (mBindingManager) {
    PRBool thisChanged = PR_FALSE;
    mBindingManager->MediumFeaturesChanged(aPresContext, &thisChanged);
    stylesChanged = stylesChanged || thisChanged;
  }

  return stylesChanged;
}
