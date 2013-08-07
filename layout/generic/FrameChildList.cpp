/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set shiftwidth=2 tabstop=8 autoindent cindent expandtab: */
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
 * The Initial Developer of the Original Code is the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Mats Palmgren <matspal@gmail.com> (original author)
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

#include "mozilla/layout/FrameChildList.h"

#include "nsIFrame.h"

namespace mozilla {
namespace layout {

FrameChildListIterator::FrameChildListIterator(const nsIFrame* aFrame)
  : FrameChildListArrayIterator(mLists)
{
  aFrame->GetChildLists(&mLists);
#ifdef DEBUG
  // Make sure that there are no duplicate list IDs.
  FrameChildListIDs ids;
  PRUint32 count = mLists.Length();
  for (PRUint32 i = 0; i < count; ++i) {
    NS_ASSERTION(!ids.Contains(mLists[i].mID),
                 "Duplicate item found!");
    ids |= mLists[i].mID;
  }
#endif
}

#ifdef DEBUG
const char*
ChildListName(FrameChildListID aListID)
{
  switch (aListID) {
    case kPrincipalList: return "";
    case kPopupList: return "PopupList";
    case kCaptionList: return "CaptionList";
    case kColGroupList: return "ColGroupList";
    case kSelectPopupList: return "SelectPopupList";
    case kAbsoluteList: return "AbsoluteList";
    case kFixedList: return "FixedList";
    case kOverflowList: return "OverflowList";
    case kOverflowContainersList: return "OverflowContainersList";
    case kExcessOverflowContainersList: return "ExcessOverflowContainersList";
    case kOverflowOutOfFlowList: return "OverflowOutOfFlowList";
    case kFloatList: return "FloatList";
    case kBulletList: return "BulletList";
    case kPushedFloatsList: return "PushedFloatsList";
    case kNoReflowPrincipalList: return "NoReflowPrincipalList";
  }

  NS_NOTREACHED("unknown list");
  return "UNKNOWN_FRAME_CHILD_LIST";
}
#endif

} // namespace layout
} // namespace mozilla
