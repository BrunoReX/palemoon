pref("startup.homepage_override_url","http://www.palemoon.org/");
pref("startup.homepage_welcome_url","http://www.palemoon.org/firstrun.shtml");
pref("browser.panorama.welcome_url", "http://www.mozilla.com/firefox/panorama/");
// Interval: Time between checks for a new version (in seconds) -- 1 week for Pale Moon
pref("app.update.interval", 604800);
pref("app.update.auto", false);
pref("app.update.enabled", true);
// URL for update checks, re-enabled on palemoon.org (369)
pref("app.update.url", "http://www.palemoon.org/update/%VERSION%/update.xml");
// The time interval between the downloading of mar file chunks in the
// background (in seconds)
pref("app.update.download.backgroundInterval", 600);
// URL user can browse to manually if for some reason all update installation
// attempts fail.
pref("app.update.url.manual", "http://www.palemoon.org/");
// A default value for the "More information about this update" link
// supplied in the "An update is available" page of the update wizard. 
pref("app.update.url.details", "http://www.palemoon.org/");
// Additional Update fixes - no SSL damnit, I don't have a cert (4.0)
pref("app.update.cert.checkAttributes", false);
pref("app.update.cert.requireBuiltIn", false);

// Release notes and vendor URLs
pref("app.releaseNotesURL", "http://www.palemoon.org/releasenotes.shtml");
pref("app.vendorURL", "http://www.palemoon.org/");

pref("browser.search.param.ms-pc", "MOZI");
pref("browser.search.param.yahoo-fr", "moz35");
pref("browser.search.param.yahoo-fr-cjkt", "moz35"); // now unused
pref("browser.search.param.yahoo-fr-ja", "mozff");

//Palemoon tweaks
pref("network.prefetch-next", false); //prefetching engine off by default!
pref("network.http.pipelining"      , true); //pipelining on by default, haven't seen any issues
pref("network.http.pipelining.ssl"  , false); // disable pipelining over SSL
pref("network.http.proxy.pipelining", false); // pipeline proxy requests - breaks some proxies! (406)
pref("network.http.pipelining.maxrequests" , 16);  // Max number of requests in the pipeline

pref("browser.tabs.insertRelatedAfterCurrent", false); //use old method of tabbed browsing instead of "Chrome" style
pref("general.warnOnAboutConfig", false); //about:config warning. annoying. I don't give warranty.
pref("browser.download.useDownloadDir", false); //don't use default download location as standard. ASK.

//Fix useragent for annoying websites
pref("general.useragent.compatMode.firefox", true);

//Ctrl-Tab page previews (361)
pref("browser.ctrlTab.previews", true);
//All Tabs previews (3615/400)
pref("browser.allTabs.previews", true);

//Downloadmanager (361)
pref("browser.download.manager.flashCount", 10);
pref("browser.download.manager.scanWhenDone", false);

//plugin kill timeout (366)
pref("dom.ipc.plugins.timeoutSecs", 20);

//support url
pref("app.support.baseURL", "http://www.palemoon.org/support/");

//DNS handling (368)
pref("network.dnsCacheEntries", 1024); //cache 1024 instead of 20
pref("network.dnsCacheExpiration", 3600); //TTL 1 hour

//Slightly lower default initial rendering delay (368)
pref("nglayout.initialpaint.delay", 150);

//webGL (4.0)
pref("webgl.prefer-native-gl", true);
//enable it even if ANGLE isn't built. Will disable anyway if no driver present.
pref("webgl.force-enabled", true); 

//D2D/DirectWrite (4.0)
//Disabled by default after finding the poor IGFX support for it... (402)
pref("gfx.font_rendering.directwrite.enabled", false);
//D2D force may cause issues for poor drivers, so off by default.
pref("gfx.direct2d.force-enabled", false);

//JIT the chrome! (402)
pref("javascript.options.jitprofiling.chrome", true);
pref("javascript.options.methodjit.chrome", true);
pref("javascript.options.methodjit_always", true);

//Add-on window fixes (368)
pref("extensions.getAddons.browseAddons", "https://addons.mozilla.org/%LOCALE%/firefox");
pref("extensions.getAddons.maxResults", 10);
pref("extensions.getAddons.recommended.browseURL", "https://addons.mozilla.org/%LOCALE%/firefox/recommended");
pref("extensions.getAddons.recommended.url", "https://services.addons.mozilla.org/%LOCALE%/firefox/api/%API_VERSION%/list/featured/all/10/%OS%/%VERSION%");
pref("extensions.getAddons.search.browseURL", "https://addons.mozilla.org/%LOCALE%/firefox/search?q=%TERMS%");
pref("extensions.getAddons.search.url", "https://services.addons.mozilla.org/%LOCALE%/firefox/api/%API_VERSION%/search/%TERMS%/all/10/%OS%/%VERSION%");
pref("extensions.getMoreThemesURL", "https://addons.mozilla.org/%LOCALE%/firefox/getpersonas");
pref("extensions.blocklist.url", "https://addons.mozilla.org/blocklist/3/firefox/%APP_VERSION%/%PRODUCT%/%BUILD_ID%/%BUILD_TARGET%/%LOCALE%/%CHANNEL%/%OS_VERSION%/%DISTRIBUTION%/%DISTRIBUTION_VERSION%/");
//MORE add-ons fixes (405)
pref("extensions.webservice.discoverURL","https://services.addons.mozilla.org/%LOCALE%/firefox/discovery/pane/%VERSION%/%OS%");
pref("extensions.getAddons.get.url","https://services.addons.mozilla.org/%LOCALE%/firefox/api/%API_VERSION%/search/guid:%IDS%?src=firefox&appOS=%OS%&appVersion=%VERSION%&tMain=%TIME_MAIN%&tFirstPaint=%TIME_FIRST_PAINT%&tSessionRestored=%TIME_SESSION_RESTORED%");

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

//Pale Moon 5 specific

//Pale Moon 6 specific

//give people a choice for add-on updates.
pref("extensions.update.autoUpdateDefault", false);
