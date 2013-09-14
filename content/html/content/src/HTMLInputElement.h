/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_HTMLInputElement_h
#define mozilla_dom_HTMLInputElement_h

#include "mozilla/Attributes.h"
#include "nsGenericHTMLElement.h"
#include "nsImageLoadingContent.h"
#include "nsIDOMHTMLInputElement.h"
#include "nsITextControlElement.h"
#include "nsIPhonetic.h"
#include "nsIDOMNSEditableElement.h"
#include "nsTextEditorState.h"
#include "nsCOMPtr.h"
#include "nsIConstraintValidation.h"
#include "nsDOMFile.h"
#include "mozilla/dom/HTMLFormElement.h" // for ShouldShowInvalidUI()
#include "nsIFile.h"
#include "nsIFilePicker.h"
#include "nsIContentPrefService2.h"
#include "mozilla/Decimal.h"

class nsDOMFileList;
class nsIFilePicker;
class nsIRadioGroupContainer;
class nsIRadioGroupVisitor;
class nsIRadioVisitor;

namespace mozilla {
namespace dom {

class UploadLastDir MOZ_FINAL : public nsIObserver, public nsSupportsWeakReference {
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  /**
   * Fetch the last used directory for this location from the content
   * pref service, and display the file picker opened in that directory.
   *
   * @param aDoc          current document
   * @param aFilePicker   the file picker to open
   * @param aFpCallback   the callback object to be run when the file is shown.
   */
  nsresult FetchDirectoryAndDisplayPicker(nsIDocument* aDoc,
                                          nsIFilePicker* aFilePicker,
                                          nsIFilePickerShownCallback* aFpCallback);

  /**
   * Store the last used directory for this location using the
   * content pref service, if it is available
   * @param aURI URI of the current page
   * @param aDomFile file chosen by the user - the path to the parent of this
   *        file will be stored
   */
  nsresult StoreLastUsedDirectory(nsIDocument* aDoc, nsIDOMFile* aDomFile);

  class ContentPrefCallback MOZ_FINAL : public nsIContentPrefCallback2
  {
    public:
    ContentPrefCallback(nsIFilePicker* aFilePicker, nsIFilePickerShownCallback* aFpCallback)
    : mFilePicker(aFilePicker)
    , mFpCallback(aFpCallback)
    { }

    virtual ~ContentPrefCallback()
    { }

    NS_DECL_ISUPPORTS
    NS_DECL_NSICONTENTPREFCALLBACK2

    nsCOMPtr<nsIFilePicker> mFilePicker;
    nsCOMPtr<nsIFilePickerShownCallback> mFpCallback;
    nsCOMPtr<nsIContentPref> mResult;
  };
};

class HTMLInputElement : public nsGenericHTMLFormElement,
                         public nsImageLoadingContent,
                         public nsIDOMHTMLInputElement,
                         public nsITextControlElement,
                         public nsIPhonetic,
                         public nsIDOMNSEditableElement,
                         public nsIConstraintValidation
{
public:
  using nsIConstraintValidation::GetValidationMessage;
  using nsIConstraintValidation::CheckValidity;
  using nsIConstraintValidation::WillValidate;
  using nsIConstraintValidation::Validity;
  using nsGenericHTMLFormElement::GetForm;

  HTMLInputElement(already_AddRefed<nsINodeInfo> aNodeInfo,
                   mozilla::dom::FromParser aFromParser);
  virtual ~HTMLInputElement();

  NS_IMPL_FROMCONTENT_HTML_WITH_TAG(HTMLInputElement, input)

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  // nsIDOMNode
  NS_FORWARD_NSIDOMNODE_TO_NSINODE

  // nsIDOMElement
  NS_FORWARD_NSIDOMELEMENT_TO_GENERIC

  // nsIDOMHTMLElement
  NS_FORWARD_NSIDOMHTMLELEMENT_TO_GENERIC
  virtual int32_t TabIndexDefault() MOZ_OVERRIDE;
  virtual void Focus(ErrorResult& aError) MOZ_OVERRIDE;

  // nsIDOMHTMLInputElement
  NS_DECL_NSIDOMHTMLINPUTELEMENT

  // nsIPhonetic
  NS_DECL_NSIPHONETIC

  // nsIDOMNSEditableElement
  NS_IMETHOD GetEditor(nsIEditor** aEditor) MOZ_OVERRIDE
  {
    return nsGenericHTMLElement::GetEditor(aEditor);
  }

  NS_IMETHOD SetUserInput(const nsAString& aInput) MOZ_OVERRIDE;

  // Overriden nsIFormControl methods
  NS_IMETHOD_(uint32_t) GetType() const MOZ_OVERRIDE { return mType; }
  NS_IMETHOD Reset() MOZ_OVERRIDE;
  NS_IMETHOD SubmitNamesValues(nsFormSubmission* aFormSubmission) MOZ_OVERRIDE;
  NS_IMETHOD SaveState() MOZ_OVERRIDE;
  virtual bool RestoreState(nsPresState* aState) MOZ_OVERRIDE;
  virtual bool AllowDrop() MOZ_OVERRIDE;
  virtual bool IsDisabledForEvents(uint32_t aMessage) MOZ_OVERRIDE;

  virtual void FieldSetDisabledChanged(bool aNotify) MOZ_OVERRIDE;

  // nsIContent
  virtual bool IsHTMLFocusable(bool aWithMouse, bool *aIsFocusable, int32_t *aTabIndex) MOZ_OVERRIDE;

  virtual bool ParseAttribute(int32_t aNamespaceID,
                                nsIAtom* aAttribute,
                                const nsAString& aValue,
                                nsAttrValue& aResult) MOZ_OVERRIDE;
  virtual nsChangeHint GetAttributeChangeHint(const nsIAtom* aAttribute,
                                              int32_t aModType) const MOZ_OVERRIDE;
  NS_IMETHOD_(bool) IsAttributeMapped(const nsIAtom* aAttribute) const MOZ_OVERRIDE;
  virtual nsMapRuleToAttributesFunc GetAttributeMappingFunction() const MOZ_OVERRIDE;

  virtual nsresult PreHandleEvent(nsEventChainPreVisitor& aVisitor) MOZ_OVERRIDE;
  virtual nsresult PostHandleEvent(nsEventChainPostVisitor& aVisitor) MOZ_OVERRIDE;
  void PostHandleEventForRangeThumb(nsEventChainPostVisitor& aVisitor);
  void StartRangeThumbDrag(nsGUIEvent* aEvent);
  void FinishRangeThumbDrag(nsGUIEvent* aEvent = nullptr);
  void CancelRangeThumbDrag(bool aIsForUserEvent = true);
  void SetValueOfRangeForUserEvent(Decimal aValue);

  virtual nsresult BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                              nsIContent* aBindingParent,
                              bool aCompileEventHandlers) MOZ_OVERRIDE;
  virtual void UnbindFromTree(bool aDeep = true,
                              bool aNullParent = true) MOZ_OVERRIDE;

