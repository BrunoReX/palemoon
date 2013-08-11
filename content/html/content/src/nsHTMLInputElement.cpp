/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Pierre Phaneuf <pp@ludusdesign.com>
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
#include "nsCOMPtr.h"
#include "nsIDOMHTMLInputElement.h"
#include "nsIDOMNSHTMLInputElement.h"
#include "nsITextControlElement.h"
#include "nsIFileControlElement.h"
#include "nsIDOMNSEditableElement.h"
#include "nsIRadioControlElement.h"
#include "nsIRadioVisitor.h"
#include "nsIPhonetic.h"

#include "nsIControllers.h"
#include "nsFocusManager.h"
#include "nsPIDOMWindow.h"
#include "nsContentCID.h"
#include "nsIComponentManager.h"
#include "nsIDOMHTMLFormElement.h"
#include "nsIDOMEventTarget.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsStyleConsts.h"
#include "nsPresContext.h"
#include "nsMappedAttributes.h"
#include "nsIFormControl.h"
#include "nsIForm.h"
#include "nsIFormSubmission.h"
#include "nsITextControlFrame.h"
#include "nsIRadioControlFrame.h"
#include "nsIDocument.h"
#include "nsIPresShell.h"
#include "nsIFormControlFrame.h"
#include "nsITextControlFrame.h"
#include "nsIFrame.h"
#include "nsIEventStateManager.h"
#include "nsIServiceManager.h"
#include "nsIScriptSecurityManager.h"
#include "nsDOMError.h"
#include "nsIPrivateDOMEvent.h"
#include "nsIEditor.h"
#include "nsGUIEvent.h"

#include "nsPresState.h"
#include "nsLayoutErrors.h"
#include "nsIDOMEvent.h"
#include "nsIDOMNSEvent.h"
#include "nsIDOMNodeList.h"
#include "nsIDOMHTMLCollection.h"
#include "nsICheckboxControlFrame.h"
#include "nsLinebreakConverter.h" //to strip out carriage returns
#include "nsReadableUtils.h"
#include "nsUnicharUtils.h"
#include "nsEventDispatcher.h"
#include "nsLayoutUtils.h"
#include "nsWidgetsCID.h"
#include "nsILookAndFeel.h"

#include "nsIDOMMutationEvent.h"
#include "nsIDOMEventTarget.h"
#include "nsMutationEvent.h"
#include "nsIEventListenerManager.h"

#include "nsRuleData.h"

// input type=radio
#include "nsIRadioControlFrame.h"
#include "nsIRadioGroupContainer.h"

// input type=file
#include "nsIMIMEService.h"
#include "nsCExternalHandlerService.h"
#include "nsIFile.h"
#include "nsILocalFile.h"
#include "nsIFileStreams.h"
#include "nsNetUtil.h"
#include "nsDOMFile.h"

// input type=image
#include "nsImageLoadingContent.h"
#include "nsIDOMWindowInternal.h"

#include "mozAutoDocUpdate.h"

// XXX align=left, hspace, vspace, border? other nav4 attrs

static NS_DEFINE_CID(kXULControllersCID,  NS_XULCONTROLLERS_CID);
static NS_DEFINE_CID(kLookAndFeelCID, NS_LOOKANDFEEL_CID);

//
// Accessors for mBitField
//
#define BF_DISABLED_CHANGED 0
#define BF_HANDLING_CLICK 1
#define BF_VALUE_CHANGED 2
#define BF_CHECKED_CHANGED 3
#define BF_CHECKED 4
#define BF_HANDLING_SELECT_EVENT 5
#define BF_SHOULD_INIT_CHECKED 6
#define BF_PARSER_CREATING 7
#define BF_IN_INTERNAL_ACTIVATE 8
#define BF_CHECKED_IS_TOGGLED 9
#define BF_INDETERMINATE 10

#define GET_BOOLBIT(bitfield, field) (((bitfield) & (0x01 << (field))) \
                                        ? PR_TRUE : PR_FALSE)
#define SET_BOOLBIT(bitfield, field, b) ((b) \
                                        ? ((bitfield) |=  (0x01 << (field))) \
                                        : ((bitfield) &= ~(0x01 << (field))))

// First bits are needed for the control type.
#define NS_OUTER_ACTIVATE_EVENT   (1 << 9)
#define NS_ORIGINAL_CHECKED_VALUE (1 << 10)
#define NS_NO_CONTENT_DISPATCH    (1 << 11)
#define NS_ORIGINAL_INDETERMINATE_VALUE (1 << 12)
#define NS_CONTROL_TYPE(bits)  ((bits) & ~( \
  NS_OUTER_ACTIVATE_EVENT | NS_ORIGINAL_CHECKED_VALUE | NS_NO_CONTENT_DISPATCH | \
  NS_ORIGINAL_INDETERMINATE_VALUE))

static const char kWhitespace[] = "\n\r\t\b";

// whether textfields should be selected once focused:
//  -1: no, 1: yes, 0: uninitialized
static PRInt32 gSelectTextFieldOnFocus;

#define NS_INPUT_ELEMENT_STATE_IID                 \
{ /* dc3b3d14-23e2-4479-b513-7b369343e3a0 */       \
  0xdc3b3d14,                                      \
  0x23e2,                                          \
  0x4479,                                          \
  {0xb5, 0x13, 0x7b, 0x36, 0x93, 0x43, 0xe3, 0xa0} \
}

class nsHTMLInputElementState : public nsISupports
{
  public:
    NS_DECLARE_STATIC_IID_ACCESSOR(NS_INPUT_ELEMENT_STATE_IID)
    NS_DECL_ISUPPORTS

    PRBool IsCheckedSet() {
      return mCheckedSet;
    }

    PRBool GetChecked() {
      return mChecked;
    }

    void SetChecked(PRBool aChecked) {
      mChecked = aChecked;
      mCheckedSet = PR_TRUE;
    }

    const nsString& GetValue() {
      return mValue;
    }

    void SetValue(const nsAString &aValue) {
      mValue = aValue;
    }

    const nsTArray<nsString>& GetFilenames() {
      return mFilenames;
    }

    void SetFilenames(const nsTArray<nsString> &aFilenames) {
      mFilenames = aFilenames;
    }

    nsHTMLInputElementState()
      : mValue()
      , mChecked(PR_FALSE)
      , mCheckedSet(PR_FALSE)
    {};
 
  protected:
    nsString mValue;
    nsTArray<nsString> mFilenames;
    PRPackedBool mChecked;
    PRPackedBool mCheckedSet;
};

NS_IMPL_ISUPPORTS1(nsHTMLInputElementState, nsHTMLInputElementState)
NS_DEFINE_STATIC_IID_ACCESSOR(nsHTMLInputElementState, NS_INPUT_ELEMENT_STATE_IID)

class nsHTMLInputElement : public nsGenericHTMLFormElement,
                           public nsImageLoadingContent,
                           public nsIDOMHTMLInputElement,
                           public nsIDOMNSHTMLInputElement,
                           public nsITextControlElement,
                           public nsIRadioControlElement,
                           public nsIPhonetic,
                           public nsIDOMNSEditableElement,
                           public nsIFileControlElement
{
public:
  nsHTMLInputElement(nsINodeInfo *aNodeInfo, PRBool aFromParser);
  virtual ~nsHTMLInputElement();

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  // nsIDOMNode
  NS_FORWARD_NSIDOMNODE(nsGenericHTMLFormElement::)

  // nsIDOMElement
  NS_FORWARD_NSIDOMELEMENT(nsGenericHTMLFormElement::)

  // nsIDOMHTMLElement
  NS_FORWARD_NSIDOMHTMLELEMENT(nsGenericHTMLFormElement::)

  // nsIDOMHTMLInputElement
  NS_DECL_NSIDOMHTMLINPUTELEMENT

  // nsIDOMNSHTMLInputElement
  NS_DECL_NSIDOMNSHTMLINPUTELEMENT

  // nsIPhonetic
  NS_DECL_NSIPHONETIC

  // nsIDOMNSEditableElement
  NS_IMETHOD GetEditor(nsIEditor** aEditor)
  {
    return nsGenericHTMLElement::GetEditor(aEditor);
  }
  NS_IMETHOD SetUserInput(const nsAString& aInput);

  // Overriden nsIFormControl methods
  NS_IMETHOD_(PRInt32) GetType() const { return mType; }
  NS_IMETHOD Reset();
  NS_IMETHOD SubmitNamesValues(nsIFormSubmission* aFormSubmission,
                               nsIContent* aSubmitElement);
  NS_IMETHOD SaveState();
  virtual PRBool RestoreState(nsPresState* aState);
  virtual PRBool AllowDrop();

  // nsIContent
  virtual PRBool IsHTMLFocusable(PRBool *aIsFocusable, PRInt32 *aTabIndex);

  virtual PRBool ParseAttribute(PRInt32 aNamespaceID,
                                nsIAtom* aAttribute,
                                const nsAString& aValue,
                                nsAttrValue& aResult);
  virtual nsChangeHint GetAttributeChangeHint(const nsIAtom* aAttribute,
                                              PRInt32 aModType) const;
  NS_IMETHOD_(PRBool) IsAttributeMapped(const nsIAtom* aAttribute) const;
  virtual nsMapRuleToAttributesFunc GetAttributeMappingFunction() const;

  virtual nsresult PreHandleEvent(nsEventChainPreVisitor& aVisitor);
  virtual nsresult PostHandleEvent(nsEventChainPostVisitor& aVisitor);

  virtual nsresult BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                              nsIContent* aBindingParent,
                              PRBool aCompileEventHandlers);
  virtual void UnbindFromTree(PRBool aDeep = PR_TRUE,
                              PRBool aNullParent = PR_TRUE);

  virtual void DoneCreatingElement();

  virtual PRInt32 IntrinsicState() const;

  // nsITextControlElement
  NS_IMETHOD TakeTextFrameValue(const nsAString& aValue);
  NS_IMETHOD SetValueChanged(PRBool aValueChanged);
  
  // nsIFileControlElement
  virtual void GetDisplayFileName(nsAString& aFileName);
  virtual void GetFileArray(nsCOMArray<nsIFile> &aFile);
  virtual void SetFileNames(const nsTArray<nsString>& aFileNames);

  // nsIRadioControlElement
  NS_IMETHOD RadioSetChecked(PRBool aNotify);
  NS_IMETHOD SetCheckedChanged(PRBool aCheckedChanged);
  NS_IMETHOD SetCheckedChangedInternal(PRBool aCheckedChanged);
  NS_IMETHOD GetCheckedChanged(PRBool* aCheckedChanged);
  NS_IMETHOD AddedToRadioGroup(PRBool aNotify = PR_TRUE);
  NS_IMETHOD WillRemoveFromRadioGroup();
  /**
   * Get the radio group container for this button (form or document)
   * @return the radio group container (or null if no form or document)
   */
  virtual already_AddRefed<nsIRadioGroupContainer> GetRadioGroupContainer();

  virtual nsresult Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const;

  virtual void UpdateEditableState()
  {
    return UpdateEditableFormControlState();
  }

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED_NO_UNLINK(nsHTMLInputElement,
                                                     nsGenericHTMLFormElement)

  void MaybeLoadImage();
protected:
  // Helper method
  nsresult SetValueInternal(const nsAString& aValue,
                            nsITextControlFrame* aFrame,
                            PRBool aUserInput);

  void ClearFileNames() {
    nsTArray<nsString> fileNames;
    SetFileNames(fileNames);
  }

  void SetSingleFileName(const nsAString& aFileName) {
    nsAutoTArray<nsString, 1> fileNames;
    fileNames.AppendElement(aFileName);
    SetFileNames(fileNames);
  }

  nsresult SetIndeterminateInternal(PRBool aValue,
                                    PRBool aShouldInvalidate);

  nsresult GetSelectionRange(PRInt32* aSelectionStart, PRInt32* aSelectionEnd);

  /**
   * Get the name if it exists and return whether it did exist
   * @param aName the name returned [OUT]
   * @param true if the name existed, false if not
   */
  PRBool GetNameIfExists(nsAString& aName) {
    return GetAttr(kNameSpaceID_None, nsGkAtoms::name, aName);
  }

  /**
   * Called when an attribute is about to be changed
   */
  virtual nsresult BeforeSetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                                 const nsAString* aValue, PRBool aNotify);
  /**
   * Called when an attribute has just been changed
   */
  virtual nsresult AfterSetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                                const nsAString* aValue, PRBool aNotify);

  /**
   * Dispatch a select event. Returns true if the event was not cancelled.
   */
  PRBool DispatchSelectEvent(nsPresContext* aPresContext);

  void SelectAll(nsPresContext* aPresContext);
  PRBool IsImage() const
  {
    return AttrValueIs(kNameSpaceID_None, nsGkAtoms::type,
                       nsGkAtoms::image, eIgnoreCase);
  }

  /**
   * Fire the onChange event
   */
  void FireOnChange();

  /**
   * Visit a the group of radio buttons this radio belongs to
   * @param aVisitor the visitor to visit with
   */
  nsresult VisitGroup(nsIRadioVisitor* aVisitor, PRBool aFlushContent);

  /**
   * Do all the work that |SetChecked| does (radio button handling, etc.), but
   * take an |aNotify| parameter.
   */
  nsresult DoSetChecked(PRBool aValue, PRBool aNotify = PR_TRUE);

  /**
   * Do all the work that |SetCheckedChanged| does (radio button handling,
   * etc.), but take an |aNotify| parameter that lets it avoid flushing content
   * when it can.
   */
  nsresult DoSetCheckedChanged(PRBool aCheckedChanged, PRBool aNotify);

  /**
   * Actually set checked and notify the frame of the change.
   * @param aValue the value of checked to set
   */
  nsresult SetCheckedInternal(PRBool aValue, PRBool aNotify);

  /**
   * MaybeSubmitForm looks for a submit input or a single text control
   * and submits the form if either is present.
   */
  nsresult MaybeSubmitForm(nsPresContext* aPresContext);

  /**
   * Update mFileList with the currently selected file.
   */
  nsresult UpdateFileList();

  nsCOMPtr<nsIControllers> mControllers;

  /**
   * The type of this input (<input type=...>) as an integer.
   * @see nsIFormControl.h (specifically NS_FORM_INPUT_*)
   */
  PRInt8                   mType;
  /**
   * A bitfield containing our booleans
   * @see GET_BOOLBIT / SET_BOOLBIT macros and BF_* field identifiers
   */
  PRInt16                  mBitField;
  /**
   * The current value of the input if it has been changed from the default
   */
  char*                    mValue;
  /**
   * The value of the input if it is a file input. This is the list of filenames
   * used when uploading a file. It is vital that this is kept separate from
   * mValue so that it won't be possible to 'leak' the value from a text-input
   * to a file-input. Additionally, the logic for this value is kept as simple
   * as possible to avoid accidental errors where the wrong filename is used.
   * Therefor the list of filenames is always owned by this member, never by
   * the frame. Whenever the frame wants to change the filename it has to call
   * SetFileNames to update this member.
   */
  nsTArray<nsString>       mFileNames;

  nsRefPtr<nsDOMFileList>  mFileList;
};

