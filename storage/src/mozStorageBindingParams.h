/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=2 et lcs=trail\:.,tab\:>~ :
 * ***** BEGIN LICENSE BLOCK *****
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
 * Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Shawn Wilsher <me@shawnwilsher.com> (Original Author)
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

#ifndef _mozStorageBindingParams_h_
#define _mozStorageBindingParams_h_

#include "nsAutoPtr.h"
#include "nsCOMArray.h"
#include "nsIVariant.h"

#include "mozStorageBindingParamsArray.h"
#include "mozStorageStatement.h"
#include "mozIStorageBindingParams.h"

class mozIStorageError;
struct sqlite3_stmt;

namespace mozilla {
namespace storage {

class BindingParams : public mozIStorageBindingParams
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_MOZISTORAGEBINDINGPARAMS

  /**
   * Locks the parameters and prevents further modification to it (such as
   * binding more elements to it).
   */
  void lock();

  /**
   * Unlocks the parameters and allows modification to it again.
   */
  void unlock();

  /**
   * @returns the pointer to the owning BindingParamsArray.
   */
  const BindingParamsArray *getOwner() const;

  /**
   * Binds our stored data to the statement.
   *
   * @param aStatement
   *        The statement to bind our data to.
   * @returns nsnull on success, or a mozIStorageError object if an error
   *          occurred.
   */
  already_AddRefed<mozIStorageError> bind(sqlite3_stmt *aStatement);

  BindingParams(BindingParamsArray *aOwningArray,
                Statement *aOwningStatement);

private:
  nsRefPtr<BindingParamsArray> mOwningArray;
  Statement *mOwningStatement;
  nsCOMArray<nsIVariant> mParameters;
  PRUint32 mParamCount;
  bool mLocked;
};

} // namespace storage
} // namespace mozilla

#endif // _mozStorageBindingParams_h_