  virtual void DoneCreatingElement() MOZ_OVERRIDE;

  virtual nsEventStates IntrinsicState() const MOZ_OVERRIDE;

  // nsITextControlElement
  NS_IMETHOD SetValueChanged(bool aValueChanged) MOZ_OVERRIDE;
  NS_IMETHOD_(bool) IsSingleLineTextControl() const MOZ_OVERRIDE;
  NS_IMETHOD_(bool) IsTextArea() const MOZ_OVERRIDE;
  NS_IMETHOD_(bool) IsPlainTextControl() const MOZ_OVERRIDE;
  NS_IMETHOD_(bool) IsPasswordTextControl() const MOZ_OVERRIDE;
  NS_IMETHOD_(int32_t) GetCols() MOZ_OVERRIDE;
  NS_IMETHOD_(int32_t) GetWrapCols() MOZ_OVERRIDE;
  NS_IMETHOD_(int32_t) GetRows() MOZ_OVERRIDE;
  NS_IMETHOD_(void) GetDefaultValueFromContent(nsAString& aValue) MOZ_OVERRIDE;
  NS_IMETHOD_(bool) ValueChanged() const MOZ_OVERRIDE;
  NS_IMETHOD_(void) GetTextEditorValue(nsAString& aValue, bool aIgnoreWrap) const MOZ_OVERRIDE;
  NS_IMETHOD_(nsIEditor*) GetTextEditor() MOZ_OVERRIDE;
  NS_IMETHOD_(nsISelectionController*) GetSelectionController() MOZ_OVERRIDE;
  NS_IMETHOD_(nsFrameSelection*) GetConstFrameSelection() MOZ_OVERRIDE;
  NS_IMETHOD BindToFrame(nsTextControlFrame* aFrame) MOZ_OVERRIDE;
  NS_IMETHOD_(void) UnbindFromFrame(nsTextControlFrame* aFrame) MOZ_OVERRIDE;
  NS_IMETHOD CreateEditor() MOZ_OVERRIDE;
  NS_IMETHOD_(nsIContent*) GetRootEditorNode() MOZ_OVERRIDE;
  NS_IMETHOD_(nsIContent*) CreatePlaceholderNode() MOZ_OVERRIDE;
  NS_IMETHOD_(nsIContent*) GetPlaceholderNode() MOZ_OVERRIDE;
  NS_IMETHOD_(void) UpdatePlaceholderVisibility(bool aNotify) MOZ_OVERRIDE;
  NS_IMETHOD_(bool) GetPlaceholderVisibility() MOZ_OVERRIDE;
  NS_IMETHOD_(void) InitializeKeyboardEventListeners() MOZ_OVERRIDE;
  NS_IMETHOD_(void) OnValueChanged(bool aNotify) MOZ_OVERRIDE;
  NS_IMETHOD_(bool) HasCachedSelection() MOZ_OVERRIDE;

  void GetDisplayFileName(nsAString& aFileName) const;

  const nsCOMArray<nsIDOMFile>& GetFilesInternal() const
  {
    return mFiles;
  }

  void SetFiles(const nsCOMArray<nsIDOMFile>& aFiles, bool aSetValueChanged);
  void SetFiles(nsIDOMFileList* aFiles, bool aSetValueChanged);

  void SetCheckedChangedInternal(bool aCheckedChanged);
  bool GetCheckedChanged() const {
    return mCheckedChanged;
  }
  void AddedToRadioGroup();
  void WillRemoveFromRadioGroup();

 /**
   * Helper function returning the currently selected button in the radio group.
   * Returning null if the element is not a button or if there is no selectied
   * button in the group.
   *
   * @return the selected button (or null).
   */
  already_AddRefed<nsIDOMHTMLInputElement> GetSelectedRadioButton();

  virtual nsresult Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const MOZ_OVERRIDE;

