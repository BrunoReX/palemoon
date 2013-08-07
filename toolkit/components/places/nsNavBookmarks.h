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
 * The Original Code is Places.
 *
 * The Initial Developer of the Original Code is
 * Google Inc.
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Brian Ryner <bryner@brianryner.com> (original author)
 *   Dietrich Ayala <dietrich@mozilla.com>
 *   Marco Bonardo <mak77@bonardo.net>
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

#ifndef nsNavBookmarks_h_
#define nsNavBookmarks_h_

#include "nsINavBookmarksService.h"
#include "nsIAnnotationService.h"
#include "nsITransaction.h"
#include "nsNavHistory.h"
#include "nsToolkitCompsCID.h"
#include "nsCategoryCache.h"
#include "nsTHashtable.h"

namespace mozilla {
namespace places {

  enum BookmarkStatementId {
    DB_FIND_REDIRECTED_BOOKMARK = 0
  , DB_GET_BOOKMARKS_FOR_URI
  };

  struct BookmarkData {
    PRInt64 id;
    nsCString url;
    nsCString title;
    PRInt32 position;
    PRInt64 placeId;
    PRInt64 parentId;
    PRInt64 grandParentId;
    PRInt32 type;
    nsCString serviceCID;
    PRTime dateAdded;
    PRTime lastModified;
    nsCString guid;
    nsCString parentGuid;
  };

  struct ItemVisitData {
    BookmarkData bookmark;
    PRInt64 visitId;
    PRUint32 transitionType;
    PRTime time;
  };

  struct ItemChangeData {
    BookmarkData bookmark;
    nsCString property;
    PRBool isAnnotation;
    nsCString newValue;
  };

  typedef void (nsNavBookmarks::*ItemVisitMethod)(const ItemVisitData&);
  typedef void (nsNavBookmarks::*ItemChangeMethod)(const ItemChangeData&);

  class BookmarkKeyClass : public nsTrimInt64HashKey
  {
    public:
    BookmarkKeyClass(const PRInt64* aItemId)
    : nsTrimInt64HashKey(aItemId)
    , creationTime(PR_Now())
    {
    }
    BookmarkKeyClass(const BookmarkKeyClass& aOther)
    : nsTrimInt64HashKey(aOther)
    , creationTime(PR_Now())
    {
      NS_NOTREACHED("Do not call me!");
    }
    BookmarkData bookmark;
    PRTime creationTime;
  };

} // namespace places
} // namespace mozilla

class nsIOutputStream;

