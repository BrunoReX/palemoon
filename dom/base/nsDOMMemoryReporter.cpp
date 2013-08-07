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
 * The Original Code is Mozilla code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Mounir Lamouri <mounir.lamouri@mozilla.com> (original author)
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

#include "nsDOMMemoryReporter.h"
#include "nsGlobalWindow.h"


nsDOMMemoryMultiReporter::nsDOMMemoryMultiReporter()
{
}

NS_IMPL_ISUPPORTS1(nsDOMMemoryMultiReporter, nsIMemoryMultiReporter)

/* static */
void
nsDOMMemoryMultiReporter::Init()
{
  // The memory reporter manager is going to own this object.
  NS_RegisterMemoryMultiReporter(new nsDOMMemoryMultiReporter());
}

static bool
AppendWindowURI(nsGlobalWindow *aWindow, nsACString& aStr)
{
  nsCOMPtr<nsIDocument> doc = do_QueryInterface(aWindow->GetExtantDocument());
  nsCOMPtr<nsIURI> uri;

  if (doc) {
    uri = doc->GetDocumentURI();
  }

  if (!uri) {
    nsIPrincipal *principal = aWindow->GetPrincipal();

    if (principal) {
      principal->GetURI(getter_AddRefs(uri));
    }
  }

  if (!uri) {
    return false;
  }

  nsCString spec;
  uri->GetSpec(spec);

  // A hack: replace forward slashes with '\\' so they aren't
  // treated as path separators.  Users of the reporters
  // (such as about:memory) have to undo this change.
  spec.ReplaceChar('/', '\\');

  aStr += spec;

  return true;
}

static void
CollectWindowMemoryUsage(nsGlobalWindow *aWindow,
                         nsIMemoryMultiReporterCallback *aCb,
                         nsISupports *aClosure)
{
  NS_NAMED_LITERAL_CSTRING(kWindowDesc,
                           "Memory used by a window and the DOM within it.");

  // DOM window objects fall into one of three categories:
  // - "active" windows are currently either displayed in an active
  //   tab, or a child of such a window.
  // - "cached" windows are in the fastback cache.
  // - "other" windows are closed (or navigated away from w/o being
  //   cached) yet held alive by either a website or our code. The
  //   latter case may be a memory leak, but not necessarily.
  //
  // For inner windows we show how much memory the window and its
  // document etc use, and we report those per URI, where the URI is
  // the document URI, if available, or the codebase of the principal
  // in the window. In the case where we're unable to find a URI we're
  // dealing with a chrome window with no document in it (or
  // somesuch), and for that we make the URI be the string "[system]".
  //
  // For outer windows we simply group them all together and just show
  // the combined count and amount of memory used, which is generally
  // a constant amount per window (since all the actual data lives in
  // the inner window).
  //
  // The path we give to the reporter callback for inner windows are
  // as follows:
  //
  //   explicit/dom/window-objects/<category>/top=<top-outer-id> (inner=<top-inner-id>)/inner-window(id=<id>, uri=<uri>)
  //
  // Where:
  // - <category> is active, cached, or other, as described above.
  // - <top-outer-id> is the window id (nsPIDOMWindow::WindowID()) of
  //   the top outer window (i.e. tab, or top level chrome window).
  // - <top-inner-id> is the window id of the top window's inner
  //   window.
  // - <id> is the window id of the inner window in question.
  // - <uri> is the URI per above description.
  //
  // Exposing the window ids is done to get logical grouping in
  // about:memory, and also for debuggability since one can get to the
  // nsGlobalWindow for a window id by calling the static method
  // nsGlobalWindow::GetInnerWindowWithId(id) (or
  // GetOuterWindowWithId(id) in a debugger.
  //
  // For outer windows we simply use:
  // 
  //   explicit/dom/window-objects/<category>/outer-windows
  //
  // Which gives us simple counts of how many outer windows (and their
  // combined sizes) per category.

  nsCAutoString str("explicit/dom/window-objects/");

  nsIDocShell *docShell = aWindow->GetDocShell();

  nsGlobalWindow *top = aWindow->GetTop();
  PRInt64 windowSize = aWindow->SizeOf();

  if (docShell && aWindow->IsFrozen()) {
    str += NS_LITERAL_CSTRING("cached/");
  } else if (docShell) {
    str += NS_LITERAL_CSTRING("active/");
  } else {
    str += NS_LITERAL_CSTRING("other/");
  }

  if (aWindow->IsInnerWindow()) {
    str += NS_LITERAL_CSTRING("top=");

    if (top) {
      str.AppendInt(top->WindowID());

      nsGlobalWindow *topInner = top->GetCurrentInnerWindowInternal();
      if (topInner) {
        str += NS_LITERAL_CSTRING(" (inner=");
        str.AppendInt(topInner->WindowID());
        str += NS_LITERAL_CSTRING(")");
      }
    } else {
      str += NS_LITERAL_CSTRING("none");
    }

    str += NS_LITERAL_CSTRING("/inner-window(id=");
    str.AppendInt(aWindow->WindowID());
    str += NS_LITERAL_CSTRING(", uri=");

    if (!AppendWindowURI(aWindow, str)) {
      str += NS_LITERAL_CSTRING("[system]");
    }

    str += NS_LITERAL_CSTRING(")");
  } else {
    // Combine all outer windows per section (active/cached/other) as
    // they basically never contain anything of interest, and are
    // always pretty much the same size.

    str += NS_LITERAL_CSTRING("outer-windows");
  }

  aCb->Callback(EmptyCString(), str, nsIMemoryReporter::KIND_HEAP,
                nsIMemoryReporter::UNITS_BYTES, windowSize, kWindowDesc,
                aClosure);
}

typedef nsTArray< nsRefPtr<nsGlobalWindow> > WindowArray;

static
PLDHashOperator
GetWindows(const PRUint64& aId, nsGlobalWindow*& aWindow, void* aClosure)
{
  ((WindowArray *)aClosure)->AppendElement(aWindow);

  return PL_DHASH_NEXT;
}

NS_IMETHODIMP
nsDOMMemoryMultiReporter::CollectReports(nsIMemoryMultiReporterCallback* aCb,
                                         nsISupports* aClosure)
{
  nsGlobalWindow::WindowByIdTable* windowsById =
    nsGlobalWindow::GetWindowsTable();
  NS_ENSURE_TRUE(windowsById, NS_OK);

  // Hold on to every window in memory so that window objects can't be
  // destroyed while we're calling the memory reporter callback.
  WindowArray windows;
  windowsById->Enumerate(GetWindows, &windows);

  // Collect window memory usage.
  nsRefPtr<nsGlobalWindow> *w = windows.Elements();
  nsRefPtr<nsGlobalWindow> *end = w + windows.Length();
  for (; w != end; ++w) {
    CollectWindowMemoryUsage(*w, aCb, aClosure);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDOMMemoryMultiReporter::GetExplicitNonHeap(PRInt64* aAmount)
{
  // This reporter only measures heap memory.
  *aAmount = 0;
  return NS_OK;
}


