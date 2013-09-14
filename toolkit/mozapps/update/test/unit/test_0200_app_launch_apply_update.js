/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/* Test applying an update by staging an update and launching an application */

/**
 * The MAR file used for this test should not contain a version 2 update
 * manifest file (e.g. updatev2.manifest).
 */

const TEST_ID = "0200";

// Backup the updater.ini and use a custom one to prevent the updater from
// launching a post update executable.
const FILE_UPDATER_INI_BAK = "updater.ini.bak";

// Number of milliseconds for each do_timeout call.
const CHECK_TIMEOUT_MILLI = 1000;

// How many of CHECK_TIMEOUT_MILLI to wait before we abort the test.
const MAX_TIMEOUT_RUNS = 300;

// Maximum number of milliseconds the process that is launched can run before
// the test will try to kill it.
const APP_TIMER_TIMEOUT = 120000;

let gAppTimer;
let gProcess;
let gTimeoutRuns = 0;

function run_test() {
  if (APP_BIN_NAME == "xulrunner") {
    logTestInfo("Unable to run this test on xulrunner");
    return;
  }

  do_test_pending();
  do_register_cleanup(end_test);

  removeUpdateDirsAndFiles();

  if (!gAppBinPath) {
    do_throw("Main application binary not found... expected: " +
             APP_BIN_NAME + APP_BIN_SUFFIX);
    return;
  }

  gEnvSKipUpdateDirHashing = true;
  let channel = Services.prefs.getCharPref(PREF_APP_UPDATE_CHANNEL);
  let patches = getLocalPatchString(null, null, null, null, null, "true",
                                    STATE_PENDING);
  let updates = getLocalUpdateString(patches, null, null, null, null, null,
                                     null, null, null, null, null, null,
                                     null, "true", channel);
  writeUpdatesToXMLFile(getLocalUpdatesXMLString(updates), true);

  // Read the application.ini and use its application version
  let processDir = getCurrentProcessDir();
  let file = processDir.clone();
  file.append("application.ini");
  let ini = AUS_Cc["@mozilla.org/xpcom/ini-parser-factory;1"].
            getService(AUS_Ci.nsIINIParserFactory).
            createINIParser(file);
  let version = ini.getString("App", "Version");
  writeVersionFile(version);
  writeStatusFile(STATE_PENDING);

  // This is the directory where the update files will be located
  let updateTestDir = getUpdateTestDir();
  try {
    removeDirRecursive(updateTestDir);
  }
  catch (e) {
    logTestInfo("unable to remove directory - path: " + updateTestDir.path +
                ", exception: " + e);
  }

  // Add the directory where the update files will be added and add files that
  // will be removed.
  if (!updateTestDir.exists()) {
    updateTestDir.create(AUS_Ci.nsIFile.DIRECTORY_TYPE, PERMS_DIRECTORY);
  }
  logTestInfo("update test directory path: " + updateTestDir.path);

  file = updateTestDir.clone();
  file.append("UpdateTestRemoveFile");
  writeFile(file, "ToBeRemoved");

  file = updateTestDir.clone();
  file.append("UpdateTestAddFile");
  writeFile(file, "ToBeReplaced");

  file = updateTestDir.clone();
  file.append("removed-files");
  writeFile(file, "ToBeReplaced");

  let updatesPatchDir = getUpdatesDir();
  updatesPatchDir.append("0");
  let mar = do_get_file("data/simple.mar");
  mar.copyTo(updatesPatchDir, FILE_UPDATE_ARCHIVE);

  // Backup the updater.ini file if it exists by moving it. This prevents the
  // post update executable from being launched if it is specified.
  let updaterIni = processDir.clone();
  updaterIni.append(FILE_UPDATER_INI);
  if (updaterIni.exists()) {
    updaterIni.moveTo(processDir, FILE_UPDATER_INI_BAK);
  }

  // Backup the updater-settings.ini file if it exists by moving it.
  let updateSettingsIni = processDir.clone();
  updateSettingsIni.append(FILE_UPDATE_SETTINGS_INI);
  if (updateSettingsIni.exists()) {
    updateSettingsIni.moveTo(processDir, FILE_UPDATE_SETTINGS_INI_BAK);
  }
  updateSettingsIni = processDir.clone();
  updateSettingsIni.append(FILE_UPDATE_SETTINGS_INI);
  writeFile(updateSettingsIni, UPDATE_SETTINGS_CONTENTS);

  let launchBin = getLaunchBin();
  let args = getProcessArgs();
  logTestInfo("launching " + launchBin.path + " " + args.join(" "));

  gProcess = AUS_Cc["@mozilla.org/process/util;1"].
                createInstance(AUS_Ci.nsIProcess);
  gProcess.init(launchBin);

  gAppTimer = AUS_Cc["@mozilla.org/timer;1"].createInstance(AUS_Ci.nsITimer);
  gAppTimer.initWithCallback(gTimerCallback, APP_TIMER_TIMEOUT,
                             AUS_Ci.nsITimer.TYPE_ONE_SHOT);

  setEnvironment();

  gProcess.runAsync(args, args.length, gProcessObserver);

  resetEnvironment();
}