#ifdef ACCESSIBILITY
//Helper method
static nsresult FireEventForAccessibility(nsIDOMHTMLInputElement* aTarget,
                                          nsPresContext* aPresContext,
                                          const nsAString& aEventType);
#endif

//
// construction, destruction
//

NS_IMPL_NS_NEW_HTML_ELEMENT_CHECK_PARSER(Input)

nsHTMLInputElement::nsHTMLInputElement(nsINodeInfo *aNodeInfo,
                                       PRBool aFromParser)
  : nsGenericHTMLFormElement(aNodeInfo),
    mType(NS_FORM_INPUT_TEXT), // default value
    mBitField(0),
    mValue(nsnull)
{
  SET_BOOLBIT(mBitField, BF_PARSER_CREATING, aFromParser);
}

nsHTMLInputElement::~nsHTMLInputElement()
{
  DestroyImageLoadingContent();
  if (mValue) {
    nsMemory::Free(mValue);
  }
}


// nsISupports

NS_IMPL_CYCLE_COLLECTION_CLASS(nsHTMLInputElement)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(nsHTMLInputElement,
                                                  nsGenericHTMLFormElement)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mControllers)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_ADDREF_INHERITED(nsHTMLInputElement, nsGenericElement) 
NS_IMPL_RELEASE_INHERITED(nsHTMLInputElement, nsGenericElement) 


// QueryInterface implementation for nsHTMLInputElement
NS_INTERFACE_TABLE_HEAD_CYCLE_COLLECTION_INHERITED(nsHTMLInputElement)
  NS_HTML_CONTENT_INTERFACE_TABLE10(nsHTMLInputElement,
                                    nsIDOMHTMLInputElement,
                                    nsIDOMNSHTMLInputElement,
                                    nsITextControlElement,
                                    nsIFileControlElement,
                                    nsIRadioControlElement,
                                    nsIPhonetic,
                                    imgIDecoderObserver,
                                    nsIImageLoadingContent,
                                    imgIContainerObserver,
                                    nsIDOMNSEditableElement)
  NS_HTML_CONTENT_INTERFACE_TABLE_TO_MAP_SEGUE(nsHTMLInputElement,
                                               nsGenericHTMLFormElement)
NS_HTML_CONTENT_INTERFACE_TABLE_TAIL_CLASSINFO(HTMLInputElement)


// nsIDOMNode

nsresult
nsHTMLInputElement::Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const
{
  *aResult = nsnull;

  nsHTMLInputElement *it = new nsHTMLInputElement(aNodeInfo, PR_FALSE);
  if (!it) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  nsCOMPtr<nsINode> kungFuDeathGrip = it;
  nsresult rv = CopyInnerTo(it);
  NS_ENSURE_SUCCESS(rv, rv);

  switch (mType) {
    case NS_FORM_INPUT_TEXT:
    case NS_FORM_INPUT_PASSWORD:
      if (GET_BOOLBIT(mBitField, BF_VALUE_CHANGED)) {
        // We don't have our default value anymore.  Set our value on
        // the clone.
        // XXX GetValue should be const
        nsAutoString value;
        const_cast<nsHTMLInputElement*>(this)->GetValue(value);
        // SetValueInternal handles setting the VALUE_CHANGED bit for us
        it->SetValueInternal(value, nsnull, PR_FALSE);
      }
      break;
    case NS_FORM_INPUT_FILE:
      it->mFileNames = mFileNames;
      break;
    case NS_FORM_INPUT_RADIO:
    case NS_FORM_INPUT_CHECKBOX:
      if (GET_BOOLBIT(mBitField, BF_CHECKED_CHANGED)) {
        // We no longer have our original checked state.  Set our
        // checked state on the clone.
        // XXX GetChecked should be const
        PRBool checked;
        const_cast<nsHTMLInputElement*>(this)->GetChecked(&checked);
        it->DoSetChecked(checked, PR_FALSE);
      }
      break;
    default:
      break;
  }

  kungFuDeathGrip.swap(*aResult);

  return NS_OK;
}

nsresult
nsHTMLInputElement::BeforeSetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                                  const nsAString* aValue,
                                  PRBool aNotify)
{
  if (aNameSpaceID == kNameSpaceID_None) {
    //
    // When name or type changes, radio should be removed from radio group.
    // (type changes are handled in the form itself currently)
    // If the parser is not done creating the radio, we also should not do it.
    //
    if ((aName == nsGkAtoms::name ||
         (aName == nsGkAtoms::type && !mForm)) &&
        mType == NS_FORM_INPUT_RADIO &&
        (mForm || !(GET_BOOLBIT(mBitField, BF_PARSER_CREATING)))) {
      WillRemoveFromRadioGroup();
    } else if (aNotify && aName == nsGkAtoms::src &&
               mType == NS_FORM_INPUT_IMAGE) {
      if (aValue) {
        LoadImage(*aValue, PR_TRUE, aNotify);
      } else {
        // Null value means the attr got unset; drop the image
        CancelImageRequests(aNotify);
      }
    } else if (aNotify && aName == nsGkAtoms::disabled) {
      SET_BOOLBIT(mBitField, BF_DISABLED_CHANGED, PR_TRUE);
    }
  }

  return nsGenericHTMLFormElement::BeforeSetAttr(aNameSpaceID, aName,
                                                 aValue, aNotify);
}

nsresult
nsHTMLInputElement::AfterSetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                                 const nsAString* aValue,
                                 PRBool aNotify)
{
  if (aNameSpaceID == kNameSpaceID_None) {
    //
    // When name or type changes, radio should be added to radio group.
    // (type changes are handled in the form itself currently)
    // If the parser is not done creating the radio, we also should not do it.
    //
    if ((aName == nsGkAtoms::name ||
         (aName == nsGkAtoms::type && !mForm)) &&
        mType == NS_FORM_INPUT_RADIO &&
        (mForm || !(GET_BOOLBIT(mBitField, BF_PARSER_CREATING)))) {
      AddedToRadioGroup();
    }

    //
    // Some elements have to change their value when the value and checked
    // attributes change (but they only do so when ValueChanged() and
    // CheckedChanged() are false--i.e. the value has not been changed by the
    // user or by JS)
    //
    // We only really need to call reset for the value so that the text control
    // knows the new value.  No other reason.
    //
    if (aName == nsGkAtoms::value &&
        !GET_BOOLBIT(mBitField, BF_VALUE_CHANGED) &&
        (mType == NS_FORM_INPUT_TEXT ||
         mType == NS_FORM_INPUT_PASSWORD ||
         mType == NS_FORM_INPUT_FILE)) {
      Reset();
    }
    //
    // Checked must be set no matter what type of control it is, since
    // GetChecked() must reflect the new value
    if (aName == nsGkAtoms::checked &&
        !GET_BOOLBIT(mBitField, BF_CHECKED_CHANGED)) {
      // Delay setting checked if the parser is creating this element (wait
      // until everything is set)
      if (GET_BOOLBIT(mBitField, BF_PARSER_CREATING)) {
        SET_BOOLBIT(mBitField, BF_SHOULD_INIT_CHECKED, PR_TRUE);
      } else {
        PRBool defaultChecked;
        GetDefaultChecked(&defaultChecked);
        DoSetChecked(defaultChecked);
        SetCheckedChanged(PR_FALSE);
      }
    }

    if (aName == nsGkAtoms::type) {
      // Changing type means notifying on state changes.  Just start a batch
      // now.
      nsIDocument* document = GetCurrentDoc();
      MOZ_AUTO_DOC_UPDATE(document, UPDATE_CONTENT_STATE, aNotify);

      UpdateEditableState();

      if (!aValue) {
        // We're now a text input.  Note that we have to handle this manually,
        // since removing an attribute (which is what happened, since aValue is
        // null) doesn't call ParseAttribute.
        mType = NS_FORM_INPUT_TEXT;
      }
    
      // If we are changing type from File/Text/Passwd to other input types
      // we need save the mValue into value attribute
      if (mValue &&
          mType != NS_FORM_INPUT_TEXT &&
          mType != NS_FORM_INPUT_PASSWORD &&
          mType != NS_FORM_INPUT_FILE) {
        SetAttr(kNameSpaceID_None, nsGkAtoms::value,
                NS_ConvertUTF8toUTF16(mValue), PR_FALSE);
        if (mValue) {
          nsMemory::Free(mValue);
          mValue = nsnull;
        }
      }

      if (mType != NS_FORM_INPUT_IMAGE) {
        // We're no longer an image input.  Cancel our image requests, if we have
        // any.  Note that doing this when we already weren't an image is ok --
        // just does nothing.
        CancelImageRequests(aNotify);
      } else if (aNotify) {
        // We just got switched to be an image input; we should see
        // whether we have an image to load;
        nsAutoString src;
        if (GetAttr(kNameSpaceID_None, nsGkAtoms::src, src)) {
          LoadImage(src, PR_FALSE, aNotify);
        }
      }

      if (aNotify && document) {
        // Changing type affects the applicability of some states.  Just notify
        // on them all now, just in case.  Note that we can't rely on the
        // notifications LoadImage or CancelImageRequests might have sent,
        // because those didn't include all the possibly-changed states in the
        // mask.  We have to do this here because we just updated mType, so the
        // code in nsGenericElement::SetAttrAndNotify didn't see the new
        // states.
        document->ContentStatesChanged(this, nsnull,
                                       NS_EVENT_STATE_CHECKED |
                                       NS_EVENT_STATE_DEFAULT |
                                       NS_EVENT_STATE_BROKEN |
                                       NS_EVENT_STATE_USERDISABLED |
                                       NS_EVENT_STATE_SUPPRESSED |
                                       NS_EVENT_STATE_LOADING |
                                       NS_EVENT_STATE_INDETERMINATE |
                                       NS_EVENT_STATE_MOZ_READONLY |
                                       NS_EVENT_STATE_MOZ_READWRITE);
      }
    }

    // If readonly is changed for text and password we need to handle
    // :read-only / :read-write
    if (aNotify && aName == nsGkAtoms::readonly &&
        (mType == NS_FORM_INPUT_TEXT || mType == NS_FORM_INPUT_PASSWORD)) {
      UpdateEditableState();

      nsIDocument* document = GetCurrentDoc();
      if (document) {
        mozAutoDocUpdate upd(document, UPDATE_CONTENT_STATE, PR_TRUE);
        document->ContentStatesChanged(this, nsnull,
                                       NS_EVENT_STATE_MOZ_READONLY |
                                       NS_EVENT_STATE_MOZ_READWRITE);
      }
    }
  }

  return nsGenericHTMLFormElement::AfterSetAttr(aNameSpaceID, aName,
                                                aValue, aNotify);
}

// nsIDOMHTMLInputElement

NS_IMETHODIMP
nsHTMLInputElement::GetForm(nsIDOMHTMLFormElement** aForm)
{
  return nsGenericHTMLFormElement::GetForm(aForm);
}

//NS_IMPL_STRING_ATTR(nsHTMLInputElement, DefaultValue, value)
NS_IMPL_BOOL_ATTR(nsHTMLInputElement, DefaultChecked, checked)
NS_IMPL_STRING_ATTR(nsHTMLInputElement, Accept, accept)
NS_IMPL_STRING_ATTR(nsHTMLInputElement, AccessKey, accesskey)
NS_IMPL_STRING_ATTR(nsHTMLInputElement, Align, align)
NS_IMPL_STRING_ATTR(nsHTMLInputElement, Alt, alt)
//NS_IMPL_BOOL_ATTR(nsHTMLInputElement, Checked, checked)
NS_IMPL_BOOL_ATTR(nsHTMLInputElement, Disabled, disabled)
NS_IMPL_BOOL_ATTR(nsHTMLInputElement, Multiple, multiple)
NS_IMPL_INT_ATTR(nsHTMLInputElement, MaxLength, maxlength)
NS_IMPL_STRING_ATTR(nsHTMLInputElement, Name, name)
NS_IMPL_BOOL_ATTR(nsHTMLInputElement, ReadOnly, readonly)
NS_IMPL_URI_ATTR(nsHTMLInputElement, Src, src)
NS_IMPL_INT_ATTR_DEFAULT_VALUE(nsHTMLInputElement, TabIndex, tabindex, 0)
NS_IMPL_STRING_ATTR(nsHTMLInputElement, UseMap, usemap)
//NS_IMPL_STRING_ATTR(nsHTMLInputElement, Value, value)
//NS_IMPL_INT_ATTR_DEFAULT_VALUE(nsHTMLInputElement, Size, size, 0)
//NS_IMPL_STRING_ATTR_DEFAULT_VALUE(nsHTMLInputElement, Type, type, "text")

