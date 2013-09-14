/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef MOZ_LOGGING
// so we can get logging even in release builds
#define FORCE_PR_LOG 1
#endif

#include "MetroUtils.h"
#include <windows.h>
#include "nsICommandLineRunner.h"
#include "nsNetUtil.h"
#include "nsIBrowserDOMWindow.h"
#include "nsIWebNavigation.h"
#include "nsIDocShellTreeItem.h"
#include "nsIDOMWindow.h"
#include "nsIDOMChromeWindow.h"
#include "nsIWindowMediator.h"
#include "nsIURI.h"
#include "prlog.h"
#include "nsIObserverService.h"

#include <wrl/wrappers/corewrappers.h>
#include <windows.ui.applicationsettings.h>
#include <windows.graphics.display.h>

using namespace ABI::Windows::UI::ApplicationSettings;

using namespace mozilla;

using namespace ABI::Windows::Foundation;
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::UI::ViewManagement;
using namespace ABI::Windows::Graphics::Display;

// File-scoped statics (unnamed namespace)
namespace {
#ifdef PR_LOGGING
  PRLogModuleInfo* metroWidgetLog = PR_NewLogModule("MetroWidget");
#endif

  FLOAT LogToPhysFactor() {
    ComPtr<IDisplayPropertiesStatics> dispProps;
    if (SUCCEEDED(GetActivationFactory(HStringReference(RuntimeClass_Windows_Graphics_Display_DisplayProperties).Get(),
                                       dispProps.GetAddressOf()))) {
      FLOAT dpi;
      if (SUCCEEDED(dispProps->get_LogicalDpi(&dpi))) {
        return dpi / 96.0f;
      }
    }
    return 1.0f;
  }

  FLOAT PhysToLogFactor() {
    ComPtr<IDisplayPropertiesStatics> dispProps;
    if (SUCCEEDED(GetActivationFactory(HStringReference(RuntimeClass_Windows_Graphics_Display_DisplayProperties).Get(),
                                       dispProps.GetAddressOf()))) {
      FLOAT dpi;
      if (SUCCEEDED(dispProps->get_LogicalDpi(&dpi))) {
        return 96.0f / dpi;
      }
    }
    return 1.0f;
  }
};

void LogW(const wchar_t *fmt, ...)
{
  va_list args = NULL;
  if(!lstrlenW(fmt))
    return;
  va_start(args, fmt);
  int buflen = _vscwprintf(fmt, args);
  wchar_t* buffer = new wchar_t[buflen+1];
  if (!buffer) {
    va_end(args);
    return;
  }
  vswprintf(buffer, buflen, fmt, args);
  va_end(args);

  // MSVC, including remote debug sessions
  OutputDebugStringW(buffer);
  OutputDebugStringW(L"\n");

  int len = wcslen(buffer);
  if (len) {
    char* utf8 = new char[len+1];
    memset(utf8, 0, sizeof(utf8));
    if (WideCharToMultiByte(CP_ACP, 0, buffer,
                            -1, utf8, len+1, NULL,
                            NULL) > 0) {
      // desktop console
      printf("%s\n", utf8);
#ifdef PR_LOGGING
      NS_ASSERTION(metroWidgetLog, "Called MetroUtils Log() but MetroWidget "
                                   "log module doesn't exist!");
      PR_LOG(metroWidgetLog, PR_LOG_ALWAYS, (utf8));
#endif
    }
    delete[] utf8;
  }
  delete[] buffer;
}

void Log(const char *fmt, ...)
{
  va_list args = NULL;
  if(!strlen(fmt))
    return;
  va_start(args, fmt);
  int buflen = _vscprintf(fmt, args);
  char* buffer = new char[buflen+1];
  if (!buffer) {
    va_end(args);
    return;
  }
  vsprintf(buffer, fmt, args);
  va_end(args);

  // MSVC, including remote debug sessions
  OutputDebugStringA(buffer);
  OutputDebugStringW(L"\n");

  // desktop console
  printf("%s\n", buffer);

#ifdef PR_LOGGING
  NS_ASSERTION(metroWidgetLog, "Called MetroUtils Log() but MetroWidget "
                               "log module doesn't exist!");
  PR_LOG(metroWidgetLog, PR_LOG_ALWAYS, (buffer));
#endif
  delete[] buffer;
}

