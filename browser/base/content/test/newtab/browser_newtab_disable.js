/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * These tests make sure that the 'New Tab Page' feature can be disabled if the
 * decides not to use it.
 */
function runTests() {
  // create a new tab page and hide it.
  setLinks("0,1,2,3,4,5,6,7,8");
  setPinnedLinks("");

  yield addNewTabPageTab();
  let gridNode = cw.gGrid.node;

  ok(!gridNode.hasAttribute("page-disabled"), "page is not disabled");

  cw.gToolbar.hide();
  ok(gridNode.hasAttribute("page-disabled"), "page is disabled");

  let oldGridNode = cw.gGrid.node;

  // create a second new tage page and make sure it's disabled. enable it
  // again and check if the former page gets enabled as well.
  yield addNewTabPageTab();
  ok(gridNode.hasAttribute("page-disabled"), "page is disabled");

  // check that no sites have been rendered
  is(0, cw.document.querySelectorAll(".site").length, "no sites have been rendered");

  cw.gToolbar.show();
  ok(!gridNode.hasAttribute("page-disabled"), "page is not disabled");
  ok(!oldGridNode.hasAttribute("page-disabled"), "old page is not disabled");
}
