/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:expandtab:shiftwidth=2:tabstop=2:
 */
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
 * The Initial Developer of the Original Code is IBM Corporation
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Aaron Leventhal <aleventh@us.ibm.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#include "nsARIAMap.h"

#include "nsIAccessibleRole.h"
#include "Role.h"
#include "States.h"

#include "nsIContent.h"

using namespace mozilla::a11y;

/**
 *  This list of WAI-defined roles are currently hardcoded.
 *  Eventually we will most likely be loading an RDF resource that contains this information
 *  Using RDF will also allow for role extensibility. See bug 280138.
 *
 *  Definition of nsRoleMapEntry and nsStateMapEntry contains comments explaining this table.
 *
 *  When no nsIAccessibleRole enum mapping exists for an ARIA role, the
 *  role will be exposed via the object attribute "xml-roles".
 *  In addition, in MSAA, the unmapped role will also be exposed as a BSTR string role.
 *
 *  There are no nsIAccessibleRole enums for the following landmark roles:
 *    banner, contentinfo, main, navigation, note, search, secondary, seealso, breadcrumbs
 */

nsRoleMapEntry nsARIAMap::gWAIRoleMap[] = 
{
  {
    "alert",
    roles::ALERT,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates
  },
  {
    "alertdialog",
    roles::DIALOG,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates
  },
  {
    "application",
    roles::APPLICATION,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates
  },
  {
    "article",
    roles::DOCUMENT,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    states::READONLY
  },
  {
    "button",
    roles::PUSHBUTTON,
    kUseMapRole,
    eNoValue,
    eClickAction,
    eNoLiveAttr,
    kNoReqStates,
    eARIAPressed
  },
  {
    "checkbox",
    roles::CHECKBUTTON,
    kUseMapRole,
    eNoValue,
    eCheckUncheckAction,
    eNoLiveAttr,
    kNoReqStates,
    eARIACheckableMixed,
    eARIAReadonly
  },
  {
    "columnheader",
    roles::COLUMNHEADER,
    kUseMapRole,
    eNoValue,
    eSortAction,
    eNoLiveAttr,
    kNoReqStates,
    eARIASelectable,
    eARIAReadonly
  },
  {
    "combobox",
    roles::COMBOBOX,
    kUseMapRole,
    eHasValueMinMax,
    eOpenCloseAction,
    eNoLiveAttr,
    states::COLLAPSED | states::HASPOPUP,
    eARIAAutoComplete,
    eARIAReadonly
  },
  {
    "dialog",
    roles::DIALOG,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates
  },
  {
    "directory",
    roles::LIST,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates
  },
  {
    "document",
    roles::DOCUMENT,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    states::READONLY
  },
  {
    "grid",
    roles::TABLE,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    states::FOCUSABLE,
    eARIAMultiSelectable,
    eARIAReadonly
  },
  {
    "gridcell",
    roles::GRID_CELL,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates,
    eARIASelectable,
    eARIAReadonly
  },
  {
    "group",
    roles::GROUPING,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates
  },
  {
    "heading",
    roles::HEADING,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates
  },
  {
    "img",
    roles::GRAPHIC,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates
  },
  {
    "label",
    roles::LABEL,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates
  },
  {
    "link",
    roles::LINK,
    kUseMapRole,
    eNoValue,
    eJumpAction,
    eNoLiveAttr,
    states::LINKED
  },
  {
    "list",
    roles::LIST,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    states::READONLY,
    eARIAMultiSelectable
  },
  {
    "listbox",
    roles::LISTBOX,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates,
    eARIAMultiSelectable,
    eARIAReadonly
  },
  {
    "listitem",
    roles::LISTITEM,
    kUseMapRole,
    eNoValue,
    eNoAction, // XXX: should depend on state, parent accessible
    eNoLiveAttr,
    states::READONLY,
    eARIASelectable,
    eARIACheckedMixed
  },
  {
    "log",
    roles::NOTHING,
    kUseNativeRole,
    eNoValue,
    eNoAction,
    ePoliteLiveAttr,
    kNoReqStates
  },
  {
    "marquee",
    roles::ANIMATION,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eOffLiveAttr,
    kNoReqStates
  },
  {
    "math",
    roles::FLAT_EQUATION,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates
  },
  {
    "menu",
    roles::MENUPOPUP,
    kUseMapRole,
    eNoValue,
    eNoAction, // XXX: technically accessibles of menupopup role haven't
               // any action, but menu can be open or close.
    eNoLiveAttr,
    kNoReqStates
  },
  {
    "menubar",
    roles::MENUBAR,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates
  },
  {
    "menuitem",
    roles::MENUITEM,
    kUseMapRole,
    eNoValue,
    eClickAction,
    eNoLiveAttr,
    kNoReqStates,
    eARIACheckedMixed
  },
  {
    "menuitemcheckbox",
    roles::CHECK_MENU_ITEM,
    kUseMapRole,
    eNoValue,
    eClickAction,
    eNoLiveAttr,
    kNoReqStates,
    eARIACheckableMixed
  },
  {
    "menuitemradio",
    roles::RADIO_MENU_ITEM,
    kUseMapRole,
    eNoValue,
    eClickAction,
    eNoLiveAttr,
    kNoReqStates,
    eARIACheckableBool
  },
  {
    "option",
    roles::OPTION,
    kUseMapRole,
    eNoValue,
    eSelectAction,
    eNoLiveAttr,
    kNoReqStates,
    eARIASelectable,
    eARIACheckedMixed
  },
  {
    "presentation",
    roles::NOTHING,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates
  },
  {
    "progressbar",
    roles::PROGRESSBAR,
    kUseMapRole,
    eHasValueMinMax,
    eNoAction,
    eNoLiveAttr,
    states::READONLY
  },
  {
    "radio",
    roles::RADIOBUTTON,
    kUseMapRole,
    eNoValue,
    eSelectAction,
    eNoLiveAttr,
    kNoReqStates,
    eARIACheckableBool
  },
  {
    "radiogroup",
    roles::GROUPING,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates
  },
  {
    "region",
    roles::PANE,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates
  },
  {
    "row",
    roles::ROW,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates,
    eARIASelectable
  },
  {
    "rowheader",
    roles::ROWHEADER,
    kUseMapRole,
    eNoValue,
    eSortAction,
    eNoLiveAttr,
    kNoReqStates,
    eARIASelectable,
    eARIAReadonly
  },
  {
    "scrollbar",
    roles::SCROLLBAR,
    kUseMapRole,
    eHasValueMinMax,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates,
    eARIAOrientation,
    eARIAReadonly
  },
  {
    "separator",
    roles::SEPARATOR,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates,
    eARIAOrientation
  },
  {
    "slider",
    roles::SLIDER,
    kUseMapRole,
    eHasValueMinMax,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates,
    eARIAOrientation,
    eARIAReadonly
  },
  {
    "spinbutton",
    roles::SPINBUTTON,
    kUseMapRole,
    eHasValueMinMax,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates,
    eARIAReadonly
  },
  {
    "status",
    roles::STATUSBAR,
    kUseMapRole,
    eNoValue,
    eNoAction,
    ePoliteLiveAttr,
    kNoReqStates
  },
  {
    "tab",
    roles::PAGETAB,
    kUseMapRole,
    eNoValue,
    eSwitchAction,
    eNoLiveAttr,
    kNoReqStates,
    eARIASelectable
  },
  {
    "tablist",
    roles::PAGETABLIST,
    kUseMapRole,
    eNoValue,
    eNoAction,
    ePoliteLiveAttr,
    kNoReqStates
  },
  {
    "tabpanel",
    roles::PROPERTYPAGE,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates
  },
  {
    "textbox",
    roles::ENTRY,
    kUseMapRole,
    eNoValue,
    eActivateAction,
    eNoLiveAttr,
    kNoReqStates,
    eARIAAutoComplete,
    eARIAMultiline,
    eARIAReadonlyOrEditable
  },
  {
    "timer",
    roles::NOTHING,
    kUseNativeRole,
    eNoValue,
    eNoAction,
    eOffLiveAttr,
    kNoReqStates
  },
  {
    "toolbar",
    roles::TOOLBAR,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates
  },
  {
    "tooltip",
    roles::TOOLTIP,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates
  },
  {
    "tree",
    roles::OUTLINE,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates,
    eARIAReadonly,
    eARIAMultiSelectable
  },
  {
    "treegrid",
    roles::TREE_TABLE,
    kUseMapRole,
    eNoValue,
    eNoAction,
    eNoLiveAttr,
    kNoReqStates,
    eARIAReadonly,
    eARIAMultiSelectable
  },
  {
    "treeitem",
    roles::OUTLINEITEM,
    kUseMapRole,
    eNoValue,
    eActivateAction, // XXX: should expose second 'expand/collapse' action based
                     // on states
    eNoLiveAttr,
    kNoReqStates,
    eARIASelectable,
    eARIACheckedMixed
  }
};