// Conversion between logical and physical coordinates
int32_t
MetroUtils::LogToPhys(FLOAT aValue)
{
  return int32_t(NS_round(aValue * LogToPhysFactor()));
}

nsIntPoint
MetroUtils::LogToPhys(const Point& aPt)
{
  FLOAT factor = LogToPhysFactor();
  return nsIntPoint(int32_t(NS_round(aPt.X * factor)), int32_t(NS_round(aPt.Y * factor)));
}

nsIntRect
MetroUtils::LogToPhys(const Rect& aRect)
{
  FLOAT factor = LogToPhysFactor();
  return nsIntRect(int32_t(NS_round(aRect.X * factor)),
                   int32_t(NS_round(aRect.Y * factor)),
                   int32_t(NS_round(aRect.Width * factor)),
                   int32_t(NS_round(aRect.Height * factor)));
}

FLOAT
MetroUtils::PhysToLog(int32_t aValue)
{
  return FLOAT(aValue) * PhysToLogFactor();
}

Point
MetroUtils::PhysToLog(const nsIntPoint& aPt)
{
  FLOAT factor = PhysToLogFactor();
  Point p = { FLOAT(aPt.x) * factor, FLOAT(aPt.y) * factor };
  return p;
}

nsresult
MetroUtils::FireObserver(const char* aMessage, const PRUnichar* aData)
{
  nsCOMPtr<nsIObserverService> observerService =
    mozilla::services::GetObserverService();
  if (observerService) {
    return observerService->NotifyObservers(nullptr, aMessage, aData);
  }
  return NS_ERROR_FAILURE;
}

HRESULT MetroUtils::CreateUri(HSTRING aUriStr, ComPtr<IUriRuntimeClass>& aUriOut)
{
  HRESULT hr;
  ComPtr<IUriRuntimeClassFactory> uriFactory;
  hr = GetActivationFactory(HStringReference(RuntimeClass_Windows_Foundation_Uri).Get(), &uriFactory);
  AssertRetHRESULT(hr, hr);
  ComPtr<IUriRuntimeClass> uri;
  return uriFactory->CreateUri(aUriStr, &aUriOut);
}

HRESULT MetroUtils::CreateUri(HString& aHString, ComPtr<IUriRuntimeClass>& aUriOut)
{
  return MetroUtils::CreateUri(aHString.Get(), aUriOut);
}

HRESULT
MetroUtils::GetViewState(ApplicationViewState& aState)
{
  HRESULT hr;
  ComPtr<IApplicationViewStatics> appViewStatics;
  hr = GetActivationFactory(HStringReference(RuntimeClass_Windows_UI_ViewManagement_ApplicationView).Get(),
    appViewStatics.GetAddressOf());
  AssertRetHRESULT(hr, hr);
  hr = appViewStatics->get_Value(&aState);
  return hr;
}

HRESULT
MetroUtils::TryUnsnap(bool* aResult)
{
  HRESULT hr;
  ComPtr<IApplicationViewStatics> appViewStatics;
  hr = GetActivationFactory(HStringReference(RuntimeClass_Windows_UI_ViewManagement_ApplicationView).Get(),
    appViewStatics.GetAddressOf());
  AssertRetHRESULT(hr, hr);
  boolean success = false;
  hr = appViewStatics->TryUnsnap(&success);
  if (aResult)
    *aResult = success;
  return hr;
}

HRESULT
MetroUtils::ShowSettingsFlyout()
{
  ComPtr<ISettingsPaneStatics> settingsPaneStatics;
  HRESULT hr = GetActivationFactory(HStringReference(RuntimeClass_Windows_UI_ApplicationSettings_SettingsPane).Get(),
                                    settingsPaneStatics.GetAddressOf());
  if (SUCCEEDED(hr)) {
    settingsPaneStatics->Show();
  }

  return hr;
}
