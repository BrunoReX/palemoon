/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at:
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Code.
 *
 * The Initial Developer of the Original Code is
 *   The Mozilla Foundation
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Justin Lebar <justin.lebar@gmail.com>
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

#ifndef mozilla_hal_WindowIdentifier_h
#define mozilla_hal_WindowIdentifier_h

#include "mozilla/Types.h"
#include "nsTArray.h"
#include "nsCOMPtr.h"
#include "nsIDOMWindow.h"

namespace mozilla {
namespace hal {

/**
 * This class serves two purposes.
 *
 * First, this class wraps a pointer to a window.
 *
 * Second, WindowIdentifier lets us uniquely identify a window across
 * processes.  A window exposes an ID which is unique only within its
 * process.  Thus to identify a window, we need to know the ID of the
 * process which contains it.  But the scope of a process's ID is its
 * parent; that is, two processes with different parents might have
 * the same ID.
 *
 * So to identify a window, we need its ID plus the IDs of all the
 * processes in the path from the window's process to the root
 * process.  We throw in the IDs of the intermediate windows (a
 * content window is contained in a window at each level of the
 * process tree) for good measures.
 *
 * You can access this list of IDs by calling AsArray().
 */
class WindowIdentifier
{
public:
  /**
   * Create an empty WindowIdentifier.  Calls to any of this object's
   * public methods will assert -- an empty WindowIdentifier may be
   * used only as a placeholder to code which promises not to touch
   * the object.
   */
  WindowIdentifier();

  /**
   * Copy constructor.
   */
  WindowIdentifier(const WindowIdentifier& other);

  /**
   * Wrap the given window in a WindowIdentifier.  These two
   * constructors automatically grab the window's ID and append it to
   * the array of IDs.
   *
   * Note that these constructors allow an implicit conversion to a
   * WindowIdentifier.
   */
  explicit WindowIdentifier(nsIDOMWindow* window);

  /**
   * Create a new WindowIdentifier with the given id array and window.
   * This automatically grabs the window's ID and appends it to the
   * array.
   */
  WindowIdentifier(const nsTArray<uint64>& id, nsIDOMWindow* window);

  /**
   * Get the list of window and process IDs we contain.
   */
  typedef InfallibleTArray<uint64> IDArrayType;
  const IDArrayType& AsArray() const;

  /**
   * Append the ID of the ContentChild singleton to our array of
   * window/process IDs.
   */
  void AppendProcessID();

  /**
   * Does this WindowIdentifier identify both a window and the process
   * containing that window?  If so, we say it has traveled through
   * IPC.
   */
  bool HasTraveledThroughIPC() const;

  /**
   * Get the window this object wraps.
   */
  nsIDOMWindow* GetWindow() const;

private:
  /**
   * Get the ID of the window object we wrap.
   */
  uint64 GetWindowID() const;

  AutoInfallibleTArray<uint64, 3> mID;
  nsCOMPtr<nsIDOMWindow> mWindow;
  bool mIsEmpty;
};

} // namespace hal
} // namespace mozilla

#endif // mozilla_hal_WindowIdentifier_h
