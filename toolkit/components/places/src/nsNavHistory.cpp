//* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * The Original Code is Mozilla History System.
 *
 * The Initial Developer of the Original Code is Google Inc.
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Brett Wilson <brettw@gmail.com> (original author)
 *   Dietrich Ayala <dietrich@mozilla.com>
 *   Seth Spitzer <sspitzer@mozilla.com>
 *   Asaf Romano <mano@mozilla.com>
 *   Marco Bonardo <mak77@bonardo.net>
 *   Edward Lee <edward.lee@engineering.uiuc.edu>
 *   Michael Ventnor <m.ventnor@gmail.com>
 *   Ehsan Akhgari <ehsan.akhgari@gmail.com>
 *   Drew Willcoxon <adw@mozilla.com>
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

#include <stdio.h>
#include "nsNavHistory.h"
#include "nsNavBookmarks.h"
#include "nsAnnotationService.h"
#include "nsILivemarkService.h"

#include "nsPlacesTables.h"
#include "nsPlacesIndexes.h"
#include "nsPlacesTriggers.h"
#include "nsPlacesMacros.h"
#include "SQLFunctions.h"

#include "nsIArray.h"
#include "nsTArray.h"
#include "nsArrayEnumerator.h"
#include "nsCollationCID.h"
#include "nsCOMPtr.h"
#include "nsCRT.h"
#include "nsDebug.h"
#include "nsEnumeratorUtils.h"
#include "nsFaviconService.h"
#include "nsIChannelEventSink.h"
#include "nsIComponentManager.h"
#include "nsILocaleService.h"
#include "nsILocalFile.h"
#include "nsIPrefBranch2.h"
#include "nsIServiceManager.h"
#include "nsISimpleEnumerator.h"
#include "nsISupportsPrimitives.h"
#include "nsIURI.h"
#include "nsIURL.h"
#include "nsNetUtil.h"
#include "nsPrintfCString.h"
#include "nsPromiseFlatString.h"
#include "nsString.h"
#include "nsUnicharUtils.h"
#include "prsystem.h"
#include "prtime.h"
#include "prprf.h"
#include "nsEscape.h"
#include "nsIVariant.h"
#include "nsVariant.h"
#include "nsIEffectiveTLDService.h"
#include "nsIIDNService.h"
#include "nsIClassInfoImpl.h"
#include "nsThreadUtils.h"
#include "nsAppDirectoryServiceDefs.h"
#include "mozilla/storage.h"

#ifdef MOZ_XUL
#include "nsIAutoCompleteInput.h"
#include "nsIAutoCompletePopup.h"
#endif

#include "nsMathUtils.h" // for NS_ceilf()

using namespace mozilla::places;

// Microsecond timeout for "recent" events such as typed and bookmark following.
// If you typed it more than this time ago, it's not recent.
// This is 15 minutes           m    s/m  us/s
#define RECENT_EVENT_THRESHOLD (15 * 60 * PR_USEC_PER_SEC)

// Microseconds ago to look for redirects when updating bookmarks. Used to
// compute the threshold for nsNavBookmarks::AddBookmarkToHash
#define BOOKMARK_REDIRECT_TIME_THRESHOLD (2 * 60 * PR_USEC_PER_SEC)

// The maximum number of things that we will store in the recent events list
// before calling ExpireNonrecentEvents. This number should be big enough so it
// is very difficult to get that many unconsumed events (for example, typed but
// never visited) in the RECENT_EVENT_THRESHOLD. Otherwise, we'll start
// checking each one for every page visit, which will be somewhat slower.
#define RECENT_EVENT_QUEUE_MAX_LENGTH 128

// preference ID strings
#define PREF_BRANCH_BASE                        "browser."
#define PREF_BROWSER_HISTORY_EXPIRE_DAYS_MIN    "history_expire_days_min"
#define PREF_BROWSER_HISTORY_EXPIRE_DAYS_MAX    "history_expire_days"
#define PREF_BROWSER_HISTORY_EXPIRE_SITES       "history_expire_sites"
#define PREF_DB_CACHE_PERCENTAGE                "history_cache_percentage"
#define PREF_FRECENCY_NUM_VISITS                "places.frecency.numVisits"
#define PREF_FRECENCY_FIRST_BUCKET_CUTOFF       "places.frecency.firstBucketCutoff"
#define PREF_FRECENCY_SECOND_BUCKET_CUTOFF      "places.frecency.secondBucketCutoff"
#define PREF_FRECENCY_THIRD_BUCKET_CUTOFF       "places.frecency.thirdBucketCutoff"
#define PREF_FRECENCY_FOURTH_BUCKET_CUTOFF      "places.frecency.fourthBucketCutoff"
#define PREF_FRECENCY_FIRST_BUCKET_WEIGHT       "places.frecency.firstBucketWeight"
#define PREF_FRECENCY_SECOND_BUCKET_WEIGHT      "places.frecency.secondBucketWeight"
#define PREF_FRECENCY_THIRD_BUCKET_WEIGHT       "places.frecency.thirdBucketWeight"
#define PREF_FRECENCY_FOURTH_BUCKET_WEIGHT      "places.frecency.fourthBucketWeight"
#define PREF_FRECENCY_DEFAULT_BUCKET_WEIGHT     "places.frecency.defaultBucketWeight"
#define PREF_FRECENCY_EMBED_VISIT_BONUS         "places.frecency.embedVisitBonus"
#define PREF_FRECENCY_LINK_VISIT_BONUS          "places.frecency.linkVisitBonus"
#define PREF_FRECENCY_TYPED_VISIT_BONUS         "places.frecency.typedVisitBonus"
#define PREF_FRECENCY_BOOKMARK_VISIT_BONUS      "places.frecency.bookmarkVisitBonus"
#define PREF_FRECENCY_DOWNLOAD_VISIT_BONUS      "places.frecency.downloadVisitBonus"
#define PREF_FRECENCY_PERM_REDIRECT_VISIT_BONUS "places.frecency.permRedirectVisitBonus"
#define PREF_FRECENCY_TEMP_REDIRECT_VISIT_BONUS "places.frecency.tempRedirectVisitBonus"
#define PREF_FRECENCY_DEFAULT_VISIT_BONUS       "places.frecency.defaultVisitBonus"
#define PREF_FRECENCY_UNVISITED_BOOKMARK_BONUS  "places.frecency.unvisitedBookmarkBonus"
#define PREF_FRECENCY_UNVISITED_TYPED_BONUS     "places.frecency.unvisitedTypedBonus"
#define PREF_LAST_VACUUM                        "places.last_vacuum"

// Default (integer) value of PREF_DB_CACHE_PERCENTAGE from 0-100
// This is 6% of machine memory, giving 15MB for a user with 256MB of memory.
// The most that will be used is the size of the DB file. Normal history sizes
// look like 10MB would be a high average for a typical user, so the maximum
// should not normally be required.
#define DEFAULT_DB_CACHE_PERCENTAGE 6

// We set the default database page size to be larger. sqlite's default is 1K.
// This gives good performance when many small parts of the file have to be
// loaded for each statement. Because we try to keep large chunks of the file
// in memory, a larger page size should give better I/O performance. 32K is
// sqlite's default max page size.
#define DEFAULT_DB_PAGE_SIZE 4096

// the value of mLastNow expires every 3 seconds
#define HISTORY_EXPIRE_NOW_TIMEOUT (3 * PR_MSEC_PER_SEC)

// see bug #319004 -- clamp title and URL to generously-large but not too large
// length
#define HISTORY_URI_LENGTH_MAX 65536
#define HISTORY_TITLE_LENGTH_MAX 4096

// db file name
#define DB_FILENAME NS_LITERAL_STRING("places.sqlite")

// db backup file name
#define DB_CORRUPT_FILENAME NS_LITERAL_STRING("places.sqlite.corrupt")

// Lazy adding

#ifdef LAZY_ADD

// time that we'll wait before committing messages
#define LAZY_MESSAGE_TIMEOUT (3 * PR_MSEC_PER_SEC)

// the maximum number of times we'll postpone a lazy timer before committing
// See StartLazyTimer()
#define MAX_LAZY_TIMER_DEFERMENTS 2

#endif // LAZY_ADD

// Limit the number of items in the history for performance reasons
#define EXPIRATION_CAP_SITES 40000

// character-set annotation
#define CHARSET_ANNO NS_LITERAL_CSTRING("URIProperties/characterSet")

// We use the TRUNCATE journal mode to reduce the number of fsyncs.  Without
// this setting we had a Ts hit on Linux.  See bug 460315 for details.
#define DEFAULT_JOURNAL_MODE "TRUNCATE"

// These macros are used when splitting history by date.
// These are the day containers and catch-all final container.
#define ADDITIONAL_DATE_CONT_NUM 3
// We use a guess of the number of months considering all of them 30 days
// long, but we split only the last 6 months.
#define DATE_CONT_NUM(_expireDays) \
  (ADDITIONAL_DATE_CONT_NUM + PR_MIN(6, (_expireDays/30)))

// fraction of free pages in the database to force a vacuum between
// MAX_TIME_BEFORE_VACUUM and MIN_TIME_BEFORE_VACUUM.
#define VACUUM_FREEPAGES_THRESHOLD 0.1
// This is the maximum time (in microseconds) that can pass between 2 VACUUM
// operations.
#define MAX_TIME_BEFORE_VACUUM (PRInt64)60 * 24 * 60 * 60 * 1000 * 1000
// This is the minimum time (in microseconds) that should pass between 2 VACUUM
// operations.
#define MIN_TIME_BEFORE_VACUUM (PRInt64)30 * 24 * 60 * 60 * 1000 * 1000

NS_IMPL_THREADSAFE_ADDREF(nsNavHistory)
NS_IMPL_THREADSAFE_RELEASE(nsNavHistory)

NS_INTERFACE_MAP_BEGIN(nsNavHistory)
  NS_INTERFACE_MAP_ENTRY(nsINavHistoryService)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsIGlobalHistory2, nsIGlobalHistory3)
  NS_INTERFACE_MAP_ENTRY(nsIGlobalHistory3)
  NS_INTERFACE_MAP_ENTRY(nsIDownloadHistory)
  NS_INTERFACE_MAP_ENTRY(nsIBrowserHistory)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY(nsICharsetResolver)
  NS_INTERFACE_MAP_ENTRY(nsPIPlacesDatabase)
  NS_INTERFACE_MAP_ENTRY(nsPIPlacesHistoryListenersNotifier)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsINavHistoryService)
  NS_IMPL_QUERY_CLASSINFO(nsNavHistory)
NS_INTERFACE_MAP_END

// We don't care about flattening everything
NS_IMPL_CI_INTERFACE_GETTER5(
  nsNavHistory
, nsINavHistoryService
, nsIGlobalHistory3
, nsIGlobalHistory2
, nsIDownloadHistory
, nsIBrowserHistory
)

static nsresult GetReversedHostname(nsIURI* aURI, nsAString& host);
static void GetReversedHostname(const nsString& aForward, nsAString& aReversed);
static nsresult GenerateTitleFromURI(nsIURI* aURI, nsAString& aTitle);
static PRInt64 GetSimpleBookmarksQueryFolder(
    const nsCOMArray<nsNavHistoryQuery>& aQueries,
    nsNavHistoryQueryOptions* aOptions);
static void ParseSearchTermsFromQueries(const nsCOMArray<nsNavHistoryQuery>& aQueries,
                                        nsTArray<nsTArray<nsString>*>* aTerms);

inline void ReverseString(const nsString& aInput, nsAString& aReversed)
{
  aReversed.Truncate(0);
  for (PRInt32 i = aInput.Length() - 1; i >= 0; i --)
    aReversed.Append(aInput[i]);
}

namespace mozilla {
  namespace places {

    bool hasRecentCorruptDB()
    {
      nsCOMPtr<nsIFile> profDir;
      nsresult rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                           getter_AddRefs(profDir));
      NS_ENSURE_SUCCESS(rv, false);
      nsCOMPtr<nsISimpleEnumerator> entries;
      rv = profDir->GetDirectoryEntries(getter_AddRefs(entries));
      NS_ENSURE_SUCCESS(rv, false);
      PRBool hasMore;
      while (NS_SUCCEEDED(entries->HasMoreElements(&hasMore)) && hasMore) {
        nsCOMPtr<nsISupports> next;
        rv = entries->GetNext(getter_AddRefs(next));
        NS_ENSURE_SUCCESS(rv, false);
        nsCOMPtr<nsIFile> currFile = do_QueryInterface(next, &rv);
        NS_ENSURE_SUCCESS(rv, false);

        nsAutoString leafName;
        rv = currFile->GetLeafName(leafName);
        NS_ENSURE_SUCCESS(rv, false);
        if (leafName.Length() >= DB_CORRUPT_FILENAME.Length() &&
            leafName.Find(".corrupt", DB_FILENAME.Length()) != -1) {
          PRInt64 lastMod;
          rv = currFile->GetLastModifiedTime(&lastMod);
          NS_ENSURE_SUCCESS(rv, false);
          if (PR_Now() - lastMod > (PRInt64)24 * 60 * 60 * 1000 * 1000)
           return true;
        }
      }
      return false;
    }

    void GetTagsSqlFragment(PRInt64 aTagsFolder,
                            const nsACString& aRelation,
                            PRBool aHasSearchTerms,
                            nsACString& _sqlFragment) {
      if (!aHasSearchTerms)
        _sqlFragment.AssignLiteral("null");
      else {
        _sqlFragment.Assign(NS_LITERAL_CSTRING(
             "(SELECT GROUP_CONCAT(tag_title, ', ') "
              "FROM ( "
                "SELECT t_t.title AS tag_title "
                "FROM moz_bookmarks b_t "
                "JOIN moz_bookmarks t_t ON t_t.id = b_t.parent  "
                "WHERE b_t.fk = ") +
                aRelation + NS_LITERAL_CSTRING(" "
                "AND LENGTH(t_t.title) > 0 "
                "AND t_t.parent = ") +
                nsPrintfCString("%lld", aTagsFolder) + NS_LITERAL_CSTRING(" "
                "ORDER BY t_t.title COLLATE NOCASE ASC "
              ") "
             ")"));
      }

      _sqlFragment.AppendLiteral(" AS tags ");
    }

  }
}

// UpdateBatchScoper
//
//    This just sets begin/end of batch updates to correspond to C++ scopes so
//    we can be sure end always gets called.

class UpdateBatchScoper
{
public:
  UpdateBatchScoper(nsNavHistory& aNavHistory) : mNavHistory(aNavHistory)
  {
    mNavHistory.BeginUpdateBatch();
  }
  ~UpdateBatchScoper()
  {
    mNavHistory.EndUpdateBatch();
  }
protected:
  nsNavHistory& mNavHistory;
};

class PlacesEvent : public nsRunnable {
  public:
  PlacesEvent(const char* aTopic) {
    mTopic = aTopic;
  }

  NS_IMETHOD Run() {
    nsresult rv;
    nsCOMPtr<nsIObserverService> observerService =
      do_GetService("@mozilla.org/observer-service;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = observerService->NotifyObservers(nsnull, mTopic, nsnull);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }
  protected:
  const char* mTopic;
};

// if adding a new one, be sure to update nsNavBookmarks statements and
// its kGetChildrenIndex_* constants
const PRInt32 nsNavHistory::kGetInfoIndex_PageID = 0;
const PRInt32 nsNavHistory::kGetInfoIndex_URL = 1;
const PRInt32 nsNavHistory::kGetInfoIndex_Title = 2;
const PRInt32 nsNavHistory::kGetInfoIndex_RevHost = 3;
const PRInt32 nsNavHistory::kGetInfoIndex_VisitCount = 4;
const PRInt32 nsNavHistory::kGetInfoIndex_VisitDate = 5;
const PRInt32 nsNavHistory::kGetInfoIndex_FaviconURL = 6;
const PRInt32 nsNavHistory::kGetInfoIndex_SessionId = 7;
const PRInt32 nsNavHistory::kGetInfoIndex_ItemId = 8;
const PRInt32 nsNavHistory::kGetInfoIndex_ItemDateAdded = 9;
const PRInt32 nsNavHistory::kGetInfoIndex_ItemLastModified = 10;
const PRInt32 nsNavHistory::kGetInfoIndex_ItemParentId = 11;
const PRInt32 nsNavHistory::kGetInfoIndex_ItemTags = 12;


static const char* gQuitApplicationGrantedMessage = "quit-application-granted";
static const char* gXpcomShutdown = "xpcom-shutdown";
static const char* gAutoCompleteFeedback = "autocomplete-will-enter-text";
static const char* gIdleDaily = "idle-daily";

// annotation names
const char nsNavHistory::kAnnotationPreviousEncoding[] = "history/encoding";

// code borrowed from mozilla/xpfe/components/history/src/nsGlobalHistory.cpp
// pass in a pre-normalized now and a date, and we'll find
// the difference since midnight on each of the days.
//
// USECS_PER_DAY == PR_USEC_PER_SEC * 60 * 60 * 24;
static const PRInt64 USECS_PER_DAY = LL_INIT(20, 500654080);

PLACES_FACTORY_SINGLETON_IMPLEMENTATION(nsNavHistory, gHistoryService)

// nsNavHistory::nsNavHistory

nsNavHistory::nsNavHistory() : mBatchLevel(0),
                               mBatchHasTransaction(PR_FALSE),
                               mNowValid(PR_FALSE),
                               mExpireNowTimer(nsnull),
                               mExpireDaysMin(0),
                               mExpireDaysMax(0),
                               mExpireSites(0),
                               mNumVisitsForFrecency(10),
                               mTagsFolder(-1),
                               mInPrivateBrowsing(PRIVATEBROWSING_NOTINITED),
                               mDatabaseStatus(DATABASE_STATUS_OK),
                               mCanNotify(true),
                               mCacheObservers("history-observers")
{
#ifdef LAZY_ADD
  mLazyTimerSet = PR_TRUE;
  mLazyTimerDeferments = 0;
#endif
  NS_ASSERTION(!gHistoryService,
               "Attempting to create two instances of the service!");
  gHistoryService = this;
}

// nsNavHistory::~nsNavHistory

nsNavHistory::~nsNavHistory()
{
  // remove the static reference to the service. Check to make sure its us
  // in case somebody creates an extra instance of the service.
  NS_ASSERTION(gHistoryService == this,
               "Deleting a non-singleton instance of the service");
  if (gHistoryService == this)
    gHistoryService = nsnull;
}


// nsNavHistory::Init

nsresult
nsNavHistory::Init()
{
  nsresult rv;

  // prefs (must be before DB init, which uses the pref service)
  nsCOMPtr<nsIPrefService> prefService =
    do_GetService(NS_PREFSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = prefService->GetBranch(PREF_BRANCH_BASE, getter_AddRefs(mPrefBranch));
  NS_ENSURE_SUCCESS(rv, rv);

  // prefs
  LoadPrefs(PR_TRUE);

  // Init the database file.  If we won't be able to connect to the database it
  // is most likely corrupt, so we will backup it and create a new one.
  rv = InitDBFile(PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);

  // Init the database schema.  If this will fail there's an high possibility
  // the schema is corrupt or incorrect, so we will force a new database
  // initialization.
  rv = InitDB();
  if (NS_FAILED(rv)) {
    // Forced InitDBFile will backup the old db and create a new one.
    rv = InitDBFile(PR_TRUE);
    NS_ENSURE_SUCCESS(rv, rv);
    // Try to initialize the schema again on the new database.
    rv = InitDB();
  }
  NS_ENSURE_SUCCESS(rv, rv);

  // Initialize all the items that are not part of the on-disk database, like
  // views, temp tables, functions.  Do not initialize these in InitDBFile, or
  // in case of failure we would mark the database as corrupt and try to
  // replace it, even if it's sane.
  rv = InitAdditionalDBItems();
  NS_ENSURE_SUCCESS(rv, rv);

  // Initialize expiration.  There's no need to do this before, since just now
  // we have a valid database and a working connection.
  mExpire = new nsNavHistoryExpire();

  // Notify we have finished database initialization.
  // Enqueue the notification, so if we init another service that requires
  // nsNavHistoryService we don't recursive try to get it.
  nsRefPtr<PlacesEvent> completeEvent =
    new PlacesEvent(PLACES_INIT_COMPLETE_TOPIC);
  rv = NS_DispatchToMainThread(completeEvent);
  NS_ENSURE_SUCCESS(rv, rv);

  // extract the last session ID so we know where to pick up. There is no index
  // over sessions so the naive statement "SELECT MAX(session) FROM
  // moz_historyvisits" won't have good performance.
  // This is long before we use our temporary tables, so we do not have to join
  // on moz_historyvisits_temp to get the right result here.
  {
    nsCOMPtr<mozIStorageStatement> selectSession;
    rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
        "SELECT session FROM moz_historyvisits "
        "ORDER BY visit_date DESC LIMIT 1"),
      getter_AddRefs(selectSession));
    NS_ENSURE_SUCCESS(rv, rv);
    PRBool hasSession;
    if (NS_SUCCEEDED(selectSession->ExecuteStep(&hasSession)) && hasSession)
      mLastSessionID = selectSession->AsInt64(0);
    else
      mLastSessionID = 1;
  }

  // recent events hash tables
  NS_ENSURE_TRUE(mRecentTyped.Init(128), NS_ERROR_OUT_OF_MEMORY);
  NS_ENSURE_TRUE(mRecentBookmark.Init(128), NS_ERROR_OUT_OF_MEMORY);
  NS_ENSURE_TRUE(mRecentRedirects.Init(128), NS_ERROR_OUT_OF_MEMORY);

  /*****************************************************************************
   *** IMPORTANT NOTICE!
   ***
   *** Nothing after these add observer calls should return anything but NS_OK.
   *** If a failure code is returned, this nsNavHistory object will be held onto
   *** by the observer service and the preference service. 
   ****************************************************************************/

  nsCOMPtr<nsIObserverService> observerService =
    do_GetService("@mozilla.org/observer-service;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIPrefBranch2> pbi = do_QueryInterface(mPrefBranch);
  if (pbi) {
    pbi->AddObserver(PREF_BROWSER_HISTORY_EXPIRE_DAYS_MAX, this, PR_FALSE);
    pbi->AddObserver(PREF_BROWSER_HISTORY_EXPIRE_DAYS_MIN, this, PR_FALSE);
    pbi->AddObserver(PREF_BROWSER_HISTORY_EXPIRE_SITES, this, PR_FALSE);
  }

  observerService->AddObserver(this, gQuitApplicationGrantedMessage, PR_FALSE);
  observerService->AddObserver(this, gXpcomShutdown, PR_FALSE);
  observerService->AddObserver(this, gAutoCompleteFeedback, PR_FALSE);
  observerService->AddObserver(this, gIdleDaily, PR_FALSE);
  observerService->AddObserver(this, NS_PRIVATE_BROWSING_SWITCH_TOPIC, PR_FALSE);
  // In case we've either imported or done a migration from a pre-frecency
  // build, we will calculate the first cutoff period's frecencies once the rest
  // of the places infrastructure has been initialized.
  if (mDatabaseStatus == DATABASE_STATUS_CREATE ||
      mDatabaseStatus == DATABASE_STATUS_UPGRADED) {
    (void)observerService->AddObserver(this, PLACES_INIT_COMPLETE_TOPIC,
                                       PR_FALSE);
  }

  /*****************************************************************************
   *** IMPORTANT NOTICE!
   ***
   *** NO CODE SHOULD GO BEYOND THIS POINT THAT WOULD PROPAGATE AN ERROR.  IN
   *** OTHER WORDS, THE ONLY THING THAT SHOULD BE RETURNED AFTER THIS POINT IS
   *** NS_OK.
   ****************************************************************************/

  if (mDatabaseStatus == DATABASE_STATUS_CREATE) {
    nsCOMPtr<nsIFile> historyFile;
    rv = NS_GetSpecialDirectory(NS_APP_HISTORY_50_FILE,
                                getter_AddRefs(historyFile));
    if (NS_SUCCEEDED(rv) && historyFile) {
      (void)ImportHistory(historyFile);
    }
  }

  // Don't add code that can fail here! Do it up above, before we add our
  // observers.

  return NS_OK;
}

// nsNavHistory::InitDBFile
nsresult
nsNavHistory::InitDBFile(PRBool aForceInit)
{
  if (aForceInit) {
    NS_ASSERTION(mDBConn,
                 "When forcing initialization, a database connection must exist!");
    NS_ASSERTION(mDBService,
                 "When forcing initialization, the database service must exist!");
  }

  // get profile dir, file
  nsCOMPtr<nsIFile> profDir;
  nsresult rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                       getter_AddRefs(profDir));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = profDir->Clone(getter_AddRefs(mDBFile));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mDBFile->Append(DB_FILENAME);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aForceInit) {
    // If forcing initialization, backup and remove the old file.  If we have
    // already failed in the last 24 hours avoid to create another corrupt file,
    // since doing so, in some situation, could cause us to create a new corrupt
    // file at every try to access any Places service.  That is bad because it
    // would quickly fill the user's disk space without any notice.
    if (!hasRecentCorruptDB()) {
      // backup the database
      nsCOMPtr<nsIFile> backup;
      rv = mDBService->BackupDatabaseFile(mDBFile, DB_CORRUPT_FILENAME, profDir,
                                          getter_AddRefs(backup));
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // Close database connection if open.
    // If there's any not finalized statement or this fails for any reason
    // we won't be able to remove the database.
    rv = mDBConn->Close();
    NS_ENSURE_SUCCESS(rv, rv);

    // Remove the broken database.
    rv = mDBFile->Remove(PR_FALSE);
    if (NS_FAILED(rv)) {
      // If the file is still in use this will fail and we won't be able to
      // start with a clean database.  The process of backing up a corrupt
      // database will loop on the same database file at any next service
      // request.
      // We can't do much at this point, so fire a locked event so that user is
      // notified that we can't ensure Places to work.
      nsRefPtr<PlacesEvent> lockedEvent =
        new PlacesEvent(PLACES_DB_LOCKED_TOPIC);
      (void)NS_DispatchToMainThread(lockedEvent);
    }
    NS_ENSURE_SUCCESS(rv, rv);

    // If aForceInit is true we were unable to initialize or upgrade the current
    // database, so it was corrupt.
    mDatabaseStatus = DATABASE_STATUS_CORRUPT;
  }
  else {
    // file exists?
    PRBool dbExists = PR_TRUE;
    rv = mDBFile->Exists(&dbExists);
    NS_ENSURE_SUCCESS(rv, rv);
    // If the database didn't previously exist, we create it.
    if (!dbExists)
      mDatabaseStatus = DATABASE_STATUS_CREATE;
  }

  // open the database
  mDBService = do_GetService(MOZ_STORAGE_SERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mDBService->OpenUnsharedDatabase(mDBFile, getter_AddRefs(mDBConn));
  if (rv == NS_ERROR_FILE_CORRUPTED) {
    // The database is corrupt, we create a new one.
    mDatabaseStatus = DATABASE_STATUS_CORRUPT;

    // backup file
    nsCOMPtr<nsIFile> backup;
    rv = mDBService->BackupDatabaseFile(mDBFile, DB_CORRUPT_FILENAME, profDir,
                                        getter_AddRefs(backup));
    NS_ENSURE_SUCCESS(rv, rv);
 
    // remove existing file 
    rv = mDBFile->Remove(PR_FALSE);
    NS_ENSURE_SUCCESS(rv, rv);

    // and try again
    rv = profDir->Clone(getter_AddRefs(mDBFile));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mDBFile->Append(DB_FILENAME);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mDBService->OpenUnsharedDatabase(mDBFile, getter_AddRefs(mDBConn));
  }
 
  if (rv != NS_OK && rv != NS_ERROR_FILE_CORRUPTED) {
    // If the database cannot be opened for any reason other than corruption,
    // send out a notification and do not continue initialization.
    // Note: We swallow errors here, since we want service init to fail anyway.
    nsRefPtr<PlacesEvent> lockedEvent =
      new PlacesEvent(PLACES_DB_LOCKED_TOPIC);
    (void)NS_DispatchToMainThread(lockedEvent);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

// nsNavHistory::InitDB
//

#define PLACES_SCHEMA_VERSION 10

nsresult
nsNavHistory::InitDB()
{
  PRInt32 pageSize = DEFAULT_DB_PAGE_SIZE;

  // Get the places schema version, which we store in the user_version PRAGMA.
  PRInt32 DBSchemaVersion = 0;
  nsresult rv = mDBConn->GetSchemaVersion(&DBSchemaVersion);
  NS_ENSURE_SUCCESS(rv, rv);
  bool databaseInitialized = (DBSchemaVersion > 0);

  if (!databaseInitialized) {
    // IMPORTANT NOTE:
    // setting page_size must happen first, see bug #401985 for details
    //
    // Set the database page size.
    // This will only have any effect on empty files, so must be done before
    // anything else. If the file already exists, we'll get that file's page
    // size and this would have no effect.
    nsCAutoString pageSizePragma("PRAGMA page_size = ");
    pageSizePragma.AppendInt(pageSize);
    rv = mDBConn->ExecuteSimpleSQL(pageSizePragma);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    // Get the page size.  This may be different than the default if the
    // database file already existed with a different page size.
    nsCOMPtr<mozIStorageStatement> statement;
    rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
        "PRAGMA page_size"),
      getter_AddRefs(statement));
    NS_ENSURE_SUCCESS(rv, rv);

    PRBool hasResult;
    rv = statement->ExecuteStep(&hasResult);
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ENSURE_TRUE(hasResult, NS_ERROR_FAILURE);
    pageSize = statement->AsInt32(0);
  }

  // Ensure that temp tables are held in memory, not on disk.  We use temp
  // tables mainly for fsync and I/O reduction.
  rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "PRAGMA temp_store = MEMORY"));
  NS_ENSURE_SUCCESS(rv, rv);

  // Set pragma synchronous to FULL to ensure maximum data integrity, even in
  // case of crashes or unclean shutdowns.
  // The suggested setting from SQLite is FULL, but Storage defaults to NORMAL.
  rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "PRAGMA synchronous = FULL"));
  NS_ENSURE_SUCCESS(rv, rv);


  // Compute the size of the database cache.
  PRInt32 cachePercentage;
  if (NS_FAILED(mPrefBranch->GetIntPref(PREF_DB_CACHE_PERCENTAGE,
                                        &cachePercentage)))
    cachePercentage = DEFAULT_DB_CACHE_PERCENTAGE;
  if (cachePercentage > 50)
    cachePercentage = 50; // sanity check, don't take too much
  if (cachePercentage < 0)
    cachePercentage = 0;
  PRInt64 cacheSize = PR_GetPhysicalMemorySize() * cachePercentage / 100;
  PRInt64 cachePages = cacheSize / pageSize;

  // Set the cache size.  We don't use default_cache_size so the database can
  // be moved between computers and the value will change dynamically.
  nsCAutoString pageSizePragma("PRAGMA cache_size = ");
  pageSizePragma.AppendInt(cachePages);
  rv = mDBConn->ExecuteSimpleSQL(pageSizePragma);
  NS_ENSURE_SUCCESS(rv, rv);

  // Lock the db file.  This is done partly to avoid third party applications
  // to access the database while it's in use, partly for performance reasons.
  // http://www.sqlite.org/pragma.html#pragma_locking_mode
  rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "PRAGMA locking_mode = EXCLUSIVE"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "PRAGMA journal_mode = " DEFAULT_JOURNAL_MODE));
  NS_ENSURE_SUCCESS(rv, rv);

  // We are going to initialize tables, so everything from now on should be in
  // a transaction for performances.
  mozStorageTransaction transaction(mDBConn, PR_FALSE);

  // Initialize the other places services' database tables. We do this before
  // creating our statements. Some of our statements depend on these external
  // tables, such as the bookmarks or favicon tables.
  rv = nsNavBookmarks::InitTables(mDBConn);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = nsFaviconService::InitTables(mDBConn);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = nsAnnotationService::InitTables(mDBConn);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!databaseInitialized) {
    // This is the first run, so we set schema version to the latest one, since
    // we don't need to migrate anything.  We will create tables from scratch.
    rv = UpdateSchemaVersion();
    NS_ENSURE_SUCCESS(rv, rv);
    DBSchemaVersion = PLACES_SCHEMA_VERSION;
  }

  if (PLACES_SCHEMA_VERSION != DBSchemaVersion) {
    // Migration How-to:
    //
    // 1. increment PLACES_SCHEMA_VERSION.
    // 2. implement a method that performs up/sidegrade to your version
    //    from the current version.
    //
    // NOTE: We don't support downgrading back to History-only Places.
    // If you want to go from newer schema version back to V0, you'll need to
    // blow away your sqlite file. Subsequent up/downgrades have backwards and
    // forward migration code.
    //
    // XXX Backup places.sqlite to places-{version}.sqlite when doing db migration?
    
    if (DBSchemaVersion < PLACES_SCHEMA_VERSION) {
      // Upgrading
      mDatabaseStatus = DATABASE_STATUS_UPGRADED;

      // Migrate anno tables up to V3
      if (DBSchemaVersion < 3) {
        rv = MigrateV3Up(mDBConn);
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Migrate bookmarks tables up to V5
      if (DBSchemaVersion < 5) {
        rv = ForceMigrateBookmarksDB(mDBConn);
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Migrate anno tables up to V6
      if (DBSchemaVersion < 6) {
        rv = MigrateV6Up(mDBConn);
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Migrate historyvisits and bookmarks up to V7
      if (DBSchemaVersion < 7) {
        rv = MigrateV7Up(mDBConn);
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Migrate historyvisits up to V8
      if (DBSchemaVersion < 8) {
        rv = MigrateV8Up(mDBConn);
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Migrate places up to V9
      if (DBSchemaVersion < 9) {
        rv = MigrateV9Up(mDBConn);
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Migrate places up to V10
      if (DBSchemaVersion < 10) {
        rv = MigrateV10Up(mDBConn);
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Schema Upgrades must add migration code here.

    } else {
      // Downgrading

      // XXX Need to prompt user or otherwise notify of 
      // potential dataloss when downgrading.

      // XXX Downgrades from >V6 must add migration code here.

      // Downgrade v1,2,4,5
      // v3,6 have no backwards incompatible changes.
      if (DBSchemaVersion > 2 && DBSchemaVersion < 6) {
        // perform downgrade to v2
        rv = ForceMigrateBookmarksDB(mDBConn);
        NS_ENSURE_SUCCESS(rv, rv);
      }
    }

    // Update schema version to the current one.
    rv = UpdateSchemaVersion();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (!databaseInitialized) {
    // CREATE TABLE moz_places.
    rv = mDBConn->ExecuteSimpleSQL(CREATE_MOZ_PLACES);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_URL);
    NS_ENSURE_SUCCESS(rv, rv);

    // This index is used for favicon expiration, see nsNavHistoryExpire::ExpireItems.
    rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_FAVICON);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_REVHOST);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_VISITCOUNT);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_FRECENCY);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_LASTVISITDATE);
    NS_ENSURE_SUCCESS(rv, rv);

    // CREATE TABLE moz_historyvisits.
    rv = mDBConn->ExecuteSimpleSQL(CREATE_MOZ_HISTORYVISITS);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_HISTORYVISITS_PLACEDATE);
    NS_ENSURE_SUCCESS(rv, rv);

    // This makes a big difference in startup time for large profiles because of
    // finding bookmark redirects using the referring page. 
    rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_HISTORYVISITS_FROMVISIT);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_HISTORYVISITS_VISITDATE);
    NS_ENSURE_SUCCESS(rv, rv);

    // moz_inputhistory
    rv = mDBConn->ExecuteSimpleSQL(CREATE_MOZ_INPUTHISTORY);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = transaction.Commit();
  NS_ENSURE_SUCCESS(rv, rv);

  // ANY FAILURE IN THIS METHOD WILL CAUSE US TO MARK THE DATABASE AS CORRUPT
  // AND TRY TO REPLACE IT.
  // DO NOT PUT HERE ANYTHING THAT IS NOT RELATED TO INITIALIZATION OR MODIFYING
  // THE DISK DATABASE.

  return NS_OK;
}

nsresult
nsNavHistory::InitAdditionalDBItems()
{
  nsresult rv = InitTempTables();
  NS_ENSURE_SUCCESS(rv, rv);
  rv = InitViews();
  NS_ENSURE_SUCCESS(rv, rv);
  rv = InitFunctions();
  NS_ENSURE_SUCCESS(rv, rv);
  rv = InitStatements();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
nsNavHistory::GetDatabaseStatus(PRUint16 *aDatabaseStatus)
{
  NS_ENSURE_ARG_POINTER(aDatabaseStatus);
  *aDatabaseStatus = mDatabaseStatus;
  return NS_OK;
}

// nsNavHistory::UpdateSchemaVersion
//
// Called by the individual services' InitTables()
nsresult
nsNavHistory::UpdateSchemaVersion()
{
  return mDBConn->SetSchemaVersion(PLACES_SCHEMA_VERSION);
}

// nsNavHistory::InitFunctions
//
//    Called after InitDB, this creates our own functions

class mozStorageFunctionGetUnreversedHost: public mozIStorageFunction
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_MOZISTORAGEFUNCTION
};

NS_IMPL_ISUPPORTS1(mozStorageFunctionGetUnreversedHost, mozIStorageFunction)