class nsNavBookmarks : public nsINavBookmarksService,
                       public nsINavHistoryObserver,
                       public nsIAnnotationObserver,
                       public nsIObserver
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSINAVBOOKMARKSSERVICE
  NS_DECL_NSINAVHISTORYOBSERVER
  NS_DECL_NSIANNOTATIONOBSERVER
  NS_DECL_NSIOBSERVER

  nsNavBookmarks();

  /**
   * Obtains the service's object.
   */
  static nsNavBookmarks* GetSingleton();

  /**
   * Initializes the service's object.  This should only be called once.
   */
  nsresult Init();

  // called by nsNavHistory::Init
  static nsresult InitTables(mozIStorageConnection* aDBConn);

  static nsNavBookmarks* GetBookmarksServiceIfAvailable() {
    return gBookmarksService;
  }

  static nsNavBookmarks* GetBookmarksService() {
    if (!gBookmarksService) {
      nsCOMPtr<nsINavBookmarksService> serv =
        do_GetService(NS_NAVBOOKMARKSSERVICE_CONTRACTID);
      NS_ENSURE_TRUE(serv, nsnull);
      NS_ASSERTION(gBookmarksService,
                   "Should have static instance pointer now");
    }
    return gBookmarksService;
  }

  typedef mozilla::places::BookmarkData BookmarkData;
  typedef mozilla::places::BookmarkKeyClass BookmarkKeyClass;
  typedef mozilla::places::ItemVisitData ItemVisitData;
  typedef mozilla::places::ItemChangeData ItemChangeData;
  typedef mozilla::places::BookmarkStatementId BookmarkStatementId;

  nsresult ResultNodeForContainer(PRInt64 aID,
                                  nsNavHistoryQueryOptions* aOptions,
                                  nsNavHistoryResultNode** aNode);

  // Find all the children of a folder, using the given query and options.
  // For each child, a ResultNode is created and added to |children|.
  // The results are ordered by folder position.
  nsresult QueryFolderChildren(PRInt64 aFolderId,
                               nsNavHistoryQueryOptions* aOptions,
                               nsCOMArray<nsNavHistoryResultNode>* children);

  /**
   * Turns aRow into a node and appends it to aChildren if it is appropriate to
   * do so.
   *
   * @param aRow
   *        A Storage statement (in the case of synchronous execution) or row of
   *        a result set (in the case of asynchronous execution).
   * @param aOptions
   *        The options of the parent folder node.
   * @param aChildren
   *        The children of the parent folder node.
   * @param aCurrentIndex
   *        The index of aRow within the results.  When called on the first row,
   *        this should be set to -1.
   */
  nsresult ProcessFolderNodeRow(mozIStorageValueArray* aRow,
                                nsNavHistoryQueryOptions* aOptions,
                                nsCOMArray<nsNavHistoryResultNode>* aChildren,
                                PRInt32& aCurrentIndex);

  /**
   * The async version of QueryFolderChildren.
   *
   * @param aNode
   *        The folder node that will receive the children.
   * @param _pendingStmt
   *        The Storage pending statement that will be used to control async
   *        execution.
   */
  nsresult QueryFolderChildrenAsync(nsNavHistoryFolderResultNode* aNode,
                                    PRInt64 aFolderId,
                                    mozIStoragePendingStatement** _pendingStmt);

  // If aFolder is -1, uses the autoincrement id for folder index. Returns
  // the index of the new folder in aIndex, whether it was passed in or
  // generated by autoincrement.
  nsresult CreateContainerWithID(PRInt64 aId, PRInt64 aParent,
                                 const nsACString& aTitle,
                                 const nsAString& aContractId,
                                 PRBool aIsBookmarkFolder,
                                 PRInt32* aIndex,
                                 PRInt64* aNewFolder);

  /**
   * Determines if we have a real bookmark or not (not a livemark).
   *
   * @param aPlaceId
   *        The place_id of the location to check against.
   * @return true if it's a real bookmark, false otherwise.
   */
  PRBool IsRealBookmark(PRInt64 aPlaceId);

  /**
   * Fetches information about the specified id from the database.
   *
   * @param aItemId
   *        Id of the item to fetch information for.
   * @param aBookmark
   *        BookmarkData to store the information.
   */
  nsresult FetchItemInfo(PRInt64 aItemId,
                         BookmarkData& _bookmark);

  /**
   * Finalize all internal statements.
   */
  nsresult FinalizeStatements();

  mozIStorageStatement* GetStatementById(BookmarkStatementId aStatementId)
  {
    using namespace mozilla::places;
    switch(aStatementId) {
      case DB_FIND_REDIRECTED_BOOKMARK:
        return GetStatement(mDBFindRedirectedBookmark);
      case DB_GET_BOOKMARKS_FOR_URI:
        return GetStatement(mDBFindURIBookmarks);
    }
    return nsnull;
  }

  /**
   * Notifies that a bookmark has been visited.
   *
   * @param aItemId
   *        The visited item id.
   * @param aData
   *        Details about the new visit.
   */
  void NotifyItemVisited(const ItemVisitData& aData);

  /**
   * Notifies that a bookmark has changed.
   *
   * @param aItemId
   *        The changed item id.
   * @param aData
   *        Details about the change.
   */
  void NotifyItemChanged(const ItemChangeData& aData);

