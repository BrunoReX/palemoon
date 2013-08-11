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
 * The Original Code is the Mozilla SVG project.
 *
 * The Initial Developer of the Original Code is IBM Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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

// include nsSVGUtils.h first to ensure definition of M_SQRT1_2 is picked up
#include "nsSVGUtils.h"
#include "nsIDOMDocument.h"
#include "nsIDOMSVGElement.h"
#include "nsIDOMSVGSVGElement.h"
#include "nsStyleCoord.h"
#include "nsPresContext.h"
#include "nsSVGSVGElement.h"
#include "nsIContent.h"
#include "nsIDocument.h"
#include "nsIFrame.h"
#include "nsGkAtoms.h"
#include "nsIURI.h"
#include "nsStyleStruct.h"
#include "nsIPresShell.h"
#include "nsISVGGlyphFragmentLeaf.h"
#include "nsNetUtil.h"
#include "nsFrameList.h"
#include "nsISVGChildFrame.h"
#include "nsContentDLF.h"
#include "nsContentUtils.h"
#include "nsSVGFilterFrame.h"
#include "nsINameSpaceManager.h"
#include "nsIDOMSVGPoint.h"
#include "nsSVGPoint.h"
#include "nsDOMError.h"
#include "nsSVGOuterSVGFrame.h"
#include "nsSVGInnerSVGFrame.h"
#include "nsSVGPreserveAspectRatio.h"
#include "nsSVGMatrix.h"
#include "nsSVGClipPathFrame.h"
#include "nsSVGMaskFrame.h"
#include "nsSVGContainerFrame.h"
#include "nsSVGLength2.h"
#include "nsGenericElement.h"
#include "nsSVGGraphicElement.h"
#include "nsAttrValue.h"
#include "nsSVGGeometryFrame.h"
#include "nsIScriptError.h"
#include "gfxContext.h"
#include "gfxMatrix.h"
#include "gfxRect.h"
#include "gfxImageSurface.h"
#include "gfxPlatform.h"
#include "nsSVGForeignObjectFrame.h"
#include "nsIFontMetrics.h"
#include "nsIDOMSVGUnitTypes.h"
#include "nsSVGEffects.h"
#include "nsSVGIntegrationUtils.h"
#include "nsSVGFilterPaintCallback.h"
#include "nsSVGGeometryFrame.h"
#include "nsSVGPathGeometryFrame.h"

gfxASurface *nsSVGUtils::mThebesComputationalSurface = nsnull;

// c = n / 255
// (c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1 / 2.4) - 0.055) * 255 + 0.5
static const PRUint8 glinearRGBTosRGBMap[256] = {
  0,  13,  22,  28,  34,  38,  42,  46,
 50,  53,  56,  59,  61,  64,  66,  69,
 71,  73,  75,  77,  79,  81,  83,  85,
 86,  88,  90,  92,  93,  95,  96,  98,
 99, 101, 102, 104, 105, 106, 108, 109,
110, 112, 113, 114, 115, 117, 118, 119,
120, 121, 122, 124, 125, 126, 127, 128,
129, 130, 131, 132, 133, 134, 135, 136,
137, 138, 139, 140, 141, 142, 143, 144,
145, 146, 147, 148, 148, 149, 150, 151,
152, 153, 154, 155, 155, 156, 157, 158,
159, 159, 160, 161, 162, 163, 163, 164,
165, 166, 167, 167, 168, 169, 170, 170,
171, 172, 173, 173, 174, 175, 175, 176,
177, 178, 178, 179, 180, 180, 181, 182,
182, 183, 184, 185, 185, 186, 187, 187,
188, 189, 189, 190, 190, 191, 192, 192,
193, 194, 194, 195, 196, 196, 197, 197,
198, 199, 199, 200, 200, 201, 202, 202,
203, 203, 204, 205, 205, 206, 206, 207,
208, 208, 209, 209, 210, 210, 211, 212,
212, 213, 213, 214, 214, 215, 215, 216,
216, 217, 218, 218, 219, 219, 220, 220,
221, 221, 222, 222, 223, 223, 224, 224,
225, 226, 226, 227, 227, 228, 228, 229,
229, 230, 230, 231, 231, 232, 232, 233,
233, 234, 234, 235, 235, 236, 236, 237,
237, 238, 238, 238, 239, 239, 240, 240,
241, 241, 242, 242, 243, 243, 244, 244,
245, 245, 246, 246, 246, 247, 247, 248,
248, 249, 249, 250, 250, 251, 251, 251,
252, 252, 253, 253, 254, 254, 255, 255
};

// c = n / 255
// c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4)) * 255 + 0.5
static const PRUint8 gsRGBToLinearRGBMap[256] = {
  0,   0,   0,   0,   0,   0,   0,   1,
  1,   1,   1,   1,   1,   1,   1,   1,
  1,   1,   2,   2,   2,   2,   2,   2,
  2,   2,   3,   3,   3,   3,   3,   3,
  4,   4,   4,   4,   4,   5,   5,   5,
  5,   6,   6,   6,   6,   7,   7,   7,
  8,   8,   8,   8,   9,   9,   9,  10,
 10,  10,  11,  11,  12,  12,  12,  13,
 13,  13,  14,  14,  15,  15,  16,  16,
 17,  17,  17,  18,  18,  19,  19,  20,
 20,  21,  22,  22,  23,  23,  24,  24,
 25,  25,  26,  27,  27,  28,  29,  29,
 30,  30,  31,  32,  32,  33,  34,  35,
 35,  36,  37,  37,  38,  39,  40,  41,
 41,  42,  43,  44,  45,  45,  46,  47,
 48,  49,  50,  51,  51,  52,  53,  54,
 55,  56,  57,  58,  59,  60,  61,  62,
 63,  64,  65,  66,  67,  68,  69,  70,
 71,  72,  73,  74,  76,  77,  78,  79,
 80,  81,  82,  84,  85,  86,  87,  88,
 90,  91,  92,  93,  95,  96,  97,  99,
100, 101, 103, 104, 105, 107, 108, 109,
111, 112, 114, 115, 116, 118, 119, 121,
122, 124, 125, 127, 128, 130, 131, 133,
134, 136, 138, 139, 141, 142, 144, 146,
147, 149, 151, 152, 154, 156, 157, 159,
161, 163, 164, 166, 168, 170, 171, 173,
175, 177, 179, 181, 183, 184, 186, 188,
190, 192, 194, 196, 198, 200, 202, 204,
206, 208, 210, 212, 214, 216, 218, 220,
222, 224, 226, 229, 231, 233, 235, 237,
239, 242, 244, 246, 248, 250, 253, 255
};

static PRBool gSVGEnabled;
static const char SVG_PREF_STR[] = "svg.enabled";

#ifdef MOZ_SMIL
static PRBool gSMILEnabled;
static const char SMIL_PREF_STR[] = "svg.smil.enabled";
#endif // MOZ_SMIL

static int
SVGPrefChanged(const char *aPref, void *aClosure)
{
  PRBool prefVal = nsContentUtils::GetBoolPref(SVG_PREF_STR);
  if (prefVal == gSVGEnabled)
    return 0;

  gSVGEnabled = prefVal;
  if (gSVGEnabled)
    nsContentDLF::RegisterSVG();
  else
    nsContentDLF::UnregisterSVG();

  return 0;
}

