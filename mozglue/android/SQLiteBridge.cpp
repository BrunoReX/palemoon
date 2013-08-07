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
 * The Original Code is Mozilla Android code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2012
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Gian-Carlo Pascutto <gpascutto@mozilla.com>
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

#include <stdlib.h>
#include <jni.h>
#include <android/log.h>
#include "dlfcn.h"
#include "APKOpen.h"
#ifndef MOZ_OLD_LINKER
#include "ElfLoader.h"
#endif
#include "SQLiteBridge.h"

#ifdef DEBUG
#define LOG(x...) __android_log_print(ANDROID_LOG_INFO, "GeckoJNI", x)
#else
#define LOG(x...)
#endif

#define SQLITE_WRAPPER_INT(name) name ## _t f_ ## name;

SQLITE_WRAPPER_INT(sqlite3_open)
SQLITE_WRAPPER_INT(sqlite3_errmsg)
SQLITE_WRAPPER_INT(sqlite3_prepare_v2)
SQLITE_WRAPPER_INT(sqlite3_bind_parameter_count)
SQLITE_WRAPPER_INT(sqlite3_bind_text)
SQLITE_WRAPPER_INT(sqlite3_step)
SQLITE_WRAPPER_INT(sqlite3_column_count)
SQLITE_WRAPPER_INT(sqlite3_finalize)
SQLITE_WRAPPER_INT(sqlite3_close)
SQLITE_WRAPPER_INT(sqlite3_column_name)
SQLITE_WRAPPER_INT(sqlite3_column_type)
SQLITE_WRAPPER_INT(sqlite3_column_blob)
SQLITE_WRAPPER_INT(sqlite3_column_bytes)
SQLITE_WRAPPER_INT(sqlite3_column_text)

void setup_sqlite_functions(void *sqlite_handle)
{
#define GETFUNC(name) f_ ## name = (name ## _t) __wrap_dlsym(sqlite_handle, #name)
  GETFUNC(sqlite3_open);
  GETFUNC(sqlite3_errmsg);
  GETFUNC(sqlite3_prepare_v2);
  GETFUNC(sqlite3_bind_parameter_count);
  GETFUNC(sqlite3_bind_text);
  GETFUNC(sqlite3_step);
  GETFUNC(sqlite3_column_count);
  GETFUNC(sqlite3_finalize);
  GETFUNC(sqlite3_close);
  GETFUNC(sqlite3_column_name);
  GETFUNC(sqlite3_column_type);
  GETFUNC(sqlite3_column_blob);
  GETFUNC(sqlite3_column_bytes);
  GETFUNC(sqlite3_column_text);
#undef GETFUNC
}

static bool initialized = false;
static jclass stringClass;
static jclass objectClass;
static jclass byteBufferClass;
static jclass arrayListClass;
static jmethodID jByteBufferAllocateDirect;
static jmethodID jArrayListAdd;
static jobject jNull;

static void
JNI_Throw(JNIEnv* jenv, const char* name, const char* msg)
{
    jclass cls = jenv->FindClass(name);
    if (cls == NULL) {
        LOG("Couldn't find exception class (or exception pending)\n");
        return;
    }
    int rc = jenv->ThrowNew(cls, msg);
    if (rc < 0) {
        LOG("Error throwing exception\n");
    }
    jenv->DeleteLocalRef(cls);
}

static void
JNI_Setup(JNIEnv* jenv)
{
    if (initialized) return;

    objectClass     = jenv->FindClass("java/lang/Object");
    stringClass     = jenv->FindClass("java/lang/String");
    byteBufferClass = jenv->FindClass("java/nio/ByteBuffer");
    arrayListClass  = jenv->FindClass("java/util/ArrayList");
    jNull           = jenv->NewGlobalRef(NULL);

    if (stringClass == NULL || objectClass == NULL
        || byteBufferClass == NULL || arrayListClass == NULL) {
        LOG("Error finding classes");
        JNI_Throw(jenv, "org/mozilla/gecko/sqlite/SQLiteBridgeException",
                  "FindClass error");
        return;
    }

    // public static ByteBuffer allocateDirect(int capacity)
    jByteBufferAllocateDirect =
        jenv->GetStaticMethodID(byteBufferClass, "allocateDirect", "(I)Ljava/nio/ByteBuffer;");
    // boolean add(Object o)
    jArrayListAdd =
        jenv->GetMethodID(arrayListClass, "add", "(Ljava/lang/Object;)Z");

    if (jByteBufferAllocateDirect == NULL || jArrayListAdd == NULL) {
        LOG("Error finding methods");
        JNI_Throw(jenv, "org/mozilla/gecko/sqlite/SQLiteBridgeException",
                  "GetMethodId error");
        return;
    }

    initialized = true;
}