private:
  static nsNavBookmarks* gBookmarksService;

  ~nsNavBookmarks();

  /**
   * Locates the root items in the bookmarks folder hierarchy assigning folder
   * ids to the root properties that are exposed through the service interface.
   * 
   * @param aForceCreate
   *        Whether the method should try creating the roots.  It should be set
   *        to true if the database has just been created or upgraded.
   *
   * @note The creation of roots skips already existing entries.
   */
  nsresult InitRoots(bool aForceCreate);

  /**
   * Tries to create a root folder with the given name.
   *
   * @param name
   *        Name associated to the root.
   * @param _itemId
   *        if set CreateRoot will skip creation, otherwise will return the
   *        newly created folder id.
   * @param aParentId
   *        Id of the parent that should cotain this root.
   * @param aBundle
   *        Stringbundle used to get the visible title of the root.
   * @param aTitleStringId
   *        Id of the title string in the stringbundle.
   */
  nsresult CreateRoot(const nsCString& name,
                      PRInt64* _itemId,
                      PRInt64 aParentId,
                      nsIStringBundle* aBundle,
                      const PRUnichar* aTitleStringId);

  nsresult AdjustIndices(PRInt64 aFolder,
                         PRInt32 aStartIndex,
                         PRInt32 aEndIndex,
                         PRInt32 aDelta);

  /**
   * Fetches properties of a folder.
   *
   * @param aFolderId
   *        Folder to count children for.
   * @param _folderCount
   *        Number of children in the folder.
   * @param _guid
   *        Unique id of the folder.
   * @param _parentId
   *        Id of the parent of the folder.
   *
   * @throws If folder does not exist.
   */
  nsresult FetchFolderInfo(PRInt64 aFolderId,
                           PRInt32* _folderCount,
                           nsACString& _guid,
                           PRInt64* _parentId);

  nsresult GetFolderType(PRInt64 aFolder, nsACString& aType);

  nsresult GetLastChildId(PRInt64 aFolder, PRInt64* aItemId);

  /**
   * This is the basic Places read-write connection, obtained from history.
   */
  nsCOMPtr<mozIStorageConnection> mDBConn;
  /**
   * Cloned read-only connection.  Can be used to read from the database
   * without being locked out by writers.
   */
  nsCOMPtr<mozIStorageConnection> mDBReadOnlyConn;

  nsString mGUIDBase;
  nsresult GetGUIDBase(nsAString& aGUIDBase);

  PRInt32 mItemCount;

  nsMaybeWeakPtrArray<nsINavBookmarkObserver> mObservers;

  PRInt64 mRoot;
  PRInt64 mMenuRoot;
  PRInt64 mTagsRoot;
  PRInt64 mUnfiledRoot;
  PRInt64 mToolbarRoot;

  nsresult IsBookmarkedInDatabase(PRInt64 aBookmarkID, PRBool* aIsBookmarked);

  nsresult SetItemDateInternal(mozIStorageStatement* aStatement,
                               PRInt64 aItemId,
                               PRTime aValue);

  // Recursive method to build an array of folder's children
  nsresult GetDescendantChildren(PRInt64 aFolderId,
                                 const nsACString& aFolderGuid,
                                 PRInt64 aGrandParentId,
                                 nsTArray<BookmarkData>& aFolderChildrenArray);

  enum ItemType {
    BOOKMARK = TYPE_BOOKMARK,
    FOLDER = TYPE_FOLDER,
    SEPARATOR = TYPE_SEPARATOR,
    DYNAMIC_CONTAINER = TYPE_DYNAMIC_CONTAINER
  };

  /**
   * Helper to insert a bookmark in the database.
   *
   *  @param aItemId
   *         The itemId to insert, pass -1 to generate a new one.
   *  @param aPlaceId
   *         The placeId to which this bookmark refers to, pass nsnull for
   *         items that don't refer to an URI (eg. folders, separators, ...).
   *  @param aItemType
   *         The type of the new bookmark, see TYPE_* constants.
   *  @param aParentId
   *         The itemId of the parent folder.
   *  @param aIndex
   *         The position inside the parent folder.
   *  @param aTitle
   *         The title for the new bookmark.
   *         Pass a void string to set a NULL title.
   *  @param aDateAdded
   *         The date for the insertion.
   *  @param [optional] aLastModified
   *         The last modified date for the insertion.
   *         It defaults to aDateAdded.
   *  @param [optional] aServiceContractId
   *         The contract id for a dynamic container.
   *         Pass EmptyCString() for other type of containers.
   *
   *  @return The new item id that has been inserted.
   *
   *  @note This will also update last modified date of the parent folder.
   */
  nsresult InsertBookmarkInDB(PRInt64 aPlaceId,
                              enum ItemType aItemType,
                              PRInt64 aParentId,
                              PRInt32 aIndex,
                              const nsACString& aTitle,
                              PRTime aDateAdded,
                              PRTime aLastModified,
                              const nsAString& aServiceContractId,
                              PRInt64* _itemId,
                              nsACString& _guid);

  /**
   * TArray version of getBookmarksIdForURI for ease of use in C++ code.
   * Pass in a reference to a TArray; it will get filled with the
   * resulting list of bookmark IDs.
   *
   * @param aURI
   *        URI to get bookmarks for.
   * @param aResult
   *        Array of bookmark ids.
   * @param aSkipTags
   *        If true ids of tags-as-bookmarks entries will be excluded.
   */
  nsresult GetBookmarkIdsForURITArray(nsIURI* aURI,
                                      nsTArray<PRInt64>& aResult,
                                      bool aSkipTags);

  nsresult GetBookmarksForURI(nsIURI* aURI,
                              nsTArray<BookmarkData>& _bookmarks);

  PRInt64 RecursiveFindRedirectedBookmark(PRInt64 aPlaceId);

  /**
   *  You should always use this getter and never use directly the nsCOMPtr.
   */
  mozIStorageStatement* GetStatement(const nsCOMPtr<mozIStorageStatement>& aStmt);

  nsCOMPtr<mozIStorageStatement> mDBGetChildren;
  // These columns sit to the right of the kGetInfoIndex_* columns.
  static const PRInt32 kGetChildrenIndex_Position;
  static const PRInt32 kGetChildrenIndex_Type;
  static const PRInt32 kGetChildrenIndex_PlaceID;
  static const PRInt32 kGetChildrenIndex_FolderTitle;
  static const PRInt32 kGetChildrenIndex_ServiceContractId;
  static const PRInt32 kGetChildrenIndex_Guid;

  nsCOMPtr<mozIStorageStatement> mDBFindURIBookmarks;
  static const PRInt32 kFindURIBookmarksIndex_Id;
  static const PRInt32 kFindURIBookmarksIndex_Guid;
  static const PRInt32 kFindURIBookmarksIndex_ParentId;
  static const PRInt32 kFindURIBookmarksIndex_LastModified;
  static const PRInt32 kFindURIBookmarksIndex_ParentGuid;
  static const PRInt32 kFindURIBookmarksIndex_GrandParentId;

  nsCOMPtr<mozIStorageStatement> mDBGetItemProperties;
  static const PRInt32 kGetItemPropertiesIndex_Id;
  static const PRInt32 kGetItemPropertiesIndex_Url;
  static const PRInt32 kGetItemPropertiesIndex_Title;
  static const PRInt32 kGetItemPropertiesIndex_Position;
  static const PRInt32 kGetItemPropertiesIndex_PlaceId;
  static const PRInt32 kGetItemPropertiesIndex_ParentId;
  static const PRInt32 kGetItemPropertiesIndex_Type;
  static const PRInt32 kGetItemPropertiesIndex_ServiceContractId;
  static const PRInt32 kGetItemPropertiesIndex_DateAdded;
  static const PRInt32 kGetItemPropertiesIndex_LastModified;
  static const PRInt32 kGetItemPropertiesIndex_Guid;
  static const PRInt32 kGetItemPropertiesIndex_ParentGuid;
  static const PRInt32 kGetItemPropertiesIndex_GrandParentId;

  nsCOMPtr<mozIStorageStatement> mDBInsertBookmark;
  static const PRInt32 kInsertBookmarkIndex_Id;
  static const PRInt32 kInsertBookmarkIndex_PlaceId;
  static const PRInt32 kInsertBookmarkIndex_Type;
  static const PRInt32 kInsertBookmarkIndex_Parent;
  static const PRInt32 kInsertBookmarkIndex_Position;
  static const PRInt32 kInsertBookmarkIndex_Title;
  static const PRInt32 kInsertBookmarkIndex_ServiceContractId;
  static const PRInt32 kInsertBookmarkIndex_DateAdded;
  static const PRInt32 kInsertBookmarkIndex_LastModified;

  nsCOMPtr<mozIStorageStatement> mDBFolderInfo;
  nsCOMPtr<mozIStorageStatement> mDBGetItemIndex;
  nsCOMPtr<mozIStorageStatement> mDBGetChildAt;
  nsCOMPtr<mozIStorageStatement> mDBGetItemIdForGUID;
  nsCOMPtr<mozIStorageStatement> mDBIsBookmarkedInDatabase;
  nsCOMPtr<mozIStorageStatement> mDBIsURIBookmarkedInDatabase;
  nsCOMPtr<mozIStorageStatement> mDBIsRealBookmark;
  nsCOMPtr<mozIStorageStatement> mDBGetLastBookmarkID;
  nsCOMPtr<mozIStorageStatement> mDBSetItemDateAdded;
  nsCOMPtr<mozIStorageStatement> mDBSetItemLastModified;
  nsCOMPtr<mozIStorageStatement> mDBSetItemIndex;
  nsCOMPtr<mozIStorageStatement> mDBGetKeywordForURI;
  nsCOMPtr<mozIStorageStatement> mDBGetBookmarksToKeywords;
  nsCOMPtr<mozIStorageStatement> mDBAdjustPosition;
  nsCOMPtr<mozIStorageStatement> mDBRemoveItem;
  nsCOMPtr<mozIStorageStatement> mDBGetLastChildId;
  nsCOMPtr<mozIStorageStatement> mDBMoveItem;
  nsCOMPtr<mozIStorageStatement> mDBSetItemTitle;
  nsCOMPtr<mozIStorageStatement> mDBChangeBookmarkURI;
  nsCOMPtr<mozIStorageStatement> mDBFindRedirectedBookmark;

  class RemoveFolderTransaction : public nsITransaction {
  public:
    RemoveFolderTransaction(PRInt64 aID) : mID(aID) {}

    NS_DECL_ISUPPORTS

    NS_IMETHOD DoTransaction() {
      nsNavBookmarks* bookmarks = nsNavBookmarks::GetBookmarksService();
      NS_ENSURE_TRUE(bookmarks, NS_ERROR_OUT_OF_MEMORY);
      BookmarkData folder;
      nsresult rv = bookmarks->FetchItemInfo(mID, folder);
      // TODO (Bug 656935): store the BookmarkData struct instead.
      mParent = folder.parentId;
      mIndex = folder.position;

      rv = bookmarks->GetItemTitle(mID, mTitle);
      NS_ENSURE_SUCCESS(rv, rv);

      nsCAutoString type;
      rv = bookmarks->GetFolderType(mID, type);
      NS_ENSURE_SUCCESS(rv, rv);
      CopyUTF8toUTF16(type, mType);

      return bookmarks->RemoveItem(mID);
    }

    NS_IMETHOD UndoTransaction() {
      nsNavBookmarks* bookmarks = nsNavBookmarks::GetBookmarksService();
      NS_ENSURE_TRUE(bookmarks, NS_ERROR_OUT_OF_MEMORY);
      PRInt64 newFolder;
      return bookmarks->CreateContainerWithID(mID, mParent, mTitle, mType, PR_TRUE,
                                              &mIndex, &newFolder); 
    }

    NS_IMETHOD RedoTransaction() {
      return DoTransaction();
    }

    NS_IMETHOD GetIsTransient(PRBool* aResult) {
      *aResult = PR_FALSE;
      return NS_OK;
    }
    
    NS_IMETHOD Merge(nsITransaction* aTransaction, PRBool* aResult) {
      *aResult = PR_FALSE;
      return NS_OK;
    }

  private:
    PRInt64 mID;
    PRInt64 mParent;
    nsCString mTitle;
    nsString mType;
    PRInt32 mIndex;
  };

  // Used to enable and disable the observer notifications.
  bool mCanNotify;
  nsCategoryCache<nsINavBookmarkObserver> mCacheObservers;

  bool mShuttingDown;

  // Tracks whether we are in batch mode.
  // Note: this is only tracking bookmarks batches, not history ones.
  bool mBatching;

  /**
   * Always call EnsureKeywordsHash() and check it for errors before actually
   * using the hash.  Internal keyword methods are already doing that.
   */
  nsresult EnsureKeywordsHash();
  nsDataHashtable<nsTrimInt64HashKey, nsString> mBookmarkToKeywordHash;

  /**
   * This function must be called every time a bookmark is removed.
   *
   * @param aURI
   *        Uri to test.
   */
  nsresult UpdateKeywordsHashForRemovedBookmark(PRInt64 aItemId);

  /**
   * Cache for the last fetched BookmarkData entries.
   * This is used to speed up repeated requests to the same item id.
   */
  nsTHashtable<BookmarkKeyClass> mRecentBookmarksCache;
};

#endif // nsNavBookmarks_h_