PRUint32 nsARIAMap::gWAIRoleMapLength = NS_ARRAY_LENGTH(nsARIAMap::gWAIRoleMap);

nsRoleMapEntry nsARIAMap::gLandmarkRoleMap = {
  "",
  roles::NOTHING,
  kUseNativeRole,
  eNoValue,
  eNoAction,
  eNoLiveAttr,
  kNoReqStates
};

nsRoleMapEntry nsARIAMap::gEmptyRoleMap = {
  "",
  roles::NOTHING,
  kUseMapRole,
  eNoValue,
  eNoAction,
  eNoLiveAttr,
  kNoReqStates
};

nsStateMapEntry nsARIAMap::gWAIStateMap[] = {
  // eARIANone
  nsStateMapEntry(),

  // eARIAAutoComplete
  nsStateMapEntry(&nsGkAtoms::aria_autocomplete,
                  "inline", states::SUPPORTS_AUTOCOMPLETION,
                  "list", states::HASPOPUP | states::SUPPORTS_AUTOCOMPLETION,
                  "both", states::HASPOPUP | states::SUPPORTS_AUTOCOMPLETION),

  // eARIABusy
  nsStateMapEntry(&nsGkAtoms::aria_busy,
                  "true", states::BUSY,
                  "error", states::INVALID),

  // eARIACheckableBool
  nsStateMapEntry(&nsGkAtoms::aria_checked, kBoolType,
                  states::CHECKABLE, states::CHECKED, 0, true),

  // eARIACheckableMixed
  nsStateMapEntry(&nsGkAtoms::aria_checked, kMixedType,
                  states::CHECKABLE, states::CHECKED, 0, true),

  // eARIACheckedMixed
  nsStateMapEntry(&nsGkAtoms::aria_checked, kMixedType,
                  states::CHECKABLE, states::CHECKED, 0),

  // eARIADisabled
  nsStateMapEntry(&nsGkAtoms::aria_disabled, kBoolType,
                  0, states::UNAVAILABLE),

  // eARIAExpanded
  nsStateMapEntry(&nsGkAtoms::aria_expanded, kBoolType,
                  0, states::EXPANDED, states::COLLAPSED),

  // eARIAHasPopup
  nsStateMapEntry(&nsGkAtoms::aria_haspopup, kBoolType,
                  0, states::HASPOPUP),

  // eARIAInvalid
  nsStateMapEntry(&nsGkAtoms::aria_invalid, kBoolType,
                  0, states::INVALID),

  // eARIAMultiline
  nsStateMapEntry(&nsGkAtoms::aria_multiline, kBoolType,
                  0, states::MULTI_LINE, states::SINGLE_LINE, true),

  // eARIAMultiSelectable
  nsStateMapEntry(&nsGkAtoms::aria_multiselectable, kBoolType,
                  0, states::MULTISELECTABLE | states::EXTSELECTABLE),

  // eARIAOrientation
  nsStateMapEntry(&nsGkAtoms::aria_orientation, eUseFirstState,
                  "horizontal", states::HORIZONTAL,
                  "vertical", states::VERTICAL),

  // eARIAPressed
  nsStateMapEntry(&nsGkAtoms::aria_pressed, kMixedType,
                  states::CHECKABLE, states::PRESSED),

  // eARIAReadonly
  nsStateMapEntry(&nsGkAtoms::aria_readonly, kBoolType,
                  0, states::READONLY),

  // eARIAReadonlyOrEditable
  nsStateMapEntry(&nsGkAtoms::aria_readonly, kBoolType,
                  0, states::READONLY, states::EDITABLE, true),

  // eARIARequired
  nsStateMapEntry(&nsGkAtoms::aria_required, kBoolType,
                  0, states::REQUIRED),

  // eARIASelectable
  nsStateMapEntry(&nsGkAtoms::aria_selected, kBoolType,
                  states::SELECTABLE, states::SELECTED, 0, true)
};