PRBool
NS_SVGEnabled()
{
  static PRBool sInitialized = PR_FALSE;
  
  if (!sInitialized) {
    /* check and register ourselves with the pref */
    gSVGEnabled = nsContentUtils::GetBoolPref(SVG_PREF_STR);
    nsContentUtils::RegisterPrefCallback(SVG_PREF_STR, SVGPrefChanged, nsnull);

    sInitialized = PR_TRUE;
  }

  return gSVGEnabled;
}

#ifdef MOZ_SMIL
static int
SMILPrefChanged(const char *aPref, void *aClosure)
{
  PRBool prefVal = nsContentUtils::GetBoolPref(SMIL_PREF_STR);
  gSMILEnabled = prefVal;
  return 0;
}

PRBool
NS_SMILEnabled()
{
  static PRBool sInitialized = PR_FALSE;
  
  if (!sInitialized) {
    /* check and register ourselves with the pref */
    gSMILEnabled = nsContentUtils::GetBoolPref(SMIL_PREF_STR);
    nsContentUtils::RegisterPrefCallback(SMIL_PREF_STR, SMILPrefChanged, nsnull);

    sInitialized = PR_TRUE;
  }

  return gSMILEnabled;
}
#endif // MOZ_SMIL

static nsIFrame*
GetFrameForContent(nsIContent* aContent)
{
  if (!aContent)
    return nsnull;

  nsIDocument *doc = aContent->GetCurrentDoc();
  if (!doc)
    return nsnull;

  return nsGenericElement::GetPrimaryFrameFor(aContent, doc);
}

nsIContent*
nsSVGUtils::GetParentElement(nsIContent *aContent)
{
  // XXXbz I _think_ this is right.  We want to be using the binding manager
  // that would have attached the binding that gives us our anonymous parent.
  // That's the binding manager for the document we actually belong to, which
  // is our owner doc.
  nsIDocument* ownerDoc = aContent->GetOwnerDoc();
  nsBindingManager* bindingManager =
    ownerDoc ? ownerDoc->BindingManager() : nsnull;

  if (bindingManager) {
    // if we have a binding manager -- do we have an anonymous parent?
    nsIContent *result = bindingManager->GetInsertionParent(aContent);
    if (result) {
      return result;
    }
  }

  // otherewise use the explicit one, whether it's null or not...
  return aContent->GetParent();
}

float
nsSVGUtils::GetFontSize(nsIContent *aContent)
{
  nsIFrame* frame = GetFrameForContent(aContent);
  if (!frame) {
    NS_WARNING("no frame in GetFontSize()");
    return 1.0f;
  }

  return GetFontSize(frame);
}

float
nsSVGUtils::GetFontSize(nsIFrame *aFrame)
{
  return nsPresContext::AppUnitsToFloatCSSPixels(aFrame->GetStyleFont()->mSize) /
         aFrame->PresContext()->TextZoom();
}

float
nsSVGUtils::GetFontXHeight(nsIContent *aContent)
{
  nsIFrame* frame = GetFrameForContent(aContent);
  if (!frame) {
    NS_WARNING("no frame in GetFontXHeight()");
    return 1.0f;
  }

  return GetFontXHeight(frame);
}
  
float
nsSVGUtils::GetFontXHeight(nsIFrame *aFrame)
{
  nsCOMPtr<nsIFontMetrics> fontMetrics;
  nsLayoutUtils::GetFontMetricsForFrame(aFrame, getter_AddRefs(fontMetrics));

  if (!fontMetrics) {
    NS_WARNING("no FontMetrics in GetFontXHeight()");
    return 1.0f;
  }

  nscoord xHeight;
  fontMetrics->GetXHeight(xHeight);
  return nsPresContext::AppUnitsToFloatCSSPixels(xHeight) /
         aFrame->PresContext()->TextZoom();
}

void
nsSVGUtils::UnPremultiplyImageDataAlpha(PRUint8 *data, 
                                        PRInt32 stride,
                                        const nsIntRect &rect)
{
  for (PRInt32 y = rect.y; y < rect.YMost(); y++) {
    for (PRInt32 x = rect.x; x < rect.XMost(); x++) {
      PRUint8 *pixel = data + stride * y + 4 * x;

      PRUint8 a = pixel[GFX_ARGB32_OFFSET_A];
      if (a == 255)
        continue;

      if (a) {
        pixel[GFX_ARGB32_OFFSET_B] = (255 * pixel[GFX_ARGB32_OFFSET_B]) / a;
        pixel[GFX_ARGB32_OFFSET_G] = (255 * pixel[GFX_ARGB32_OFFSET_G]) / a;
        pixel[GFX_ARGB32_OFFSET_R] = (255 * pixel[GFX_ARGB32_OFFSET_R]) / a;
      } else {
        pixel[GFX_ARGB32_OFFSET_B] = 0;
        pixel[GFX_ARGB32_OFFSET_G] = 0;
        pixel[GFX_ARGB32_OFFSET_R] = 0;
      }
    }
  }
}

void
nsSVGUtils::PremultiplyImageDataAlpha(PRUint8 *data, 
                                      PRInt32 stride,
                                      const nsIntRect &rect)
{
  for (PRInt32 y = rect.y; y < rect.YMost(); y++) {
    for (PRInt32 x = rect.x; x < rect.XMost(); x++) {
      PRUint8 *pixel = data + stride * y + 4 * x;

      PRUint8 a = pixel[GFX_ARGB32_OFFSET_A];
      if (a == 255)
        continue;

      FAST_DIVIDE_BY_255(pixel[GFX_ARGB32_OFFSET_B],
                         pixel[GFX_ARGB32_OFFSET_B] * a);
      FAST_DIVIDE_BY_255(pixel[GFX_ARGB32_OFFSET_G],
                         pixel[GFX_ARGB32_OFFSET_G] * a);
      FAST_DIVIDE_BY_255(pixel[GFX_ARGB32_OFFSET_R],
                         pixel[GFX_ARGB32_OFFSET_R] * a);
    }
  }
}

void
nsSVGUtils::ConvertImageDataToLinearRGB(PRUint8 *data, 
                                        PRInt32 stride,
                                        const nsIntRect &rect)
{
  for (PRInt32 y = rect.y; y < rect.YMost(); y++) {
    for (PRInt32 x = rect.x; x < rect.XMost(); x++) {
      PRUint8 *pixel = data + stride * y + 4 * x;

      pixel[GFX_ARGB32_OFFSET_B] =
        gsRGBToLinearRGBMap[pixel[GFX_ARGB32_OFFSET_B]];
      pixel[GFX_ARGB32_OFFSET_G] =
        gsRGBToLinearRGBMap[pixel[GFX_ARGB32_OFFSET_G]];
      pixel[GFX_ARGB32_OFFSET_R] =
        gsRGBToLinearRGBMap[pixel[GFX_ARGB32_OFFSET_R]];
    }
  }
}

void
nsSVGUtils::ConvertImageDataFromLinearRGB(PRUint8 *data, 
                                          PRInt32 stride,
                                          const nsIntRect &rect)
{
  for (PRInt32 y = rect.y; y < rect.YMost(); y++) {
    for (PRInt32 x = rect.x; x < rect.XMost(); x++) {
      PRUint8 *pixel = data + stride * y + 4 * x;

      pixel[GFX_ARGB32_OFFSET_B] =
        glinearRGBTosRGBMap[pixel[GFX_ARGB32_OFFSET_B]];
      pixel[GFX_ARGB32_OFFSET_G] =
        glinearRGBTosRGBMap[pixel[GFX_ARGB32_OFFSET_G]];
      pixel[GFX_ARGB32_OFFSET_R] =
        glinearRGBTosRGBMap[pixel[GFX_ARGB32_OFFSET_R]];
    }
  }
}