  void MaybeFireAsyncClickHandler(nsEventChainPostVisitor& aVisitor);
  NS_IMETHOD FireAsyncClickHandler();

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(HTMLInputElement,
                                           nsGenericHTMLFormElement)

  static UploadLastDir* gUploadLastDir;
  // create and destroy the static UploadLastDir object for remembering
  // which directory was last used on a site-by-site basis
  static void InitUploadLastDir();
  static void DestroyUploadLastDir();

  void MaybeLoadImage();

  virtual nsIDOMNode* AsDOMNode() MOZ_OVERRIDE { return this; }

  // nsIConstraintValidation
  bool     IsTooLong();
  bool     IsValueMissing() const;
  bool     HasTypeMismatch() const;
  bool     HasPatternMismatch() const;
  bool     IsRangeOverflow() const;
  bool     IsRangeUnderflow() const;
  bool     HasStepMismatch() const;
  void     UpdateTooLongValidityState();
  void     UpdateValueMissingValidityState();
  void     UpdateTypeMismatchValidityState();
  void     UpdatePatternMismatchValidityState();
  void     UpdateRangeOverflowValidityState();
  void     UpdateRangeUnderflowValidityState();
  void     UpdateStepMismatchValidityState();
  void     UpdateAllValidityStates(bool aNotify);
  void     UpdateBarredFromConstraintValidation();
  nsresult GetValidationMessage(nsAString& aValidationMessage,
                                ValidityStateType aType) MOZ_OVERRIDE;
  /**
   * Update the value missing validity state for radio elements when they have
   * a group.
   *
   * @param aIgnoreSelf Whether the required attribute and the checked state
   * of the current radio should be ignored.
   * @note This method shouldn't be called if the radio element hasn't a group.
   */
  void     UpdateValueMissingValidityStateForRadio(bool aIgnoreSelf);

  /**
   * Set filters to the filePicker according to the accept attribute value.
   *
   * See:
   * http://dev.w3.org/html5/spec/forms.html#attr-input-accept
   *
   * @note You should not call this function if the element has no @accept.
   * @note "All Files" filter is always set, no matter if there is a valid
   * filter specifed or not.
   * @note If there is only one valid filter that is audio or video or image,
   * it will be selected as the default filter. Otherwise "All files" remains
   * the default filter.
   * @note If more than one valid filter is found, the "All Supported Types"
   * filter is added, which is the concatenation of all valid filters.
   */
  void SetFilePickerFiltersFromAccept(nsIFilePicker* filePicker);

  /**
   * Returns the filter which should be used for the file picker according to
   * the accept attribute value.
   *
   * See:
   * http://dev.w3.org/html5/spec/forms.html#attr-input-accept
   *
   * @return Filter to use on the file picker with AppendFilters, 0 if none.
   *
   * @note You should not call this function if the element has no @accept.
   * @note This will only filter for one type of file. If more than one filter
   * is specified by the accept attribute they will *all* be ignored.
   */
  int32_t GetFilterFromAccept();

  /**
   * The form might need to request an update of the UI bits
   * (BF_CAN_SHOW_INVALID_UI and BF_CAN_SHOW_VALID_UI) when an invalid form
   * submission is tried.
   *
   * @param aIsFocused Whether the element is currently focused.
   *
   * @note The caller is responsible to call ContentStatesChanged.
   */
  void UpdateValidityUIBits(bool aIsFocused);

  /**
   * Fires change event if mFocusedValue and current value held are unequal.
   */
  void FireChangeEventIfNeeded();

  /**
   * Returns the input element's value as a Decimal.
   * Returns NaN if the current element's value is not a floating point number.
   *
   * @return the input element's value as a Decimal.
   */
  Decimal GetValueAsDecimal() const;

  /**
   * Returns the input's "minimum" (as defined by the HTML5 spec) as a double.
   * Note this takes account of any default minimum that the type may have.
   * Returns NaN if the min attribute isn't a valid floating point number and
   * the input's type does not have a default minimum.
   *
   * NOTE: Only call this if you know DoesMinMaxApply() returns true.
   */
  Decimal GetMinimum() const;

  /**
   * Returns the input's "maximum" (as defined by the HTML5 spec) as a double.
   * Note this takes account of any default maximum that the type may have.
   * Returns NaN if the max attribute isn't a valid floating point number and
   * the input's type does not have a default maximum.
   *
   * NOTE:Only call this if you know DoesMinMaxApply() returns true.
   */
  Decimal GetMaximum() const;

  // WebIDL

  // XPCOM GetAccept() is OK
  void SetAccept(const nsAString& aValue, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::accept, aValue, aRv);
  }

