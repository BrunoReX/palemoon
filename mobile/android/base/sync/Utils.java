/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.sync;

import java.io.BufferedReader;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.UnsupportedEncodingException;
import java.math.BigDecimal;
import java.math.BigInteger;
import java.net.URLDecoder;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;

import org.json.simple.JSONArray;
import org.mozilla.apache.commons.codec.binary.Base32;
import org.mozilla.apache.commons.codec.binary.Base64;

import android.content.Context;
import android.content.SharedPreferences;

public class Utils {

  private static final String LOG_TAG = "Utils";

  private static SecureRandom sharedSecureRandom = new SecureRandom();

  // See <http://developer.android.com/reference/android/content/Context.html#getSharedPreferences%28java.lang.String,%20int%29>
  public static final int SHARED_PREFERENCES_MODE = 0;

  public static String generateGuid() {
    byte[] encodedBytes = Base64.encodeBase64(generateRandomBytes(9), false);
    return new String(encodedBytes).replace("+", "-").replace("/", "_");
  }

  /**
   * Helper to generate secure random bytes.
   *
   * @param length
   *        Number of bytes to generate.
   */
  public static byte[] generateRandomBytes(int length) {
    byte[] bytes = new byte[length];
    sharedSecureRandom.nextBytes(bytes);
    return bytes;
  }

  /**
   * Helper to generate a random integer in a specified range.
   *
   * @param r
   *        Generate an integer between 0 and r-1 inclusive.
   */
  public static BigInteger generateBigIntegerLessThan(BigInteger r) {
    int maxBytes = (int) Math.ceil(((double) r.bitLength()) / 8);
    BigInteger randInt = new BigInteger(generateRandomBytes(maxBytes));
    return randInt.mod(r);
  }

  /**
   * Helper to reseed the shared secure random number generator.
   */
  public static void reseedSharedRandom() {
    sharedSecureRandom.setSeed(sharedSecureRandom.generateSeed(8));
  }

  /**
   * Helper to convert a byte array to a hex-encoded string
   */
  public static String byte2hex(byte[] b) {
    // StringBuffer should be used instead.
    String hs = "";
    String stmp;

    for (int n = 0; n < b.length; n++) {
      stmp = java.lang.Integer.toHexString(b[n] & 0XFF);

      if (stmp.length() == 1) {
        hs = hs + "0" + stmp;
      } else {
        hs = hs + stmp;
      }

      if (n < b.length - 1) {
        hs = hs + "";
      }
    }

    return hs;
  }

  public static byte[] concatAll(byte[] first, byte[]... rest) {
    int totalLength = first.length;
    for (byte[] array : rest) {
      totalLength += array.length;
    }

    byte[] result = new byte[totalLength];
    int offset = first.length;

    System.arraycopy(first, 0, result, 0, offset);

    for (byte[] array : rest) {
      System.arraycopy(array, 0, result, offset, array.length);
      offset += array.length;
    }
    return result;
  }

  /**
   * Utility for Base64 decoding. Should ensure that the correct
   * Apache Commons version is used.
   *
   * @param base64
   *        An input string. Will be decoded as UTF-8.
   * @return
   *        A byte array of decoded values.
   * @throws UnsupportedEncodingException
   *         Should not occur.
   */
  public static byte[] decodeBase64(String base64) throws UnsupportedEncodingException {
    return Base64.decodeBase64(base64.getBytes("UTF-8"));
  }

  public static byte[] decodeFriendlyBase32(String base32) {
    Base32 converter = new Base32();
    final String translated = base32.replace('8', 'l').replace('9', 'o');
    return converter.decode(translated.toUpperCase());
  }

  public static byte[] hex2Byte(String str) {
    if (str.length() % 2 == 1) {
      str = "0" + str;
    }

    byte[] bytes = new byte[str.length() / 2];
    for (int i = 0; i < bytes.length; i++) {
      bytes[i] = (byte) Integer.parseInt(str.substring(2 * i, 2 * i + 2), 16);
    }
    return bytes;
  }

  public static String millisecondsToDecimalSecondsString(long ms) {
    return millisecondsToDecimalSeconds(ms).toString();
  }

  // For dumping into JSON without quotes.
  public static BigDecimal millisecondsToDecimalSeconds(long ms) {
    return new BigDecimal(ms).movePointLeft(3);
  }

  // This lives until Bug 708956 lands, and we don't have to do it any more.
  public static long decimalSecondsToMilliseconds(String decimal) {
    try {
      return new BigDecimal(decimal).movePointRight(3).longValue();
    } catch (Exception e) {
      return -1;
    }
  }

  // Oh, Java.
  public static long decimalSecondsToMilliseconds(Double decimal) {
    // Truncates towards 0.
    return (long)(decimal * 1000);
  }
  public static long decimalSecondsToMilliseconds(Long decimal) {
    return decimal * 1000;
  }
  public static long decimalSecondsToMilliseconds(Integer decimal) {
    return (long)(decimal * 1000);
  }

  public static byte[] sha1(String utf8)
      throws NoSuchAlgorithmException, UnsupportedEncodingException {
    MessageDigest sha1 = MessageDigest.getInstance("SHA-1");
    return sha1.digest(utf8.getBytes("UTF-8"));
  }