nsresult
nsSVGUtils::ReportToConsole(nsIDocument* doc,
                            const char* aWarning,
                            const PRUnichar **aParams,
                            PRUint32 aParamsLength)
{
  return nsContentUtils::ReportToConsole(nsContentUtils::eSVG_PROPERTIES,
                                         aWarning,
                                         aParams, aParamsLength,
                                         doc ? doc->GetDocumentURI() : nsnull,
                                         EmptyString(), 0, 0,
                                         nsIScriptError::warningFlag,
                                         "SVG");
}

float
nsSVGUtils::CoordToFloat(nsPresContext *aPresContext,
                         nsSVGElement *aContent,
                         const nsStyleCoord &aCoord)
{
  switch (aCoord.GetUnit()) {
  case eStyleUnit_Factor:
    // user units
    return aCoord.GetFactorValue();

  case eStyleUnit_Coord:
    return nsPresContext::AppUnitsToFloatCSSPixels(aCoord.GetCoordValue());

  case eStyleUnit_Percent: {
      nsSVGSVGElement* ctx = aContent->GetCtx();
      return ctx ? aCoord.GetPercentValue() * ctx->GetLength(nsSVGUtils::XY) : 0.0f;
    }
  default:
    return 0.0f;
  }
}

PRBool
nsSVGUtils::EstablishesViewport(nsIContent *aContent)
{
  return aContent && aContent->GetNameSpaceID() == kNameSpaceID_SVG &&
           (aContent->Tag() == nsGkAtoms::svg ||
            aContent->Tag() == nsGkAtoms::image ||
            aContent->Tag() == nsGkAtoms::foreignObject ||
            aContent->Tag() == nsGkAtoms::symbol);
}

already_AddRefed<nsIDOMSVGElement>
nsSVGUtils::GetNearestViewportElement(nsIContent *aContent)
{
  nsIContent *element = GetParentElement(aContent);

  while (element && element->GetNameSpaceID() == kNameSpaceID_SVG) {
    if (EstablishesViewport(element)) {
      if (element->Tag() == nsGkAtoms::foreignObject) {
        return nsnull;
      }
      return nsCOMPtr<nsIDOMSVGElement>(do_QueryInterface(element)).forget();
    }
    element = GetParentElement(element);
  }
  return nsnull;
}

already_AddRefed<nsIDOMSVGElement>
nsSVGUtils::GetFarthestViewportElement(nsIContent *aContent)
{
  nsIContent *element = nsnull;
  nsIContent *ancestor = GetParentElement(aContent);

  while (ancestor && ancestor->GetNameSpaceID() == kNameSpaceID_SVG &&
                     ancestor->Tag() != nsGkAtoms::foreignObject) {
    element = ancestor;
    ancestor = GetParentElement(element);
  }

  if (element && element->Tag() == nsGkAtoms::svg) {
    return nsCOMPtr<nsIDOMSVGElement>(do_QueryInterface(element)).forget();
  }
  return nsnull;
}

gfxMatrix
nsSVGUtils::GetCTM(nsSVGElement *aElement, PRBool aScreenCTM)
{
  nsIDocument* currentDoc = aElement->GetCurrentDoc();
  if (currentDoc) {
    // Flush all pending notifications so that our frames are up to date
    currentDoc->FlushPendingNotifications(Flush_Layout);
  }

  gfxMatrix matrix = aElement->PrependLocalTransformTo(gfxMatrix());
  nsSVGElement *element = aElement;
  nsIContent *ancestor = GetParentElement(aElement);

  while (ancestor && ancestor->GetNameSpaceID() == kNameSpaceID_SVG &&
                     ancestor->Tag() != nsGkAtoms::foreignObject) {
    // ignore unknown XML elements in the SVG namespace
    if (ancestor->IsNodeOfType(nsINode::eSVG)) {
      element = static_cast<nsSVGElement*>(ancestor);
      matrix *= element->PrependLocalTransformTo(gfxMatrix()); // i.e. *A*ppend
      if (!aScreenCTM && EstablishesViewport(element)) {
        if (!element->NodeInfo()->Equals(nsGkAtoms::svg, kNameSpaceID_SVG) &&
            !element->NodeInfo()->Equals(nsGkAtoms::symbol, kNameSpaceID_SVG)) {
          NS_ERROR("New (SVG > 1.1) SVG viewport establishing element?");
          return gfxMatrix(0.0, 0.0, 0.0, 0.0, 0.0, 0.0); // singular
        }
        // XXX spec seems to say x,y translation should be undone for IsInnerSVG
        return matrix;
      }
    }
    ancestor = GetParentElement(ancestor);      
  }
  if (!aScreenCTM) {
    // didn't find a nearestViewportElement
    return gfxMatrix(0.0, 0.0, 0.0, 0.0, 0.0, 0.0); // singular
  }
  if (!ancestor || !ancestor->IsNodeOfType(nsINode::eELEMENT)) {
    return matrix;
  }
  if (ancestor->GetNameSpaceID() == kNameSpaceID_SVG) {
    if (element->Tag() != nsGkAtoms::svg) {
      return gfxMatrix(0.0, 0.0, 0.0, 0.0, 0.0, 0.0); // singular
    }
    return matrix * GetCTM(static_cast<nsSVGElement*>(ancestor), PR_TRUE);
  }
  // XXX this does not take into account CSS transform, or that the non-SVG
  // content that we've hit may itself be inside an SVG foreignObject higher up
  float x = 0.0f, y = 0.0f;
  if (currentDoc && element->NodeInfo()->Equals(nsGkAtoms::svg, kNameSpaceID_SVG)) {
    nsIPresShell *presShell = currentDoc->GetPrimaryShell();
    if (presShell) {
      nsPresContext *context = presShell->GetPresContext();
      if (context) {
        nsIFrame* frame = presShell->GetPrimaryFrameFor(element);
        nsIFrame* ancestorFrame = presShell->GetRootFrame();
        if (frame && ancestorFrame) {
          nsPoint point = frame->GetOffsetTo(ancestorFrame);
          x = nsPresContext::AppUnitsToFloatCSSPixels(point.x);
          y = nsPresContext::AppUnitsToFloatCSSPixels(point.y);
        }
      }
    }
  }
  return matrix * gfxMatrix().Translate(gfxPoint(x, y));
}

nsSVGDisplayContainerFrame*
nsSVGUtils::GetNearestSVGViewport(nsIFrame *aFrame)
{
  NS_ASSERTION(aFrame->IsFrameOfType(nsIFrame::eSVG), "SVG frame expected");
  if (aFrame->GetType() == nsGkAtoms::svgOuterSVGFrame) {
    return nsnull;
  }
  while ((aFrame = aFrame->GetParent())) {
    NS_ASSERTION(aFrame->IsFrameOfType(nsIFrame::eSVG), "SVG frame expected");
    if (aFrame->GetType() == nsGkAtoms::svgInnerSVGFrame ||
        aFrame->GetType() == nsGkAtoms::svgOuterSVGFrame) {
      return do_QueryFrame(aFrame);
    }
  }
  NS_NOTREACHED("This is not reached. It's only needed to compile.");
  return nsnull;
}

