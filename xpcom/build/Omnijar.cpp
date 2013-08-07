/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
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
 * The Original Code is Mozilla code.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Michael Wu <mwu@mozilla.com>
 *   Mike Hommey <mh@glandium.org>
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

#include "Omnijar.h"

#include "nsDirectoryService.h"
#include "nsDirectoryServiceDefs.h"
#include "nsIFile.h"
#include "nsZipArchive.h"
#include "nsNetUtil.h"

namespace mozilla {

nsIFile *Omnijar::sPath[2] = { nsnull, nsnull };
nsZipArchive *Omnijar::sReader[2] = { nsnull, nsnull };
bool Omnijar::sInitialized = false;
static bool sIsUnified = false;
static bool sIsNested[2] = { false, false };

static const char *sProp[2] =
    { NS_GRE_DIR, NS_XPCOM_CURRENT_PROCESS_DIR };

#define SPROP(Type) ((Type == mozilla::Omnijar::GRE) ? sProp[GRE] : sProp[APP])

void
Omnijar::CleanUpOne(Type aType)
{
    if (sReader[aType]) {
        sReader[aType]->CloseArchive();
        NS_IF_RELEASE(sReader[aType]);
    }
    sReader[aType] = nsnull;
    NS_IF_RELEASE(sPath[aType]);
}

void
Omnijar::InitOne(nsIFile *aPath, Type aType)
{
    nsCOMPtr<nsIFile> file;
    if (aPath) {
        file = aPath;
    } else {
        nsCOMPtr<nsIFile> dir;
        nsDirectoryService::gService->Get(SPROP(aType), NS_GET_IID(nsIFile), getter_AddRefs(dir));
        NS_NAMED_LITERAL_CSTRING (kOmnijarName, NS_STRINGIFY(OMNIJAR_NAME));
        if (NS_FAILED(dir->Clone(getter_AddRefs(file))) ||
            NS_FAILED(file->AppendNative(kOmnijarName)))
            return;
    }
    bool isFile;
    if (NS_FAILED(file->IsFile(&isFile)) || !isFile) {
        // If we're not using an omni.jar for GRE, and we don't have an
        // omni.jar for APP, check if both directories are the same.
        if ((aType == APP) && (!sPath[GRE])) {
            nsCOMPtr<nsIFile> greDir, appDir;
            bool equals;
            nsDirectoryService::gService->Get(SPROP(GRE), NS_GET_IID(nsIFile), getter_AddRefs(greDir));
            nsDirectoryService::gService->Get(SPROP(APP), NS_GET_IID(nsIFile), getter_AddRefs(appDir));
            if (NS_SUCCEEDED(greDir->Equals(appDir, &equals)) && equals)
                sIsUnified = true;
        }
        return;
    }

    bool equals;
    if ((aType == APP) && (sPath[GRE]) &&
        NS_SUCCEEDED(sPath[GRE]->Equals(file, &equals)) && equals) {
        // If we're using omni.jar on both GRE and APP and their path
        // is the same, we're in the unified case.
        sIsUnified = true;
        return;
    }

    nsRefPtr<nsZipArchive> zipReader = new nsZipArchive();
    if (NS_FAILED(zipReader->OpenArchive(file))) {
        return;
    }

    nsRefPtr<nsZipHandle> handle;
    if (NS_SUCCEEDED(nsZipHandle::Init(zipReader, NS_STRINGIFY(OMNIJAR_NAME), getter_AddRefs(handle)))) {
        zipReader = new nsZipArchive();
        if (NS_FAILED(zipReader->OpenArchive(handle)))
            return;
        sIsNested[aType] = true;
    }

    CleanUpOne(aType);
    sReader[aType] = zipReader;
    NS_IF_ADDREF(sReader[aType]);
    sPath[aType] = file;
    NS_IF_ADDREF(sPath[aType]);
}

void
Omnijar::Init(nsIFile *aGrePath, nsIFile *aAppPath)
{
    InitOne(aGrePath, GRE);
    InitOne(aAppPath, APP);
    sInitialized = true;
}

void
Omnijar::CleanUp()
{
    CleanUpOne(GRE);
    CleanUpOne(APP);
    sInitialized = false;
}

already_AddRefed<nsZipArchive>
Omnijar::GetReader(nsIFile *aPath)
{
    NS_ABORT_IF_FALSE(IsInitialized(), "Omnijar not initialized");

    bool equals;
    nsresult rv;

    if (sPath[GRE] && !sIsNested[GRE]) {
        rv = sPath[GRE]->Equals(aPath, &equals);
        if (NS_SUCCEEDED(rv) && equals)
            return GetReader(GRE);
    }
    if (sPath[APP] && !sIsNested[APP]) {
        rv = sPath[APP]->Equals(aPath, &equals);
        if (NS_SUCCEEDED(rv) && equals)
            return GetReader(APP);
    }
    return nsnull;
}

nsresult
Omnijar::GetURIString(Type aType, nsACString &result)
{
    NS_ABORT_IF_FALSE(IsInitialized(), "Omnijar not initialized");

    result.Truncate();

    // Return an empty string for APP in the unified case.
    if ((aType == APP) && sIsUnified) {
        return NS_OK;
    }

    nsCAutoString omniJarSpec;
    if (sPath[aType]) {
        nsresult rv = NS_GetURLSpecFromActualFile(sPath[aType], omniJarSpec);
        NS_ENSURE_SUCCESS(rv, rv);

        result = "jar:";
        if (sIsNested[aType])
            result += "jar:";
        result += omniJarSpec;
        result += "!";
        if (sIsNested[aType])
            result += "/" NS_STRINGIFY(OMNIJAR_NAME) "!";
    } else {
        nsCOMPtr<nsIFile> dir;
        nsDirectoryService::gService->Get(SPROP(aType), NS_GET_IID(nsIFile), getter_AddRefs(dir));
        nsresult rv = NS_GetURLSpecFromActualFile(dir, result);
        NS_ENSURE_SUCCESS(rv, rv);
    }
    result += "/";
    return NS_OK;
}

} /* namespace mozilla */