  public static String sha1Base32(String utf8)
      throws NoSuchAlgorithmException, UnsupportedEncodingException {
    return new Base32().encodeAsString(sha1(utf8)).toLowerCase(Locale.US);
  }

  public static String getPrefsPath(String username, String serverURL)
    throws NoSuchAlgorithmException, UnsupportedEncodingException {
    return "sync.prefs." + sha1Base32(serverURL + ":" + username);
  }

  public static SharedPreferences getSharedPreferences(Context context, String username, String serverURL) throws NoSuchAlgorithmException, UnsupportedEncodingException {
    String prefsPath = getPrefsPath(username, serverURL);
    Logger.debug(LOG_TAG, "Shared preferences: " + prefsPath);
    return context.getSharedPreferences(prefsPath, SHARED_PREFERENCES_MODE);
  }

  public static void addToIndexBucketMap(TreeMap<Long, ArrayList<String>> map, long index, String value) {
    ArrayList<String> bucket = map.get(index);
    if (bucket == null) {
      bucket = new ArrayList<String>();
    }
    bucket.add(value);
    map.put(index, bucket);
  }

  /**
   * Yes, an equality method that's null-safe.
   */
  private static boolean same(Object a, Object b) {
    if (a == b) {
      return true;
    }
    if (a == null || b == null) {
      return false;      // If both null, case above applies.
    }
    return a.equals(b);
  }

  /**
   * Return true if the two arrays are both null, or are both arrays
   * containing the same elements in the same order.
   */
  public static boolean sameArrays(JSONArray a, JSONArray b) {
    if (a == b) {
      return true;
    }
    if (a == null || b == null) {
      return false;
    }
    final int size = a.size();
    if (size != b.size()) {
      return false;
    }
    for (int i = 0; i < size; ++i) {
      if (!same(a.get(i), b.get(i))) {
        return false;
      }
    }
    return true;
  }

  /**
   * Takes a URI, extracting URI components.
   * @param scheme the URI scheme on which to match.
   */
  public static Map<String, String> extractURIComponents(String scheme, String uri) {
    if (uri.indexOf(scheme) != 0) {
      throw new IllegalArgumentException("URI scheme does not match: " + scheme);
    }

    // Do this the hard way to avoid taking a large dependency on
    // HttpClient or getting all regex-tastic.
    String components = uri.substring(scheme.length());
    HashMap<String, String> out = new HashMap<String, String>();
    String[] parts = components.split("&");
    for (int i = 0; i < parts.length; ++i) {
      String part = parts[i];
      if (part.length() == 0) {
        continue;
      }
      String[] pair = part.split("=", 2);
      switch (pair.length) {
      case 0:
        continue;
      case 1:
        out.put(URLDecoder.decode(pair[0]), null);
        break;
      case 2:
        out.put(URLDecoder.decode(pair[0]), URLDecoder.decode(pair[1]));
        break;
      }
    }
    return out;
  }

  // Because TextUtils.join is not stubbed.
  public static String toDelimitedString(String delimiter, Collection<String> items) {
    if (items == null || items.size() == 0) {
      return "";
    }

    StringBuilder sb = new StringBuilder();
    int i = 0;
    int c = items.size();
    for (String string : items) {
      sb.append(string);
      if (++i < c) {
        sb.append(delimiter);
      }
    }
    return sb.toString();
  }

  public static String toCommaSeparatedString(Collection<String> items) {
    return toDelimitedString(", ", items);
  }

  /**
   * Read contents of file as a string.
   *
   * @param context Android context.
   * @param filename name of file to read; must not be null.
   * @return <code>String</code> instance.
   */
  public static String readFile(final Context context, final String filename) {
    if (filename == null) {
      throw new IllegalArgumentException("Passed null filename in readFile.");
    }

    FileInputStream fis = null;
    InputStreamReader isr = null;
    BufferedReader br = null;

    try {
      fis = context.openFileInput(filename);
      isr = new InputStreamReader(fis);
      br = new BufferedReader(isr);
      StringBuilder sb = new StringBuilder();
      String line;
      while ((line = br.readLine()) != null) {
        sb.append(line);
      }
      return sb.toString();
    } catch (Exception e) {
      return null;
    } finally {
      if (isr != null) {
        try {
          isr.close();
        } catch (IOException e) {
          // Ignore.
        }
      }
      if (fis != null) {
        try {
          fis.close();
        } catch (IOException e) {
          // Ignore.
        }
      }
    }
  }

  /**
   * This will take a string containing a UTF-8 representation of a UTF-8
   * byte array — e.g., "pÃ¯gÃ©ons1" — and return UTF-8 (e.g., "pïgéons1").
   *
   * This is the format produced by desktop Firefox when exchanging credentials
   * containing non-ASCII characters.
   */
  public static String decodeUTF8(final String in) throws UnsupportedEncodingException {
    final int length = in.length();
    final byte[] asciiBytes = new byte[length];
    for (int i = 0; i < length; ++i) {
      asciiBytes[i] = (byte) in.codePointAt(i);
    }
    return new String(asciiBytes, "UTF-8");
  }
}