nsRect
nsSVGUtils::FindFilterInvalidation(nsIFrame *aFrame, const nsRect& aRect)
{
  PRInt32 appUnitsPerDevPixel = aFrame->PresContext()->AppUnitsPerDevPixel();
  nsIntRect rect = aRect.ToOutsidePixels(appUnitsPerDevPixel);

  while (aFrame) {
    if (aFrame->GetStateBits() & NS_STATE_IS_OUTER_SVG)
      break;

    nsSVGFilterFrame *filter = nsSVGEffects::GetFilterFrame(aFrame);
    if (filter) {
      // When we are under AttributeChanged, we can no longer get the old bbox
      // by calling GetBBox(), and we need that to set up the filter region
      // with the correct position. :-(
      //rect = filter->GetInvalidationBBox(aFrame, rect);

      // XXX [perf] As a horrible workaround, for now we just invalidate the
      // entire area of the nearest viewport establishing frame that doesnt
      // have overflow:visible. See bug 463939.
      nsSVGDisplayContainerFrame* viewportFrame = GetNearestSVGViewport(aFrame);
      while (viewportFrame && !viewportFrame->GetStyleDisplay()->IsScrollableOverflow()) {
        viewportFrame = GetNearestSVGViewport(viewportFrame);
      }
      if (!viewportFrame) {
        viewportFrame = GetOuterSVGFrame(aFrame);
      }
      if (viewportFrame->GetType() == nsGkAtoms::svgOuterSVGFrame) {
        nsRect r = viewportFrame->GetOverflowRect();
        // GetOverflowRect is relative to our border box, but we need it
        // relative to our content box.
        nsMargin bp = viewportFrame->GetUsedBorderAndPadding();
        viewportFrame->ApplySkipSides(bp);
        r.MoveBy(-bp.left, -bp.top);
        return r;
      }
      NS_ASSERTION(viewportFrame->GetType() == nsGkAtoms::svgInnerSVGFrame,
                   "Wrong frame type");
      nsSVGInnerSVGFrame* innerSvg = do_QueryFrame(static_cast<nsIFrame*>(viewportFrame));
      nsSVGDisplayContainerFrame* innerSvgParent = do_QueryFrame(viewportFrame->GetParent());
      float x, y, width, height;
      static_cast<nsSVGSVGElement*>(innerSvg->GetContent())->
        GetAnimatedLengthValues(&x, &y, &width, &height, nsnull);
      gfxRect bounds = nsSVGUtils::GetCanvasTM(innerSvgParent).
                         TransformBounds(gfxRect(x, y, width, height));
      bounds.RoundOut();
      nsIntRect r;
      if (NS_SUCCEEDED(nsSVGUtils::GfxRectToIntRect(bounds, &r))) {
        rect = r;
      } else {
        NS_NOTREACHED("Not going to invalidate the correct area");
      }
      aFrame = viewportFrame;
    }
    aFrame = aFrame->GetParent();
  }

  return rect.ToAppUnits(appUnitsPerDevPixel);
}

void
nsSVGUtils::InvalidateCoveredRegion(nsIFrame *aFrame)
{
  if (aFrame->GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD)
    return;

  nsSVGOuterSVGFrame* outerSVGFrame = nsSVGUtils::GetOuterSVGFrame(aFrame);
  NS_ASSERTION(outerSVGFrame, "no outer svg frame");
  if (outerSVGFrame)
    outerSVGFrame->InvalidateCoveredRegion(aFrame);
}

void
nsSVGUtils::UpdateGraphic(nsISVGChildFrame *aSVGFrame)
{
  nsIFrame *frame = do_QueryFrame(aSVGFrame);

  nsSVGEffects::InvalidateRenderingObservers(frame);

  if (frame->GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD)
    return;

  nsSVGOuterSVGFrame *outerSVGFrame = nsSVGUtils::GetOuterSVGFrame(frame);
  if (!outerSVGFrame) {
    NS_ERROR("null outerSVGFrame");
    return;
  }

  if (outerSVGFrame->IsRedrawSuspended()) {
    frame->AddStateBits(NS_STATE_SVG_DIRTY);
  } else {
    frame->RemoveStateBits(NS_STATE_SVG_DIRTY);

    PRBool changed = outerSVGFrame->UpdateAndInvalidateCoveredRegion(frame);
    if (changed) {
      NotifyAncestorsOfFilterRegionChange(frame);
    }
  }
}

void
nsSVGUtils::NotifyAncestorsOfFilterRegionChange(nsIFrame *aFrame)
{
  if (aFrame->GetStateBits() & NS_STATE_IS_OUTER_SVG) {
    // It would be better if we couldn't get here
    return;
  }

  aFrame = aFrame->GetParent();

  while (aFrame) {
    if (aFrame->GetStateBits() & NS_STATE_IS_OUTER_SVG)
      return;

    nsSVGFilterProperty *property = nsSVGEffects::GetFilterProperty(aFrame);
    if (property) {
      property->Invalidate();
    }
    aFrame = aFrame->GetParent();
  }
}

double
nsSVGUtils::ComputeNormalizedHypotenuse(double aWidth, double aHeight)
{
  return sqrt((aWidth*aWidth + aHeight*aHeight)/2);
}

float
nsSVGUtils::ObjectSpace(const gfxRect &aRect, const nsSVGLength2 *aLength)
{
  float fraction, axis;

  switch (aLength->GetCtxType()) {
  case X:
    axis = aRect.Width();
    break;
  case Y:
    axis = aRect.Height();
    break;
  case XY:
    axis = float(ComputeNormalizedHypotenuse(aRect.Width(), aRect.Height()));
  }

  if (aLength->IsPercentage()) {
    fraction = aLength->GetAnimValInSpecifiedUnits() / 100;
  } else
    fraction = aLength->GetAnimValue(static_cast<nsSVGSVGElement*>
                                                (nsnull));

  return fraction * axis;
}

float
nsSVGUtils::UserSpace(nsSVGElement *aSVGElement, const nsSVGLength2 *aLength)
{
  return aLength->GetAnimValue(aSVGElement);
}

float
nsSVGUtils::UserSpace(nsIFrame *aNonSVGContext, const nsSVGLength2 *aLength)
{
  return aLength->GetAnimValue(aNonSVGContext);
}

float
nsSVGUtils::AngleBisect(float a1, float a2)
{
  float delta = fmod(a2 - a1, static_cast<float>(2*M_PI));
  if (delta < 0) {
    delta += 2*M_PI;
  }
  /* delta is now the angle from a1 around to a2, in the range [0, 2*M_PI) */
  float r = a1 + delta/2;
  if (delta >= M_PI) {
    /* the arc from a2 to a1 is smaller, so use the ray on that side */
    r += M_PI;
  }
  return r;
}

nsSVGOuterSVGFrame *
nsSVGUtils::GetOuterSVGFrame(nsIFrame *aFrame)
{
  while (aFrame) {
    if (aFrame->GetStateBits() & NS_STATE_IS_OUTER_SVG) {
      return static_cast<nsSVGOuterSVGFrame*>(aFrame);
    }
    aFrame = aFrame->GetParent();
  }

  return nsnull;
}

nsIFrame*
nsSVGUtils::GetOuterSVGFrameAndCoveredRegion(nsIFrame* aFrame, nsRect* aRect)
{
  nsISVGChildFrame* svg = do_QueryFrame(aFrame);
  if (!svg)
    return nsnull;
  *aRect = svg->GetCoveredRegion();
  return GetOuterSVGFrame(aFrame);
}

