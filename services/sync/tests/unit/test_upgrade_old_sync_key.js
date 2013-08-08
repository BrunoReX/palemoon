/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

Cu.import("resource://services-sync/constants.js");
Cu.import("resource://services-sync/identity.js");
Cu.import("resource://services-sync/main.js");
var btoa = Cu.import("resource://services-sync/util.js").btoa;

// Test upgrade of a dashed old-style sync key.
function run_test() {
  const PBKDF2_KEY_BYTES = 16;
  initTestLogging("Trace");

  let passphrase = "abcde-abcde-abcde-abcde";
  do_check_false(Utils.isPassphrase(passphrase));

  let normalized = Utils.normalizePassphrase(passphrase);
  _("Normalized: " + normalized);

  // Still not a modern passphrase...
  do_check_false(Utils.isPassphrase(normalized));

  // ... but different.
  do_check_neq(normalized, passphrase);
  do_check_eq(normalized, "abcdeabcdeabcdeabcde");

  // Now run through the upgrade.
  Identity.account = "johndoe";
  Weave.Service.syncID = "1234567890";
  Identity.syncKey = normalized; // UI normalizes.
  do_check_false(Utils.isPassphrase(Identity.syncKey));
  Weave.Service.upgradeSyncKey(Weave.Service.syncID);
  let upgraded = Identity.syncKey;
  _("Upgraded: " + upgraded);
  do_check_true(Utils.isPassphrase(upgraded));

  // Now let's verify that it's been derived correctly, from the normalized
  // version, and the encoded sync ID.
  _("Sync ID: " + Weave.Service.syncID);
  let derivedKeyStr =
    Utils.derivePresentableKeyFromPassphrase(normalized,
                                             btoa(Weave.Service.syncID),
                                             PBKDF2_KEY_BYTES, true);
  _("Derived: " + derivedKeyStr);

  // Success!
  do_check_eq(derivedKeyStr, upgraded);
}