NS_IMETHODIMP
mozStorageFunctionGetUnreversedHost::OnFunctionCall(
  mozIStorageValueArray* aFunctionArguments,
  nsIVariant** _retval)
{
  NS_ASSERTION(aFunctionArguments, "Must have non-null function args");
  NS_ASSERTION(_retval, "Must have non-null return pointer");

  nsAutoString src;
  aFunctionArguments->GetString(0, src);

  nsresult rv;
  nsCOMPtr<nsIWritableVariant> result(do_CreateInstance(
      "@mozilla.org/variant;1", &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  if (src.Length()>1) {
    src.Truncate(src.Length() - 1);
    nsAutoString dest;
    ReverseString(src, dest);
    result->SetAsAString(dest);
  } else {
    result->SetAsAString(NS_LITERAL_STRING(""));
  }
  NS_ADDREF(*_retval = result);
  return NS_OK;
}

nsresult
nsNavHistory::InitTempTables()
{
  nsresult rv;

  // moz_places_temp
  rv = mDBConn->ExecuteSimpleSQL(CREATE_MOZ_PLACES_TEMP);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_TEMP_URL);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_TEMP_FAVICON);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_TEMP_REVHOST);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_TEMP_VISITCOUNT);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_TEMP_FRECENCY);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBConn->ExecuteSimpleSQL(CREATE_MOZ_PLACES_SYNC_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);


  // moz_historyvisits_temp
  rv = mDBConn->ExecuteSimpleSQL(CREATE_MOZ_HISTORYVISITS_TEMP);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_HISTORYVISITS_TEMP_PLACEDATE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_HISTORYVISITS_TEMP_FROMVISIT);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_HISTORYVISITS_TEMP_VISITDATE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBConn->ExecuteSimpleSQL(CREATE_MOZ_HISTORYVISITS_SYNC_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
nsNavHistory::InitViews()
{
  nsresult rv;

  // moz_places_view
  rv = mDBConn->ExecuteSimpleSQL(CREATE_MOZ_PLACES_VIEW);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBConn->ExecuteSimpleSQL(CREATE_PLACES_VIEW_INSERT_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mDBConn->ExecuteSimpleSQL(CREATE_PLACES_VIEW_DELETE_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mDBConn->ExecuteSimpleSQL(CREATE_PLACES_VIEW_UPDATE_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);

  // moz_historyvisits_view
  rv = mDBConn->ExecuteSimpleSQL(CREATE_MOZ_HISTORYVISITS_VIEW);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBConn->ExecuteSimpleSQL(CREATE_HISTORYVISITS_VIEW_INSERT_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mDBConn->ExecuteSimpleSQL(CREATE_HISTORYVISITS_VIEW_DELETE_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mDBConn->ExecuteSimpleSQL(CREATE_HISTORYVISITS_VIEW_UPDATE_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
nsNavHistory::InitFunctions()
{
  nsCOMPtr<mozIStorageFunction> func =
    new mozStorageFunctionGetUnreversedHost;
  NS_ENSURE_TRUE(func, NS_ERROR_OUT_OF_MEMORY);
  nsresult rv = mDBConn->CreateFunction(
    NS_LITERAL_CSTRING("get_unreversed_host"), 1, func
  );
  NS_ENSURE_SUCCESS(rv, rv);

  rv = MatchAutoCompleteFunction::create(mDBConn);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

// nsNavHistory::InitStatements
//
//    Called after InitDB, this creates our stored statements

nsresult
nsNavHistory::InitStatements()
{
  // mDBGetURLPageInfo
  // We are not checking for duplicated ids into the unified table
  // for perf reasons, LIMIT 1 will discard duplicates faster since we
  // have unique urls.
  nsresult rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
    "SELECT id, url, title, rev_host, visit_count "
    "FROM moz_places_temp "
    "WHERE url = ?1 "
    "UNION ALL "
    "SELECT id, url, title, rev_host, visit_count "
    "FROM moz_places "
    "WHERE url = ?1 "
    "LIMIT 1"),
    getter_AddRefs(mDBGetURLPageInfo));
  NS_ENSURE_SUCCESS(rv, rv);

  // mDBGetIdPageInfo
  // We are not checking for duplicated ids into the unified table
  // for perf reasons, LIMIT 1 will discard duplicates faster since we
  // have unique place ids.
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT id, url, title, rev_host, visit_count "
      "FROM moz_places_temp "
      "WHERE id = ?1 "
      "UNION ALL "
      "SELECT id, url, title, rev_host, visit_count "
      "FROM moz_places "
      "WHERE id = ?1 "
      "LIMIT 1"),
    getter_AddRefs(mDBGetIdPageInfo));
  NS_ENSURE_SUCCESS(rv, rv);

  // mDBRecentVisitOfURL
  // We are not checking for duplicated ids into the unified table
  // for perf reasons, LIMIT 1 will discard duplicates faster since we
  // expect visits in temp table being the most recent.
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT id, session, visit_date "
      "FROM moz_historyvisits_temp "
      "WHERE place_id = IFNULL((SELECT id FROM moz_places_temp WHERE url = ?1), "
                              "(SELECT id FROM moz_places WHERE url = ?1)) "
      "UNION ALL "
      "SELECT id, session, visit_date "
      "FROM moz_historyvisits "
      "WHERE place_id = IFNULL((SELECT id FROM moz_places_temp WHERE url = ?1), "
                              "(SELECT id FROM moz_places WHERE url = ?1)) "
      "ORDER BY visit_date DESC "
      "LIMIT 1 "),
    getter_AddRefs(mDBRecentVisitOfURL));
  NS_ENSURE_SUCCESS(rv, rv);

  // mDBRecentVisitOfPlace
  // We are not checking for duplicated ids into the unified table
  // for perf reasons, LIMIT 1 will discard duplicates faster since we
  // expect visits in temp table being the most recent.  
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT id FROM moz_historyvisits_temp "
      "WHERE place_id = ?1 "
        "AND visit_date = ?2 "
        "AND session = ?3 "
      "UNION ALL "
      "SELECT id FROM moz_historyvisits "
      "WHERE place_id = ?1 "
        "AND visit_date = ?2 "
        "AND session = ?3 "
      "LIMIT 1"),
    getter_AddRefs(mDBRecentVisitOfPlace));
  NS_ENSURE_SUCCESS(rv, rv);

  // mDBInsertVisit
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "INSERT INTO moz_historyvisits_view "
        "(from_visit, place_id, visit_date, visit_type, session) "
      "VALUES (?1, ?2, ?3, ?4, ?5)"),
    getter_AddRefs(mDBInsertVisit));
  NS_ENSURE_SUCCESS(rv, rv);

  // mDBGetPageVisitStats (see InternalAdd)
  // We are not checking for duplicated ids into the unified table
  // for perf reasons, LIMIT 1 will discard duplicates faster since we
  // have unique place ids.
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT id, visit_count, typed, hidden "
      "FROM moz_places_temp "
      "WHERE url = ?1 "
      "UNION ALL "
      "SELECT id, visit_count, typed, hidden "
      "FROM moz_places "
      "WHERE url = ?1 "
      "LIMIT 1"),
    getter_AddRefs(mDBGetPageVisitStats));
  NS_ENSURE_SUCCESS(rv, rv);

  // mDBIsPageVisited
  // We are not checking for duplicated ids into the unified table
  // for perf reasons, LIMIT 1 will discard duplicates faster since we
  // only need to know if a visit exists.
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT h.id "
      "FROM moz_places_temp h "
      "WHERE url = ?1 " 
        "AND ( "
          "EXISTS(SELECT id FROM moz_historyvisits_temp WHERE place_id = h.id LIMIT 1) "
          "OR EXISTS(SELECT id FROM moz_historyvisits WHERE place_id = h.id LIMIT 1) "
        ") "
      "UNION ALL "
      "SELECT h.id "
      "FROM moz_places h "
      "WHERE url = ?1 "
      "AND ( "
        "EXISTS(SELECT id FROM moz_historyvisits_temp WHERE place_id = h.id LIMIT 1) "
        "OR EXISTS(SELECT id FROM moz_historyvisits WHERE place_id = h.id LIMIT 1) "
      ") "
      "LIMIT 1"), 
    getter_AddRefs(mDBIsPageVisited));
  NS_ENSURE_SUCCESS(rv, rv);

  // mDBUpdatePageVisitStats (see InternalAdd)
  // we don't need to update visit_count since it's maintained
  // in sync by triggers, and we must NEVER touch it
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "UPDATE moz_places_view "
      "SET hidden = ?2, typed = ?3 "
      "WHERE id = ?1"),
    getter_AddRefs(mDBUpdatePageVisitStats));
  NS_ENSURE_SUCCESS(rv, rv);

  // mDBAddNewPage (see InternalAddNewPage)
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "INSERT INTO moz_places_view "
        "(url, title, rev_host, hidden, typed, frecency) "
      "VALUES (?1, ?2, ?3, ?4, ?5, ?6)"),
    getter_AddRefs(mDBAddNewPage));
  NS_ENSURE_SUCCESS(rv, rv);

  // mDBGetTags
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "/* do not warn (bug 487594) */ "
      "SELECT GROUP_CONCAT(tag_title, ', ') "
      "FROM ( "
        "SELECT t.title AS tag_title "
        "FROM moz_bookmarks b "
        "JOIN moz_bookmarks t ON t.id = b.parent "
        "WHERE b.fk = IFNULL((SELECT id FROM moz_places_temp WHERE url = ?2), "
                            "(SELECT id FROM moz_places WHERE url = ?2)) "
          "AND LENGTH(t.title) > 0 "
          "AND b.type = ") +
            nsPrintfCString("%d", nsINavBookmarksService::TYPE_BOOKMARK) +
          NS_LITERAL_CSTRING(" AND t.parent = ?1 "
        "ORDER BY t.title COLLATE NOCASE ASC)"),
    getter_AddRefs(mDBGetTags));
  NS_ENSURE_SUCCESS(rv, rv);

  // mDBGetItemsWithAnno
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT a.item_id, a.content "
      "FROM moz_anno_attributes n "
      "JOIN moz_items_annos a ON n.id = a.anno_attribute_id "
      "WHERE n.name = ?1"),
    getter_AddRefs(mDBGetItemsWithAnno));
   NS_ENSURE_SUCCESS(rv, rv);

  // mDBSetPlaceTitle
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "UPDATE moz_places_view "
      "SET title = ?1 "
      "WHERE url = ?2"),
    getter_AddRefs(mDBSetPlaceTitle));
  NS_ENSURE_SUCCESS(rv, rv);


  // mDBVisitsForFrecency
  // NOTE: we are not limiting to visits with "visit_type NOT IN (0,4,7)"
  // because if we do that, mDBVisitsForFrecency would return no visits
  // for places with only embed (or undefined) visits.  That would
  // cause use to estimate a frecency based on what information we do have,
  // see CalculateFrecencyInternal(). That would result in a non-zero frecency
  // for a place with only embedded visits, instead of a frecency of 0. If we
  // have a temporary or permanent redirect, calculate the frecency as if it
  // was the original page visited.
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT v.visit_date, COALESCE( "
        "(SELECT r.visit_type FROM moz_historyvisits_temp r "
          "WHERE v.visit_type IN ") +
            nsPrintfCString("(%d,%d) ", TRANSITION_REDIRECT_PERMANENT,
                                        TRANSITION_REDIRECT_TEMPORARY) +
            NS_LITERAL_CSTRING(" AND r.id = v.from_visit), "
        "(SELECT r.visit_type FROM moz_historyvisits r "
          "WHERE v.visit_type IN ") +
            nsPrintfCString("(%d,%d) ", TRANSITION_REDIRECT_PERMANENT,
                                        TRANSITION_REDIRECT_TEMPORARY) +
            NS_LITERAL_CSTRING(" AND r.id = v.from_visit), "
        "visit_type) "
      "FROM moz_historyvisits_temp v "
      "WHERE v.place_id = ?1 "
      "UNION ALL "
      "SELECT v.visit_date, COALESCE( "
        "(SELECT r.visit_type FROM moz_historyvisits_temp r "
          "WHERE v.visit_type IN ") +
            nsPrintfCString("(%d,%d) ", TRANSITION_REDIRECT_PERMANENT,
                                        TRANSITION_REDIRECT_TEMPORARY) +
            NS_LITERAL_CSTRING(" AND r.id = v.from_visit), "
        "(SELECT r.visit_type FROM moz_historyvisits r "
          "WHERE v.visit_type IN ") +
            nsPrintfCString("(%d,%d) ", TRANSITION_REDIRECT_PERMANENT,
                                        TRANSITION_REDIRECT_TEMPORARY) +
            NS_LITERAL_CSTRING(" AND r.id = v.from_visit), "
        "visit_type) "
      "FROM moz_historyvisits v "
      "WHERE v.place_id = ?1 "
        "AND v.id NOT IN (SELECT id FROM moz_historyvisits_temp) "
      "ORDER BY visit_date DESC LIMIT ") +
        nsPrintfCString("%d", mNumVisitsForFrecency),
    getter_AddRefs(mDBVisitsForFrecency));
  NS_ENSURE_SUCCESS(rv, rv);

  // mDBUpdateFrecencyAndHidden
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "UPDATE moz_places_view SET frecency = ?2, hidden = ?3 WHERE id = ?1"),
    getter_AddRefs(mDBUpdateFrecencyAndHidden));
  NS_ENSURE_SUCCESS(rv, rv);

  // mDBGetPlaceVisitStats
  // We are not checking for duplicated ids into the unified table
  // for perf reasons, LIMIT 1 will discard duplicates faster since we
  // have unique place ids.
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT typed, hidden, frecency "
      "FROM moz_places_temp WHERE id = ?1 "
      "UNION ALL "
      "SELECT typed, hidden, frecency "
      "FROM moz_places WHERE id = ?1 "
      "LIMIT 1"),
    getter_AddRefs(mDBGetPlaceVisitStats));
  NS_ENSURE_SUCCESS(rv, rv);

  // when calculating frecency, we want the visit count to be 
  // all the visits.
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT "
        "(SELECT COUNT(*) FROM moz_historyvisits WHERE place_id = ?1) + "
        "(SELECT COUNT(*) FROM moz_historyvisits_temp WHERE place_id = ?1 "
            "AND id NOT IN (SELECT id FROM moz_historyvisits))"),
    getter_AddRefs(mDBFullVisitCount));
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

// nsNavHistory::ForceMigrateBookmarksDB
//
//    This dumps all bookmarks-related tables, and recreates them,
//    forcing a re-import of bookmarks.html.
//
//    NOTE: This may cause data-loss if downgrading!
//    Only use this for migration if you're sure that bookmarks.html
//    and the target version support all bookmarks fields.
nsresult
nsNavHistory::ForceMigrateBookmarksDB(mozIStorageConnection* aDBConn) 
{
  // drop bookmarks tables
  nsresult rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "DROP TABLE IF EXISTS moz_bookmarks"));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "DROP TABLE IF EXISTS moz_bookmarks_folders"));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "DROP TABLE IF EXISTS moz_bookmarks_roots"));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "DROP TABLE IF EXISTS moz_keywords"));
  NS_ENSURE_SUCCESS(rv, rv);

  // initialize bookmarks tables
  rv = nsNavBookmarks::InitTables(aDBConn);
  NS_ENSURE_SUCCESS(rv, rv);

  // We have done a new database init, so we mark this as if the database has
  // been created now, so the frontend can distinguish this status and import
  // if needed.
  mDatabaseStatus = DATABASE_STATUS_CREATE;

  return NS_OK;
}

// nsNavHistory::MigrateV3Up
nsresult
nsNavHistory::MigrateV3Up(mozIStorageConnection* aDBConn) 
{
  // if type col is already there, then a partial update occurred.
  // return, making no changes, and allowing db version to be updated.
  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT type from moz_annos"),
    getter_AddRefs(statement));
  if (NS_SUCCEEDED(rv))
    return NS_OK;

  // add type column to moz_annos
  rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "ALTER TABLE moz_annos ADD type INTEGER DEFAULT 0"));
  if (NS_FAILED(rv)) {
    // if the alteration failed, force-migrate
    rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "DROP TABLE IF EXISTS moz_annos"));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = nsAnnotationService::InitTables(mDBConn);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

// nsNavHistory::MigrateV6Up
nsresult
nsNavHistory::MigrateV6Up(mozIStorageConnection* aDBConn) 
{
  mozStorageTransaction transaction(aDBConn, PR_FALSE);

  // if dateAdded & lastModified cols are already there, then a partial update occurred,
  // and so we should not attempt to add these cols.
  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT a.dateAdded, a.lastModified FROM moz_annos a"), 
    getter_AddRefs(statement));
  if (NS_FAILED(rv)) {
    // add dateAdded and lastModified columns to moz_annos
    rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "ALTER TABLE moz_annos ADD dateAdded INTEGER DEFAULT 0"));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "ALTER TABLE moz_annos ADD lastModified INTEGER DEFAULT 0"));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // if dateAdded & lastModified cols are already there, then a partial update occurred,
  // and so we should not attempt to add these cols.  see bug #408443 for details.
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT b.dateAdded, b.lastModified FROM moz_items_annos b"), 
    getter_AddRefs(statement));
  if (NS_FAILED(rv)) {
    // add dateAdded and lastModified columns to moz_items_annos
    rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "ALTER TABLE moz_items_annos ADD dateAdded INTEGER DEFAULT 0"));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "ALTER TABLE moz_items_annos ADD lastModified INTEGER DEFAULT 0"));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // we used to create an indexes on moz_favicons.url and
  // moz_anno_attributes.name, but those indexes are not needed
  // because those columns are UNIQUE, so remove them.
  // see bug #386303 for more details
  rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "DROP INDEX IF EXISTS moz_favicons_url"));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "DROP INDEX IF EXISTS moz_anno_attributes_nameindex"));
  NS_ENSURE_SUCCESS(rv, rv);


  // bug #371800 - remove moz_places.user_title
  // test for moz_places.user_title
  nsCOMPtr<mozIStorageStatement> statement2;
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT user_title FROM moz_places"),
    getter_AddRefs(statement2));
  if (NS_SUCCEEDED(rv)) {
    // 1. Indexes are moved along with the renamed table. Since we're dropping
    // that table, we're also dropping its indexes, and later re-creating them
    // for the new table.
    rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "DROP INDEX IF EXISTS moz_places_urlindex"));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "DROP INDEX IF EXISTS moz_places_titleindex"));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "DROP INDEX IF EXISTS moz_places_faviconindex"));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "DROP INDEX IF EXISTS moz_places_hostindex"));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "DROP INDEX IF EXISTS moz_places_visitcount"));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "DROP INDEX IF EXISTS moz_places_frecencyindex"));
    NS_ENSURE_SUCCESS(rv, rv);

    // 2. remove any duplicate URIs
    rv = RemoveDuplicateURIs();
    NS_ENSURE_SUCCESS(rv, rv);

    // 3. rename moz_places to moz_places_backup
    rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "ALTER TABLE moz_places RENAME TO moz_places_backup"));
    NS_ENSURE_SUCCESS(rv, rv);

    // 4. create moz_places w/o user_title
    rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "CREATE TABLE moz_places ("
          "id INTEGER PRIMARY KEY, "
          "url LONGVARCHAR, "
          "title LONGVARCHAR, "
          "rev_host LONGVARCHAR, "
          "visit_count INTEGER DEFAULT 0, "
          "hidden INTEGER DEFAULT 0 NOT NULL, "
          "typed INTEGER DEFAULT 0 NOT NULL, "
          "favicon_id INTEGER, "
          "frecency INTEGER DEFAULT -1 NOT NULL)"));
    NS_ENSURE_SUCCESS(rv, rv);

    // 5. recreate the indexes
    // NOTE: tests showed that it's faster to create the indexes prior to filling
    // the table than it is to add them afterwards.
    rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_URL);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_FAVICON);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_REVHOST);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_VISITCOUNT);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_FRECENCY);
    NS_ENSURE_SUCCESS(rv, rv);

    // 6. copy all data into moz_places
    rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "INSERT INTO moz_places (" MOZ_PLACES_COLUMNS ")"
        "SELECT " MOZ_PLACES_COLUMNS " FROM moz_places_backup"));
    NS_ENSURE_SUCCESS(rv, rv);

    // 7. drop moz_places_backup
    rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "DROP TABLE moz_places_backup"));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return transaction.Commit();
}

// nsNavHistory::MigrateV7Up
nsresult
nsNavHistory::MigrateV7Up(mozIStorageConnection* aDBConn) 
{
  mozStorageTransaction transaction(aDBConn, PR_FALSE);

  // We need an index on lastModified to catch quickly last modified bookmark
  // title for tag container's children. This will be useful for sync too.
  PRBool lastModIndexExists = PR_FALSE;
  nsresult rv = aDBConn->IndexExists(
    NS_LITERAL_CSTRING("moz_bookmarks_itemlastmodifiedindex"),
    &lastModIndexExists);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!lastModIndexExists) {
    rv = aDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_BOOKMARKS_PLACELASTMODIFIED);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // We need to do a one-time change of the moz_historyvisits.pageindex
  // to speed up finding last visit date when joinin with moz_places.
  // See bug 392399 for more details.
  PRBool pageIndexExists = PR_FALSE;
  rv = aDBConn->IndexExists(
    NS_LITERAL_CSTRING("moz_historyvisits_pageindex"), &pageIndexExists);
  NS_ENSURE_SUCCESS(rv, rv);

  if (pageIndexExists) {
    // drop old index
    rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "DROP INDEX IF EXISTS moz_historyvisits_pageindex"));
    NS_ENSURE_SUCCESS(rv, rv);

    // create the new multi-column index
    rv = aDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_HISTORYVISITS_PLACEDATE);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // for existing profiles, we may not have a frecency column
  nsCOMPtr<mozIStorageStatement> hasFrecencyStatement;
  rv = aDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT frecency FROM moz_places"),
    getter_AddRefs(hasFrecencyStatement));

  if (NS_FAILED(rv)) {
    // Add frecency column to moz_places, default to -1 so that all the
    // frecencies are invalid
    rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "ALTER TABLE moz_places ADD frecency INTEGER DEFAULT -1 NOT NULL"));
    NS_ENSURE_SUCCESS(rv, rv);

    // create index for the frecency column
    // XXX multi column index with typed, and visit_count?
    rv = aDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_FRECENCY);
    NS_ENSURE_SUCCESS(rv, rv);

    // for place: items and unvisited livemark items, we need to set
    // the frecency to 0 so that they don't show up in url bar autocomplete
    rv = FixInvalidFrecenciesForExcludedPlaces();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Temporary migration code for bug 396300
  nsCOMPtr<mozIStorageStatement> moveUnfiledBookmarks;
  rv = aDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "UPDATE moz_bookmarks "
      "SET parent = ("
        "SELECT folder_id "
        "FROM moz_bookmarks_roots "
        "WHERE root_name = ?1 "
      ") "
      "WHERE type = ?2 "
      "AND parent = ("
        "SELECT folder_id "
        "FROM moz_bookmarks_roots "
        "WHERE root_name = ?3 "
      ")"),
    getter_AddRefs(moveUnfiledBookmarks));
  rv = moveUnfiledBookmarks->BindUTF8StringParameter(0, NS_LITERAL_CSTRING("unfiled"));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = moveUnfiledBookmarks->BindInt32Parameter(1, nsINavBookmarksService::TYPE_BOOKMARK);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = moveUnfiledBookmarks->BindUTF8StringParameter(2, NS_LITERAL_CSTRING("places"));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = moveUnfiledBookmarks->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  // Create a statement to test for trigger creation
  nsCOMPtr<mozIStorageStatement> triggerDetection;
  rv = aDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT name "
      "FROM sqlite_master "
      "WHERE type = 'trigger' "
      "AND name = ?"),
    getter_AddRefs(triggerDetection));
  NS_ENSURE_SUCCESS(rv, rv);

  // Check for existence
  PRBool triggerExists;
  rv = triggerDetection->BindUTF8StringParameter(
    0, NS_LITERAL_CSTRING("moz_historyvisits_afterinsert_v1_trigger")
  );
  NS_ENSURE_SUCCESS(rv, rv);
  rv = triggerDetection->ExecuteStep(&triggerExists);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = triggerDetection->Reset();
  NS_ENSURE_SUCCESS(rv, rv);

  // We need to create two triggers on moz_historyvists to maintain the
  // accuracy of moz_places.visit_count.  For this to work, we must ensure that
  // all moz_places.visit_count values are correct.
  // See bug 416313 for details.
  if (!triggerExists) {
    // First, we do a one-time reset of all the moz_places.visit_count values.
    rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "UPDATE moz_places SET visit_count = "
          "(SELECT count(*) FROM moz_historyvisits "
           "WHERE place_id = moz_places.id "
            "AND visit_type NOT IN ") +
              nsPrintfCString("(0,%d,%d) ",
                              nsINavHistoryService::TRANSITION_EMBED,
                              nsINavHistoryService::TRANSITION_DOWNLOAD) +
          NS_LITERAL_CSTRING(")"));
    NS_ENSURE_SUCCESS(rv, rv);

    // We used to create two triggers here, but we no longer need that with
    // schema version eight and greater.  We've removed their creation here as
    // a result.
  }

  // Check for existence
  rv = triggerDetection->BindUTF8StringParameter(
    0, NS_LITERAL_CSTRING("moz_bookmarks_beforedelete_v1_trigger")
  );
  NS_ENSURE_SUCCESS(rv, rv);
  rv = triggerDetection->ExecuteStep(&triggerExists);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = triggerDetection->Reset();
  NS_ENSURE_SUCCESS(rv, rv);

  // We need to create one trigger on moz_bookmarks to remove unused keywords.
  // See bug 421180 for details.
  if (!triggerExists) {
    // First, remove any existing dangling keywords
    rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "DELETE FROM moz_keywords "
        "WHERE id IN ("
          "SELECT k.id "
          "FROM moz_keywords k "
          "LEFT OUTER JOIN moz_bookmarks b "
          "ON b.keyword_id = k.id "
          "WHERE b.id IS NULL"
        ")"));
    NS_ENSURE_SUCCESS(rv, rv);

    // Now we create our trigger
    rv = aDBConn->ExecuteSimpleSQL(CREATE_KEYWORD_VALIDITY_TRIGGER);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return transaction.Commit();
}

nsresult
nsNavHistory::MigrateV8Up(mozIStorageConnection *aDBConn)
{
  mozStorageTransaction transaction(aDBConn, PR_FALSE);

  nsresult rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "DROP TRIGGER IF EXISTS moz_historyvisits_afterinsert_v1_trigger"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "DROP TRIGGER IF EXISTS moz_historyvisits_afterdelete_v1_trigger"));
  NS_ENSURE_SUCCESS(rv, rv);


  // bug #381795 - remove unused indexes
  rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "DROP INDEX IF EXISTS moz_places_titleindex"));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "DROP INDEX IF EXISTS moz_annos_item_idindex"));
  NS_ENSURE_SUCCESS(rv, rv);


  // Do a one-time re-creation of the moz_annos indexes (bug 415201)
  PRBool oldIndexExists = PR_FALSE;
  rv = mDBConn->IndexExists(NS_LITERAL_CSTRING("moz_annos_attributesindex"), &oldIndexExists);
  NS_ENSURE_SUCCESS(rv, rv);
  if (oldIndexExists) {
    // drop old uri annos index
    rv = mDBConn->ExecuteSimpleSQL(
        NS_LITERAL_CSTRING("DROP INDEX moz_annos_attributesindex"));
    NS_ENSURE_SUCCESS(rv, rv);

    // create new uri annos index
    rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_ANNOS_PLACEATTRIBUTE);
    NS_ENSURE_SUCCESS(rv, rv);

    // drop old item annos index
    rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "DROP INDEX IF EXISTS moz_items_annos_attributesindex"));
    NS_ENSURE_SUCCESS(rv, rv);

    // create new item annos index
    rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_ITEMSANNOS_PLACEATTRIBUTE);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return transaction.Commit();
}

nsresult
nsNavHistory::MigrateV9Up(mozIStorageConnection *aDBConn)
{
  mozStorageTransaction transaction(aDBConn, PR_FALSE);
  // Added in Bug 488966.  The last_visit_date column caches the last
  // visit date, this enhances SELECT performances when we
  // need to sort visits by visit date.
  // The cached value is synced by INSERT and DELETE triggers on
  // moz_historyvisits_view, on every added or removed visit.
  // See nsPlacesTriggers.h for details on the triggers.
  PRBool oldIndexExists = PR_FALSE;
  nsresult rv = mDBConn->IndexExists(
    NS_LITERAL_CSTRING("moz_places_lastvisitdateindex"), &oldIndexExists);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!oldIndexExists) {
    // Add last_visit_date column to moz_places.
    rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "ALTER TABLE moz_places ADD last_visit_date INTEGER"));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mDBConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_LASTVISITDATE);
    NS_ENSURE_SUCCESS(rv, rv);

    // Now let's sync the column contents with real visit dates.
    // This query can be really slow due to disk access, since it will basically
    // dupe the table contents in the journal file, and then write them down
    // in the database.
    // We will temporary use a memory journal file, this has the advantage of
    // reducing write times by a half, but will temporary consume more memory
    // and increase risks of corruption if we should crash in the middle of this
    // update.
    rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "PRAGMA journal_mode = MEMORY"));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "UPDATE moz_places SET last_visit_date = "
          "(SELECT MAX(visit_date) "
           "FROM moz_historyvisits "
           "WHERE place_id = moz_places.id)"));
    NS_ENSURE_SUCCESS(rv, rv);

    // Restore the default journal mode.
    rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
        "PRAGMA journal_mode = " DEFAULT_JOURNAL_MODE));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return transaction.Commit();
}

nsresult
nsNavHistory::MigrateV10Up(mozIStorageConnection *aDBConn)
{
  // LastModified is set to the same value as dateAdded on item creation.
  // This way we can use lastModified index to sort.
  nsresult rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "UPDATE moz_bookmarks SET lastModified = dateAdded "
      "WHERE lastModified IS NULL"));
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}
   
// nsNavHistory::GetUrlIdFor
//
//    Called by the bookmarks and annotation services, this function returns the
//    ID of the row for the given URL, optionally creating one if it doesn't
//    exist. A newly created entry will have no visits.
//
//    If aAutoCreate is false and the item doesn't exist, the entry ID will be
//    zero.
//
//    This DOES NOT check for bad URLs other than that they're nonempty.

nsresult
nsNavHistory::GetUrlIdFor(nsIURI* aURI, PRInt64* aEntryID,
                          PRBool aAutoCreate)
{
  *aEntryID = 0;

  mozStorageStatementScoper statementResetter(mDBGetURLPageInfo);
  nsresult rv = BindStatementURI(mDBGetURLPageInfo, 0, aURI);
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool hasEntry = PR_FALSE;
  rv = mDBGetURLPageInfo->ExecuteStep(&hasEntry);
  NS_ENSURE_SUCCESS(rv, rv);

  if (hasEntry)
    return mDBGetURLPageInfo->GetInt64(kGetInfoIndex_PageID, aEntryID);

  if (aAutoCreate) {
    // create a new hidden, untyped, unvisited entry
    mDBGetURLPageInfo->Reset();
    statementResetter.Abandon();
    nsString voidString;
    voidString.SetIsVoid(PR_TRUE);
    return InternalAddNewPage(aURI, voidString, PR_TRUE, PR_FALSE, 0, PR_TRUE, aEntryID);
  }

  // Doesn't exist: don't do anything, entry ID was already set to 0 above
  return NS_OK;
}


// nsNavHistory::InternalAddNewPage
//
//    Adds a new page to the DB.
//    THIS SHOULD BE THE ONLY PLACE NEW moz_places ROWS ARE
//    CREATED. This allows us to maintain better consistency.
//
//    If non-null, the new page ID will be placed into aPageID.

