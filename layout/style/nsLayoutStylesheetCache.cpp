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
 * The Original Code is Mozilla Gecko Layout
 *
 * The Initial Developer of the Original Code is
 * Benjamin Smedberg <bsmedberg@covad.net>
 * Portions created by the Initial Developer are Copyright (C) 2004
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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

#include "nsLayoutStylesheetCache.h"

#include "nsAppDirectoryServiceDefs.h"
#include "nsICSSLoader.h"
#include "nsIFile.h"
#include "nsLayoutCID.h"
#include "nsNetUtil.h"
#include "nsIObserverService.h"
#include "nsServiceManagerUtils.h"
#include "nsIXULRuntime.h"

NS_IMPL_ISUPPORTS1(nsLayoutStylesheetCache, nsIObserver)

nsresult
nsLayoutStylesheetCache::Observe(nsISupports* aSubject,
                            const char* aTopic,
                            const PRUnichar* aData)
{
  if (!strcmp(aTopic, "profile-before-change")) {
    mUserContentSheet = nsnull;
    mUserChromeSheet  = nsnull;
  }
  else if (!strcmp(aTopic, "profile-do-change")) {
    InitFromProfile();
  }
  else if (strcmp(aTopic, "chrome-flush-skin-caches") == 0 ||
           strcmp(aTopic, "chrome-flush-caches") == 0) {
    mScrollbarsSheet = nsnull;
    mFormsSheet = nsnull;
  }
  else {
    NS_NOTREACHED("Unexpected observer topic.");
  }
  return NS_OK;
}

nsICSSStyleSheet*
nsLayoutStylesheetCache::ScrollbarsSheet()
{
  EnsureGlobal();
  if (!gStyleCache)
    return nsnull;

  if (!gStyleCache->mScrollbarsSheet) {
    nsCOMPtr<nsIURI> sheetURI;
    NS_NewURI(getter_AddRefs(sheetURI),
              NS_LITERAL_CSTRING("chrome://global/skin/scrollbars.css"));

    // Scrollbars don't need access to unsafe rules
    if (sheetURI)
      LoadSheet(sheetURI, gStyleCache->mScrollbarsSheet, PR_FALSE, PR_TRUE);
    NS_ASSERTION(gStyleCache->mScrollbarsSheet, "Could not load scrollbars.css.");
  }

  return gStyleCache->mScrollbarsSheet;
}

nsICSSStyleSheet*
nsLayoutStylesheetCache::FormsSheet()
{
  EnsureGlobal();
  if (!gStyleCache)
    return nsnull;

  if (!gStyleCache->mFormsSheet) {
    nsCOMPtr<nsIURI> sheetURI;
      NS_NewURI(getter_AddRefs(sheetURI),
                NS_LITERAL_CSTRING("resource://gre/res/forms.css"));

    // forms.css needs access to unsafe rules
    if (sheetURI)
      LoadSheet(sheetURI, gStyleCache->mFormsSheet, PR_TRUE, PR_FALSE);

    NS_ASSERTION(gStyleCache->mFormsSheet, "Could not load forms.css.");
  }

  return gStyleCache->mFormsSheet;
}

nsICSSStyleSheet*
nsLayoutStylesheetCache::UserContentSheet()
{
  EnsureGlobal();
  if (!gStyleCache)
    return nsnull;

  return gStyleCache->mUserContentSheet;
}

nsICSSStyleSheet*
nsLayoutStylesheetCache::UserChromeSheet()
{
  EnsureGlobal();
  if (!gStyleCache)
    return nsnull;

  return gStyleCache->mUserChromeSheet;
}

nsICSSStyleSheet*
nsLayoutStylesheetCache::UASheet()
{
  EnsureGlobal();
  if (!gStyleCache)
    return nsnull;

  return gStyleCache->mUASheet;
}

nsICSSStyleSheet*
nsLayoutStylesheetCache::QuirkSheet()
{
  EnsureGlobal();
  if (!gStyleCache)
    return nsnull;

  return gStyleCache->mQuirkSheet;
}

void
nsLayoutStylesheetCache::Shutdown()
{
  NS_IF_RELEASE(gCSSLoader);
  NS_IF_RELEASE(gCaseSensitiveCSSLoader);
  NS_IF_RELEASE(gStyleCache);
}

