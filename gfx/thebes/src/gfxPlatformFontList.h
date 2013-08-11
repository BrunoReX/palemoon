/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
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
 * The Original Code is Mozilla Corporation code.
 *
 * The Initial Developer of the Original Code is Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2008-2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Jonathan Kew <jfkthame@gmail.com>
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

#ifndef GFXPLATFORMFONTLIST_H_
#define GFXPLATFORMFONTLIST_H_

#include "nsDataHashtable.h"
#include "nsRefPtrHashtable.h"

#include "gfxFontUtils.h"
#include "gfxFont.h"
#include "gfxPlatform.h"

// gfxPlatformFontList is an abstract class for the global font list on the system;
// concrete subclasses for each platform implement the actual interface to the system fonts.
// This class exists because we cannot rely on the platform font-finding APIs to behave
// in sensible/similar ways, particularly with rich, complex OpenType families,
// so we do our own font family/style management here instead.

// Much of this is based on the old gfxQuartzFontCache, but adapted for use on all platforms.

class gfxPlatformFontList : protected gfxFontInfoLoader
{
public:
    static gfxPlatformFontList* PlatformFontList() {
        return sPlatformFontList;
    }

    static nsresult Init() {
        NS_ASSERTION(!sPlatformFontList, "What's this doing here?");
        sPlatformFontList = gfxPlatform::GetPlatform()->CreatePlatformFontList();
        if (!sPlatformFontList) return NS_ERROR_OUT_OF_MEMORY;
        sPlatformFontList->InitFontList();
        return NS_OK;
    }

    static void Shutdown() {
        delete sPlatformFontList;
        sPlatformFontList = nsnull;
    }

    void GetFontList (const nsACString& aLangGroup,
                      const nsACString& aGenericFamily,
                      nsTArray<nsString>& aListOfFonts);

    virtual PRBool ResolveFontName(const nsAString& aFontName,
                                   nsAString& aResolvedFontName);

    void UpdateFontList() { InitFontList(); }

    void ClearPrefFonts() { mPrefFonts.Clear(); }

    void GetFontFamilyList(nsTArray<nsRefPtr<gfxFontFamily> >& aFamilyArray);

    gfxFontEntry* FindFontForChar(const PRUint32 aCh, gfxFont *aPrevFont);

    gfxFontFamily* FindFamily(const nsAString& aFamily);

    gfxFontEntry* FindFontForFamily(const nsAString& aFamily, const gfxFontStyle* aStyle, PRBool& aNeedsBold);

    PRBool GetPrefFontFamilyEntries(eFontPrefLang aLangGroup, nsTArray<nsRefPtr<gfxFontFamily> > *array);
    void SetPrefFontFamilyEntries(eFontPrefLang aLangGroup, nsTArray<nsRefPtr<gfxFontFamily> >& array);

    void AddOtherFamilyName(gfxFontFamily *aFamilyEntry, nsAString& aOtherFamilyName);

    // pure virtual functions, to be provided by concrete subclasses

    // get the system default font
    virtual gfxFontEntry* GetDefaultFont(const gfxFontStyle* aStyle,
                                         PRBool& aNeedsBold) = 0;

    // look up a font by name on the host platform
    virtual gfxFontEntry* LookupLocalFont(const gfxProxyFontEntry *aProxyEntry,
                                          const nsAString& aFontName) = 0;

    // create a new platform font from downloaded data (@font-face)
    // this method is responsible to ensure aFontData is NS_Free()'d
    virtual gfxFontEntry* MakePlatformFont(const gfxFontEntry *aProxyEntry,
                                           const PRUint8 *aFontData,
                                           PRUint32 aLength) = 0;

    // get the standard family name on the platform for a given font name
    virtual PRBool GetStandardFamilyName(const nsAString& aFontName, nsAString& aFamilyName) = 0;

protected:
    gfxPlatformFontList();

    static gfxPlatformFontList *sPlatformFontList;

    static PLDHashOperator FindFontForCharProc(nsStringHashKey::KeyType aKey,
                                               nsRefPtr<gfxFontFamily>& aFamilyEntry,
                                               void* userArg);

    // initialize font lists [pure virtual]
    virtual void InitFontList() = 0;

    // read secondary family names
    void ReadOtherFamilyNamesForFamily(const nsAString& aFamilyName);

    // separate initialization for reading in name tables, since this is expensive
    void InitOtherFamilyNames();

    // commonly used fonts for which the name table should be loaded at startup
    virtual void PreloadNamesList();

    // initialize the bad underline blacklist from pref.
    virtual void InitBadUnderlineList();

    // explicitly set fixed-pitch flag for all faces
    void SetFixedPitch(const nsAString& aFamilyName);

    static PLDHashOperator InitOtherFamilyNamesProc(nsStringHashKey::KeyType aKey,
                                                    nsRefPtr<gfxFontFamily>& aFamilyEntry,
                                                    void* userArg);

    void GenerateFontListKey(const nsAString& aKeyName, nsAString& aResult);

    static PLDHashOperator
        HashEnumFuncForFamilies(nsStringHashKey::KeyType aKey,
                                nsRefPtr<gfxFontFamily>& aFamilyEntry,
                                void* aUserArg);

    // gfxFontInfoLoader overrides, used to load in font cmaps
    virtual void InitLoader();
    virtual PRBool RunLoader();
    virtual void FinishLoader();

    // canonical family name ==> family entry (unique, one name per family entry)
    nsRefPtrHashtable<nsStringHashKey, gfxFontFamily> mFontFamilies;    

    // other family name ==> family entry (not unique, can have multiple names per 
    // family entry, only names *other* than the canonical names are stored here)
    nsRefPtrHashtable<nsStringHashKey, gfxFontFamily> mOtherFamilyNames;    

    // cached pref font lists
    // maps list of family names ==> array of family entries, one per lang group
    nsDataHashtable<nsUint32HashKey, nsTArray<nsRefPtr<gfxFontFamily> > > mPrefFonts;

    // when system-wide font lookup fails for a character, cache it to skip future searches
    gfxSparseBitSet mCodepointsWithNoFonts;

    // the family to use for U+FFFD fallback, to avoid expensive search every time
    // on pages with lots of problems
    nsString mReplacementCharFallbackFamily;

    // flag set after InitOtherFamilyNames is called upon first name lookup miss
    PRPackedBool mOtherFamilyNamesInitialized;

    // data used as part of the font cmap loading process
    nsTArray<nsRefPtr<gfxFontFamily> > mFontFamiliesToLoad;
    PRUint32 mStartIndex;
    PRUint32 mIncrement;
    PRUint32 mNumFamilies;
};


// helper class for adding other family names back into font cache
class AddOtherFamilyNameFunctor 
{
public:
    AddOtherFamilyNameFunctor(gfxPlatformFontList *aFontList) :
        mFontList(aFontList)
    {}

    void operator() (gfxFontFamily *aFamilyEntry, nsAString& aOtherName) {
        mFontList->AddOtherFamilyName(aFamilyEntry, aOtherName);
    }

    gfxPlatformFontList *mFontList;
};


#endif /* GFXPLATFORMFONTLIST_H_ */