nsresult
nsNavHistory::InternalAddNewPage(nsIURI* aURI,
                                 const nsAString& aTitle,
                                 PRBool aHidden,
                                 PRBool aTyped,
                                 PRInt32 aVisitCount,
                                 PRBool aCalculateFrecency,
                                 PRInt64* aPageID)
{
  mozStorageStatementScoper scoper(mDBAddNewPage);
  nsresult rv = BindStatementURI(mDBAddNewPage, 0, aURI);
  NS_ENSURE_SUCCESS(rv, rv);

  // title
  if (aTitle.IsVoid()) {
    // if no title is specified, make up a title based on the filename
    nsAutoString title;
    GenerateTitleFromURI(aURI, title);
    rv = mDBAddNewPage->BindStringParameter(1,
        StringHead(title, HISTORY_TITLE_LENGTH_MAX));
  } else {
    rv = mDBAddNewPage->BindStringParameter(1,
        StringHead(aTitle, HISTORY_TITLE_LENGTH_MAX));
  }
  NS_ENSURE_SUCCESS(rv, rv);

  // host (reversed with trailing period)
  nsAutoString revHost;
  rv = GetReversedHostname(aURI, revHost);
  // Not all URI types have hostnames, so this is optional.
  if (NS_SUCCEEDED(rv)) {
    rv = mDBAddNewPage->BindStringParameter(2, revHost);
  } else {
    rv = mDBAddNewPage->BindNullParameter(2);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  // hidden
  rv = mDBAddNewPage->BindInt32Parameter(3, aHidden);
  NS_ENSURE_SUCCESS(rv, rv);

  // typed
  rv = mDBAddNewPage->BindInt32Parameter(4, aTyped);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCAutoString url;
  rv = aURI->GetSpec(url);
  NS_ENSURE_SUCCESS(rv, rv);

  // frecency
  PRInt32 frecency = -1;
  if (aCalculateFrecency) {
    rv = CalculateFrecency(-1 /* no page id, since this page doesn't exist */,
                           aTyped, aVisitCount, url, &frecency);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = mDBAddNewPage->BindInt32Parameter(5, frecency);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBAddNewPage->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  // If the caller wants the page ID, go get it
  if (aPageID) {
    mozStorageStatementScoper scoper(mDBGetURLPageInfo);

    rv = BindStatementURI(mDBGetURLPageInfo, 0, aURI);
    NS_ENSURE_SUCCESS(rv, rv);

    PRBool hasResult = PR_FALSE;
    rv = mDBGetURLPageInfo->ExecuteStep(&hasResult);
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ASSERTION(hasResult, "hasResult is false but the call succeeded?");

    *aPageID = mDBGetURLPageInfo->AsInt64(0);
  }

  return NS_OK;
}

// nsNavHistory::InternalAddVisit
//
//    Just a wrapper for inserting a new visit in the DB.

nsresult
nsNavHistory::InternalAddVisit(PRInt64 aPageID, PRInt64 aReferringVisit,
                               PRInt64 aSessionID, PRTime aTime,
                               PRInt32 aTransitionType, PRInt64* visitID)
{
  nsresult rv;

  {
    mozStorageStatementScoper scoper(mDBInsertVisit);
  
    rv = mDBInsertVisit->BindInt64Parameter(0, aReferringVisit);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mDBInsertVisit->BindInt64Parameter(1, aPageID);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mDBInsertVisit->BindInt64Parameter(2, aTime);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mDBInsertVisit->BindInt32Parameter(3, aTransitionType);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mDBInsertVisit->BindInt64Parameter(4, aSessionID);
    NS_ENSURE_SUCCESS(rv, rv);
  
    rv = mDBInsertVisit->Execute();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  {
    mozStorageStatementScoper scoper(mDBRecentVisitOfPlace);

    rv = mDBRecentVisitOfPlace->BindInt64Parameter(0, aPageID);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mDBRecentVisitOfPlace->BindInt64Parameter(1, aTime);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mDBRecentVisitOfPlace->BindInt64Parameter(2, aSessionID);
    NS_ENSURE_SUCCESS(rv, rv);

    PRBool hasResult;
    rv = mDBRecentVisitOfPlace->ExecuteStep(&hasResult);
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ASSERTION(hasResult, "hasResult is false but the call succeeded?");

    *visitID = mDBRecentVisitOfPlace->AsInt64(0);
  }

  return NS_OK;
}


// nsNavHistory::FindLastVisit
//
//    This finds the most recent visit to the given URL. If found, it will put
//    that visit's ID and session into the respective out parameters and return
//    true. Returns false if no visit is found.
//
//    This is used to compute the referring visit.

PRBool
nsNavHistory::FindLastVisit(nsIURI* aURI, PRInt64* aVisitID,
                            PRInt64* aSessionID)
{
  mozStorageStatementScoper scoper(mDBRecentVisitOfURL);
  nsresult rv = BindStatementURI(mDBRecentVisitOfURL, 0, aURI);
  NS_ENSURE_SUCCESS(rv, PR_FALSE);

  PRBool hasMore;
  rv = mDBRecentVisitOfURL->ExecuteStep(&hasMore);
  NS_ENSURE_SUCCESS(rv, PR_FALSE);
  if (hasMore) {
    *aVisitID = mDBRecentVisitOfURL->AsInt64(0);
    *aSessionID = mDBRecentVisitOfURL->AsInt64(1);
    return PR_TRUE;
  }
  return PR_FALSE;
}


// nsNavHistory::IsURIStringVisited
//
//    Takes a URL as a string and returns true if we've visited it.
//
//    Be careful to always reset the statement since it will be reused.

PRBool nsNavHistory::IsURIStringVisited(const nsACString& aURIString)
{
#ifdef LAZY_ADD
  // check the lazy list to see if this has recently been added
  for (PRUint32 i = 0; i < mLazyMessages.Length(); i ++) {
    if (mLazyMessages[i].type == LazyMessage::Type_AddURI) {
      if (aURIString.Equals(mLazyMessages[i].uriSpec))
        return PR_TRUE;
    }
  }
#endif

  // check the main DB
  mozStorageStatementScoper scoper(mDBIsPageVisited);
  nsresult rv = BindStatementURLCString(mDBIsPageVisited, 0, aURIString);
  NS_ENSURE_SUCCESS(rv, PR_FALSE);

  PRBool hasMore = PR_FALSE;
  rv = mDBIsPageVisited->ExecuteStep(&hasMore);
  NS_ENSURE_SUCCESS(rv, PR_FALSE);
  return hasMore;
}


// nsNavHistory::LoadPrefs

nsresult
nsNavHistory::LoadPrefs(PRBool aInitializing)
{
  if (! mPrefBranch)
    return NS_OK;

  mPrefBranch->GetIntPref(PREF_BROWSER_HISTORY_EXPIRE_DAYS_MAX, &mExpireDaysMax);
  mPrefBranch->GetIntPref(PREF_BROWSER_HISTORY_EXPIRE_DAYS_MIN, &mExpireDaysMin);
  // Cap max days to min days to prevent expiring pages younger than min
  // NOTE: if history is disabled in preferences, then mExpireDaysMax == 0
  if (mExpireDaysMax && mExpireDaysMax < mExpireDaysMin)
    mExpireDaysMax = mExpireDaysMin;
  if (NS_FAILED(mPrefBranch->GetIntPref(PREF_BROWSER_HISTORY_EXPIRE_SITES,
                                        &mExpireSites)))
    mExpireSites = EXPIRATION_CAP_SITES;

  // get the frecency prefs
  nsCOMPtr<nsIPrefBranch> prefs(do_GetService("@mozilla.org/preferences-service;1"));
  if (prefs) {
    prefs->GetIntPref(PREF_FRECENCY_NUM_VISITS, 
      &mNumVisitsForFrecency);
    prefs->GetIntPref(PREF_FRECENCY_FIRST_BUCKET_CUTOFF, 
      &mFirstBucketCutoffInDays);
    prefs->GetIntPref(PREF_FRECENCY_SECOND_BUCKET_CUTOFF,
      &mSecondBucketCutoffInDays);
    prefs->GetIntPref(PREF_FRECENCY_THIRD_BUCKET_CUTOFF, 
      &mThirdBucketCutoffInDays);
    prefs->GetIntPref(PREF_FRECENCY_FOURTH_BUCKET_CUTOFF, 
      &mFourthBucketCutoffInDays);
    prefs->GetIntPref(PREF_FRECENCY_EMBED_VISIT_BONUS, 
      &mEmbedVisitBonus);
    prefs->GetIntPref(PREF_FRECENCY_LINK_VISIT_BONUS, 
      &mLinkVisitBonus);
    prefs->GetIntPref(PREF_FRECENCY_TYPED_VISIT_BONUS, 
      &mTypedVisitBonus);
    prefs->GetIntPref(PREF_FRECENCY_BOOKMARK_VISIT_BONUS, 
      &mBookmarkVisitBonus);
    prefs->GetIntPref(PREF_FRECENCY_DOWNLOAD_VISIT_BONUS, 
      &mDownloadVisitBonus);
    prefs->GetIntPref(PREF_FRECENCY_PERM_REDIRECT_VISIT_BONUS, 
      &mPermRedirectVisitBonus);
    prefs->GetIntPref(PREF_FRECENCY_TEMP_REDIRECT_VISIT_BONUS, 
      &mTempRedirectVisitBonus);
    prefs->GetIntPref(PREF_FRECENCY_DEFAULT_VISIT_BONUS, 
      &mDefaultVisitBonus);
    prefs->GetIntPref(PREF_FRECENCY_UNVISITED_BOOKMARK_BONUS, 
      &mUnvisitedBookmarkBonus);
    prefs->GetIntPref(PREF_FRECENCY_UNVISITED_TYPED_BONUS,
      &mUnvisitedTypedBonus);
    prefs->GetIntPref(PREF_FRECENCY_FIRST_BUCKET_WEIGHT, 
      &mFirstBucketWeight);
    prefs->GetIntPref(PREF_FRECENCY_SECOND_BUCKET_WEIGHT, 
      &mSecondBucketWeight);
    prefs->GetIntPref(PREF_FRECENCY_THIRD_BUCKET_WEIGHT, 
      &mThirdBucketWeight);
    prefs->GetIntPref(PREF_FRECENCY_FOURTH_BUCKET_WEIGHT, 
      &mFourthBucketWeight);
    prefs->GetIntPref(PREF_FRECENCY_DEFAULT_BUCKET_WEIGHT, 
      &mDefaultWeight);
  }
  return NS_OK;
}


// nsNavHistory::GetNow
//
//    This is a hack to avoid calling PR_Now() too often, as is the case when
//    we're asked the ageindays of many history entries in a row. A timer is
//    set which will clear our valid flag after a short timeout.

PRTime
nsNavHistory::GetNow()
{
  if (!mNowValid) {
    mLastNow = PR_Now();
    mNowValid = PR_TRUE;
    if (!mExpireNowTimer)
      mExpireNowTimer = do_CreateInstance("@mozilla.org/timer;1");

    if (mExpireNowTimer)
      mExpireNowTimer->InitWithFuncCallback(expireNowTimerCallback, this,
                                            HISTORY_EXPIRE_NOW_TIMEOUT,
                                            nsITimer::TYPE_ONE_SHOT);
  }

  return mLastNow;
}


// nsNavHistory::expireNowTimerCallback

void nsNavHistory::expireNowTimerCallback(nsITimer* aTimer, void* aClosure)
{
  nsNavHistory *history = static_cast<nsNavHistory *>(aClosure);
  history->mNowValid = PR_FALSE;
  history->mExpireNowTimer = nsnull;
}

static PRTime
NormalizeTimeRelativeToday(PRTime aTime)
{
  // round to midnight this morning
  PRExplodedTime explodedTime;
  PR_ExplodeTime(aTime, PR_LocalTimeParameters, &explodedTime);

  // set to midnight (0:00)
  explodedTime.tm_min =
    explodedTime.tm_hour =
    explodedTime.tm_sec =
    explodedTime.tm_usec = 0;

  return PR_ImplodeTime(&explodedTime);
}

// nsNavHistory::NormalizeTime
//
//    Converts a nsINavHistoryQuery reference+offset time into a PRTime
//    relative to the epoch.
//
//    It is important that this function NOT use the current time optimization.
//    It is called to update queries, and we really need to know what right
//    now is because those incoming values will also have current times that
//    we will have to compare against.

PRTime // static
nsNavHistory::NormalizeTime(PRUint32 aRelative, PRTime aOffset)
{
  PRTime ref;
  switch (aRelative)
  {
    case nsINavHistoryQuery::TIME_RELATIVE_EPOCH:
      return aOffset;
    case nsINavHistoryQuery::TIME_RELATIVE_TODAY:
      ref = NormalizeTimeRelativeToday(PR_Now());
      break;
    case nsINavHistoryQuery::TIME_RELATIVE_NOW:
      ref = PR_Now();
      break;
    default:
      NS_NOTREACHED("Invalid relative time");
      return 0;
  }
  return ref + aOffset;
}

// nsNavHistory::GetUpdateRequirements
//
//    Returns conditions for query update.
//
//    QUERYUPDATE_TIME:
//      This query is only limited by an inclusive time range on the first
//      query object. The caller can quickly evaluate the time itself if it
//      chooses. This is even simpler than "simple" below.
//    QUERYUPDATE_SIMPLE:
//      This query is evaluatable using EvaluateQueryForNode to do live
//      updating.
//    QUERYUPDATE_COMPLEX:
//      This query is not evaluatable using EvaluateQueryForNode. When something
//      happens that this query updates, you will need to re-run the query.
//    QUERYUPDATE_COMPLEX_WITH_BOOKMARKS:
//      A complex query that additionally has dependence on bookmarks. All
//      bookmark-dependent queries fall under this category.
//
//    aHasSearchTerms will be set to true if the query has any dependence on
//    keywords. When there is no dependence on keywords, we can handle title
//    change operations as simple instead of complex.

PRUint32
nsNavHistory::GetUpdateRequirements(const nsCOMArray<nsNavHistoryQuery>& aQueries,
                                    nsNavHistoryQueryOptions* aOptions,
                                    PRBool* aHasSearchTerms)
{
  NS_ASSERTION(aQueries.Count() > 0, "Must have at least one query");

  // first check if there are search terms
  *aHasSearchTerms = PR_FALSE;
  PRInt32 i;
  for (i = 0; i < aQueries.Count(); i ++) {
    aQueries[i]->GetHasSearchTerms(aHasSearchTerms);
    if (*aHasSearchTerms)
      break;
  }

  PRBool nonTimeBasedItems = PR_FALSE;
  PRBool domainBasedItems = PR_FALSE;

  for (i = 0; i < aQueries.Count(); i ++) {
    nsNavHistoryQuery* query = aQueries[i];

    if (query->Folders().Length() > 0 ||
        query->OnlyBookmarked() ||
        query->Tags().Length() > 0) {
      return QUERYUPDATE_COMPLEX_WITH_BOOKMARKS;
    }
    // Note: we don't currently have any complex non-bookmarked items, but these
    // are expected to be added. Put detection of these items here.
    if (! query->SearchTerms().IsEmpty() ||
        ! query->Domain().IsVoid() ||
        query->Uri() != nsnull)
      nonTimeBasedItems = PR_TRUE;

    if (! query->Domain().IsVoid())
      domainBasedItems = PR_TRUE;
  }

  if (aOptions->ResultType() ==
      nsINavHistoryQueryOptions::RESULTS_AS_TAG_QUERY)
    return QUERYUPDATE_COMPLEX_WITH_BOOKMARKS;

  // Whenever there is a maximum number of results, 
  // and we are not a bookmark query we must requery. This
  // is because we can't generally know if any given addition/change causes
  // the item to be in the top N items in the database.
  if (aOptions->MaxResults() > 0)
    return QUERYUPDATE_COMPLEX;

  if (aQueries.Count() == 1 && domainBasedItems)
    return QUERYUPDATE_HOST;
  if (aQueries.Count() == 1 && ! nonTimeBasedItems)
    return QUERYUPDATE_TIME;
  return QUERYUPDATE_SIMPLE;
}


// nsNavHistory::EvaluateQueryForNode
//
//    This runs the node through the given queries to see if satisfies the
//    query conditions. Not every query parameters are handled by this code,
//    but we handle the most common ones so that performance is better.
//
//    We assume that the time on the node is the time that we want to compare.
//    This is not necessarily true because URL nodes have the last access time,
//    which is not necessarily the same. However, since this is being called
//    to update the list, we assume that the last access time is the current
//    access time that we are being asked to compare so it works out.
//
//    Returns true if node matches the query, false if not.

PRBool
nsNavHistory::EvaluateQueryForNode(const nsCOMArray<nsNavHistoryQuery>& aQueries,
                                   nsNavHistoryQueryOptions* aOptions,
                                   nsNavHistoryResultNode* aNode)
{
  // lazily created from the node's string when we need to match URIs
  nsCOMPtr<nsIURI> nodeUri;

  for (PRInt32 i = 0; i < aQueries.Count(); i ++) {
    PRBool hasIt;
    nsCOMPtr<nsNavHistoryQuery> query = aQueries[i];

    // --- begin time ---
    query->GetHasBeginTime(&hasIt);
    if (hasIt) {
      PRTime beginTime = NormalizeTime(query->BeginTimeReference(),
                                       query->BeginTime());
      if (aNode->mTime < beginTime)
        continue; // before our time range
    }

    // --- end time ---
    query->GetHasEndTime(&hasIt);
    if (hasIt) {
      PRTime endTime = NormalizeTime(query->EndTimeReference(),
                                     query->EndTime());
      if (aNode->mTime > endTime)
        continue; // after our time range
    }

    // --- search terms ---
    if (! query->SearchTerms().IsEmpty()) {
      // we can use the existing filtering code, just give it our one object in
      // an array.
      nsCOMArray<nsNavHistoryResultNode> inputSet;
      inputSet.AppendObject(aNode);
      nsCOMArray<nsNavHistoryQuery> queries;
      queries.AppendObject(query);
      nsCOMArray<nsNavHistoryResultNode> filteredSet;
      nsresult rv = FilterResultSet(nsnull, inputSet, &filteredSet, queries, aOptions);
      if (NS_FAILED(rv))
        continue;
      if (! filteredSet.Count())
        continue; // did not make it through the filter, doesn't match
    }

    // --- domain/host matching ---
    query->GetHasDomain(&hasIt);
    if (hasIt) {
      if (! nodeUri) {
        // lazy creation of nodeUri, which might be checked for multiple queries
        if (NS_FAILED(NS_NewURI(getter_AddRefs(nodeUri), aNode->mURI)))
          continue;
      }
      nsCAutoString asciiRequest;
      if (NS_FAILED(AsciiHostNameFromHostString(query->Domain(), asciiRequest)))
        continue;

      if (query->DomainIsHost()) {
        nsCAutoString host;
        if (NS_FAILED(nodeUri->GetAsciiHost(host)))
          continue;

        if (! asciiRequest.Equals(host))
          continue; // host names don't match
      }
      // check domain names
      nsCAutoString domain;
      DomainNameFromURI(nodeUri, domain);
      if (! asciiRequest.Equals(domain))
        continue; // domain names don't match
    }

    // --- URI matching ---
    if (query->Uri()) {
      if (! nodeUri) { // lazy creation of nodeUri
        if (NS_FAILED(NS_NewURI(getter_AddRefs(nodeUri), aNode->mURI)))
          continue;
      }
      if (! query->UriIsPrefix()) {
        // easy case: the URI is an exact match
        PRBool equals;
        nsresult rv = query->Uri()->Equals(nodeUri, &equals);
        NS_ENSURE_SUCCESS(rv, PR_FALSE);
        if (! equals)
          continue;
      } else {
        // harder case: match prefix, note that we need to get the ASCII string
        // from the node's parsed URI instead of using the node's mUrl string,
        // because that might not be normalized
        nsCAutoString nodeUriString;
        nodeUri->GetAsciiSpec(nodeUriString);
        nsCAutoString queryUriString;
        query->Uri()->GetAsciiSpec(queryUriString);
        if (queryUriString.Length() > nodeUriString.Length())
          continue; // not long enough to match as prefix
        nodeUriString.SetLength(queryUriString.Length());
        if (! nodeUriString.Equals(queryUriString))
          continue; // prefixes don't match
      }
    }

    // If we ever make it to the bottom of this loop, that means it passed all
    // tests for the given query. Since queries are ORed together, that means
    // it passed everything and we are done.
    return PR_TRUE;
  }

  // didn't match any query
  return PR_FALSE;
}


// nsNavHistory::AsciiHostNameFromHostString
//
//    We might have interesting encodings and different case in the host name.
//    This will convert that host name into an ASCII host name by sending it
//    through the URI canonicalization. The result can be used for comparison
//    with other ASCII host name strings.

nsresult // static
nsNavHistory::AsciiHostNameFromHostString(const nsACString& aHostName,
                                          nsACString& aAscii)
{
  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), aHostName);
  NS_ENSURE_SUCCESS(rv, rv);
  return uri->GetAsciiHost(aAscii);
}


// nsNavHistory::DomainNameFromURI
//
//    This does the www.mozilla.org -> mozilla.org and
//    foo.theregister.co.uk -> theregister.co.uk conversion

void
nsNavHistory::DomainNameFromURI(nsIURI *aURI,
                                nsACString& aDomainName)
{
  // lazily get the effective tld service
  if (!mTLDService)
    mTLDService = do_GetService(NS_EFFECTIVETLDSERVICE_CONTRACTID);

  if (mTLDService) {
    // get the base domain for a given hostname.
    // e.g. for "images.bbc.co.uk", this would be "bbc.co.uk".
    nsresult rv = mTLDService->GetBaseDomain(aURI, 0, aDomainName);
    if (NS_SUCCEEDED(rv))
      return;
  }

  // just return the original hostname
  // (it's also possible the host is an IP address)
  aURI->GetAsciiHost(aDomainName);
}


// Nav history *****************************************************************


// nsNavHistory::GetHasHistoryEntries

NS_IMETHODIMP
nsNavHistory::GetHasHistoryEntries(PRBool* aHasEntries)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG_POINTER(aHasEntries);

  nsCOMPtr<mozIStorageStatement> dbSelectStatement;
  nsresult rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT 1 "
      "WHERE EXISTS (SELECT id FROM moz_historyvisits_temp LIMIT 1) "
        "OR EXISTS (SELECT id FROM moz_historyvisits LIMIT 1)"),
    getter_AddRefs(dbSelectStatement));
  NS_ENSURE_SUCCESS(rv, rv);
  return dbSelectStatement->ExecuteStep(aHasEntries);
}

nsresult
nsNavHistory::FixInvalidFrecenciesForExcludedPlaces()
{
  // for every moz_place that has an invalid frecency (< 0) and
  // is an unvisited child of a livemark feed, or begins with "place:",
  // set frecency to 0 so that it is excluded from url bar autocomplete.
  nsCOMPtr<mozIStorageStatement> dbUpdateStatement;
  nsresult rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "UPDATE moz_places_view "
      "SET frecency = 0 WHERE id IN ("
        "SELECT h.id FROM moz_places h "
        "WHERE h.url >= 'place:' AND h.url < 'place;' "
        "UNION "
        "SELECT h.id FROM moz_places_temp h "
        "WHERE  h.url >= 'place:' AND h.url < 'place;' "
        "UNION "
        // Unvisited child of a livemark
        "SELECT b.fk FROM moz_bookmarks b "
        "JOIN moz_bookmarks bp ON bp.id = b.parent "
        "JOIN moz_items_annos a ON a.item_id = bp.id "
        "JOIN moz_anno_attributes n ON n.id = a.anno_attribute_id "
        "WHERE n.name = ?1 "
        "AND b.fk IN( "
          "SELECT id FROM moz_places WHERE visit_count = 0 AND frecency < 0 "
          "UNION ALL "
          "SELECT id FROM moz_places_temp WHERE visit_count = 0 AND frecency < 0 "
        ") "
      ")"),
    getter_AddRefs(dbUpdateStatement));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = BindStatementURLCString(dbUpdateStatement, 0, NS_LITERAL_CSTRING(LMANNO_FEEDURI));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = dbUpdateStatement->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
nsNavHistory::CalculateFullVisitCount(PRInt64 aPlaceId, PRInt32 *aVisitCount)
{
  mozStorageStatementScoper scope(mDBFullVisitCount);

  nsresult rv = mDBFullVisitCount->BindInt64Parameter(0, aPlaceId);
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool hasVisits = PR_TRUE;
  rv = mDBFullVisitCount->ExecuteStep(&hasVisits);
  NS_ENSURE_SUCCESS(rv, rv);

  if (hasVisits) {
    rv = mDBFullVisitCount->GetInt32(0, aVisitCount);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else
    *aVisitCount = 0;
  
  return NS_OK;
}

// nsNavHistory::MarkPageAsFollowedBookmark
//
// We call MarkPageAsFollowedBookmark() before visiting a URL in order to 
// help determine the transition type of the visit.  
// We keep track of the URL so that later, in AddVisitChain() 
// we can use TRANSITION_BOOKMARK as the transition.
// Note, AddVisitChain() is not called immediately when we are doing LAZY_ADDs
//
// @see MarkPageAsTyped

NS_IMETHODIMP
nsNavHistory::MarkPageAsFollowedBookmark(nsIURI* aURI)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aURI);

  // don't add when history is disabled
  if (IsHistoryDisabled())
    return NS_OK;

  nsCAutoString uriString;
  nsresult rv = aURI->GetSpec(uriString);
  NS_ENSURE_SUCCESS(rv, rv);

  // if URL is already in the bookmark queue, then we need to remove the old one
  PRInt64 unusedEventTime;
  if (mRecentBookmark.Get(uriString, &unusedEventTime))
    mRecentBookmark.Remove(uriString);

  if (mRecentBookmark.Count() > RECENT_EVENT_QUEUE_MAX_LENGTH)
    ExpireNonrecentEvents(&mRecentBookmark);

  mRecentBookmark.Put(uriString, GetNow());
  return NS_OK;
}


// nsNavHistory::CanAddURI
//
//    Filter out unwanted URIs such as "chrome:", "mailbox:", etc.
//
//    The model is if we don't know differently then add which basically means
//    we are suppose to try all the things we know not to allow in and then if
//    we don't bail go on and allow it in.

NS_IMETHODIMP
nsNavHistory::CanAddURI(nsIURI* aURI, PRBool* canAdd)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aURI);
  NS_ENSURE_ARG_POINTER(canAdd);

  // If the user is in private browsing mode, don't add any entry.
  if (InPrivateBrowsingMode()) {
    *canAdd = PR_FALSE;
    return NS_OK;
  }

  nsCAutoString scheme;
  nsresult rv = aURI->GetScheme(scheme);
  NS_ENSURE_SUCCESS(rv, rv);

  // first check the most common cases (HTTP, HTTPS) to allow in to avoid most
  // of the work
  if (scheme.EqualsLiteral("http")) {
    *canAdd = PR_TRUE;
    return NS_OK;
  }
  if (scheme.EqualsLiteral("https")) {
    *canAdd = PR_TRUE;
    return NS_OK;
  }

  // now check for all bad things
  if (scheme.EqualsLiteral("about") ||
      scheme.EqualsLiteral("imap") ||
      scheme.EqualsLiteral("news") ||
      scheme.EqualsLiteral("mailbox") ||
      scheme.EqualsLiteral("moz-anno") ||
      scheme.EqualsLiteral("view-source") ||
      scheme.EqualsLiteral("chrome") ||
      scheme.EqualsLiteral("data") ||
      scheme.EqualsLiteral("wyciwyg")) {
    *canAdd = PR_FALSE;
    return NS_OK;
  }
  *canAdd = PR_TRUE;
  return NS_OK;
}

// nsNavHistory::AddVisit
//
//    Adds or updates a page with the given URI. The ID of the new visit will
//    be put into aVisitID.
//
//    THE RETURNED NEW VISIT ID MAY BE 0 indicating that this page should not be
//    added to the history.

NS_IMETHODIMP
nsNavHistory::AddVisit(nsIURI* aURI, PRTime aTime, nsIURI* aReferringURI,
                       PRInt32 aTransitionType, PRBool aIsRedirect,
                       PRInt64 aSessionID, PRInt64* aVisitID)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aURI);
  NS_ENSURE_ARG_POINTER(aVisitID);

  // Filter out unwanted URIs, silently failing
  PRBool canAdd = PR_FALSE;
  nsresult rv = CanAddURI(aURI, &canAdd);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!canAdd) {
    *aVisitID = 0;
    return NS_OK;
  }

  // This will prevent corruption since we have to do a two-phase add.
  // Generally this won't do anything because AddURI has its own transaction.
  mozStorageTransaction transaction(mDBConn, PR_FALSE);

  // see if this is an update (revisit) or a new page
  mozStorageStatementScoper scoper(mDBGetPageVisitStats);
  rv = BindStatementURI(mDBGetPageVisitStats, 0, aURI);
  NS_ENSURE_SUCCESS(rv, rv);
  PRBool alreadyVisited = PR_FALSE;
  rv = mDBGetPageVisitStats->ExecuteStep(&alreadyVisited);
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt64 pageID = 0;
  PRInt32 hidden;
  PRInt32 typed;
  PRBool newItem = PR_FALSE; // used to send out notifications at the end
  if (alreadyVisited) {
    // Update the existing entry...
    rv = mDBGetPageVisitStats->GetInt64(0, &pageID);
    NS_ENSURE_SUCCESS(rv, rv);

    PRInt32 oldVisitCount = 0;
    rv = mDBGetPageVisitStats->GetInt32(1, &oldVisitCount);
    NS_ENSURE_SUCCESS(rv, rv);

    PRInt32 oldTypedState = 0;
    rv = mDBGetPageVisitStats->GetInt32(2, &oldTypedState);
    NS_ENSURE_SUCCESS(rv, rv);

    PRBool oldHiddenState = 0;
    rv = mDBGetPageVisitStats->GetInt32(3, &oldHiddenState);
    NS_ENSURE_SUCCESS(rv, rv);

    // free the previous statement before we make a new one
    mDBGetPageVisitStats->Reset();
    scoper.Abandon();

    // embedded links and redirects will be hidden, but don't hide pages that
    // are already unhidden.
    //
    // Note that we test the redirect flag and not for the redirect transition
    // type. The transition type refers to how we got here, and whether a page
    // is shown does not depend on whether you got to it through a redirect.
    // Rather, we want to hide pages that redirect themselves somewhere
    // else, which is what the redirect flag means.
    //
    // note, we want to unhide any hidden pages that the user explicitly types
    // (aTransitionType == TRANSITION_TYPED) so that they will appear in
    // the history UI (sidebar, history menu, url bar autocomplete, etc)
    hidden = oldHiddenState;
    if (hidden == 1 && (!aIsRedirect || aTransitionType == TRANSITION_TYPED) &&
        aTransitionType != TRANSITION_EMBED)
      hidden = 0; // unhide

    typed = (PRInt32)(oldTypedState == 1 || (aTransitionType == TRANSITION_TYPED));

    // some items may have a visit count of 0 which will not count for link
    // visiting, so be sure to note this transition
    if (oldVisitCount == 0)
      newItem = PR_TRUE;

    // update with new stats
    mozStorageStatementScoper updateScoper(mDBUpdatePageVisitStats);
    rv = mDBUpdatePageVisitStats->BindInt64Parameter(0, pageID);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mDBUpdatePageVisitStats->BindInt32Parameter(1, hidden);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mDBUpdatePageVisitStats->BindInt32Parameter(2, typed);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mDBUpdatePageVisitStats->Execute();
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    // New page
    newItem = PR_TRUE;

    // free the previous statement before we make a new one
    mDBGetPageVisitStats->Reset();
    scoper.Abandon();

    // Hide only embedded links and redirects
    // See the hidden computation code above for a little more explanation.
    hidden = (PRInt32)(aTransitionType == TRANSITION_EMBED || aIsRedirect);

    typed = (PRInt32)(aTransitionType == TRANSITION_TYPED);

    // set as visited once, no title
    nsString voidString;
    voidString.SetIsVoid(PR_TRUE);
    rv = InternalAddNewPage(aURI, voidString, hidden == 1, typed == 1, 1,
                            PR_TRUE, &pageID);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Get the place id for the referrer, if we have one
  PRInt64 referringVisitID = 0;
  PRInt64 referringSessionID;
  if (aReferringURI &&
      !FindLastVisit(aReferringURI, &referringVisitID, &referringSessionID)) {
    // Add the referrer
    rv = AddVisit(aReferringURI, aTime - 1, nsnull, TRANSITION_LINK, PR_FALSE,
                  aSessionID, &referringVisitID);
    if (NS_FAILED(rv))
      referringVisitID = 0;
  }

  rv = InternalAddVisit(pageID, referringVisitID, aSessionID, aTime,
                        aTransitionType, aVisitID);
  transaction.Commit();

  // Update frecency (*after* the visit info is in the db)
  // Swallow errors here, since if we've gotten this far, it's more
  // important to notify the observers below.
  nsNavBookmarks *bs = nsNavBookmarks::GetBookmarksService();
  NS_ENSURE_TRUE(bs, NS_ERROR_OUT_OF_MEMORY);
  (void)UpdateFrecency(pageID, bs->IsRealBookmark(pageID));

  // Notify observers: The hidden detection code must match that in
  // GetQueryResults to maintain consistency.
  // FIXME bug 325241: make a way to observe hidden URLs
  PRUint32 added = 0;
  if (!hidden && aTransitionType != TRANSITION_EMBED &&
                 aTransitionType != TRANSITION_DOWNLOAD) {
    ENUMERATE_OBSERVERS(mCanNotify, mCacheObservers, mObservers, nsINavHistoryObserver,
                        OnVisit(aURI, *aVisitID, aTime, aSessionID,
                                referringVisitID, aTransitionType, &added));
  }

  // Normally docshell sends the link visited observer notification for us (this
  // will tell all the documents to update their visited link coloring).
  // However, for redirects (since we implement nsIGlobalHistory3) and downloads
  // (since we implement nsIDownloadHistory) this will not happen and we need to
  // send it ourselves.
  if (newItem && (aIsRedirect || aTransitionType == TRANSITION_DOWNLOAD)) {
    nsCOMPtr<nsIObserverService> obsService =
      do_GetService("@mozilla.org/observer-service;1");
    if (obsService)
      obsService->NotifyObservers(aURI, NS_LINK_VISITED_EVENT_TOPIC, nsnull);
  }

  return NS_OK;
}


// nsNavHistory::GetNewQuery

NS_IMETHODIMP
nsNavHistory::GetNewQuery(nsINavHistoryQuery **_retval)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG_POINTER(_retval);

  *_retval = new nsNavHistoryQuery();
  if (! *_retval)
    return NS_ERROR_OUT_OF_MEMORY;
  NS_ADDREF(*_retval);
  return NS_OK;
}

// nsNavHistory::GetNewQueryOptions

NS_IMETHODIMP
nsNavHistory::GetNewQueryOptions(nsINavHistoryQueryOptions **_retval)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG_POINTER(_retval);

  *_retval = new nsNavHistoryQueryOptions();
  NS_ENSURE_TRUE(*_retval, NS_ERROR_OUT_OF_MEMORY);
  NS_ADDREF(*_retval);
  return NS_OK;
}

// nsNavHistory::ExecuteQuery
//

NS_IMETHODIMP
nsNavHistory::ExecuteQuery(nsINavHistoryQuery *aQuery, nsINavHistoryQueryOptions *aOptions,
                           nsINavHistoryResult** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aQuery);
  NS_ENSURE_ARG(aOptions);
  NS_ENSURE_ARG_POINTER(_retval);

  return ExecuteQueries(&aQuery, 1, aOptions, _retval);
}


// nsNavHistory::ExecuteQueries
//
//    This function is actually very simple, we just create the proper root node (either
//    a bookmark folder or a complex query node) and assign it to the result. The node
//    will then populate itself accordingly.
//
//    Quick overview of query operation: When you call this function, we will construct
//    the correct container node and set the options you give it. This node will then
//    fill itself. Folder nodes will call nsNavBookmarks::QueryFolderChildren, and
//    all other queries will call GetQueryResults. If these results contain other
//    queries, those will be populated when the container is opened.

NS_IMETHODIMP
nsNavHistory::ExecuteQueries(nsINavHistoryQuery** aQueries, PRUint32 aQueryCount,
                             nsINavHistoryQueryOptions *aOptions,
                             nsINavHistoryResult** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aQueries);
  NS_ENSURE_ARG(aOptions);
  NS_ENSURE_ARG(aQueryCount);
  NS_ENSURE_ARG_POINTER(_retval);

  nsresult rv;
  // concrete options
  nsCOMPtr<nsNavHistoryQueryOptions> options = do_QueryInterface(aOptions);
  NS_ENSURE_TRUE(options, NS_ERROR_INVALID_ARG);

  // concrete queries array
  nsCOMArray<nsNavHistoryQuery> queries;
  for (PRUint32 i = 0; i < aQueryCount; i ++) {
    nsCOMPtr<nsNavHistoryQuery> query = do_QueryInterface(aQueries[i], &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    queries.AppendObject(query);
  }

  // root node
  nsRefPtr<nsNavHistoryContainerResultNode> rootNode;
  PRInt64 folderId = GetSimpleBookmarksQueryFolder(queries, options);
  if (folderId) {
    // In the simple case where we're just querying children of a single bookmark
    // folder, we can more efficiently generate results.
    nsNavBookmarks *bookmarks = nsNavBookmarks::GetBookmarksService();
    NS_ENSURE_TRUE(bookmarks, NS_ERROR_OUT_OF_MEMORY);
    nsRefPtr<nsNavHistoryResultNode> tempRootNode;
    rv = bookmarks->ResultNodeForContainer(folderId, options,
                                           getter_AddRefs(tempRootNode));
    NS_ENSURE_SUCCESS(rv, rv);
    rootNode = tempRootNode->GetAsContainer();
  } else {
    // complex query
    rootNode = new nsNavHistoryQueryResultNode(EmptyCString(), EmptyCString(),
                                               queries, options);
    NS_ENSURE_TRUE(rootNode, NS_ERROR_OUT_OF_MEMORY);
  }

  // result object
  nsRefPtr<nsNavHistoryResult> result;
  rv = nsNavHistoryResult::NewHistoryResult(aQueries, aQueryCount, options, rootNode,
                                            getter_AddRefs(result));
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ADDREF(*_retval = result);
  return NS_OK;
}

// determine from our nsNavHistoryQuery array and nsNavHistoryQueryOptions
// if this is the place query from the history menu.
// from browser-menubar.inc, our history menu query is:
// place:redirectsMode=2&sort=4&maxResults=10
// note, any maxResult > 0 will still be considered a history menu query
// or if this is the place query from the "Most Visited" item in the "Smart Bookmarks" folder:
// place:redirectsMode=2&sort=8&maxResults=10
// note, any maxResult > 0 will still be considered a Most Visited menu query
static
PRBool IsOptimizableHistoryQuery(const nsCOMArray<nsNavHistoryQuery>& aQueries,
                                 nsNavHistoryQueryOptions *aOptions,
                                 PRUint16 aSortMode)
{
  if (aQueries.Count() != 1)
    return PR_FALSE;

  nsNavHistoryQuery *aQuery = aQueries[0];
 
  if (aOptions->QueryType() != nsINavHistoryQueryOptions::QUERY_TYPE_HISTORY)
    return PR_FALSE;

  if (aOptions->ResultType() != nsINavHistoryQueryOptions::RESULTS_AS_URI)
    return PR_FALSE;

  if (aOptions->SortingMode() != aSortMode)
    return PR_FALSE;

  if (aOptions->MaxResults() <= 0)
    return PR_FALSE;

  if (aOptions->ExcludeItems())
    return PR_FALSE;

  if (aOptions->IncludeHidden())
    return PR_FALSE;

  if (aQuery->MinVisits() != -1 || aQuery->MaxVisits() != -1)
    return PR_FALSE;

  if (aQuery->BeginTime() || aQuery->BeginTimeReference()) 
    return PR_FALSE;

  if (aQuery->EndTime() || aQuery->EndTimeReference()) 
    return PR_FALSE;

  if (!aQuery->SearchTerms().IsEmpty()) 
    return PR_FALSE;

  if (aQuery->OnlyBookmarked()) 
    return PR_FALSE;

  if (aQuery->DomainIsHost() || !aQuery->Domain().IsEmpty())
    return PR_FALSE;

  if (aQuery->AnnotationIsNot() || !aQuery->Annotation().IsEmpty()) 
    return PR_FALSE;

  if (aQuery->UriIsPrefix() || aQuery->Uri()) 
    return PR_FALSE;

  if (aQuery->Folders().Length() > 0)
    return PR_FALSE;

  if (aQuery->Tags().Length() > 0)
    return PR_FALSE;

  return PR_TRUE;
}

static
PRBool NeedToFilterResultSet(const nsCOMArray<nsNavHistoryQuery>& aQueries, 
                             nsNavHistoryQueryOptions *aOptions)
{
  // Never filter queries returning queries
  PRUint16 resultType = aOptions->ResultType();
  if (resultType == nsINavHistoryQueryOptions::RESULTS_AS_DATE_QUERY ||
      resultType == nsINavHistoryQueryOptions::RESULTS_AS_DATE_SITE_QUERY ||
      resultType == nsINavHistoryQueryOptions::RESULTS_AS_TAG_QUERY ||
      resultType == nsINavHistoryQueryOptions::RESULTS_AS_SITE_QUERY)
    return PR_FALSE;

  // Always filter bookmarks queries to avoid the inclusion of query nodes,
  // but RESULTS AS TAG QUERY never needs to be filtered.
  if (aOptions->QueryType() == nsINavHistoryQueryOptions::QUERY_TYPE_BOOKMARKS)
    return PR_TRUE;

  nsCString parentAnnotationToExclude;
  nsresult rv = aOptions->GetExcludeItemIfParentHasAnnotation(parentAnnotationToExclude);
  NS_ENSURE_SUCCESS(rv, PR_TRUE);
  if (!parentAnnotationToExclude.IsEmpty())
    return PR_TRUE;

  PRInt32 i;
  for (i = 0; i < aQueries.Count(); i ++) {
    if (aQueries[i]->Folders().Length() != 0) {
      return PR_TRUE;
    } else {
      PRBool hasSearchTerms;
      nsresult rv = aQueries[i]->GetHasSearchTerms(&hasSearchTerms);
      if (NS_FAILED(rv) || hasSearchTerms)
        return PR_TRUE;
    }
  }
  return PR_FALSE;
}

// ** Helper class for ConstructQueryString **/

class PlacesSQLQueryBuilder
{
public:
  PlacesSQLQueryBuilder(const nsCString& aConditions,
                        nsNavHistoryQueryOptions* aOptions,
                        PRBool aUseLimit,
                        nsNavHistory::StringHash& aAddParams,
                        PRBool aHasSearchTerms);

  nsresult GetQueryString(nsCString& aQueryString);

private:
  nsresult Select();

  nsresult SelectAsURI();
  nsresult SelectAsVisit();
  nsresult SelectAsDay();
  nsresult SelectAsSite();
  nsresult SelectAsTag();

  nsresult Where();
  nsresult GroupBy();
  nsresult OrderBy();
  nsresult Limit();

  void OrderByColumnIndexAsc(PRInt32 aIndex);
  void OrderByColumnIndexDesc(PRInt32 aIndex);
  // Use these if you want a case insensitive sorting.
  void OrderByTextColumnIndexAsc(PRInt32 aIndex);
  void OrderByTextColumnIndexDesc(PRInt32 aIndex);

  const nsCString& mConditions;
  PRBool mUseLimit;
  PRBool mHasSearchTerms;

  PRUint16 mResultType;
  PRUint16 mQueryType;
  PRBool mIncludeHidden;
  PRUint16 mRedirectsMode;
  PRUint16 mSortingMode;
  PRUint32 mMaxResults;

  nsCString mQueryString;
  nsCString mGroupBy;
  PRBool mHasDateColumns;
  PRBool mSkipOrderBy;
  nsNavHistory::StringHash& mAddParams;
};

PlacesSQLQueryBuilder::PlacesSQLQueryBuilder(
    const nsCString& aConditions, 
    nsNavHistoryQueryOptions* aOptions, 
    PRBool aUseLimit,
    nsNavHistory::StringHash& aAddParams,
    PRBool aHasSearchTerms) :
  mConditions(aConditions),
  mUseLimit(aUseLimit),
  mResultType(aOptions->ResultType()),
  mQueryType(aOptions->QueryType()),
  mIncludeHidden(aOptions->IncludeHidden()),
  mRedirectsMode(aOptions->RedirectsMode()),
  mSortingMode(aOptions->SortingMode()),
  mMaxResults(aOptions->MaxResults()),
  mSkipOrderBy(PR_FALSE),
  mAddParams(aAddParams),
  mHasSearchTerms(aHasSearchTerms)
{
  mHasDateColumns = (mQueryType == nsINavHistoryQueryOptions::QUERY_TYPE_BOOKMARKS);
}

nsresult
PlacesSQLQueryBuilder::GetQueryString(nsCString& aQueryString)
{
  nsresult rv = Select();
  NS_ENSURE_SUCCESS(rv, rv);
  rv = Where();
  NS_ENSURE_SUCCESS(rv, rv);
  rv = GroupBy();
  NS_ENSURE_SUCCESS(rv, rv);
  rv = OrderBy();
  NS_ENSURE_SUCCESS(rv, rv);
  rv = Limit();
  NS_ENSURE_SUCCESS(rv, rv);

  aQueryString = mQueryString;
  return NS_OK;
}

nsresult
PlacesSQLQueryBuilder::Select()
{
  nsresult rv;

  switch (mResultType)
  {
    case nsINavHistoryQueryOptions::RESULTS_AS_URI:
    case nsINavHistoryQueryOptions::RESULTS_AS_TAG_CONTENTS:
      rv = SelectAsURI();
      NS_ENSURE_SUCCESS(rv, rv);
      break;

    case nsINavHistoryQueryOptions::RESULTS_AS_VISIT:
    case nsINavHistoryQueryOptions::RESULTS_AS_FULL_VISIT:
      rv = SelectAsVisit();
      NS_ENSURE_SUCCESS(rv, rv);
      break;

    case nsINavHistoryQueryOptions::RESULTS_AS_DATE_QUERY:
    case nsINavHistoryQueryOptions::RESULTS_AS_DATE_SITE_QUERY:
      rv = SelectAsDay();
      NS_ENSURE_SUCCESS(rv, rv);
      break;

    case nsINavHistoryQueryOptions::RESULTS_AS_SITE_QUERY:
      rv = SelectAsSite();
      NS_ENSURE_SUCCESS(rv, rv);
      break;

    case nsINavHistoryQueryOptions::RESULTS_AS_TAG_QUERY:
      rv = SelectAsTag();
      NS_ENSURE_SUCCESS(rv, rv);
      break;

    default:
      NS_NOTREACHED("Invalid result type");
  }
  return NS_OK;
}