function end_test() {
  if (gProcess.isRunning) {
    logTestInfo("attempt to kill process");
    gProcess.kill();
  }

  if (gAppTimer) {
    logTestInfo("cancelling timer");
    gAppTimer.cancel();
    gAppTimer = null;
  }

  resetEnvironment();

  let processDir = getCurrentProcessDir();
  // Restore the backup of the updater.ini if it exists.
  let updaterIni = processDir.clone();
  updaterIni.append(FILE_UPDATER_INI_BAK);
  if (updaterIni.exists()) {
    updaterIni.moveTo(processDir, FILE_UPDATER_INI);
  }

  // Restore the backed up updater-settings.ini if it exists.
  let updateSettingsIni = processDir.clone();
  updateSettingsIni.append(FILE_UPDATE_SETTINGS_INI_BAK);
  if (updateSettingsIni.exists()) {
    updateSettingsIni.moveTo(processDir, FILE_UPDATE_SETTINGS_INI);
  }

  if (IS_WIN) {
    // Remove the copy of the application executable used for the test on
    // Windows if it exists.
    let appBinCopy = processDir.clone();
    appBinCopy.append(FILE_WIN_TEST_EXE);
    if (appBinCopy.exists()) {
      appBinCopy.remove(false);
    }
  }

  // Remove the files added by the update.
  let updateTestDir = getUpdateTestDir();
  try {
    logTestInfo("removing update test directory " + updateTestDir.path);
    removeDirRecursive(updateTestDir);
  }
  catch (e) {
    logTestInfo("unable to remove directory - path: " + updateTestDir.path +
                ", exception: " + e);
  }

  // This will delete the app console log file if it exists.
  try {
    getAppConsoleLogPath();
  }
  catch (e) {
    logTestInfo("unable to remove file during end_test. Exception: " + e);
  }

  if (IS_UNIX) {
    // This will delete the launch script if it exists.
    getLaunchScript();
    if (IS_MACOSX) {
      // This will delete the version script and version file if they exist.
      getVersionScriptAndFile();
    }
  }

  cleanUp();
}

/**
 * Gets the directory where the update adds / removes the files contained in the
 * update.
 *
 * @return  nsIFile for the directory where the update adds / removes the files
 *          contained in the update mar.
 */
function getUpdateTestDir() {
  let updateTestDir = getCurrentProcessDir();
  if (IS_MACOSX) {
    updateTestDir = updateTestDir.parent.parent;
  }
  updateTestDir.append("update_test");
  return updateTestDir;
}

/**
 * Checks if the update has finished, renames the update.log file to make
 * sure it is not in use, and then calls restoreLogFile to continue the test.
 */
function checkUpdateFinished() {
  gTimeoutRuns++;
  // Don't proceed until the update.log has been created.
  let updatesDir = getUpdatesDir();
  updatesDir.append("0");
  let log = updatesDir.clone();
  log.append(FILE_UPDATE_LOG);
  if (!log.exists()) {
    if (gTimeoutRuns > MAX_TIMEOUT_RUNS)
      do_throw("Exceeded MAX_TIMEOUT_RUNS whilst waiting for updates log to " +
               "be created at " + log.path);
    else
      do_timeout(CHECK_TIMEOUT_MILLI, checkUpdateFinished);
    return;
  }

  // Don't proceed until the update status is no longer pending or applying.
  let status = readStatusFile();
  if (status == STATE_PENDING || status == STATE_APPLYING) {
    if (gTimeoutRuns > MAX_TIMEOUT_RUNS)
      do_throw("Exceeded MAX_TIMEOUT_RUNS whilst waiting for updates status " +
               "to not be pending or applying, current status is: " + status);
    else
      do_timeout(CHECK_TIMEOUT_MILLI, checkUpdateFinished);
    return;
  }

  // Don't proceed until the update.log file can be renamed to update.log.bak
  // (bug 889860).
  try {
    log.moveTo(updatesDir, FILE_UPDATE_LOG + ".bak");
  }
  catch (e) {
    logTestInfo("unable to rename file " + FILE_UPDATE_LOG + " to " +
                FILE_UPDATE_LOG + ".bak. Exception: " + e);
    if (gTimeoutRuns > MAX_TIMEOUT_RUNS)
      do_throw("Exceeded MAX_TIMEOUT_RUNS whilst waiting to be able to " +
               "rename " + FILE_UPDATE_LOG + " to " + FILE_UPDATE_LOG +
               ".bak. Path: " + log.path);
    else
      do_timeout(CHECK_TIMEOUT_MILLI, checkUpdateFinished);
    return;
  }

  restoreLogFile();
}

