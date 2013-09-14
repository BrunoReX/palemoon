/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ion_arm_BaselineCompiler_arm_h
#define ion_arm_BaselineCompiler_arm_h

#include "ion/shared/BaselineCompiler-shared.h"

namespace js {
namespace ion {

class BaselineCompilerARM : public BaselineCompilerShared
{
  protected:
    BaselineCompilerARM(JSContext *cx, HandleScript script);
};

typedef BaselineCompilerARM BaselineCompilerSpecific;

} // namespace ion
} // namespace js

#endif /* ion_arm_BaselineCompiler_arm_h */
