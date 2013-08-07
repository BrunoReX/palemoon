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
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Jeff Muizelaar <jmuizelaar@mozilla.com>
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

#include <windows.h>
#include <tlhelp32.h>

#include "shared-libraries.h"
#include "nsWindowsHelpers.h"

SharedLibraryInfo SharedLibraryInfo::GetInfoForSelf()
{
  SharedLibraryInfo sharedLibraryInfo;

  nsAutoHandle snap(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId()));

  MODULEENTRY32 module = {0};
  module.dwSize = sizeof(MODULEENTRY32);
  if (Module32First(snap, &module)) {
    do {
      SharedLibrary shlib((uintptr_t)module.modBaseAddr,
                          (uintptr_t)module.modBaseAddr+module.modBaseSize,
                          0, // DLLs are always mapped at offset 0 on Windows
                          module.szModule);
      sharedLibraryInfo.AddSharedLibrary(shlib);
    } while (Module32Next(snap, &module));
  }

  return sharedLibraryInfo;
}