nsresult
PlacesSQLQueryBuilder::SelectAsURI()
{
  nsNavHistory *history = nsNavHistory::GetHistoryService();
  nsCAutoString tagsSqlFragment;

  switch (mQueryType) {
    case nsINavHistoryQueryOptions::QUERY_TYPE_HISTORY:
      GetTagsSqlFragment(history->GetTagsFolder(),
                         NS_LITERAL_CSTRING("h.id"),
                         mHasSearchTerms,
                         tagsSqlFragment);

      mQueryString = NS_LITERAL_CSTRING(
        "SELECT h.id, h.url, h.title, h.rev_host, h.visit_count, "
        "h.last_visit_date, f.url, v.session, null, null, null, null, ") +
        tagsSqlFragment + NS_LITERAL_CSTRING(
        "FROM moz_places_temp h "
        "JOIN moz_historyvisits_temp v ON h.id = v.place_id "
        "LEFT JOIN moz_favicons f ON h.favicon_id = f.id "
        // WHERE 1 is a no-op since additonal conditions will start with AND.
        "WHERE 1 "
          "{QUERY_OPTIONS_VISITS} {QUERY_OPTIONS_PLACES} "
          "{ADDITIONAL_CONDITIONS} "
        "GROUP BY h.id "
        "UNION ALL "
        "SELECT h.id, h.url, h.title, h.rev_host, h.visit_count, "
        "h.last_visit_date, f.url, v.session, null, null, null, null, ") +
        tagsSqlFragment + NS_LITERAL_CSTRING(
        "FROM moz_places_temp h "
        "JOIN moz_historyvisits v ON h.id = v.place_id "
        "LEFT JOIN moz_favicons f ON h.favicon_id = f.id "
        "WHERE h.id NOT IN (SELECT place_id FROM moz_historyvisits_temp) "
          "{QUERY_OPTIONS_VISITS} {QUERY_OPTIONS_PLACES} "
          "{ADDITIONAL_CONDITIONS} "
        "GROUP BY h.id "
        "UNION ALL "
        "SELECT h.id, h.url, h.title, h.rev_host, h.visit_count, "
        "h.last_visit_date, f.url, v.session, null, null, null, null, ") +
        tagsSqlFragment + NS_LITERAL_CSTRING(
        "FROM moz_places h "
        "JOIN moz_historyvisits_temp v ON h.id = v.place_id "
        "LEFT JOIN moz_favicons f ON h.favicon_id = f.id "
        "WHERE h.id NOT IN (SELECT id FROM moz_places_temp) "
          "{QUERY_OPTIONS_VISITS} {QUERY_OPTIONS_PLACES} "
          "{ADDITIONAL_CONDITIONS} "
        "GROUP BY h.id "
        "UNION ALL "
        "SELECT h.id, h.url, h.title, h.rev_host, h.visit_count, "
        "h.last_visit_date, f.url, v.session, null, null, null, null, ") +
        tagsSqlFragment + NS_LITERAL_CSTRING(
        "FROM moz_places h "
        "JOIN moz_historyvisits v ON h.id = v.place_id "
        "LEFT JOIN moz_favicons f ON h.favicon_id = f.id "
        "WHERE h.id NOT IN (SELECT id FROM moz_places_temp) "
          "AND h.id NOT IN (SELECT place_id FROM moz_historyvisits_temp) "
          "{QUERY_OPTIONS_VISITS} {QUERY_OPTIONS_PLACES} "
          "{ADDITIONAL_CONDITIONS} "
        "GROUP BY h.id ");
      break;

    case nsINavHistoryQueryOptions::QUERY_TYPE_BOOKMARKS:
      if (mResultType == nsINavHistoryQueryOptions::RESULTS_AS_TAG_CONTENTS) {
        // Order-by clause is hardcoded because we need to discard duplicates
        // in FilterResultSet. We will retain only the last modified item,
        // so we are ordering by place id and last modified to do a faster
        // filtering.
        mSkipOrderBy = PR_TRUE;

        GetTagsSqlFragment(history->GetTagsFolder(),
                           NS_LITERAL_CSTRING("b2.fk"),
                           mHasSearchTerms,
                           tagsSqlFragment);

        mQueryString = NS_LITERAL_CSTRING(
          "SELECT b2.fk, h.url, COALESCE(b2.title, h.title), h.rev_host, "
            "h.visit_count, h.last_visit_date, f.url, null, b2.id, "
            "b2.dateAdded, b2.lastModified, b2.parent, ") +
            tagsSqlFragment + NS_LITERAL_CSTRING(
          "FROM moz_bookmarks b2 "
          "JOIN (SELECT b.fk "
                "FROM moz_bookmarks b "
                // ADDITIONAL_CONDITIONS will filter on parent.
                "WHERE b.type = 1 {ADDITIONAL_CONDITIONS} "
                ") AS seed ON b2.fk = seed.fk "
          "JOIN moz_places_temp h ON h.id = b2.fk "
          "LEFT OUTER JOIN moz_favicons f ON h.favicon_id = f.id "
          "WHERE NOT EXISTS ( "
            "SELECT id FROM moz_bookmarks WHERE id = b2.parent AND parent = ") +
                nsPrintfCString("%lld", history->GetTagsFolder()) +
          NS_LITERAL_CSTRING(") "
          "UNION ALL "
          "SELECT b2.fk, h.url, COALESCE(b2.title, h.title), h.rev_host, "
            "h.visit_count, h.last_visit_date, f.url, null, b2.id, "
            "b2.dateAdded, b2.lastModified, b2.parent, ") +
            tagsSqlFragment + NS_LITERAL_CSTRING(
          "FROM moz_bookmarks b2 "
          "JOIN (SELECT b.fk "
                "FROM moz_bookmarks b "
                // ADDITIONAL_CONDITIONS will filter on parent.
                "WHERE b.type = 1 {ADDITIONAL_CONDITIONS} "
                ") AS seed ON b2.fk = seed.fk "
          "JOIN moz_places h ON h.id = b2.fk "
          "LEFT OUTER JOIN moz_favicons f ON h.favicon_id = f.id "
          "WHERE NOT EXISTS ( "
            "SELECT id FROM moz_bookmarks WHERE id = b2.parent AND parent = ") +
                nsPrintfCString("%lld", history->GetTagsFolder()) +
          NS_LITERAL_CSTRING(") "
            "AND h.id NOT IN (SELECT id FROM moz_places_temp) "
          "ORDER BY b2.fk DESC, b2.lastModified DESC");
      }
      else {
        GetTagsSqlFragment(history->GetTagsFolder(),
                           NS_LITERAL_CSTRING("b.fk"),
                           mHasSearchTerms,
                           tagsSqlFragment);
        mQueryString = NS_LITERAL_CSTRING(
          "SELECT b.fk, h.url, COALESCE(b.title, h.title), h.rev_host, "
            "h.visit_count, h.last_visit_date, f.url, null, b.id, "
            "b.dateAdded, b.lastModified, b.parent, ") +
            tagsSqlFragment + NS_LITERAL_CSTRING(
          "FROM moz_bookmarks b "
          "JOIN moz_places_temp h ON b.fk = h.id AND b.type = 1 "
          "LEFT OUTER JOIN moz_favicons f ON h.favicon_id = f.id "
          "WHERE NOT EXISTS "
            "(SELECT id FROM moz_bookmarks "
              "WHERE id = b.parent AND parent = ") +
                nsPrintfCString("%lld", history->GetTagsFolder()) +
            NS_LITERAL_CSTRING(") "
            "{ADDITIONAL_CONDITIONS}"
          "UNION ALL "
          "SELECT b.fk, h.url, COALESCE(b.title, h.title), h.rev_host, "
            "h.visit_count, h.last_visit_date, f.url, null, b.id, "
            "b.dateAdded, b.lastModified, b.parent, ") +
            tagsSqlFragment + NS_LITERAL_CSTRING(
          "FROM moz_bookmarks b "
          "JOIN moz_places h ON b.fk = h.id AND b.type = 1 "
          "LEFT OUTER JOIN moz_favicons f ON h.favicon_id = f.id "
          "WHERE h.id NOT IN (SELECT id FROM moz_places_temp) "
            "AND NOT EXISTS "
              "(SELECT id FROM moz_bookmarks "
                "WHERE id = b.parent AND parent = ") +
                  nsPrintfCString("%lld", history->GetTagsFolder()) +
              NS_LITERAL_CSTRING(") "
            "{ADDITIONAL_CONDITIONS}");
      }
      break;

    default:
      return NS_ERROR_NOT_IMPLEMENTED;
  }
  return NS_OK;
}

nsresult
PlacesSQLQueryBuilder::SelectAsVisit()
{
  nsNavHistory *history = nsNavHistory::GetHistoryService();
  nsCAutoString tagsSqlFragment;
  GetTagsSqlFragment(history->GetTagsFolder(),
                     NS_LITERAL_CSTRING("h.id"),
                     mHasSearchTerms,
                     tagsSqlFragment);
  mQueryString = NS_LITERAL_CSTRING(
    "SELECT h.id, h.url, h.title, h.rev_host, h.visit_count, "
      "v.visit_date, f.url, v.session, null, null, null, null, ") +
      tagsSqlFragment + NS_LITERAL_CSTRING(
    "FROM moz_places_temp h "
    "JOIN moz_historyvisits_temp v ON h.id = v.place_id "
    "LEFT JOIN moz_favicons f ON h.favicon_id = f.id "
    // WHERE 1 is a no-op since additonal conditions will start with AND.
    "WHERE 1 "
      "{QUERY_OPTIONS_VISITS} {QUERY_OPTIONS_PLACES} "
      "{ADDITIONAL_CONDITIONS} "
    "UNION ALL "
    "SELECT h.id, h.url, h.title, h.rev_host, h.visit_count, "
      "v.visit_date, f.url, v.session, null, null, null, null, ") +
      tagsSqlFragment + NS_LITERAL_CSTRING(
    "FROM moz_places_temp h "
    "JOIN moz_historyvisits v ON h.id = v.place_id "
    "LEFT JOIN moz_favicons f ON h.favicon_id = f.id "
    // WHERE 1 is a no-op since additonal conditions will start with AND.
    "WHERE 1 "
      "{QUERY_OPTIONS_VISITS} {QUERY_OPTIONS_PLACES} "
      "{ADDITIONAL_CONDITIONS} "
    "UNION ALL "
    "SELECT h.id, h.url, h.title, h.rev_host, h.visit_count, "
      "v.visit_date, f.url, v.session, null, null, null, null, ") +
      tagsSqlFragment + NS_LITERAL_CSTRING(
    "FROM moz_places h "
    "JOIN moz_historyvisits_temp v ON h.id = v.place_id "
    "LEFT JOIN moz_favicons f ON h.favicon_id = f.id "
    "WHERE h.id NOT IN (SELECT id FROM moz_places_temp) "
      "{QUERY_OPTIONS_VISITS} {QUERY_OPTIONS_PLACES} "
      "{ADDITIONAL_CONDITIONS} "
    "UNION ALL "
    "SELECT h.id, h.url, h.title, h.rev_host, h.visit_count, "
      "v.visit_date, f.url, v.session, null, null, null, null, ") +
      tagsSqlFragment + NS_LITERAL_CSTRING(
    "FROM moz_places h "
    "JOIN moz_historyvisits v ON h.id = v.place_id "
    "LEFT JOIN moz_favicons f ON h.favicon_id = f.id "
    "WHERE h.id NOT IN (SELECT id FROM moz_places_temp) "
      "{QUERY_OPTIONS_VISITS} {QUERY_OPTIONS_PLACES} "
      "{ADDITIONAL_CONDITIONS} ");

  return NS_OK;
}

nsresult
PlacesSQLQueryBuilder::SelectAsDay()
{
  mSkipOrderBy = PR_TRUE;

  // Sort child queries based on sorting mode if it's provided, otherwise
  // fallback to default sort by title ascending.
  PRUint16 sortingMode = nsINavHistoryQueryOptions::SORT_BY_TITLE_ASCENDING;
  if (mSortingMode != nsINavHistoryQueryOptions::SORT_BY_NONE &&
      mResultType == nsINavHistoryQueryOptions::RESULTS_AS_DATE_QUERY)
    sortingMode = mSortingMode;

  PRUint16 resultType =
    mResultType == nsINavHistoryQueryOptions::RESULTS_AS_DATE_QUERY ?
                   nsINavHistoryQueryOptions::RESULTS_AS_URI :
                   nsINavHistoryQueryOptions::RESULTS_AS_SITE_QUERY;

  // beginTime will become the node's time property, we don't use endTime
  // because it could overlap, and we use time to sort containers and find
  // insert position in a result.
  mQueryString = nsPrintfCString(1024,
     "SELECT null, "
       "'place:type=%ld&sort=%ld&beginTime='||beginTime||'&endTime='||endTime, "
      "dayTitle, null, null, beginTime, null, null, null, null, null, null "
     "FROM (", // TOUTER BEGIN
     resultType,
     sortingMode);

  nsNavHistory *history = nsNavHistory::GetHistoryService();
  NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);

  for (PRInt32 i = 0; i <= DATE_CONT_NUM(history->mExpireDaysMax); i++) {
    nsCAutoString dateName;
    // Timeframes are calculated as BeginTime <= container < EndTime.
    // Notice times can't be relative to now, since to recognize a query we
    // must ensure it won't change based on the time it is built.
    // So, to select till now, we really select till start of tomorrow, that is
    // a fixed timestamp.
    // These are used as limits for the inside containers.
    nsCAutoString sqlFragmentContainerBeginTime, sqlFragmentContainerEndTime;
    // These are used to query if the container should be visible.
    nsCAutoString sqlFragmentSearchBeginTime, sqlFragmentSearchEndTime;
    switch(i) {
       case 0:
        // Today
         history->GetStringFromName(
          NS_LITERAL_STRING("finduri-AgeInDays-is-0").get(), dateName);
        // From start of today
        sqlFragmentContainerBeginTime = NS_LITERAL_CSTRING(
          "(strftime('%s','now','localtime','start of day','utc')*1000000)");
        // To now (tomorrow)
        sqlFragmentContainerEndTime = NS_LITERAL_CSTRING(
          "(strftime('%s','now','localtime','start of day','+1 day','utc')*1000000)");
        // Search for the same timeframe.
        sqlFragmentSearchBeginTime = sqlFragmentContainerBeginTime;
        sqlFragmentSearchEndTime = sqlFragmentContainerEndTime;
         break;
       case 1:
        // Yesterday
         history->GetStringFromName(
          NS_LITERAL_STRING("finduri-AgeInDays-is-1").get(), dateName);
        // From start of yesterday
        sqlFragmentContainerBeginTime = NS_LITERAL_CSTRING(
          "(strftime('%s','now','localtime','start of day','-1 day','utc')*1000000)");
        // To start of today
        sqlFragmentContainerEndTime = NS_LITERAL_CSTRING(
          "(strftime('%s','now','localtime','start of day','utc')*1000000)");
        // Search for the same timeframe.
        sqlFragmentSearchBeginTime = sqlFragmentContainerBeginTime;
        sqlFragmentSearchEndTime = sqlFragmentContainerEndTime;
        break;
      case 2:
        // Last 7 days
        history->GetAgeInDaysString(7,
          NS_LITERAL_STRING("finduri-AgeInDays-last-is").get(), dateName);
        // From start of 7 days ago
        sqlFragmentContainerBeginTime = NS_LITERAL_CSTRING(
          "(strftime('%s','now','localtime','start of day','-7 days','utc')*1000000)");
        // To now (tomorrow)
        sqlFragmentContainerEndTime = NS_LITERAL_CSTRING(
          "(strftime('%s','now','localtime','start of day','+1 day','utc')*1000000)");
        // This is an overlapped container, but we show it only if there are
        // visits older than yesterday.
        sqlFragmentSearchBeginTime = sqlFragmentContainerBeginTime;
        sqlFragmentSearchEndTime = NS_LITERAL_CSTRING(
          "(strftime('%s','now','localtime','start of day','-2 days','utc')*1000000)");
        break;
      case 3:
        // This month
        history->GetStringFromName(
          NS_LITERAL_STRING("finduri-AgeInMonths-is-0").get(), dateName);
        // From start of this month
        sqlFragmentContainerBeginTime = NS_LITERAL_CSTRING(
          "(strftime('%s','now','localtime','start of month','utc')*1000000)");
        // To now (tomorrow)
        sqlFragmentContainerEndTime = NS_LITERAL_CSTRING(
          "(strftime('%s','now','localtime','start of day','+1 day','utc')*1000000)");
        // This is an overlapped container, but we show it only if there are
        // visits older than 7 days ago.
        sqlFragmentSearchBeginTime = sqlFragmentContainerBeginTime;
        sqlFragmentSearchEndTime = NS_LITERAL_CSTRING(
          "(strftime('%s','now','localtime','start of day','-7 days','utc')*1000000)");
         break;
       default:
        if (i == ADDITIONAL_DATE_CONT_NUM + 6) {
          // Older than 6 months
          history->GetAgeInDaysString(6,
            NS_LITERAL_STRING("finduri-AgeInMonths-isgreater").get(), dateName);
          // From start of epoch
          sqlFragmentContainerBeginTime = NS_LITERAL_CSTRING(
            "(datetime(0, 'unixepoch')*1000000)");
          // To start of 6 months ago ( 5 months + this month).
          sqlFragmentContainerEndTime = NS_LITERAL_CSTRING(
            "(strftime('%s','now','localtime','start of month','-5 months','utc')*1000000)");
          // Search for the same timeframe.
          sqlFragmentSearchBeginTime = sqlFragmentContainerBeginTime;
          sqlFragmentSearchEndTime = sqlFragmentContainerEndTime;
          break;
        }
        PRInt32 MonthIndex = i - ADDITIONAL_DATE_CONT_NUM;
        // Previous months' titles are month's name if inside this year,
        // month's name and year for previous years.
        PRExplodedTime tm;
        PR_ExplodeTime(PR_Now(), PR_LocalTimeParameters, &tm);
        PRUint16 currentYear = tm.tm_year;
        // Set day before month, setting month without day could cause issues.
        // For example setting month to February when today is 30, since
        // February has not 30 days, will return March instead.
        tm.tm_mday = 1;
        tm.tm_month -= MonthIndex;
        // Notice we use GMTParameters because we just want to get the first
        // day of each month.  Using LocalTimeParameters would instead force us
        // to apply a DST correction that we don't really need here.
        PR_NormalizeTime(&tm, PR_GMTParameters);
        // tm_month starts from 0 while GetMonthName expects a 1-based index.
        history->GetMonthName(tm.tm_month+1, dateName);

        // If the container is for a past year, add the year as suffix.
        if (tm.tm_year < currentYear)
          dateName.Append(nsPrintfCString(" %d", tm.tm_year));

        // From start of MonthIndex + 1 months ago
        sqlFragmentContainerBeginTime = NS_LITERAL_CSTRING(
          "(strftime('%s','now','localtime','start of month','-");
        sqlFragmentContainerBeginTime.AppendInt(MonthIndex);
        sqlFragmentContainerBeginTime.Append(NS_LITERAL_CSTRING(
            " months','utc')*1000000)"));
        // To start of MonthIndex months ago
        sqlFragmentContainerEndTime = NS_LITERAL_CSTRING(
          "(strftime('%s','now','localtime','start of month','-");
        sqlFragmentContainerEndTime.AppendInt(MonthIndex - 1);
        sqlFragmentContainerEndTime.Append(NS_LITERAL_CSTRING(
            " months','utc')*1000000)"));
        // Search for the same timeframe.
        sqlFragmentSearchBeginTime = sqlFragmentContainerBeginTime;
        sqlFragmentSearchEndTime = sqlFragmentContainerEndTime;
        break;
    }

    nsPrintfCString dateParam("dayTitle%d", i);
    mAddParams.Put(dateParam, dateName);

     nsPrintfCString dayRange(1024,
        "SELECT :%s AS dayTitle, "
               "%s AS beginTime, "
               "%s AS endTime "
         "WHERE EXISTS ( "
           "SELECT id FROM moz_historyvisits_temp "
          "WHERE visit_date >= %s "
            "AND visit_date < %s "
            "AND visit_type NOT IN (0,%d) "
            "{QUERY_OPTIONS_VISITS} "
          "UNION ALL "
          "SELECT id FROM moz_historyvisits "
          "WHERE visit_date >= %s "
            "AND visit_date < %s "
             "AND visit_type NOT IN (0,%d) "
             "{QUERY_OPTIONS_VISITS} "
           "LIMIT 1 "
        ") ",
      dateParam.get(),
      sqlFragmentContainerBeginTime.get(),
      sqlFragmentContainerEndTime.get(),
      sqlFragmentSearchBeginTime.get(),
      sqlFragmentSearchEndTime.get(),
       nsINavHistoryService::TRANSITION_EMBED,
      sqlFragmentSearchBeginTime.get(),
      sqlFragmentSearchEndTime.get(),
      nsINavHistoryService::TRANSITION_EMBED);

    mQueryString.Append(dayRange);

    if (i < DATE_CONT_NUM(history->mExpireDaysMax))
        mQueryString.Append(NS_LITERAL_CSTRING(" UNION ALL "));
  }

  mQueryString.Append(NS_LITERAL_CSTRING(") ")); // TOUTER END

  return NS_OK;
}

nsresult
PlacesSQLQueryBuilder::SelectAsSite()
{
  nsCAutoString localFiles;

  nsNavHistory *history = nsNavHistory::GetHistoryService();
  NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);

  history->GetStringFromName(NS_LITERAL_STRING("localhost").get(), localFiles);
  mAddParams.Put(NS_LITERAL_CSTRING("localhost"), localFiles);

  // We want just sites, but from whole database.
  if (mConditions.IsEmpty()) {
    mQueryString = nsPrintfCString(2048,
      "SELECT DISTINCT null, "
             "'place:type=%ld&sort=%ld&domain=&domainIsHost=true', "
             ":localhost, :localhost, null, null, null, null, null, null, null "
      "WHERE EXISTS ( "
        "SELECT id FROM moz_places_temp "
        "WHERE hidden <> 1 "
          "AND rev_host = '.' "
          "AND visit_count > 0 "
          "AND url BETWEEN 'file://' AND 'file:/~' "
        "UNION ALL "
        "SELECT id FROM moz_places "
        "WHERE id NOT IN (SELECT id FROM moz_places_temp) "
          "AND hidden <> 1 "
          "AND rev_host = '.' "
          "AND visit_count > 0 "
          "AND url BETWEEN 'file://' AND 'file:/~' "
      ") "
      "UNION ALL "
      "SELECT DISTINCT null, "
             "'place:type=%ld&sort=%ld&domain='||host||'&domainIsHost=true', "
             "host, host, null, null, null, null, null, null, null "
      "FROM ( "
        "SELECT get_unreversed_host(rev_host) host "
        "FROM ( "
          "SELECT DISTINCT rev_host FROM moz_places_temp "
          "WHERE hidden <> 1 "
            "AND rev_host <> '.' "
            "AND visit_count > 0 "
          "UNION ALL "
          "SELECT DISTINCT rev_host FROM moz_places "
          "WHERE id NOT IN (SELECT id FROM moz_places_temp) "
            "AND hidden <> 1 "
            "AND rev_host <> '.' "
            "AND visit_count > 0 "
        ") "
      "ORDER BY 1 ASC) ",
      nsINavHistoryQueryOptions::RESULTS_AS_URI,
      mSortingMode,
      nsINavHistoryQueryOptions::RESULTS_AS_URI,
      mSortingMode);
  // Now we need to use the filters - we need them all
  } else {

    mQueryString = nsPrintfCString(4096,
      "SELECT DISTINCT null, "
             "'place:type=%ld&sort=%ld&domain=&domainIsHost=true"
               "&beginTime='||:begin_time||'&endTime='||:end_time, "
             ":localhost, :localhost, null, null, null, null, null, null, null "
      "WHERE EXISTS( "
        "SELECT h.id "
        "FROM moz_places h "
        "JOIN moz_historyvisits v ON v.place_id = h.id "
        "WHERE h.hidden <> 1 AND h.rev_host = '.' "
          "AND h.visit_count > 0 "
          "AND h.url BETWEEN 'file://' AND 'file:/~' "
          "{QUERY_OPTIONS_VISITS} {QUERY_OPTIONS_PLACES} "
          "{ADDITIONAL_CONDITIONS} "
        "UNION "
        "SELECT h.id "
        "FROM moz_places_temp h "
        "JOIN moz_historyvisits v ON v.place_id = h.id "
        "WHERE h.hidden <> 1 AND h.rev_host = '.' "
          "AND h.visit_count > 0 "
          "AND h.url BETWEEN 'file://' AND 'file:/~' "
          "{QUERY_OPTIONS_VISITS} {QUERY_OPTIONS_PLACES} "
          "{ADDITIONAL_CONDITIONS} "
        "UNION "
        "SELECT h.id "
        "FROM moz_places h "
        "JOIN moz_historyvisits_temp v ON v.place_id = h.id "
        "WHERE h.hidden <> 1 AND h.rev_host = '.' "
          "AND h.visit_count > 0 "
          "AND h.url BETWEEN 'file://' AND 'file:/~' "
          "{QUERY_OPTIONS_VISITS} {QUERY_OPTIONS_PLACES} "
          "{ADDITIONAL_CONDITIONS} "
        "UNION "
        "SELECT h.id "
        "FROM moz_places_temp h "
        "JOIN moz_historyvisits_temp v ON v.place_id = h.id "
        "WHERE h.hidden <> 1 AND h.rev_host = '.' "
          "AND h.visit_count > 0 "
          "AND h.url BETWEEN 'file://' AND 'file:/~' "
          "{QUERY_OPTIONS_VISITS}  {QUERY_OPTIONS_PLACES} "
          "{ADDITIONAL_CONDITIONS} "        
      ") "
      "UNION ALL "
      "SELECT DISTINCT null, "
             "'place:type=%ld&sort=%ld&domain='||host||'&domainIsHost=true"
               "&beginTime='||:begin_time||'&endTime='||:end_time, "
             "host, host, null, null, null, null, null, null, null "
      "FROM ( "
        "SELECT DISTINCT get_unreversed_host(rev_host) AS host "
        "FROM moz_places h "
        "JOIN moz_historyvisits v ON v.place_id = h.id "
        "WHERE h.rev_host <> '.' "
          "AND h.visit_count > 0 "
          "{QUERY_OPTIONS_VISITS} {QUERY_OPTIONS_PLACES} "
          "{ADDITIONAL_CONDITIONS} "
        "UNION "
        "SELECT DISTINCT get_unreversed_host(rev_host) AS host "
        "FROM moz_places_temp h "
        "JOIN moz_historyvisits v ON v.place_id = h.id "
        "WHERE h.rev_host <> '.' "
          "AND h.visit_count > 0 "
          "{QUERY_OPTIONS_VISITS} {QUERY_OPTIONS_PLACES} "
          "{ADDITIONAL_CONDITIONS} "
        "UNION "
        "SELECT DISTINCT get_unreversed_host(rev_host) AS host "
        "FROM moz_places h "
        "JOIN moz_historyvisits_temp v ON v.place_id = h.id "
        "WHERE h.rev_host <> '.' "
          "AND h.visit_count > 0 "
          "{QUERY_OPTIONS_VISITS} {QUERY_OPTIONS_PLACES} "
          "{ADDITIONAL_CONDITIONS} "
        "UNION "
        "SELECT DISTINCT get_unreversed_host(rev_host) AS host "
        "FROM moz_places_temp h "
        "JOIN moz_historyvisits_temp v ON v.place_id = h.id "        
        "WHERE h.rev_host <> '.' "
          "AND h.visit_count > 0 "
          "{QUERY_OPTIONS_VISITS} {QUERY_OPTIONS_PLACES} "
          "{ADDITIONAL_CONDITIONS} "        
        "ORDER BY 1 ASC "
      ") ",
      nsINavHistoryQueryOptions::RESULTS_AS_URI,
      mSortingMode,
      nsINavHistoryQueryOptions::RESULTS_AS_URI,
      mSortingMode);
  }

  return NS_OK;
}

nsresult
PlacesSQLQueryBuilder::SelectAsTag()
{
  nsNavHistory *history = nsNavHistory::GetHistoryService();
  NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);

  // This allows sorting by date fields what is not possible with
  // other history queries.
  mHasDateColumns = PR_TRUE; 

  mQueryString = nsPrintfCString(2048,
    "SELECT null, 'place:folder=' || id || '&queryType=%d&type=%ld', "
      "title, null, null, null, null, null, null, dateAdded, lastModified, "
      "null, null "
    "FROM   moz_bookmarks "
    "WHERE  parent = %lld",
    nsINavHistoryQueryOptions::QUERY_TYPE_BOOKMARKS,
    nsINavHistoryQueryOptions::RESULTS_AS_TAG_CONTENTS,
    history->GetTagsFolder());

  return NS_OK;
}

nsresult
PlacesSQLQueryBuilder::Where()
{

  // Set query options
  nsCAutoString additionalVisitsConditions;
  nsCAutoString additionalPlacesConditions;

  if (mRedirectsMode == nsINavHistoryQueryOptions::REDIRECTS_MODE_SOURCE) {
    additionalVisitsConditions += NS_LITERAL_CSTRING(
      "AND visit_type NOT IN ") +
      nsPrintfCString("(%d,%d) ", nsINavHistoryService::TRANSITION_REDIRECT_PERMANENT,
                                  nsINavHistoryService::TRANSITION_REDIRECT_TEMPORARY);
  }
  else if (mRedirectsMode == nsINavHistoryQueryOptions::REDIRECTS_MODE_TARGET) {
    additionalVisitsConditions += NS_LITERAL_CSTRING(
      "AND NOT EXISTS ( "
        "SELECT id FROM moz_historyvisits_temp WHERE from_visit = v.id "
        "AND visit_type IN ") +
        nsPrintfCString("(%d,%d) ", nsINavHistoryService::TRANSITION_REDIRECT_PERMANENT,
                                    nsINavHistoryService::TRANSITION_REDIRECT_TEMPORARY) +
      NS_LITERAL_CSTRING(") AND NOT EXISTS ( "
        "SELECT id FROM moz_historyvisits WHERE from_visit = v.id "
        "AND visit_type IN ") +
        nsPrintfCString("(%d,%d) ", nsINavHistoryService::TRANSITION_REDIRECT_PERMANENT,
                                    nsINavHistoryService::TRANSITION_REDIRECT_TEMPORARY) +
      NS_LITERAL_CSTRING(") ");
  }

  if (!mIncludeHidden) {
    additionalVisitsConditions += NS_LITERAL_CSTRING(
      "AND visit_type NOT IN ") +
      nsPrintfCString("(0,%d) ", nsINavHistoryService::TRANSITION_EMBED);
    additionalPlacesConditions += NS_LITERAL_CSTRING(
      "AND hidden <> 1 ");
  }

  mQueryString.ReplaceSubstring("{QUERY_OPTIONS_VISITS}",
                                additionalVisitsConditions.get());
  mQueryString.ReplaceSubstring("{QUERY_OPTIONS_PLACES}",
                                additionalPlacesConditions.get());

  // If we used WHERE already, we inject the conditions 
  // in place of {ADDITIONAL_CONDITIONS}
  PRInt32 useInnerCondition;
  useInnerCondition = mQueryString.Find("{ADDITIONAL_CONDITIONS}", 0);
  if (useInnerCondition != kNotFound) {

    nsCAutoString innerCondition;
    // If we have condition AND it
    if (!mConditions.IsEmpty()) {
      innerCondition = " AND (";
      innerCondition += mConditions;
      innerCondition += ")";
    }
    mQueryString.ReplaceSubstring("{ADDITIONAL_CONDITIONS}",
                                  innerCondition.get());

  } else if (!mConditions.IsEmpty()) {

    mQueryString += "WHERE ";
    mQueryString += mConditions;

  }
  return NS_OK;
}

nsresult
PlacesSQLQueryBuilder::GroupBy()
{
  mQueryString += mGroupBy;
  return NS_OK;
}

nsresult
PlacesSQLQueryBuilder::OrderBy()
{
  if (mSkipOrderBy)
    return NS_OK;

  // Sort clause: we will sort later, but if it comes out of the DB sorted,
  // our later sort will be basically free. The DB can sort these for free
  // most of the time anyway, because it has indices over these items.
  switch(mSortingMode)
  {
    case nsINavHistoryQueryOptions::SORT_BY_NONE:
      // If this is an URI query the sorting could change based on the
      // sync status of disk and temp tables, we must ensure sorting does not
      // change between queries.
      if (mResultType == nsINavHistoryQueryOptions::RESULTS_AS_URI) {
        if (mQueryType == nsINavHistoryQueryOptions::QUERY_TYPE_BOOKMARKS)
          mQueryString += NS_LITERAL_CSTRING(" ORDER BY b.id ASC ");
        else if (mQueryType == nsINavHistoryQueryOptions::QUERY_TYPE_HISTORY)
          mQueryString += NS_LITERAL_CSTRING(" ORDER BY h.id ASC ");
      }
      break;
    case nsINavHistoryQueryOptions::SORT_BY_TITLE_ASCENDING:
    case nsINavHistoryQueryOptions::SORT_BY_TITLE_DESCENDING:
      // If the user wants few results, we limit them by date, necessitating
      // a sort by date here (see the IDL definition for maxResults).
      // Otherwise we will do actual sorting by title, but since we could need
      // to special sort for some locale we will repeat a second sorting at the
      // end in nsNavHistoryResult, that should be faster since the list will be
      // almost ordered.
      if (mMaxResults > 0)
        OrderByColumnIndexDesc(nsNavHistory::kGetInfoIndex_VisitDate);
      else if (mSortingMode == nsINavHistoryQueryOptions::SORT_BY_TITLE_ASCENDING)
        OrderByTextColumnIndexAsc(nsNavHistory::kGetInfoIndex_Title);
      else
        OrderByTextColumnIndexDesc(nsNavHistory::kGetInfoIndex_Title);
      break;
    case nsINavHistoryQueryOptions::SORT_BY_DATE_ASCENDING:
      OrderByColumnIndexAsc(nsNavHistory::kGetInfoIndex_VisitDate);
      break;
    case nsINavHistoryQueryOptions::SORT_BY_DATE_DESCENDING:
      OrderByColumnIndexDesc(nsNavHistory::kGetInfoIndex_VisitDate);
      break;
    case nsINavHistoryQueryOptions::SORT_BY_URI_ASCENDING:
      OrderByColumnIndexAsc(nsNavHistory::kGetInfoIndex_URL);
      break;
    case nsINavHistoryQueryOptions::SORT_BY_URI_DESCENDING:
      OrderByColumnIndexDesc(nsNavHistory::kGetInfoIndex_URL);
      break;
    case nsINavHistoryQueryOptions::SORT_BY_VISITCOUNT_ASCENDING:
      OrderByColumnIndexAsc(nsNavHistory::kGetInfoIndex_VisitCount);
      break;
    case nsINavHistoryQueryOptions::SORT_BY_VISITCOUNT_DESCENDING:
      OrderByColumnIndexDesc(nsNavHistory::kGetInfoIndex_VisitCount);
      break;
    case nsINavHistoryQueryOptions::SORT_BY_DATEADDED_ASCENDING:
      if (mHasDateColumns)
        OrderByColumnIndexAsc(nsNavHistory::kGetInfoIndex_ItemDateAdded);
      break;
    case nsINavHistoryQueryOptions::SORT_BY_DATEADDED_DESCENDING:
      if (mHasDateColumns)
        OrderByColumnIndexDesc(nsNavHistory::kGetInfoIndex_ItemDateAdded);
      break;
    case nsINavHistoryQueryOptions::SORT_BY_LASTMODIFIED_ASCENDING:
      if (mHasDateColumns)
        OrderByColumnIndexAsc(nsNavHistory::kGetInfoIndex_ItemLastModified);
      break;
    case nsINavHistoryQueryOptions::SORT_BY_LASTMODIFIED_DESCENDING:
      if (mHasDateColumns)
        OrderByColumnIndexDesc(nsNavHistory::kGetInfoIndex_ItemLastModified);
      break;
    case nsINavHistoryQueryOptions::SORT_BY_TAGS_ASCENDING:
    case nsINavHistoryQueryOptions::SORT_BY_TAGS_DESCENDING:
    case nsINavHistoryQueryOptions::SORT_BY_ANNOTATION_ASCENDING:
    case nsINavHistoryQueryOptions::SORT_BY_ANNOTATION_DESCENDING:
      break; // Sort later in nsNavHistoryQueryResultNode::FillChildren()
    default:
      NS_NOTREACHED("Invalid sorting mode");
  }
  return NS_OK;
}

void PlacesSQLQueryBuilder::OrderByColumnIndexAsc(PRInt32 aIndex)
{
  mQueryString += nsPrintfCString(128, " ORDER BY %d ASC", aIndex+1);
}

void PlacesSQLQueryBuilder::OrderByColumnIndexDesc(PRInt32 aIndex)
{
  mQueryString += nsPrintfCString(128, " ORDER BY %d DESC", aIndex+1);
}

void PlacesSQLQueryBuilder::OrderByTextColumnIndexAsc(PRInt32 aIndex)
{
  mQueryString += nsPrintfCString(128, " ORDER BY %d COLLATE NOCASE ASC",
                                  aIndex+1);
}

void PlacesSQLQueryBuilder::OrderByTextColumnIndexDesc(PRInt32 aIndex)
{
  mQueryString += nsPrintfCString(128, " ORDER BY %d COLLATE NOCASE DESC",
                                  aIndex+1);
}

nsresult
PlacesSQLQueryBuilder::Limit()
{
  if (mUseLimit && mMaxResults > 0) {
    mQueryString += NS_LITERAL_CSTRING(" LIMIT ");
    mQueryString.AppendInt(mMaxResults);
    mQueryString.AppendLiteral(" ");
  }
  return NS_OK;
}

