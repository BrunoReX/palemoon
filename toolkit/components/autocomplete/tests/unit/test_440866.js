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
 * The Original Code is Bug 440866 unit test code.
 *
 * The Initial Developer of the Original Code is
 *   The Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Felix Fung <felix.the.cheshire.cat@gmail.com>
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

/**
 * Unit test for Bug 440866 - First AutoCompleteSearch that returns
 * RESULT_NOMATCH cancels all other searches when popup is open
 */



/**
 * Dummy nsIAutoCompleteInput source that returns
 * the given list of AutoCompleteSearch names.
 *
 * Implements only the methods needed for this test.
 */
function AutoCompleteInput(aSearches) {
  this.searches = aSearches;
}
AutoCompleteInput.prototype = {
  constructor: AutoCompleteInput,

  // Array of AutoCompleteSearch names
  searches: null,

  minResultsForPopup: 0,
  timeout: 10,
  searchParam: "",
  textValue: "",
  disableAutoComplete: false,
  completeDefaultIndex: false,

  get searchCount() {
    return this.searches.length;
  },

  getSearchAt: function(aIndex) {
    return this.searches[aIndex];
  },

  onSearchBegin: function() {},
  onSearchComplete: function() {},

  popupOpen: false,

  popup: {
    setSelectedIndex: function(aIndex) {},
    invalidate: function() {},

    // nsISupports implementation
    QueryInterface: function(iid) {
      if (iid.equals(Ci.nsISupports) ||
          iid.equals(Ci.nsIAutoCompletePopup))
        return this;

      throw Components.results.NS_ERROR_NO_INTERFACE;
    }
  },

  // nsISupports implementation
  QueryInterface: function(iid) {
    if (iid.equals(Ci.nsISupports) ||
        iid.equals(Ci.nsIAutoCompleteInput))
      return this;

    throw Components.results.NS_ERROR_NO_INTERFACE;
  }
}



/**
 * nsIAutoCompleteResult implementation
 */
function AutoCompleteResult(aValues, aComments, aStyles) {
  this._values = aValues;
  this._comments = aComments;
  this._styles = aStyles;

  if (this._values.length > 0) {
    this.searchResult = Ci.nsIAutoCompleteResult.RESULT_SUCCESS;
  } else {
    this.searchResult = Ci.nsIAutoCompleteResult.NOMATCH;
  }
}
AutoCompleteResult.prototype = {
  constructor: AutoCompleteResult,

  // Arrays
  _values: null,
  _comments: null,
  _styles: null,

  searchString: "",
  searchResult: null,

  defaultIndex: 0,

  get matchCount() {
    return this._values.length;
  },

  getValueAt: function(aIndex) {
    return this._values[aIndex];
  },

  getLabelAt: function(aIndex) {
    return this.getValueAt(aIndex);
  },

  getCommentAt: function(aIndex) {
    return this._comments[aIndex];
  },

  getStyleAt: function(aIndex) {
    return this._styles[aIndex];
  },

  getImageAt: function(aIndex) {
    return "";
  },

  removeValueAt: function (aRowIndex, aRemoveFromDb) {},

  // nsISupports implementation
  QueryInterface: function(iid) {
    if (iid.equals(Ci.nsISupports) ||
        iid.equals(Ci.nsIAutoCompleteResult))
      return this;

    throw Components.results.NS_ERROR_NO_INTERFACE;
  }
}



/**
 * nsIAutoCompleteSearch implementation that always returns
 * the same result set.
 */
function AutoCompleteSearch(aName, aResult) {
  this.name = aName;
  this._result = aResult;
}
AutoCompleteSearch.prototype = {
  constructor: AutoCompleteSearch,

  // Search name. Used by AutoCompleteController
  name: null,

  // AutoCompleteResult
  _result:null,


  /**
   * Return the same result set for every search
   */
  startSearch: function(aSearchString,
                        aSearchParam,
                        aPreviousResult,
                        aListener)
  {
    aListener.onSearchResult(this, this._result);
  },

  stopSearch: function() {},

  // nsISupports implementation
  QueryInterface: function(iid) {
    if (iid.equals(Ci.nsISupports) ||
        iid.equals(Ci.nsIFactory) ||
        iid.equals(Ci.nsIAutoCompleteSearch))
      return this;

    throw Components.results.NS_ERROR_NO_INTERFACE;
  },

  // nsIFactory implementation
  createInstance: function(outer, iid) {
    return this.QueryInterface(iid);
  }
}



/**
 * Helper to register an AutoCompleteSearch with the given name.
 * Allows the AutoCompleteController to find the search.
 */
function registerAutoCompleteSearch(aSearch) {
  var name = "@mozilla.org/autocomplete/search;1?name=" + aSearch.name;

  var uuidGenerator = Cc["@mozilla.org/uuid-generator;1"].
                      getService(Ci.nsIUUIDGenerator);
  var cid = uuidGenerator.generateUUID();

  var desc = "Test AutoCompleteSearch";

  var componentManager = Components.manager
                                   .QueryInterface(Ci.nsIComponentRegistrar);
  componentManager.registerFactory(cid, desc, name, aSearch);

  // Keep the id on the object so we can unregister later
  aSearch.cid = cid;
}



/**
 * Helper to unregister an AutoCompleteSearch.
 */
function unregisterAutoCompleteSearch(aSearch) {
  var componentManager = Components.manager
                                   .QueryInterface(Ci.nsIComponentRegistrar);
  componentManager.unregisterFactory(aSearch.cid, aSearch);
}



/**
 * Test AutoComplete with multiple AutoCompleteSearch sources.
 */
function run_test() {

  // Make an AutoCompleteSearch that always returns nothing
  var emptySearch = new AutoCompleteSearch("test-empty-search",
                             new AutoCompleteResult([], [], []));

  // Make an AutoCompleteSearch that returns two values
  var expectedValues = ["test1", "test2"];
  var regularSearch = new AutoCompleteSearch("test-regular-search",
                             new AutoCompleteResult(expectedValues, [], []));

  // Register searches so AutoCompleteController can find them
  registerAutoCompleteSearch(emptySearch);
  registerAutoCompleteSearch(regularSearch);

  var controller = Components.classes["@mozilla.org/autocomplete/controller;1"].
                   getService(Components.interfaces.nsIAutoCompleteController);

  // Make an AutoCompleteInput that uses our searches
  // and confirms results on search complete
  var input = new AutoCompleteInput([emptySearch.name, regularSearch.name]);
  var numSearchesStarted = 0;

  input.onSearchBegin = function() {
    numSearchesStarted++;
    do_check_eq(numSearchesStarted, 1);
    do_check_eq(input.searchCount, 2);
  };

  input.onSearchComplete = function() {
    do_check_eq(numSearchesStarted, 1);

    do_check_eq(controller.searchStatus,
                Ci.nsIAutoCompleteController.STATUS_COMPLETE_MATCH);
    do_check_eq(controller.matchCount, 2);

    // Confirm expected result values
    for (var i = 0; i < expectedValues.length; i++) {
      do_check_eq(expectedValues[i], controller.getValueAt(i));
    }

    do_check_true(input.popupOpen);

    // Unregister searches
    unregisterAutoCompleteSearch(emptySearch);
    unregisterAutoCompleteSearch(regularSearch);

    do_test_finished();
  };

  controller.input = input;

  // Search is asynchronous, so don't let the test finish immediately
  do_test_pending();

  controller.startSearch("test");
}

