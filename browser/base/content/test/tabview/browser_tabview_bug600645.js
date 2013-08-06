/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

let newTab;

function test() {
  waitForExplicitFinish();

  newTab = gBrowser.addTab();
  gBrowser.pinTab(newTab);

  window.addEventListener("tabviewshown", onTabViewWindowLoaded, false);
  TabView.toggle();
}

function onTabViewWindowLoaded() {
  window.removeEventListener("tabviewshown", onTabViewWindowLoaded, false);

  let contentWindow = document.getElementById("tab-view").contentWindow;
  is(contentWindow.GroupItems.groupItems.length, 1, 
     "There is one group item on startup");

  let groupItem = contentWindow.GroupItems.groupItems[0];
  let icon = contentWindow.iQ(".appTabIcon", groupItem.$appTabTray)[0];
  let $icon = contentWindow.iQ(icon);

  is($icon.data("xulTab"), newTab, 
     "The app tab icon has the right tab reference")
  // check to see whether it's showing the default one or not.
  is($icon.attr("src"), contentWindow.Utils.defaultFaviconURL, 
     "The icon is showing the default fav icon for blank tab");

  let errorHandler = function(event) {
    newTab.removeEventListener("error", errorHandler, false);

    // since the browser code and test code are invoked when an error event is 
    // fired, a delay is used here to avoid the test code run before the browser 
    // code.
    executeSoon(function() {
      is($icon.attr("src"), contentWindow.Utils.defaultFaviconURL, 
         "The icon is showing th default fav icon");

      // clean up
      gBrowser.removeTab(newTab);
      let endGame = function() {
        window.removeEventListener("tabviewhidden", endGame, false);

        ok(!TabView.isVisible(), "Tab View is hidden");
        finish();
      }
      window.addEventListener("tabviewhidden", endGame, false);
      TabView.toggle();
    });
  };
  newTab.addEventListener("error", errorHandler, false);

  newTab.linkedBrowser.loadURI(
    "http://mochi.test:8888/browser/browser/base/content/test/tabview/test_bug600645.html");
}