nsresult
nsNavHistory::ConstructQueryString(
    const nsCOMArray<nsNavHistoryQuery>& aQueries,
    nsNavHistoryQueryOptions* aOptions, 
    nsCString& queryString, 
    PRBool& aParamsPresent,
    nsNavHistory::StringHash& aAddParams)
{
  // For information about visit_type see nsINavHistoryService.idl.
  // visitType == 0 is undefined (see bug #375777 for details).
  // Some sites, especially Javascript-heavy ones, load things in frames to 
  // display them, resulting in a lot of these entries. This is the reason 
  // why such visits are filtered out.
  nsresult rv;
  aParamsPresent = PR_FALSE;

  PRInt32 sortingMode = aOptions->SortingMode();
  NS_ASSERTION(sortingMode >= nsINavHistoryQueryOptions::SORT_BY_NONE &&
               sortingMode <= nsINavHistoryQueryOptions::SORT_BY_ANNOTATION_DESCENDING,
               "Invalid sortingMode found while building query!");

  PRBool hasSearchTerms = PR_FALSE;
  for (PRInt32 i = 0; i < aQueries.Count() && !hasSearchTerms; i++) {
    aQueries[i]->GetHasSearchTerms(&hasSearchTerms);
  }

  nsCAutoString tagsSqlFragment;
  GetTagsSqlFragment(GetTagsFolder(),
                     NS_LITERAL_CSTRING("h.id"),
                     hasSearchTerms,
                     tagsSqlFragment);

  if (IsOptimizableHistoryQuery(aQueries, aOptions,
        nsINavHistoryQueryOptions::SORT_BY_DATE_DESCENDING) ||
      IsOptimizableHistoryQuery(aQueries, aOptions,
        nsINavHistoryQueryOptions::SORT_BY_VISITCOUNT_DESCENDING)) {
    // Generate an optimized query for the history menu and most visited
    // smart bookmark.
    queryString = NS_LITERAL_CSTRING(
      "SELECT h.id, h.url, h.title, h.rev_host, h.visit_count, h.last_visit_date, "
          "f.url, null, null, null, null, null, ") +
          tagsSqlFragment + NS_LITERAL_CSTRING(
        "FROM moz_places_temp h "
        "LEFT OUTER JOIN moz_favicons f ON h.favicon_id = f.id "
        "WHERE h.hidden <> 1 "
          "AND EXISTS (SELECT id FROM moz_historyvisits_temp WHERE place_id = h.id "
                       "AND visit_type NOT IN ") +
                       nsPrintfCString("(0,%d) ",
                                       nsINavHistoryService::TRANSITION_EMBED) +
                       NS_LITERAL_CSTRING("UNION ALL "
                       "SELECT id FROM moz_historyvisits WHERE place_id = h.id "
                       "AND visit_type NOT IN ") +
                       nsPrintfCString("(0,%d) ",
                                       nsINavHistoryService::TRANSITION_EMBED) +
                       NS_LITERAL_CSTRING("LIMIT 1) "
          "{QUERY_OPTIONS} "
      "UNION ALL "
      "SELECT h.id, h.url, h.title, h.rev_host, h.visit_count, h.last_visit_date, "
          "f.url, null, null, null, null, null, ") +
          tagsSqlFragment + NS_LITERAL_CSTRING(
        "FROM moz_places h "
        "LEFT OUTER JOIN moz_favicons f ON h.favicon_id = f.id "
        "WHERE h.hidden <> 1 "
          "AND h.id NOT IN (SELECT id FROM moz_places_temp) "
          "AND EXISTS (SELECT id FROM moz_historyvisits_temp WHERE place_id = h.id "
                       "AND visit_type NOT IN ") +
                       nsPrintfCString("(0,%d) ",
                                       nsINavHistoryService::TRANSITION_EMBED) +
                       NS_LITERAL_CSTRING("UNION ALL "
                       "SELECT id FROM moz_historyvisits WHERE place_id = h.id "
                       "AND visit_type NOT IN ") +
                       nsPrintfCString("(0,%d) ",
                                       nsINavHistoryService::TRANSITION_EMBED) +
                       NS_LITERAL_CSTRING("LIMIT 1) "
          "{QUERY_OPTIONS} "
        );

    queryString.Append(NS_LITERAL_CSTRING("ORDER BY "));
    if (sortingMode == nsINavHistoryQueryOptions::SORT_BY_DATE_DESCENDING)
      queryString.Append(NS_LITERAL_CSTRING("last_visit_date DESC "));
    else
      queryString.Append(NS_LITERAL_CSTRING("visit_count DESC "));

    queryString.Append(NS_LITERAL_CSTRING("LIMIT "));
    queryString.AppendInt(aOptions->MaxResults());

    nsCAutoString additionalQueryOptions;
    if (aOptions->RedirectsMode() ==
          nsINavHistoryQueryOptions::REDIRECTS_MODE_SOURCE) {
      additionalQueryOptions +=  nsPrintfCString(256,
        "AND NOT EXISTS ( "
          "SELECT id FROM moz_historyvisits_temp WHERE place_id = h.id "
                                             "AND visit_type IN (%d,%d)"
        ") "
        "AND NOT EXISTS ( "
          "SELECT id FROM moz_historyvisits WHERE place_id = h.id "
                                             "AND visit_type IN (%d,%d)"
        ") ",
        TRANSITION_REDIRECT_PERMANENT,
        TRANSITION_REDIRECT_TEMPORARY,
        TRANSITION_REDIRECT_PERMANENT,
        TRANSITION_REDIRECT_TEMPORARY);
    }
    else if (aOptions->RedirectsMode() ==
              nsINavHistoryQueryOptions::REDIRECTS_MODE_TARGET) {
      additionalQueryOptions += nsPrintfCString(1024,
        "AND NOT EXISTS ( "
          "SELECT id "
          "FROM moz_historyvisits_temp v "
          "WHERE place_id = h.id "
            "AND EXISTS(SELECT id FROM moz_historyvisits_temp "
                           "WHERE from_visit = v.id AND visit_type IN (%d,%d) "
                        "UNION ALL "
                        "SELECT id FROM moz_historyvisits "
                           "WHERE from_visit = v.id AND visit_type IN (%d,%d)) "
          "UNION ALL "
          "SELECT id "
          "FROM moz_historyvisits v "
          "WHERE place_id = h.id "
            "AND EXISTS(SELECT id FROM moz_historyvisits_temp "
                           "WHERE from_visit = v.id AND visit_type IN (%d,%d) "
                        "UNION ALL "
                        "SELECT id FROM moz_historyvisits "
                           "WHERE from_visit = v.id AND visit_type IN (%d,%d)) "
        ") ",
        TRANSITION_REDIRECT_PERMANENT,
        TRANSITION_REDIRECT_TEMPORARY,
        TRANSITION_REDIRECT_PERMANENT,
        TRANSITION_REDIRECT_TEMPORARY,
        TRANSITION_REDIRECT_PERMANENT,
        TRANSITION_REDIRECT_TEMPORARY,
        TRANSITION_REDIRECT_PERMANENT,
        TRANSITION_REDIRECT_TEMPORARY);
    }
    queryString.ReplaceSubstring("{QUERY_OPTIONS}",
                                  additionalQueryOptions.get());
    return NS_OK;
  }

  nsCAutoString conditions;
  for (PRInt32 i = 0; i < aQueries.Count(); i++) {
    nsCString queryClause;
    rv = QueryToSelectClause(aQueries[i], aOptions, i, &queryClause);
    NS_ENSURE_SUCCESS(rv, rv);
    if (! queryClause.IsEmpty()) {
      aParamsPresent = PR_TRUE;
      if (! conditions.IsEmpty()) // exists previous clause: multiple ones are ORed
        conditions += NS_LITERAL_CSTRING(" OR ");
      conditions += NS_LITERAL_CSTRING("(") + queryClause +
        NS_LITERAL_CSTRING(")");
    }
  }

  // Determine whether we can push maxResults constraints into the queries
  // as LIMIT, or if we need to do result count clamping later
  // using FilterResultSet()
  PRBool useLimitClause = !NeedToFilterResultSet(aQueries, aOptions);

  PlacesSQLQueryBuilder queryStringBuilder(conditions, aOptions,
                                           useLimitClause, aAddParams,
                                           hasSearchTerms);
  rv = queryStringBuilder.GetQueryString(queryString);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

PLDHashOperator BindAdditionalParameter(nsNavHistory::StringHash::KeyType aParamName, 
                                        nsCString aParamValue,
                                        void* aStatement)
{
  mozIStorageStatement* stmt = static_cast<mozIStorageStatement*>(aStatement);

  PRUint32 index;
  nsresult rv = stmt->GetParameterIndex(aParamName, &index);

  if (NS_FAILED(rv))
    return PL_DHASH_STOP;

  rv = stmt->BindUTF8StringParameter(index, aParamValue);
  if (NS_FAILED(rv))
    return PL_DHASH_STOP;

  return PL_DHASH_NEXT;
}

// nsNavHistory::GetQueryResults
//
//    Call this to get the results from a complex query. This is used by
//    nsNavHistoryQueryResultNode to populate its children. For simple bookmark
//    queries, use nsNavBookmarks::QueryFolderChildren.
//
//    THIS DOES NOT DO SORTING. You will need to sort the container yourself
//    when you get the results. This is because sorting depends on tree
//    statistics that will be built from the perspective of the tree. See
//    nsNavHistoryQueryResultNode::FillChildren
//
//    FIXME: This only does keyword searching for the first query, and does
//    it ANDed with the all the rest of the queries.

nsresult
nsNavHistory::GetQueryResults(nsNavHistoryQueryResultNode *aResultNode,
                              const nsCOMArray<nsNavHistoryQuery>& aQueries,
                              nsNavHistoryQueryOptions *aOptions,
                              nsCOMArray<nsNavHistoryResultNode>* aResults)
{
  NS_ENSURE_ARG_POINTER(aOptions);
  NS_ASSERTION(aResults->Count() == 0, "Initial result array must be empty");
  if (! aQueries.Count())
    return NS_ERROR_INVALID_ARG;

  nsCString queryString;
  PRBool paramsPresent = PR_FALSE;
  nsNavHistory::StringHash addParams;
  addParams.Init(DATE_CONT_NUM(mExpireDaysMax));
  nsresult rv = ConstructQueryString(aQueries, aOptions, queryString, 
                                     paramsPresent, addParams);
  NS_ENSURE_SUCCESS(rv,rv);

  // create statement
  nsCOMPtr<mozIStorageStatement> statement;
  rv = mDBConn->CreateStatement(queryString, getter_AddRefs(statement));
#ifdef DEBUG
  if (NS_FAILED(rv)) {
    nsCAutoString lastErrorString;
    (void)mDBConn->GetLastErrorString(lastErrorString);
    PRInt32 lastError = 0;
    (void)mDBConn->GetLastError(&lastError);
    printf("Places failed to create a statement from this query:\n%s\nStorage error (%d): %s\n",
           PromiseFlatCString(queryString).get(),
           lastError,
           PromiseFlatCString(lastErrorString).get());
  }
#endif
  NS_ENSURE_SUCCESS(rv, rv);

  if (paramsPresent) {
    // bind parameters
    PRInt32 i;
    for (i = 0; i < aQueries.Count(); i++) {
      rv = BindQueryClauseParameters(statement, i, aQueries[i], aOptions);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  addParams.EnumerateRead(BindAdditionalParameter, statement.get());

  // optimize the case where we just use the results as is
  // and we don't need to do any post-query filtering
  if (NeedToFilterResultSet(aQueries, aOptions)) {
    // generate the toplevel results
    nsCOMArray<nsNavHistoryResultNode> toplevel;
    rv = ResultsAsList(statement, aOptions, &toplevel);
    NS_ENSURE_SUCCESS(rv, rv);

    FilterResultSet(aResultNode, toplevel, aResults, aQueries, aOptions);
  } else {
    rv = ResultsAsList(statement, aOptions, aResults);
    NS_ENSURE_SUCCESS(rv, rv);
  } 

  return NS_OK;
}

// nsNavHistory::AddObserver

NS_IMETHODIMP
nsNavHistory::AddObserver(nsINavHistoryObserver* aObserver, PRBool aOwnsWeak)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aObserver);

  return mObservers.AppendWeakElement(aObserver, aOwnsWeak);
}


// nsNavHistory::RemoveObserver

NS_IMETHODIMP
nsNavHistory::RemoveObserver(nsINavHistoryObserver* aObserver)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aObserver);

  return mObservers.RemoveWeakElement(aObserver);
}

// nsNavHistory::BeginUpdateBatch
// See RunInBatchMode
nsresult
nsNavHistory::BeginUpdateBatch()
{
  if (mBatchLevel++ == 0) {
    PRBool transactionInProgress = PR_TRUE; // default to no transaction on err
    mDBConn->GetTransactionInProgress(&transactionInProgress);
    mBatchHasTransaction = ! transactionInProgress;
    if (mBatchHasTransaction)
      mDBConn->BeginTransaction();

    ENUMERATE_OBSERVERS(mCanNotify, mCacheObservers, mObservers, nsINavHistoryObserver,
                        OnBeginUpdateBatch());
  }
  return NS_OK;
}

// nsNavHistory::EndUpdateBatch
nsresult
nsNavHistory::EndUpdateBatch()
{
  if (--mBatchLevel == 0) {
    if (mBatchHasTransaction)
      mDBConn->CommitTransaction();
    mBatchHasTransaction = PR_FALSE;
    ENUMERATE_OBSERVERS(mCanNotify, mCacheObservers, mObservers, nsINavHistoryObserver,
                        OnEndUpdateBatch());
  }
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistory::RunInBatchMode(nsINavHistoryBatchCallback* aCallback,
                             nsISupports* aUserData)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aCallback);

  UpdateBatchScoper batch(*this);
  return aCallback->RunBatched(aUserData);
}

NS_IMETHODIMP
nsNavHistory::GetHistoryDisabled(PRBool *_retval)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG_POINTER(_retval);

  *_retval = IsHistoryDisabled();
  return NS_OK;
}

// Browser history *************************************************************


// nsNavHistory::AddPageWithDetails
//
//    This function is used by the migration components to import history.
//
//    Note that this always adds the page with one visit and no parent, which
//    is appropriate for imported URIs.

NS_IMETHODIMP
nsNavHistory::AddPageWithDetails(nsIURI *aURI, const PRUnichar *aTitle,
                                 PRInt64 aLastVisited)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aURI);

  // Don't update the page title inside the private browsing mode.
  if (InPrivateBrowsingMode())
    return NS_OK;

  PRInt64 visitID;
  nsresult rv = AddVisit(aURI, aLastVisited, 0, TRANSITION_LINK, PR_FALSE,
                         0, &visitID);
  NS_ENSURE_SUCCESS(rv, rv);

  return SetPageTitleInternal(aURI, nsString(aTitle));
}


// nsNavHistory::GetLastPageVisited
//
//    This was once used when the new window is set to "previous page." It
//    doesn't seem to be used anymore, so we don't spend any time precompiling
//    the statement.

NS_IMETHODIMP
nsNavHistory::GetLastPageVisited(nsACString & aLastPageVisited)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");

  nsCOMPtr<mozIStorageStatement> statement;
  // We are not checking for duplicated ids into the unified table
  // for perf reasons, LIMIT 1 will discard duplicates faster since we
  // expect newest visits being in temp table.
  nsresult rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT url, visit_date FROM moz_historyvisits_temp v "
      "JOIN moz_places_temp h ON v.place_id = h.id "
      "WHERE h.hidden <> 1 "
      "UNION ALL "
      "SELECT url, visit_date FROM moz_historyvisits_temp v "
      "JOIN moz_places h ON v.place_id = h.id "
      "WHERE h.hidden <> 1 "
      "UNION ALL "
      "SELECT url, visit_date FROM moz_historyvisits v "
      "JOIN moz_places_temp h ON v.place_id = h.id "
      "WHERE h.hidden <> 1 "
      "UNION ALL "
      "SELECT url, visit_date FROM moz_historyvisits v "
      "JOIN moz_places h ON v.place_id = h.id "
      "WHERE h.hidden <> 1 "
      "ORDER BY visit_date DESC "
      "LIMIT 1 "),
    getter_AddRefs(statement));
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool hasMatch = PR_FALSE;
  if (NS_SUCCEEDED(statement->ExecuteStep(&hasMatch)) && hasMatch)
    return statement->GetUTF8String(0, aLastPageVisited);

  aLastPageVisited.Truncate(0);
  return NS_OK;
}


// nsNavHistory::GetCount
//
//    This function is used in legacy code to see if there is any history to
//    clear. Counting the actual number of history entries is very slow, so
//    we just see if there are any and return 0 or 1, which is enough to make
//    all the code that uses this function happy.

NS_IMETHODIMP
nsNavHistory::GetCount(PRUint32 *aCount)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG_POINTER(aCount);

  PRBool hasEntries = PR_FALSE;
  nsresult rv = GetHasHistoryEntries(&hasEntries);
  if (hasEntries)
    *aCount = 1;
  else
    *aCount = 0;
  return rv;
}


// nsNavHistory::RemovePagesInternal
//
//    Deletes a list of placeIds from history.
//    This is an internal method used by RemovePages, RemovePagesFromHost and
//    RemovePagesByTimeframe.
//    Takes a comma separated list of place ids.
//    This method does not do any observer notification.

nsresult
nsNavHistory::RemovePagesInternal(const nsCString& aPlaceIdsQueryString)
{
  // early return if there is nothing to delete
  if (aPlaceIdsQueryString.IsEmpty())
    return NS_OK;

  mozStorageTransaction transaction(mDBConn, PR_FALSE);

  nsresult rv = PreparePlacesForVisitsDelete(aPlaceIdsQueryString);
  NS_ENSURE_SUCCESS(rv, rv);

  // delete all visits
  rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "DELETE FROM moz_historyvisits_view WHERE place_id IN (") +
        aPlaceIdsQueryString +
        NS_LITERAL_CSTRING(")"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = CleanupPlacesOnVisitsDelete(aPlaceIdsQueryString);
  NS_ENSURE_SUCCESS(rv, rv);

  return transaction.Commit();
}


/**
 * Prepares for deletion places that are about to have all their visits removed.
 * This is an internal method used by RemovePagesInternal and
 * RemoveVisitsByTimeframe.  This method does not execute in a transaction, so
 * callers should make sure they begin one if needed.
 *
 * @param aPlaceIdsQueryString
 *        A comma-separated list of place IDs, each of which is about to have
 *        all its visits removed
 */
nsresult
nsNavHistory::PreparePlacesForVisitsDelete(const nsCString& aPlaceIdsQueryString)
{
  // Return early if there is nothing to delete.
  if (aPlaceIdsQueryString.IsEmpty())
    return NS_OK;

  // if a moz_place is annotated or was a bookmark,
  // we won't delete it, but we will delete the moz_visits
  // so we need to reset the frecency.  Note, we set frecency to
  // -visit_count, as we use that value in our "on idle" query
  // to figure out which places to recalculate frecency first.
  // Pay attention to not set frecency = 0 if visit_count = 0
  nsresult rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "UPDATE moz_places_view "
      "SET frecency = -MAX(visit_count, 1) "
      "WHERE id IN ( "
        "SELECT h.id " 
        "FROM moz_places_temp h "
        "WHERE h.id IN ( ") + aPlaceIdsQueryString + NS_LITERAL_CSTRING(") "
          "AND ( "
            "EXISTS (SELECT b.id FROM moz_bookmarks b WHERE b.fk =h.id) "
            "OR EXISTS (SELECT a.id FROM moz_annos a WHERE a.place_id = h.id) "
          ") "
        "UNION ALL "
        "SELECT h.id " 
        "FROM moz_places h "
        "WHERE h.id IN ( ") + aPlaceIdsQueryString + NS_LITERAL_CSTRING(") "
          "AND h.id NOT IN (SELECT id FROM moz_places_temp) "
          "AND ( "
            "EXISTS (SELECT b.id FROM moz_bookmarks b WHERE b.fk =h.id) "
            "OR EXISTS (SELECT a.id FROM moz_annos a WHERE a.place_id = h.id) "
          ") "        
      ")"));
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


/**
 * Performs cleanup on places that just had all their visits removed, including
 * deletion of those places.  This is an internal method used by
 * RemovePagesInternal and RemoveVisitsByTimeframe.  This method does not
 * execute in a transaction, so callers should make sure they begin one if
 * needed.
 *
 * @param aPlaceIdsQueryString
 *        A comma-separated list of place IDs, each of which just had all its
 *        visits removed
 */
nsresult
nsNavHistory::CleanupPlacesOnVisitsDelete(const nsCString& aPlaceIdsQueryString)
{
  // Return early if there is nothing to delete.
  if (aPlaceIdsQueryString.IsEmpty())
    return NS_OK;

  // now that visits have been removed, run annotation expiration.
  // this will remove all expire-able annotations for these URIs.
  (void)mExpire->OnDeleteVisits();

  // if the entry is not bookmarked and is not a place: uri
  // then we can remove it from moz_places.
  // Note that we do NOT delete favicons. Any unreferenced favicons will be
  // deleted next time the browser is shut down.
  nsresult rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "DELETE FROM moz_places_view WHERE id IN ("
        "SELECT h.id FROM moz_places_temp h "
        "WHERE h.id IN ( ") + aPlaceIdsQueryString + NS_LITERAL_CSTRING(") "
          "AND SUBSTR(h.url, 1, 6) <> 'place:' "
          "AND NOT EXISTS "
            "(SELECT b.id FROM moz_bookmarks b WHERE b.fk = h.id LIMIT 1) "
        "UNION ALL "
        "SELECT h.id FROM moz_places h "
        "WHERE h.id NOT IN (SELECT id FROM moz_places_temp) "
          "AND h.id IN ( ") + aPlaceIdsQueryString + NS_LITERAL_CSTRING(") "
          "AND SUBSTR(h.url, 1, 6) <> 'place:' "
          "AND NOT EXISTS "
            "(SELECT b.id FROM moz_bookmarks b WHERE b.fk = h.id LIMIT 1) "
    ")"));
  NS_ENSURE_SUCCESS(rv, rv);

  // If we have removed all visits to a livemark's child, we need to fix its
  // frecency, or it would appear in the url bar autocomplete.
  // XXX this might be dog slow, further degrading delete perf.
  rv = FixInvalidFrecenciesForExcludedPlaces();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


// nsNavHistory::RemovePages
//
//    Removes a bunch of uris from history.
//    Has better performance than RemovePage when deleting a lot of history.
//    Notice that this function does not call the onDeleteURI observers,
//    instead, if aDoBatchNotify is true, we call OnBegin/EndUpdateBatch.
//    We don't do duplicates removal, URIs array should be cleaned-up before.

NS_IMETHODIMP
nsNavHistory::RemovePages(nsIURI **aURIs, PRUint32 aLength, PRBool aDoBatchNotify)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aURIs);

#ifdef LAZY_ADD
  // We must ensure to remove pages from the lazy messages queue too.
  CommitLazyMessages();
#endif

  nsresult rv;
  // build a list of place ids to delete
  nsCString deletePlaceIdsQueryString;
  for (PRUint32 i = 0; i < aLength; i++) {
    PRInt64 placeId;
    rv = GetUrlIdFor(aURIs[i], &placeId, PR_FALSE);
    NS_ENSURE_SUCCESS(rv, rv);
    if (placeId != 0) {
      if (!deletePlaceIdsQueryString.IsEmpty())
        deletePlaceIdsQueryString.AppendLiteral(",");
      deletePlaceIdsQueryString.AppendInt(placeId);
    }
  }

  rv = RemovePagesInternal(deletePlaceIdsQueryString);
  NS_ENSURE_SUCCESS(rv, rv);

  // force a full refresh calling onEndUpdateBatch (will call Refresh())
  if (aDoBatchNotify)
    UpdateBatchScoper batch(*this); // sends Begin/EndUpdateBatch to observers

  return NS_OK;
}


// nsNavHistory::RemovePage
//
//    Removes all visits and the main history entry for the given URI.
//    Silently fails if we have no knowledge of the page.

NS_IMETHODIMP
nsNavHistory::RemovePage(nsIURI *aURI)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aURI);

  // Before we remove, we have to notify our observers!
  ENUMERATE_OBSERVERS(mCanNotify, mCacheObservers, mObservers,
                      nsINavHistoryObserver, OnBeforeDeleteURI(aURI));

  nsIURI** URIs = &aURI;
  nsresult rv = RemovePages(URIs, 1, PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);

  // Notify our observers that the URI has been removed.
  ENUMERATE_OBSERVERS(mCanNotify, mCacheObservers, mObservers,
                      nsINavHistoryObserver, OnDeleteURI(aURI));
  return NS_OK;
}


// nsNavHistory::RemovePagesFromHost
//
//    This function will delete all history information about pages from a
//    given host. If aEntireDomain is set, we will also delete pages from
//    sub hosts (so if we are passed in "microsoft.com" we delete
//    "www.microsoft.com", "msdn.microsoft.com", etc.). An empty host name
//    means local files and anything else with no host name. You can also pass
//    in the localized "(local files)" title given to you from a history query.
//
//    Silently fails if we have no knowledge of the host.
//
//    This sends onBeginUpdateBatch/onEndUpdateBatch to observers

NS_IMETHODIMP
nsNavHistory::RemovePagesFromHost(const nsACString& aHost, PRBool aEntireDomain)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");

#ifdef LAZY_ADD
  // We must ensure to remove pages from the lazy messages queue too.
  CommitLazyMessages();
#endif

  nsresult rv;
  // Local files don't have any host name. We don't want to delete all files in
  // history when we get passed an empty string, so force to exact match
  if (aHost.IsEmpty())
    aEntireDomain = PR_FALSE;

  // translate "(local files)" to an empty host name
  // be sure to use the TitleForDomain to get the localized name
  nsCString localFiles;
  TitleForDomain(EmptyCString(), localFiles);
  nsAutoString host16;
  if (!aHost.Equals(localFiles))
    CopyUTF8toUTF16(aHost, host16);

  // nsISupports version of the host string for passing to observers
  nsCOMPtr<nsISupportsString> hostSupports(do_CreateInstance(NS_SUPPORTS_STRING_CONTRACTID, &rv));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = hostSupports->SetData(host16);
  NS_ENSURE_SUCCESS(rv, rv);

  // see BindQueryClauseParameters for how this host selection works
  nsAutoString revHostDot;
  GetReversedHostname(host16, revHostDot);
  NS_ASSERTION(revHostDot[revHostDot.Length() - 1] == '.', "Invalid rev. host");
  nsAutoString revHostSlash(revHostDot);
  revHostSlash.Truncate(revHostSlash.Length() - 1);
  revHostSlash.Append(NS_LITERAL_STRING("/"));

  // build condition string based on host selection type
  nsCAutoString conditionString;
  if (aEntireDomain)
    conditionString.AssignLiteral("rev_host >= ?1 AND rev_host < ?2 ");
  else
    conditionString.AssignLiteral("rev_host = ?1 ");

  nsCOMPtr<mozIStorageStatement> statement;

  // create statement depending on delete type
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT id FROM moz_places_temp "
      "WHERE ") + conditionString + NS_LITERAL_CSTRING(
      "UNION ALL "
      "SELECT id FROM moz_places "
      "WHERE id NOT IN (SELECT id FROM moz_places_temp) "
        "AND ") + conditionString,
    getter_AddRefs(statement));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = statement->BindStringParameter(0, revHostDot);
  NS_ENSURE_SUCCESS(rv, rv);
  if (aEntireDomain) {
    rv = statement->BindStringParameter(1, revHostSlash);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsCString hostPlaceIds;
  PRBool hasMore = PR_FALSE;
  while (NS_SUCCEEDED(statement->ExecuteStep(&hasMore)) && hasMore) {
    if (!hostPlaceIds.IsEmpty())
      hostPlaceIds.AppendLiteral(",");
    PRInt64 placeId;
    rv = statement->GetInt64(0, &placeId);
    NS_ENSURE_SUCCESS(rv, rv);
    hostPlaceIds.AppendInt(placeId);
  }

  // force a full refresh calling onEndUpdateBatch (will call Refresh())
  UpdateBatchScoper batch(*this); // sends Begin/EndUpdateBatch to observers

  rv = RemovePagesInternal(hostPlaceIds);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


// nsNavHistory::RemovePagesByTimeframe
//
//    This function will delete all history information about
//    pages for a given timeframe.
//    Limits are included: aBeginTime <= timeframe <= aEndTime
//
//    This method sends onBeginUpdateBatch/onEndUpdateBatch to observers

NS_IMETHODIMP
nsNavHistory::RemovePagesByTimeframe(PRTime aBeginTime, PRTime aEndTime)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");

#ifdef LAZY_ADD
  // We must ensure to remove pages from the lazy messages queue too.
  CommitLazyMessages();
#endif

  nsresult rv;
  // build a list of place ids to delete
  nsCString deletePlaceIdsQueryString;

  // we only need to know if a place has a visit into the given timeframe
  // this query is faster than actually selecting in moz_historyvisits
  nsCOMPtr<mozIStorageStatement> selectByTime;
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT h.id FROM moz_places_temp h WHERE "
        "EXISTS "
          "(SELECT id FROM moz_historyvisits v WHERE v.place_id = h.id "
            "AND v.visit_date >= ?1 AND v.visit_date <= ?2 LIMIT 1)"
        "OR EXISTS "
          "(SELECT id FROM moz_historyvisits_temp v WHERE v.place_id = h.id "
            "AND v.visit_date >= ?1 AND v.visit_date <= ?2 LIMIT 1) "
      "UNION "
      "SELECT h.id FROM moz_places h WHERE "
        "EXISTS "
          "(SELECT id FROM moz_historyvisits v WHERE v.place_id = h.id "
            "AND v.visit_date >= ?1 AND v.visit_date <= ?2 LIMIT 1)"
        "OR EXISTS "
          "(SELECT id FROM moz_historyvisits_temp v WHERE v.place_id = h.id "
            "AND v.visit_date >= ?1 AND v.visit_date <= ?2 LIMIT 1)"),
    getter_AddRefs(selectByTime));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = selectByTime->BindInt64Parameter(0, aBeginTime);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = selectByTime->BindInt64Parameter(1, aEndTime);
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool hasMore = PR_FALSE;
  while (NS_SUCCEEDED(selectByTime->ExecuteStep(&hasMore)) && hasMore) {
    PRInt64 placeId;
    rv = selectByTime->GetInt64(0, &placeId);
    NS_ENSURE_SUCCESS(rv, rv);
    if (placeId != 0) {
      if (!deletePlaceIdsQueryString.IsEmpty())
        deletePlaceIdsQueryString.AppendLiteral(",");
      deletePlaceIdsQueryString.AppendInt(placeId);
    }
  }

  rv = RemovePagesInternal(deletePlaceIdsQueryString);
  NS_ENSURE_SUCCESS(rv, rv);

  // force a full refresh calling onEndUpdateBatch (will call Refresh())
  UpdateBatchScoper batch(*this); // sends Begin/EndUpdateBatch to observers

  return NS_OK;
}


/**
 * Removes all visits in a given timeframe.  Limits are included:
 * aBeginTime <= timeframe <= aEndTime.  Any place that becomes unvisited
 * as a result will also be deleted.
 *
 * Note that removal is performed in batch, so observers will not be
 * notified of individual places that are deleted.  Instead they will be
 * notified onBeginUpdateBatch and onEndUpdateBatch.
 *
 * @param aBeginTime
 *        The start of the timeframe, inclusive
 * @param aEndTime
 *        The end of the timeframe, inclusive
 */
NS_IMETHODIMP
nsNavHistory::RemoveVisitsByTimeframe(PRTime aBeginTime, PRTime aEndTime)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");

#ifdef LAZY_ADD
  // We must ensure to remove pages from the lazy messages queue too.
  CommitLazyMessages();
#endif

  nsresult rv;

  // Build a list of place IDs whose visits fall entirely within the timespan.
  // These places will be deleted by the call to CleanupPlacesOnVisitsDelete
  // below.
  nsCString deletePlaceIdsQueryString;
  {
    nsCOMPtr<mozIStorageStatement> selectByTime;
    mozStorageStatementScoper scope(selectByTime);
    rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
        "SELECT place_id "
        "FROM moz_historyvisits_temp "
        "WHERE ?1 <= visit_date AND visit_date <= ?2 "
        "UNION "
        "SELECT place_id "
        "FROM moz_historyvisits "
        "WHERE ?1 <= visit_date AND visit_date <= ?2 "
        "EXCEPT "
        "SELECT place_id "
        "FROM moz_historyvisits_temp "
        "WHERE visit_date < ?1 OR ?2 < visit_date "
        "EXCEPT "
        "SELECT place_id "
        "FROM moz_historyvisits "
        "WHERE visit_date < ?1 OR ?2 < visit_date"),
      getter_AddRefs(selectByTime));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = selectByTime->BindInt64Parameter(0, aBeginTime);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = selectByTime->BindInt64Parameter(1, aEndTime);
    NS_ENSURE_SUCCESS(rv, rv);

    PRBool hasMore = PR_FALSE;
    while (NS_SUCCEEDED(selectByTime->ExecuteStep(&hasMore)) && hasMore) {
      PRInt64 placeId;
      rv = selectByTime->GetInt64(0, &placeId);
      NS_ENSURE_SUCCESS(rv, rv);
      // placeId should not be <= 0, but be defensive.
      if (placeId > 0) {
        if (!deletePlaceIdsQueryString.IsEmpty())
          deletePlaceIdsQueryString.AppendLiteral(",");
        deletePlaceIdsQueryString.AppendInt(placeId);
      }
    }
  }

  // force a full refresh calling onEndUpdateBatch (will call Refresh())
  UpdateBatchScoper batch(*this); // sends Begin/EndUpdateBatch to observers

  mozStorageTransaction transaction(mDBConn, PR_FALSE);

  rv = PreparePlacesForVisitsDelete(deletePlaceIdsQueryString);
  NS_ENSURE_SUCCESS(rv, rv);

  // Delete all visits within the timeframe.
  nsCOMPtr<mozIStorageStatement> deleteVisitsStmt;
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "DELETE FROM moz_historyvisits_view "
      "WHERE ?1 <= visit_date AND visit_date <= ?2"),
    getter_AddRefs(deleteVisitsStmt));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = deleteVisitsStmt->BindInt64Parameter(0, aBeginTime);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = deleteVisitsStmt->BindInt64Parameter(1, aEndTime);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = deleteVisitsStmt->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = CleanupPlacesOnVisitsDelete(deletePlaceIdsQueryString);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = transaction.Commit();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


// nsNavHistory::RemoveAllPages
//
//    This function is used to clear history.

NS_IMETHODIMP
nsNavHistory::RemoveAllPages()
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");

#ifdef LAZY_ADD
  // We must ensure to remove pages from the lazy messages queue too.
  CommitLazyMessages();
#endif

  // expire everything
  mExpire->ClearHistory();

  // Compress DB. Currently commented out because compression is very slow.
  // Deleted data will be overwritten with 0s by sqlite.
#if 0
  nsresult rv = mDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING("VACUUM"));
  NS_ENSURE_SUCCESS(rv, rv);
#endif

  // privacy cleanup, if there's an old history.dat around, just delete it
  nsCOMPtr<nsIFile> oldHistoryFile;
  nsresult rv = NS_GetSpecialDirectory(NS_APP_HISTORY_50_FILE,
                                       getter_AddRefs(oldHistoryFile));
  if (NS_FAILED(rv)) return rv;

  PRBool fileExists;
  if (NS_SUCCEEDED(oldHistoryFile->Exists(&fileExists)) && fileExists) {
    rv = oldHistoryFile->Remove(PR_FALSE);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}


// nsNavHistory::HidePage
//
//    Sets the 'hidden' column to true. If we've not heard of the page, we
//    succeed and do nothing.

NS_IMETHODIMP
nsNavHistory::HidePage(nsIURI *aURI)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aURI);

  return NS_ERROR_NOT_IMPLEMENTED;
  /*
  // for speed to save disk accesses
  mozStorageTransaction transaction(mDBConn, PR_FALSE,
                                  mozIStorageConnection::TRANSACTION_EXCLUSIVE);

  // We need to do a query anyway to see if this URL is already in the DB.
  // Might as well ask for the hidden column to save updates in some cases.
  nsCOMPtr<mozIStorageStatement> dbSelectStatement;
  nsresult rv = mDBConn->CreateStatement(
      NS_LITERAL_CSTRING("SELECT id,hidden FROM moz_places WHERE url = ?1"),
      getter_AddRefs(dbSelectStatement));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = BindStatementURI(dbSelectStatement, 0, aURI);
  NS_ENSURE_SUCCESS(rv, rv);
  PRBool alreadyVisited = PR_TRUE;
  rv = dbSelectStatement->ExecuteStep(&alreadyVisited);
  NS_ENSURE_SUCCESS(rv, rv);

  // don't need to do anything if we've never heard of this page
  if (!alreadyVisited)
    return NS_OK;
 
  // modify the existing page if necessary

  PRInt32 oldHiddenState = 0;
  rv = dbSelectStatement->GetInt32(1, &oldHiddenState);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!oldHiddenState)
    return NS_OK; // already marked as hidden, we're done

  // find the old ID, which can be found faster than long URLs
  PRInt32 entryid = 0;
  rv = dbSelectStatement->GetInt32(0, &entryid);
  NS_ENSURE_SUCCESS(rv, rv);

  // need to clear the old statement before we create a new one
  dbSelectStatement = nsnull;

  nsCOMPtr<mozIStorageStatement> dbModStatement;
  rv = mDBConn->CreateStatement(
      NS_LITERAL_CSTRING("UPDATE moz_places SET hidden = 1 WHERE id = ?1"),
      getter_AddRefs(dbModStatement));
  NS_ENSURE_SUCCESS(rv, rv);

  dbModStatement->BindInt32Parameter(0, entryid);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = dbModStatement->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  // notify observers, finish transaction first
  transaction.Commit();
  ENUMERATE_OBSERVERS(mCanNotify, mCacheObservers, mObservers, nsINavHistoryObserver,
                      OnPageChanged(aURI,
                                    nsINavHistoryObserver::ATTRIBUTE_HIDDEN,
                                    EmptyString()))

  return NS_OK;
  */
}


// nsNavHistory::MarkPageAsTyped
//
// We call MarkPageAsTyped() before visiting a URL in order to 
// help determine the transition type of the visit.  
// We keep track of the URL so that later, in AddVisitChain() 
// we can use TRANSITION_TYPED as the transition.
// Note, AddVisitChain() is not called immediately when we are doing LAZY_ADDs
//
// @see MarkPageAsFollowedBookmark

