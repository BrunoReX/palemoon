/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_gamepad_Gamepad_h
#define mozilla_dom_gamepad_Gamepad_h

#include "mozilla/ErrorResult.h"
#include "mozilla/StandardInteger.h"
#include "nsCOMPtr.h"
#include "nsIDOMGamepad.h"
#include "nsIVariant.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsWrapperCache.h"

namespace mozilla {
namespace dom {

enum GamepadMappingType
{
  NoMapping = 0,
  StandardMapping = 1
};

// TODO: fix the spec to expose both pressed and value:
// https://www.w3.org/Bugs/Public/show_bug.cgi?id=21388
struct GamepadButton
{
  bool pressed;
  double value;

  GamepadButton(): pressed(false), value(0.0) {}
};

class Gamepad : public nsIDOMGamepad
              , public nsWrapperCache
{
public:
  Gamepad(nsISupports* aParent,
          const nsAString& aID, uint32_t aIndex,
          GamepadMappingType aMapping,
          uint32_t aNumButtons, uint32_t aNumAxes);
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(Gamepad)

  void SetConnected(bool aConnected);
  void SetButton(uint32_t aButton, bool aPressed, double aValue);
  void SetAxis(uint32_t aAxis, double aValue);
  void SetIndex(uint32_t aIndex);

  // Make the state of this gamepad equivalent to other.
  void SyncState(Gamepad* aOther);

  // Return a new Gamepad containing the same data as this object,
  // parented to aParent.
  already_AddRefed<Gamepad> Clone(nsISupports* aParent);

  nsISupports* GetParentObject() const
  {
    return mParent;
  }

  virtual JSObject* WrapObject(JSContext* aCx,
			       JS::Handle<JSObject*> aScope) MOZ_OVERRIDE;

  void GetId(nsAString& aID) const
  {
    aID = mID;
  }

  void GetMapping(nsAString& aMapping) const
  {
    if (mMapping == StandardMapping) {
      aMapping = NS_LITERAL_STRING("standard");
    } else {
      aMapping = NS_LITERAL_STRING("");
    }
  }

  bool Connected() const
  {
    return mConnected;
  }

  uint32_t Index() const
  {
    return mIndex;
  }

  already_AddRefed<nsIVariant> GetButtons(mozilla::ErrorResult& aRv)
  {
    nsCOMPtr<nsIVariant> buttons;
    aRv = GetButtons(getter_AddRefs(buttons));
    return buttons.forget();
  }

  already_AddRefed<nsIVariant> GetAxes(mozilla::ErrorResult& aRv)
  {
    nsCOMPtr<nsIVariant> axes;
    aRv = GetAxes(getter_AddRefs(axes));
    return axes.forget();
  }

private:
  virtual ~Gamepad() {}

  nsresult GetButtons(nsIVariant** aButtons);
  nsresult GetAxes(nsIVariant** aAxes);

protected:
  nsCOMPtr<nsISupports> mParent;
  nsString mID;
  uint32_t mIndex;

  // The mapping in use.
  GamepadMappingType mMapping;

  // true if this gamepad is currently connected.
  bool mConnected;

  // Current state of buttons, axes.
  nsTArray<GamepadButton> mButtons;
  nsTArray<double> mAxes;
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_gamepad_Gamepad_h