  // XPCOM GetAlt() is OK
  void SetAlt(const nsAString& aValue, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::alt, aValue, aRv);
  }

  // XPCOM GetAutocomplete() is OK
  void SetAutocomplete(const nsAString& aValue, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::autocomplete, aValue, aRv);
  }

  bool Autofocus() const
  {
    return GetBoolAttr(nsGkAtoms::autofocus);
  }

  void SetAutofocus(bool aValue, ErrorResult& aRv)
  {
    SetHTMLBoolAttr(nsGkAtoms::autofocus, aValue, aRv);
  }

  bool DefaultChecked() const
  {
    return HasAttr(kNameSpaceID_None, nsGkAtoms::checked);
  }

  void SetDefaultChecked(bool aValue, ErrorResult& aRv)
  {
    SetHTMLBoolAttr(nsGkAtoms::checked, aValue, aRv);
  }

  bool Checked() const
  {
    return mChecked;
  }
  // XPCOM SetChecked() is OK

  bool Disabled() const
  {
    return GetBoolAttr(nsGkAtoms::disabled);
  }

  void SetDisabled(bool aValue,ErrorResult& aRv)
  {
    SetHTMLBoolAttr(nsGkAtoms::disabled, aValue, aRv);
  }

  // XPCOM GetForm() is OK

  nsDOMFileList* GetFiles();

  // XPCOM GetFormAction() is OK
  void SetFormAction(const nsAString& aValue, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::formaction, aValue, aRv);
  }

  // XPCOM GetFormEnctype() is OK
  void SetFormEnctype(const nsAString& aValue, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::formenctype, aValue, aRv);
  }

  // XPCOM GetFormMethod() is OK
  void SetFormMethod(const nsAString& aValue, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::formmethod, aValue, aRv);
  }

  bool FormNoValidate() const
  {
    return GetBoolAttr(nsGkAtoms::formnovalidate);
  }

  void SetFormNoValidate(bool aValue, ErrorResult& aRv)
  {
    SetHTMLBoolAttr(nsGkAtoms::formnovalidate, aValue, aRv);
  }

  // XPCOM GetFormTarget() is OK
  void SetFormTarget(const nsAString& aValue, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::formtarget, aValue, aRv);
  }

  uint32_t Height();

  void SetHeight(uint32_t aValue, ErrorResult& aRv)
  {
    SetUnsignedIntAttr(nsGkAtoms::height, aValue, aRv);
  }

  bool Indeterminate() const
  {
    return mIndeterminate;
  }
  // XPCOM SetIndeterminate() is OK

  // XPCOM GetInputMode() is OK
  void SetInputMode(const nsAString& aValue, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::inputmode, aValue, aRv);
  }

  nsGenericHTMLElement* GetList() const;

  // XPCOM GetMax() is OK
  void SetMax(const nsAString& aValue, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::max, aValue, aRv);
  }

  int32_t MaxLength() const
  {
    return GetIntAttr(nsGkAtoms::maxlength, -1);
  }

  void SetMaxLength(int32_t aValue, ErrorResult& aRv)
  {
    if (aValue < 0) {
      aRv.Throw(NS_ERROR_DOM_INDEX_SIZE_ERR);
      return;
    }

    SetHTMLIntAttr(nsGkAtoms::maxlength, aValue, aRv);
  }

  // XPCOM GetMin() is OK
  void SetMin(const nsAString& aValue, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::min, aValue, aRv);
  }

  bool Multiple() const
  {
    return GetBoolAttr(nsGkAtoms::multiple);
  }

  void SetMultiple(bool aValue, ErrorResult& aRv)
  {
    SetHTMLBoolAttr(nsGkAtoms::multiple, aValue, aRv);
  }

  // XPCOM GetName() is OK
  void SetName(const nsAString& aValue, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::name, aValue, aRv);
  }

  // XPCOM GetPattern() is OK
  void SetPattern(const nsAString& aValue, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::pattern, aValue, aRv);
  }

  // XPCOM GetPlaceholder() is OK
  void SetPlaceholder(const nsAString& aValue, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::placeholder, aValue, aRv);
  }

  bool ReadOnly() const
  {
    return GetBoolAttr(nsGkAtoms::readonly);
  }

  void SetReadOnly(bool aValue, ErrorResult& aRv)
  {
    SetHTMLBoolAttr(nsGkAtoms::readonly, aValue, aRv);
  }

  bool Required() const
  {
    return GetBoolAttr(nsGkAtoms::required);
  }

  void SetRequired(bool aValue, ErrorResult& aRv)
  {
    SetHTMLBoolAttr(nsGkAtoms::required, aValue, aRv);
  }

  uint32_t Size() const
  {
    return GetUnsignedIntAttr(nsGkAtoms::size, DEFAULT_COLS);
  }

  void SetSize(uint32_t aValue, ErrorResult& aRv)
  {
    if (aValue == 0) {
      aRv.Throw(NS_ERROR_DOM_INDEX_SIZE_ERR);
      return;
    }

    SetUnsignedIntAttr(nsGkAtoms::size, aValue, aRv);
  }

  // XPCOM GetSrc() is OK
  void SetSrc(const nsAString& aValue, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::src, aValue, aRv);
  }

  // XPCOM GetStep() is OK
  void SetStep(const nsAString& aValue, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::step, aValue, aRv);
  }

  // XPCOM GetType() is OK
  void SetType(const nsAString& aValue, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::type, aValue, aRv);
  }

  // XPCOM GetDefaultValue() is OK
  void SetDefaultValue(const nsAString& aValue, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::value, aValue, aRv);
  }

  // XPCOM GetValue() is OK
  void SetValue(const nsAString& aValue, ErrorResult& aRv);

  Nullable<Date> GetValueAsDate(ErrorResult& aRv);

  void SetValueAsDate(Nullable<Date>, ErrorResult& aRv);

  double ValueAsNumber() const
  {
    return DoesValueAsNumberApply() ? GetValueAsDecimal().toDouble()
                                    : UnspecifiedNaN();
  }

  void SetValueAsNumber(double aValue, ErrorResult& aRv);

  uint32_t Width();

  void SetWidth(uint32_t aValue, ErrorResult& aRv)
  {
    SetUnsignedIntAttr(nsGkAtoms::width, aValue, aRv);
  }

  void StepUp(int32_t aN, ErrorResult& aRv)
  {
    aRv = ApplyStep(aN);
  }

  void StepDown(int32_t aN, ErrorResult& aRv)
  {
    aRv = ApplyStep(-aN);
  }

  /**
   * Returns the current step value.
   * Returns kStepAny if the current step is "any" string.
   *
   * @return the current step value.
   */
  Decimal GetStep() const;

  void GetValidationMessage(nsAString& aValidationMessage, ErrorResult& aRv);

  // XPCOM GetCustomVisibility() is OK

  // XPCOM Select() is OK

  int32_t GetSelectionStart(ErrorResult& aRv);
  void SetSelectionStart(int32_t aValue, ErrorResult& aRv);

  int32_t GetSelectionEnd(ErrorResult& aRv);
  void SetSelectionEnd(int32_t aValue, ErrorResult& aRv);

  void GetSelectionDirection(nsAString& aValue, ErrorResult& aRv);
  void SetSelectionDirection(const nsAString& aValue, ErrorResult& aRv);

  void SetSelectionRange(int32_t aStart, int32_t aEnd,
                         const Optional< nsAString >& direction,
                         ErrorResult& aRv);

  // XPCOM GetAlign() is OK
  void SetAlign(const nsAString& aValue, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::align, aValue, aRv);
  }

  // XPCOM GetUseMap() is OK
  void SetUseMap(const nsAString& aValue, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::usemap, aValue, aRv);
  }

  nsIControllers* GetControllers(ErrorResult& aRv);

  int32_t GetTextLength(ErrorResult& aRv);

  void MozGetFileNameArray(nsTArray< nsString >& aFileNames);

  void MozSetFileNameArray(const Sequence< nsString >& aFileNames);

  bool MozIsTextField(bool aExcludePassword);

  nsIEditor* GetEditor();

  // XPCOM SetUserInput() is OK

  // XPCOM GetPhonetic() is OK