NS_IMETHODIMP
nsNavHistory::MarkPageAsTyped(nsIURI *aURI)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aURI);

  // don't add when history is disabled
  if (IsHistoryDisabled())
    return NS_OK;

  nsCAutoString uriString;
  nsresult rv = aURI->GetSpec(uriString);
  NS_ENSURE_SUCCESS(rv, rv);

  // if URL is already in the typed queue, then we need to remove the old one
  PRInt64 unusedEventTime;
  if (mRecentTyped.Get(uriString, &unusedEventTime))
    mRecentTyped.Remove(uriString);

  if (mRecentTyped.Count() > RECENT_EVENT_QUEUE_MAX_LENGTH)
    ExpireNonrecentEvents(&mRecentTyped);

  mRecentTyped.Put(uriString, GetNow());
  return NS_OK;
}


// nsNavHistory::SetCharsetForURI
//
// Sets the character-set for an URI.
// If aCharset is empty remove character-set annotation for aURI.

NS_IMETHODIMP
nsNavHistory::SetCharsetForURI(nsIURI* aURI,
                               const nsAString& aCharset)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aURI);

  nsAnnotationService* annosvc = nsAnnotationService::GetAnnotationService();
  NS_ENSURE_TRUE(annosvc, NS_ERROR_OUT_OF_MEMORY);

  if (aCharset.IsEmpty()) {
    // remove the current page character-set annotation
    nsresult rv = annosvc->RemovePageAnnotation(aURI, CHARSET_ANNO);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    // Set page character-set annotation, silently overwrite if already exists
    nsresult rv = annosvc->SetPageAnnotationString(aURI, CHARSET_ANNO,
                                                   aCharset, 0,
                                                   nsAnnotationService::EXPIRE_NEVER);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}


// nsNavHistory::GetCharsetForURI
//
// Get the last saved character-set for an URI.

NS_IMETHODIMP
nsNavHistory::GetCharsetForURI(nsIURI* aURI, 
                               nsAString& aCharset)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aURI);

  nsAnnotationService* annosvc = nsAnnotationService::GetAnnotationService();
  NS_ENSURE_TRUE(annosvc, NS_ERROR_OUT_OF_MEMORY);

  nsAutoString charset;
  nsresult rv = annosvc->GetPageAnnotationString(aURI, CHARSET_ANNO, aCharset);
  if (NS_FAILED(rv)) {
    // be sure to return an empty string if character-set is not found
    aCharset.Truncate();
  }
  return NS_OK;
}


// nsGlobalHistory2 ************************************************************


// nsNavHistory::AddURI
//
//    This is the main method of adding history entries.

NS_IMETHODIMP
nsNavHistory::AddURI(nsIURI *aURI, PRBool aRedirect,
                     PRBool aToplevel, nsIURI *aReferrer)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aURI);

  // don't add when history is disabled
  if (IsHistoryDisabled())
    return NS_OK;

  // filter out any unwanted URIs
  PRBool canAdd = PR_FALSE;
  nsresult rv = CanAddURI(aURI, &canAdd);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!canAdd)
    return NS_OK;

  PRTime now = PR_Now();

#ifdef LAZY_ADD
  LazyMessage message;
  rv = message.Init(LazyMessage::Type_AddURI, aURI);
  NS_ENSURE_SUCCESS(rv, rv);
  message.isRedirect = aRedirect;
  message.isToplevel = aToplevel;
  if (aReferrer) {
    rv = aReferrer->Clone(getter_AddRefs(message.referrer));
    NS_ENSURE_SUCCESS(rv, rv);
  }
  message.time = now;
  rv = AddLazyMessage(message);
  NS_ENSURE_SUCCESS(rv, rv);
#else
  rv = AddURIInternal(aURI, now, aRedirect, aToplevel, aReferrer);
  NS_ENSURE_SUCCESS(rv, rv);
#endif

  return NS_OK;
}


// nsNavHistory::AddURIInternal
//
//    This does the work of AddURI so it can be done lazily.

nsresult
nsNavHistory::AddURIInternal(nsIURI* aURI, PRTime aTime, PRBool aRedirect,
                             PRBool aToplevel, nsIURI* aReferrer)
{
  mozStorageTransaction transaction(mDBConn, PR_FALSE);

  PRInt64 redirectBookmark = 0;
  PRInt64 visitID, sessionID;
  nsresult rv = AddVisitChain(aURI, aTime, aToplevel, aRedirect, aReferrer,
                              &visitID, &sessionID, &redirectBookmark);
  NS_ENSURE_SUCCESS(rv, rv);

  // The bookmark cache of redirects may be out-of-date with this addition, so
  // we need to update it. The issue here is if they bookmark "mozilla.org" by
  // typing it in without ever having visited "www.mozilla.org". They will then
  // get redirected to the latter, and we need to add mozilla.org ->
  // www.mozilla.org to the bookmark hashtable.
  //
  // AddVisitChain will put the spec of a bookmarked URI if it encounters one
  // into bookmarkURI. If this is non-empty, we know that something has happened
  // with a bookmark and we should probably go update it.
  if (redirectBookmark) {
    nsNavBookmarks *bookmarkService = nsNavBookmarks::GetBookmarksService();
    if (bookmarkService) {
      PRTime now = GetNow();
      bookmarkService->AddBookmarkToHash(redirectBookmark,
                                         now - BOOKMARK_REDIRECT_TIME_THRESHOLD);
    }
  }

  return transaction.Commit();
}


// nsNavHistory::AddVisitChain
//
//    This function is sits between AddURI (which is called when a page is
//    visited) and AddVisit (which creates the DB entries) to figure out what
//    we should add and what are the detailed parameters that should be used
//    (like referring visit ID and typed/bookmarked state).
//
//    This function walks up the referring chain and recursively calls itself,
//    each time calling InternalAdd to create a new history entry. (When we
//    get notified of redirects, we don't actually add any history entries, just
//    save them in mRecentRedirects. This function will add all of them for a
//    given destination page when that page is actually visited.)
//    See GetRedirectFor for more information about how redirects work.
//
//    aRedirectBookmark should be empty when this function is first called. If
//    there are any redirects that are bookmarks the specs will be placed in
//    this buffer. The caller can then determine if any bookmarked items were
//    visited so it knows whether to update the bookmark service's redirect
//    hashtable.

nsresult
nsNavHistory::AddVisitChain(nsIURI* aURI, PRTime aTime,
                            PRBool aToplevel, PRBool aIsRedirect,
                            nsIURI* aReferrerURI, PRInt64* aVisitID,
                            PRInt64* aSessionID, PRInt64* aRedirectBookmark)
{
  PRUint32 transitionType = 0;
  PRInt64 referringVisit = 0;
  PRTime visitTime = 0;
  nsCOMPtr<nsIURI> fromVisitURI = aReferrerURI;

  nsCAutoString spec;
  nsresult rv = aURI->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCAutoString redirectSource;
  if (GetRedirectFor(spec, redirectSource, &visitTime, &transitionType)) {
    // this was a redirect: See GetRedirectFor for info on how this works
    nsCOMPtr<nsIURI> redirectURI;
    rv = NS_NewURI(getter_AddRefs(redirectURI), redirectSource);
    NS_ENSURE_SUCCESS(rv, rv);

    // remember if any redirect sources were bookmarked
    nsNavBookmarks *bookmarkService = nsNavBookmarks::GetBookmarksService();
    PRBool isBookmarked;
    if (bookmarkService &&
        NS_SUCCEEDED(bookmarkService->IsBookmarked(redirectURI, &isBookmarked))
        && isBookmarked) {
      GetUrlIdFor(redirectURI, aRedirectBookmark, PR_FALSE);
    }

    // Find the visit for the source. Note that we decrease the time counter,
    // which will ensure that the referrer and this page will appear in history
    // in the correct order. Since the times are in microseconds, it should not
    // normally be possible to get two pages within one microsecond of each
    // other so the referrer won't appear before a previous page viewed.
    rv = AddVisitChain(redirectURI, aTime - 1, aToplevel, PR_TRUE, aReferrerURI,
                       &referringVisit, aSessionID, aRedirectBookmark);
    NS_ENSURE_SUCCESS(rv, rv);

    // for redirects in frames, we don't want to see those items in history
    // see bug #381453 for more details
    if (!aToplevel) {
      transitionType = nsINavHistoryService::TRANSITION_EMBED;
    }

    // We have been redirected, if the previous site was not a redirect
    // update the referrer so we can walk up the redirect chain.
    // See bug 411966 and Bug 428690 for details.
    fromVisitURI = redirectURI;
  } else if (aReferrerURI) {
    // We do not want to add a new visit if the referring site is the same as
    // the new site.  This is the situation where a page refreshes itself to
    // give the user updated information.
    PRBool referrerIsSame;
    if (NS_SUCCEEDED(aURI->Equals(aReferrerURI, &referrerIsSame)) && referrerIsSame)
      return NS_OK;

    // If there is a referrer, we know you came from somewhere, either manually
    // or automatically. For toplevel windows, assume its manual and you want
    // to see this in history. For other things, it's some kind of embedded
    // navigation. This is true of images and other content the user doesn't
    // want to see in their history, but also of embedded frames that the user
    // navigated manually and probably DOES want to see in history.
    // Unfortunately, there isn't any easy way to distinguish these.
    //
    // Generally, it boils down to the problem of detecting whether a frame
    // content change is the result of a user action, which isn't well defined
    // since script could change a frame's source as a result of user request,
    // or just because it feels like loading a new ad. The "back" button will
    // undo either of these actions.
    if (aToplevel)
      transitionType = nsINavHistoryService::TRANSITION_LINK;
    else
      transitionType = nsINavHistoryService::TRANSITION_EMBED;

    // Note that here we should NOT use the GetNow function. That function
    // caches the value of "now" until next time the event loop runs. This
    // gives better performance, but here we may get many notifications without
    // running the event loop. We must preserve these events' ordering. This
    // most commonly happens on redirects.
    visitTime = PR_Now();

    // Try to turn the referrer into a visit.
    // This also populates the session id.
    if (!FindLastVisit(aReferrerURI, &referringVisit, aSessionID)) {
      // we couldn't find a visit for the referrer, don't set it
      *aSessionID = GetNewSessionID();
    }
  } else {
    // When there is no referrer, we know the user must have gotten the link
    // from somewhere, so check our sources to see if it was recently typed or
    // has a bookmark selected. We don't handle drag-and-drop operations.
    // note:  the link may have also come from a new window (set to load a homepage)
    // or on start up (if we've set to load the home page or restore tabs)
    // we treat these as TRANSITION_LINK (if they are top level) or
    // TRANSITION_EMBED (if not top level).  We don't want to to add visits to 
    // history without a transition type.
    if (CheckIsRecentEvent(&mRecentTyped, spec))
      transitionType = nsINavHistoryService::TRANSITION_TYPED;
    else if (CheckIsRecentEvent(&mRecentBookmark, spec))
      transitionType = nsINavHistoryService::TRANSITION_BOOKMARK;
    else if (aToplevel)
      transitionType = nsINavHistoryService::TRANSITION_LINK;
    else
      transitionType = nsINavHistoryService::TRANSITION_EMBED;

    visitTime = PR_Now();
    *aSessionID = GetNewSessionID();
  }

  // this call will create the visit and create/update the page entry
  return AddVisit(aURI, visitTime, fromVisitURI, transitionType,
                  aIsRedirect, *aSessionID, aVisitID);
}


// nsNavHistory::IsVisited
//
//    Note that this ignores the "hidden" flag. This function just checks if the
//    given page is in the DB for link coloring. The "hidden" flag affects
//    the history list view and autocomplete.

NS_IMETHODIMP
nsNavHistory::IsVisited(nsIURI *aURI, PRBool *_retval)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aURI);
  NS_ENSURE_ARG_POINTER(_retval);

  // if history is disabled, we can optimize
  if (IsHistoryDisabled()) {
    *_retval = PR_FALSE;
    return NS_OK;
  }

  nsCAutoString utf8URISpec;
  nsresult rv = aURI->GetSpec(utf8URISpec);
  NS_ENSURE_SUCCESS(rv, rv);

  *_retval = IsURIStringVisited(utf8URISpec);
  return NS_OK;
}


// nsNavHistory::SetPageTitle
//
//    This sets the page title.
//
//    Note that we do not allow empty real titles and will silently ignore such
//    requests. When a URL is added we give it a default title based on the
//    URL. Most pages provide a title and it gets replaced to something better.
//    Some pages don't: some say <title></title>, and some don't have any title
//    element. In BOTH cases, we get SetPageTitle(URI, ""), but in both cases,
//    our default title is more useful to the user than "(no title)".

NS_IMETHODIMP
nsNavHistory::SetPageTitle(nsIURI* aURI,
                           const nsAString& aTitle)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aURI);

  // Don't update the page title inside the private browsing mode.
  if (InPrivateBrowsingMode())
    return NS_OK;

  // if aTitle is empty we want to clear the previous title.
  // We don't want to set it to an empty string, but to a NULL value,
  // so we use SetIsVoid and SetPageTitleInternal will take care of that

#ifdef LAZY_ADD
  LazyMessage message;
  nsresult rv = message.Init(LazyMessage::Type_Title, aURI);
  NS_ENSURE_SUCCESS(rv, rv);
  message.title = aTitle;
  if (aTitle.IsEmpty())
    message.title.SetIsVoid(PR_TRUE);
  return AddLazyMessage(message);
#else
  if (aTitle.IsEmpty()) {
    nsString voidString;
    voidString.SetIsVoid(PR_TRUE);
    return SetPageTitleInternal(aURI, voidString);
  }
  return SetPageTitleInternal(aURI, aTitle);
#endif
}

NS_IMETHODIMP
nsNavHistory::GetPageTitle(nsIURI* aURI, nsAString& aTitle)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aURI);

  aTitle.Truncate(0);

  mozStorageStatementScoper scope(mDBGetURLPageInfo);
  nsresult rv = BindStatementURI(mDBGetURLPageInfo, 0, aURI);
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool results;
  rv = mDBGetURLPageInfo->ExecuteStep(&results);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!results) {
    aTitle.SetIsVoid(PR_TRUE);
    return NS_OK; // not found: return void string
  }

  return mDBGetURLPageInfo->GetString(nsNavHistory::kGetInfoIndex_Title, aTitle);
}


// nsNavHistory::GetURIGeckoFlags
//
//    FIXME: should we try to use annotations for this stuff?

NS_IMETHODIMP
nsNavHistory::GetURIGeckoFlags(nsIURI* aURI, PRUint32* aResult)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aURI);
  NS_ENSURE_ARG_POINTER(aResult);

  return NS_ERROR_NOT_IMPLEMENTED;
}


// nsNavHistory::SetURIGeckoFlags
//
//    FIXME: should we try to use annotations for this stuff?

NS_IMETHODIMP
nsNavHistory::SetURIGeckoFlags(nsIURI* aURI, PRUint32 aFlags)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aURI);

  return NS_ERROR_NOT_IMPLEMENTED;
}

// nsIGlobalHistory3 ***********************************************************

// nsNavHistory::AddDocumentRedirect
//
//    This adds a redirect mapping from the destination of the redirect to the
//    source, time, and type. This mapping is used by GetRedirectFor when we
//    get a page added to reconstruct the redirects that happened when a page
//    is visited. See GetRedirectFor for more information

// this is the expiration callback function that deletes stale entries
PLDHashOperator nsNavHistory::ExpireNonrecentRedirects(
    nsCStringHashKey::KeyType aKey, RedirectInfo& aData, void* aUserArg)
{
  PRInt64* threshold = reinterpret_cast<PRInt64*>(aUserArg);
  if (aData.mTimeCreated < *threshold)
    return PL_DHASH_REMOVE;
  return PL_DHASH_NEXT;
}

NS_IMETHODIMP
nsNavHistory::AddDocumentRedirect(nsIChannel *aOldChannel,
                                  nsIChannel *aNewChannel,
                                  PRInt32 aFlags,
                                  PRBool aTopLevel)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aOldChannel);
  NS_ENSURE_ARG(aNewChannel);

  // ignore internal redirects
  if (aFlags & nsIChannelEventSink::REDIRECT_INTERNAL)
    return NS_OK;

  nsresult rv;
  nsCOMPtr<nsIURI> oldURI, newURI;
  rv = aOldChannel->GetURI(getter_AddRefs(oldURI));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aNewChannel->GetURI(getter_AddRefs(newURI));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCString oldSpec, newSpec;
  rv = oldURI->GetSpec(oldSpec);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = newURI->GetSpec(newSpec);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mRecentRedirects.Count() > RECENT_EVENT_QUEUE_MAX_LENGTH) {
    // expire out-of-date ones
    PRInt64 threshold = PR_Now() - RECENT_EVENT_THRESHOLD;
    mRecentRedirects.Enumerate(ExpireNonrecentRedirects,
                               reinterpret_cast<void*>(&threshold));
  }

  RedirectInfo info;

  // remove any old entries for this redirect destination
  if (mRecentRedirects.Get(newSpec, &info))
    mRecentRedirects.Remove(newSpec);

  // save the new redirect info
  info.mSourceURI = oldSpec;
  info.mTimeCreated = PR_Now();
  if (aFlags & nsIChannelEventSink::REDIRECT_TEMPORARY)
    info.mType = TRANSITION_REDIRECT_TEMPORARY;
  else
    info.mType = TRANSITION_REDIRECT_PERMANENT;
  mRecentRedirects.Put(newSpec, info);

  return NS_OK;
}


// nsIDownloadHistory **********************************************************

NS_IMETHODIMP
nsNavHistory::AddDownload(nsIURI* aSource, nsIURI* aReferrer,
                          PRTime aStartTime)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aSource);

  // don't add when history is disabled and silently fail
  if (IsHistoryDisabled())
    return NS_OK;

  PRInt64 visitID;
  return AddVisit(aSource, aStartTime, aReferrer, TRANSITION_DOWNLOAD, PR_FALSE,
                  0, &visitID);
}

// nsPIPlacesDatabase **********************************************************

NS_IMETHODIMP
nsNavHistory::GetDBConnection(mozIStorageConnection **_DBConnection)
{
  NS_ENSURE_ARG_POINTER(_DBConnection);
  NS_ADDREF(*_DBConnection = mDBConn);
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistory::FinalizeInternalStatements()
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");

#ifdef LAZY_ADD
  // Kill lazy timer or it could fire later when statements won't be valid
  // anymore.
  // At this point we should have called CommitPendingChanges before the last
  // sync, so all data is saved to disk and we can finalize all statements.
  if (mLazyTimer)
    mLazyTimer->Cancel();
  NS_ABORT_IF_FALSE(mLazyMessages.Length() == 0,
    "There are pending lazy messages, did you call CommitPendingChanges()?");
#endif

  // nsNavHistory
  nsresult rv = FinalizeStatements();
  NS_ENSURE_SUCCESS(rv, rv);

  // nsNavBookmarks
  nsNavBookmarks *bookmarks = nsNavBookmarks::GetBookmarksService();
  NS_ENSURE_TRUE(bookmarks, NS_ERROR_OUT_OF_MEMORY);
  rv = bookmarks->FinalizeStatements();
  NS_ENSURE_SUCCESS(rv, rv);

  // nsAnnotationService
  nsAnnotationService* annosvc = nsAnnotationService::GetAnnotationService();
  NS_ENSURE_TRUE(annosvc, NS_ERROR_OUT_OF_MEMORY);
  rv = annosvc->FinalizeStatements();
  NS_ENSURE_SUCCESS(rv, rv);

  // nsFaviconService
  nsFaviconService* iconsvc = nsFaviconService::GetFaviconService();
  NS_ENSURE_TRUE(iconsvc, NS_ERROR_OUT_OF_MEMORY);
  rv = iconsvc->FinalizeStatements();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
nsNavHistory::CommitPendingChanges()
{
  #ifdef LAZY_ADD
    CommitLazyMessages();
  #endif

  // Immediately serve topics we generated, this way they won't try to access
  // the database after CommitPendingChanges has been called.
  nsCOMPtr<nsIObserverService> os =
    do_GetService("@mozilla.org/observer-service;1");
  NS_ENSURE_TRUE(os, NS_ERROR_FAILURE);
  nsCOMPtr<nsISimpleEnumerator> e;
  nsresult rv = os->EnumerateObservers(PLACES_INIT_COMPLETE_TOPIC,
                                       getter_AddRefs(e));
  if (NS_SUCCEEDED(rv) && e) {
    nsCOMPtr<nsIObserver> observer;
    PRBool loop = PR_TRUE;
    while(NS_SUCCEEDED(e->HasMoreElements(&loop)) && loop)
    {
      e->GetNext(getter_AddRefs(observer));
      rv = observer->Observe(observer,
                             PLACES_INIT_COMPLETE_TOPIC,
                             nsnull);
    }
  }

  return NS_OK;
}

// nsPIPlacesHistoryListenersNotifier ******************************************

NS_IMETHODIMP
nsNavHistory::NotifyOnPageExpired(nsIURI *aURI, PRTime aVisitTime,
                                  PRBool aWholeEntry)
{
  ENUMERATE_OBSERVERS(mCanNotify, mCacheObservers, mObservers, nsINavHistoryObserver,
                      OnPageExpired(aURI, aVisitTime, aWholeEntry));
  if (aWholeEntry) {
    // Notify our observers that the URI has been removed.
    ENUMERATE_OBSERVERS(mCanNotify, mCacheObservers, mObservers,
                        nsINavHistoryObserver, OnDeleteURI(aURI));
  }

  return NS_OK;
}

// nsIObserver *****************************************************************

NS_IMETHODIMP
nsNavHistory::Observe(nsISupports *aSubject, const char *aTopic,
                    const PRUnichar *aData)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");

  if (strcmp(aTopic, gQuitApplicationGrantedMessage) == 0) {
    nsresult rv;
    nsCOMPtr<nsIPrefService> prefService =
      do_GetService(NS_PREFSERVICE_CONTRACTID, &rv);
    if (NS_SUCCEEDED(rv))
      prefService->SavePrefFile(nsnull);

    // Start shutdown expiration.
    mExpire->OnQuit();
  }
  else if (strcmp(aTopic, gXpcomShutdown) == 0) {
    nsresult rv;
    nsCOMPtr<nsIObserverService> observerService =
      do_GetService("@mozilla.org/observer-service;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    observerService->RemoveObserver(this, gAutoCompleteFeedback);
    observerService->RemoveObserver(this, NS_PRIVATE_BROWSING_SWITCH_TOPIC);
    observerService->RemoveObserver(this, gIdleDaily);
    observerService->RemoveObserver(this, gXpcomShutdown);
    observerService->RemoveObserver(this, gQuitApplicationGrantedMessage);
  }
#ifdef MOZ_XUL
  else if (strcmp(aTopic, gAutoCompleteFeedback) == 0) {
    nsCOMPtr<nsIAutoCompleteInput> input = do_QueryInterface(aSubject);
    if (!input)
      return NS_OK;

    nsCOMPtr<nsIAutoCompletePopup> popup;
    input->GetPopup(getter_AddRefs(popup));
    if (!popup)
      return NS_OK;

    nsCOMPtr<nsIAutoCompleteController> controller;
    input->GetController(getter_AddRefs(controller));
    if (!controller)
      return NS_OK;

    // Don't bother if the popup is closed
    PRBool open;
    nsresult rv = popup->GetPopupOpen(&open);
    NS_ENSURE_SUCCESS(rv, rv);
    if (!open)
      return NS_OK;

    // Ignore if nothing selected from the popup
    PRInt32 selectedIndex;
    rv = popup->GetSelectedIndex(&selectedIndex);
    NS_ENSURE_SUCCESS(rv, rv);
    if (selectedIndex == -1)
      return NS_OK;

    rv = AutoCompleteFeedback(selectedIndex, controller);
    NS_ENSURE_SUCCESS(rv, rv);
  }
#endif
  else if (strcmp(aTopic, "nsPref:changed") == 0) {
    PRInt32 oldDaysMin = mExpireDaysMin;
    PRInt32 oldDaysMax = mExpireDaysMax;
    PRInt32 oldVisits = mExpireSites;
    LoadPrefs(PR_FALSE);
    if (oldDaysMin != mExpireDaysMin || oldDaysMax != mExpireDaysMax ||
        oldVisits != mExpireSites)
      mExpire->OnExpirationChanged();
  }
  else if (strcmp(aTopic, gIdleDaily) == 0) {
    // Ensure our connection is still alive.  The idle-daily observer is removed
    // on xpcom-shutdown, but we could have closed the connection earlier due
    // to errors or during normal shutdown process.
    NS_ENSURE_TRUE(mDBConn, NS_OK);

    (void)DecayFrecency();
    (void)VacuumDatabase();
  }
  else if (strcmp(aTopic, NS_PRIVATE_BROWSING_SWITCH_TOPIC) == 0) {
    if (NS_LITERAL_STRING(NS_PRIVATE_BROWSING_ENTER).Equals(aData)) {
#ifdef LAZY_ADD
      // Commit all lazy messages in order to protect against edge cases where a
      // lazy message which is not allowed in private browsing mode has been
      // added before entering the private browsing mode, and is going to be
      // scheduled to be processed after entering the private browsing mode.
      CommitLazyMessages();
#endif

      mInPrivateBrowsing = PR_TRUE;
    }
    else if (NS_LITERAL_STRING(NS_PRIVATE_BROWSING_LEAVE).Equals(aData)) {
#ifdef LAZY_ADD
      // Commit all lazy messages in order to protect against edge cases where a
      // lazy message which should be processed in private browsing mode has been
      // added before leaving the private browsing mode, and is going to be
      // scheduled to be processed after leaving the private browsing mode.
      CommitLazyMessages();
#endif

      mInPrivateBrowsing = PR_FALSE;
    }
  }
  else if (strcmp(aTopic, PLACES_INIT_COMPLETE_TOPIC) == 0) {
    nsCOMPtr<nsIObserverService> os =
      do_GetService("@mozilla.org/observer-service;1");
    NS_ENSURE_TRUE(os, NS_ERROR_FAILURE);
    (void)os->RemoveObserver(this, PLACES_INIT_COMPLETE_TOPIC);

    // This code is only called if we've either imported or done a migration
    // from a pre-frecency build, so we will calculate all their frecencies.
    (void)FixInvalidFrecencies();
  }

  return NS_OK;
}

NS_HIDDEN_(nsresult)
nsNavHistory::VacuumDatabase()
{
  // SQLite cannot give us a real value for fragmentation percentage,
  // we could analyze the database file page by page, and count fragmented
  // space, but that would be slow and not maintainable across different SQLite
  // versions.
  // For this reason we just take a guess using the freelist count.
  // This way we know how much pages are unused, but we don't know anything
  // about fragmentation.
  // This ratio is used in conjunction with a time pref to avoid vacuuming too
  // often or too rarely.

  PRInt32 lastVacuumPref;
  PRInt64 lastVacuumTime = 0;
  nsCOMPtr<nsIPrefBranch> prefSvc =
    do_GetService("@mozilla.org/preferences-service;1");
  NS_ENSURE_TRUE(prefSvc, NS_ERROR_OUT_OF_MEMORY);
  if (NS_SUCCEEDED(prefSvc->GetIntPref(PREF_LAST_VACUUM, &lastVacuumPref))) {
    // Value are seconds till epoch, convert it to microseconds.
    lastVacuumTime = (PRInt64)lastVacuumPref * PR_USEC_PER_SEC;
  }

  nsresult rv;
  float freePagesRatio = 0;
  if (!lastVacuumTime ||
      (lastVacuumTime < (PR_Now() - MIN_TIME_BEFORE_VACUUM) &&
       lastVacuumTime > (PR_Now() - MAX_TIME_BEFORE_VACUUM))) {
    // This is the first vacuum, or we are in the timeframe where vacuum could
    // happen.  Calculate the vacuum ratio and vacuum if it is less then
    // threshold.
    nsCOMPtr<mozIStorageStatement> statement;
    rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING("PRAGMA page_count"),
                                  getter_AddRefs(statement));
    NS_ENSURE_SUCCESS(rv, rv);
    PRBool hasResult = PR_FALSE;
    rv = statement->ExecuteStep(&hasResult);
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ENSURE_TRUE(hasResult, NS_ERROR_FAILURE);
    PRInt32 pageCount = statement->AsInt32(0);

    rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING("PRAGMA freelist_count"),
                                  getter_AddRefs(statement));
    NS_ENSURE_SUCCESS(rv, rv);
    hasResult = PR_FALSE;
    rv = statement->ExecuteStep(&hasResult);
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ENSURE_TRUE(hasResult, NS_ERROR_FAILURE);
    PRInt32 freelistCount = statement->AsInt32(0);

    freePagesRatio = (float)(freelistCount / pageCount);
  }
  
  if (freePagesRatio > VACUUM_FREEPAGES_THRESHOLD ||
      lastVacuumTime < (PR_Now() - MAX_TIME_BEFORE_VACUUM)) {
    // We vacuum in 2 cases:
    //  - We are in the valid vacuum timeframe and vacuum ratio is high.
    //  - Last vacuum has been executed a lot of time ago.

    // Notify we are about to vacuum.  This is mostly for testability.
    nsCOMPtr<nsIObserverService> observerService =
      do_GetService("@mozilla.org/observer-service;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = observerService->NotifyObservers(nsnull,
                                          PLACES_VACUUM_STARTING_TOPIC,
                                          nsnull);
    NS_ENSURE_SUCCESS(rv, rv);

    // Actually vacuuming a database is a slow operation, since it could take
    // seconds.  Part of the time is spent in updating the journal file on disk
    // and this is particularly bad on devices with slow I/O.  Temporary
    // moving the journal to memory could increase a bit the possibility of
    // corruption if we crash during this time, but makes the process really
    // faster.
    nsCOMPtr<mozIStorageStatement> journalToMemory;
    rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
        "PRAGMA journal_mode = MEMORY"),
      getter_AddRefs(journalToMemory));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<mozIStorageStatement> vacuum;
    rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING("VACUUM"),
                                  getter_AddRefs(vacuum));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<mozIStorageStatement> journalToDefault;
    rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
        "PRAGMA journal_mode = " DEFAULT_JOURNAL_MODE),
      getter_AddRefs(journalToDefault));
    NS_ENSURE_SUCCESS(rv, rv);

    mozIStorageStatement *stmts[] = {
      journalToMemory,
      vacuum,
      journalToDefault
    };
    nsCOMPtr<mozIStoragePendingStatement> ps;
    rv = mDBConn->ExecuteAsync(stmts, NS_ARRAY_LENGTH(stmts), nsnull,
                               getter_AddRefs(ps));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = prefSvc->SetIntPref(PREF_LAST_VACUUM,
                             (PRInt32)(PR_Now() / PR_USEC_PER_SEC));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

NS_HIDDEN_(nsresult)
nsNavHistory::DecayFrecency()
{
  // Update frecency values.
  nsresult rv = FixInvalidFrecencies();
  NS_ENSURE_SUCCESS(rv, rv);

  // Globally decay places frecency rankings to estimate reduced frecency
  // values of pages that haven't been visited for a while, i.e., they do
  // not get an updated frecency. We directly modify moz_places to avoid
  // bringing the whole database into places_temp through places_view. A
  // scaling factor of .975 results in .5 the original value after 28 days.
  nsCOMPtr<mozIStorageStatement> decayFrecency;
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "UPDATE moz_places SET frecency = ROUND(frecency * .975) "
      "WHERE frecency > 0"),
    getter_AddRefs(decayFrecency));
  NS_ENSURE_SUCCESS(rv, rv);

  // Decay potentially unused adaptive entries (e.g. those that are at 1)
  // to allow better chances for new entries that will start at 1.
  nsCOMPtr<mozIStorageStatement> decayAdaptive;
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "UPDATE moz_inputhistory SET use_count = use_count * .975"),
    getter_AddRefs(decayAdaptive));
  NS_ENSURE_SUCCESS(rv, rv);

  // Delete any adaptive entries that won't help in ordering anymore.
  nsCOMPtr<mozIStorageStatement> deleteAdaptive;
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "DELETE FROM moz_inputhistory WHERE use_count < .01"),
    getter_AddRefs(deleteAdaptive));
  NS_ENSURE_SUCCESS(rv, rv);

  mozIStorageStatement *stmts[] = {
    decayFrecency,
    decayAdaptive,
    deleteAdaptive
  };
  nsCOMPtr<mozIStoragePendingStatement> ps;
  rv = mDBConn->ExecuteAsync(stmts, NS_ARRAY_LENGTH(stmts), nsnull,
                             getter_AddRefs(ps));
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

// Lazy stuff ******************************************************************

#ifdef LAZY_ADD

// nsNavHistory::AddLazyLoadFaviconMessage

nsresult
nsNavHistory::AddLazyLoadFaviconMessage(nsIURI* aPage, nsIURI* aFavicon,
                                        PRBool aForceReload)
{
  LazyMessage message;
  nsresult rv = message.Init(LazyMessage::Type_Favicon, aPage);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aFavicon->Clone(getter_AddRefs(message.favicon));
  NS_ENSURE_SUCCESS(rv, rv);
  message.alwaysLoadFavicon = aForceReload;
  return AddLazyMessage(message);
}


// nsNavHistory::StartLazyTimer
//
//    This schedules flushing of the lazy message queue for the future.
//
//    If we already have timer set, we canel it and schedule a new timer in
//    the future. This saves you from having to wait if you open a bunch of
//    pages in a row. However, we don't want to defer too long, so we'll only
//    push it back MAX_LAZY_TIMER_DEFERMENTS times. After that we always
//    let the timer go the next time.

nsresult
nsNavHistory::StartLazyTimer()
{
  if (! mLazyTimer) {
    mLazyTimer = do_CreateInstance("@mozilla.org/timer;1");
    if (! mLazyTimer)
      return NS_ERROR_OUT_OF_MEMORY;
  } else {
    if (mLazyTimerSet) {
      if (mLazyTimerDeferments >= MAX_LAZY_TIMER_DEFERMENTS) {
        // already set and we don't want to push it back any later, use that one
        return NS_OK;
      } else {
        // push back the active timer
        mLazyTimer->Cancel();
        mLazyTimerDeferments ++;
      }
    }
  }
  nsresult rv = mLazyTimer->InitWithFuncCallback(LazyTimerCallback, this,
                                                 LAZY_MESSAGE_TIMEOUT,
                                                 nsITimer::TYPE_ONE_SHOT);
  NS_ENSURE_SUCCESS(rv, rv);
  mLazyTimerSet = PR_TRUE;
  return NS_OK;
}


// nsNavHistory::AddLazyMessage

nsresult
nsNavHistory::AddLazyMessage(const LazyMessage& aMessage)
{
  if (! mLazyMessages.AppendElement(aMessage))
    return NS_ERROR_OUT_OF_MEMORY;
  return StartLazyTimer();
}


// nsNavHistory::LazyTimerCallback

void // static
nsNavHistory::LazyTimerCallback(nsITimer* aTimer, void* aClosure)
{
  nsNavHistory* that = static_cast<nsNavHistory*>(aClosure);
  that->mLazyTimerSet = PR_FALSE;
  that->mLazyTimerDeferments = 0;
  that->CommitLazyMessages();
}

// nsNavHistory::CommitLazyMessages

void
nsNavHistory::CommitLazyMessages()
{
  mozStorageTransaction transaction(mDBConn, PR_TRUE);
  for (PRUint32 i = 0; i < mLazyMessages.Length(); i ++) {
    LazyMessage& message = mLazyMessages[i];
    switch (message.type) {
      case LazyMessage::Type_AddURI:
        AddURIInternal(message.uri, message.time, message.isRedirect,
                       message.isToplevel, message.referrer);
        break;
      case LazyMessage::Type_Title:
        SetPageTitleInternal(message.uri, message.title);
        break;
      case LazyMessage::Type_Favicon: {
        nsFaviconService* faviconService = nsFaviconService::GetFaviconService();
        if (faviconService) {
          faviconService->DoSetAndLoadFaviconForPage(message.uri,
                                                     message.favicon,
                                                     message.alwaysLoadFavicon);
        }
        break;
      }
      default:
        NS_NOTREACHED("Invalid lazy message type");
    }
  }
  mLazyMessages.Clear();
}
#endif // LAZY_ADD


// Query stuff *****************************************************************

// Helper class for QueryToSelectClause
//
// This class helps to build part of the WHERE clause. It supports 
// multiple queries by appending the query index to the parameter name. 
// For the query with index 0 the parameter name is not altered what
// allows using this parameter in other situations (see SelectAsSite). 

class ConditionBuilder
{
public:

  ConditionBuilder(PRInt32 aQueryIndex): mQueryIndex(aQueryIndex)
  { }

  ConditionBuilder& Condition(const char* aStr)
  {
    if (!mClause.IsEmpty())
      mClause.AppendLiteral(" AND ");
    Str(aStr);
    return *this;
  }

  ConditionBuilder& Str(const char* aStr)
  {
    mClause.Append(' ');
    mClause.Append(aStr);
    mClause.Append(' ');
    return *this;
  }

  ConditionBuilder& Param(const char* aParam)
  {
    mClause.Append(' ');
    if (!mQueryIndex)
      mClause.Append(aParam);
    else
      mClause += nsPrintfCString("%s%d", aParam, mQueryIndex);

    mClause.Append(' ');
    return *this;
  }

  void GetClauseString(nsCString& aResult) 
  {
    aResult = mClause;
  }

private:

  PRInt32 mQueryIndex;
  nsCString mClause;
};


// nsNavHistory::QueryToSelectClause
//
//    THE BEHAVIOR SHOULD BE IN SYNC WITH BindQueryClauseParameters
//
//    I don't check return values from the query object getters because there's
//    no way for those to fail.

