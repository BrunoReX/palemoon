/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Ehsan Akhgari <ehsan@mozilla.com> (Original Author)
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

#ifndef nsSetDllDirectory_h
#define nsSetDllDirectory_h

#ifndef XP_WIN
#error This file only makes sense on Windows.
#endif

#include <windows.h>
#include <nscore.h>
#include <stdlib.h>

namespace mozilla {

static void SanitizeEnvironmentVariables()
{
  DWORD bufferSize = GetEnvironmentVariableW(L"PATH", NULL, 0);
  if (bufferSize) {
    wchar_t* originalPath = new wchar_t[bufferSize];
    if (bufferSize - 1 == GetEnvironmentVariableW(L"PATH", originalPath, bufferSize)) {
      bufferSize = ExpandEnvironmentStringsW(originalPath, NULL, 0);
      if (bufferSize) {
        wchar_t* newPath = new wchar_t[bufferSize];
        if (ExpandEnvironmentStringsW(originalPath,
                                      newPath,
                                      bufferSize)) {
          SetEnvironmentVariableW(L"PATH", newPath);
        }
        delete[] newPath;
      }
    }
    delete[] originalPath;
  }
}

// Sets the directory from which DLLs can be loaded if the SetDllDirectory OS
// API is available.
// You must call SanitizeEnvironmentVariables before this function when calling
// it the first time.
static inline void NS_SetDllDirectory(const WCHAR *aDllDirectory)
{
  typedef BOOL
  (WINAPI *pfnSetDllDirectory) (LPCWSTR);
  pfnSetDllDirectory setDllDirectory = nsnull;
  setDllDirectory = reinterpret_cast<pfnSetDllDirectory>
      (GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetDllDirectoryW"));
  if (setDllDirectory) {
    setDllDirectory(aDllDirectory);
  }
}

}

#endif
