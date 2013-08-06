/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Roland Mainz <roland.mainz@informatik.med.uni-giessen.de>
 *   Florian Hänel <heeen@gmx.de>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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


#ifndef nsDeviceContextSpecQt_h___
#define nsDeviceContextSpecQt_h___

#include "nsIDeviceContextSpec.h"
#include "nsIPrintSettings.h"
#include "nsIPrintOptions.h"
#include "nsCOMPtr.h"
#include "nsString.h"

#include "nsCRT.h" /* should be <limits.h>? */

class nsDeviceContextSpecQt : public nsIDeviceContextSpec
{
public:
    nsDeviceContextSpecQt();
    virtual ~nsDeviceContextSpecQt();

    NS_DECL_ISUPPORTS

    NS_IMETHOD GetSurfaceForPrinter(gfxASurface** surface);

    NS_IMETHOD Init(nsIWidget* aWidget,
                    nsIPrintSettings* aPS,
                    PRBool aIsPrintPreview);
    NS_IMETHOD BeginDocument(PRUnichar* aTitle,
                             PRUnichar* aPrintToFileName,
                             PRInt32 aStartPage,
                             PRInt32 aEndPage);
    NS_IMETHOD EndDocument();
    NS_IMETHOD BeginPage() { return NS_OK; }
    NS_IMETHOD EndPage() { return NS_OK; }

    NS_IMETHOD GetPath (const char** aPath);

protected:
    nsCOMPtr<nsIPrintSettings> mPrintSettings;
    PRPackedBool mToPrinter : 1;      /* If PR_TRUE, print to printer */
    PRPackedBool mIsPPreview : 1;     /* If PR_TRUE, is print preview */
    char   mPath[PATH_MAX];     /* If toPrinter = PR_FALSE, dest file */
    char   mPrinter[256];       /* Printer name */
    nsCString              mSpoolName;
    nsCOMPtr<nsILocalFile> mSpoolFile;
};

class nsPrinterEnumeratorQt : public nsIPrinterEnumerator
{
public:
    nsPrinterEnumeratorQt();
    NS_DECL_ISUPPORTS
    NS_DECL_NSIPRINTERENUMERATOR
};

#endif /* !nsDeviceContextSpecQt_h___ */