nsresult
nsNavHistory::QueryToSelectClause(nsNavHistoryQuery* aQuery, // const
                                  nsNavHistoryQueryOptions* aOptions,
                                  PRInt32 aQueryIndex,
                                  nsCString* aClause)
{
  PRBool hasIt;

  ConditionBuilder clause(aQueryIndex);

  // begin time
  if (NS_SUCCEEDED(aQuery->GetHasBeginTime(&hasIt)) && hasIt) 
    clause.Condition("v.visit_date >=").Param(":begin_time");

  // end time
  if (NS_SUCCEEDED(aQuery->GetHasEndTime(&hasIt)) && hasIt)
    clause.Condition("v.visit_date <=").Param(":end_time");

  // search terms FIXME

  // min and max visit count
  if (aQuery->MinVisits() >= 0)
    clause.Condition("h.visit_count >=").Param(":min_visits");

  if (aQuery->MaxVisits() >= 0)
    clause.Condition("h.visit_count <=").Param(":max_visits");
  
  // only bookmarked, has no affect on bookmarks-only queries
  if (aOptions->QueryType() != nsINavHistoryQueryOptions::QUERY_TYPE_BOOKMARKS &&
      aQuery->OnlyBookmarked())
    clause.Condition("EXISTS (SELECT b.fk FROM moz_bookmarks b WHERE b.type = ")
          .Str(nsPrintfCString("%d", nsNavBookmarks::TYPE_BOOKMARK).get())
          .Str("AND b.fk = h.id)");

  // domain
  if (NS_SUCCEEDED(aQuery->GetHasDomain(&hasIt)) && hasIt) {
    PRBool domainIsHost = PR_FALSE;
    aQuery->GetDomainIsHost(&domainIsHost);
    if (domainIsHost)
      clause.Condition("h.rev_host =").Param(":domain_lower");
    else
      // see domain setting in BindQueryClauseParameters for why we do this
      clause.Condition("h.rev_host >=").Param(":domain_lower")
            .Condition("h.rev_host <").Param(":domain_upper");
  }

  // URI
  if (NS_SUCCEEDED(aQuery->GetHasUri(&hasIt)) && hasIt) {
    if (aQuery->UriIsPrefix()) {
      clause.Condition("h.url >= ").Param(":uri")
            .Condition("h.url <= ").Param(":uri_upper");
    }
    else
      clause.Condition("h.url =").Param(":uri");
  }

  // annotation
  aQuery->GetHasAnnotation(&hasIt);
  if (hasIt) {
    clause.Condition("");
    if (aQuery->AnnotationIsNot())
      clause.Str("NOT");
    clause.Str(
      "EXISTS "
        "(SELECT h.id "
         "FROM moz_annos anno "
         "JOIN moz_anno_attributes annoname "
           "ON anno.anno_attribute_id = annoname.id "
         "WHERE anno.place_id = h.id "
           "AND annoname.name = ").Param(":anno").Str(")");
    // annotation-based queries don't get the common conditions, so you get
    // all URLs with that annotation
  }

  // tags
  const nsTArray<nsString> &tags = aQuery->Tags();
  if (tags.Length() > 0) {
    clause.Condition("h.id");
    if (aQuery->TagsAreNot())
      clause.Str("NOT");
    clause.Str(
      "IN "
        "(SELECT bms.fk "
         "FROM moz_bookmarks bms "
         "JOIN moz_bookmarks tags ON bms.parent = tags.id "
         "WHERE tags.parent =").
           Param(":tags_folder").
           Str("AND tags.title IN (");
    for (PRUint32 i = 0; i < tags.Length(); ++i) {
      nsPrintfCString param(":tag%d_", i);
      clause.Param(param.get());
      if (i < tags.Length() - 1)
        clause.Str(",");
    }
    clause.Str(")");
    if (!aQuery->TagsAreNot())
      clause.Str("GROUP BY bms.fk HAVING count(*) >=").Param(":tag_count");
    clause.Str(")");
  }

  // parent parameter is used in tag contents queries.
  // Only one folder should be defined for them.
  if (aOptions->ResultType() == nsINavHistoryQueryOptions::RESULTS_AS_TAG_CONTENTS &&
      aQuery->Folders().Length() == 1) {
    clause.Condition("b.parent =").Param(":parent");
  }

  clause.GetClauseString(*aClause);
  return NS_OK;
}

// Helper class for BindQueryClauseParameters
//
// This class converts parameter names to parameter indexes. It supports 
// multiple queries by appending the query index to the parameter name. 
// For the query with index 0 the parameter name is not altered what
// allows using this parameter in other situations (see SelectAsSite). 

class IndexGetter
{
public:
  IndexGetter(PRInt32 aQueryIndex, mozIStorageStatement* aStatement) : 
    mQueryIndex(aQueryIndex), mStatement(aStatement)
  {
    mResult = NS_OK;
  }

  PRUint32 For(const char* aName) 
  {
    PRUint32 index;

    // Do not execute if we already had an error
    if (NS_SUCCEEDED(mResult)) {
      if (!mQueryIndex)
        mResult = mStatement->GetParameterIndex(nsCAutoString(aName), &index);
      else
        mResult = mStatement->GetParameterIndex(
                      nsPrintfCString("%s%d", aName, mQueryIndex), &index);
    }

    if (NS_SUCCEEDED(mResult))
      return index;

    return -1; // Invalid index
  }

  nsresult Result() 
  {
    return mResult;
  }

private:
  PRInt32 mQueryIndex;
  mozIStorageStatement* mStatement;
  nsresult mResult;
};

// nsNavHistory::BindQueryClauseParameters
//
//    THE BEHAVIOR SHOULD BE IN SYNC WITH QueryToSelectClause

