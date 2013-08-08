/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=99 ft=cpp:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Assertions.h"

/* Implementations of runtime and static assertion macros for C and C++. */

extern "C" {

MOZ_EXPORT_API(void)
MOZ_Assert(const char* s, const char* file, int ln)
{
  MOZ_OutputAssertMessage(s, file, ln);
  MOZ_CRASH();
}

}