gfxMatrix
nsSVGUtils::GetViewBoxTransform(float aViewportWidth, float aViewportHeight,
                                float aViewboxX, float aViewboxY,
                                float aViewboxWidth, float aViewboxHeight,
                                const nsSVGPreserveAspectRatio &aPreserveAspectRatio,
                                PRBool aIgnoreAlign)
{
  NS_ASSERTION(aViewboxWidth > 0, "viewBox width must be greater than zero!");
  NS_ASSERTION(aViewboxHeight > 0, "viewBox height must be greater than zero!");

  PRUint16 align = aPreserveAspectRatio.GetAnimValue().GetAlign();
  PRUint16 meetOrSlice = aPreserveAspectRatio.GetAnimValue().GetMeetOrSlice();

  // default to the defaults
  if (align == nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_UNKNOWN)
    align = nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_XMIDYMID;
  if (meetOrSlice == nsIDOMSVGPreserveAspectRatio::SVG_MEETORSLICE_UNKNOWN)
    meetOrSlice = nsIDOMSVGPreserveAspectRatio::SVG_MEETORSLICE_MEET;

  // alignment disabled for this matrix setup
  if (aIgnoreAlign)
    align = nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_XMINYMIN;
    
  float a, d, e, f;
  a = aViewportWidth / aViewboxWidth;
  d = aViewportHeight / aViewboxHeight;
  e = 0.0f;
  f = 0.0f;

  if (align != nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_NONE &&
      a != d) {
    if ((meetOrSlice == nsIDOMSVGPreserveAspectRatio::SVG_MEETORSLICE_MEET &&
        a < d) ||
        (meetOrSlice == nsIDOMSVGPreserveAspectRatio::SVG_MEETORSLICE_SLICE &&
        d < a)) {
      d = a;
      switch (align) {
      case nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_XMINYMIN:
      case nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_XMIDYMIN:
      case nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_XMAXYMIN:
        break;
      case nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_XMINYMID:
      case nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_XMIDYMID:
      case nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_XMAXYMID:
        f = (aViewportHeight - a * aViewboxHeight) / 2.0f;
        break;
      case nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_XMINYMAX:
      case nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_XMIDYMAX:
      case nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_XMAXYMAX:
        f = aViewportHeight - a * aViewboxHeight;
        break;
      default:
        NS_NOTREACHED("Unknown value for align");
      }
    }
    else if (
      (meetOrSlice == nsIDOMSVGPreserveAspectRatio::SVG_MEETORSLICE_MEET &&
      d < a) ||
      (meetOrSlice == nsIDOMSVGPreserveAspectRatio::SVG_MEETORSLICE_SLICE &&
      a < d)) {
      a = d;
      switch (align) {
      case nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_XMINYMIN:
      case nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_XMINYMID:
      case nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_XMINYMAX:
        break;
      case nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_XMIDYMIN:
      case nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_XMIDYMID:
      case nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_XMIDYMAX:
        e = (aViewportWidth - a * aViewboxWidth) / 2.0f;
        break;
      case nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_XMAXYMIN:
      case nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_XMAXYMID:
      case nsIDOMSVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_XMAXYMAX:
        e = aViewportWidth - a * aViewboxWidth;
        break;
      default:
        NS_NOTREACHED("Unknown value for align");
      }
    }
    else NS_NOTREACHED("Unknown value for meetOrSlice");
  }
  
  if (aViewboxX) e += -a * aViewboxX;
  if (aViewboxY) f += -d * aViewboxY;
  
  return gfxMatrix(a, 0.0f, 0.0f, d, e, f);
}

gfxMatrix
nsSVGUtils::GetCanvasTM(nsIFrame *aFrame)
{
  // XXX yuck, we really need a common interface for GetCanvasTM

  if (!aFrame->IsFrameOfType(nsIFrame::eSVG)) {
    return nsSVGIntegrationUtils::GetInitialMatrix(aFrame);
  }

  nsIAtom* type = aFrame->GetType();
  if (type == nsGkAtoms::svgForeignObjectFrame) {
    return static_cast<nsSVGForeignObjectFrame*>(aFrame)->GetCanvasTM();
  }

  nsSVGContainerFrame *containerFrame = do_QueryFrame(aFrame);
  if (containerFrame) {
    return containerFrame->GetCanvasTM();
  }

  return static_cast<nsSVGGeometryFrame*>(aFrame)->GetCanvasTM();
}

void 
nsSVGUtils::NotifyChildrenOfSVGChange(nsIFrame *aFrame, PRUint32 aFlags)
{
  nsIFrame *aKid = aFrame->GetFirstChild(nsnull);

  while (aKid) {
    nsISVGChildFrame* SVGFrame = do_QueryFrame(aKid);
    if (SVGFrame) {
      SVGFrame->NotifySVGChanged(aFlags); 
    } else {
      NS_ASSERTION(aKid->IsFrameOfType(nsIFrame::eSVG), "SVG frame expected");
      // recurse into the children of container frames e.g. <clipPath>, <mask>
      // in case they have child frames with transformation matrices
      nsSVGUtils::NotifyChildrenOfSVGChange(aKid, aFlags);
    }
    aKid = aKid->GetNextSibling();
  }
}

// ************************************************************

class SVGPaintCallback : public nsSVGFilterPaintCallback
{
public:
  virtual void Paint(nsSVGRenderState *aContext, nsIFrame *aTarget,
                     const nsIntRect* aDirtyRect)
  {
    nsISVGChildFrame *svgChildFrame = do_QueryFrame(aTarget);
    NS_ASSERTION(svgChildFrame, "Expected SVG frame here");

    nsIntRect* dirtyRect = nsnull;
    nsIntRect tmpDirtyRect;

    // aDirtyRect is in user-space pixels, we need to convert to
    // outer-SVG-frame-relative device pixels.
    if (aDirtyRect) {
      gfxMatrix userToDeviceSpace = nsSVGUtils::GetCanvasTM(aTarget);
      if (userToDeviceSpace.IsSingular()) {
        return;
      }
      gfxRect dirtyBounds = userToDeviceSpace.TransformBounds(
        gfxRect(aDirtyRect->x, aDirtyRect->y, aDirtyRect->width, aDirtyRect->height));
      dirtyBounds.RoundOut();
      if (NS_SUCCEEDED(nsSVGUtils::GfxRectToIntRect(dirtyBounds, &tmpDirtyRect))) {
        dirtyRect = &tmpDirtyRect;
      }
    }

    svgChildFrame->PaintSVG(aContext, dirtyRect);
  }
};