nsLayoutStylesheetCache::nsLayoutStylesheetCache()
{
  nsCOMPtr<nsIObserverService> obsSvc =
    do_GetService("@mozilla.org/observer-service;1");
  NS_ASSERTION(obsSvc, "No global observer service?");

  if (obsSvc) {
    obsSvc->AddObserver(this, "profile-before-change", PR_FALSE);
    obsSvc->AddObserver(this, "profile-do-change", PR_FALSE);
    obsSvc->AddObserver(this, "chrome-flush-skin-caches", PR_FALSE);
    obsSvc->AddObserver(this, "chrome-flush-caches", PR_FALSE);
  }

  InitFromProfile();

  // And make sure that we load our UA sheets.  No need to do this
  // per-profile, since they're profile-invariant.
  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), "resource://gre/res/ua.css");
  if (uri) {
    LoadSheet(uri, mUASheet, PR_TRUE, PR_FALSE);
  }
  NS_ASSERTION(mUASheet, "Could not load ua.css");

  NS_NewURI(getter_AddRefs(uri), "resource://gre/res/quirk.css");
  if (uri) {
    LoadSheet(uri, mQuirkSheet, PR_TRUE, PR_FALSE);
  }
  NS_ASSERTION(mQuirkSheet, "Could not load quirk.css");
}

nsLayoutStylesheetCache::~nsLayoutStylesheetCache()
{
  gCSSLoader = nsnull;
  gCaseSensitiveCSSLoader = nsnull;
  gStyleCache = nsnull;
}

void
nsLayoutStylesheetCache::EnsureGlobal()
{
  if (gStyleCache) return;

  gStyleCache = new nsLayoutStylesheetCache();
  if (!gStyleCache) return;

  NS_ADDREF(gStyleCache);
}

void
nsLayoutStylesheetCache::InitFromProfile()
{
  nsCOMPtr<nsIXULRuntime> appInfo = do_GetService("@mozilla.org/xre/app-info;1");
  if (appInfo) {
    PRBool inSafeMode = PR_FALSE;
    appInfo->GetInSafeMode(&inSafeMode);
    if (inSafeMode)
      return;
  }
  nsCOMPtr<nsIFile> contentFile;
  nsCOMPtr<nsIFile> chromeFile;

  NS_GetSpecialDirectory(NS_APP_USER_CHROME_DIR,
                         getter_AddRefs(contentFile));
  if (!contentFile) {
    // if we don't have a profile yet, that's OK!
    return;
  }

  contentFile->Clone(getter_AddRefs(chromeFile));
  if (!chromeFile) return;

  contentFile->Append(NS_LITERAL_STRING("userContent.css"));
  chromeFile->Append(NS_LITERAL_STRING("userChrome.css"));

  LoadSheetFile(contentFile, mUserContentSheet);
  LoadSheetFile(chromeFile, mUserChromeSheet);
}

void
nsLayoutStylesheetCache::LoadSheetFile(nsIFile* aFile, nsCOMPtr<nsICSSStyleSheet> &aSheet)
{
  PRBool exists = PR_FALSE;
  aFile->Exists(&exists);

  if (!exists) return;

  nsCOMPtr<nsIURI> uri;
  NS_NewFileURI(getter_AddRefs(uri), aFile);

  LoadSheet(uri, aSheet, PR_FALSE, PR_FALSE);
}

void
nsLayoutStylesheetCache::LoadSheet(nsIURI* aURI, nsCOMPtr<nsICSSStyleSheet> &aSheet,
                                   PRBool aEnableUnsafeRules,
                                   PRBool aUseCaseSensitiveLoader)
{
  if (!aURI) {
    NS_ERROR("Null URI. Out of memory?");
    return;
  }

  nsICSSLoader** cssLoader =
    aUseCaseSensitiveLoader ? &gCaseSensitiveCSSLoader : &gCSSLoader;

  if (!*cssLoader) {
    NS_NewCSSLoader(cssLoader);
    if (aUseCaseSensitiveLoader) {
      (*cssLoader)->SetCaseSensitive(PR_TRUE);
    }
  }

  if (*cssLoader) {
    (*cssLoader)->LoadSheetSync(aURI, aEnableUnsafeRules, PR_TRUE,
                                getter_AddRefs(aSheet));
  }
}  

nsLayoutStylesheetCache*
nsLayoutStylesheetCache::gStyleCache = nsnull;

nsICSSLoader*
nsLayoutStylesheetCache::gCSSLoader = nsnull;

nsICSSLoader*
nsLayoutStylesheetCache::gCaseSensitiveCSSLoader = nsnull;
