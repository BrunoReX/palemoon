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

/* debugging interface for all rendering objects */

#ifndef nsIFrameDebug_h___
#define nsIFrameDebug_h___

#include "nsIFrame.h"

class nsPresContext;
struct PRLogModuleInfo;

/**
 * Debug related functions
 */
class nsIFrameDebug {
public:
  NS_DECL_QUERYFRAME_TARGET(nsIFrameDebug)

  NS_IMETHOD  List(FILE* out, PRInt32 aIndent) const = 0;
  /**
   * lists the frames beginning from the root frame
   * - calls root frame's List(...)
   */
  static void RootFrameList(nsPresContext* aPresContext, FILE* out, PRInt32 aIndent);

  static void DumpFrameTree(nsIFrame* aFrame);

  /**
   * Get a printable from of the name of the frame type.
   * XXX This should be eliminated and we use GetType() instead...
   */
  NS_IMETHOD  GetFrameName(nsAString& aResult) const = 0;
  /**
   * Return the state bits that are relevant to regression tests (that is, those bits which indicate a real difference when they differ
   */
  NS_IMETHOD_(nsFrameState)  GetDebugStateBits() const = 0;
  /**
   * Called to dump out regression data that describes the layout
   * of the frame and its children, and so on. The format of the
   * data is dictated to be XML (using a specific DTD); the
   * specific kind of data dumped is up to the frame itself, with
   * the caveat that some base types are defined.
   * For more information, see XXX.
   */
  NS_IMETHOD  DumpRegressionData(nsPresContext* aPresContext, FILE* out, PRInt32 aIndent) = 0;

  /**
   * See if style tree verification is enabled. To enable style tree 
   * verification add "styleverifytree:1" to your NSPR_LOG_MODULES 
   * environment variable (any non-zero debug level will work). Or, 
   * call SetVerifyStyleTreeEnable with PR_TRUE.
   */
  static PRBool GetVerifyStyleTreeEnable();

  /**
   * Set the verify-style-tree enable flag.
   */
  static void SetVerifyStyleTreeEnable(PRBool aEnabled);

  /**
   * The frame class and related classes share an nspr log module
   * for logging frame activity.
   *
   * Note: the log module is created during library initialization which
   * means that you cannot perform logging before then.
   */
  static PRLogModuleInfo* GetLogModuleInfo();

  // Show frame borders when rendering
  static void ShowFrameBorders(PRBool aEnable);
  static PRBool GetShowFrameBorders();

  // Show frame border of event target
  static void ShowEventTargetFrameBorder(PRBool aEnable);
  static PRBool GetShowEventTargetFrameBorder();
  
  static void PrintDisplayList(nsDisplayListBuilder* aBuilder, const nsDisplayList& aList);
};

#endif /* nsIFrameDebug_h___ */