protected:
  virtual JSObject* WrapNode(JSContext* aCx,
                             JS::Handle<JSObject*> aScope) MOZ_OVERRIDE;

  // Pull IsSingleLineTextControl into our scope, otherwise it'd be hidden
  // by the nsITextControlElement version.
  using nsGenericHTMLFormElement::IsSingleLineTextControl;

  /**
   * The ValueModeType specifies how the value IDL attribute should behave.
   *
   * See: http://dev.w3.org/html5/spec/forms.html#dom-input-value
   */
  enum ValueModeType
  {
    // On getting, returns the value.
    // On setting, sets value.
    VALUE_MODE_VALUE,
    // On getting, returns the value if present or the empty string.
    // On setting, sets the value.
    VALUE_MODE_DEFAULT,
    // On getting, returns the value if present or "on".
    // On setting, sets the value.
    VALUE_MODE_DEFAULT_ON,
    // On getting, returns "C:\fakepath\" followed by the file name of the
    // first file of the selected files if any.
    // On setting the empty string, empties the selected files list, otherwise
    // throw the INVALID_STATE_ERR exception.
    VALUE_MODE_FILENAME
  };

  /**
   * This helper method returns true if aValue is a valid email address.
   * This is following the HTML5 specification:
   * http://dev.w3.org/html5/spec/forms.html#valid-e-mail-address
   *
   * @param aValue  the email address to check.
   * @result        whether the given string is a valid email address.
   */
  static bool IsValidEmailAddress(const nsAString& aValue);

  /**
   * This helper method returns true if aValue is a valid email address list.
   * Email address list is a list of email address separated by comas (,) which
   * can be surrounded by space charecters.
   * This is following the HTML5 specification:
   * http://dev.w3.org/html5/spec/forms.html#valid-e-mail-address-list
   *
   * @param aValue  the email address list to check.
   * @result        whether the given string is a valid email address list.
   */
  static bool IsValidEmailAddressList(const nsAString& aValue);

  /**
   * This helper method convert a sub-string that contains only digits to a
   * number (unsigned int given that it can't contain a minus sign).
   * This method will return whether the sub-string is correctly formatted
   * (ie. contains only digit) and it can be successfuly parsed to generate a
   * number).
   * If the method returns true, |aResult| will contained the parsed number.
   *
   * @param aValue  the string on which the sub-string will be extracted and parsed.
   * @param aStart  the beginning of the sub-string in aValue.
   * @param aLen    the length of the sub-string.
   * @param aResult the parsed number.
   * @return whether the sub-string has been parsed successfully.
   */
  static bool DigitSubStringToNumber(const nsAString& aValue, uint32_t aStart,
                                     uint32_t aLen, uint32_t* aResult);

  // Helper method
  nsresult SetValueInternal(const nsAString& aValue,
                            bool aUserInput,
                            bool aSetValueChanged);

  nsresult GetValueInternal(nsAString& aValue) const;

  /**
   * Returns whether the current value is the empty string.
   *
   * @return whether the current value is the empty string.
   */
  bool IsValueEmpty() const;

  void ClearFiles(bool aSetValueChanged) {
    nsCOMArray<nsIDOMFile> files;
    SetFiles(files, aSetValueChanged);
  }

  void SetIndeterminateInternal(bool aValue,
                                bool aShouldInvalidate);

  nsresult GetSelectionRange(int32_t* aSelectionStart, int32_t* aSelectionEnd);

  /**
   * Called when an attribute is about to be changed
   */
  virtual nsresult BeforeSetAttr(int32_t aNameSpaceID, nsIAtom* aName,
                                 const nsAttrValueOrString* aValue,
                                 bool aNotify) MOZ_OVERRIDE;
  /**
   * Called when an attribute has just been changed
   */
  virtual nsresult AfterSetAttr(int32_t aNameSpaceID, nsIAtom* aName,
                                const nsAttrValue* aValue, bool aNotify) MOZ_OVERRIDE;

  /**
   * Dispatch a select event. Returns true if the event was not cancelled.
   */
  bool DispatchSelectEvent(nsPresContext* aPresContext);

  void SelectAll(nsPresContext* aPresContext);
  bool IsImage() const
  {
    return AttrValueIs(kNameSpaceID_None, nsGkAtoms::type,
                       nsGkAtoms::image, eIgnoreCase);
  }

  /**
   * Visit the group of radio buttons this radio belongs to
   * @param aVisitor the visitor to visit with
   */
  nsresult VisitGroup(nsIRadioVisitor* aVisitor, bool aFlushContent);

  /**
   * Do all the work that |SetChecked| does (radio button handling, etc.), but
   * take an |aNotify| parameter.
   */
  void DoSetChecked(bool aValue, bool aNotify, bool aSetValueChanged);

  /**
   * Do all the work that |SetCheckedChanged| does (radio button handling,
   * etc.), but take an |aNotify| parameter that lets it avoid flushing content
   * when it can.
   */
  void DoSetCheckedChanged(bool aCheckedChanged, bool aNotify);

  /**
   * Actually set checked and notify the frame of the change.
   * @param aValue the value of checked to set
   */
  void SetCheckedInternal(bool aValue, bool aNotify);

  void RadioSetChecked(bool aNotify);
  void SetCheckedChanged(bool aCheckedChanged);

  /**
   * MaybeSubmitForm looks for a submit input or a single text control
   * and submits the form if either is present.
   */
  nsresult MaybeSubmitForm(nsPresContext* aPresContext);

  /**
   * Update mFileList with the currently selected file.
   */
  nsresult UpdateFileList();

  /**
   * Called after calling one of the SetFiles() functions.
   */
  void AfterSetFiles(bool aSetValueChanged);

  /**
   * Determine whether the editor needs to be initialized explicitly for
   * a particular event.
   */
  bool NeedToInitializeEditorForEvent(nsEventChainPreVisitor& aVisitor) const;

  /**
   * Get the value mode of the element, depending of the type.
   */
  ValueModeType GetValueMode() const;

  /**
   * Get the mutable state of the element.
   * When the element isn't mutable (immutable), the value or checkedness
   * should not be changed by the user.
   *
   * See: http://dev.w3.org/html5/spec/forms.html#concept-input-mutable
   */
  bool IsMutable() const;

  /**
   * Returns if the readonly attribute applies for the current type.
   */
  bool DoesReadOnlyApply() const;

  /**
   * Returns if the required attribute applies for the current type.
   */
  bool DoesRequiredApply() const;

  /**
   * Returns if the pattern attribute applies for the current type.
   */
  bool DoesPatternApply() const;

  /**
   * Returns if the min and max attributes apply for the current type.
   */
  bool DoesMinMaxApply() const;

  /**
   * Returns if the step attribute apply for the current type.
   */
  bool DoesStepApply() const { return DoesMinMaxApply(); }

  /**
   * Returns if stepDown and stepUp methods apply for the current type.
   */
  bool DoStepDownStepUpApply() const { return DoesStepApply(); }

  /**
   * Returns if valueAsNumber attribute applies for the current type.
   */
  bool DoesValueAsNumberApply() const { return DoesMinMaxApply(); }

  /**
   * Returns if the maxlength attribute applies for the current type.
   */
  bool MaxLengthApplies() const { return IsSingleLineTextControl(false, mType); }

  void FreeData();
  nsTextEditorState *GetEditorState() const;

  /**
   * Manages the internal data storage across type changes.
   */
  void HandleTypeChange(uint8_t aNewType);

  /**
   * Sanitize the value of the element depending of its current type.
   * See: http://www.whatwg.org/specs/web-apps/current-work/#value-sanitization-algorithm
   */
  void SanitizeValue(nsAString& aValue);

  /**
   * Returns whether the placeholder attribute applies for the current type.
   */
  bool PlaceholderApplies() const;

  /**
   * Set the current default value to the value of the input element.
   * @note You should not call this method if GetValueMode() doesn't return
   * VALUE_MODE_VALUE.
   */
  nsresult SetDefaultValueAsValue();

  virtual void SetDirectionIfAuto(bool aAuto, bool aNotify);

  /**
   * Return if an element should have a specific validity UI
   * (with :-moz-ui-invalid and :-moz-ui-valid pseudo-classes).
   *
   * @return Whether the element should have a validity UI.
   */
  bool ShouldShowValidityUI() const {
    /**
     * Always show the validity UI if the form has already tried to be submitted
     * but was invalid.
     *
     * Otherwise, show the validity UI if the element's value has been changed.
     */
    if (mForm && mForm->HasEverTriedInvalidSubmit()) {
      return true;
    }

    switch (GetValueMode()) {
      case VALUE_MODE_DEFAULT:
        return true;
      case VALUE_MODE_DEFAULT_ON:
        return GetCheckedChanged();
      case VALUE_MODE_VALUE:
      case VALUE_MODE_FILENAME:
        return mValueChanged;
      default:
        NS_NOTREACHED("We should not be there: there are no other modes.");
        return false;
    }
  }

  /**
   * Returns the radio group container if the element has one, null otherwise.
   * The radio group container will be the form owner if there is one.
   * The current document otherwise.
   * @return the radio group container if the element has one, null otherwise.
   */
  nsIRadioGroupContainer* GetRadioGroupContainer() const;

  /**
   * Convert a string to a Decimal number in a type specific way,
   * http://www.whatwg.org/specs/web-apps/current-work/multipage/the-input-element.html#concept-input-value-string-number
   * ie parse a date string to a timestamp if type=date,
   * or parse a number string to its value if type=number.
   * @param aValue the string to be parsed.
   * @param aResultValue the number as a Decimal.
   * @result whether the parsing was successful.
   */
  bool ConvertStringToNumber(nsAString& aValue, Decimal& aResultValue) const;

  /**
   * Convert a Decimal to a string in a type specific way, ie convert a timestamp
   * to a date string if type=date or append the number string representing the
   * value if type=number.
   *
   * @param aValue the Decimal to be converted
   * @param aResultString [out] the string representing the Decimal
   * @return whether the function succeded, it will fail if the current input's
   *         type is not supported or the number can't be converted to a string
   *         as expected by the type.
   */
  bool ConvertNumberToString(Decimal aValue, nsAString& aResultString) const;

  /**
   * Parse a color string of the form #XXXXXX where X should be hexa characters
   * @param the string to be parsed.
   * @return whether the string is a valid simple color.
   * Note : this function does not consider the empty string as valid.
   */
  bool IsValidSimpleColor(const nsAString& aValue) const;

  /**
   * Parse a date string of the form yyyy-mm-dd
   * @param the string to be parsed.
   * @return whether the string is a valid date.
   * Note : this function does not consider the empty string as valid.
   */
  bool IsValidDate(const nsAString& aValue) const;

  /**
   * Parse a date string of the form yyyy-mm-dd
   * @param the string to be parsed.
   * @return the date in aYear, aMonth, aDay.
   * @return whether the parsing was successful.
   */
  bool GetValueAsDate(const nsAString& aValue,
                      uint32_t* aYear,
                      uint32_t* aMonth,
                      uint32_t* aDay) const;

  /**
   * This methods returns the number of days in a given month, for a given year.
   */
  uint32_t NumberOfDaysInMonth(uint32_t aMonth, uint32_t aYear) const;

  /**
   * Returns whether aValue is a valid time as described by HTML specifications:
   * http://www.whatwg.org/specs/web-apps/current-work/multipage/common-microsyntaxes.html#valid-time-string
   *
   * @param aValue the string to be tested.
   * @return Whether the string is a valid time per HTML specifications.
   */
  bool IsValidTime(const nsAString& aValue) const;

  /**
   * Returns the time expressed in milliseconds of |aValue| being parsed as a
   * time following the HTML specifications:
   * http://www.whatwg.org/specs/web-apps/current-work/#parse-a-time-string
   *
   * Note: |aResult| can be null.
   *
   * @param aValue  the string to be parsed.
   * @param aResult the time expressed in milliseconds representing the time [out]
   * @return Whether the parsing was successful.
   */
  static bool ParseTime(const nsAString& aValue, uint32_t* aResult);

  /**
   * Sets the value of the element to the string representation of the Decimal.
   *
   * @param aValue The Decimal that will be used to set the value.
   */
  void SetValue(Decimal aValue);

  /**
   * Update the HAS_RANGE bit field value.
   */
  void UpdateHasRange();

   /**
    * Get the step scale value for the current type.
    * See:
    * http://www.whatwg.org/specs/web-apps/current-work/multipage/common-input-element-attributes.html#concept-input-step-scale
    */
  Decimal GetStepScaleFactor() const;

  /**
   * Return the base used to compute if a value matches step.
   * Basically, it's the min attribute if present and a default value otherwise.
   *
   * @return The step base.
   */
  Decimal GetStepBase() const;

  /**
   * Returns the default step for the current type.
   * @return the default step for the current type.
   */
  Decimal GetDefaultStep() const;

  /**
   * Apply a step change from stepUp or stepDown by multiplying aStep by the
   * current step value.
   *
   * @param aStep The value used to be multiplied against the step value.
   */
  nsresult ApplyStep(int32_t aStep);

  /**
   * Returns if the current type is an experimental mobile type.
   */
  static bool IsExperimentalMobileType(uint8_t aType)
  {
    return aType == NS_FORM_INPUT_NUMBER || aType == NS_FORM_INPUT_DATE ||
           aType == NS_FORM_INPUT_TIME;
  }

  /**
   * Returns true if the element should prevent dispatching another DOMActivate.
   * This is used in situations where the anonymous subtree should already have
   * sent a DOMActivate and prevents firing more than once.
   */
  bool ShouldPreventDOMActivateDispatch(EventTarget* aOriginalTarget);

  nsCOMPtr<nsIControllers> mControllers;

  /*
   * In mInputData, the mState field is used if IsSingleLineTextControl returns
   * true and mValue is used otherwise.  We have to be careful when handling it
   * on a type change.
   *
   * Accessing the mState member should be done using the GetEditorState function,
   * which returns null if the state is not present.
   */
  union InputData {
    /**
     * The current value of the input if it has been changed from the default
     */
    PRUnichar*               mValue;
    /**
     * The state of the text editor associated with the text/password input
     */
    nsTextEditorState*       mState;
  } mInputData;
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
  nsCOMArray<nsIDOMFile>   mFiles;

  nsRefPtr<nsDOMFileList>  mFileList;

  nsString mStaticDocFileList;
  
  /** 
   * The value of the input element when first initialized and it is updated
   * when the element is either changed through a script, focused or dispatches   
   * a change event. This is to ensure correct future change event firing.
   * NB: This is ONLY applicable where the element is a text control. ie,
   * where type= "text", "email", "search", "tel", "url" or "password".
   */
  nsString mFocusedValue;  

  /**
   * If mIsDraggingRange is true, this is the value that the input had before
   * the drag started. Used to reset the input to its old value if the drag is
   * canceled.
   */
  Decimal mRangeThumbDragStartValue;

  // Step scale factor values, for input types that have one.
  static const Decimal kStepScaleFactorDate;
  static const Decimal kStepScaleFactorNumberRange;
  static const Decimal kStepScaleFactorTime;

  // Default step base value when a type do not have specific one.
  static const Decimal kDefaultStepBase;

  // Default step used when there is no specified step.
  static const Decimal kDefaultStep;
  static const Decimal kDefaultStepTime;

  // Float value returned by GetStep() when the step attribute is set to 'any'.
  static const Decimal kStepAny;

  /**
   * The type of this input (<input type=...>) as an integer.
   * @see nsIFormControl.h (specifically NS_FORM_INPUT_*)
   */
  uint8_t                  mType;
  bool                     mDisabledChanged     : 1;
  bool                     mValueChanged        : 1;
  bool                     mCheckedChanged      : 1;
  bool                     mChecked             : 1;
  bool                     mHandlingSelectEvent : 1;
  bool                     mShouldInitChecked   : 1;
  bool                     mParserCreating      : 1;
  bool                     mInInternalActivate  : 1;
  bool                     mCheckedIsToggled    : 1;
  bool                     mIndeterminate       : 1;
  bool                     mInhibitRestoration  : 1;
  bool                     mCanShowValidUI      : 1;
  bool                     mCanShowInvalidUI    : 1;
  bool                     mHasRange            : 1;
  bool                     mIsDraggingRange     : 1;

