/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_SVGPolylineElement_h
#define mozilla_dom_SVGPolylineElement_h

#include "nsSVGPolyElement.h"

nsresult NS_NewSVGPolylineElement(nsIContent **aResult,
                                  already_AddRefed<nsINodeInfo> aNodeInfo);

typedef nsSVGPolyElement SVGPolylineElementBase;

namespace mozilla {
namespace dom {

class SVGPolylineElement MOZ_FINAL : public SVGPolylineElementBase
{
protected:
  SVGPolylineElement(already_AddRefed<nsINodeInfo> aNodeInfo);
  virtual JSObject* WrapNode(JSContext *cx,
                             JS::Handle<JSObject*> scope) MOZ_OVERRIDE;
  friend nsresult (::NS_NewSVGPolylineElement(nsIContent **aResult,
                                              already_AddRefed<nsINodeInfo> aNodeInfo));

public:
  // nsIContent interface
  virtual nsresult Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const;
};

} // namespace mozilla
} // namespace dom

#endif // mozilla_dom_SVGPolylineElement_h
