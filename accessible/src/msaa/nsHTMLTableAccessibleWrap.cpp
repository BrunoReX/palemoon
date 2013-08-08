/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:expandtab:shiftwidth=2:tabstop=2:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsHTMLTableAccessibleWrap.h"

////////////////////////////////////////////////////////////////////////////////
// nsHTMLTableAccessibleWrap
////////////////////////////////////////////////////////////////////////////////

NS_IMPL_ISUPPORTS_INHERITED0(nsHTMLTableAccessibleWrap,
                             nsHTMLTableAccessible)

IMPL_IUNKNOWN_INHERITED1(nsHTMLTableAccessibleWrap,
                         AccessibleWrap,
                         CAccessibleTable)


////////////////////////////////////////////////////////////////////////////////
// nsHTMLTableCellAccessibleWrap
////////////////////////////////////////////////////////////////////////////////

NS_IMPL_ISUPPORTS_INHERITED0(nsHTMLTableCellAccessibleWrap,
                             nsHTMLTableCellAccessible)

IMPL_IUNKNOWN_INHERITED1(nsHTMLTableCellAccessibleWrap,
                         HyperTextAccessibleWrap,
                         CAccessibleTableCell)


////////////////////////////////////////////////////////////////////////////////
// nsHTMLTableCellAccessibleWrap
////////////////////////////////////////////////////////////////////////////////

NS_IMPL_ISUPPORTS_INHERITED0(nsHTMLTableHeaderCellAccessibleWrap,
                             nsHTMLTableHeaderCellAccessible)

IMPL_IUNKNOWN_INHERITED1(nsHTMLTableHeaderCellAccessibleWrap,
                         HyperTextAccessibleWrap,
                         CAccessibleTableCell)
