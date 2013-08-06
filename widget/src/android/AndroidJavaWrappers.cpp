/* -*- Mode: c++; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * ***** BEGIN LICENSE BLOCK *****
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
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Vladimir Vukicevic <vladimir@pobox.com>
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

#include "AndroidJavaWrappers.h"
#include "AndroidBridge.h"

using namespace mozilla;

jclass AndroidGeckoEvent::jGeckoEventClass = 0;
jfieldID AndroidGeckoEvent::jActionField = 0;
jfieldID AndroidGeckoEvent::jTypeField = 0;
jfieldID AndroidGeckoEvent::jTimeField = 0;
jfieldID AndroidGeckoEvent::jP0Field = 0;
jfieldID AndroidGeckoEvent::jP1Field = 0;
jfieldID AndroidGeckoEvent::jAlphaField = 0;
jfieldID AndroidGeckoEvent::jBetaField = 0;
jfieldID AndroidGeckoEvent::jGammaField = 0;
jfieldID AndroidGeckoEvent::jXField = 0;
jfieldID AndroidGeckoEvent::jYField = 0;
jfieldID AndroidGeckoEvent::jZField = 0;
jfieldID AndroidGeckoEvent::jRectField = 0;
jfieldID AndroidGeckoEvent::jNativeWindowField = 0;

jfieldID AndroidGeckoEvent::jCharactersField = 0;
jfieldID AndroidGeckoEvent::jKeyCodeField = 0;
jfieldID AndroidGeckoEvent::jMetaStateField = 0;
jfieldID AndroidGeckoEvent::jFlagsField = 0;
jfieldID AndroidGeckoEvent::jUnicodeCharField = 0;
jfieldID AndroidGeckoEvent::jOffsetField = 0;
jfieldID AndroidGeckoEvent::jCountField = 0;
jfieldID AndroidGeckoEvent::jRangeTypeField = 0;
jfieldID AndroidGeckoEvent::jRangeStylesField = 0;
jfieldID AndroidGeckoEvent::jRangeForeColorField = 0;
jfieldID AndroidGeckoEvent::jRangeBackColorField = 0;
jfieldID AndroidGeckoEvent::jLocationField = 0;
jfieldID AndroidGeckoEvent::jAddressField = 0;

jclass AndroidPoint::jPointClass = 0;
jfieldID AndroidPoint::jXField = 0;
jfieldID AndroidPoint::jYField = 0;

jclass AndroidRect::jRectClass = 0;
jfieldID AndroidRect::jBottomField = 0;
jfieldID AndroidRect::jLeftField = 0;
jfieldID AndroidRect::jRightField = 0;
jfieldID AndroidRect::jTopField = 0;

jclass AndroidLocation::jLocationClass = 0;
jmethodID AndroidLocation::jGetLatitudeMethod = 0;
jmethodID AndroidLocation::jGetLongitudeMethod = 0;
jmethodID AndroidLocation::jGetAltitudeMethod = 0;
jmethodID AndroidLocation::jGetAccuracyMethod = 0;
jmethodID AndroidLocation::jGetBearingMethod = 0;
jmethodID AndroidLocation::jGetSpeedMethod = 0;
jmethodID AndroidLocation::jGetTimeMethod = 0;

jclass AndroidAddress::jAddressClass = 0;
jmethodID AndroidAddress::jGetAddressLineMethod;
jmethodID AndroidAddress::jGetAdminAreaMethod;
jmethodID AndroidAddress::jGetCountryCodeMethod;
jmethodID AndroidAddress::jGetCountryNameMethod;
jmethodID AndroidAddress::jGetFeatureNameMethod;
jmethodID AndroidAddress::jGetLocalityMethod;
jmethodID AndroidAddress::jGetPostalCodeMethod;
jmethodID AndroidAddress::jGetPremisesMethod;
jmethodID AndroidAddress::jGetSubAdminAreaMethod;
jmethodID AndroidAddress::jGetSubLocalityMethod;
jmethodID AndroidAddress::jGetSubThoroughfareMethod;
jmethodID AndroidAddress::jGetThoroughfareMethod;

jclass AndroidGeckoSurfaceView::jGeckoSurfaceViewClass = 0;
jmethodID AndroidGeckoSurfaceView::jBeginDrawingMethod = 0;
jmethodID AndroidGeckoSurfaceView::jEndDrawingMethod = 0;
jmethodID AndroidGeckoSurfaceView::jDraw2DBitmapMethod = 0;
jmethodID AndroidGeckoSurfaceView::jDraw2DBufferMethod = 0;
jmethodID AndroidGeckoSurfaceView::jGetSoftwareDrawBitmapMethod = 0;
jmethodID AndroidGeckoSurfaceView::jGetSoftwareDrawBufferMethod = 0;
jmethodID AndroidGeckoSurfaceView::jGetHolderMethod = 0;

#define JNI()  (AndroidBridge::JNI())

#define initInit() jclass jClass

// note that this also sets jClass
#define getClassGlobalRef(cname) \
    (jClass = jclass(jEnv->NewGlobalRef(jEnv->FindClass(cname))))

#define getField(fname, ftype) \
    ((jfieldID) jEnv->GetFieldID(jClass, fname, ftype))

#define getMethod(fname, ftype) \
    ((jmethodID) jEnv->GetMethodID(jClass, fname, ftype))

void
mozilla::InitAndroidJavaWrappers(JNIEnv *jEnv)
{
    AndroidGeckoEvent::InitGeckoEventClass(jEnv);
    AndroidGeckoSurfaceView::InitGeckoSurfaceViewClass(jEnv);
    AndroidPoint::InitPointClass(jEnv);
    AndroidLocation::InitLocationClass(jEnv);
    AndroidAddress::InitAddressClass(jEnv);
}

void
AndroidGeckoEvent::InitGeckoEventClass(JNIEnv *jEnv)
{
    initInit();

    jGeckoEventClass = getClassGlobalRef("org/mozilla/gecko/GeckoEvent");

    jActionField = getField("mAction", "I");
    jTypeField = getField("mType", "I");
    jTimeField = getField("mTime", "J");
    jP0Field = getField("mP0", "Landroid/graphics/Point;");
    jP1Field = getField("mP1", "Landroid/graphics/Point;");
    jAlphaField = getField("mAlpha", "D");
    jBetaField = getField("mBeta", "D");
    jGammaField = getField("mGamma", "D");
    jXField = getField("mX", "D");
    jYField = getField("mY", "D");
    jZField = getField("mZ", "D");
    jRectField = getField("mRect", "Landroid/graphics/Rect;");

    jCharactersField = getField("mCharacters", "Ljava/lang/String;");
    jKeyCodeField = getField("mKeyCode", "I");
    jMetaStateField = getField("mMetaState", "I");
    jFlagsField = getField("mFlags", "I");
    jUnicodeCharField = getField("mUnicodeChar", "I");
    jOffsetField = getField("mOffset", "I");
    jCountField = getField("mCount", "I");
    jRangeTypeField = getField("mRangeType", "I");
    jRangeStylesField = getField("mRangeStyles", "I");
    jRangeForeColorField = getField("mRangeForeColor", "I");
    jRangeBackColorField = getField("mRangeBackColor", "I");
    jLocationField = getField("mLocation", "Landroid/location/Location;");
    jAddressField = getField("mAddress", "Landroid/location/Address;");
}

void
AndroidGeckoSurfaceView::InitGeckoSurfaceViewClass(JNIEnv *jEnv)
{
    initInit();

    jGeckoSurfaceViewClass = getClassGlobalRef("org/mozilla/gecko/GeckoSurfaceView");

    jBeginDrawingMethod = getMethod("beginDrawing", "()I");
    jGetSoftwareDrawBitmapMethod = getMethod("getSoftwareDrawBitmap", "()Landroid/graphics/Bitmap;");
    jGetSoftwareDrawBufferMethod = getMethod("getSoftwareDrawBuffer", "()Ljava/nio/ByteBuffer;");
    jEndDrawingMethod = getMethod("endDrawing", "()V");
    jDraw2DBitmapMethod = getMethod("draw2D", "(Landroid/graphics/Bitmap;II)V");
    jDraw2DBufferMethod = getMethod("draw2D", "(Ljava/nio/ByteBuffer;I)V");
    jGetHolderMethod = getMethod("getHolder", "()Landroid/view/SurfaceHolder;");
}

void
AndroidLocation::InitLocationClass(JNIEnv *jEnv)
{
    initInit();

    jLocationClass = getClassGlobalRef("android/location/Location");
    jGetLatitudeMethod = getMethod("getLatitude", "()D");
    jGetLongitudeMethod = getMethod("getLongitude", "()D");
    jGetAltitudeMethod = getMethod("getAltitude", "()D");
    jGetAccuracyMethod = getMethod("getAccuracy", "()F");
    jGetBearingMethod = getMethod("getBearing", "()F");
    jGetSpeedMethod = getMethod("getSpeed", "()F");
    jGetTimeMethod = getMethod("getTime", "()J");
}

void
AndroidAddress::InitAddressClass(JNIEnv *jEnv)
{
    initInit();

    jAddressClass = getClassGlobalRef("android/location/Address");

    jGetAddressLineMethod = getMethod("getAddressLine", "(I)Ljava/lang/String;");
    jGetAdminAreaMethod = getMethod("getAdminArea", "()Ljava/lang/String;");
    jGetCountryCodeMethod = getMethod("getCountryCode", "()Ljava/lang/String;");
    jGetCountryNameMethod = getMethod("getCountryName", "()Ljava/lang/String;");
    jGetFeatureNameMethod = getMethod("getFeatureName", "()Ljava/lang/String;");
    jGetLocalityMethod  = getMethod("getLocality", "()Ljava/lang/String;");
    jGetPostalCodeMethod = getMethod("getPostalCode", "()Ljava/lang/String;");
    jGetPremisesMethod = getMethod("getPremises", "()Ljava/lang/String;");
    jGetSubAdminAreaMethod = getMethod("getSubAdminArea", "()Ljava/lang/String;");
    jGetSubLocalityMethod = getMethod("getSubLocality", "()Ljava/lang/String;");
    jGetSubThoroughfareMethod = getMethod("getSubThoroughfare", "()Ljava/lang/String;");
    jGetThoroughfareMethod = getMethod("getThoroughfare", "()Ljava/lang/String;");
}

nsGeoPositionAddress*
AndroidAddress::CreateGeoPositionAddress(JNIEnv *jenv, jobject jobj)
{
    nsJNIString streetNumber(static_cast<jstring>(jenv->CallObjectMethod(jobj, jGetSubThoroughfareMethod)), jenv);
    nsJNIString street(static_cast<jstring>(jenv->CallObjectMethod(jobj, jGetThoroughfareMethod)), jenv);
    nsJNIString city(static_cast<jstring>(jenv->CallObjectMethod(jobj, jGetLocalityMethod)), jenv);
    nsJNIString county(static_cast<jstring>(jenv->CallObjectMethod(jobj, jGetSubAdminAreaMethod)), jenv);
    nsJNIString country(static_cast<jstring>(jenv->CallObjectMethod(jobj, jGetCountryNameMethod)), jenv);
    nsJNIString countryCode(static_cast<jstring>(jenv->CallObjectMethod(jobj, jGetCountryCodeMethod)), jenv);
    nsJNIString premises(static_cast<jstring>(jenv->CallObjectMethod(jobj, jGetPremisesMethod)), jenv);
    nsJNIString postalCode(static_cast<jstring>(jenv->CallObjectMethod(jobj, jGetPostalCodeMethod)), jenv);
    nsJNIString region(static_cast<jstring>(jenv->CallObjectMethod(jobj, jGetAdminAreaMethod, 0)), jenv);

#ifdef DEBUG
    printf_stderr("!!!!!!!!!!!!!! AndroidAddress::CreateGeoPositionAddress:\n\t%s\n\t%s\n\t%s\n\t%s\n\t%s\n\t%s\n\t%s\n\t%s\n\t%s\n",
                  NS_LossyConvertUTF16toASCII(streetNumber).get(),
                  NS_LossyConvertUTF16toASCII(street).get(),
                  NS_LossyConvertUTF16toASCII(premises).get(),
                  NS_LossyConvertUTF16toASCII(city).get(),
                  NS_LossyConvertUTF16toASCII(county).get(),
                  NS_LossyConvertUTF16toASCII(region).get(),
                  NS_LossyConvertUTF16toASCII(country).get(),
                  NS_LossyConvertUTF16toASCII(countryCode).get(),
                  NS_LossyConvertUTF16toASCII(postalCode).get());
#endif

    return new nsGeoPositionAddress(streetNumber,
                                    street,
                                    premises,
                                    city,
                                    county,
                                    region,
                                    country,
                                    countryCode,
                                    postalCode);
}

nsGeoPosition*
AndroidLocation::CreateGeoPosition(JNIEnv *jenv, jobject jobj)
{
    double latitude  = jenv->CallDoubleMethod(jobj, jGetLatitudeMethod);
    double longitude = jenv->CallDoubleMethod(jobj, jGetLongitudeMethod);
    double altitude  = jenv->CallDoubleMethod(jobj, jGetAltitudeMethod);
    float  accuracy  = jenv->CallFloatMethod (jobj, jGetAccuracyMethod);
    float  bearing   = jenv->CallFloatMethod (jobj, jGetBearingMethod);
    float  speed     = jenv->CallFloatMethod (jobj, jGetSpeedMethod);
    long long time   = jenv->CallLongMethod  (jobj, jGetTimeMethod);

    return new nsGeoPosition(latitude, longitude,
                             altitude, accuracy,
                             accuracy, bearing,
                             speed,    time);
}

void
AndroidPoint::InitPointClass(JNIEnv *jEnv)
{
    initInit();

    jPointClass = getClassGlobalRef("android/graphics/Point");

    jXField = getField("x", "I");
    jYField = getField("y", "I");
}

void
AndroidRect::InitRectClass(JNIEnv *jEnv)
{
    initInit();

    jRectClass = getClassGlobalRef("android/graphics/Rect");

    jBottomField = getField("bottom", "I");
    jLeftField = getField("left", "I");
    jTopField = getField("top", "I");
    jRightField = getField("right", "I");
}

#undef initInit
#undef initClassGlobalRef
#undef getField
#undef getMethod

void
AndroidGeckoEvent::ReadP0Field(JNIEnv *jenv)
{
    AndroidPoint p0(jenv, jenv->GetObjectField(wrappedObject(), jP0Field));
    mP0.x = p0.X();
    mP0.y = p0.Y();
}

void
AndroidGeckoEvent::ReadP1Field(JNIEnv *jenv)
{
    AndroidPoint p1(jenv, jenv->GetObjectField(wrappedObject(), jP1Field));
    mP1.x = p1.X();
    mP1.y = p1.Y();
}

void
AndroidGeckoEvent::ReadRectField(JNIEnv *jenv)
{
    AndroidRect r(jenv, jenv->GetObjectField(wrappedObject(), jRectField));
    if (!r.isNull()) {
        mRect.SetRect(r.Left(),
                      r.Top(),
                      r.Right() - r.Left(),
                      r.Bottom() - r.Top());
    } else {
        mRect.SetEmpty();
    }
}

void
AndroidGeckoEvent::ReadCharactersField(JNIEnv *jenv)
{
    jstring s = (jstring) jenv->GetObjectField(wrapped_obj, jCharactersField);
    if (!s) {
        mCharacters.SetIsVoid(PR_TRUE);
        return;
    }

    int len = jenv->GetStringLength(s);
    mCharacters.SetLength(len);
    jenv->GetStringRegion(s, 0, len, mCharacters.BeginWriting());
}

void
AndroidGeckoEvent::Init(JNIEnv *jenv, jobject jobj)
{
    NS_ASSERTION(!wrapped_obj, "Init called on non-null wrapped_obj!");

    wrapped_obj = jobj;

    if (!jobj)
        return;

    mAction = jenv->GetIntField(jobj, jActionField);
    mType = jenv->GetIntField(jobj, jTypeField);

    switch (mType) {
        case SIZE_CHANGED:
            ReadP0Field(jenv);
            ReadP1Field(jenv);
            break;

        case KEY_EVENT:
            mTime = jenv->GetLongField(jobj, jTimeField);
            mMetaState = jenv->GetIntField(jobj, jMetaStateField);
            mFlags = jenv->GetIntField(jobj, jFlagsField);
            mKeyCode = jenv->GetIntField(jobj, jKeyCodeField);
            mUnicodeChar = jenv->GetIntField(jobj, jUnicodeCharField);
            ReadCharactersField(jenv);
            break;

        case MOTION_EVENT:
            mTime = jenv->GetLongField(jobj, jTimeField);
            mMetaState = jenv->GetIntField(jobj, jMetaStateField);
            mCount = jenv->GetIntField(jobj, jCountField);
            ReadP0Field(jenv);
            if (mCount > 1)
                ReadP1Field(jenv);
            break;

        case IME_EVENT:
            if (mAction == IME_GET_TEXT || mAction == IME_SET_SELECTION) {
                mOffset = jenv->GetIntField(jobj, jOffsetField);
                mCount = jenv->GetIntField(jobj, jCountField);
            } else if (mAction == IME_SET_TEXT || mAction == IME_ADD_RANGE) {
                if (mAction == IME_SET_TEXT)
                    ReadCharactersField(jenv);
                mOffset = jenv->GetIntField(jobj, jOffsetField);
                mCount = jenv->GetIntField(jobj, jCountField);
                mRangeType = jenv->GetIntField(jobj, jRangeTypeField);
                mRangeStyles = jenv->GetIntField(jobj, jRangeStylesField);
                mRangeForeColor =
                    jenv->GetIntField(jobj, jRangeForeColorField);
                mRangeBackColor =
                    jenv->GetIntField(jobj, jRangeBackColorField);
            }
            break;

        case DRAW:
            ReadRectField(jenv);
            break;

        case ORIENTATION_EVENT:
            mAlpha = jenv->GetDoubleField(jobj, jAlphaField);
            mBeta = jenv->GetDoubleField(jobj, jBetaField);
            mGamma = jenv->GetDoubleField(jobj, jGammaField);
            break;

       case ACCELERATION_EVENT:
            mX = jenv->GetDoubleField(jobj, jXField);
            mY = jenv->GetDoubleField(jobj, jYField);
            mZ = jenv->GetDoubleField(jobj, jZField);
            break;

        case LOCATION_EVENT: {
            jobject location = jenv->GetObjectField(jobj, jLocationField);
            jobject address  = jenv->GetObjectField(jobj, jAddressField);

            mGeoPosition = AndroidLocation::CreateGeoPosition(jenv, location);
            if (address)
                mGeoAddress = AndroidAddress::CreateGeoPositionAddress(jenv, address);
            break;
        }

        case LOAD_URI: {
            ReadCharactersField(jenv);
            break;
        }

        default:
            break;
    }

#ifndef ANDROID_DEBUG_EVENTS
    ALOG("AndroidGeckoEvent: %p : %d", (void*)jobj, mType);
#endif
}

void
AndroidGeckoEvent::Init(int aType)
{
    mType = aType;
}

void
AndroidGeckoEvent::Init(int x1, int y1, int x2, int y2)
{
    mType = DRAW;
    mRect.SetEmpty();
}

void
AndroidGeckoSurfaceView::Init(jobject jobj)
{
    NS_ASSERTION(wrapped_obj == nsnull, "Init called on non-null wrapped_obj!");

    wrapped_obj = jobj;
}

int
AndroidGeckoSurfaceView::BeginDrawing()
{
    NS_ASSERTION(!isNull(), "BeginDrawing called on null surfaceview!");

    return JNI()->CallIntMethod(wrapped_obj, jBeginDrawingMethod);
}

void
AndroidGeckoSurfaceView::EndDrawing()
{
    JNI()->CallVoidMethod(wrapped_obj, jEndDrawingMethod);
}

void
AndroidGeckoSurfaceView::Draw2D(jobject bitmap, int width, int height)
{
    JNI()->CallVoidMethod(wrapped_obj, jDraw2DBitmapMethod, bitmap, width, height);
}

void
AndroidGeckoSurfaceView::Draw2D(jobject buffer, int stride)
{
    JNI()->CallVoidMethod(wrapped_obj, jDraw2DBufferMethod, buffer, stride);
}

jobject
AndroidGeckoSurfaceView::GetSoftwareDrawBitmap()
{
    return JNI()->CallObjectMethod(wrapped_obj, jGetSoftwareDrawBitmapMethod);
}

jobject
AndroidGeckoSurfaceView::GetSoftwareDrawBuffer()
{
    return JNI()->CallObjectMethod(wrapped_obj, jGetSoftwareDrawBufferMethod);
}

jobject
AndroidGeckoSurfaceView::GetSurfaceHolder()
{
    return JNI()->CallObjectMethod(wrapped_obj, jGetHolderMethod);
}

void
AndroidPoint::Init(JNIEnv *jenv, jobject jobj)
{
    NS_ASSERTION(wrapped_obj == nsnull, "Init called on non-null wrapped_obj!");

    wrapped_obj = jobj;

    if (jobj) {
        mX = jenv->GetIntField(jobj, jXField);
        mY = jenv->GetIntField(jobj, jYField);
    } else {
        mX = 0;
        mY = 0;
    }
}

void
AndroidRect::Init(JNIEnv *jenv, jobject jobj)
{
    NS_ASSERTION(wrapped_obj == nsnull, "Init called on non-null wrapped_obj!");

    wrapped_obj = jobj;

    if (jobj) {
        mTop = jenv->GetIntField(jobj, jTopField);
        mLeft = jenv->GetIntField(jobj, jLeftField);
        mRight = jenv->GetIntField(jobj, jRightField);
        mBottom = jenv->GetIntField(jobj, jBottomField);
    } else {
        mTop = 0;
        mLeft = 0;
        mRight = 0;
        mBottom = 0;
    }
}

nsJNIString::nsJNIString(jstring jstr, JNIEnv *jenv)
{
    if (!jstr) {
        SetIsVoid(PR_TRUE);
        return;
    }
    JNIEnv *jni = jenv;
    if (!jni)
        jni = JNI();
    const jchar* jCharPtr = jni->GetStringChars(jstr, NULL);

    if (!jCharPtr) {
        SetIsVoid(PR_TRUE);
        return;
    }

    jsize len = jni->GetStringLength(jstr);

    if (len <= 0) {
        SetIsVoid(PR_TRUE);
    } else {
        Assign(jCharPtr, len);
    }
    jni->ReleaseStringChars(jstr, jCharPtr);
}
