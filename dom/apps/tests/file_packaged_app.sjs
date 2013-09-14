var Cc = Components.classes;
var Ci = Components.interfaces;
var Cu = Components.utils;

// From prio.h
const PR_RDWR        = 0x04;
const PR_CREATE_FILE = 0x08;
const PR_TRUNCATE    = 0x20;

Cu.import("resource://gre/modules/FileUtils.jsm");
Cu.import("resource://gre/modules/NetUtil.jsm");

var gBasePath = "tests/dom/apps/tests/";
var gMiniManifestTemplate = "file_packaged_app.template.webapp";
var gAppTemplate = "file_packaged_app.template.html";
var gAppName = "appname";
var gDevName = "devname";
var gDevUrl = "http://dev.url";

function handleRequest(request, response) {
  var query = getQuery(request);

  response.setHeader("Access-Control-Allow-Origin", "*", false);

  var packageSize = ("packageSize" in query) ? query.packageSize : 0;
  var appName = ("appName" in query) ? query.appName : gAppName;
  var devName = ("devName" in query) ? query.devName : gDevName;
  var devUrl = ("devUrl" in query) ? query.devUrl : gDevUrl;

  // If this is a version update, update state, prepare the manifest,
  // the application package and return.
  if ("setVersion" in query) {
    var version = query.setVersion;
    setState("version", version);

    var packageName = "test_packaged_app_" + version + ".zip";
    setState("packageName", packageName);
    var packagePath = "/" + gBasePath + "file_packaged_app.sjs?getPackage=" +
                      packageName;
    setState("packagePath", packagePath);

    // Create the application package.
    var zipWriter = Cc["@mozilla.org/zipwriter;1"]
                    .createInstance(Ci.nsIZipWriter);
    var zipFile = FileUtils.getFile("TmpD", [packageName]);
    zipWriter.open(zipFile, PR_RDWR | PR_CREATE_FILE | PR_TRUNCATE);

    // We want to run some tests without the manifest included in the zip.
    if (version != "0") {
      var manifestTemplate = gBasePath + gMiniManifestTemplate;
      var manifest = makeResource(manifestTemplate, version, packagePath,
                                  packageSize, appName, devName, devUrl);
      addZipEntry(zipWriter, manifest, "manifest.webapp");
    }

    var appTemplate = gBasePath + gAppTemplate;
    var app = makeResource(appTemplate, version, packagePath, packageSize,
                           appName, devName, devUrl);
    addZipEntry(zipWriter, app, "index.html");

    zipWriter.close();

    response.setHeader("Content-Type", "text/html", false);
    response.write("OK");
    return;
  }

  // Get the version from server state
  var version = Number(getState("version"));
  var packageName = String(getState("packageName"));
  var packagePath = String(getState("packagePath"));

  var etag = getEtag(request, version);

  if (etagMatches(request, etag)) {
    dump("Etags Match. Sending 304\n");
    response.setStatusLine(request.httpVersion, "304", "Not modified");
    return;
  }

  response.setHeader("Etag", etag, false);

  // Serve the application package corresponding to the requested app version.
  if ("getPackage" in query) {
    var resource = readFile(packageName, true);
    response.setHeader("Content-Type",
                       "Content-Type: application/java-archive", false);
    response.write(resource);
    return;
  }

  // Serve the mini-manifest corresponding to the requested app version.
  if ("getManifest" in query) {
    var template = gBasePath + gMiniManifestTemplate;
    if (!("noManifestContentType" in query)) {
      response.setHeader("Content-Type",
                         "application/x-web-app-manifest+json", false);
    }
    packagePath = "wrongPackagePath" in query ? "" : packagePath;
    var manifest = makeResource(template, version, packagePath, packageSize,
                                appName, devName, devUrl);
    response.write(manifest);
    return;
  }

  response.setHeader("Content-type", "text-html", false);
  response.write("KO");
}

function getQuery(request) {
  var query = {};
  request.queryString.split('&').forEach(function (val) {
    var [name, value] = val.split('=');
    query[name] = unescape(value);
  });
  return query;
}

function getEtag(request, version) {
  return request.queryString.replace(/&/g, '-').replace(/=/g, '-') +
         '-' + version;
}

function etagMatches(request, etag) {
  return request.hasHeader("If-None-Match") &&
         request.getHeader("If-None-Match") == etag;
}

// File and resources helpers

function addZipEntry(zipWriter, entry, entryName) {
  var stream = Cc["@mozilla.org/io/string-input-stream;1"]
               .createInstance(Ci.nsIStringInputStream);
  stream.setData(entry, entry.length);
  zipWriter.addEntryStream(entryName, Date.now(),
                           Ci.nsIZipWriter.COMPRESSION_BEST, stream, false);
}

function readFile(path, fromTmp) {
  var dir = fromTmp ? "TmpD" : "CurWorkD";
  var file = Cc["@mozilla.org/file/directory_service;1"]
             .getService(Ci.nsIProperties)
             .get(dir, Ci.nsILocalFile);
  var fstream = Cc["@mozilla.org/network/file-input-stream;1"]
                .createInstance(Ci.nsIFileInputStream);
  var split = path.split("/");
  for(var i = 0; i < split.length; ++i) {
    file.append(split[i]);
  }
  fstream.init(file, -1, 0, 0);
  var data = NetUtil.readInputStreamToString(fstream, fstream.available());
  fstream.close();
  return data;
}

function makeResource(templatePath, version, packagePath, packageSize,
                      appName, developerName, developerUrl) {
  var res = readFile(templatePath, false)
            .replace(/VERSIONTOKEN/g, version)
            .replace(/PACKAGEPATHTOKEN/g, packagePath)
            .replace(/PACKAGESIZETOKEN/g, packageSize)
            .replace(/NAMETOKEN/g, appName)
            .replace(/DEVELOPERTOKEN/g, developerName)
            .replace(/DEVELOPERURLTOKEN/g, developerUrl);
  return res;
}
