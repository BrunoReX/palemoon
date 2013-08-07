/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
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
 * The Original Code is the Mozilla browser.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications, Inc.
 * Portions created by the Initial Developer are Copyright (C) 1999
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Adam Lock <adamlock@netscape.com>
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

#ifndef nsWebBrowserPersist_h__
#define nsWebBrowserPersist_h__

#include "nsCOMPtr.h"
#include "nsWeakReference.h"

#include "nsIInterfaceRequestor.h"
#include "nsIMIMEService.h"
#include "nsIStreamListener.h"
#include "nsIOutputStream.h"
#include "nsIInputStream.h"
#include "nsIChannel.h"
#include "nsIStyleSheet.h"
#include "nsIDocumentEncoder.h"
#include "nsITransport.h"
#include "nsIProgressEventSink.h"
#include "nsILocalFile.h"
#include "nsIWebProgressListener2.h"

#include "nsHashtable.h"
#include "nsTArray.h"

#include "nsCWebBrowserPersist.h"

class nsEncoderNodeFixup;
class nsIStorageStream;

struct URIData;
struct CleanupData;
struct DocData;

class nsWebBrowserPersist : public nsIInterfaceRequestor,
                            public nsIWebBrowserPersist,
                            public nsIStreamListener,
                            public nsIProgressEventSink,
                            public nsSupportsWeakReference
{
    friend class nsEncoderNodeFixup;

// Public members
public:
    nsWebBrowserPersist();
    
    NS_DECL_ISUPPORTS
    NS_DECL_NSIINTERFACEREQUESTOR
    NS_DECL_NSICANCELABLE
    NS_DECL_NSIWEBBROWSERPERSIST
    NS_DECL_NSIREQUESTOBSERVER
    NS_DECL_NSISTREAMLISTENER
    NS_DECL_NSIPROGRESSEVENTSINK

// Protected members
protected:    
    virtual ~nsWebBrowserPersist();
    nsresult CloneNodeWithFixedUpAttributes(
        nsIDOMNode *aNodeIn, bool *aSerializeCloneKids, nsIDOMNode **aNodeOut);
    nsresult SaveURIInternal(
        nsIURI *aURI, nsISupports *aCacheKey, nsIURI *aReferrer,
        nsIInputStream *aPostData, const char *aExtraHeaders, nsIURI *aFile,
        bool aCalcFileExt);
    nsresult SaveChannelInternal(
        nsIChannel *aChannel, nsIURI *aFile, bool aCalcFileExt);
    nsresult SaveDocumentInternal(
        nsIDOMDocument *aDocument, nsIURI *aFile, nsIURI *aDataPath);
    nsresult SaveDocuments();
    nsresult GetDocEncoderContentType(
        nsIDOMDocument *aDocument, const PRUnichar *aContentType,
        PRUnichar **aRealContentType);
    nsresult GetExtensionForContentType(
        const PRUnichar *aContentType, PRUnichar **aExt);
    nsresult GetDocumentExtension(nsIDOMDocument *aDocument, PRUnichar **aExt);

// Private members
private:
    void Cleanup();
    void CleanupLocalFiles();
    nsresult GetValidURIFromObject(nsISupports *aObject, nsIURI **aURI) const;
    nsresult GetLocalFileFromURI(nsIURI *aURI, nsILocalFile **aLocalFile) const;
    nsresult AppendPathToURI(nsIURI *aURI, const nsAString & aPath) const;
    nsresult MakeAndStoreLocalFilenameInURIMap(
        nsIURI *aURI, bool aNeedsPersisting, URIData **aData);
    nsresult MakeOutputStream(
        nsIURI *aFile, nsIOutputStream **aOutputStream);
    nsresult MakeOutputStreamFromFile(
        nsILocalFile *aFile, nsIOutputStream **aOutputStream);
    nsresult MakeOutputStreamFromURI(nsIURI *aURI, nsIOutputStream  **aOutStream);
    nsresult CreateChannelFromURI(nsIURI *aURI, nsIChannel **aChannel);
    nsresult StartUpload(nsIStorageStream *aOutStream, nsIURI *aDestinationURI,
        const nsACString &aContentType);
    nsresult StartUpload(nsIInputStream *aInputStream, nsIURI *aDestinationURI,
        const nsACString &aContentType);
    nsresult CalculateAndAppendFileExt(nsIURI *aURI, nsIChannel *aChannel,
        nsIURI *aOriginalURIWithExtension);
    nsresult CalculateUniqueFilename(nsIURI *aURI);
    nsresult MakeFilenameFromURI(
        nsIURI *aURI, nsString &aFilename);
    nsresult StoreURI(
        const char *aURI,
        bool aNeedsPersisting = true,
        URIData **aData = nsnull);
    nsresult StoreURI(
        nsIURI *aURI,
        bool aNeedsPersisting = true,
        URIData **aData = nsnull);
    nsresult StoreURIAttributeNS(
        nsIDOMNode *aNode, const char *aNamespaceURI, const char *aAttribute,
        bool aNeedsPersisting = true,
        URIData **aData = nsnull);
    nsresult StoreURIAttribute(
        nsIDOMNode *aNode, const char *aAttribute,
        bool aNeedsPersisting = true,
        URIData **aData = nsnull)
    {
        return StoreURIAttributeNS(aNode, "", aAttribute, aNeedsPersisting, aData);
    }
    bool GetQuotedAttributeValue(
    const nsAString &aSource, const nsAString &aAttribute, nsAString &aValue);
    bool DocumentEncoderExists(const PRUnichar *aContentType);

    nsresult GetNodeToFixup(nsIDOMNode *aNodeIn, nsIDOMNode **aNodeOut);
    nsresult FixupURI(nsAString &aURI);
    nsresult FixupNodeAttributeNS(nsIDOMNode *aNode, const char *aNamespaceURI, const char *aAttribute);
    nsresult FixupNodeAttribute(nsIDOMNode *aNode, const char *aAttribute)
    {
        return FixupNodeAttributeNS(aNode, "", aAttribute);
    }
    nsresult FixupAnchor(nsIDOMNode *aNode);
    nsresult FixupXMLStyleSheetLink(nsIDOMProcessingInstruction *aPI, const nsAString &aHref);
    nsresult GetXMLStyleSheetLink(nsIDOMProcessingInstruction *aPI, nsAString &aHref);

    nsresult StoreAndFixupStyleSheet(nsIStyleSheet *aStyleSheet);
    nsresult SaveDocumentWithFixup(
        nsIDOMDocument *pDocument, nsIDocumentEncoderNodeFixup *pFixup,
        nsIURI *aFile, bool aReplaceExisting, const nsACString &aFormatType,
        const nsCString &aSaveCharset, PRUint32  aFlags);
    nsresult SaveSubframeContent(
        nsIDOMDocument *aFrameContent, URIData *aData);
    nsresult SetDocumentBase(nsIDOMDocument *aDocument, nsIURI *aBaseURI);
    nsresult SendErrorStatusChange(
        bool aIsReadError, nsresult aResult, nsIRequest *aRequest, nsIURI *aURI);
    nsresult OnWalkDOMNode(nsIDOMNode *aNode);

    nsresult FixRedirectedChannelEntry(nsIChannel *aNewChannel);

    void EndDownload(nsresult aResult = NS_OK);
    nsresult SaveGatheredURIs(nsIURI *aFileAsURI);
    bool SerializeNextFile();
    void CalcTotalProgress();

    void SetApplyConversionIfNeeded(nsIChannel *aChannel);

    // Hash table enumerators
    static bool EnumPersistURIs(
        nsHashKey *aKey, void *aData, void* closure);
    static bool EnumCleanupURIMap(
        nsHashKey *aKey, void *aData, void* closure);
    static bool EnumCleanupOutputMap(
        nsHashKey *aKey, void *aData, void* closure);
    static bool EnumCleanupUploadList(
        nsHashKey *aKey, void *aData, void* closure);
    static bool EnumCalcProgress(
        nsHashKey *aKey, void *aData, void* closure);
    static bool EnumCalcUploadProgress(
        nsHashKey *aKey, void *aData, void* closure);
    static bool EnumFixRedirect(
        nsHashKey *aKey, void *aData, void* closure);
    static bool EnumCountURIsToPersist(
        nsHashKey *aKey, void *aData, void* closure);

    nsCOMPtr<nsIURI>          mCurrentDataPath;
    bool                      mCurrentDataPathIsRelative;
    nsCString                 mCurrentRelativePathToData;
    nsCOMPtr<nsIURI>          mCurrentBaseURI;
    nsCString                 mCurrentCharset;
    nsCOMPtr<nsIURI>          mTargetBaseURI;
    PRUint32                  mCurrentThingsToPersist;

    nsCOMPtr<nsIMIMEService>  mMIMEService;
    nsCOMPtr<nsIURI>          mURI;
    nsCOMPtr<nsIWebProgressListener> mProgressListener;
    /**
     * Progress listener for 64-bit values; this is the same object as
     * mProgressListener, but is a member to avoid having to qi it for each
     * progress notification.
     */
    nsCOMPtr<nsIWebProgressListener2> mProgressListener2;
    nsCOMPtr<nsIProgressEventSink> mEventSink;
    nsHashtable               mOutputMap;
    nsHashtable               mUploadList;
    nsHashtable               mURIMap;
    nsTArray<DocData*>        mDocList;
    nsTArray<CleanupData*>    mCleanupList;
    nsTArray<nsCString>       mFilenameList;
    bool                      mFirstAndOnlyUse;
    bool                      mCancel;
    bool                      mJustStartedLoading;
    bool                      mCompleted;
    bool                      mStartSaving;
    bool                      mReplaceExisting;
    bool                      mSerializingOutput;
    PRUint32                  mPersistFlags;
    PRUint32                  mPersistResult;
    PRInt64                   mTotalCurrentProgress;
    PRInt64                   mTotalMaxProgress;
    PRInt16                   mWrapColumn;
    PRUint32                  mEncodingFlags;
    nsString                  mContentType;
};

// Helper class does node fixup during persistence
class nsEncoderNodeFixup : public nsIDocumentEncoderNodeFixup
{
public:
    nsEncoderNodeFixup();
    
    NS_DECL_ISUPPORTS
    NS_IMETHOD FixupNode(nsIDOMNode *aNode, bool *aSerializeCloneKids, nsIDOMNode **aOutNode);
    
    nsWebBrowserPersist *mWebBrowserPersist;

protected:    
    virtual ~nsEncoderNodeFixup();
};

#endif
