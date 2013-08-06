/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

function add_visit(aURI, aReferrer) {
  return PlacesUtils.history.addVisit(aURI, Date.now() * 1000, aReferrer,
                                      PlacesUtils.history.TRANSITION_TYPED,
                                      false, 0);
}

function add_bookmark(aURI) {
  return PlacesUtils.bookmarks.insertBookmark(PlacesUtils.unfiledBookmarksFolderId,
                                              aURI, PlacesUtils.bookmarks.DEFAULT_INDEX,
                                              "bookmark/" + aURI.spec);
}

Components.utils.import("resource://gre/modules/NetUtil.jsm");

const TEST_URL = "http://example.com/";
const MOZURISPEC = "http://mozilla.com/";

let gLibrary;
let PlacesOrganizer;

function test() {
  waitForExplicitFinish();
  gLibrary = window.openDialog("chrome://browser/content/places/places.xul",
                               "", "chrome,toolbar=yes,dialog=no,resizable");
  waitForFocus(onLibraryReady, gLibrary);
}

function onLibraryReady() {
  ok(PlacesUtils, "PlacesUtils in scope");
  ok(PlacesUIUtils, "PlacesUIUtils in scope");

  PlacesOrganizer = gLibrary.PlacesOrganizer;
  ok(PlacesOrganizer, "Places organizer in scope");

  tests.makeHistVisit();
  tests.makeTag();
  tests.focusTag();
  tests.copyHistNode();
  tests.waitForClipboard();
}

function onClipboardReady() {
  tests.pasteToTag();
  tests.historyNode();
  tests.checkForBookmarkInUI();

  gLibrary.close();

  // Remove new Places data we created.
  PlacesUtils.tagging.untagURI(NetUtil.newURI(MOZURISPEC), ["foo"]);
  PlacesUtils.tagging.untagURI(NetUtil.newURI(TEST_URL), ["foo"]);
  let tags = PlacesUtils.tagging.getTagsForURI(NetUtil.newURI(TEST_URL));
  is(tags.length, 0, "tags are gone");
  PlacesUtils.bookmarks.removeFolderChildren(PlacesUtils.unfiledBookmarksFolderId);
  
  waitForClearHistory(finish);
}

let tests = {

  makeHistVisit: function() {
    // need to add a history object
    let testURI1 = NetUtil.newURI(MOZURISPEC);
    isnot(testURI1, null, "testURI is not null");
    let visitId = add_visit(testURI1);
    ok(visitId > 0, "A visit was added to the history");
    ok(PlacesUtils.ghistory2.isVisited(testURI1), MOZURISPEC + " is a visited url.");
  },

  makeTag: function() {
    // create an initial tag to work with
    let bmId = add_bookmark(NetUtil.newURI(TEST_URL));
    ok(bmId > 0, "A bookmark was added");
    PlacesUtils.tagging.tagURI(NetUtil.newURI(TEST_URL), ["foo"]);
    let tags = PlacesUtils.tagging.getTagsForURI(NetUtil.newURI(TEST_URL));
    is(tags[0], 'foo', "tag is foo");
  },

  focusTag: function (paste){
    // focus the new tag
    PlacesOrganizer.selectLeftPaneQuery("Tags");
    let tags = PlacesOrganizer._places.selectedNode;
    tags.containerOpen = true;
    let fooTag = tags.getChild(0);
    this.tagNode = fooTag;
    PlacesOrganizer._places.selectNode(fooTag);
    is(this.tagNode.title, 'foo', "tagNode title is foo");
    let ip = PlacesOrganizer._places.insertionPoint;
    ok(ip.isTag, "IP is a tag");
    if (paste) {
      ok(true, "About to paste");
      PlacesOrganizer._places.controller.paste();
    }
  },

  histNode: null,

  copyHistNode: function (){
    // focus the history object
    PlacesOrganizer.selectLeftPaneQuery("History");
    let histContainer = PlacesOrganizer._places.selectedNode;
    PlacesUtils.asContainer(histContainer);
    histContainer.containerOpen = true;
    PlacesOrganizer._places.selectNode(histContainer.getChild(0));
    this.histNode = PlacesOrganizer._content.view.nodeForTreeIndex(0);
    PlacesOrganizer._content.selectNode(this.histNode);
    is(this.histNode.uri, MOZURISPEC,
       "historyNode exists: " + this.histNode.uri);
    // copy the history node
    PlacesOrganizer._content.controller.copy();
  },

  waitForClipboard: function (){
    try {
      let xferable = Cc["@mozilla.org/widget/transferable;1"].
                     createInstance(Ci.nsITransferable);
      xferable.addDataFlavor(PlacesUtils.TYPE_X_MOZ_PLACE);
      let clipboard = Cc["@mozilla.org/widget/clipboard;1"].
                      getService(Ci.nsIClipboard);
      clipboard.getData(xferable, Ci.nsIClipboard.kGlobalClipboard);
      let data = { }, type = { };
      xferable.getAnyTransferData(type, data, { });
      // Data is in the clipboard
      onClipboardReady();
    } catch (ex) {
      // check again after 100ms.
      setTimeout(arguments.callee, 100);
    }
  },

  pasteToTag: function (){
    // paste history node into tag
    this.focusTag(true);
  },

  historyNode: function (){
    // re-focus the history again
    PlacesOrganizer.selectLeftPaneQuery("History");
    let histContainer = PlacesOrganizer._places.selectedNode;
    PlacesUtils.asContainer(histContainer);
    histContainer.containerOpen = true;
    PlacesOrganizer._places.selectNode(histContainer.getChild(0));
    let histNode = PlacesOrganizer._content.view.nodeForTreeIndex(0);
    ok(histNode, "histNode exists: " + histNode.title);
    // check to see if the history node is tagged!
    let tags = PlacesUtils.tagging.getTagsForURI(NetUtil.newURI(MOZURISPEC));
    ok(tags.length == 1, "history node is tagged: " + tags.length);
    // check if a bookmark was created
    let isBookmarked = PlacesUtils.bookmarks.isBookmarked(NetUtil.newURI(MOZURISPEC));
    is(isBookmarked, true, MOZURISPEC + " is bookmarked");
    let bookmarkIds = PlacesUtils.bookmarks.getBookmarkIdsForURI(
                        NetUtil.newURI(histNode.uri));
    ok(bookmarkIds.length > 0, "bookmark exists for the tagged history item: " + bookmarkIds);
  },

  checkForBookmarkInUI: function(){
    // is the bookmark visible in the UI?
    // get the Unsorted Bookmarks node
    PlacesOrganizer.selectLeftPaneQuery("UnfiledBookmarks");
    // now we can see what is in the _content tree
    let unsortedNode = PlacesOrganizer._content.view.nodeForTreeIndex(1);
    ok(unsortedNode, "unsortedNode is not null: " + unsortedNode.uri);
    is(unsortedNode.uri, MOZURISPEC, "node uri's are the same");
  },

  tagNode: null,
};

/**
 * Clears history invoking callback when done.
 */
function waitForClearHistory(aCallback) {
  const TOPIC_EXPIRATION_FINISHED = "places-expiration-finished";
  let observer = {
    observe: function(aSubject, aTopic, aData) {
      Services.obs.removeObserver(this, TOPIC_EXPIRATION_FINISHED);
      aCallback();
    }
  };
  Services.obs.addObserver(observer, TOPIC_EXPIRATION_FINISHED, false);
  PlacesUtils.bhistory.removeAllPages();
}
