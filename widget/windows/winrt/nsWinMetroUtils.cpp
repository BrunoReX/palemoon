/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWinMetroUtils.h"
#include "MetroUtils.h"
#include "nsXULAppAPI.h"
#include "FrameworkView.h"
#include "MetroApp.h"
#include "nsIWindowsRegKey.h"

#include <shldisp.h>
#include <shellapi.h>
#include <windows.ui.viewmanagement.h>
#include <windows.ui.startscreen.h>
#include <Wincrypt.h>

using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::UI::StartScreen;
using namespace ABI::Windows::UI::ViewManagement;
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace mozilla::widget::winrt;

namespace mozilla {
namespace widget {
namespace winrt {
extern ComPtr<MetroApp> sMetroApp;
extern nsTArray<nsString>* sSettingsArray;
extern ComPtr<FrameworkView> sFrameworkView;
} } }

namespace mozilla {
namespace widget {

static LPCWSTR sSyncEmailField = L"sync-e";
static LPCWSTR sSyncPasswordField = L"sync-p";
static LPCWSTR sSyncKeyField = L"sync-k";
static LPCSTR sRegPath = "Software\\Mozilla\\Firefox";

NS_IMPL_ISUPPORTS1(nsWinMetroUtils, nsIWinMetroUtils)

nsWinMetroUtils::nsWinMetroUtils()
{
}

nsWinMetroUtils::~nsWinMetroUtils()
{
}

/**
 * Pins a new tile to the Windows 8 start screen.
 *
 * @param aTileID         An ID which can later be used to remove the tile
 * @param aShortName      A short name for the tile
 * @param aDiplayName     The name that will be displayed on the tile
 * @param aActivationArgs The arguments to pass to the browser upon
 *                        activation of the tile
 * @param aTileImage An image for the normal tile view
 * @param aSmallTileImage An image for the small tile view
 */
NS_IMETHODIMP
nsWinMetroUtils::PinTileAsync(const nsAString &aTileID,
                              const nsAString &aShortName,
                              const nsAString &aDisplayName,
                              const nsAString &aActivationArgs,
                              const nsAString &aTileImage,
                              const nsAString &aSmallTileImage)
{
  if (XRE_GetWindowsEnvironment() == WindowsEnvironmentType_Desktop) {
    NS_WARNING("PinTileAsync can't be called on the desktop.");
    return NS_ERROR_FAILURE;
  }
  HRESULT hr;

  HString logoStr, smallLogoStr, displayName, shortName;

  logoStr.Set(aTileImage.BeginReading());
  smallLogoStr.Set(aSmallTileImage.BeginReading());
  displayName.Set(aDisplayName.BeginReading());
  shortName.Set(aShortName.BeginReading());

  ComPtr<IUriRuntimeClass> logo, smallLogo;
  AssertRetHRESULT(MetroUtils::CreateUri(logoStr, logo), NS_ERROR_FAILURE);
  AssertRetHRESULT(MetroUtils::CreateUri(smallLogoStr, smallLogo), NS_ERROR_FAILURE);

  HString tileActivationArgumentsStr, tileIdStr;
  tileActivationArgumentsStr.Set(aActivationArgs.BeginReading());
  tileIdStr.Set(aTileID.BeginReading());

  ComPtr<ISecondaryTileFactory> tileFactory;
  ComPtr<ISecondaryTile> secondaryTile;
  hr = GetActivationFactory(HStringReference(RuntimeClass_Windows_UI_StartScreen_SecondaryTile).Get(),
                            tileFactory.GetAddressOf());
  AssertRetHRESULT(hr, NS_ERROR_FAILURE);
  hr = tileFactory->CreateWithId(tileIdStr.Get(), secondaryTile.GetAddressOf());
  AssertRetHRESULT(hr, NS_ERROR_FAILURE);

  secondaryTile->put_Logo(logo.Get());
  secondaryTile->put_SmallLogo(smallLogo.Get());
  secondaryTile->put_DisplayName(displayName.Get());
  secondaryTile->put_ShortName(shortName.Get());
  secondaryTile->put_Arguments(tileActivationArgumentsStr.Get());
  secondaryTile->put_TileOptions(TileOptions::TileOptions_ShowNameOnLogo);

  // The tile is created and we can now attempt to pin the tile.
  ComPtr<IAsyncOperationCompletedHandler<bool>> callback(Callback<IAsyncOperationCompletedHandler<bool>>(
    sMetroApp.Get(), &MetroApp::OnAsyncTileCreated));
  ComPtr<IAsyncOperation<bool>> operation;
  AssertRetHRESULT(secondaryTile->RequestCreateAsync(operation.GetAddressOf()), NS_ERROR_FAILURE);
  operation->put_Completed(callback.Get());
  return NS_OK;
}

/**
 * Unpins a tile from the Windows 8 start screen.
 *
 * @param aTileID An existing ID which was previously pinned
 */
NS_IMETHODIMP
nsWinMetroUtils::UnpinTileAsync(const nsAString &aTileID)
{
  if (XRE_GetWindowsEnvironment() == WindowsEnvironmentType_Desktop) {
    NS_WARNING("UnpinTileAsync can't be called on the desktop.");
    return NS_ERROR_FAILURE;
  }
  HRESULT hr;
  HString tileIdStr;
  tileIdStr.Set(aTileID.BeginReading());

  ComPtr<ISecondaryTileFactory> tileFactory;
  ComPtr<ISecondaryTile> secondaryTile;
  hr = GetActivationFactory(HStringReference(RuntimeClass_Windows_UI_StartScreen_SecondaryTile).Get(),
                            tileFactory.GetAddressOf());
  AssertRetHRESULT(hr, NS_ERROR_FAILURE);
  hr = tileFactory->CreateWithId(tileIdStr.Get(), secondaryTile.GetAddressOf());
  AssertRetHRESULT(hr, NS_ERROR_FAILURE);

  // Attempt to unpin the tile
  ComPtr<IAsyncOperationCompletedHandler<bool>> callback(Callback<IAsyncOperationCompletedHandler<bool>>(
    sMetroApp.Get(), &MetroApp::OnAsyncTileCreated));
  ComPtr<IAsyncOperation<bool>> operation;
  AssertRetHRESULT(secondaryTile->RequestDeleteAsync(operation.GetAddressOf()), NS_ERROR_FAILURE);
  operation->put_Completed(callback.Get());
  return NS_OK;
}

/**
 * Determines if a tile is pinned to the Windows 8 start screen.
 *
 * @param aTileID   An ID which may have been pinned with pinTileAsync
 * @param aIsPinned Out parameter for determining if the tile is pinned or not
 */
NS_IMETHODIMP
nsWinMetroUtils::IsTilePinned(const nsAString &aTileID, bool *aIsPinned)
{
  if (XRE_GetWindowsEnvironment() == WindowsEnvironmentType_Desktop) {
    NS_WARNING("IsTilePinned can't be called on the desktop.");
    return NS_ERROR_FAILURE;
  }
  NS_ENSURE_ARG_POINTER(aIsPinned);

  HRESULT hr;
  HString tileIdStr;
  tileIdStr.Set(aTileID.BeginReading());

  ComPtr<ISecondaryTileStatics> tileStatics;
  hr = GetActivationFactory(HStringReference(RuntimeClass_Windows_UI_StartScreen_SecondaryTile).Get(),
                            tileStatics.GetAddressOf());
  AssertRetHRESULT(hr, NS_ERROR_FAILURE);
  boolean result = false;
  tileStatics->Exists(tileIdStr.Get(), &result);
  *aIsPinned = result;
  return NS_OK;
}

/**
  * Stores the sync info securely in Windows
  *
  * @param aEmail The sync account email
  * @param aPassword The sync account password
  * @param aKey The sync account key
  */
NS_IMETHODIMP
nsWinMetroUtils::StoreSyncInfo(const nsAString &aEmail,
                               const nsAString &aPassword,
                               const nsAString &aKey)
{
  DATA_BLOB emailIn = {
    (aEmail.Length() + 1) * 2,
    (BYTE *)aEmail.BeginReading()},
  passwordIn = {
    (aPassword.Length() + 1) * 2,
    (BYTE *)aPassword.BeginReading()},
  keyIn = {
    (aKey.Length() + 1) * 2,
    (BYTE *)aKey.BeginReading()};
  DATA_BLOB emailOut = { 0, nullptr }, passwordOut = {0, nullptr }, keyOut = { 0, nullptr };
  bool succeeded = CryptProtectData(&emailIn, nullptr, nullptr, nullptr,
                                    nullptr, 0, &emailOut) &&
                   CryptProtectData(&passwordIn, nullptr, nullptr, nullptr,
                                    nullptr, 0, &passwordOut) &&
                   CryptProtectData(&keyIn, nullptr, nullptr, nullptr,
                                    nullptr, 0, &keyOut);

  if (succeeded) {
    nsresult rv;
    nsCOMPtr<nsIWindowsRegKey> regKey
      (do_CreateInstance("@mozilla.org/windows-registry-key;1", &rv));
    NS_ENSURE_SUCCESS(rv, rv);
    regKey->Create(nsIWindowsRegKey::ROOT_KEY_CURRENT_USER,
                  NS_ConvertUTF8toUTF16(sRegPath),
                  nsIWindowsRegKey::ACCESS_SET_VALUE);

    if (NS_FAILED(regKey->WriteBinaryValue(nsDependentString(sSyncEmailField),
                                           nsAutoCString((const char *)emailOut.pbData,
                                                         emailOut.cbData)))) {
      succeeded = false;
    }

    if (succeeded &&
        NS_FAILED(regKey->WriteBinaryValue(nsDependentString(sSyncPasswordField),
                                           nsAutoCString((const char *)passwordOut.pbData,
                                                         passwordOut.cbData)))) {
      succeeded = false;
    }

    if (succeeded &&
        NS_FAILED(regKey->WriteBinaryValue(nsDependentString(sSyncKeyField),
                                           nsAutoCString((const char *)keyOut.pbData,
                                                         keyOut.cbData)))) {
      succeeded = false;
    }
    regKey->Close();
  }

  LocalFree(emailOut.pbData);
  LocalFree(passwordOut.pbData);
  LocalFree(keyOut.pbData);

  return succeeded ? NS_OK : NS_ERROR_FAILURE;
}

/**
  * Loads the sync info securely in Windows
  *
  * @param aEmail The sync account email
  * @param aPassword The sync account password
  * @param aKey The sync account key
  */
NS_IMETHODIMP
nsWinMetroUtils::LoadSyncInfo(nsAString &aEmail, nsAString &aPassword,
                              nsAString &aKey)
{
  nsresult rv;
  nsCOMPtr<nsIWindowsRegKey> regKey
    (do_CreateInstance("@mozilla.org/windows-registry-key;1", &rv));
  NS_ENSURE_SUCCESS(rv, rv);
  regKey->Create(nsIWindowsRegKey::ROOT_KEY_CURRENT_USER,
                 NS_ConvertUTF8toUTF16(sRegPath),
                 nsIWindowsRegKey::ACCESS_QUERY_VALUE);

  nsAutoCString email, password, key;
  if (NS_FAILED(regKey->ReadBinaryValue(nsDependentString(sSyncEmailField), email)) ||
      NS_FAILED(regKey->ReadBinaryValue(nsDependentString(sSyncPasswordField), password)) ||
      NS_FAILED(regKey->ReadBinaryValue(nsDependentString(sSyncKeyField), key))) {
    return NS_ERROR_FAILURE;
  }
  regKey->Close();

  DATA_BLOB emailIn = { email.Length(), (BYTE*)email.BeginReading() },
            passwordIn = { password.Length(), (BYTE*)password.BeginReading() },
            keyIn = { key.Length(), (BYTE*)key.BeginReading() };
  DATA_BLOB emailOut = { 0, nullptr }, passwordOut = { 0, nullptr }, keyOut = { 0, nullptr };
  bool succeeded = CryptUnprotectData(&emailIn, nullptr, nullptr, nullptr,
                                      nullptr, 0, &emailOut) &&
                   CryptUnprotectData(&passwordIn, nullptr, nullptr, nullptr,
                                      nullptr, 0, &passwordOut) &&
                   CryptUnprotectData(&keyIn, nullptr, nullptr, nullptr,
                                      nullptr, 0, &keyOut);
  if (succeeded) {
    aEmail = reinterpret_cast<wchar_t*>(emailOut.pbData);
    aPassword = reinterpret_cast<wchar_t*>(passwordOut.pbData);
    aKey = reinterpret_cast<wchar_t*>(keyOut.pbData);
  }

  LocalFree(emailOut.pbData);
  LocalFree(passwordOut.pbData);
  LocalFree(keyOut.pbData);

  return succeeded ? NS_OK : NS_ERROR_FAILURE;
}

/**
  * Clears the stored sync info if any.
  */
NS_IMETHODIMP
nsWinMetroUtils::ClearSyncInfo()
{
  nsresult rv;
  nsCOMPtr<nsIWindowsRegKey> regKey
    (do_CreateInstance("@mozilla.org/windows-registry-key;1", &rv));
  NS_ENSURE_SUCCESS(rv, rv);
  regKey->Create(nsIWindowsRegKey::ROOT_KEY_CURRENT_USER,
                 NS_ConvertUTF8toUTF16(sRegPath),
                 nsIWindowsRegKey::ACCESS_WRITE);
  nsresult rv1 = regKey->RemoveValue(nsDependentString(sSyncEmailField));
  nsresult rv2 = regKey->RemoveValue(nsDependentString(sSyncPasswordField));
  nsresult rv3 = regKey->RemoveValue(nsDependentString(sSyncKeyField));
  regKey->Close();

  if (NS_FAILED(rv1) || NS_FAILED(rv2) || NS_FAILED(rv3)) {
      return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

/**
 * Launches the specified application with the specified arguments and
 * switches to Desktop mode if in metro mode.
*/
NS_IMETHODIMP
nsWinMetroUtils::LaunchInDesktop(const nsAString &aPath, const nsAString &aArguments)
{
  SHELLEXECUTEINFOW sinfo;
  memset(&sinfo, 0, sizeof(SHELLEXECUTEINFOW));
  sinfo.cbSize       = sizeof(SHELLEXECUTEINFOW);
  // Per the Metro style enabled desktop browser, for some reason,
  // SEE_MASK_FLAG_LOG_USAGE is needed to change from immersive mode
  // to desktop.
  sinfo.fMask        = SEE_MASK_FLAG_LOG_USAGE;
  sinfo.hwnd         = NULL;
  sinfo.lpFile       = aPath.BeginReading();
  sinfo.lpParameters = aArguments.BeginReading();
  sinfo.lpVerb       = L"open";
  sinfo.nShow        = SW_SHOWNORMAL;

  if (!ShellExecuteEx(&sinfo)) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsWinMetroUtils::GetSnappedState(int32_t *aSnappedState)
{
  if (XRE_GetWindowsEnvironment() == WindowsEnvironmentType_Desktop) {
    NS_WARNING("GetSnappedState can't be called on the desktop.");
    return NS_ERROR_FAILURE;
  }
  NS_ENSURE_ARG_POINTER(aSnappedState);
  ApplicationViewState viewState;
  AssertRetHRESULT(MetroUtils::GetViewState(viewState), NS_ERROR_UNEXPECTED);
  *aSnappedState = (int32_t) viewState;
  return NS_OK;
}

NS_IMETHODIMP
nsWinMetroUtils::Unsnap()
{
  if (XRE_GetWindowsEnvironment() == WindowsEnvironmentType_Desktop) {
    NS_WARNING("Unsnap can't be called on the desktop.");
    return NS_ERROR_FAILURE;
  }

  HRESULT hr = MetroUtils::TryUnsnap();
  return SUCCEEDED(hr) ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsWinMetroUtils::ShowSettingsFlyout()
{
  if (XRE_GetWindowsEnvironment() == WindowsEnvironmentType_Desktop) {
    NS_WARNING("Settings flyout can't be shown on the desktop.");
    return NS_ERROR_FAILURE;
  }

  HRESULT hr = MetroUtils::ShowSettingsFlyout();
  return SUCCEEDED(hr) ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsWinMetroUtils::GetImmersive(bool *aImersive)
{
  *aImersive =
    XRE_GetWindowsEnvironment() == WindowsEnvironmentType_Metro;
  return NS_OK;
}

NS_IMETHODIMP
nsWinMetroUtils::GetHandPreference(int32_t *aHandPreference)
{
  if (XRE_GetWindowsEnvironment() == WindowsEnvironmentType_Desktop) {
    *aHandPreference = nsIWinMetroUtils::handPreferenceRight;
    return NS_OK;
  }

  ComPtr<IUISettings> uiSettings;
  AssertRetHRESULT(ActivateGenericInstance(RuntimeClass_Windows_UI_ViewManagement_UISettings, uiSettings), NS_ERROR_UNEXPECTED);

  HandPreference value;
  uiSettings->get_HandPreference(&value);
  if (value == HandPreference::HandPreference_LeftHanded)
    *aHandPreference = nsIWinMetroUtils::handPreferenceLeft;
  else
    *aHandPreference = nsIWinMetroUtils::handPreferenceRight;

  return NS_OK;
}

NS_IMETHODIMP
nsWinMetroUtils::GetActivationURI(nsAString &aActivationURI)
{
  if (!sFrameworkView) {
    NS_WARNING("GetActivationURI used before view is created!");
    return NS_OK;
  }
  sFrameworkView->GetActivationURI(aActivationURI);
  return NS_OK;
}

NS_IMETHODIMP
nsWinMetroUtils::GetKeyboardVisible(bool *aImersive)
{
  *aImersive = mozilla::widget::winrt::FrameworkView::IsKeyboardVisible();
  return NS_OK;
}

NS_IMETHODIMP
nsWinMetroUtils::GetKeyboardX(uint32_t *aX)
{
  *aX = (uint32_t)floor(mozilla::widget::winrt::FrameworkView::KeyboardVisibleRect().X);
  return NS_OK;
}

NS_IMETHODIMP
nsWinMetroUtils::GetKeyboardY(uint32_t *aY)
{
  *aY = (uint32_t)floor(mozilla::widget::winrt::FrameworkView::KeyboardVisibleRect().Y);
  return NS_OK;
}

NS_IMETHODIMP
nsWinMetroUtils::GetKeyboardWidth(uint32_t *aWidth)
{
  *aWidth = (uint32_t)ceil(mozilla::widget::winrt::FrameworkView::KeyboardVisibleRect().Width);
  return NS_OK;
}

NS_IMETHODIMP
nsWinMetroUtils::GetKeyboardHeight(uint32_t *aHeight)
{
  *aHeight = (uint32_t)ceil(mozilla::widget::winrt::FrameworkView::KeyboardVisibleRect().Height);
  return NS_OK;
}

NS_IMETHODIMP
nsWinMetroUtils::AddSettingsPanelEntry(const nsAString &aLabel, uint32_t *aId)
{
  NS_ENSURE_ARG_POINTER(aId);
  if (!sSettingsArray)
    return NS_ERROR_UNEXPECTED;

  *aId = sSettingsArray->Length();
  sSettingsArray->AppendElement(nsString(aLabel));
  return NS_OK;
}

NS_IMETHODIMP
nsWinMetroUtils::SwapMouseButton(bool aValue, bool *aOriginalValue)
{
  *aOriginalValue = ::SwapMouseButton(aValue);
  return NS_OK;
}

} // widget
} // mozilla