void
nsSVGUtils::PaintFrameWithEffects(nsSVGRenderState *aContext,
                                  const nsIntRect *aDirtyRect,
                                  nsIFrame *aFrame)
{
  nsISVGChildFrame *svgChildFrame = do_QueryFrame(aFrame);
  if (!svgChildFrame)
    return;

  float opacity = aFrame->GetStyleDisplay()->mOpacity;
  if (opacity == 0.0f)
    return;

  /* Properties are added lazily and may have been removed by a restyle,
     so make sure all applicable ones are set again. */

  nsSVGEffects::EffectProperties effectProperties =
    nsSVGEffects::GetEffectProperties(aFrame);

  PRBool isOK = PR_TRUE;
  nsSVGFilterFrame *filterFrame = effectProperties.GetFilterFrame(&isOK);

  /* Check if we need to draw anything. HasValidCoveredRect only returns
   * true for path geometry and glyphs, so basically we're traversing
   * all containers and we can only skip leaves here.
   */
  if (aDirtyRect && svgChildFrame->HasValidCoveredRect()) {
    if (filterFrame) {
      if (!aDirtyRect->Intersects(filterFrame->GetFilterBBox(aFrame, nsnull)))
        return;
    } else {
      nsRect rect = aDirtyRect->ToAppUnits(aFrame->PresContext()->AppUnitsPerDevPixel());
      if (!rect.Intersects(aFrame->GetRect()))
        return;
    }
  }

  /* SVG defines the following rendering model:
   *
   *  1. Render fill
   *  2. Render stroke
   *  3. Render markers
   *  4. Apply filter
   *  5. Apply clipping, masking, group opacity
   *
   * We follow this, but perform a couple of optimizations:
   *
   * + Use cairo's clipPath when representable natively (single object
   *   clip region).
   *
   * + Merge opacity and masking if both used together.
   */

  if (opacity != 1.0f && CanOptimizeOpacity(aFrame))
    opacity = 1.0f;

  gfxContext *gfx = aContext->GetGfxContext();
  PRBool complexEffects = PR_FALSE;

  nsSVGClipPathFrame *clipPathFrame = effectProperties.GetClipPathFrame(&isOK);
  nsSVGMaskFrame *maskFrame = effectProperties.GetMaskFrame(&isOK);

  PRBool isTrivialClip = clipPathFrame ? clipPathFrame->IsTrivial() : PR_TRUE;

  if (!isOK) {
    // Some resource is missing. We shouldn't paint anything.
    return;
  }
  
  gfxMatrix matrix;
  if (clipPathFrame || maskFrame)
    matrix = GetCanvasTM(aFrame);

  /* Check if we need to do additional operations on this child's
   * rendering, which necessitates rendering into another surface. */
  if (opacity != 1.0f || maskFrame || (clipPathFrame && !isTrivialClip)) {
    complexEffects = PR_TRUE;
    gfx->Save();
    gfx->PushGroup(gfxASurface::CONTENT_COLOR_ALPHA);
  }

  /* If this frame has only a trivial clipPath, set up cairo's clipping now so
   * we can just do normal painting and get it clipped appropriately.
   */
  if (clipPathFrame && isTrivialClip) {
    gfx->Save();
    clipPathFrame->ClipPaint(aContext, aFrame, matrix);
  }

  /* Paint the child */
  if (filterFrame) {
    SVGPaintCallback paintCallback;
    filterFrame->FilterPaint(aContext, aFrame, &paintCallback, aDirtyRect);
  } else {
    svgChildFrame->PaintSVG(aContext, aDirtyRect);
  }

  if (clipPathFrame && isTrivialClip) {
    gfx->Restore();
  }

  /* No more effects, we're done. */
  if (!complexEffects)
    return;

  gfx->PopGroupToSource();

  nsRefPtr<gfxPattern> maskSurface =
    maskFrame ? maskFrame->ComputeMaskAlpha(aContext, aFrame,
                                            matrix, opacity) : nsnull;

  nsRefPtr<gfxPattern> clipMaskSurface;
  if (clipPathFrame && !isTrivialClip) {
    gfx->PushGroup(gfxASurface::CONTENT_COLOR_ALPHA);

    nsresult rv = clipPathFrame->ClipPaint(aContext, aFrame, matrix);
    clipMaskSurface = gfx->PopGroup();

    if (NS_SUCCEEDED(rv) && clipMaskSurface) {
      // Still more set after clipping, so clip to another surface
      if (maskSurface || opacity != 1.0f) {
        gfx->PushGroup(gfxASurface::CONTENT_COLOR_ALPHA);
        gfx->Mask(clipMaskSurface);
        gfx->PopGroupToSource();
      } else {
        gfx->Mask(clipMaskSurface);
      }
    }
  }

  if (maskSurface) {
    gfx->Mask(maskSurface);
  } else if (opacity != 1.0f) {
    gfx->Paint(opacity);
  }

  gfx->Restore();
}

PRBool
nsSVGUtils::HitTestClip(nsIFrame *aFrame, const nsPoint &aPoint)
{
  nsSVGEffects::EffectProperties props =
    nsSVGEffects::GetEffectProperties(aFrame);
  if (!props.mClipPath)
    return PR_TRUE;

  nsSVGClipPathFrame *clipPathFrame = props.GetClipPathFrame(nsnull);
  if (!clipPathFrame) {
    // clipPath is not a valid resource, so nothing gets painted, so
    // hit-testing must fail.
    return PR_FALSE;
  }

  return clipPathFrame->ClipHitTest(aFrame, GetCanvasTM(aFrame), aPoint);
}

nsIFrame *
nsSVGUtils::HitTestChildren(nsIFrame *aFrame, const nsPoint &aPoint)
{
  // XXX: The frame's children are linked in a singly-linked list in document
  // order. If we were to hit test the children in this order we would need to
  // hit test *every* SVG frame, since even if we get a hit, later SVG frames
  // may lie on top of the matching frame. We really want to traverse SVG
  // frames in reverse order so we can stop at the first match. Since we don't
  // have a doubly-linked list, for the time being we traverse the
  // singly-linked list backwards by first reversing the nextSibling pointers
  // in place, and then restoring them when done.
  //
  // Note: While the child list pointers are reversed, any method which walks
  // the list would only encounter a single child!

  nsIFrame* current = nsnull;
  nsIFrame* next = aFrame->GetFirstChild(nsnull);

  nsIFrame* result = nsnull;

  // reverse sibling pointers
  while (next) {
    nsIFrame* temp = next->GetNextSibling();
    next->SetNextSibling(current);
    current = next;
    next = temp;    
  }

  // now do the backwards traversal
  while (current) {
    nsISVGChildFrame* SVGFrame = do_QueryFrame(current);
    if (SVGFrame) {
       result = SVGFrame->GetFrameForPoint(aPoint);
       if (result)
         break;
    }
    // restore current frame's sibling pointer
    nsIFrame* temp = current->GetNextSibling();
    current->SetNextSibling(next);
    next = current;
    current = temp;
  }

  // restore remaining pointers
  while (current) {
    nsIFrame* temp = current->GetNextSibling();
    current->SetNextSibling(next);
    next = current;
    current = temp;
  }

  if (result && !HitTestClip(aFrame, aPoint))
    result = nsnull;

  return result;
}

nsRect
nsSVGUtils::GetCoveredRegion(const nsFrameList &aFrames)
{
  nsRect rect;

  for (nsIFrame* kid = aFrames.FirstChild();
       kid;
       kid = kid->GetNextSibling()) {
    nsISVGChildFrame* child = do_QueryFrame(kid);
    if (child) {
      nsRect childRect = child->GetCoveredRegion();
      rect.UnionRect(rect, childRect);
    }
  }

  return rect;
}

nsRect
nsSVGUtils::ToAppPixelRect(nsPresContext *aPresContext,
                           double xmin, double ymin,
                           double xmax, double ymax)
{
  return ToAppPixelRect(aPresContext,
                        gfxRect(xmin, ymin, xmax - xmin, ymax - ymin));
}

nsRect
nsSVGUtils::ToAppPixelRect(nsPresContext *aPresContext, const gfxRect& rect)
{
  return nsRect(aPresContext->DevPixelsToAppUnits(NSToIntFloor(rect.X())),
                aPresContext->DevPixelsToAppUnits(NSToIntFloor(rect.Y())),
                aPresContext->DevPixelsToAppUnits(NSToIntCeil(rect.XMost()) - NSToIntFloor(rect.X())),
                aPresContext->DevPixelsToAppUnits(NSToIntCeil(rect.YMost()) - NSToIntFloor(rect.Y())));
}