/**
 * Checks if the renamed the update.log file can be renamed back to make
 * sure it is not in use and if so, continues the test.
 */
function restoreLogFile() {
  gTimeoutRuns++;
  // Don't proceed until the update.log.bak file can be renamed back to
  // update.log (bug 889860).
  let updatesDir = getUpdatesDir();
  updatesDir.append("0");
  let log = updatesDir.clone();
  log.append(FILE_UPDATE_LOG + ".bak");
  try {
    log.moveTo(updatesDir, FILE_UPDATE_LOG);
  }
  catch (e) {
    logTestInfo("unable to rename file " + FILE_UPDATE_LOG + ".bak back to " +
                FILE_UPDATE_LOG + ". Exception: " + e);
    if (gTimeoutRuns > MAX_TIMEOUT_RUNS)
      do_throw("Exceeded MAX_TIMEOUT_RUNS whilst waiting to be able to " +
               "rename " + FILE_UPDATE_LOG + ".bak back to " + FILE_UPDATE_LOG +
               ". Path: " + log.path);
    else
      do_timeout(CHECK_TIMEOUT_MILLI, restoreLogFile);
    return;
  }

  // Log the contents of the update.log so it is simpler to diagnose a test
  // failure. For example, on Windows if the application binary is in use the
  // updater will not apply the update.
  let contents = readFile(log);
  logTestInfo("contents of " + log.path + ":\n" +  
              contents.replace(/\r\n/g, "\n"));

  if (IS_WIN && contents.indexOf("NS_main: file in use") != -1) {
    do_throw("the application can't be in use when running this test");
  }

  let status = readStatusFile();
  do_check_eq(status, STATE_SUCCEEDED);

  standardInit();

  let update = gUpdateManager.getUpdateAt(0);
  do_check_eq(update.state, STATE_SUCCEEDED);

  let updateTestDir = getUpdateTestDir();

  let file = updateTestDir.clone();
  file.append("UpdateTestRemoveFile");
  do_check_false(file.exists());

  file = updateTestDir.clone();
  file.append("UpdateTestAddFile");
  do_check_true(file.exists());
  do_check_eq(readFileBytes(file), "UpdateTestAddFile\n");

  file = updateTestDir.clone();
  file.append("removed-files");
  do_check_true(file.exists());
  do_check_eq(readFileBytes(file), "update_test/UpdateTestRemoveFile\n");

  checkLogRenameFinished();
}

/**
 * Checks if the update.log file has been renamed after launch and if so,
 * continues the test.
 */
function checkLogRenameFinished() {
  gTimeoutRuns++;
  // Don't proceed until the update.log has been renamed to last-update.log.
  let log = getUpdatesDir();
  log.append("0");
  log.append(FILE_UPDATE_LOG);
  if (log.exists()) {
    if (gTimeoutRuns > MAX_TIMEOUT_RUNS)
      do_throw("Exceeded MAX_TIMEOUT_RUNS whilst waiting for update log to " +
               "be renamed to last-update.log at " + log.path);
    else
      do_timeout(CHECK_TIMEOUT_MILLI, checkLogRenameFinished);
    return;
  }

  let updatesDir = getUpdatesDir();
  log = updatesDir.clone();
  log.append("0");
  log.append(FILE_UPDATE_LOG);
  logTestInfo("testing " + log.path + " shouldn't exist");
  do_check_false(log.exists());

  log = updatesDir.clone();
  log.append(FILE_LAST_LOG);
  logTestInfo("testing " + log.path + " should exist");
  do_check_true(log.exists());

  log = updatesDir.clone();
  log.append(FILE_BACKUP_LOG);
  logTestInfo("testing " + log.path + " shouldn't exist");
  do_check_false(log.exists());

  updatesDir.append("0");
  logTestInfo("testing " + updatesDir.path + " should exist");
  do_check_true(updatesDir.exists());

  removeCallbackCopy();
}