NS_IMETHODIMP
nsHTMLInputElement::GetDefaultValue(nsAString& aValue)
{
  GetAttrHelper(nsGkAtoms::value, aValue);

  if (mType != NS_FORM_INPUT_HIDDEN) {
    // Bug 114997: trim \n, etc. for non-hidden inputs
    aValue = nsContentUtils::TrimCharsInSet(kWhitespace, aValue);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLInputElement::SetDefaultValue(const nsAString& aValue)
{
  return SetAttrHelper(nsGkAtoms::value, aValue);
}

NS_IMETHODIMP
nsHTMLInputElement::GetIndeterminate(PRBool* aValue)
{
  *aValue = GET_BOOLBIT(mBitField, BF_INDETERMINATE);
  return NS_OK;
}

nsresult
nsHTMLInputElement::SetIndeterminateInternal(PRBool aValue,
                                             PRBool aShouldInvalidate)
{
  SET_BOOLBIT(mBitField, BF_INDETERMINATE, aValue);

  if (aShouldInvalidate) {
    // Repaint the frame
    nsIFrame* frame = GetPrimaryFrame();
    if (frame)
      frame->InvalidateOverflowRect();
  }

  // Notify the document so it can update :indeterminate pseudoclass rules
  nsIDocument* document = GetCurrentDoc();
  if (document) {
    mozAutoDocUpdate upd(document, UPDATE_CONTENT_STATE, PR_TRUE);
    document->ContentStatesChanged(this, nsnull, NS_EVENT_STATE_INDETERMINATE);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLInputElement::SetIndeterminate(PRBool aValue)
{
  return SetIndeterminateInternal(aValue, PR_TRUE);
}

NS_IMETHODIMP
nsHTMLInputElement::GetSize(PRUint32* aValue)
{
  const nsAttrValue* attrVal = mAttrsAndChildren.GetAttr(nsGkAtoms::size);
  if (attrVal && attrVal->Type() == nsAttrValue::eInteger) {
    *aValue = attrVal->GetIntegerValue();
  }
  else {
    *aValue = 0;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLInputElement::SetSize(PRUint32 aValue)
{
  nsAutoString val;
  val.AppendInt(aValue);

  return SetAttr(kNameSpaceID_None, nsGkAtoms::size, val, PR_TRUE);
}

NS_IMETHODIMP 
nsHTMLInputElement::GetValue(nsAString& aValue)
{
  if (mType == NS_FORM_INPUT_TEXT || mType == NS_FORM_INPUT_PASSWORD) {
    // No need to flush here, if there's no frame created for this
    // input yet, there won't be a value in it (that we don't already
    // have) even if we force it to be created
    nsIFormControlFrame* formControlFrame = GetFormControlFrame(PR_FALSE);

    PRBool frameOwnsValue = PR_FALSE;
    if (formControlFrame) {
      nsITextControlFrame* textControlFrame = do_QueryFrame(formControlFrame);
      if (textControlFrame) {
        textControlFrame->OwnsValue(&frameOwnsValue);
      } else {
        // We assume if it's not a text control frame that it owns the value
        frameOwnsValue = PR_TRUE;
      }
    }

    if (frameOwnsValue) {
      formControlFrame->GetFormProperty(nsGkAtoms::value, aValue);
    } else {
      if (!GET_BOOLBIT(mBitField, BF_VALUE_CHANGED) || !mValue) {
        GetDefaultValue(aValue);
      } else {
        CopyUTF8toUTF16(mValue, aValue);
      }
    }

    return NS_OK;
  }

  if (mType == NS_FORM_INPUT_FILE) {
    if (nsContentUtils::IsCallerTrustedForCapability("UniversalFileRead")) {
      if (!mFileNames.IsEmpty()) {
        aValue = mFileNames[0];
      }
      else {
        aValue.Truncate();
      }
    } else {
      // Just return the leaf name
      nsCOMArray<nsIFile> files;
      GetFileArray(files);
      if (files.Count() == 0 || NS_FAILED(files[0]->GetLeafName(aValue))) {
        aValue.Truncate();
      }
    }
    
    return NS_OK;
  }

  // Treat value == defaultValue for other input elements
  if (!GetAttr(kNameSpaceID_None, nsGkAtoms::value, aValue) &&
      (mType == NS_FORM_INPUT_RADIO || mType == NS_FORM_INPUT_CHECKBOX)) {
    // The default value of a radio or checkbox input is "on".
    aValue.AssignLiteral("on");
  }

  if (mType != NS_FORM_INPUT_HIDDEN) {
    aValue = nsContentUtils::TrimCharsInSet(kWhitespace, aValue);
  }

  return NS_OK;
}

NS_IMETHODIMP 
nsHTMLInputElement::SetValue(const nsAString& aValue)
{
  // check security.  Note that setting the value to the empty string is always
  // OK and gives pages a way to clear a file input if necessary.
  if (mType == NS_FORM_INPUT_FILE) {
    if (!aValue.IsEmpty()) {
      if (!nsContentUtils::IsCallerTrustedForCapability("UniversalFileRead")) {
        // setting the value of a "FILE" input widget requires the
        // UniversalFileRead privilege
        return NS_ERROR_DOM_SECURITY_ERR;
      }
      SetSingleFileName(aValue);
    }
    else {
      ClearFileNames();
    }
  }
  else {
    SetValueInternal(aValue, nsnull, PR_FALSE);
  }

  return NS_OK;
}

NS_IMETHODIMP 
nsHTMLInputElement::MozGetFileNameArray(PRUint32 *aLength, PRUnichar ***aFileNames)
{
  if (!nsContentUtils::IsCallerTrustedForCapability("UniversalFileRead")) {
    // Since this function returns full paths it's important that normal pages
    // can't call it.
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  *aLength = mFileNames.Length();
  PRUnichar **ret =
    static_cast<PRUnichar **>(NS_Alloc(mFileNames.Length() * sizeof(PRUnichar*)));
  
  for (PRUint32 i = 0; i <  mFileNames.Length(); i++) {
    ret[i] = NS_strdup(mFileNames[i].get());
  }

  *aFileNames = ret;

  return NS_OK;
}

NS_IMETHODIMP 
nsHTMLInputElement::MozSetFileNameArray(const PRUnichar **aFileNames, PRUint32 aLength)
{
  if (!nsContentUtils::IsCallerTrustedForCapability("UniversalFileRead")) {
    // setting the value of a "FILE" input widget requires the
    // UniversalFileRead privilege
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  nsTArray<nsString> fileNames(aLength);
  for (PRUint32 i = 0; i < aLength; ++i) {
    fileNames.AppendElement(aFileNames[i]);
  }

  SetFileNames(fileNames);

  return NS_OK;
}

NS_IMETHODIMP 
nsHTMLInputElement::SetUserInput(const nsAString& aValue)
{
  if (!nsContentUtils::IsCallerTrustedForWrite()) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  if (mType == NS_FORM_INPUT_FILE)
  {
    SetSingleFileName(aValue);
  } else {
    SetValueInternal(aValue, nsnull, PR_TRUE);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsHTMLInputElement::TakeTextFrameValue(const nsAString& aValue)
{
  if (mValue) {
    nsMemory::Free(mValue);
  }
  mValue = ToNewUTF8String(aValue);
  return NS_OK;
}

void
nsHTMLInputElement::GetDisplayFileName(nsAString& aValue)
{
  aValue.Truncate();
  for (PRUint32 i = 0; i < mFileNames.Length(); ++i) {
    if (i == 0) {
      aValue.Append(mFileNames[i]);
    }
    else {
      aValue.Append(NS_LITERAL_STRING(", ") + mFileNames[i]);
    }
  }
}

void
nsHTMLInputElement::SetFileNames(const nsTArray<nsString>& aFileNames)
{
  mFileNames = aFileNames;
#if DEBUG
  for (PRUint32 i = 0; i < (PRUint32)aFileNames.Length(); ++i) {
    NS_ASSERTION(!aFileNames[i].IsEmpty(), "Empty file name");
  }
#endif
  // No need to flush here, if there's no frame at this point we
  // don't need to force creation of one just to tell it about this
  // new value.  We just want the display to update as needed.
  nsIFormControlFrame* formControlFrame = GetFormControlFrame(PR_FALSE);
  if (formControlFrame) {
    nsAutoString readableValue;
    GetDisplayFileName(readableValue);
    formControlFrame->SetFormProperty(nsGkAtoms::value, readableValue);
  }

  UpdateFileList();
  
  SetValueChanged(PR_TRUE);
}

void
nsHTMLInputElement::GetFileArray(nsCOMArray<nsIFile> &aFiles)
{
  aFiles.Clear();

  if (mType != NS_FORM_INPUT_FILE) {
    return;
  }

  for (PRUint32 i = 0; i < mFileNames.Length(); ++i) {
    nsCOMPtr<nsIFile> file;
    if (StringBeginsWith(mFileNames[i], NS_LITERAL_STRING("file:"),
                         nsCaseInsensitiveStringComparator())) {
      // Converts the URL string into the corresponding nsIFile if possible.
      // A local file will be created if the URL string begins with file://.
      NS_GetFileFromURLSpec(NS_ConvertUTF16toUTF8(mFileNames[i]),
                            getter_AddRefs(file));
    }

    if (!file) {
      // this is no "file://", try as local file
      nsCOMPtr<nsILocalFile> localFile;
      NS_NewLocalFile(mFileNames[i], PR_FALSE, getter_AddRefs(localFile));
      // Wish there was a better way to downcast an already_AddRefed
      file = dont_AddRef(static_cast<nsIFile*>(localFile.forget().get()));
    }

    if (file) {
      aFiles.AppendObject(file);
    }
  }
}

nsresult
nsHTMLInputElement::UpdateFileList()
{
  if (mFileList) {
    mFileList->Clear();

    nsCOMArray<nsIFile> files;
    GetFileArray(files);
    for (PRUint32 i = 0; i < (PRUint32)files.Count(); ++i) {
      nsRefPtr<nsDOMFile> domFile = new nsDOMFile(files[i]);
      if (domFile) {
        if (!mFileList->Append(domFile)) {
          return NS_ERROR_FAILURE;
        }
      }
    }
  }

  return NS_OK;
}

nsresult
nsHTMLInputElement::SetValueInternal(const nsAString& aValue,
                                     nsITextControlFrame* aFrame,
                                     PRBool aUserInput)
{
  NS_PRECONDITION(mType != NS_FORM_INPUT_FILE,
                  "Don't call SetValueInternal for file inputs");

  if (mType == NS_FORM_INPUT_TEXT || mType == NS_FORM_INPUT_PASSWORD) {

    nsIFormControlFrame* formControlFrame = aFrame;
    if (!formControlFrame) {
      // No need to flush here, if there's no frame at this point we
      // don't need to force creation of one just to tell it about this
      // new value.
      formControlFrame = GetFormControlFrame(PR_FALSE);
    }

    if (formControlFrame) {
      // Always set the value in the frame.  If the frame does not own the
      // value yet (per OwnsValue()), it will turn around and call
      // TakeTextFrameValue() on us, but will update its display with the new
      // value if needed.
      formControlFrame->SetFormProperty(
        aUserInput ? nsGkAtoms::userInput : nsGkAtoms::value, aValue);
      return NS_OK;
    }

    SetValueChanged(PR_TRUE);
    return TakeTextFrameValue(aValue);
  }

  if (mType == NS_FORM_INPUT_FILE) {
    return NS_ERROR_UNEXPECTED;
  }

  // If the value of a hidden input was changed, we mark it changed so that we
  // will know we need to save / restore the value.  Yes, we are overloading
  // the meaning of ValueChanged just a teensy bit to save a measly byte of
  // storage space in nsHTMLInputElement.  Yes, you are free to make a new flag,
  // NEED_TO_SAVE_VALUE, at such time as mBitField becomes a 16-bit value.
  if (mType == NS_FORM_INPUT_HIDDEN) {
    SetValueChanged(PR_TRUE);
  }

  // Treat value == defaultValue for other input elements.
  return nsGenericHTMLFormElement::SetAttr(kNameSpaceID_None,
                                           nsGkAtoms::value, aValue,
                                           PR_TRUE);
}

NS_IMETHODIMP
nsHTMLInputElement::SetValueChanged(PRBool aValueChanged)
{
  SET_BOOLBIT(mBitField, BF_VALUE_CHANGED, aValueChanged);
  if (!aValueChanged) {
    if (mValue) {
      nsMemory::Free(mValue);
      mValue = nsnull;
    }
  }
  return NS_OK;
}

NS_IMETHODIMP 
nsHTMLInputElement::GetChecked(PRBool* aChecked)
{
  *aChecked = GET_BOOLBIT(mBitField, BF_CHECKED);
  return NS_OK;
}

NS_IMETHODIMP
nsHTMLInputElement::SetCheckedChanged(PRBool aCheckedChanged)
{
  return DoSetCheckedChanged(aCheckedChanged, PR_TRUE);
}

nsresult
nsHTMLInputElement::DoSetCheckedChanged(PRBool aCheckedChanged,
                                        PRBool aNotify)
{
  if (mType == NS_FORM_INPUT_RADIO) {
    if (GET_BOOLBIT(mBitField, BF_CHECKED_CHANGED) != aCheckedChanged) {
      nsCOMPtr<nsIRadioVisitor> visitor;
      NS_GetRadioSetCheckedChangedVisitor(aCheckedChanged,
                                          getter_AddRefs(visitor));
      VisitGroup(visitor, aNotify);
    }
  } else {
    SetCheckedChangedInternal(aCheckedChanged);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsHTMLInputElement::SetCheckedChangedInternal(PRBool aCheckedChanged)
{
  SET_BOOLBIT(mBitField, BF_CHECKED_CHANGED, aCheckedChanged);
  return NS_OK;
}


NS_IMETHODIMP
nsHTMLInputElement::GetCheckedChanged(PRBool* aCheckedChanged)
{
  *aCheckedChanged = GET_BOOLBIT(mBitField, BF_CHECKED_CHANGED);
  return NS_OK;
}

NS_IMETHODIMP
nsHTMLInputElement::SetChecked(PRBool aChecked)
{
  return DoSetChecked(aChecked);
}

nsresult
nsHTMLInputElement::DoSetChecked(PRBool aChecked, PRBool aNotify)
{
  nsresult rv = NS_OK;

  //
  // If the user or JS attempts to set checked, whether it actually changes the
  // value or not, we say the value was changed so that defaultValue don't
  // affect it no more.
  //
  DoSetCheckedChanged(PR_TRUE, aNotify);

  //
  // Don't do anything if we're not changing whether it's checked (it would
  // screw up state actually, especially when you are setting radio button to
  // false)
  //
  PRBool checked = PR_FALSE;
  GetChecked(&checked);
  if (checked == aChecked) {
    return NS_OK;
  }

  //
  // Set checked
  //
  if (mType == NS_FORM_INPUT_RADIO) {
    //
    // For radio button, we need to do some extra fun stuff
    //
    if (aChecked) {
      rv = RadioSetChecked(aNotify);
    } else {
      rv = SetCheckedInternal(PR_FALSE, aNotify);
      nsCOMPtr<nsIRadioGroupContainer> container = GetRadioGroupContainer();
      if (container) {
        nsAutoString name;
        if (GetNameIfExists(name)) {
          container->SetCurrentRadioButton(name, nsnull);
        }
      }
    }
  } else {
    rv = SetCheckedInternal(aChecked, aNotify);
  }

  return rv;
}

NS_IMETHODIMP
nsHTMLInputElement::RadioSetChecked(PRBool aNotify)
{
  nsresult rv = NS_OK;

  //
  // Find the selected radio button so we can deselect it
  //
  nsCOMPtr<nsIDOMHTMLInputElement> currentlySelected;
  nsCOMPtr<nsIRadioGroupContainer> container = GetRadioGroupContainer();
  // This is ONLY INITIALIZED IF container EXISTS
  nsAutoString name;
  PRBool nameExists = PR_FALSE;
  if (container) {
    nameExists = GetNameIfExists(name);
    if (nameExists) {
      container->GetCurrentRadioButton(name, getter_AddRefs(currentlySelected));
    }
  }

  //
  // Deselect the currently selected radio button
  //
  if (currentlySelected) {
    // Pass PR_TRUE for the aNotify parameter since the currently selected
    // button is already in the document.
    rv = static_cast<nsHTMLInputElement*>
                    (static_cast<nsIDOMHTMLInputElement*>(currentlySelected))->SetCheckedInternal(PR_FALSE, PR_TRUE);
  }

  //
  // Actually select this one
  //
  if (NS_SUCCEEDED(rv)) {
    rv = SetCheckedInternal(PR_TRUE, aNotify);
  }

  //
  // Let the group know that we are now the One True Radio Button
  //
  NS_ENSURE_SUCCESS(rv, rv);
  if (container && nameExists) {
    rv = container->SetCurrentRadioButton(name, this);
  }

  return rv;
}

/* virtual */ already_AddRefed<nsIRadioGroupContainer>
nsHTMLInputElement::GetRadioGroupContainer()
{
  nsIRadioGroupContainer* retval = nsnull;
  if (mForm) {
    CallQueryInterface(mForm, &retval);
  } else {
    nsIDocument* currentDoc = GetCurrentDoc();
    if (currentDoc) {
      CallQueryInterface(currentDoc, &retval);
    }
  }
  return retval;
}

nsresult
nsHTMLInputElement::MaybeSubmitForm(nsPresContext* aPresContext)
{
  if (!mForm) {
    // Nothing to do here.
    return NS_OK;
  }
  
  nsCOMPtr<nsIPresShell> shell = aPresContext->GetPresShell();
  if (!shell) {
    return NS_OK;
  }

  // Get the default submit element
  nsIFormControl* submitControl = mForm->GetDefaultSubmitElement();
  if (submitControl) {
    nsCOMPtr<nsIContent> submitContent(do_QueryInterface(submitControl));
    NS_ASSERTION(submitContent, "Form control not implementing nsIContent?!");
    // Fire the button's onclick handler and let the button handle
    // submitting the form.
    nsMouseEvent event(PR_TRUE, NS_MOUSE_CLICK, nsnull, nsMouseEvent::eReal);
    nsEventStatus status = nsEventStatus_eIgnore;
    shell->HandleDOMEventWithTarget(submitContent, &event, &status);
  } else if (mForm->HasSingleTextControl()) {
    // If there's only one text control, just submit the form
    nsCOMPtr<nsIContent> form = do_QueryInterface(mForm);
    nsFormEvent event(PR_TRUE, NS_FORM_SUBMIT);
    nsEventStatus status  = nsEventStatus_eIgnore;
    shell->HandleDOMEventWithTarget(form, &event, &status);
  }

  return NS_OK;
}

nsresult
nsHTMLInputElement::SetCheckedInternal(PRBool aChecked, PRBool aNotify)
{
  //
  // Set the value
  //
  SET_BOOLBIT(mBitField, BF_CHECKED, aChecked);

  //
  // Notify the frame
  //
  nsIFrame* frame = GetPrimaryFrame();
  if (frame) {
    nsPresContext *presContext = GetPresContext();

    if (mType == NS_FORM_INPUT_CHECKBOX) {
      nsICheckboxControlFrame* checkboxFrame = do_QueryFrame(frame);
      if (checkboxFrame) {
        checkboxFrame->OnChecked(presContext, aChecked);
      }
    } else if (mType == NS_FORM_INPUT_RADIO) {
      nsIRadioControlFrame* radioFrame = do_QueryFrame(frame);
      if (radioFrame) {
        radioFrame->OnChecked(presContext, aChecked);
      }
    }
  }

  // Notify the document that the CSS :checked pseudoclass for this element
  // has changed state.
  if (aNotify) {
    nsIDocument* document = GetCurrentDoc();
    if (document) {
      mozAutoDocUpdate upd(document, UPDATE_CONTENT_STATE, aNotify);
      document->ContentStatesChanged(this, nsnull, NS_EVENT_STATE_CHECKED);
    }
  }

  return NS_OK;
}


void
nsHTMLInputElement::FireOnChange()
{
  //
  // Since the value is changing, send out an onchange event (bug 23571)
  //
  nsEventStatus status = nsEventStatus_eIgnore;
  nsEvent event(PR_TRUE, NS_FORM_CHANGE);
  nsCOMPtr<nsPresContext> presContext = GetPresContext();
  nsEventDispatcher::Dispatch(static_cast<nsIContent*>(this), presContext,
                              &event, nsnull, &status);
}

NS_IMETHODIMP
nsHTMLInputElement::Blur()
{
  return nsGenericHTMLElement::Blur();
}

NS_IMETHODIMP
nsHTMLInputElement::Focus()
{
  if (mType == NS_FORM_INPUT_FILE) {
    // for file inputs, focus the button instead
    nsIFrame* frame = GetPrimaryFrame();
    if (frame) {
      nsIFrame* childFrame = frame->GetFirstChild(nsnull);
      while (childFrame) {
        // see if the child is a button control
        nsCOMPtr<nsIFormControl> formCtrl =
          do_QueryInterface(childFrame->GetContent());
        if (formCtrl && formCtrl->GetType() == NS_FORM_INPUT_BUTTON) {
          nsCOMPtr<nsIDOMElement> element(do_QueryInterface(formCtrl));
          nsIFocusManager* fm = nsFocusManager::GetFocusManager();
          if (fm && element)
            fm->SetFocus(element, 0);
          break;
        }

        childFrame = childFrame->GetNextSibling();
      }
    }

    return NS_OK;
  }

  return nsGenericHTMLElement::Focus();
}

NS_IMETHODIMP
nsHTMLInputElement::Select()
{
  if (mType != NS_FORM_INPUT_PASSWORD && mType != NS_FORM_INPUT_TEXT) {
    return NS_OK;
  }

  // XXX Bug?  We have to give the input focus before contents can be
  // selected

  FocusTristate state = FocusState();
  if (state == eUnfocusable) {
    return NS_OK;
  }

  nsIFocusManager* fm = nsFocusManager::GetFocusManager();

  nsCOMPtr<nsPresContext> presContext = GetPresContext();
  if (state == eInactiveWindow) {
    if (fm)
      fm->SetFocus(this, nsIFocusManager::FLAG_NOSCROLL);
    SelectAll(presContext);
    return NS_OK;
  }

  if (DispatchSelectEvent(presContext) && fm) {
    fm->SetFocus(this, nsIFocusManager::FLAG_NOSCROLL);

    // ensure that the element is actually focused
    nsCOMPtr<nsIDOMElement> focusedElement;
    fm->GetFocusedElement(getter_AddRefs(focusedElement));
    if (SameCOMIdentity(static_cast<nsIDOMNode *>(this), focusedElement)) {
      // Now Select all the text!
      SelectAll(presContext);
    }
  }

  return NS_OK;
}

PRBool
nsHTMLInputElement::DispatchSelectEvent(nsPresContext* aPresContext)
{
  nsEventStatus status = nsEventStatus_eIgnore;

  // If already handling select event, don't dispatch a second.
  if (!GET_BOOLBIT(mBitField, BF_HANDLING_SELECT_EVENT)) {
    nsEvent event(nsContentUtils::IsCallerChrome(), NS_FORM_SELECTED);

    SET_BOOLBIT(mBitField, BF_HANDLING_SELECT_EVENT, PR_TRUE);
    nsEventDispatcher::Dispatch(static_cast<nsIContent*>(this),
                                aPresContext, &event, nsnull, &status);
    SET_BOOLBIT(mBitField, BF_HANDLING_SELECT_EVENT, PR_FALSE);
  }

  // If the DOM event was not canceled (e.g. by a JS event handler
  // returning false)
  return (status == nsEventStatus_eIgnore);
}
    
void
nsHTMLInputElement::SelectAll(nsPresContext* aPresContext)
{
  nsIFormControlFrame* formControlFrame = GetFormControlFrame(PR_TRUE);

  if (formControlFrame) {
    formControlFrame->SetFormProperty(nsGkAtoms::select, EmptyString());
  }
}

NS_IMETHODIMP
nsHTMLInputElement::Click()
{
  nsresult rv = NS_OK;

  if (GET_BOOLBIT(mBitField, BF_HANDLING_CLICK)) // Fixes crash as in bug 41599
      return rv;                      // --heikki@netscape.com

  // first see if we are disabled or not. If disabled then do nothing.
  nsAutoString disabled;
  if (HasAttr(kNameSpaceID_None, nsGkAtoms::disabled)) {
    return NS_OK;
  }

  // see what type of input we are.  Only click button, checkbox, radio,
  // reset, submit, & image
  if (mType == NS_FORM_INPUT_BUTTON   ||
      mType == NS_FORM_INPUT_CHECKBOX ||
      mType == NS_FORM_INPUT_RADIO    ||
      mType == NS_FORM_INPUT_RESET    ||
      mType == NS_FORM_INPUT_SUBMIT   ||
      mType == NS_FORM_INPUT_IMAGE) {

    // Strong in case the event kills it
    nsCOMPtr<nsIDocument> doc = GetCurrentDoc();
    if (!doc) {
      return rv;
    }
    
    nsIPresShell *shell = doc->GetPrimaryShell();

    if (shell) {
      nsCOMPtr<nsPresContext> context = shell->GetPresContext();

      if (context) {
        // Click() is never called from native code, but it may be
        // called from chrome JS. Mark this event trusted if Click()
        // is called from chrome code.
        nsMouseEvent event(nsContentUtils::IsCallerChrome(),
                           NS_MOUSE_CLICK, nsnull, nsMouseEvent::eReal);
        nsEventStatus status = nsEventStatus_eIgnore;

        SET_BOOLBIT(mBitField, BF_HANDLING_CLICK, PR_TRUE);

        nsEventDispatcher::Dispatch(static_cast<nsIContent*>(this), context,
                                    &event, nsnull, &status);

        SET_BOOLBIT(mBitField, BF_HANDLING_CLICK, PR_FALSE);
      }
    }
  }

  return NS_OK;
}

nsresult
nsHTMLInputElement::PreHandleEvent(nsEventChainPreVisitor& aVisitor)
{
  // Do not process any DOM events if the element is disabled
  aVisitor.mCanHandle = PR_FALSE;
  PRBool disabled;
  nsresult rv = GetDisabled(&disabled);
  NS_ENSURE_SUCCESS(rv, rv);
  if (disabled) {
    return NS_OK;
  }
  
  // For some reason or another we also need to check if the style shows us
  // as disabled.
  {
    nsIFrame* frame = GetPrimaryFrame();
    if (frame) {
      const nsStyleUserInterface* uiStyle = frame->GetStyleUserInterface();

      if (uiStyle->mUserInput == NS_STYLE_USER_INPUT_NONE ||
          uiStyle->mUserInput == NS_STYLE_USER_INPUT_DISABLED) {
        return NS_OK;
      }
    }
  }

  //FIXME Allow submission etc. also when there is no prescontext, Bug 329509.
  if (!aVisitor.mPresContext) {
    return nsGenericHTMLElement::PreHandleEvent(aVisitor);
  }
  //
  // Web pages expect the value of a radio button or checkbox to be set
  // *before* onclick and DOMActivate fire, and they expect that if they set
  // the value explicitly during onclick or DOMActivate it will not be toggled
  // or any such nonsense.
  // In order to support that (bug 57137 and 58460 are examples) we toggle
  // the checked attribute *first*, and then fire onclick.  If the user
  // returns false, we reset the control to the old checked value.  Otherwise,
  // we dispatch DOMActivate.  If DOMActivate is cancelled, we also reset
  // the control to the old checked value.  We need to keep track of whether
  // we've already toggled the state from onclick since the user could
  // explicitly dispatch DOMActivate on the element.
  //
  // This is a compatibility hack.
  //

  // Track whether we're in the outermost Dispatch invocation that will
  // cause activation of the input.  That is, if we're a click event, or a
  // DOMActivate that was dispatched directly, this will be set, but if we're
  // a DOMActivate dispatched from click handling, it will not be set.
  PRBool outerActivateEvent =
    (NS_IS_MOUSE_LEFT_CLICK(aVisitor.mEvent) ||
     (aVisitor.mEvent->message == NS_UI_ACTIVATE &&
      !GET_BOOLBIT(mBitField, BF_IN_INTERNAL_ACTIVATE)));

  if (outerActivateEvent) {
    aVisitor.mItemFlags |= NS_OUTER_ACTIVATE_EVENT;
  }

  PRBool originalCheckedValue = PR_FALSE;

  if (outerActivateEvent) {
    SET_BOOLBIT(mBitField, BF_CHECKED_IS_TOGGLED, PR_FALSE);

    switch(mType) {
      case NS_FORM_INPUT_CHECKBOX:
        {
          if (GET_BOOLBIT(mBitField, BF_INDETERMINATE)) {
            // indeterminate is always set to FALSE when the checkbox is toggled
            SetIndeterminateInternal(PR_FALSE, PR_FALSE);
            aVisitor.mItemFlags |= NS_ORIGINAL_INDETERMINATE_VALUE;
          }

          GetChecked(&originalCheckedValue);
          DoSetChecked(!originalCheckedValue);
          SET_BOOLBIT(mBitField, BF_CHECKED_IS_TOGGLED, PR_TRUE);
        }
        break;

      case NS_FORM_INPUT_RADIO:
        {
          nsCOMPtr<nsIRadioGroupContainer> container = GetRadioGroupContainer();
          if (container) {
            nsAutoString name;
            if (GetNameIfExists(name)) {
              nsCOMPtr<nsIDOMHTMLInputElement> selectedRadioButton;
              container->GetCurrentRadioButton(name,
                                               getter_AddRefs(selectedRadioButton));
              aVisitor.mItemData = selectedRadioButton;
            }
          }

          GetChecked(&originalCheckedValue);
          if (!originalCheckedValue) {
            DoSetChecked(PR_TRUE);
            SET_BOOLBIT(mBitField, BF_CHECKED_IS_TOGGLED, PR_TRUE);
          }
        }
        break;

      case NS_FORM_INPUT_SUBMIT:
      case NS_FORM_INPUT_IMAGE:
        if(mForm) {
          // tell the form that we are about to enter a click handler.
          // that means that if there are scripted submissions, the
          // latest one will be deferred until after the exit point of the handler. 
          mForm->OnSubmitClickBegin();
        }
        break;

      default:
        break;
    } //switch
  }

  if (originalCheckedValue) {
    aVisitor.mItemFlags |= NS_ORIGINAL_CHECKED_VALUE;
  }

  // If NS_EVENT_FLAG_NO_CONTENT_DISPATCH is set we will not allow content to handle
  // this event.  But to allow middle mouse button paste to work we must allow 
  // middle clicks to go to text fields anyway.
  if (aVisitor.mEvent->flags & NS_EVENT_FLAG_NO_CONTENT_DISPATCH) {
    aVisitor.mItemFlags |= NS_NO_CONTENT_DISPATCH;
  }
  if ((mType == NS_FORM_INPUT_TEXT || mType == NS_FORM_INPUT_PASSWORD) &&
      aVisitor.mEvent->message == NS_MOUSE_CLICK &&
      aVisitor.mEvent->eventStructType == NS_MOUSE_EVENT &&
      static_cast<nsMouseEvent*>(aVisitor.mEvent)->button ==
        nsMouseEvent::eMiddleButton) {
    aVisitor.mEvent->flags &= ~NS_EVENT_FLAG_NO_CONTENT_DISPATCH;
  }

  // We must cache type because mType may change during JS event (bug 2369)
  aVisitor.mItemFlags |= static_cast<PRUint8>(mType);

  // Fire onchange (if necessary), before we do the blur, bug 357684.
  if (aVisitor.mEvent->message == NS_BLUR_CONTENT) {
    nsIFrame* primaryFrame = GetPrimaryFrame();
    if (primaryFrame) {
      nsITextControlFrame* textFrame = do_QueryFrame(primaryFrame);
      if (textFrame) {
        textFrame->CheckFireOnChange();
      }
    }
  }

  return nsGenericHTMLFormElement::PreHandleEvent(aVisitor);
}

static PRBool
SelectTextFieldOnFocus()
{
  if (!gSelectTextFieldOnFocus) {
    nsCOMPtr<nsILookAndFeel> lookNFeel(do_GetService(kLookAndFeelCID));
    if (lookNFeel) {
      PRInt32 selectTextfieldsOnKeyFocus = -1;
      lookNFeel->GetMetric(nsILookAndFeel::eMetric_SelectTextfieldsOnKeyFocus,
                           selectTextfieldsOnKeyFocus);
      gSelectTextFieldOnFocus = selectTextfieldsOnKeyFocus != 0 ? 1 : -1;
    }
    else {
      gSelectTextFieldOnFocus = -1;
    }
  }

  return gSelectTextFieldOnFocus == 1;
}

nsresult
nsHTMLInputElement::PostHandleEvent(nsEventChainPostVisitor& aVisitor)
{
  if (!aVisitor.mPresContext) {
    return NS_OK;
  }
  nsresult rv = NS_OK;
  PRBool outerActivateEvent = !!(aVisitor.mItemFlags & NS_OUTER_ACTIVATE_EVENT);
  PRBool originalCheckedValue =
    !!(aVisitor.mItemFlags & NS_ORIGINAL_CHECKED_VALUE);
  PRBool noContentDispatch = !!(aVisitor.mItemFlags & NS_NO_CONTENT_DISPATCH);
  PRInt8 oldType = NS_CONTROL_TYPE(aVisitor.mItemFlags);
  // Ideally we would make the default action for click and space just dispatch
  // DOMActivate, and the default action for DOMActivate flip the checkbox/
  // radio state and fire onchange.  However, for backwards compatibility, we
  // need to flip the state before firing click, and we need to fire click
  // when space is pressed.  So, we just nest the firing of DOMActivate inside
  // the click event handling, and allow cancellation of DOMActivate to cancel
  // the click.
  if (aVisitor.mEventStatus != nsEventStatus_eConsumeNoDefault &&
      mType != NS_FORM_INPUT_TEXT &&
      NS_IS_MOUSE_LEFT_CLICK(aVisitor.mEvent)) {
    nsUIEvent actEvent(NS_IS_TRUSTED_EVENT(aVisitor.mEvent), NS_UI_ACTIVATE, 1);

    nsCOMPtr<nsIPresShell> shell = aVisitor.mPresContext->GetPresShell();
    if (shell) {
      nsEventStatus status = nsEventStatus_eIgnore;
      SET_BOOLBIT(mBitField, BF_IN_INTERNAL_ACTIVATE, PR_TRUE);
      rv = shell->HandleDOMEventWithTarget(this, &actEvent, &status);
      SET_BOOLBIT(mBitField, BF_IN_INTERNAL_ACTIVATE, PR_FALSE);

      // If activate is cancelled, we must do the same as when click is
      // cancelled (revert the checkbox to its original value).
      if (status == nsEventStatus_eConsumeNoDefault)
        aVisitor.mEventStatus = status;
    }
  }

  if (outerActivateEvent) {
    switch(oldType) {
      case NS_FORM_INPUT_SUBMIT:
      case NS_FORM_INPUT_IMAGE:
        if(mForm) {
          // tell the form that we are about to exit a click handler
          // so the form knows not to defer subsequent submissions
          // the pending ones that were created during the handler
          // will be flushed or forgoten.
          mForm->OnSubmitClickEnd();
        }
        break;
    } //switch
  }

  // Reset the flag for other content besides this text field
  aVisitor.mEvent->flags |=
    noContentDispatch ? NS_EVENT_FLAG_NO_CONTENT_DISPATCH : NS_EVENT_FLAG_NONE;

  // now check to see if the event was "cancelled"
  if (GET_BOOLBIT(mBitField, BF_CHECKED_IS_TOGGLED) && outerActivateEvent) {
    if (aVisitor.mEventStatus == nsEventStatus_eConsumeNoDefault) {
      // if it was cancelled and a radio button, then set the old
      // selected btn to TRUE. if it is a checkbox then set it to its
      // original value
      nsCOMPtr<nsIDOMHTMLInputElement> selectedRadioButton =
        do_QueryInterface(aVisitor.mItemData);
      if (selectedRadioButton) {
        selectedRadioButton->SetChecked(PR_TRUE);
        // If this one is no longer a radio button we must reset it back to
        // false to cancel the action.  See how the web of hack grows?
        if (mType != NS_FORM_INPUT_RADIO) {
          DoSetChecked(PR_FALSE);
        }
      } else if (oldType == NS_FORM_INPUT_CHECKBOX) {
        PRBool originalIndeterminateValue =
          !!(aVisitor.mItemFlags & NS_ORIGINAL_INDETERMINATE_VALUE);
        SetIndeterminateInternal(originalIndeterminateValue, PR_FALSE);
        DoSetChecked(originalCheckedValue);
      }
    } else {
      FireOnChange();
#ifdef ACCESSIBILITY
      // Fire an event to notify accessibility
      if (mType == NS_FORM_INPUT_CHECKBOX) {
        FireEventForAccessibility(this, aVisitor.mPresContext,
                                  NS_LITERAL_STRING("CheckboxStateChange"));
      } else {
        FireEventForAccessibility(this, aVisitor.mPresContext,
                                  NS_LITERAL_STRING("RadioStateChange"));
        // Fire event for the previous selected radio.
        nsCOMPtr<nsIDOMHTMLInputElement> previous =
          do_QueryInterface(aVisitor.mItemData);
        if(previous) {
          FireEventForAccessibility(previous, aVisitor.mPresContext,
                                    NS_LITERAL_STRING("RadioStateChange"));
        }
      }
#endif
    }
  }

  if (NS_SUCCEEDED(rv)) {
    if (nsEventStatus_eIgnore == aVisitor.mEventStatus) {
      switch (aVisitor.mEvent->message) {

        case NS_FOCUS_CONTENT:
        {
          nsIFocusManager* fm = nsFocusManager::GetFocusManager();
          if (fm && (mType == NS_FORM_INPUT_TEXT || mType == NS_FORM_INPUT_PASSWORD) &&
              SelectTextFieldOnFocus()) {
            // select the text if the field was focused by the keyboard or a
            // navigation.
            nsIDocument* document = GetCurrentDoc();
            if (document) {
              PRUint32 lastFocusMethod;
              fm->GetLastFocusMethod(document->GetWindow(), &lastFocusMethod);
              if (lastFocusMethod &
                  (nsIFocusManager::FLAG_BYKEY | nsIFocusManager::FLAG_BYMOVEFOCUS)) {
                nsCOMPtr<nsPresContext> presContext = GetPresContext();
                if (DispatchSelectEvent(presContext)) {
                  SelectAll(presContext);
                }
              }
            }
          }
          break;
        }

        case NS_KEY_PRESS:
        case NS_KEY_UP:
        {
          // For backwards compat, trigger checks/radios/buttons with
          // space or enter (bug 25300)
          nsKeyEvent * keyEvent = (nsKeyEvent *)aVisitor.mEvent;

          if ((aVisitor.mEvent->message == NS_KEY_PRESS &&
               keyEvent->keyCode == NS_VK_RETURN) ||
              (aVisitor.mEvent->message == NS_KEY_UP &&
               keyEvent->keyCode == NS_VK_SPACE)) {
            switch(mType) {
              case NS_FORM_INPUT_CHECKBOX:
              case NS_FORM_INPUT_RADIO:
              {
                // Checkbox and Radio try to submit on Enter press
                if (keyEvent->keyCode != NS_VK_SPACE) {
                  MaybeSubmitForm(aVisitor.mPresContext);

                  break;  // If we are submitting, do not send click event
                }
                // else fall through and treat Space like click...
              }
              case NS_FORM_INPUT_BUTTON:
              case NS_FORM_INPUT_RESET:
              case NS_FORM_INPUT_SUBMIT:
              case NS_FORM_INPUT_IMAGE: // Bug 34418
              {
                nsMouseEvent event(NS_IS_TRUSTED_EVENT(aVisitor.mEvent),
                                   NS_MOUSE_CLICK, nsnull, nsMouseEvent::eReal);
                nsEventStatus status = nsEventStatus_eIgnore;

                nsEventDispatcher::Dispatch(static_cast<nsIContent*>(this),
                                            aVisitor.mPresContext, &event,
                                            nsnull, &status);
              } // case
            } // switch
          }
          if (aVisitor.mEvent->message == NS_KEY_PRESS &&
              mType == NS_FORM_INPUT_RADIO && !keyEvent->isAlt &&
              !keyEvent->isControl && !keyEvent->isMeta) {
            PRBool isMovingBack = PR_FALSE;
            switch (keyEvent->keyCode) {
              case NS_VK_UP: 
              case NS_VK_LEFT:
                isMovingBack = PR_TRUE;
              case NS_VK_DOWN:
              case NS_VK_RIGHT:
              // Arrow key pressed, focus+select prev/next radio button
              nsCOMPtr<nsIRadioGroupContainer> container = GetRadioGroupContainer();
              if (container) {
                nsAutoString name;
                if (GetNameIfExists(name)) {
                  nsCOMPtr<nsIDOMHTMLInputElement> selectedRadioButton;
                  container->GetNextRadioButton(name, isMovingBack, this,
                                                getter_AddRefs(selectedRadioButton));
                  nsCOMPtr<nsIContent> radioContent =
                    do_QueryInterface(selectedRadioButton);
                  if (radioContent) {
                    rv = selectedRadioButton->Focus();
                    if (NS_SUCCEEDED(rv)) {
                      nsEventStatus status = nsEventStatus_eIgnore;
                      nsMouseEvent event(NS_IS_TRUSTED_EVENT(aVisitor.mEvent),
                                         NS_MOUSE_CLICK, nsnull,
                                         nsMouseEvent::eReal);
                      rv = nsEventDispatcher::Dispatch(radioContent,
                                                       aVisitor.mPresContext,
                                                       &event, nsnull, &status);
                      if (NS_SUCCEEDED(rv)) {
                        aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
                      }
                    }
                  }
                }
              }
            }
          }

          /*
           * If this is input type=text, and the user hit enter, fire onChange
           * and submit the form (if we are in one)
           *
           * Bug 99920, bug 109463 and bug 147850:
           * (a) if there is a submit control in the form, click the first
           *     submit control in the form.
           * (b) if there is just one text control in the form, submit by
           *     sending a submit event directly to the form
           * (c) if there is more than one text input and no submit buttons, do
           *     not submit, period.
           */

          if (aVisitor.mEvent->message == NS_KEY_PRESS &&
              (keyEvent->keyCode == NS_VK_RETURN ||
               keyEvent->keyCode == NS_VK_ENTER) &&
              (mType == NS_FORM_INPUT_TEXT ||
               mType == NS_FORM_INPUT_PASSWORD ||
               mType == NS_FORM_INPUT_FILE)) {

            PRBool isButton = PR_FALSE;
            // If this is an enter on the button of a file input, don't submit
            // -- that's supposed to put up the filepicker
            if (mType == NS_FORM_INPUT_FILE) {
              nsCOMPtr<nsIContent> maybeButton =
                do_QueryInterface(aVisitor.mEvent->originalTarget);
              if (maybeButton) {
                isButton = maybeButton->AttrValueIs(kNameSpaceID_None,
                                                    nsGkAtoms::type,
                                                    nsGkAtoms::button,
                                                    eCaseMatters);
              }
            }

            if (!isButton) {
              nsIFrame* primaryFrame = GetPrimaryFrame();
              if (primaryFrame) {
                nsITextControlFrame* textFrame = do_QueryFrame(primaryFrame);
              
                // Fire onChange (if necessary)
                if (textFrame) {
                  textFrame->CheckFireOnChange();
                }
              }

              rv = MaybeSubmitForm(aVisitor.mPresContext);
              NS_ENSURE_SUCCESS(rv, rv);
            }
          }

        } break; // NS_KEY_PRESS || NS_KEY_UP

        case NS_MOUSE_BUTTON_DOWN:
        case NS_MOUSE_BUTTON_UP:
        case NS_MOUSE_DOUBLECLICK:
        {
          // cancel all of these events for buttons
          //XXXsmaug Why?
          if (aVisitor.mEvent->eventStructType == NS_MOUSE_EVENT &&
              (static_cast<nsMouseEvent*>(aVisitor.mEvent)->button ==
                 nsMouseEvent::eMiddleButton ||
               static_cast<nsMouseEvent*>(aVisitor.mEvent)->button ==
                 nsMouseEvent::eRightButton)) {
            if (mType == NS_FORM_INPUT_BUTTON ||
                mType == NS_FORM_INPUT_RESET ||
                mType == NS_FORM_INPUT_SUBMIT) {
              if (aVisitor.mDOMEvent) {
                aVisitor.mDOMEvent->StopPropagation();
              } else {
                rv = NS_ERROR_FAILURE;
              }
            }

          }
          break;
        }
        default:
          break;
      }

      if (outerActivateEvent) {
        if (mForm && (oldType == NS_FORM_INPUT_SUBMIT ||
                      oldType == NS_FORM_INPUT_IMAGE)) {
          if (mType != NS_FORM_INPUT_SUBMIT && mType != NS_FORM_INPUT_IMAGE) {
            // If the type has changed to a non-submit type, then we want to
            // flush the stored submission if there is one (as if the submit()
            // was allowed to succeed)
            mForm->FlushPendingSubmission();
          }
        }
        switch(mType) {
        case NS_FORM_INPUT_RESET:
        case NS_FORM_INPUT_SUBMIT:
        case NS_FORM_INPUT_IMAGE:
          if (mForm) {
            nsFormEvent event(PR_TRUE, (mType == NS_FORM_INPUT_RESET) ?
                              NS_FORM_RESET : NS_FORM_SUBMIT);
            event.originator      = this;
            nsEventStatus status  = nsEventStatus_eIgnore;

            nsCOMPtr<nsIPresShell> presShell =
              aVisitor.mPresContext->GetPresShell();

            // If |nsIPresShell::Destroy| has been called due to
            // handling the event the pres context will return a null
            // pres shell.  See bug 125624.
            if (presShell) {
              nsCOMPtr<nsIContent> form(do_QueryInterface(mForm));
              presShell->HandleDOMEventWithTarget(form, &event, &status);
            }
          }
          break;

        default:
          break;
        } //switch 
      } //click or outer activate event
    } else if (outerActivateEvent &&
               (oldType == NS_FORM_INPUT_SUBMIT ||
                oldType == NS_FORM_INPUT_IMAGE) &&
               mForm) {
      // tell the form to flush a possible pending submission.
      // the reason is that the script returned false (the event was
      // not ignored) so if there is a stored submission, it needs to
      // be submitted immediately.
      mForm->FlushPendingSubmission();
    }
  } // if

  return rv;
}

void
nsHTMLInputElement::MaybeLoadImage()
{
  // Our base URI may have changed; claim that our URI changed, and the
  // nsImageLoadingContent will decide whether a new image load is warranted.
  nsAutoString uri;
  if (mType == NS_FORM_INPUT_IMAGE &&
      GetAttr(kNameSpaceID_None, nsGkAtoms::src, uri) &&
      (NS_FAILED(LoadImage(uri, PR_FALSE, PR_TRUE)) ||
       !LoadingEnabled())) {
    CancelImageRequests(PR_TRUE);
  }
}

nsresult
nsHTMLInputElement::BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                               nsIContent* aBindingParent,
                               PRBool aCompileEventHandlers)
{
  nsresult rv = nsGenericHTMLFormElement::BindToTree(aDocument, aParent,
                                                     aBindingParent,
                                                     aCompileEventHandlers);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mType == NS_FORM_INPUT_IMAGE) {
    // Our base URI may have changed; claim that our URI changed, and the
    // nsImageLoadingContent will decide whether a new image load is warranted.
    if (HasAttr(kNameSpaceID_None, nsGkAtoms::src)) {
      ClearBrokenState();
      nsContentUtils::AddScriptRunner(
        new nsRunnableMethod<nsHTMLInputElement>(this,
                                                 &nsHTMLInputElement::MaybeLoadImage));
    }
  }

  // Add radio to document if we don't have a form already (if we do it's
  // already been added into that group)
  if (aDocument && !mForm && mType == NS_FORM_INPUT_RADIO) {
    AddedToRadioGroup();
  }

  return rv;
}

void
nsHTMLInputElement::UnbindFromTree(PRBool aDeep, PRBool aNullParent)
{
  // If we have a form and are unbound from it,
  // nsGenericHTMLFormElement::UnbindFromTree() will unset the form and
  // that takes care of form's WillRemove so we just have to take care
  // of the case where we're removing from the document and we don't
  // have a form
  if (!mForm && mType == NS_FORM_INPUT_RADIO) {
    WillRemoveFromRadioGroup();
  }

  nsGenericHTMLFormElement::UnbindFromTree(aDeep, aNullParent);
}

static const nsAttrValue::EnumTable kInputTypeTable[] = {
  { "button", NS_FORM_INPUT_BUTTON },
  { "checkbox", NS_FORM_INPUT_CHECKBOX },
  { "file", NS_FORM_INPUT_FILE },
  { "hidden", NS_FORM_INPUT_HIDDEN },
  { "reset", NS_FORM_INPUT_RESET },
  { "image", NS_FORM_INPUT_IMAGE },
  { "password", NS_FORM_INPUT_PASSWORD },
  { "radio", NS_FORM_INPUT_RADIO },
  { "submit", NS_FORM_INPUT_SUBMIT },
  { "text", NS_FORM_INPUT_TEXT },
  { 0 }
};

PRBool
nsHTMLInputElement::ParseAttribute(PRInt32 aNamespaceID,
                                   nsIAtom* aAttribute,
                                   const nsAString& aValue,
                                   nsAttrValue& aResult)
{
  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::type) {
      // XXX ARG!! This is major evilness. ParseAttribute
      // shouldn't set members. Override SetAttr instead
      PRInt32 newType;
      PRBool success;
      if ((success = aResult.ParseEnumValue(aValue, kInputTypeTable))) {
        newType = aResult.GetEnumValue();
      } else {
        newType = NS_FORM_INPUT_TEXT;
      }

      if (newType != mType) {
        // Make sure to do the check for newType being NS_FORM_INPUT_FILE and
        // the corresponding SetValueInternal() call _before_ we set mType.
        // That way the logic in SetValueInternal() will work right (that logic
        // makes assumptions about our frame based on mType, but we won't have
        // had time to recreate frames yet -- that happens later in the
        // SetAttr() process).
        if (newType == NS_FORM_INPUT_FILE || mType == NS_FORM_INPUT_FILE) {
          // This call isn't strictly needed any more since we'll never
          // confuse values and filenames. However it's there for backwards
          // compat.
          ClearFileNames();
        }

        mType = newType;
      }

      return success;
    }
    if (aAttribute == nsGkAtoms::width) {
      return aResult.ParseSpecialIntValue(aValue, PR_TRUE);
    }
    if (aAttribute == nsGkAtoms::height) {
      return aResult.ParseSpecialIntValue(aValue, PR_TRUE);
    }
    if (aAttribute == nsGkAtoms::maxlength) {
      return aResult.ParseIntWithBounds(aValue, 0);
    }
    if (aAttribute == nsGkAtoms::size) {
      return aResult.ParseIntWithBounds(aValue, 0);
    }
    if (aAttribute == nsGkAtoms::border) {
      return aResult.ParseIntWithBounds(aValue, 0);
    }
    if (aAttribute == nsGkAtoms::align) {
      return ParseAlignValue(aValue, aResult);
    }
    if (ParseImageAttribute(aAttribute, aValue, aResult)) {
      // We have to call |ParseImageAttribute| unconditionally since we
      // don't know if we're going to have a type="image" attribute yet,
      // (or could have it set dynamically in the future).  See bug
      // 214077.
      return PR_TRUE;
    }
  }

  return nsGenericHTMLElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                              aResult);
}

NS_IMETHODIMP
nsHTMLInputElement::GetType(nsAString& aValue)
{
  const nsAttrValue::EnumTable *table = kInputTypeTable;

  while (table->tag) {
    if (mType == table->value) {
      CopyUTF8toUTF16(table->tag, aValue);

      return NS_OK;
    }

    ++table;
  }

  NS_ERROR("Shouldn't get here!");

  aValue.Truncate();

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLInputElement::SetType(const nsAString& aValue)
{
  return SetAttrHelper(nsGkAtoms::type, aValue);
}

static void
MapAttributesIntoRule(const nsMappedAttributes* aAttributes,
                      nsRuleData* aData)
{
  const nsAttrValue* value = aAttributes->GetAttr(nsGkAtoms::type);
  if (value && value->Type() == nsAttrValue::eEnum &&
      value->GetEnumValue() == NS_FORM_INPUT_IMAGE) {
    nsGenericHTMLFormElement::MapImageBorderAttributeInto(aAttributes, aData);
    nsGenericHTMLFormElement::MapImageMarginAttributeInto(aAttributes, aData);
    nsGenericHTMLFormElement::MapImageSizeAttributesInto(aAttributes, aData);
    // Images treat align as "float"
    nsGenericHTMLFormElement::MapImageAlignAttributeInto(aAttributes, aData);
  } 

  nsGenericHTMLFormElement::MapCommonAttributesInto(aAttributes, aData);
}

nsChangeHint
nsHTMLInputElement::GetAttributeChangeHint(const nsIAtom* aAttribute,
                                           PRInt32 aModType) const
{
  nsChangeHint retval =
    nsGenericHTMLFormElement::GetAttributeChangeHint(aAttribute, aModType);
  if (aAttribute == nsGkAtoms::type) {
    NS_UpdateHint(retval, NS_STYLE_HINT_FRAMECHANGE);
  } else if (mType == NS_FORM_INPUT_IMAGE &&
             (aAttribute == nsGkAtoms::alt ||
              aAttribute == nsGkAtoms::value)) {
    // We might need to rebuild our alt text.  Just go ahead and
    // reconstruct our frame.  This should be quite rare..
    NS_UpdateHint(retval, NS_STYLE_HINT_FRAMECHANGE);
  } else if (aAttribute == nsGkAtoms::value) {
    NS_UpdateHint(retval, NS_STYLE_HINT_REFLOW);
  } else if (aAttribute == nsGkAtoms::size &&
             (mType == NS_FORM_INPUT_TEXT ||
              mType == NS_FORM_INPUT_PASSWORD)) {
    NS_UpdateHint(retval, NS_STYLE_HINT_REFLOW);
  }
  return retval;
}

NS_IMETHODIMP_(PRBool)
nsHTMLInputElement::IsAttributeMapped(const nsIAtom* aAttribute) const
{
  static const MappedAttributeEntry attributes[] = {
    { &nsGkAtoms::align },
    { &nsGkAtoms::type },
    { nsnull },
  };

  static const MappedAttributeEntry* const map[] = {
    attributes,
    sCommonAttributeMap,
    sImageMarginSizeAttributeMap,
    sImageBorderAttributeMap,
  };

  return FindAttributeDependence(aAttribute, map, NS_ARRAY_LENGTH(map));
}

nsMapRuleToAttributesFunc
nsHTMLInputElement::GetAttributeMappingFunction() const
{
  return &MapAttributesIntoRule;
}


// Controllers Methods

NS_IMETHODIMP
nsHTMLInputElement::GetControllers(nsIControllers** aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);

  //XXX: what about type "file"?
  if (mType == NS_FORM_INPUT_TEXT || mType == NS_FORM_INPUT_PASSWORD)
  {
    if (!mControllers)
    {
      nsresult rv;
      mControllers = do_CreateInstance(kXULControllersCID, &rv);
      NS_ENSURE_SUCCESS(rv, rv);

      nsCOMPtr<nsIController>
        controller(do_CreateInstance("@mozilla.org/editor/editorcontroller;1",
                                     &rv));
      NS_ENSURE_SUCCESS(rv, rv);

      mControllers->AppendController(controller);
    }
  }

  *aResult = mControllers;
  NS_IF_ADDREF(*aResult);

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLInputElement::GetTextLength(PRInt32* aTextLength)
{
  nsAutoString val;

  nsresult rv = GetValue(val);

  *aTextLength = val.Length();

  return rv;
}

NS_IMETHODIMP
nsHTMLInputElement::SetSelectionRange(PRInt32 aSelectionStart,
                                      PRInt32 aSelectionEnd)
{
  nsresult rv = NS_ERROR_FAILURE;
  nsIFormControlFrame* formControlFrame = GetFormControlFrame(PR_TRUE);

  if (formControlFrame) {
    nsITextControlFrame* textControlFrame = do_QueryFrame(formControlFrame);
    if (textControlFrame)
      rv = textControlFrame->SetSelectionRange(aSelectionStart, aSelectionEnd);
  }

  return rv;
}

NS_IMETHODIMP
nsHTMLInputElement::GetSelectionStart(PRInt32* aSelectionStart)
{
  NS_ENSURE_ARG_POINTER(aSelectionStart);
  
  PRInt32 selEnd;
  return GetSelectionRange(aSelectionStart, &selEnd);
}

NS_IMETHODIMP
nsHTMLInputElement::SetSelectionStart(PRInt32 aSelectionStart)
{
  nsresult rv = NS_ERROR_FAILURE;
  nsIFormControlFrame* formControlFrame = GetFormControlFrame(PR_TRUE);

  if (formControlFrame) {
    nsITextControlFrame* textControlFrame = do_QueryFrame(formControlFrame);
    if (textControlFrame)
      rv = textControlFrame->SetSelectionStart(aSelectionStart);
  }

  return rv;
}

NS_IMETHODIMP
nsHTMLInputElement::GetSelectionEnd(PRInt32* aSelectionEnd)
{
  NS_ENSURE_ARG_POINTER(aSelectionEnd);
  
  PRInt32 selStart;
  return GetSelectionRange(&selStart, aSelectionEnd);
}


NS_IMETHODIMP
nsHTMLInputElement::SetSelectionEnd(PRInt32 aSelectionEnd)
{
  nsresult rv = NS_ERROR_FAILURE;
  nsIFormControlFrame* formControlFrame = GetFormControlFrame(PR_TRUE);

  if (formControlFrame) {
    nsITextControlFrame* textControlFrame = do_QueryFrame(formControlFrame);
    if (textControlFrame)
      rv = textControlFrame->SetSelectionEnd(aSelectionEnd);
  }

  return rv;
}

NS_IMETHODIMP
nsHTMLInputElement::GetFiles(nsIDOMFileList** aFileList)
{
  *aFileList = nsnull;

  if (mType != NS_FORM_INPUT_FILE) {
    return NS_OK;
  }

  if (!mFileList) {
    mFileList = new nsDOMFileList();
    if (!mFileList) return NS_ERROR_OUT_OF_MEMORY;

    UpdateFileList();
  }

  NS_ADDREF(*aFileList = mFileList);

  return NS_OK;
}

nsresult
nsHTMLInputElement::GetSelectionRange(PRInt32* aSelectionStart,
                                      PRInt32* aSelectionEnd)
{
  nsresult rv = NS_ERROR_FAILURE;
  nsIFormControlFrame* formControlFrame = GetFormControlFrame(PR_TRUE);

  if (formControlFrame) {
    nsITextControlFrame* textControlFrame = do_QueryFrame(formControlFrame);
    if (textControlFrame)
      rv = textControlFrame->GetSelectionRange(aSelectionStart, aSelectionEnd);
  }

  return rv;
}

NS_IMETHODIMP
nsHTMLInputElement::GetPhonetic(nsAString& aPhonetic)
{
  aPhonetic.Truncate(0);
  nsIFormControlFrame* formControlFrame = GetFormControlFrame(PR_TRUE);

  if (formControlFrame) {
    nsITextControlFrame* textControlFrame = do_QueryFrame(formControlFrame);
    if (textControlFrame)
      textControlFrame->GetPhonetic(aPhonetic);
  }

  return NS_OK;
}

#ifdef ACCESSIBILITY
/*static*/ nsresult
FireEventForAccessibility(nsIDOMHTMLInputElement* aTarget,
                          nsPresContext* aPresContext,
                          const nsAString& aEventType)
{
  nsCOMPtr<nsIDOMEvent> event;
  if (NS_SUCCEEDED(nsEventDispatcher::CreateEvent(aPresContext, nsnull,
                                                  NS_LITERAL_STRING("Events"),
                                                  getter_AddRefs(event)))) {
    event->InitEvent(aEventType, PR_TRUE, PR_TRUE);

    nsCOMPtr<nsIPrivateDOMEvent> privateEvent(do_QueryInterface(event));
    if (privateEvent) {
      privateEvent->SetTrusted(PR_TRUE);
    }

    nsEventDispatcher::DispatchDOMEvent(aTarget, nsnull, event, aPresContext, nsnull);
  }

  return NS_OK;
}
#endif

nsresult
nsHTMLInputElement::Reset()
{
  nsresult rv = NS_OK;

  nsIFormControlFrame* formControlFrame = GetFormControlFrame(PR_FALSE);

  switch (mType) {
    case NS_FORM_INPUT_CHECKBOX:
    case NS_FORM_INPUT_RADIO:
    {
      PRBool resetVal;
      GetDefaultChecked(&resetVal);
      rv = DoSetChecked(resetVal);
      SetCheckedChanged(PR_FALSE);
      break;
    }
    case NS_FORM_INPUT_PASSWORD:
    case NS_FORM_INPUT_TEXT:
    {
      // If the frame is there, we have to set the value so that it will show
      // up.
      if (formControlFrame) {
        nsAutoString resetVal;
        GetDefaultValue(resetVal);
        rv = SetValue(resetVal);
      }
      SetValueChanged(PR_FALSE);
      break;
    }
    case NS_FORM_INPUT_FILE:
    {
      // Resetting it to blank should not perform security check
      ClearFileNames();
      break;
    }
    // Value is the same as defaultValue for hidden inputs
    case NS_FORM_INPUT_HIDDEN:
    default:
      break;
  }

  return rv;
}

NS_IMETHODIMP
nsHTMLInputElement::SubmitNamesValues(nsIFormSubmission* aFormSubmission,
                                      nsIContent* aSubmitElement)
{
  nsresult rv = NS_OK;

  //
  // Disabled elements don't submit
  //
  PRBool disabled;
  rv = GetDisabled(&disabled);
  if (NS_FAILED(rv) || disabled) {
    return rv;
  }

  //
  // For type=reset, and type=button, we just never submit, period.
  //
  if (mType == NS_FORM_INPUT_RESET || mType == NS_FORM_INPUT_BUTTON) {
    return rv;
  }

  //
  // For type=image and type=button, we only submit if we were the button
  // pressed
  //
  if ((mType == NS_FORM_INPUT_SUBMIT || mType == NS_FORM_INPUT_IMAGE)
      && aSubmitElement != this) {
    return rv;
  }

  //
  // For type=radio and type=checkbox, we only submit if checked=true
  //
  if (mType == NS_FORM_INPUT_RADIO || mType == NS_FORM_INPUT_CHECKBOX) {
    PRBool checked;
    rv = GetChecked(&checked);
    if (NS_FAILED(rv) || !checked) {
      return rv;
    }
  }

  //
  // Get the name
  //
  nsAutoString name;
  PRBool nameThere = GetNameIfExists(name);

  //
  // Submit .x, .y for input type=image
  //
  if (mType == NS_FORM_INPUT_IMAGE) {
    // Get a property set by the frame to find out where it was clicked.
    nsIntPoint* lastClickedPoint =
      static_cast<nsIntPoint*>(GetProperty(nsGkAtoms::imageClickedPoint));
    PRInt32 x, y;
    if (lastClickedPoint) {
      // Convert the values to strings for submission
      x = lastClickedPoint->x;
      y = lastClickedPoint->y;
    } else {
      x = y = 0;
    }

    nsAutoString xVal, yVal;
    xVal.AppendInt(x);
    yVal.AppendInt(y);

    if (!name.IsEmpty()) {
      aFormSubmission->AddNameValuePair(this,
                                        name + NS_LITERAL_STRING(".x"), xVal);
      aFormSubmission->AddNameValuePair(this,
                                        name + NS_LITERAL_STRING(".y"), yVal);
    } else {
      // If the Image Element has no name, simply return x and y
      // to Nav and IE compatibility.
      aFormSubmission->AddNameValuePair(this, NS_LITERAL_STRING("x"), xVal);
      aFormSubmission->AddNameValuePair(this, NS_LITERAL_STRING("y"), yVal);
    }
  }

  //
  // Submit name=value
  //

  // If name not there, don't submit
  if (!nameThere) {
    return rv;
  }

  // Get the value
  nsAutoString value;
  rv = GetValue(value);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (mType == NS_FORM_INPUT_SUBMIT && value.IsEmpty() &&
      !HasAttr(kNameSpaceID_None, nsGkAtoms::value)) {
    // Get our default value, which is the same as our default label
    nsXPIDLString defaultValue;
    nsContentUtils::GetLocalizedString(nsContentUtils::eFORMS_PROPERTIES,
                                       "Submit", defaultValue);
    value = defaultValue;
  }
      
  //
  // Submit file if its input type=file and this encoding method accepts files
  //
  if (mType == NS_FORM_INPUT_FILE) {
    // Submit files

    nsCOMPtr<nsIMIMEService> MIMEService =
      do_GetService(NS_MIMESERVICE_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMArray<nsIFile> files;
    GetFileArray(files);

    for (PRUint32 i = 0; i < (PRUint32)files.Count(); ++i) {
      nsIFile* file = files[i];

      // Get the leaf path name (to be submitted as the value)
      PRBool fileSent = PR_FALSE;

      nsAutoString filename;
      rv = file->GetLeafName(filename);
      if (NS_FAILED(rv)) {
        filename.Truncate();
      }

      if (!filename.IsEmpty() && aFormSubmission->AcceptsFiles()) {
        // Get content type
        nsCAutoString contentType;
        rv = MIMEService->GetTypeFromFile(file, contentType);
        if (NS_FAILED(rv)) {
          contentType.AssignLiteral("application/octet-stream");
        }

        // Get input stream
        nsCOMPtr<nsIInputStream> fileStream;
        rv = NS_NewLocalFileInputStream(getter_AddRefs(fileStream),
                                        file, -1, -1,
                                        nsIFileInputStream::CLOSE_ON_EOF |
                                        nsIFileInputStream::REOPEN_ON_REWIND);
        if (fileStream) {
          // Create buffered stream (for efficiency)
          nsCOMPtr<nsIInputStream> bufferedStream;
          rv = NS_NewBufferedInputStream(getter_AddRefs(bufferedStream),
                                         fileStream, 8192);
          NS_ENSURE_SUCCESS(rv, rv);

          // Submit
          aFormSubmission->AddNameFilePair(this, name, filename,
                                           bufferedStream, contentType,
                                           PR_FALSE);
          fileSent = PR_TRUE;
        }
      }

      if (!fileSent) {
        // If we don't submit as a file, at least submit the truncated filename.
        aFormSubmission->AddNameFilePair(this, name, filename,
                                         nsnull, NS_LITERAL_CSTRING("application/octet-stream"),
                                         PR_FALSE);
      }
    }

    if (files.Count() == 0) {
      // If no file was selected, pretend we had an empty file with an
      // empty filename.
      aFormSubmission->AddNameFilePair(this, name, EmptyString(), nsnull,
                                       NS_LITERAL_CSTRING("application/octet-stream"),
                                       PR_FALSE);

    }

    return NS_OK;
  }

  // Submit
  // (for type=image, only submit if value is non-null)
  if (mType != NS_FORM_INPUT_IMAGE || !value.IsEmpty()) {
    rv = aFormSubmission->AddNameValuePair(this, name, value);
  }

  return rv;
}


NS_IMETHODIMP
nsHTMLInputElement::SaveState()
{
  nsresult rv = NS_OK;

  nsRefPtr<nsHTMLInputElementState> inputState = nsnull;

  switch (mType) {
    case NS_FORM_INPUT_CHECKBOX:
    case NS_FORM_INPUT_RADIO:
      {
        PRBool checked = PR_FALSE;
        GetChecked(&checked);
        PRBool defaultChecked = PR_FALSE;
        GetDefaultChecked(&defaultChecked);
        // Only save if checked != defaultChecked (bug 62713)
        // (always save if it's a radio button so that the checked
        // state of all radio buttons is restored)
        if (mType == NS_FORM_INPUT_RADIO || checked != defaultChecked) {
          inputState = new nsHTMLInputElementState();
          if (!inputState) {
            return NS_ERROR_OUT_OF_MEMORY;
          }

          inputState->SetChecked(checked);
        }
        break;
      }

    // Never save passwords in session history
    case NS_FORM_INPUT_PASSWORD:
      break;
    case NS_FORM_INPUT_TEXT:
    case NS_FORM_INPUT_HIDDEN:
      {
        if (GET_BOOLBIT(mBitField, BF_VALUE_CHANGED)) {
          inputState = new nsHTMLInputElementState();
          if (!inputState) {
            return NS_ERROR_OUT_OF_MEMORY;
          }

          nsAutoString value;
          GetValue(value);
          rv = nsLinebreakConverter::ConvertStringLineBreaks(
                 value,
                 nsLinebreakConverter::eLinebreakPlatform,
                 nsLinebreakConverter::eLinebreakContent);
          NS_ASSERTION(NS_SUCCEEDED(rv), "Converting linebreaks failed!");
          inputState->SetValue(value);
       }
      break;
    }
    case NS_FORM_INPUT_FILE:
      {
        if (!mFileNames.IsEmpty()) {
          inputState = new nsHTMLInputElementState();
          if (!inputState) {
            return NS_ERROR_OUT_OF_MEMORY;
          }

          inputState->SetFilenames(mFileNames);
        }
        break;
      }
  }
  
  nsPresState* state = nsnull;
  if (inputState) {
    rv = GetPrimaryPresState(this, &state);
    if (state) {
      state->SetStateProperty(inputState);
    }
  }

  if (GET_BOOLBIT(mBitField, BF_DISABLED_CHANGED)) {
    rv |= GetPrimaryPresState(this, &state);
    if (state) {
      PRBool disabled;
      GetDisabled(&disabled);
      state->SetDisabled(disabled);
    }
  }

  return rv;
}

void
nsHTMLInputElement::DoneCreatingElement()
{
  SET_BOOLBIT(mBitField, BF_PARSER_CREATING, PR_FALSE);

  //
  // Restore state as needed.  Note that disabled state applies to all control
  // types.
  //
  PRBool restoredCheckedState = RestoreFormControlState(this, this);

  //
  // If restore does not occur, we initialize .checked using the CHECKED
  // property.
  //
  if (!restoredCheckedState &&
      GET_BOOLBIT(mBitField, BF_SHOULD_INIT_CHECKED)) {
    PRBool resetVal;
    GetDefaultChecked(&resetVal);
    DoSetChecked(resetVal, PR_FALSE);
    DoSetCheckedChanged(PR_FALSE, PR_FALSE);
  }

  SET_BOOLBIT(mBitField, BF_SHOULD_INIT_CHECKED, PR_FALSE);
}

PRInt32
nsHTMLInputElement::IntrinsicState() const
{
  // If you add states here, and they're type-dependent, you need to add them
  // to the type case in AfterSetAttr.
  
  PRInt32 state = nsGenericHTMLFormElement::IntrinsicState();
  if (mType == NS_FORM_INPUT_CHECKBOX || mType == NS_FORM_INPUT_RADIO) {
    // Check current checked state (:checked)
    if (GET_BOOLBIT(mBitField, BF_CHECKED)) {
      state |= NS_EVENT_STATE_CHECKED;
    }

    // Check current indeterminate state (:indeterminate)
    if (mType == NS_FORM_INPUT_CHECKBOX && GET_BOOLBIT(mBitField, BF_INDETERMINATE)) {
      state |= NS_EVENT_STATE_INDETERMINATE;
    }

    // Check whether we are the default checked element (:default)
    // The call is to an interface function, which makes it non-const, so we
    // use a nasty hack :(
    PRBool defaultState = PR_FALSE;
    const_cast<nsHTMLInputElement*>(this)->GetDefaultChecked(&defaultState);
    if (defaultState) {
      state |= NS_EVENT_STATE_DEFAULT;
    }
  } else if (mType == NS_FORM_INPUT_IMAGE) {
    state |= nsImageLoadingContent::ImageState();
  }

  return state;
}

PRBool
nsHTMLInputElement::RestoreState(nsPresState* aState)
{
  PRBool restoredCheckedState = PR_FALSE;

  nsCOMPtr<nsHTMLInputElementState> inputState
    (do_QueryInterface(aState->GetStateProperty()));

  if (inputState) {
    switch (mType) {
      case NS_FORM_INPUT_CHECKBOX:
      case NS_FORM_INPUT_RADIO:
        {
          if (inputState->IsCheckedSet()) {
            restoredCheckedState = PR_TRUE;
            DoSetChecked(inputState->GetChecked());
          }
          break;
        }

      case NS_FORM_INPUT_TEXT:
      case NS_FORM_INPUT_HIDDEN:
        {
          SetValueInternal(inputState->GetValue(), nsnull, PR_FALSE);
          break;
        }
      case NS_FORM_INPUT_FILE:
        {
          SetFileNames(inputState->GetFilenames());
          break;
        }
    }
  }

  if (aState->IsDisabledSet()) {
    SetDisabled(aState->GetDisabled());
  }

  return restoredCheckedState;
}

PRBool
nsHTMLInputElement::AllowDrop()
{
  // Allow drop on anything other than file inputs.

  return mType != NS_FORM_INPUT_FILE;
}

/*
 * Radio group stuff
 */

NS_IMETHODIMP
nsHTMLInputElement::AddedToRadioGroup(PRBool aNotify)
{
  // Make sure not to notify if we're still being created by the parser
  aNotify = aNotify && !GET_BOOLBIT(mBitField, BF_PARSER_CREATING);

  //
  //  If the input element is not in a form and
  //  not in a document, we just need to return.
  //
  if (!mForm && !(IsInDoc() && GetParent())) {
    return NS_OK;
  }

  //
  // If the input element is checked, and we add it to the group, it will
  // deselect whatever is currently selected in that group
  //
  PRBool checked;
  GetChecked(&checked);
  if (checked) {
    //
    // If it is checked, call "RadioSetChecked" to perform the selection/
    // deselection ritual.  This has the side effect of repainting the
    // radio button, but as adding a checked radio button into the group
    // should not be that common an occurrence, I think we can live with
    // that.
    //
    RadioSetChecked(aNotify);
  }

  //
  // For integrity purposes, we have to ensure that "checkedChanged" is
  // the same for this new element as for all the others in the group
  //
  PRBool checkedChanged = GET_BOOLBIT(mBitField, BF_CHECKED_CHANGED);
  nsCOMPtr<nsIRadioVisitor> visitor;
  nsresult rv = NS_GetRadioGetCheckedChangedVisitor(&checkedChanged, this,
                                           getter_AddRefs(visitor));
  NS_ENSURE_SUCCESS(rv, rv);
  
  VisitGroup(visitor, aNotify);
  SetCheckedChangedInternal(checkedChanged);
  
  //
  // Add the radio to the radio group container.
  //
  nsCOMPtr<nsIRadioGroupContainer> container = GetRadioGroupContainer();
  if (container) {
    nsAutoString name;
    if (GetNameIfExists(name)) {
      container->AddToRadioGroup(name, static_cast<nsIFormControl*>(this));
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLInputElement::WillRemoveFromRadioGroup()
{
  //
  // If the input element is not in a form and
  // not in a document, we just need to return.
  //
  if (!mForm && !(IsInDoc() && GetParent())) {
    return NS_OK;
  }

  //
  // If this button was checked, we need to notify the group that there is no
  // longer a selected radio button
  //
  PRBool checked = PR_FALSE;
  GetChecked(&checked);

  nsAutoString name;
  PRBool gotName = PR_FALSE;
  if (checked) {
    if (!gotName) {
      if (!GetNameIfExists(name)) {
        // If the name doesn't exist, nothing is going to happen anyway
        return NS_OK;
      }
      gotName = PR_TRUE;
    }

    nsCOMPtr<nsIRadioGroupContainer> container = GetRadioGroupContainer();
    if (container) {
      container->SetCurrentRadioButton(name, nsnull);
    }
  }
  
  //
  // Remove this radio from its group in the container
  //
  nsCOMPtr<nsIRadioGroupContainer> container = GetRadioGroupContainer();
  if (container) {
    if (!gotName) {
      if (!GetNameIfExists(name)) {
        // If the name doesn't exist, nothing is going to happen anyway
        return NS_OK;
      }
      gotName = PR_TRUE;
    }
    container->RemoveFromRadioGroup(name,
                                    static_cast<nsIFormControl*>(this));
  }

  return NS_OK;
}

PRBool
nsHTMLInputElement::IsHTMLFocusable(PRBool *aIsFocusable, PRInt32 *aTabIndex)
{
  if (nsGenericHTMLElement::IsHTMLFocusable(aIsFocusable, aTabIndex)) {
    return PR_TRUE;
  }

  if (HasAttr(kNameSpaceID_None, nsGkAtoms::disabled)) {
    *aIsFocusable = PR_FALSE;
    return PR_TRUE;
  }

  if (mType == NS_FORM_INPUT_TEXT || mType == NS_FORM_INPUT_PASSWORD) {
    *aIsFocusable = PR_TRUE;
    return PR_FALSE;
  }

  if (mType == NS_FORM_INPUT_FILE) {
    if (aTabIndex) {
      *aTabIndex = -1;
    }
    *aIsFocusable = PR_TRUE;
    return PR_TRUE;
  }

  if (mType == NS_FORM_INPUT_HIDDEN) {
    if (aTabIndex) {
      *aTabIndex = -1;
    }
    *aIsFocusable = PR_FALSE;
    return PR_FALSE;
  }

  if (!aTabIndex) {
    // The other controls are all focusable
    *aIsFocusable = PR_TRUE;
    return PR_FALSE;
  }

  // We need to set tabindex to -1 if we're not tabbable
  if (mType != NS_FORM_INPUT_TEXT && mType != NS_FORM_INPUT_PASSWORD &&
      !(sTabFocusModel & eTabFocus_formElementsMask)) {
    *aTabIndex = -1;
  }

  if (mType != NS_FORM_INPUT_RADIO) {
    *aIsFocusable = PR_TRUE;
    return PR_FALSE;
  }

  PRBool checked;
  GetChecked(&checked);
  if (checked) {
    // Selected radio buttons are tabbable
    *aIsFocusable = PR_TRUE;
    return PR_FALSE;
  }

  // Current radio button is not selected.
  // But make it tabbable if nothing in group is selected.
  nsCOMPtr<nsIRadioGroupContainer> container = GetRadioGroupContainer();
  nsAutoString name;
  if (!container || !GetNameIfExists(name)) {
    *aIsFocusable = PR_TRUE;
    return PR_FALSE;
  }

  nsCOMPtr<nsIDOMHTMLInputElement> currentRadio;
  container->GetCurrentRadioButton(name, getter_AddRefs(currentRadio));
  if (currentRadio) {
    *aTabIndex = -1;
  }
  *aIsFocusable = PR_TRUE;
  return PR_FALSE;
}

nsresult
nsHTMLInputElement::VisitGroup(nsIRadioVisitor* aVisitor, PRBool aFlushContent)
{
  nsresult rv = NS_OK;
  nsCOMPtr<nsIRadioGroupContainer> container = GetRadioGroupContainer();
  if (container) {
    nsAutoString name;
    if (GetNameIfExists(name)) {
      rv = container->WalkRadioGroup(name, aVisitor, aFlushContent);
    } else {
      PRBool stop;
      aVisitor->Visit(this, &stop);
    }
  } else {
    PRBool stop;
    aVisitor->Visit(this, &stop);
  }
  return rv;
}


//
// Visitor classes
//
//
// CLASS nsRadioVisitor
//
// (this is the superclass of the others)
//
class nsRadioVisitor : public nsIRadioVisitor {
public:
  nsRadioVisitor() { }
  virtual ~nsRadioVisitor() { }

  NS_DECL_ISUPPORTS

  NS_IMETHOD Visit(nsIFormControl* aRadio, PRBool* aStop) = 0;
};

NS_IMPL_ISUPPORTS1(nsRadioVisitor, nsIRadioVisitor)


//
// CLASS nsRadioSetCheckedChangedVisitor
//
class nsRadioSetCheckedChangedVisitor : public nsRadioVisitor {
public:
  nsRadioSetCheckedChangedVisitor(PRBool aCheckedChanged) :
    nsRadioVisitor(), mCheckedChanged(aCheckedChanged)
    { }

  virtual ~nsRadioSetCheckedChangedVisitor() { }

  NS_IMETHOD Visit(nsIFormControl* aRadio, PRBool* aStop)
  {
    nsCOMPtr<nsIRadioControlElement> radio(do_QueryInterface(aRadio));
    NS_ASSERTION(radio, "Visit() passed a null button (or non-radio)!");
    radio->SetCheckedChangedInternal(mCheckedChanged);
    return NS_OK;
  }

protected:
  PRPackedBool mCheckedChanged;
};

//
// CLASS nsRadioGetCheckedChangedVisitor
//
class nsRadioGetCheckedChangedVisitor : public nsRadioVisitor {
public:
  nsRadioGetCheckedChangedVisitor(PRBool* aCheckedChanged,
                                  nsIFormControl* aExcludeElement) :
    nsRadioVisitor(),
    mCheckedChanged(aCheckedChanged),
    mExcludeElement(aExcludeElement)
    { }

  virtual ~nsRadioGetCheckedChangedVisitor() { }

  NS_IMETHOD Visit(nsIFormControl* aRadio, PRBool* aStop)
  {
    if (aRadio == mExcludeElement) {
      return NS_OK;
    }
    nsCOMPtr<nsIRadioControlElement> radio(do_QueryInterface(aRadio));
    NS_ASSERTION(radio, "Visit() passed a null button (or non-radio)!");
    radio->GetCheckedChanged(mCheckedChanged);
    *aStop = PR_TRUE;
    return NS_OK;
  }

protected:
  PRBool* mCheckedChanged;
  nsIFormControl* mExcludeElement;
};

nsresult
NS_GetRadioSetCheckedChangedVisitor(PRBool aCheckedChanged,
                                    nsIRadioVisitor** aVisitor)
{
  //
  // These are static so that we don't have to keep creating new visitors for
  // such an ordinary process all the time.  There are only two possibilities
  // for this visitor: set to true, and set to false.
  //
  static nsIRadioVisitor* sVisitorTrue = nsnull;
  static nsIRadioVisitor* sVisitorFalse = nsnull;

  //
  // Get the visitor that sets them to true
  //
  if (aCheckedChanged) {
    if (!sVisitorTrue) {
      sVisitorTrue = new nsRadioSetCheckedChangedVisitor(PR_TRUE);
      if (!sVisitorTrue) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
      NS_ADDREF(sVisitorTrue);
      nsresult rv =
        nsContentUtils::ReleasePtrOnShutdown((nsISupports**)&sVisitorTrue);
      if (NS_FAILED(rv)) {
        NS_RELEASE(sVisitorTrue);
        return rv;
      }
    }
    *aVisitor = sVisitorTrue;
  }
  //
  // Get the visitor that sets them to false
  //
  else {
    if (!sVisitorFalse) {
      sVisitorFalse = new nsRadioSetCheckedChangedVisitor(PR_FALSE);
      if (!sVisitorFalse) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
      NS_ADDREF(sVisitorFalse);
      nsresult rv =
        nsContentUtils::ReleasePtrOnShutdown((nsISupports**)&sVisitorFalse);
      if (NS_FAILED(rv)) {
        NS_RELEASE(sVisitorFalse);
        return rv;
      }
    }
    *aVisitor = sVisitorFalse;
  }

  NS_ADDREF(*aVisitor);
  return NS_OK;
}

nsresult
NS_GetRadioGetCheckedChangedVisitor(PRBool* aCheckedChanged,
                                    nsIFormControl* aExcludeElement,
                                    nsIRadioVisitor** aVisitor)
{
  *aVisitor = new nsRadioGetCheckedChangedVisitor(aCheckedChanged,
                                                  aExcludeElement);
  if (!*aVisitor) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  NS_ADDREF(*aVisitor);

  return NS_OK;
}