gfxIntSize
nsSVGUtils::ConvertToSurfaceSize(const gfxSize& aSize, PRBool *aResultOverflows)
{
  gfxIntSize surfaceSize =
    gfxIntSize(PRInt32(aSize.width + 0.5), PRInt32(aSize.height + 0.5));

  *aResultOverflows = (aSize.width >= PR_INT32_MAX + 0.5 ||
                       aSize.height >= PR_INT32_MAX + 0.5 ||
                       aSize.width <= PR_INT32_MIN - 0.5 ||
                       aSize.height <= PR_INT32_MIN - 0.5);

  if (*aResultOverflows ||
      !gfxASurface::CheckSurfaceSize(surfaceSize)) {
    surfaceSize.width = PR_MIN(NS_SVG_OFFSCREEN_MAX_DIMENSION,
                               surfaceSize.width);
    surfaceSize.height = PR_MIN(NS_SVG_OFFSCREEN_MAX_DIMENSION,
                                surfaceSize.height);
    *aResultOverflows = PR_TRUE;
  }
  return surfaceSize;
}

gfxASurface *
nsSVGUtils::GetThebesComputationalSurface()
{
  if (!mThebesComputationalSurface) {
    nsRefPtr<gfxImageSurface> surface =
      new gfxImageSurface(gfxIntSize(1, 1), gfxASurface::ImageFormatARGB32);
    NS_ASSERTION(surface && !surface->CairoStatus(),
                 "Could not create offscreen surface");
    mThebesComputationalSurface = surface;
    // we want to keep this surface around
    NS_IF_ADDREF(mThebesComputationalSurface);
  }

  return mThebesComputationalSurface;
}

gfxMatrix
nsSVGUtils::ConvertSVGMatrixToThebes(nsIDOMSVGMatrix *aMatrix)
{
  if (!aMatrix) {
    return gfxMatrix();
  }
  float A, B, C, D, E, F;
  aMatrix->GetA(&A);
  aMatrix->GetB(&B);
  aMatrix->GetC(&C);
  aMatrix->GetD(&D);
  aMatrix->GetE(&E);
  aMatrix->GetF(&F);
  return gfxMatrix(A, B, C, D, E, F);
}

PRBool
nsSVGUtils::HitTestRect(const gfxMatrix &aMatrix,
                        float aRX, float aRY, float aRWidth, float aRHeight,
                        float aX, float aY)
{
  if (aMatrix.IsSingular()) {
    return PR_FALSE;
  }
  gfxContext ctx(GetThebesComputationalSurface());
  ctx.SetMatrix(aMatrix);
  ctx.NewPath();
  ctx.Rectangle(gfxRect(aRX, aRY, aRWidth, aRHeight));
  ctx.IdentityMatrix();
  return ctx.PointInFill(gfxPoint(aX, aY));
}

gfxRect
nsSVGUtils::GetClipRectForFrame(nsIFrame *aFrame,
                                float aX, float aY, float aWidth, float aHeight)
{
  const nsStyleDisplay* disp = aFrame->GetStyleDisplay();

  if (!(disp->mClipFlags & NS_STYLE_CLIP_RECT)) {
    NS_ASSERTION(disp->mClipFlags == NS_STYLE_CLIP_AUTO,
                 "We don't know about this type of clip.");
    return gfxRect(aX, aY, aWidth, aHeight);
  }

  if (disp->mOverflowX == NS_STYLE_OVERFLOW_HIDDEN ||
      disp->mOverflowY == NS_STYLE_OVERFLOW_HIDDEN) {

    nsIntRect clipPxRect =
      disp->mClip.ToOutsidePixels(aFrame->PresContext()->AppUnitsPerDevPixel());
    gfxRect clipRect =
      gfxRect(clipPxRect.x, clipPxRect.y, clipPxRect.width, clipPxRect.height);

    if (NS_STYLE_CLIP_RIGHT_AUTO & disp->mClipFlags) {
      clipRect.size.width = aWidth - clipRect.X();
    }
    if (NS_STYLE_CLIP_BOTTOM_AUTO & disp->mClipFlags) {
      clipRect.size.height = aHeight - clipRect.Y();
    }

    if (disp->mOverflowX != NS_STYLE_OVERFLOW_HIDDEN) {
      clipRect.pos.x = aX;
      clipRect.size.width = aWidth;
    }
    if (disp->mOverflowY != NS_STYLE_OVERFLOW_HIDDEN) {
      clipRect.pos.y = aY;
      clipRect.size.height = aHeight;
    }
     
    return clipRect;
  }
  return gfxRect(aX, aY, aWidth, aHeight);
}

void
nsSVGUtils::CompositeSurfaceMatrix(gfxContext *aContext,
                                   gfxASurface *aSurface,
                                   const gfxMatrix &aCTM, float aOpacity)
{
  if (aCTM.IsSingular())
    return;

  aContext->Save();
  aContext->Multiply(aCTM);
  aContext->SetSource(aSurface);
  aContext->Paint(aOpacity);
  aContext->Restore();
}

void
nsSVGUtils::CompositePatternMatrix(gfxContext *aContext,
                                   gfxPattern *aPattern,
                                   const gfxMatrix &aCTM, float aWidth, float aHeight, float aOpacity)
{
  if (aCTM.IsSingular())
    return;

  aContext->Save();
  SetClipRect(aContext, aCTM, gfxRect(0, 0, aWidth, aHeight));
  aContext->Multiply(aCTM);
  aContext->SetPattern(aPattern);
  aContext->Paint(aOpacity);
  aContext->Restore();
}

void
nsSVGUtils::SetClipRect(gfxContext *aContext,
                        const gfxMatrix &aCTM,
                        const gfxRect &aRect)
{
  if (aCTM.IsSingular())
    return;

  gfxMatrix oldMatrix = aContext->CurrentMatrix();
  aContext->Multiply(aCTM);
  aContext->Clip(aRect);
  aContext->SetMatrix(oldMatrix);
}

void
nsSVGUtils::ClipToGfxRect(nsIntRect* aRect, const gfxRect& aGfxRect)
{
  gfxRect r = aGfxRect;
  r.RoundOut();
  gfxRect r2(aRect->x, aRect->y, aRect->width, aRect->height);
  r = r.Intersect(r2);
  *aRect = nsIntRect(PRInt32(r.X()), PRInt32(r.Y()),
                     PRInt32(r.Width()), PRInt32(r.Height()));
}

nsresult
nsSVGUtils::GfxRectToIntRect(const gfxRect& aIn, nsIntRect* aOut)
{
  *aOut = nsIntRect(PRInt32(aIn.X()), PRInt32(aIn.Y()),
                    PRInt32(aIn.Width()), PRInt32(aIn.Height()));
  return gfxRect(aOut->x, aOut->y, aOut->width, aOut->height) == aIn
    ? NS_OK : NS_ERROR_FAILURE;
}

gfxRect
nsSVGUtils::GetBBox(nsIFrame *aFrame)
{
  gfxRect bbox;
  nsISVGChildFrame *svg = do_QueryFrame(aFrame);
  if (svg) {
    bbox = svg->GetBBoxContribution(gfxMatrix());
  } else {
    bbox = nsSVGIntegrationUtils::GetSVGBBoxForNonSVGFrame(aFrame);
  }
  NS_ASSERTION(bbox.Width() >= 0.0 && bbox.Height() >= 0.0, "Invalid bbox!");
  return bbox;
}