nsresult
nsNavHistory::BindQueryClauseParameters(mozIStorageStatement* statement,
                                        PRInt32 aQueryIndex,
                                        nsNavHistoryQuery* aQuery, // const
                                        nsNavHistoryQueryOptions* aOptions)
{
  nsresult rv;

  PRBool hasIt;
  IndexGetter index(aQueryIndex, statement);

  // begin time
  if (NS_SUCCEEDED(aQuery->GetHasBeginTime(&hasIt)) && hasIt) {
    PRTime time = NormalizeTime(aQuery->BeginTimeReference(),
                                aQuery->BeginTime());
    rv = statement->BindInt64Parameter(index.For("begin_time"), time);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // end time
  if (NS_SUCCEEDED(aQuery->GetHasEndTime(&hasIt)) && hasIt) {
    PRTime time = NormalizeTime(aQuery->EndTimeReference(),
                                aQuery->EndTime());
    rv = statement->BindInt64Parameter(index.For("end_time"), time);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // search terms FIXME

  // min and max visit count
  PRInt32 visits = aQuery->MinVisits();
  if (visits >= 0) {
    rv = statement->BindInt32Parameter(index.For("min_visits"), visits);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  visits = aQuery->MaxVisits();
  if (visits >= 0) {
    rv = statement->BindInt32Parameter(index.For("max_visits"), visits);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // domain (see GetReversedHostname for more info on reversed host names)
  if (NS_SUCCEEDED(aQuery->GetHasDomain(&hasIt)) && hasIt) {
    nsString revDomain;
    GetReversedHostname(NS_ConvertUTF8toUTF16(aQuery->Domain()), revDomain);

    if (aQuery->DomainIsHost()) {
      rv = statement->BindStringParameter(index.For("domain_lower"), revDomain);
      NS_ENSURE_SUCCESS(rv, rv);
    } else {
      // for "mozilla.org" do query >= "gro.allizom." AND < "gro.allizom/"
      // which will get everything starting with "gro.allizom." while using the
      // index (using SUBSTRING() causes indexes to be discarded).
      NS_ASSERTION(revDomain[revDomain.Length() - 1] == '.', "Invalid rev. host");
      rv = statement->BindStringParameter(index.For("domain_lower"), revDomain);
      NS_ENSURE_SUCCESS(rv, rv);
      revDomain.Truncate(revDomain.Length() - 1);
      revDomain.Append(PRUnichar('/'));
      rv = statement->BindStringParameter(index.For("domain_upper"), revDomain);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  // URI
  if (aQuery->Uri()) {
    BindStatementURI(statement, index.For("uri"), aQuery->Uri());
    if (aQuery->UriIsPrefix()) {
      nsCAutoString uriString;
      aQuery->Uri()->GetSpec(uriString);
      uriString.Append(char(0x7F)); // MAX_UTF8
      rv = BindStatementURLCString(statement, index.For("uri_upper"), uriString);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  // annotation
  if (!aQuery->Annotation().IsEmpty()) {
    rv = statement->BindUTF8StringParameter(index.For("anno"), 
                                            aQuery->Annotation());
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // tags
  const nsTArray<nsString> &tags = aQuery->Tags();
  if (tags.Length() > 0) {
    for (PRUint32 i = 0; i < tags.Length(); ++i) {
      nsPrintfCString param("tag%d_", i);
      NS_ConvertUTF16toUTF8 tag(tags[i]);
      rv = statement->BindUTF8StringParameter(index.For(param.get()), tag);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    PRInt64 tagsFolder = GetTagsFolder();
    rv = statement->BindInt64Parameter(index.For("tags_folder"), tagsFolder);
    NS_ENSURE_SUCCESS(rv, rv);
    if (!aQuery->TagsAreNot()) {
      rv = statement->BindInt32Parameter(index.For("tag_count"), tags.Length());
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  // parent parameter
  if (aOptions->ResultType() == nsINavHistoryQueryOptions::RESULTS_AS_TAG_CONTENTS &&
      aQuery->Folders().Length() == 1) {
    rv = statement->BindInt64Parameter(index.For("parent"),
                                       aQuery->Folders()[0]);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  NS_ENSURE_SUCCESS(index.Result(), index.Result());

  return NS_OK;
}


// nsNavHistory::ResultsAsList
//

nsresult
nsNavHistory::ResultsAsList(mozIStorageStatement* statement,
                            nsNavHistoryQueryOptions* aOptions,
                            nsCOMArray<nsNavHistoryResultNode>* aResults)
{
  nsresult rv;
  nsCOMPtr<mozIStorageValueArray> row = do_QueryInterface(statement, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool hasMore = PR_FALSE;
  while (NS_SUCCEEDED(statement->ExecuteStep(&hasMore)) && hasMore) {
    nsRefPtr<nsNavHistoryResultNode> result;
    rv = RowToResult(row, aOptions, getter_AddRefs(result));
    NS_ENSURE_SUCCESS(rv, rv);
    aResults->AppendObject(result);
  }
  return NS_OK;
}

static PRInt64
GetAgeInDays(PRTime aNormalizedNow, PRTime aDate)
{
  PRTime dateMidnight = NormalizeTimeRelativeToday(aDate);
  // if the visit time is in the future
  // treat as "today" see bug #385867
  if (dateMidnight > aNormalizedNow)
    return 0;
  else
    return ((aNormalizedNow - dateMidnight) / USECS_PER_DAY);
}

const PRInt64 UNDEFINED_URN_VALUE = -1;

// Create a urn (like
// urn:places-persist:place:group=0&group=1&sort=1&type=1,,%28local%20files%29)
// to be used to persist the open state of this container in localstore.rdf
nsresult
CreatePlacesPersistURN(nsNavHistoryQueryResultNode *aResultNode, 
                      PRInt64 aValue, const nsCString& aTitle, nsCString& aURN)
{
  nsCAutoString uri;
  nsresult rv = aResultNode->GetUri(uri);
  NS_ENSURE_SUCCESS(rv, rv);

  aURN.Assign(NS_LITERAL_CSTRING("urn:places-persist:"));
  aURN.Append(uri);

  aURN.Append(NS_LITERAL_CSTRING(","));
  if (aValue != UNDEFINED_URN_VALUE)
    aURN.AppendInt(aValue);

  aURN.Append(NS_LITERAL_CSTRING(","));
  if (!aTitle.IsEmpty()) {
    nsCAutoString escapedTitle;
    PRBool success = NS_Escape(aTitle, escapedTitle, url_XAlphas);
    NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);
    aURN.Append(escapedTitle);
  }

  return NS_OK;
}

PRInt64
nsNavHistory::GetTagsFolder()
{
  // cache our tags folder
  // note, we can't do this in nsNavHistory::Init(), 
  // as getting the bookmarks service would initialize it.
  if (mTagsFolder == -1) {
    nsNavBookmarks *bookmarks = nsNavBookmarks::GetBookmarksService();
    NS_ENSURE_TRUE(bookmarks, -1);
    
    nsresult rv = bookmarks->GetTagsFolder(&mTagsFolder);
    NS_ENSURE_SUCCESS(rv, -1);
  }
  return mTagsFolder;
}

// nsNavHistory::FilterResultSet
//
// This does some post-query-execution filtering:
//   - searching on title & url
//   - parent folder (recursively)
//   - excludeQueries
//   - tags
//   - limit count
//   - excludingLivemarkItems
//
// Note:  changes to filtering in FilterResultSet() 
// may require changes to NeedToFilterResultSet()

nsresult
nsNavHistory::FilterResultSet(nsNavHistoryQueryResultNode* aQueryNode,
                              const nsCOMArray<nsNavHistoryResultNode>& aSet,
                              nsCOMArray<nsNavHistoryResultNode>* aFiltered,
                              const nsCOMArray<nsNavHistoryQuery>& aQueries,
                              nsNavHistoryQueryOptions *aOptions)
{
  nsresult rv;

  // get the bookmarks service
  nsNavBookmarks *bookmarks = nsNavBookmarks::GetBookmarksService();
  NS_ENSURE_TRUE(bookmarks, NS_ERROR_OUT_OF_MEMORY);

  // parse the search terms
  nsTArray<nsTArray<nsString>*> terms;
  ParseSearchTermsFromQueries(aQueries, &terms);

  // The includeFolders array for each query is initialized with its
  // query's folders array. We add sub-folders as we check items.
  nsTArray< nsTArray<PRInt64>* > includeFolders;
  nsTArray< nsTArray<PRInt64>* > excludeFolders;
  for (PRInt32 queryIndex = 0;
       queryIndex < aQueries.Count(); queryIndex++) {
    includeFolders.AppendElement(new nsTArray<PRInt64>(aQueries[queryIndex]->Folders()));
    excludeFolders.AppendElement(new nsTArray<PRInt64>());
  }

  // Filter against query options.
  // XXX Only excludeQueries and excludeItemIfParentHasAnnotation are supported
  // at the moment.
  PRBool excludeQueries = PR_FALSE;
  if (aQueryNode) {
    rv = aQueryNode->mOptions->GetExcludeQueries(&excludeQueries);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsCString parentAnnotationToExclude;
  nsTArray<PRInt64> parentFoldersToExclude;
  if (aQueryNode) {
    rv = aQueryNode->mOptions->GetExcludeItemIfParentHasAnnotation(parentAnnotationToExclude);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (!parentAnnotationToExclude.IsEmpty()) {
    // Find all the folders with the annotation we are excluding and save their
    // item ids.  When doing filtering, if item id of a result's parent
    // matches one of the saved item ids, the result will be excluded.
    mozStorageStatementScoper scope(mDBGetItemsWithAnno);

    rv = mDBGetItemsWithAnno->BindUTF8StringParameter(0, parentAnnotationToExclude);
    NS_ENSURE_SUCCESS(rv, rv);

    PRBool hasMore = PR_FALSE;
    while (NS_SUCCEEDED(mDBGetItemsWithAnno->ExecuteStep(&hasMore)) && hasMore) {
      PRInt64 folderId = 0;
      rv = mDBGetItemsWithAnno->GetInt64(0, &folderId);
      NS_ENSURE_SUCCESS(rv, rv);
      parentFoldersToExclude.AppendElement(folderId);
    }
  }

  PRUint16 resultType = aOptions->ResultType();
  for (PRInt32 nodeIndex = 0; nodeIndex < aSet.Count(); nodeIndex++) {
    // exclude-queries is implicit when searching, we're only looking at
    // plan URI nodes
    if (!aSet[nodeIndex]->IsURI())
      continue;

    // RESULTS_AS_TAG_CONTENTS returns a set ordered by place_id and
    // lastModified. So, to remove duplicates, we can retain the first result
    // for each uri.
    if (resultType == nsINavHistoryQueryOptions::RESULTS_AS_TAG_CONTENTS &&
        nodeIndex > 0 && aSet[nodeIndex]->mURI == aSet[nodeIndex-1]->mURI)
      continue;

    PRInt64 parentId = -1;
    if (aSet[nodeIndex]->mItemId != -1) {
      if (aQueryNode && aQueryNode->mItemId == aSet[nodeIndex]->mItemId)
        continue;
      parentId = aSet[nodeIndex]->mFolderId;
    }

    // if we are excluding items by parent annotation, 
    // exclude items who's parent is a folder with that annotation
    if (!parentAnnotationToExclude.IsEmpty() &&
        parentFoldersToExclude.Contains(parentId))
      continue;

    // Append the node only if it matches one of the queries.
    PRBool appendNode = PR_FALSE;
    for (PRInt32 queryIndex = 0;
         queryIndex < aQueries.Count() && !appendNode; queryIndex++) {

      if (terms[queryIndex]->Length()) {
        // Filter based on search terms.
        // Convert title and url for the current node to UTF16 strings.
        NS_ConvertUTF8toUTF16 nodeTitle(aSet[nodeIndex]->mTitle);
        // Unescape the URL for search terms matching.
        nsCAutoString cNodeURL(aSet[nodeIndex]->mURI);
        NS_ConvertUTF8toUTF16 nodeURL(NS_UnescapeURL(cNodeURL));

        // Determine if every search term matches anywhere in the title, url or
        // tag.
        PRBool matchAll = PR_TRUE;
        for (PRInt32 termIndex = terms[queryIndex]->Length() - 1;
             termIndex >= 0 && matchAll;
             termIndex--) {
          nsString& term = terms[queryIndex]->ElementAt(termIndex);

          // True if any of them match; false makes us quit the loop
          matchAll = CaseInsensitiveFindInReadable(term, nodeTitle) ||
                     CaseInsensitiveFindInReadable(term, nodeURL) ||
                     CaseInsensitiveFindInReadable(term, aSet[nodeIndex]->mTags);
        }

        // Skip the node if we don't match all terms in the title, url or tag
        if (!matchAll)
          continue;
      }

      // Filter bookmarks on parent folder.
      // RESULTS_AS_TAG_CONTENTS changes bookmarks' parents, so we cannot filter
      // this kind of result based on the parent.
      if (includeFolders[queryIndex]->Length() != 0 &&
          resultType != nsINavHistoryQueryOptions::RESULTS_AS_TAG_CONTENTS) {
        // Filter out the node if its parent is in the excludeFolders
        // cache.
        if (excludeFolders[queryIndex]->Contains(parentId))
          continue;

        if (!includeFolders[queryIndex]->Contains(parentId)) {
          // If parent is not found in current includeFolders cache, we check
          // its ancestors.
          PRInt64 ancestor = parentId;
          PRBool belongs = PR_FALSE;
          nsTArray<PRInt64> ancestorFolders;

          while (!belongs) {
            // Avoid using |ancestor| itself if GetFolderIdForItem failed.
            ancestorFolders.AppendElement(ancestor);

            // GetFolderIdForItems throws when called for the places-root
            if (NS_FAILED(bookmarks->GetFolderIdForItem(ancestor, &ancestor))) {
              break;
            } else if (excludeFolders[queryIndex]->Contains(ancestor)) {
              break;
            } else if (includeFolders[queryIndex]->Contains(ancestor)) {
              belongs = PR_TRUE;
            }
          }
          // if the parentId or any of its ancestors "belong",
          // include all of them.  otherwise, exclude all of them.
          if (belongs) {
            includeFolders[queryIndex]->AppendElements(ancestorFolders);
          } else {
            excludeFolders[queryIndex]->AppendElements(ancestorFolders);
            continue;
          }
        }
      }

      // We passed all filters, so we can append the node to filtered results.
      appendNode = PR_TRUE;
    }

    if (appendNode)
      aFiltered->AppendObject(aSet[nodeIndex]);
      
    // Stop once we have reached max results.
    if (aOptions->MaxResults() > 0 &&
        (PRUint32)aFiltered->Count() >= aOptions->MaxResults())
      break;
  }

  // De-allocate the temporary matrixes.
  for (PRInt32 i = 0; i < aQueries.Count(); i++) {
    delete terms[i];
    delete includeFolders[i];
    delete excludeFolders[i];
  }

  return NS_OK;
}


// nsNavHistory::CheckIsRecentEvent
//
//    Sees if this URL happened "recently."
//
//    It is always removed from our recent list no matter what. It only counts
//    as "recent" if the event happened more recently than our event
//    threshold ago.

PRBool
nsNavHistory::CheckIsRecentEvent(RecentEventHash* hashTable,
                                 const nsACString& url)
{
  PRTime eventTime;
  if (hashTable->Get(url, &eventTime)) {
    hashTable->Remove(url);
    if (eventTime > GetNow() - RECENT_EVENT_THRESHOLD)
      return PR_TRUE;
    return PR_FALSE;
  }
  return PR_FALSE;
}


// nsNavHistory::ExpireNonrecentEvents
//
//    This goes through our

static PLDHashOperator
ExpireNonrecentEventsCallback(nsCStringHashKey::KeyType aKey,
                              PRInt64& aData,
                              void* userArg)
{
  PRInt64* threshold = reinterpret_cast<PRInt64*>(userArg);
  if (aData < *threshold)
    return PL_DHASH_REMOVE;
  return PL_DHASH_NEXT;
}
void
nsNavHistory::ExpireNonrecentEvents(RecentEventHash* hashTable)
{
  PRInt64 threshold = GetNow() - RECENT_EVENT_THRESHOLD;
  hashTable->Enumerate(ExpireNonrecentEventsCallback,
                       reinterpret_cast<void*>(&threshold));
}


// nsNavHistory::GetRedirectFor
//
//    Given a destination URI, this finds a recent redirect that resulted in
//    this URI. If it finds one, it will put the redirect source info into
//    the out params and return true. If there is no matching redirect, it will
//    return false.
//
//    @param aDestination The destination URI spec of the redirect to look for.
//    @param aSource      Will be filled with the redirect source URI when a
//                        redirect is found.
//    @param aTime        Will be filled with the time the redirect happened
//                         when a redirect is found.
//    @param aRedirectType Will be filled with the redirect type when a redirect
//                         is found. Will be either
//                         TRANSITION_REDIRECT_PERMANENT or
//                         TRANSITION_REDIRECT_TEMPORARY
//    @returns True if the redirect is found.
//
//    HOW REDIRECT TRACKING WORKS
//    ---------------------------
//    When we get an AddDocumentRedirect message, we store the redirect in
//    our mRecentRedirects which maps the destination URI to a source,time pair.
//    When we get a new URI, we see if there were any redirects to this page
//    in the hash table. If found, we know that the page came through the given
//    redirect and add it.
//
//    Example: Page S redirects throught R1, then R2, to give page D. Page S
//    will have been already added to history.
//    - AddDocumentRedirect(R1, R2)
//    - AddDocumentRedirect(R2, D)
//    - AddURI(uri=D, referrer=S)
//
//    When we get the AddURI(D), we see the hash table has a value for D from R2.
//    We have to recursively check that source since there could be more than
//    one redirect, as in this case. Here we see there was a redirect to R2 from
//    R1. The referrer for D is S, so we know S->R1->R2->D.
//
//    Alternatively, the user could have typed or followed a bookmark from S.
//    In this case, with two redirects we'll get:
//    - MarkPageAsTyped(S)
//    - AddDocumentRedirect(S, R)
//    - AddDocumentRedirect(R, D)
//    - AddURI(uri=D, referrer=null)
//    We need to be careful to add a visit to S in this case with an incoming
//    transition of typed and an outgoing transition of redirect.
//
//    Note that this can get confused in some cases where you have a page
//    open in more than one window loading at the same time. This should be rare,
//    however, and should not affect much.

PRBool
nsNavHistory::GetRedirectFor(const nsACString& aDestination,
                             nsACString& aSource, PRTime* aTime,
                             PRUint32* aRedirectType)
{
  RedirectInfo info;
  if (mRecentRedirects.Get(aDestination, &info)) {
    mRecentRedirects.Remove(aDestination);
    if (info.mTimeCreated < GetNow() - RECENT_EVENT_THRESHOLD)
      return PR_FALSE; // too long ago, probably invalid
    aSource = info.mSourceURI;
    *aTime = info.mTimeCreated;
    *aRedirectType = info.mType;
    return PR_TRUE;
  }
  return PR_FALSE;
}


// nsNavHistory::RowToResult
//
//    Here, we just have a generic row. It could be a query, URL, visit,
//    or full visit.

nsresult
nsNavHistory::RowToResult(mozIStorageValueArray* aRow,
                          nsNavHistoryQueryOptions* aOptions,
                          nsNavHistoryResultNode** aResult)
{
  *aResult = nsnull;
  NS_ASSERTION(aRow && aOptions && aResult, "Null pointer in RowToResult");

  // URL
  nsCAutoString url;
  nsresult rv = aRow->GetUTF8String(kGetInfoIndex_URL, url);
  NS_ENSURE_SUCCESS(rv, rv);

  // title
  nsCAutoString title;
  rv = aRow->GetUTF8String(kGetInfoIndex_Title, title);
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 accessCount = aRow->AsInt32(kGetInfoIndex_VisitCount);
  PRTime time = aRow->AsInt64(kGetInfoIndex_VisitDate);

  // favicon
  nsCAutoString favicon;
  rv = aRow->GetUTF8String(kGetInfoIndex_FaviconURL, favicon);
  NS_ENSURE_SUCCESS(rv, rv);

  // itemId
  PRInt64 itemId = aRow->AsInt64(kGetInfoIndex_ItemId);
  PRInt64 parentId = -1;
  if (itemId == 0) {
    // This is not a bookmark.  For non-bookmarks we use a -1 itemId value.
    // Notice ids in sqlite tables start from 1, so itemId cannot ever be 0.
    itemId = -1;
  }
  else {
    // This is a bookmark, so it has a parent.
    PRInt64 itemParentId = aRow->AsInt64(kGetInfoIndex_ItemParentId);
    if (itemParentId > 0) {
      // The Places root has parent == 0, but that item id does not really
      // exist. We want to set the parent only if it's a real one.
      parentId = itemParentId;
    }
  }

  if (IsQueryURI(url)) {
    // special case "place:" URIs: turn them into containers
      
    // We should never expose the history title for query nodes if the
    // bookmark-item's title is set to null (the history title may be the
    // query string without the place: prefix). Thus we call getItemTitle
    // explicitly. Doing this in the SQL query would be less performant since
    // it should be done for all results rather than only for queries.
    if (itemId != -1) {
      nsNavBookmarks *bookmarks = nsNavBookmarks::GetBookmarksService();
      NS_ENSURE_TRUE(bookmarks, NS_ERROR_OUT_OF_MEMORY);

      rv = bookmarks->GetItemTitle(itemId, title);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    rv = QueryRowToResult(itemId, url, title, accessCount, time, favicon, aResult);

    // If it's a simple folder node (i.e. a shortcut to another folder), apply
    // our options for it. However, if the parent type was tag query, we do not
    // apply them, because it would not yield any results.
    if (*aResult && (*aResult)->IsFolder() &&
         aOptions->ResultType() != 
           nsINavHistoryQueryOptions::RESULTS_AS_TAG_QUERY)
      (*aResult)->GetAsContainer()->mOptions = aOptions;

    // RESULTS_AS_TAG_QUERY has date columns
    if (aOptions->ResultType() == nsNavHistoryQueryOptions::RESULTS_AS_TAG_QUERY) {
      (*aResult)->mDateAdded = aRow->AsInt64(kGetInfoIndex_ItemDateAdded);
      (*aResult)->mLastModified = aRow->AsInt64(kGetInfoIndex_ItemLastModified);
    }

    return rv;
  } else if (aOptions->ResultType() == nsNavHistoryQueryOptions::RESULTS_AS_URI ||
             aOptions->ResultType() == nsNavHistoryQueryOptions::RESULTS_AS_TAG_CONTENTS) {
    *aResult = new nsNavHistoryResultNode(url, title, accessCount, time,
                                          favicon);
    if (!*aResult)
      return NS_ERROR_OUT_OF_MEMORY;

    if (itemId != -1) {
      (*aResult)->mItemId = itemId;
      (*aResult)->mFolderId = parentId;
      (*aResult)->mDateAdded = aRow->AsInt64(kGetInfoIndex_ItemDateAdded);
      (*aResult)->mLastModified = aRow->AsInt64(kGetInfoIndex_ItemLastModified);
    }

    nsAutoString tags;
    rv = aRow->GetString(kGetInfoIndex_ItemTags, tags);
    NS_ENSURE_SUCCESS(rv, rv);
    if (!tags.IsVoid())
      (*aResult)->mTags.Assign(tags);

    NS_ADDREF(*aResult);
    return NS_OK;
  }
  // now we know the result type is some kind of visit (regular or full)

  // session
  PRInt64 session = aRow->AsInt64(kGetInfoIndex_SessionId);

  if (aOptions->ResultType() == nsNavHistoryQueryOptions::RESULTS_AS_VISIT) {
    *aResult = new nsNavHistoryVisitResultNode(url, title, accessCount, time,
                                               favicon, session);
    if (! *aResult)
      return NS_ERROR_OUT_OF_MEMORY;

    nsAutoString tags;
    rv = aRow->GetString(kGetInfoIndex_ItemTags, tags);
    if (!tags.IsVoid())
      (*aResult)->mTags.Assign(tags);

    NS_ADDREF(*aResult);
    return NS_OK;
  }

  return NS_ERROR_FAILURE;
}


// nsNavHistory::QueryRowToResult
//
//    Called by RowToResult when the URI is a place: URI to generate the proper
//    folder or query node.

nsresult
nsNavHistory::QueryRowToResult(PRInt64 itemId, const nsACString& aURI,
                               const nsACString& aTitle,
                               PRUint32 aAccessCount, PRTime aTime,
                               const nsACString& aFavicon,
                               nsNavHistoryResultNode** aNode)
{
  nsCOMArray<nsNavHistoryQuery> queries;
  nsCOMPtr<nsNavHistoryQueryOptions> options;
  nsresult rv = QueryStringToQueryArray(aURI, &queries,
                                        getter_AddRefs(options));
  if (NS_FAILED(rv)) {
    // This was a query that did not parse, what do we do? We don't want to
    // return failure since that will kill the whole query process. Instead
    // make a query node with the query as a string. This way we have a valid
    // node for the user to manipulate that will look like a query, but it will
    // never populate since the query string is invalid.
    *aNode = new nsNavHistoryQueryResultNode(aURI, aTitle, aFavicon);
    if (! *aNode)
      return NS_ERROR_OUT_OF_MEMORY;
    NS_ADDREF(*aNode);
  } else {
    PRInt64 folderId = GetSimpleBookmarksQueryFolder(queries, options);
    if (folderId) {
      // simple bookmarks folder, magically generate a bookmarks folder node
      nsNavBookmarks *bookmarks = nsNavBookmarks::GetBookmarksService();
      NS_ENSURE_TRUE(bookmarks, NS_ERROR_OUT_OF_MEMORY);

      // this addrefs for us
      rv = bookmarks->ResultNodeForContainer(folderId, options, aNode);
      NS_ENSURE_SUCCESS(rv, rv);

      // this is the query item-Id, and is what is exposed by node.itemId
      (*aNode)->GetAsFolder()->mQueryItemId = itemId;

      // Use the query item title, unless it's void (in that case,
      // we keep the concrete folder title set)
      if (!aTitle.IsVoid())
        (*aNode)->mTitle = aTitle;
    } else {
      // regular query
      *aNode = new nsNavHistoryQueryResultNode(aTitle, EmptyCString(), aTime,
                                               queries, options);
      if (! *aNode)
        return NS_ERROR_OUT_OF_MEMORY;
      (*aNode)->mItemId = itemId;
      NS_ADDREF(*aNode);
    }
  }
  return NS_OK;
}


// nsNavHistory::VisitIdToResultNode
//
//    Used by the query results to create new nodes on the fly when
//    notifications come in. This just creates a node for the given visit ID.

nsresult
nsNavHistory::VisitIdToResultNode(PRInt64 visitId,
                                  nsNavHistoryQueryOptions* aOptions,
                                  nsNavHistoryResultNode** aResult)
{
  mozIStorageStatement* statement; // non-owning!

  switch (aOptions->ResultType())
  {
    case nsNavHistoryQueryOptions::RESULTS_AS_VISIT:
    case nsNavHistoryQueryOptions::RESULTS_AS_FULL_VISIT:
      // visit query - want exact visit time
      statement = GetDBVisitToVisitResult();
      break;

    case nsNavHistoryQueryOptions::RESULTS_AS_URI:
      // URL results - want last visit time
      statement = GetDBVisitToURLResult();
      break;

    default:
      // Query base types like RESULTS_AS_*_QUERY handle additions
      // by registering their own observers when they are expanded.
      return NS_OK;
  }
  NS_ENSURE_STATE(statement);

  mozStorageStatementScoper scoper(statement);
  nsresult rv = statement->BindInt64Parameter(0, visitId);
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool hasMore = PR_FALSE;
  rv = statement->ExecuteStep(&hasMore);
  NS_ENSURE_SUCCESS(rv, rv);
  if (! hasMore) {
    NS_NOTREACHED("Trying to get a result node for an invalid visit");
    return NS_ERROR_INVALID_ARG;
  }

  return RowToResult(statement, aOptions, aResult);
}

nsresult
nsNavHistory::BookmarkIdToResultNode(PRInt64 aBookmarkId, nsNavHistoryQueryOptions* aOptions,
                                     nsNavHistoryResultNode** aResult)
{
  mozIStorageStatement *stmt = GetDBBookmarkToUrlResult();
  NS_ENSURE_STATE(stmt);
  mozStorageStatementScoper scoper(stmt);
  nsresult rv = stmt->BindInt64Parameter(0, aBookmarkId);
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool hasMore = PR_FALSE;
  rv = stmt->ExecuteStep(&hasMore);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!hasMore) {
    NS_NOTREACHED("Trying to get a result node for an invalid bookmark identifier");
    return NS_ERROR_INVALID_ARG;
  }

  return RowToResult(stmt, aOptions, aResult);
}

void
nsNavHistory::SendPageChangedNotification(nsIURI* aURI, PRUint32 aWhat,
                                          const nsAString& aValue)
{
  ENUMERATE_OBSERVERS(mCanNotify, mCacheObservers, mObservers, nsINavHistoryObserver,
                      OnPageChanged(aURI, aWhat, aValue));
}

// nsNavHistory::TitleForDomain
//
//    This computes the title for a given domain. Normally, this is just the
//    domain name, but we specially handle empty cases to give you a nice
//    localized string.

void
nsNavHistory::TitleForDomain(const nsCString& domain, nsACString& aTitle)
{
  if (! domain.IsEmpty()) {
    aTitle = domain;
    return;
  }

  // use the localized one instead
  GetStringFromName(NS_LITERAL_STRING("localhost").get(), aTitle);
}

void
nsNavHistory::GetAgeInDaysString(PRInt32 aInt, const PRUnichar *aName,
                                 nsACString& aResult)
{
  nsIStringBundle *bundle = GetBundle();
  if (!bundle)
    aResult.Truncate(0);
  else {
    nsAutoString intString;
    intString.AppendInt(aInt);
    const PRUnichar* strings[1] = { intString.get() };
    nsXPIDLString value;
    nsresult rv = bundle->FormatStringFromName(aName, strings,
                                               1, getter_Copies(value));
    if (NS_SUCCEEDED(rv))
      CopyUTF16toUTF8(value, aResult);
    else
      aResult.Truncate(0);
  }
}

void
nsNavHistory::GetStringFromName(const PRUnichar *aName, nsACString& aResult)
{
  nsIStringBundle *bundle = GetBundle();
  if (!bundle)
    aResult.Truncate(0);

  nsXPIDLString value;
  nsresult rv = bundle->GetStringFromName(aName, getter_Copies(value));
  if (NS_SUCCEEDED(rv))
    CopyUTF16toUTF8(value, aResult);
  else
    aResult.Truncate(0);
}

void
nsNavHistory::GetMonthName(PRInt32 aIndex, nsACString& aResult)
{
  nsIStringBundle *bundle = GetDateFormatBundle();
  if (!bundle)
    aResult.Truncate(0);
  else {
    nsCString name = nsPrintfCString("month.%d.name", aIndex);
    nsXPIDLString value;
    nsresult rv = bundle->GetStringFromName(NS_ConvertUTF8toUTF16(name).get(),
                                            getter_Copies(value));
    if (NS_SUCCEEDED(rv))
      CopyUTF16toUTF8(value, aResult);
    else
      aResult.Truncate(0);
  }
}

// nsNavHistory::SetPageTitleInternal
//
//    Called to set the title for the given URI. Used as a
//    backend for SetTitle.
//
//    Will fail for pages that are not in the DB. To clear the corresponding
//    title, use aTitle.SetIsVoid(). Sending an empty string will save an
//    empty string instead of clearing it.

nsresult
nsNavHistory::SetPageTitleInternal(nsIURI* aURI, const nsAString& aTitle)
{
  nsresult rv;

  // first, make sure the page exists, and fetch the old title (we need the one
  // that isn't changing to send notifications)
  nsAutoString title;
  { // scope for statement
    mozStorageStatementScoper infoScoper(mDBGetURLPageInfo);
    rv = BindStatementURI(mDBGetURLPageInfo, 0, aURI);
    NS_ENSURE_SUCCESS(rv, rv);
    PRBool hasURL = PR_FALSE;
    rv = mDBGetURLPageInfo->ExecuteStep(&hasURL);
    NS_ENSURE_SUCCESS(rv, rv);
    if (! hasURL) {
      // we don't have the URL, give up
      return NS_ERROR_NOT_AVAILABLE;
    }

    // page title
    rv = mDBGetURLPageInfo->GetString(kGetInfoIndex_Title, title);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // It is actually common to set the title to be the same thing it used to
  // be. For example, going to any web page will always cause a title to be set,
  // even though it will often be unchanged since the last visit. In these
  // cases, we can avoid DB writing and (most significantly) observer overhead.
  if ((aTitle.IsVoid() && title.IsVoid()) || aTitle == title)
    return NS_OK;

  mozStorageStatementScoper scoper(mDBSetPlaceTitle);
  // title
  if (aTitle.IsVoid())
    rv = mDBSetPlaceTitle->BindNullParameter(0);
  else
    rv = mDBSetPlaceTitle->BindStringParameter(0, StringHead(aTitle, HISTORY_TITLE_LENGTH_MAX));
  NS_ENSURE_SUCCESS(rv, rv);

  // url
  rv = BindStatementURI(mDBSetPlaceTitle, 1, aURI);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBSetPlaceTitle->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  // observers (have to check first if it's bookmarked)
  ENUMERATE_OBSERVERS(mCanNotify, mCacheObservers, mObservers, nsINavHistoryObserver,
                      OnTitleChanged(aURI, aTitle));

  return NS_OK;
}

nsresult
nsNavHistory::AddPageWithVisits(nsIURI *aURI,
                                const nsString &aTitle,
                                PRInt32 aVisitCount,
                                PRInt32 aTransitionType,
                                PRTime aFirstVisitDate,
                                PRTime aLastVisitDate)
{
  PRBool canAdd = PR_FALSE;
  nsresult rv = CanAddURI(aURI, &canAdd);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!canAdd) {
    return NS_OK;
  }

  // see if this is an update (revisit) or a new page
  mozStorageStatementScoper scoper(mDBGetPageVisitStats);
  rv = BindStatementURI(mDBGetPageVisitStats, 0, aURI);
  NS_ENSURE_SUCCESS(rv, rv);
  PRBool alreadyVisited = PR_FALSE;
  rv = mDBGetPageVisitStats->ExecuteStep(&alreadyVisited);
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt64 placeId = 0;
  PRInt32 typed = 0;
  PRInt32 hidden = 0;

  if (alreadyVisited) {
    // Update the existing entry
    rv = mDBGetPageVisitStats->GetInt64(0, &placeId);
    NS_ENSURE_SUCCESS(rv, rv);
    // We don't mind visit_count
    rv = mDBGetPageVisitStats->GetInt32(2, &typed);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mDBGetPageVisitStats->GetInt32(3, &hidden);
    NS_ENSURE_SUCCESS(rv, rv);

    if (typed == 0 && aTransitionType == TRANSITION_TYPED) {
      typed = 1;
      // Update with new stats
      mozStorageStatementScoper updateScoper(mDBUpdatePageVisitStats);
      rv = mDBUpdatePageVisitStats->BindInt64Parameter(0, placeId);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = mDBUpdatePageVisitStats->BindInt32Parameter(1, hidden);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = mDBUpdatePageVisitStats->BindInt32Parameter(2, typed);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = mDBUpdatePageVisitStats->Execute();
      NS_ENSURE_SUCCESS(rv, rv);
    }
  } else {
    // Insert the new place entry
    rv = InternalAddNewPage(aURI, aTitle, hidden == 1,
                            aTransitionType == TRANSITION_TYPED, 0,
                            PR_FALSE, &placeId);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  NS_ASSERTION(placeId != 0, "Cannot add a visit to a not existant page");

  if (aFirstVisitDate != -1) {
    // Add the first visit
    PRInt64 visitId;
    rv = InternalAddVisit(placeId, 0, 0,
                          aFirstVisitDate, aTransitionType, &visitId);
    aVisitCount--;
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (aLastVisitDate != -1) {
   // Add remaining visits starting from the last one
   for (PRInt64 i = 0; i < aVisitCount; i++) {
      PRInt64 visitId;
      rv = InternalAddVisit(placeId, 0, 0,
                            aLastVisitDate - i, aTransitionType, &visitId);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  return NS_OK;
}

nsresult
nsNavHistory::RemoveDuplicateURIs()
{
  // this must be in a transaction because we do related queries
  mozStorageTransaction transaction(mDBConn, PR_FALSE);

  // this query chooses an id for every duplicate uris
  // this id will be retained while duplicates will be discarded
  // total_visit_count is the sum of all duplicate uris visit_count
  nsCOMPtr<mozIStorageStatement> selectStatement;
  nsresult rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT "
        "(SELECT h.id FROM moz_places h WHERE h.url = url "
         "ORDER BY h.visit_count DESC LIMIT 1), "
        "url, SUM(visit_count) "
      "FROM moz_places "
      "GROUP BY url HAVING( COUNT(url) > 1)"),
    getter_AddRefs(selectStatement));
  NS_ENSURE_SUCCESS(rv, rv);

  // this query remaps history visits to the retained place_id
  nsCOMPtr<mozIStorageStatement> updateStatement;
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "UPDATE moz_historyvisits "
      "SET place_id = ?1 "
      "WHERE place_id IN "
        "(SELECT id FROM moz_places WHERE id <> ?1 AND url = ?2)"),
    getter_AddRefs(updateStatement));
  NS_ENSURE_SUCCESS(rv, rv);

  // this query remaps bookmarks to the retained place_id
  nsCOMPtr<mozIStorageStatement> bookmarkStatement;
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "UPDATE moz_bookmarks "
      "SET fk = ?1 "
      "WHERE fk IN "
        "(SELECT id FROM moz_places WHERE id <> ?1 AND url = ?2)"),
    getter_AddRefs(bookmarkStatement));
  NS_ENSURE_SUCCESS(rv, rv);

  // this query remaps annotations to the retained place_id
  nsCOMPtr<mozIStorageStatement> annoStatement;
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "UPDATE moz_annos "
      "SET place_id = ?1 "
      "WHERE place_id IN "
        "(SELECT id FROM moz_places WHERE id <> ?1 AND url = ?2)"),
    getter_AddRefs(annoStatement));
  NS_ENSURE_SUCCESS(rv, rv);
  
  // this query deletes all duplicate uris except the chosen id
  nsCOMPtr<mozIStorageStatement> deleteStatement;
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "DELETE FROM moz_places WHERE url = ?1 AND id <> ?2"),
    getter_AddRefs(deleteStatement));
  NS_ENSURE_SUCCESS(rv, rv);

  // this query updates visit_count to the sum of all visits
  nsCOMPtr<mozIStorageStatement> countStatement;
  rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "UPDATE moz_places SET visit_count = ?1 WHERE id = ?2"),
    getter_AddRefs(countStatement));
  NS_ENSURE_SUCCESS(rv, rv);

  // for each duplicate uri we update historyvisit and visit_count
  PRBool hasMore;
  while (NS_SUCCEEDED(selectStatement->ExecuteStep(&hasMore)) && hasMore) {
    PRUint64 id = selectStatement->AsInt64(0);
    nsCAutoString url;
    rv = selectStatement->GetUTF8String(1, url);
    NS_ENSURE_SUCCESS(rv, rv);
    PRUint64 visit_count = selectStatement->AsInt64(2);

    // update historyvisits so they are remapped to the retained uri
    rv = updateStatement->BindInt64Parameter(0, id);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = BindStatementURLCString(updateStatement, 1, url);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = updateStatement->Execute();
    NS_ENSURE_SUCCESS(rv, rv);

    // remap bookmarks to the retained id
    rv = bookmarkStatement->BindInt64Parameter(0, id);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = BindStatementURLCString(bookmarkStatement, 1, url);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = bookmarkStatement->Execute();
    NS_ENSURE_SUCCESS(rv, rv);

    // remap annotations to the retained id
    rv = annoStatement->BindInt64Parameter(0, id);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = BindStatementURLCString(annoStatement, 1, url);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = annoStatement->Execute();
    NS_ENSURE_SUCCESS(rv, rv);
    
    // remove duplicate uris from moz_places
    rv = BindStatementURLCString(deleteStatement, 0, url);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = deleteStatement->BindInt64Parameter(1, id);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = deleteStatement->Execute();
    NS_ENSURE_SUCCESS(rv, rv);

    // update visit_count to the sum of all visit_count
    rv = countStatement->BindInt64Parameter(0, visit_count);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = countStatement->BindInt64Parameter(1, id);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = countStatement->Execute();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = transaction.Commit();
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

// Local function **************************************************************


// GetReversedHostname
//
//    This extracts the hostname from the URI and reverses it in the
//    form that we use (always ending with a "."). So
//    "http://microsoft.com/" becomes "moc.tfosorcim."
//
//    The idea behind this is that we can create an index over the items in
//    the reversed host name column, and then query for as much or as little
//    of the host name as we feel like.
//
//    For example, the query "host >= 'gro.allizom.' AND host < 'gro.allizom/'
//    Matches all host names ending in '.mozilla.org', including
//    'developer.mozilla.org' and just 'mozilla.org' (since we define all
//    reversed host names to end in a period, even 'mozilla.org' matches).
//    The important thing is that this operation uses the index. Any substring
//    calls in a select statement (even if it's for the beginning of a string)
//    will bypass any indices and will be slow).

nsresult
GetReversedHostname(nsIURI* aURI, nsAString& aRevHost)
{
  nsCString forward8;
  nsresult rv = aURI->GetHost(forward8);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // can't do reversing in UTF8, better use 16-bit chars
  NS_ConvertUTF8toUTF16 forward(forward8);
  GetReversedHostname(forward, aRevHost);
  return NS_OK;
}


// GetReversedHostname
//
//    Same as previous but for strings

void
GetReversedHostname(const nsString& aForward, nsAString& aRevHost)
{
  ReverseString(aForward, aRevHost);
  aRevHost.Append(PRUnichar('.'));
}


// GetSimpleBookmarksQueryFolder
//
//    Determines if this set of queries is a simple bookmarks query for a
//    folder with no other constraints. In these common cases, we can more
//    efficiently compute the results.
//
//    A simple bookmarks query will result in a hierarchical tree of
//    bookmark items, folders and separators.
//
//    Returns the folder ID if it is a simple folder query, 0 if not.
static PRInt64
GetSimpleBookmarksQueryFolder(const nsCOMArray<nsNavHistoryQuery>& aQueries,
                              nsNavHistoryQueryOptions* aOptions)
{
  if (aQueries.Count() != 1)
    return 0;

  nsNavHistoryQuery* query = aQueries[0];
  if (query->Folders().Length() != 1)
    return 0;

  PRBool hasIt;
  query->GetHasBeginTime(&hasIt);
  if (hasIt)
    return 0;
  query->GetHasEndTime(&hasIt);
  if (hasIt)
    return 0;
  query->GetHasDomain(&hasIt);
  if (hasIt)
    return 0;
  query->GetHasUri(&hasIt);
  if (hasIt)
    return 0;
  (void)query->GetHasSearchTerms(&hasIt);
  if (hasIt)
    return 0;
  if (query->Tags().Length() > 0)
    return 0;
  if (aOptions->MaxResults() > 0)
    return 0;

  // RESULTS_AS_TAG_CONTENTS is quite similar to a folder shortcut, but we must
  // avoid treating it like that, since we need to retain all query options.
  if(aOptions->ResultType() == nsINavHistoryQueryOptions::RESULTS_AS_TAG_CONTENTS)
    return 0;

  // Note that we don't care about the onlyBookmarked flag, if you specify a bookmark
  // folder, onlyBookmarked is inferred.
  NS_ASSERTION(query->Folders()[0] > 0, "bad folder id");
  return query->Folders()[0];
}


// ParseSearchTermsFromQueries
//
//    Construct a matrix of search terms from the given queries array.
//    All of the query objects are ORed together. Within a query, all the terms
//    are ANDed together. See nsINavHistoryService.idl.
//
//    This just breaks the query up into words. We don't do anything fancy,
//    not even quoting. We do, however, strip quotes, because people might
//    try to input quotes expecting them to do something and get no results
//    back.

inline PRBool isQueryWhitespace(PRUnichar ch)
{
  return ch == ' ';
}

void ParseSearchTermsFromQueries(const nsCOMArray<nsNavHistoryQuery>& aQueries,
                                 nsTArray<nsTArray<nsString>*>* aTerms)
{
  PRInt32 lastBegin = -1;
  for (PRInt32 i = 0; i < aQueries.Count(); i++) {
    nsTArray<nsString> *queryTerms = new nsTArray<nsString>();
    PRBool hasSearchTerms;
    if (NS_SUCCEEDED(aQueries[i]->GetHasSearchTerms(&hasSearchTerms)) &&
        hasSearchTerms) {
      const nsString& searchTerms = aQueries[i]->SearchTerms();
      for (PRUint32 j = 0; j < searchTerms.Length(); j++) {
        if (isQueryWhitespace(searchTerms[j]) ||
            searchTerms[j] == '"') {
          if (lastBegin >= 0) {
            // found the end of a word
            queryTerms->AppendElement(Substring(searchTerms, lastBegin,
                                               j - lastBegin));
            lastBegin = -1;
          }
        } else {
          if (lastBegin < 0) {
            // found the beginning of a word
            lastBegin = j;
          }
        }
      }
      // last word
      if (lastBegin >= 0)
        queryTerms->AppendElement(Substring(searchTerms, lastBegin));
    }
    aTerms->AppendElement(queryTerms);
  }
}


// GenerateTitleFromURI
//
//    Given a URL, we try to get a reasonable title for this page. We try
//    to use a filename out of the URI, then fall back on the path, then fall
//    back on the whole hostname.

nsresult // static
GenerateTitleFromURI(nsIURI* aURI, nsAString& aTitle)
{
  nsCAutoString name;
  nsCOMPtr<nsIURL> url(do_QueryInterface(aURI));
  if (url)
    url->GetFileName(name);
  if (name.IsEmpty()) {
    // path
    nsresult rv = aURI->GetPath(name);
    if (NS_FAILED(rv) || (name.Length() == 1 && name[0] == '/')) {
      // empty path name, use hostname
      rv = aURI->GetHost(name);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }
  CopyUTF8toUTF16(name, aTitle);
  return NS_OK;
}


nsresult
BindStatementURI(mozIStorageStatement* statement, PRInt32 index, nsIURI* aURI)
{
  NS_ASSERTION(statement, "Must have non-null statement");
  NS_ASSERTION(aURI, "Must have non-null uri");

  nsCAutoString utf8URISpec;
  nsresult rv = aURI->GetSpec(utf8URISpec);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = BindStatementURLCString(statement, index, utf8URISpec);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}


nsresult
BindStatementURLCString(mozIStorageStatement* statement,
                        PRInt32 index,
                        const nsACString& aURLString)
{
  NS_ASSERTION(statement, "Must have non-null statement");

  nsresult rv = statement->BindUTF8StringParameter(
    index, StringHead(aURLString, HISTORY_URI_LENGTH_MAX));
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}


nsresult
nsNavHistory::UpdateFrecency(PRInt64 aPlaceId, PRBool aIsBookmarked)
{
  mozStorageStatementScoper statsScoper(mDBGetPlaceVisitStats);
  nsresult rv = mDBGetPlaceVisitStats->BindInt64Parameter(0, aPlaceId);
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool hasResults = PR_FALSE;
  rv = mDBGetPlaceVisitStats->ExecuteStep(&hasResults);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!hasResults) {
    NS_WARNING("attempting to update frecency for a bogus place");
    // before I added the check for itemType == TYPE_BOOKMARK
    // I hit this with aPlaceId of 0 (on import)
    return NS_OK;
  }

  PRInt32 typed = 0;
  rv = mDBGetPlaceVisitStats->GetInt32(0, &typed);
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 hidden = 0;
  rv = mDBGetPlaceVisitStats->GetInt32(1, &hidden);
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 oldFrecency = 0;
  rv = mDBGetPlaceVisitStats->GetInt32(2, &oldFrecency);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = UpdateFrecencyInternal(aPlaceId, typed, hidden, oldFrecency,
                              aIsBookmarked);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
nsNavHistory::UpdateFrecencyInternal(PRInt64 aPlaceId, PRInt32 aTyped,
  PRInt32 aHidden, PRInt32 aOldFrecency, PRBool aIsBookmarked)
{
  PRInt32 visitCountForFrecency = 0;

  // because visit_count excludes visit with visit_type NOT IN(0,4,7)
  // we can't use it for calculating frecency, so we must
  // calculate it.
  nsresult rv = CalculateFullVisitCount(aPlaceId, &visitCountForFrecency);
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 newFrecency = 0;
  rv = CalculateFrecencyInternal(aPlaceId, aTyped, visitCountForFrecency,
                                 aIsBookmarked, &newFrecency);
  NS_ENSURE_SUCCESS(rv, rv);

  // save ourselves the UPDATE if the frecency hasn't changed
  // One way this can happen is with livemarks.
  // when we added the livemark, the frecency was 0.  
  // On refresh, when we remove and then add the livemark items,
  // the frecency (for a given moz_places) will not have changed
  // (if we've never visited that place).
  // Additionally, don't bother overwriting a valid frecency with an invalid one
  if (newFrecency == aOldFrecency || aOldFrecency && newFrecency < 0)
    return NS_OK;

  mozStorageStatementScoper updateScoper(mDBUpdateFrecencyAndHidden);
  rv = mDBUpdateFrecencyAndHidden->BindInt64Parameter(0, aPlaceId);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBUpdateFrecencyAndHidden->BindInt32Parameter(1, newFrecency);
  NS_ENSURE_SUCCESS(rv, rv);

  // if we calculated a non-zero frecency we should unhide this place
  // so that previously hidden (non-livebookmark item) bookmarks 
  // will now appear in autocomplete
  // if we calculated a zero frecency, we re-use the old hidden value.
  rv = mDBUpdateFrecencyAndHidden->BindInt32Parameter(2, 
         newFrecency ? 0 /* not hidden */ : aHidden);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBUpdateFrecencyAndHidden->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
nsNavHistory::CalculateFrecencyInternal(PRInt64 aPlaceId,
                                        PRInt32 aTyped,
                                        PRInt32 aVisitCount,
                                        PRBool aIsBookmarked,
                                        PRInt32 *aFrecency)
{
  PRTime normalizedNow = NormalizeTimeRelativeToday(GetNow());

  float pointsForSampledVisits = 0.0;

  if (aPlaceId != -1) {
    PRInt32 numSampledVisits = 0;

    mozStorageStatementScoper scoper(mDBVisitsForFrecency);
    nsresult rv = mDBVisitsForFrecency->BindInt64Parameter(0, aPlaceId);
    NS_ENSURE_SUCCESS(rv, rv);

    // mDBVisitsForFrecency is limited by the browser.frecency.numVisits pref
    PRBool hasMore = PR_FALSE;
    while (NS_SUCCEEDED(mDBVisitsForFrecency->ExecuteStep(&hasMore)) 
           && hasMore) {
      numSampledVisits++;

      PRInt32 visitType = mDBVisitsForFrecency->AsInt32(1);

      PRInt32 bonus = 0;

      switch (visitType) {
        case nsINavHistoryService::TRANSITION_EMBED:
          bonus = mEmbedVisitBonus;
          break;
        case nsINavHistoryService::TRANSITION_LINK:
          bonus = mLinkVisitBonus;
          break;
        case nsINavHistoryService::TRANSITION_TYPED:
          bonus = mTypedVisitBonus;
          break;
        case nsINavHistoryService::TRANSITION_BOOKMARK:
          bonus = mBookmarkVisitBonus;
          break;
        case nsINavHistoryService::TRANSITION_DOWNLOAD:
          bonus = mDownloadVisitBonus;
          break;
        case nsINavHistoryService::TRANSITION_REDIRECT_PERMANENT:
          bonus = mPermRedirectVisitBonus;
          break;
        case nsINavHistoryService::TRANSITION_REDIRECT_TEMPORARY:
          bonus = mTempRedirectVisitBonus;
          break;
        default:
          // 0 == undefined (see bug #375777 for details)
          NS_WARN_IF_FALSE(!visitType, "new transition but no weight for frecency");
          bonus = mDefaultVisitBonus;
          break;
      }

      // Always add the bookmark visit bonus.
      if (aIsBookmarked)
        bonus += mBookmarkVisitBonus;

#ifdef DEBUG_FRECENCY
      printf("CalculateFrecency() for place %lld has a bonus of %d\n", aPlaceId, bonus);
#endif

      // if bonus was zero, we can skip the work to determine the weight
      if (bonus) {
        PRTime visitDate = mDBVisitsForFrecency->AsInt64(0);
        PRInt64 ageInDays = GetAgeInDays(normalizedNow, visitDate);

        PRInt32 weight = 0;

        if (ageInDays <= mFirstBucketCutoffInDays)
          weight = mFirstBucketWeight;
        else if (ageInDays <= mSecondBucketCutoffInDays)
          weight = mSecondBucketWeight;
        else if (ageInDays <= mThirdBucketCutoffInDays)
          weight = mThirdBucketWeight;
        else if (ageInDays <= mFourthBucketCutoffInDays) 
          weight = mFourthBucketWeight;
        else
          weight = mDefaultWeight;

        pointsForSampledVisits += (float)(weight * (bonus / 100.0));
      }
    }

    if (numSampledVisits) {
      // fix for bug #412219
      if (!pointsForSampledVisits) {
        // For URIs with zero points in the sampled recent visits
        // but "browsing" type visits outside the sampling range, set
        // frecency to -visit_count, so they're still shown in autocomplete.
        PRInt32 visitCount = 0;
        mozStorageStatementScoper scoper(mDBGetIdPageInfo);
        rv = mDBGetIdPageInfo->BindInt64Parameter(0, aPlaceId);
        NS_ENSURE_SUCCESS(rv, rv);

        PRBool hasVisits = PR_TRUE;
        if (NS_SUCCEEDED(mDBGetIdPageInfo->ExecuteStep(&hasVisits)) && hasVisits) {
          rv = mDBGetIdPageInfo->GetInt32(nsNavHistory::kGetInfoIndex_VisitCount,
                                          &visitCount);
          NS_ENSURE_SUCCESS(rv, rv);
        }
        // If we don't have visits set to 0
        *aFrecency = -visitCount;
      }
      else {
        // Estimate frecency using the last few visits.
        // Use NS_ceilf() so that we don't round down to 0, which
        // would cause us to completely ignore the place during autocomplete.
        *aFrecency = (PRInt32) NS_ceilf(aVisitCount * NS_ceilf(pointsForSampledVisits) / numSampledVisits);
      }

#ifdef DEBUG_FRECENCY
      printf("CalculateFrecency() for place %lld: %d = %d * %f / %d\n", aPlaceId, *aFrecency, aVisitCount, pointsForSampledVisits, numSampledVisits);
#endif

      return NS_OK;
    }
  }
 
  // XXX the code below works well for guessing the frecency on import, and we'll correct later once we have
  // visits.
  // what if we don't have visits and we never visit?  we could end up with a really high value
  // that keeps coming up in ac results?  only do this on import?  something to figure out.
  PRInt32 bonus = 0;

  // not the same logic above, as a single visit could not be both
  // a bookmark visit and a typed visit.  but when estimating a frecency
  // for a place that doesn't have any visits, this will make it so
  // something bookmarked and typed will have a higher frecency than
  // something just typed or just bookmarked.
  if (aIsBookmarked)
    bonus += mUnvisitedBookmarkBonus;
  if (aTyped)
    bonus += mUnvisitedTypedBonus;

  // assume "now" as our ageInDays, so use the first bucket.
  pointsForSampledVisits = mFirstBucketWeight * (bonus / (float)100.0); 
   
  // for a unvisited bookmark, produce a non-zero frecency
  // so that unvisited bookmarks show up in URL bar autocomplete
  if (!aVisitCount && aIsBookmarked)
    aVisitCount = 1;

  // use NS_ceilf() so that we don't round down to 0, which
  // would cause us to completely ignore the place during autocomplete
  *aFrecency = (PRInt32) NS_ceilf(aVisitCount * NS_ceilf(pointsForSampledVisits));
#ifdef DEBUG_FRECENCY
  printf("CalculateFrecency() for unvisited: frecency %d = %f points (b: %d, t: %d) * visit count %d\n", *aFrecency, pointsForSampledVisits, aIsBookmarked, aTyped, aVisitCount);
#endif
  return NS_OK;
}

nsresult
nsNavHistory::CalculateFrecency(PRInt64 aPlaceId,
                                PRInt32 aTyped,
                                PRInt32 aVisitCount,
                                nsCAutoString &aURL,
                                PRInt32 *aFrecency)
{
  *aFrecency = 0;

  PRBool isBookmark = PR_FALSE;

  // determine if the place is a (non-livemark item) bookmark and prevent
  // place: queries from showing up in the URL bar autocomplete results
  if (!IsQueryURI(aURL) && aPlaceId != -1) {
    nsNavBookmarks *bs = nsNavBookmarks::GetBookmarksService();
    NS_ENSURE_TRUE(bs, NS_ERROR_OUT_OF_MEMORY);
    isBookmark = bs->IsRealBookmark(aPlaceId);
  }

  nsresult rv = CalculateFrecencyInternal(aPlaceId, aTyped, aVisitCount,
                                          isBookmark, aFrecency);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

nsresult
nsNavHistory::FixInvalidFrecencies()
{
  mozStorageTransaction transaction(mDBConn, PR_TRUE);

  // Find all places with invalid frecencies (frecency < 0) that occur when:
  // 1) we've done "clear private data"
  // 2) we've expired or deleted visits
  // 3) we've migrated from an older version, before global frecency
  //
  // From older versions, unmigrated bookmarks might be hidden, so we can't
  // exclude hidden places (by doing "WHERE hidden <> 1") from our query, as we
  // want to calculate the frecency for those places and unhide them (if they
  // are not livemark items and not place: queries.)
  //
  // Note, we are not limiting ourselves to places with visits because we may
  // not have any if the place is a bookmark and we expired or deleted all the
  // visits.
  nsCOMPtr<mozIStorageStatement> invalidFrecencies;
  nsresult rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT id, typed, hidden, frecency, url "
      "FROM moz_places_view "
      "WHERE frecency < 0"),
    getter_AddRefs(invalidFrecencies));
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool hasMore = PR_FALSE;
  while (NS_SUCCEEDED(invalidFrecencies->ExecuteStep(&hasMore)) && hasMore) {
    PRInt64 placeId = invalidFrecencies->AsInt64(0);
    PRInt32 typed = invalidFrecencies->AsInt32(1);
    PRInt32 hidden = invalidFrecencies->AsInt32(2);
    PRInt32 oldFrecency = invalidFrecencies->AsInt32(3);
    nsCAutoString url;
    invalidFrecencies->GetUTF8String(4, url);

    PRBool isBook = PR_FALSE;
    if (!IsQueryURI(url)) {
      nsNavBookmarks *bookmarks = nsNavBookmarks::GetBookmarksService();
      NS_ENSURE_TRUE(bookmarks, NS_ERROR_OUT_OF_MEMORY);
      isBook = bookmarks->IsRealBookmark(placeId);
    }

    rv = UpdateFrecencyInternal(placeId, typed, hidden, oldFrecency, isBook);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}


#ifdef MOZ_XUL

namespace {

// Used to notify a topic to system observers on async execute completion.
class AutoCompleteStatementCallbackNotifier : public mozIStorageStatementCallback
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_MOZISTORAGESTATEMENTCALLBACK
};

NS_IMPL_ISUPPORTS1(AutoCompleteStatementCallbackNotifier,
                   mozIStorageStatementCallback)

NS_IMETHODIMP
AutoCompleteStatementCallbackNotifier::HandleCompletion(PRUint16 aReason)
{
  if (aReason != mozIStorageStatementCallback::REASON_FINISHED)
    return NS_ERROR_UNEXPECTED;

  nsresult rv;
  nsCOMPtr<nsIObserverService> observerService =
    do_GetService("@mozilla.org/observer-service;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = observerService->NotifyObservers(nsnull,
                                        PLACES_AUTOCOMPLETE_FEEDBACK_UPDATED_TOPIC,
                                        nsnull);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
AutoCompleteStatementCallbackNotifier::HandleError(mozIStorageError *aError)
{
#ifdef DEBUG
  PRInt32 result;
  nsresult rv = aError->GetResult(&result);
  NS_ENSURE_SUCCESS(rv, rv);
  nsCAutoString message;
  rv = aError->GetMessage(message);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCAutoString warnMsg;
  warnMsg.Append("An error occured while executing an async statement: ");
  warnMsg.Append(result);
  warnMsg.Append(" ");
  warnMsg.Append(message);
  NS_WARNING(warnMsg.get());
#endif

  return NS_OK;
}

NS_IMETHODIMP
AutoCompleteStatementCallbackNotifier::HandleResult(mozIStorageResultSet *aResultSet)
{
  NS_ASSERTION(PR_FALSE, "You cannot use AutoCompleteStatementCallbackNotifier to get async statements resultset");
  return NS_OK;
}

} // anonymous namespace

nsresult
nsNavHistory::AutoCompleteFeedback(PRInt32 aIndex,
                                   nsIAutoCompleteController *aController)
{
  // We do not track user choices in the location bar in private browsing mode.
  if (InPrivateBrowsingMode())
    return NS_OK;

  mozIStorageStatement *stmt = GetDBFeedbackIncrease();
  NS_ENSURE_STATE(stmt);
  mozStorageStatementScoper scope(stmt);

  nsAutoString input;
  nsresult rv = aController->GetSearchString(input);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindStringParameter(0, input);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString url;
  rv = aController->GetValueAt(aIndex, url);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindStringParameter(1, url);
  NS_ENSURE_SUCCESS(rv, rv);

  // We do the update asynchronously and we do not care about failures.
  nsCOMPtr<AutoCompleteStatementCallbackNotifier> callback =
    new AutoCompleteStatementCallbackNotifier();
  nsCOMPtr<mozIStoragePendingStatement> canceler;
  rv = stmt->ExecuteAsync(callback, getter_AddRefs(canceler));
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

mozIStorageStatement *
nsNavHistory::GetDBFeedbackIncrease()
{
  if (mDBFeedbackIncrease)
    return mDBFeedbackIncrease;

  nsresult rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
    // Leverage the PRIMARY KEY (place_id, input) to insert/update entries.
    "INSERT OR REPLACE INTO moz_inputhistory "
      // use_count will asymptotically approach the max of 10.
      "SELECT h.id, IFNULL(i.input, ?1), IFNULL(i.use_count, 0) * .9 + 1 "
      "FROM moz_places_temp h "
      "LEFT JOIN moz_inputhistory i ON i.place_id = h.id AND i.input = ?1 "
      "WHERE url = ?2 "
      "UNION ALL "
      "SELECT h.id, IFNULL(i.input, ?1), IFNULL(i.use_count, 0) * .9 + 1 "
      "FROM moz_places h "
      "LEFT JOIN moz_inputhistory i ON i.place_id = h.id AND i.input = ?1 "
      "WHERE url = ?2 "
        "AND h.id NOT IN (SELECT id FROM moz_places_temp)"),
    getter_AddRefs(mDBFeedbackIncrease));
  NS_ENSURE_SUCCESS(rv, nsnull);

  return mDBFeedbackIncrease;
}
#endif


nsICollation *
nsNavHistory::GetCollation()
{
  if (mCollation)
    return mCollation;

  // locale
  nsCOMPtr<nsILocale> locale;
  nsCOMPtr<nsILocaleService> ls(do_GetService(NS_LOCALESERVICE_CONTRACTID));
  NS_ENSURE_TRUE(ls, nsnull);
  nsresult rv = ls->GetApplicationLocale(getter_AddRefs(locale));
  NS_ENSURE_SUCCESS(rv, nsnull);

  // collation
  nsCOMPtr<nsICollationFactory> cfact =
    do_CreateInstance(NS_COLLATIONFACTORY_CONTRACTID);
  NS_ENSURE_TRUE(cfact, nsnull);
  rv = cfact->CreateCollation(locale, getter_AddRefs(mCollation));
  NS_ENSURE_SUCCESS(rv, nsnull);

  return mCollation;
}

nsIStringBundle *
nsNavHistory::GetBundle()
{
  if (!mBundle) {
    nsCOMPtr<nsIStringBundleService> bundleService =
      do_GetService(NS_STRINGBUNDLE_CONTRACTID);
    NS_ENSURE_TRUE(bundleService, nsnull);
    nsresult rv = bundleService->CreateBundle(
        "chrome://places/locale/places.properties",
        getter_AddRefs(mBundle));
    NS_ENSURE_SUCCESS(rv, nsnull);
  }
  return mBundle;
}

nsIStringBundle *
nsNavHistory::GetDateFormatBundle()
{
  if (!mDateFormatBundle) {
    nsCOMPtr<nsIStringBundleService> bundleService =
      do_GetService(NS_STRINGBUNDLE_CONTRACTID);
    NS_ENSURE_TRUE(bundleService, nsnull);
    nsresult rv = bundleService->CreateBundle(
        "chrome://global/locale/dateFormat.properties",
        getter_AddRefs(mDateFormatBundle));
    NS_ENSURE_SUCCESS(rv, nsnull);
  }
  return mDateFormatBundle;
}

mozIStorageStatement *
nsNavHistory::GetDBVisitToVisitResult()
{
  if (mDBVisitToVisitResult)
    return mDBVisitToVisitResult;

  // mDBVisitToVisitResult, should match kGetInfoIndex_* (see GetQueryResults)
  // We are not checking for duplicated ids into the unified table
  // for perf reasons, LIMIT 1 will discard duplicates faster since we
  // have unique visit ids.
  nsresult rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT h.id, h.url, h.title, h.rev_host, h.visit_count, "
          "v.visit_date, f.url, v.session, null, null, null, null "
        "FROM moz_places_temp h "
        "LEFT JOIN moz_historyvisits_temp v_t ON h.id = v_t.place_id "
        "LEFT JOIN moz_historyvisits v ON h.id = v.place_id "
        "LEFT JOIN moz_favicons f ON h.favicon_id = f.id "
        "WHERE v.id = ?1 OR v_t.id = ?1 "
      "UNION ALL "
      "SELECT h.id, h.url, h.title, h.rev_host, h.visit_count, "
          "v.visit_date, f.url, v.session, null, null, null, null "
        "FROM moz_places h "
        "LEFT JOIN moz_historyvisits_temp v_t ON h.id = v_t.place_id "
        "LEFT JOIN moz_historyvisits v ON h.id = v.place_id "
        "LEFT JOIN moz_favicons f ON h.favicon_id = f.id "
        "WHERE v.id = ?1 OR v_t.id = ?1 "
      "LIMIT 1"),
    getter_AddRefs(mDBVisitToVisitResult));
  NS_ENSURE_SUCCESS(rv, nsnull);

  return mDBVisitToVisitResult;
}

mozIStorageStatement *
nsNavHistory::GetDBVisitToURLResult()
{
  if (mDBVisitToURLResult)
    return mDBVisitToURLResult;

  // mDBVisitToURLResult, should match kGetInfoIndex_* (see GetQueryResults)
  // We are not checking for duplicated ids into the unified table
  // for perf reasons, LIMIT 1 will discard duplicates faster since we
  // have unique visit ids.
  nsresult rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT h.id, h.url, h.title, h.rev_host, h.visit_count, "
             "h.last_visit_date, f.url, null, null, null, null, null, null "
        "FROM moz_places_temp h "
        "LEFT JOIN moz_historyvisits_temp v_t ON h.id = v_t.place_id "
        "LEFT JOIN moz_historyvisits v ON h.id = v.place_id "
        "LEFT JOIN moz_favicons f ON h.favicon_id = f.id "
        "WHERE v.id = ?1 OR v_t.id = ?1 "
      "UNION ALL "
      "SELECT h.id, h.url, h.title, h.rev_host, h.visit_count, "
             "h.last_visit_date, f.url, null, null, null, null, null, null "
        "FROM moz_places h "
        "LEFT JOIN moz_historyvisits_temp v_t ON h.id = v_t.place_id "
        "LEFT JOIN moz_historyvisits v ON h.id = v.place_id "
        "LEFT JOIN moz_favicons f ON h.favicon_id = f.id "
        "WHERE v.id = ?1 OR v_t.id = ?1 "
      "LIMIT 1"),
    getter_AddRefs(mDBVisitToURLResult));
  NS_ENSURE_SUCCESS(rv, nsnull);

  return mDBVisitToURLResult;
}

mozIStorageStatement *
nsNavHistory::GetDBBookmarkToUrlResult()
{
  if (mDBBookmarkToUrlResult)
    return mDBBookmarkToUrlResult;

  // mDBBookmarkToUrlResult, should match kGetInfoIndex_*
  // We are not checking for duplicated ids into the unified table
  // for perf reasons, LIMIT 1 will discard duplicates faster since we
  // have unique place ids.
  nsresult rv = mDBConn->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT b.fk, h.url, COALESCE(b.title, h.title), "
        "h.rev_host, h.visit_count, h.last_visit_date, f.url, null, b.id, "
        "b.dateAdded, b.lastModified, b.parent, null "
      "FROM moz_bookmarks b "
      "JOIN moz_places_temp h ON b.fk = h.id "
      "LEFT OUTER JOIN moz_favicons f ON h.favicon_id = f.id "
      "WHERE b.id = ?1 "
      "UNION ALL "
      "SELECT b.fk, h.url, COALESCE(b.title, h.title), "
        "h.rev_host, h.visit_count, h.last_visit_date, f.url, null, b.id, "
        "b.dateAdded, b.lastModified, b.parent, null "
      "FROM moz_bookmarks b "
      "JOIN moz_places h ON b.fk = h.id "
      "LEFT OUTER JOIN moz_favicons f ON h.favicon_id = f.id "
      "WHERE b.id = ?1 "
      "LIMIT 1"),
    getter_AddRefs(mDBBookmarkToUrlResult));
  NS_ENSURE_SUCCESS(rv, nsnull);

  return mDBBookmarkToUrlResult;
}

nsresult
nsNavHistory::FinalizeStatements() {
  mozIStorageStatement* stmts[] = {
#ifdef MOZ_XUL
    mDBFeedbackIncrease,
#endif
    mDBGetURLPageInfo,
    mDBGetIdPageInfo,
    mDBRecentVisitOfURL,
    mDBRecentVisitOfPlace,
    mDBInsertVisit,
    mDBGetPageVisitStats,
    mDBIsPageVisited,
    mDBUpdatePageVisitStats,
    mDBAddNewPage,
    mDBGetTags,
    mDBGetItemsWithAnno,
    mDBSetPlaceTitle,
    mDBVisitToURLResult,
    mDBVisitToVisitResult,
    mDBBookmarkToUrlResult,
    mDBVisitsForFrecency,
    mDBUpdateFrecencyAndHidden,
    mDBGetPlaceVisitStats,
    mDBFullVisitCount,
  };

  for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(stmts); i++) {
    nsresult rv = nsNavHistory::FinalizeStatement(stmts[i]);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

// nsICharsetResolver **********************************************************

NS_IMETHODIMP
nsNavHistory::RequestCharset(nsIWebNavigation* aWebNavigation,
                             nsIChannel* aChannel,
                             PRBool* aWantCharset,
                             nsISupports** aClosure,
                             nsACString& aResult)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");
  NS_ENSURE_ARG(aChannel);
  NS_ENSURE_ARG_POINTER(aWantCharset);
  NS_ENSURE_ARG_POINTER(aClosure);

  *aWantCharset = PR_FALSE;
  *aClosure = nsnull;

  nsCOMPtr<nsIURI> uri;
  nsresult rv = aChannel->GetURI(getter_AddRefs(uri));
  if (NS_FAILED(rv))
    return NS_OK;

  nsAutoString charset;
  rv = GetCharsetForURI(uri, charset);
  NS_ENSURE_SUCCESS(rv, rv);

  CopyUTF16toUTF8(charset, aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistory::NotifyResolvedCharset(const nsACString& aCharset,
                                    nsISupports* aClosure)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");

  NS_ERROR("Unexpected call to NotifyResolvedCharset -- we never set aWantCharset to true!");
  return NS_ERROR_NOT_IMPLEMENTED;
}