/**
 * Universal (Global) states:
 * The following state rules are applied to any accessible element,
 * whether there is an ARIA role or not:
 */
eStateMapEntryID nsARIAMap::gWAIUnivStateMap[] = {
  eARIABusy,
  eARIADisabled,
  eARIAExpanded,  // Currently under spec review but precedent exists
  eARIAHasPopup,  // Note this is technically a "property"
  eARIAInvalid,
  eARIARequired,  // XXX not global, Bug 553117
  eARIANone
};


/**
 * ARIA attribute map for attribute characteristics
 * 
 * @note ARIA attributes that don't have any flags are not included here
 */
nsAttributeCharacteristics nsARIAMap::gWAIUnivAttrMap[] = {
  {&nsGkAtoms::aria_activedescendant,  ATTR_BYPASSOBJ                 },
  {&nsGkAtoms::aria_atomic,                             ATTR_VALTOKEN },
  {&nsGkAtoms::aria_busy,                               ATTR_VALTOKEN },
  {&nsGkAtoms::aria_checked,           ATTR_BYPASSOBJ | ATTR_VALTOKEN }, /* exposes checkable obj attr */
  {&nsGkAtoms::aria_controls,          ATTR_BYPASSOBJ                 },
  {&nsGkAtoms::aria_describedby,       ATTR_BYPASSOBJ                 },
  {&nsGkAtoms::aria_disabled,          ATTR_BYPASSOBJ | ATTR_VALTOKEN },
  {&nsGkAtoms::aria_dropeffect,                         ATTR_VALTOKEN },
  {&nsGkAtoms::aria_expanded,          ATTR_BYPASSOBJ | ATTR_VALTOKEN },
  {&nsGkAtoms::aria_flowto,            ATTR_BYPASSOBJ                 },  
  {&nsGkAtoms::aria_grabbed,                            ATTR_VALTOKEN },
  {&nsGkAtoms::aria_haspopup,          ATTR_BYPASSOBJ | ATTR_VALTOKEN },
  {&nsGkAtoms::aria_hidden,                             ATTR_VALTOKEN },/* always expose obj attr */
  {&nsGkAtoms::aria_invalid,           ATTR_BYPASSOBJ | ATTR_VALTOKEN },
  {&nsGkAtoms::aria_label,             ATTR_BYPASSOBJ                 },
  {&nsGkAtoms::aria_labelledby,        ATTR_BYPASSOBJ                 },
  {&nsGkAtoms::aria_level,             ATTR_BYPASSOBJ                 }, /* handled via groupPosition */
  {&nsGkAtoms::aria_live,                               ATTR_VALTOKEN },
  {&nsGkAtoms::aria_multiline,         ATTR_BYPASSOBJ | ATTR_VALTOKEN },
  {&nsGkAtoms::aria_multiselectable,   ATTR_BYPASSOBJ | ATTR_VALTOKEN },
  {&nsGkAtoms::aria_owns,              ATTR_BYPASSOBJ                 },
  {&nsGkAtoms::aria_orientation,                        ATTR_VALTOKEN },
  {&nsGkAtoms::aria_posinset,          ATTR_BYPASSOBJ                 }, /* handled via groupPosition */
  {&nsGkAtoms::aria_pressed,           ATTR_BYPASSOBJ | ATTR_VALTOKEN },
  {&nsGkAtoms::aria_readonly,          ATTR_BYPASSOBJ | ATTR_VALTOKEN },
  {&nsGkAtoms::aria_relevant,          ATTR_BYPASSOBJ                 },
  {&nsGkAtoms::aria_required,          ATTR_BYPASSOBJ | ATTR_VALTOKEN },
  {&nsGkAtoms::aria_selected,          ATTR_BYPASSOBJ | ATTR_VALTOKEN },
  {&nsGkAtoms::aria_setsize,           ATTR_BYPASSOBJ                 }, /* handled via groupPosition */
  {&nsGkAtoms::aria_sort,                               ATTR_VALTOKEN },
  {&nsGkAtoms::aria_valuenow,          ATTR_BYPASSOBJ                 },
  {&nsGkAtoms::aria_valuemin,          ATTR_BYPASSOBJ                 },
  {&nsGkAtoms::aria_valuemax,          ATTR_BYPASSOBJ                 },
  {&nsGkAtoms::aria_valuetext,         ATTR_BYPASSOBJ                 }
};