gfxRect
nsSVGUtils::GetRelativeRect(PRUint16 aUnits, const nsSVGLength2 *aXYWH,
                            const gfxRect &aBBox, nsIFrame *aFrame)
{
  float x, y, width, height;
  if (aUnits == nsIDOMSVGUnitTypes::SVG_UNIT_TYPE_OBJECTBOUNDINGBOX) {
    x = aBBox.X() + ObjectSpace(aBBox, &aXYWH[0]);
    y = aBBox.Y() + ObjectSpace(aBBox, &aXYWH[1]);
    width = ObjectSpace(aBBox, &aXYWH[2]);
    height = ObjectSpace(aBBox, &aXYWH[3]);
  } else {
    x = nsSVGUtils::UserSpace(aFrame, &aXYWH[0]);
    y = nsSVGUtils::UserSpace(aFrame, &aXYWH[1]);
    width = nsSVGUtils::UserSpace(aFrame, &aXYWH[2]);
    height = nsSVGUtils::UserSpace(aFrame, &aXYWH[3]);
  }
  return gfxRect(x, y, width, height);
}

PRBool
nsSVGUtils::CanOptimizeOpacity(nsIFrame *aFrame)
{
  nsIAtom *type = aFrame->GetType();
  if (type != nsGkAtoms::svgImageFrame &&
      type != nsGkAtoms::svgPathGeometryFrame) {
    return PR_FALSE;
  }
  if (aFrame->GetStyleSVGReset()->mFilter) {
    return PR_FALSE;
  }
  // XXX The SVG WG is intending to allow fill, stroke and markers on <image>
  if (type == nsGkAtoms::svgImageFrame) {
    return PR_TRUE;
  }
  const nsStyleSVG *style = aFrame->GetStyleSVG();
  if (style->mMarkerStart || style->mMarkerMid || style->mMarkerEnd) {
    return PR_FALSE;
  }
  if (style->mFill.mType == eStyleSVGPaintType_None ||
      style->mFillOpacity <= 0 ||
      !static_cast<nsSVGPathGeometryFrame*>(aFrame)->HasStroke()) {
    return PR_TRUE;
  }
  return PR_FALSE;
}

float
nsSVGUtils::MaxExpansion(const gfxMatrix &aMatrix)
{
  // maximum expansion derivation from
  // http://lists.cairographics.org/archives/cairo/2004-October/001980.html
  // and also implemented in cairo_matrix_transformed_circle_major_axis
  double a = aMatrix.xx;
  double b = aMatrix.yx;
  double c = aMatrix.xy;
  double d = aMatrix.yy;
  double f = (a * a + b * b + c * c + d * d) / 2;
  double g = (a * a + b * b - c * c - d * d) / 2;
  double h = a * c + b * d;
  return sqrt(f + sqrt(g * g + h * h));
}

gfxMatrix
nsSVGUtils::AdjustMatrixForUnits(const gfxMatrix &aMatrix,
                                 nsSVGEnum *aUnits,
                                 nsIFrame *aFrame)
{
  if (aFrame &&
      aUnits->GetAnimValue() == nsIDOMSVGUnitTypes::SVG_UNIT_TYPE_OBJECTBOUNDINGBOX) {
    gfxRect bbox = GetBBox(aFrame);
    return gfxMatrix().Scale(bbox.Width(), bbox.Height()) *
           gfxMatrix().Translate(gfxPoint(bbox.X(), bbox.Y())) *
           aMatrix;
  }
  return aMatrix;
}

nsIFrame*
nsSVGUtils::GetFirstNonAAncestorFrame(nsIFrame* aStartFrame)
{
  for (nsIFrame *ancestorFrame = aStartFrame; ancestorFrame;
       ancestorFrame = ancestorFrame->GetParent()) {
    if (ancestorFrame->GetType() != nsGkAtoms::svgAFrame) {
      return ancestorFrame;
    }
  }
  return nsnull;
}

#ifdef DEBUG
void
nsSVGUtils::WritePPM(const char *fname, gfxImageSurface *aSurface)
{
  FILE *f = fopen(fname, "wb");
  if (!f)
    return;

  gfxIntSize size = aSurface->GetSize();
  fprintf(f, "P6\n%d %d\n255\n", size.width, size.height);
  unsigned char *data = aSurface->Data();
  PRInt32 stride = aSurface->Stride();
  for (int y=0; y<size.height; y++) {
    for (int x=0; x<size.width; x++) {
      fwrite(data + y * stride + 4 * x + GFX_ARGB32_OFFSET_R, 1, 1, f);
      fwrite(data + y * stride + 4 * x + GFX_ARGB32_OFFSET_G, 1, 1, f);
      fwrite(data + y * stride + 4 * x + GFX_ARGB32_OFFSET_B, 1, 1, f);
    }
  }
  fclose(f);
}
#endif

/*static*/ gfxRect
nsSVGUtils::PathExtentsToMaxStrokeExtents(const gfxRect& aPathExtents,
                                          nsSVGGeometryFrame* aFrame)
{
  if (aPathExtents.Width() == 0 && aPathExtents.Height() == 0) {
    return gfxRect(0, 0, 0, 0);
  }

  // The logic here comes from _cairo_stroke_style_max_distance_from_path

  double style_expansion = 0.5;

  const nsStyleSVG* style = aFrame->GetStyleSVG();

  if (style->mStrokeLinecap == NS_STYLE_STROKE_LINECAP_SQUARE) {
    style_expansion = M_SQRT1_2;
  }

  if (style->mStrokeLinejoin == NS_STYLE_STROKE_LINEJOIN_MITER &&
      style_expansion < style->mStrokeMiterlimit) {
    style_expansion = style->mStrokeMiterlimit;
  }

  style_expansion *= aFrame->GetStrokeWidth();

  gfxMatrix ctm = aFrame->GetCanvasTM();

  double dx = style_expansion * (fabs(ctm.xx) + fabs(ctm.xy));
  double dy = style_expansion * (fabs(ctm.yy) + fabs(ctm.yx));

  gfxRect strokeExtents = aPathExtents;
  strokeExtents.Outset(dy, dx, dy, dx);
  return strokeExtents;
}

/* static */ PRBool
nsSVGUtils::IsInnerSVG(nsIContent* aContent)
{
  if (!aContent->NodeInfo()->Equals(nsGkAtoms::svg, kNameSpaceID_SVG)) {
    return PR_FALSE;
  }
  nsIContent *ancestor = GetParentElement(aContent);
  return ancestor && ancestor->GetNameSpaceID() == kNameSpaceID_SVG &&
                     ancestor->Tag() != nsGkAtoms::foreignObject;
}

// ----------------------------------------------------------------------

nsSVGRenderState::nsSVGRenderState(nsIRenderingContext *aContext) :
  mRenderMode(NORMAL), mRenderingContext(aContext)
{
  mGfxContext = aContext->ThebesContext();
}

nsSVGRenderState::nsSVGRenderState(gfxASurface *aSurface) :
  mRenderMode(NORMAL)
{
  mGfxContext = new gfxContext(aSurface);
}

nsIRenderingContext*
nsSVGRenderState::GetRenderingContext(nsIFrame *aFrame)
{
  if (!mRenderingContext) {
    nsIDeviceContext* devCtx = aFrame->PresContext()->DeviceContext();
    devCtx->CreateRenderingContextInstance(*getter_AddRefs(mRenderingContext));
    if (!mRenderingContext)
      return nsnull;
    mRenderingContext->Init(devCtx, mGfxContext);
  }
  return mRenderingContext;
}