extern "C" NS_EXPORT void JNICALL
Java_org_mozilla_gecko_sqlite_SQLiteBridge_sqliteCall(JNIEnv* jenv, jclass,
                                                      jstring jDb,
                                                      jstring jQuery,
                                                      jobjectArray jParams,
                                                      jobject jColumns,
                                                      jobject jArrayList)
{
    JNI_Setup(jenv);

    const char* queryStr;
    queryStr = jenv->GetStringUTFChars(jQuery, NULL);

    const char* dbPath;
    dbPath = jenv->GetStringUTFChars(jDb, NULL);

    const char *pzTail;
    sqlite3_stmt *ppStmt;
    sqlite3 *db;
    int rc;
    rc = f_sqlite3_open(dbPath, &db);
    jenv->ReleaseStringUTFChars(jDb, dbPath);

    if (rc != SQLITE_OK) {
        LOG("Can't open database: %s\n", f_sqlite3_errmsg(db));
        goto error_close;
    }

    rc = f_sqlite3_prepare_v2(db, queryStr, -1, &ppStmt, &pzTail);
    if (rc != SQLITE_OK || ppStmt == NULL) {
        LOG("Can't prepare statement: %s\n", f_sqlite3_errmsg(db));
        goto error_close;
    }
    jenv->ReleaseStringUTFChars(jQuery, queryStr);

    // Check if number of parameters matches
    jsize numPars;
    numPars = jenv->GetArrayLength(jParams);
    int sqlNumPars;
    sqlNumPars = f_sqlite3_bind_parameter_count(ppStmt);
    if (numPars != sqlNumPars) {
        LOG("Passed parameter count (%d) doesn't match SQL parameter count (%d)\n",
            numPars, sqlNumPars);
        goto error_close;
    }
    // Bind parameters, if any
    if (numPars > 0) {
        for (int i = 0; i < numPars; i++) {
            jobject jObjectParam = jenv->GetObjectArrayElement(jParams, i);
            // IsInstanceOf or isAssignableFrom? String is final, so IsInstanceOf
            // should be OK.
            jboolean isString = jenv->IsInstanceOf(jObjectParam, stringClass);
            if (isString != JNI_TRUE) {
                LOG("Parameter is not of String type");
                goto error_close;
            }
            jstring jStringParam = (jstring)jObjectParam;
            const char* paramStr = jenv->GetStringUTFChars(jStringParam, NULL);
            // SQLite parameters index from 1.
            rc = f_sqlite3_bind_text(ppStmt, i + 1, paramStr, -1, SQLITE_TRANSIENT);
            jenv->ReleaseStringUTFChars(jStringParam, paramStr);
            if (rc != SQLITE_OK) {
                LOG("Error binding query parameter");
                goto error_close;
            }
        }
    }

    // Execute the query and step through the results
    rc = f_sqlite3_step(ppStmt);
    if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
        LOG("Can't step statement: (%d) %s\n", rc, f_sqlite3_errmsg(db));
        goto error_close;
    }

    // Get the column names
    int cols;
    cols = f_sqlite3_column_count(ppStmt);
    for (int i = 0; i < cols; i++) {
        const char* colName = f_sqlite3_column_name(ppStmt, i);
        jstring jStr = jenv->NewStringUTF(colName);
        jenv->CallBooleanMethod(jColumns, jArrayListAdd, jStr);
        jenv->DeleteLocalRef(jStr);
    }

    // For each row, add an Object[] to the passed ArrayList,
    // with that containing either String or ByteArray objects
    // containing the columns
    while (rc != SQLITE_DONE) {
        // Process row
        // Construct Object[]
        jobjectArray jRow = jenv->NewObjectArray(cols,
                                                 objectClass,
                                                 NULL);
        if (jRow == NULL) {
            LOG("Can't allocate jRow Object[]\n");
            goto error_close;
        }

        for (int i = 0; i < cols; i++) {
            int colType = f_sqlite3_column_type(ppStmt, i);
            if (colType == SQLITE_BLOB) {
                // Treat as blob
                const void* blob = f_sqlite3_column_blob(ppStmt, i);
                int colLen = f_sqlite3_column_bytes(ppStmt, i);

                // Construct ByteBuffer of correct size
                jobject jByteBuffer =
                    jenv->CallStaticObjectMethod(byteBufferClass,
                                                 jByteBufferAllocateDirect,
                                                 colLen);
                if (jByteBuffer == NULL) {
                    goto error_close;
                }

                // Get its backing array
                void* bufferArray = jenv->GetDirectBufferAddress(jByteBuffer);
                if (bufferArray == NULL) {
                    LOG("Failure calling GetDirectBufferAddress\n");
                    goto error_close;
                }
                memcpy(bufferArray, blob, colLen);

                jenv->SetObjectArrayElement(jRow, i, jByteBuffer);
                jenv->DeleteLocalRef(jByteBuffer);
            } else if (colType == SQLITE_NULL) {
                jenv->SetObjectArrayElement(jRow, i, jNull);
            } else {
                // Treat everything else as text
                const char* txt = (const char*)f_sqlite3_column_text(ppStmt, i);
                jstring jStr = jenv->NewStringUTF(txt);
                jenv->SetObjectArrayElement(jRow, i, jStr);
                jenv->DeleteLocalRef(jStr);
            }
        }

        // Append Object[] to ArrayList<Object[]>
        // JNI doesn't know about the generic, so use Object[] as Object
        jenv->CallBooleanMethod(jArrayList, jArrayListAdd, jRow);

        // Clean up
        jenv->DeleteLocalRef(jRow);

        // Get next row
        rc = f_sqlite3_step(ppStmt);
        // Real error?
        if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
            LOG("Can't re-step statement:(%d) %s\n", rc, f_sqlite3_errmsg(db));
            goto error_close;
        }
    }

    rc = f_sqlite3_finalize(ppStmt);
    if (rc != SQLITE_OK) {
        LOG("Can't finalize statement: %s\n", f_sqlite3_errmsg(db));
        goto error_close;
    }

    f_sqlite3_close(db);
    return;

error_close:
    f_sqlite3_close(db);
    JNI_Throw(jenv, "org/mozilla/gecko/sqlite/SQLiteBridgeException", "SQLite error");
    return;
}
