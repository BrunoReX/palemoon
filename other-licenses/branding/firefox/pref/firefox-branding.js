pref("startup.homepage_override_url","http://www.palemoon.org/");
pref("startup.homepage_welcome_url","http://www.palemoon.org/firstrun.shtml");
// Interval: Time between checks for a new version (in seconds) -- 1 week for Pale Moon
pref("app.update.interval", 604800);
pref("app.update.auto", false);
pref("app.update.enabled", true);
// URL for update checks, re-enabled on palemoon.org (369)
pref("app.update.url", "http://www.palemoon.org/update/%VERSION%/update.xml");
// URL user can browse to manually if for some reason all update installation
// attempts fail.
pref("app.update.url.manual", "http://www.palemoon.org/");
// A default value for the "More information about this update" link
// supplied in the "An update is available" page of the update wizard. 
pref("app.update.url.details", "http://www.palemoon.org/");

// Release notes URL
pref("app.releaseNotesURL", "http://www.palemoon.org/releasenotes.shtml");

pref("browser.search.param.yahoo-fr", "moz35");
pref("browser.search.param.yahoo-fr-cjkt", "moz35");

//Palemoon tweaks
pref("network.prefetch-next", false); //prefetching engine off by default!
pref("network.http.pipelining"      , true); //pipelining on by default, haven't seen any issues
pref("network.http.pipelining.ssl"  , false); //disable pipelining over SSL
pref("network.http.proxy.pipelining", false); //pipeline proxy requests - breaks a number of bad proxies (3617)
pref("network.http.pipelining.maxrequests" , 12);  // Max number of requests in the pipeline

pref("browser.tabs.insertRelatedAfterCurrent", false); //use old method of tabbed browsing instead of "Chrome" style
pref("general.warnOnAboutConfig", false); //about:config warning. annoying. I don't give warranty.
pref("browser.download.useDownloadDir", false); //don't use default download location as standard. ASK.

//Fix useragent for annoying websites
pref("general.useragent.extra.firefox", "Firefox/3.6.31 (Palemoon/3.6.31)");

//Automatically export bookmarks to HTML (361)
pref("browser.bookmarks.autoExportHTML", true);

//Ctrl-Tab page previews (361)
pref("browser.ctrlTab.previews", true);
//All tabs previews & button on tab bar (3615)
pref("browser.allTabs.previews",true);

//Downloadmanager (361)
pref("browser.download.manager.flashCount", 10);
pref("browser.download.manager.scanWhenDone", false);

//plugin kill timeout (366)
pref("dom.ipc.plugins.timeoutSecs", 20);

//support url fix
pref("app.support.baseURL", "http://www.palemoon.org/support/");

//DNS handling (368)
pref("network.dnsCacheEntries", 1024); //cache 1024 instead of 20
pref("network.dnsCacheExpiration", 3600); //TTL 1 hour

//Slightly lower default initial rendering delay (368)
pref("nglayout.initialpaint.delay", 150);

//Add-on window fixes (368)
pref("extensions.getAddons.browseAddons", "https://addons.mozilla.org/%LOCALE%/firefox");
pref("extensions.getAddons.maxResults", 10);
pref("extensions.getAddons.recommended.browseURL", "https://addons.mozilla.org/%LOCALE%/firefox/recommended");
pref("extensions.getAddons.recommended.url", "https://services.addons.mozilla.org/%LOCALE%/firefox/api/%API_VERSION%/list/featured/all/10/%OS%/%VERSION%");
pref("extensions.getAddons.search.browseURL", "https://addons.mozilla.org/%LOCALE%/firefox/search?q=%TERMS%");
pref("extensions.getAddons.search.url", "https://services.addons.mozilla.org/%LOCALE%/firefox/api/%API_VERSION%/search/%TERMS%/all/10/%OS%/%VERSION%");
pref("extensions.getMoreThemesURL", "https://addons.mozilla.org/%LOCALE%/firefox/getpersonas");
pref("extensions.blocklist.url", "https://addons.mozilla.org/blocklist/3/firefox/%APP_VERSION%/%PRODUCT%/%BUILD_ID%/%BUILD_TARGET%/%LOCALE%/%CHANNEL%/%OS_VERSION%/%DISTRIBUTION%/%DISTRIBUTION_VERSION%/");

//Search engine fixes (3611)
pref("browser.search.searchEnginesURL", "https://addons.mozilla.org/%LOCALE%/firefox/search-engines/");

//Safebrowsing URL fixes (3613)
pref("browser.safebrowsing.provider.0.gethashURL", "http://safebrowsing.clients.google.com/safebrowsing/gethash?client=navclient-auto-ffox&appver={moz:version}&pver=2.2");
pref("browser.safebrowsing.provider.0.keyURL", "https://sb-ssl.google.com/safebrowsing/newkey?client=palemoon&appver={moz:version}&pver=2.2");
pref("browser.safebrowsing.provider.0.lookupURL", "http://safebrowsing.clients.google.com/safebrowsing/lookup?sourceid=firefox-antiphish&features=TrustRank&client=navclient-auto-ffox&appver={moz:version}&");
pref("browser.safebrowsing.provider.0.updateURL", "http://safebrowsing.clients.google.com/safebrowsing/downloads?client=navclient-auto-ffox&appver={moz:version}&pver=2.2");
pref("browser.safebrowsing.warning.infoURL", "http://www.mozilla.com/%LOCALE%/firefox/phishing-protection/");
//Dictionary URL (3613)
pref("browser.dictionaries.download.url", "https://addons.mozilla.org/%LOCALE%/firefox/dictionaries/");
//Geolocation info URL (3613)
pref("browser.geolocation.warning.infoURL", "http://www.mozilla.com/%LOCALE%/firefox/geolocation/");

//Disable DNS prefetching to prevent router hangups (3628)
pref("network.dns.disablePrefetch",true);