PRUint32 nsARIAMap::gWAIUnivAttrMapLength = NS_ARRAY_LENGTH(nsARIAMap::gWAIUnivAttrMap);


////////////////////////////////////////////////////////////////////////////////
// nsStateMapEntry

nsStateMapEntry::nsStateMapEntry() :
  mAttributeName(nsnull),
  mIsToken(false),
  mPermanentState(0),
  mValue1(nsnull),
  mState1(0),
  mValue2(nsnull),
  mState2(0),
  mValue3(nsnull),
  mState3(0),
  mDefaultState(0),
  mDefinedIfAbsent(false)
{}

nsStateMapEntry::nsStateMapEntry(nsIAtom** aAttrName, eStateValueType aType,
                                 PRUint64 aPermanentState,
                                 PRUint64 aTrueState,
                                 PRUint64 aFalseState,
                                 bool aDefinedIfAbsent) :
  mAttributeName(aAttrName),
  mIsToken(true),
  mPermanentState(aPermanentState),
  mValue1("false"),
  mState1(aFalseState),
  mValue2(nsnull),
  mState2(0),
  mValue3(nsnull),
  mState3(0),
  mDefaultState(aTrueState),
  mDefinedIfAbsent(aDefinedIfAbsent)
{
  if (aType == kMixedType) {
    mValue2 = "mixed";
    mState2 = states::MIXED;
  }
}

