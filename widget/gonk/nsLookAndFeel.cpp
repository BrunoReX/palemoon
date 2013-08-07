/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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
 *   Mozilla Foundation
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Vladimir Vukicevic <vladimir@pobox.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#include "nsLookAndFeel.h"

nsLookAndFeel::nsLookAndFeel()
    : nsXPLookAndFeel()
{
}

nsLookAndFeel::~nsLookAndFeel()
{
}

nsresult
nsLookAndFeel::NativeGetColor(ColorID aID, nscolor &aColor)
{
    nsresult rv = NS_OK;

#define BASE_ACTIVE_COLOR     NS_RGB(0xaa,0xaa,0xaa)
#define BASE_NORMAL_COLOR     NS_RGB(0xff,0xff,0xff)
#define BASE_SELECTED_COLOR   NS_RGB(0xaa,0xaa,0xaa)
#define BG_ACTIVE_COLOR       NS_RGB(0xff,0xff,0xff)
#define BG_INSENSITIVE_COLOR  NS_RGB(0xaa,0xaa,0xaa)
#define BG_NORMAL_COLOR       NS_RGB(0xff,0xff,0xff)
#define BG_PRELIGHT_COLOR     NS_RGB(0xee,0xee,0xee)
#define BG_SELECTED_COLOR     NS_RGB(0x99,0x99,0x99)
#define DARK_NORMAL_COLOR     NS_RGB(0x88,0x88,0x88)
#define FG_INSENSITIVE_COLOR  NS_RGB(0x44,0x44,0x44)
#define FG_NORMAL_COLOR       NS_RGB(0x00,0x00,0x00)
#define FG_PRELIGHT_COLOR     NS_RGB(0x77,0x77,0x77)
#define FG_SELECTED_COLOR     NS_RGB(0xaa,0xaa,0xaa)
#define LIGHT_NORMAL_COLOR    NS_RGB(0xaa,0xaa,0xaa)
#define TEXT_ACTIVE_COLOR     NS_RGB(0x99,0x99,0x99)
#define TEXT_NORMAL_COLOR     NS_RGB(0x00,0x00,0x00)
#define TEXT_SELECTED_COLOR   NS_RGB(0x00,0x00,0x00)

    switch (aID) {
        // These colors don't seem to be used for anything anymore in Mozilla
        // (except here at least TextSelectBackground and TextSelectForeground)
        // The CSS2 colors below are used.
    case eColorID_WindowBackground:
        aColor = BASE_NORMAL_COLOR;
        break;
    case eColorID_WindowForeground:
        aColor = TEXT_NORMAL_COLOR;
        break;
    case eColorID_WidgetBackground:
        aColor = BG_NORMAL_COLOR;
        break;
    case eColorID_WidgetForeground:
        aColor = FG_NORMAL_COLOR;
        break;
    case eColorID_WidgetSelectBackground:
        aColor = BG_SELECTED_COLOR;
        break;
    case eColorID_WidgetSelectForeground:
        aColor = FG_SELECTED_COLOR;
        break;
    case eColorID_Widget3DHighlight:
        aColor = NS_RGB(0xa0,0xa0,0xa0);
        break;
    case eColorID_Widget3DShadow:
        aColor = NS_RGB(0x40,0x40,0x40);
        break;
    case eColorID_TextBackground:
        // not used?
        aColor = BASE_NORMAL_COLOR;
        break;
    case eColorID_TextForeground: 
        // not used?
        aColor = TEXT_NORMAL_COLOR;
        break;
    case eColorID_TextSelectBackground:
    case eColorID_IMESelectedRawTextBackground:
    case eColorID_IMESelectedConvertedTextBackground:
        // still used
        aColor = BASE_SELECTED_COLOR;
        break;
    case eColorID_TextSelectForeground:
    case eColorID_IMESelectedRawTextForeground:
    case eColorID_IMESelectedConvertedTextForeground:
        // still used
        aColor = TEXT_SELECTED_COLOR;
        break;
    case eColorID_IMERawInputBackground:
    case eColorID_IMEConvertedTextBackground:
        aColor = NS_TRANSPARENT;
        break;
    case eColorID_IMERawInputForeground:
    case eColorID_IMEConvertedTextForeground:
        aColor = NS_SAME_AS_FOREGROUND_COLOR;
        break;
    case eColorID_IMERawInputUnderline:
    case eColorID_IMEConvertedTextUnderline:
        aColor = NS_SAME_AS_FOREGROUND_COLOR;
        break;
    case eColorID_IMESelectedRawTextUnderline:
    case eColorID_IMESelectedConvertedTextUnderline:
        aColor = NS_TRANSPARENT;
        break;
    case eColorID_SpellCheckerUnderline:
      aColor = NS_RGB(0xff, 0, 0);
      break;

        // css2  http://www.w3.org/TR/REC-CSS2/ui.html#system-colors
    case eColorID_activeborder:
        // active window border
        aColor = BG_NORMAL_COLOR;
        break;
    case eColorID_activecaption:
        // active window caption background
        aColor = BG_NORMAL_COLOR;
        break;
    case eColorID_appworkspace:
        // MDI background color
        aColor = BG_NORMAL_COLOR;
        break;
    case eColorID_background:
        // desktop background
        aColor = BG_NORMAL_COLOR;
        break;
    case eColorID_captiontext:
        // text in active window caption, size box, and scrollbar arrow box (!)
        aColor = FG_NORMAL_COLOR;
        break;
    case eColorID_graytext:
        // disabled text in windows, menus, etc.
        aColor = FG_INSENSITIVE_COLOR;
        break;
    case eColorID_highlight:
        // background of selected item
        aColor = BASE_SELECTED_COLOR;
        break;
    case eColorID_highlighttext:
        // text of selected item
        aColor = TEXT_SELECTED_COLOR;
        break;
    case eColorID_inactiveborder:
        // inactive window border
        aColor = BG_NORMAL_COLOR;
        break;
    case eColorID_inactivecaption:
        // inactive window caption
        aColor = BG_INSENSITIVE_COLOR;
        break;
    case eColorID_inactivecaptiontext:
        // text in inactive window caption
        aColor = FG_INSENSITIVE_COLOR;
        break;
    case eColorID_infobackground:
        // tooltip background color
        aColor = BG_NORMAL_COLOR;
        break;
    case eColorID_infotext:
        // tooltip text color
        aColor = TEXT_NORMAL_COLOR;
        break;
    case eColorID_menu:
        // menu background
        aColor = BG_NORMAL_COLOR;
        break;
    case eColorID_menutext:
        // menu text
        aColor = TEXT_NORMAL_COLOR;
        break;
    case eColorID_scrollbar:
        // scrollbar gray area
        aColor = BG_ACTIVE_COLOR;
        break;

    case eColorID_threedface:
    case eColorID_buttonface:
        // 3-D face color
        aColor = BG_NORMAL_COLOR;
        break;

    case eColorID_buttontext:
        // text on push buttons
        aColor = TEXT_NORMAL_COLOR;
        break;

    case eColorID_buttonhighlight:
        // 3-D highlighted edge color
    case eColorID_threedhighlight:
        // 3-D highlighted outer edge color
        aColor = LIGHT_NORMAL_COLOR;
        break;

    case eColorID_threedlightshadow:
        // 3-D highlighted inner edge color
        aColor = BG_NORMAL_COLOR;
        break;

    case eColorID_buttonshadow:
        // 3-D shadow edge color
    case eColorID_threedshadow:
        // 3-D shadow inner edge color
        aColor = DARK_NORMAL_COLOR;
        break;

    case eColorID_threeddarkshadow:
        // 3-D shadow outer edge color
        aColor = NS_RGB(0,0,0);
        break;

    case eColorID_window:
    case eColorID_windowframe:
        aColor = BG_NORMAL_COLOR;
        break;

    case eColorID_windowtext:
        aColor = FG_NORMAL_COLOR;
        break;

    case eColorID__moz_eventreerow:
    case eColorID__moz_field:
        aColor = BASE_NORMAL_COLOR;
        break;
    case eColorID__moz_fieldtext:
        aColor = TEXT_NORMAL_COLOR;
        break;
    case eColorID__moz_dialog:
        aColor = BG_NORMAL_COLOR;
        break;
    case eColorID__moz_dialogtext:
        aColor = FG_NORMAL_COLOR;
        break;
    case eColorID__moz_dragtargetzone:
        aColor = BG_SELECTED_COLOR;
        break; 
    case eColorID__moz_buttondefault:
        // default button border color
        aColor = NS_RGB(0,0,0);
        break;
    case eColorID__moz_buttonhoverface:
        aColor = BG_PRELIGHT_COLOR;
        break;
    case eColorID__moz_buttonhovertext:
        aColor = FG_PRELIGHT_COLOR;
        break;
    case eColorID__moz_cellhighlight:
    case eColorID__moz_html_cellhighlight:
        aColor = BASE_ACTIVE_COLOR;
        break;
    case eColorID__moz_cellhighlighttext:
    case eColorID__moz_html_cellhighlighttext:
        aColor = TEXT_ACTIVE_COLOR;
        break;
    case eColorID__moz_menuhover:
        aColor = BG_PRELIGHT_COLOR;
        break;
    case eColorID__moz_menuhovertext:
        aColor = FG_PRELIGHT_COLOR;
        break;
    case eColorID__moz_oddtreerow:
        aColor = NS_TRANSPARENT;
        break;
    case eColorID__moz_nativehyperlinktext:
        aColor = NS_SAME_AS_FOREGROUND_COLOR;
        break;
    case eColorID__moz_comboboxtext:
        aColor = TEXT_NORMAL_COLOR;
        break;
    case eColorID__moz_combobox:
        aColor = BG_NORMAL_COLOR;
        break;
    case eColorID__moz_menubartext:
        aColor = TEXT_NORMAL_COLOR;
        break;
    case eColorID__moz_menubarhovertext:
        aColor = FG_PRELIGHT_COLOR;
        break;
    default:
        /* default color is BLACK */
        aColor = 0;
        rv = NS_ERROR_FAILURE;
        break;
    }

    return rv;
}