private:

  /**
   * Returns true if this input's type will fire a DOM "change" event when it
   * loses focus if its value has changed since it gained focus.
   */
  bool MayFireChangeOnBlur() const {
    return MayFireChangeOnBlur(mType);
  }

  static bool MayFireChangeOnBlur(uint8_t aType) {
    return IsSingleLineTextControl(false, aType) ||
           aType == NS_FORM_INPUT_RANGE;
  }

  struct nsFilePickerFilter {
    nsFilePickerFilter()
      : mFilterMask(0), mIsTrusted(false) {}

    nsFilePickerFilter(int32_t aFilterMask)
      : mFilterMask(aFilterMask), mIsTrusted(true) {}

    nsFilePickerFilter(const nsString& aTitle,
                       const nsString& aFilter,
                       const bool aIsTrusted = false)
      : mFilterMask(0), mTitle(aTitle), mFilter(aFilter), mIsTrusted(aIsTrusted) {}

    nsFilePickerFilter(const nsFilePickerFilter& other) {
      mFilterMask = other.mFilterMask;
      mTitle = other.mTitle;
      mFilter = other.mFilter;
      mIsTrusted = other.mIsTrusted;
    }

    bool operator== (const nsFilePickerFilter& other) const {
      if ((mFilter == other.mFilter) && (mFilterMask == other.mFilterMask)) {
        NS_ASSERTION(mIsTrusted == other.mIsTrusted,
                     "Filter with similar list of extensions and mask should"
                     " have the same trusted flag value");
        return true;
      } else {
        return false;
      }
    }
    
    // Filter mask, using values defined in nsIFilePicker
    int32_t mFilterMask;
    // If mFilterMask is defined, mTitle and mFilter are useless and should be
    // ignored
    nsString mTitle;
    nsString mFilter;
    // mIsTrusted is true if mime type comes from a "trusted" source (e.g. our
    // hard-coded set).
    // false means it may come from an "untrusted" source (e.g. OS mime types
    // mapping, which can be different accross OS, user's personal configuration, ...)
    // For now, only mask filters are considered to be "trusted".
    bool mIsTrusted; 
  };

  class AsyncClickHandler
    : public nsRunnable
  {
  public:
    AsyncClickHandler(HTMLInputElement* aInput);
    NS_IMETHOD Run() MOZ_OVERRIDE;

  protected:
    nsresult InitFilePicker();
    nsresult InitColorPicker();

    nsRefPtr<HTMLInputElement> mInput;
    PopupControlState mPopupControlState;
  };

  class nsFilePickerShownCallback
    : public nsIFilePickerShownCallback
  {
  public:
    nsFilePickerShownCallback(HTMLInputElement* aInput,
                              nsIFilePicker* aFilePicker,
                              bool aMulti);
    virtual ~nsFilePickerShownCallback()
    { }

    NS_DECL_ISUPPORTS

    NS_IMETHOD Done(int16_t aResult) MOZ_OVERRIDE;

  private:
    nsCOMPtr<nsIFilePicker> mFilePicker;
    nsRefPtr<HTMLInputElement> mInput;
    bool mMulti;
  };
};

} // namespace dom
} // namespace mozilla

#endif