nsStateMapEntry::nsStateMapEntry(nsIAtom** aAttrName,
                                 const char* aValue1, PRUint64 aState1,
                                 const char* aValue2, PRUint64 aState2,
                                 const char* aValue3, PRUint64 aState3) :
  mAttributeName(aAttrName), mIsToken(false), mPermanentState(0),
  mValue1(aValue1), mState1(aState1),
  mValue2(aValue2), mState2(aState2),
  mValue3(aValue3), mState3(aState3),
  mDefaultState(0), mDefinedIfAbsent(false)
{
}

nsStateMapEntry::nsStateMapEntry(nsIAtom** aAttrName,
                                 EDefaultStateRule aDefaultStateRule,
                                 const char* aValue1, PRUint64 aState1,
                                 const char* aValue2, PRUint64 aState2,
                                 const char* aValue3, PRUint64 aState3) :
  mAttributeName(aAttrName), mIsToken(true), mPermanentState(0),
  mValue1(aValue1), mState1(aState1),
  mValue2(aValue2), mState2(aState2),
  mValue3(aValue3), mState3(aState3),
  mDefaultState(0), mDefinedIfAbsent(true)
{
  if (aDefaultStateRule == eUseFirstState)
    mDefaultState = aState1;
}

bool
nsStateMapEntry::MapToStates(nsIContent* aContent, PRUint64* aState,
                             eStateMapEntryID aStateMapEntryID)
{
  // Return true if we should continue.
  if (aStateMapEntryID == eARIANone)
    return false;

  const nsStateMapEntry& entry = nsARIAMap::gWAIStateMap[aStateMapEntryID];

  if (entry.mIsToken) {
    // If attribute is considered as defined when it's absent then let's act
    // attribute value is "false" supposedly.
    bool hasAttr = aContent->HasAttr(kNameSpaceID_None, *entry.mAttributeName);
    if (entry.mDefinedIfAbsent && !hasAttr) {
      if (entry.mPermanentState)
        *aState |= entry.mPermanentState;
      if (entry.mState1)
        *aState |= entry.mState1;
      return true;
    }

    // We only have attribute state mappings for NMTOKEN (and boolean) based
    // ARIA attributes. According to spec, a value of "undefined" is to be
    // treated equivalent to "", or the absence of the attribute. We bail out
    // for this case here.
    // Note: If this method happens to be called with a non-token based
    // attribute, for example: aria-label="" or aria-label="undefined", we will
    // bail out and not explore a state mapping, which is safe.
    if (!hasAttr ||
        aContent->AttrValueIs(kNameSpaceID_None, *entry.mAttributeName,
                              nsGkAtoms::_empty, eCaseMatters) ||
        aContent->AttrValueIs(kNameSpaceID_None, *entry.mAttributeName,
                              nsGkAtoms::_undefined, eCaseMatters)) {

      if (entry.mPermanentState)
        *aState &= ~entry.mPermanentState;
      return true;
    }

    if (entry.mPermanentState)
      *aState |= entry.mPermanentState;
  }

  nsAutoString attrValue;
  if (!aContent->GetAttr(kNameSpaceID_None, *entry.mAttributeName, attrValue))
    return true;

  // Apply states for matched value. If no values was matched then apply default
  // states.
  bool applyDefaultStates = true;
  if (entry.mValue1) {
    if (attrValue.EqualsASCII(entry.mValue1)) {
      applyDefaultStates = false;

      if (entry.mState1)
        *aState |= entry.mState1;
    } else if (entry.mValue2) {
      if (attrValue.EqualsASCII(entry.mValue2)) {
        applyDefaultStates = false;

        if (entry.mState2)
          *aState |= entry.mState2;

      } else if (entry.mValue3) {
        if (attrValue.EqualsASCII(entry.mValue3)) {
          applyDefaultStates = false;

          if (entry.mState3)
            *aState |= entry.mState3;

        }
      }
    }
  }

  if (applyDefaultStates) {
    if (entry.mDefaultState)
      *aState |= entry.mDefaultState;
  }

  return true;
}
