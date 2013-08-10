/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/TextDecoder.h"
#include "mozilla/dom/EncodingUtils.h"
#include "nsContentUtils.h"
#include "nsICharsetConverterManager.h"
#include "nsServiceManagerUtils.h"

namespace mozilla {
namespace dom {

static const PRUnichar kReplacementChar = static_cast<PRUnichar>(0xFFFD);

void
TextDecoder::Init(const nsAString& aEncoding,
                  const TextDecoderOptions& aFatal,
                  ErrorResult& aRv)
{
  nsAutoString label(aEncoding);
  EncodingUtils::TrimSpaceCharacters(label);

  // Let encoding be the result of getting an encoding from label.
  // If encoding is failure, throw a TypeError.
  if (!EncodingUtils::FindEncodingForLabel(label, mEncoding)) {
    aRv.ThrowTypeError(MSG_ENCODING_NOT_SUPPORTED, &label);
    return;
  }

  if (mEncoding.EqualsLiteral("UTF-16")) {
    mEncoding.AssignLiteral("UTF-16LE");
  }

  mIsUTF16Family = mEncoding.EqualsLiteral("UTF-16LE") ||
                   mEncoding.EqualsLiteral("UTF-16BE");

  // If the constructor is called with an options argument,
  // and the fatal property of the dictionary is set,
  // set the internal fatal flag of the decoder object.
  mFatal = aFatal.fatal;

  // Create a decoder object for mEncoding.
  nsCOMPtr<nsICharsetConverterManager> ccm =
    do_GetService(NS_CHARSETCONVERTERMANAGER_CONTRACTID);
  if (!ccm) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return;
  }

  ccm->GetUnicodeDecoderRaw(mEncoding.get(), getter_AddRefs(mDecoder));
  if (!mDecoder) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return;
  }

  if (mFatal) {
    mDecoder->SetInputErrorBehavior(nsIUnicodeDecoder::kOnError_Signal);
  }
}

void
TextDecoder::ResetDecoder()
{
  mDecoder->Reset();
  mOffset = 0;
}

void
TextDecoder::Decode(const ArrayBufferView* aView,
                    const TextDecodeOptions& aOptions,
                    nsAString& aOutDecodedString,
                    ErrorResult& aRv)
{
  const char* data;
  uint32_t length;
  // If view is not specified, let view be a Uint8Array of length 0.
  if (!aView) {
    data = EmptyCString().BeginReading();
    length = EmptyCString().Length();
  } else {
    data = reinterpret_cast<const char*>(aView->Data());
    length = aView->Length();
  }

  aOutDecodedString.Truncate();
  if (mIsUTF16Family && mOffset < 2) {
    HandleBOM(data, length, aOptions, aOutDecodedString, aRv);
    if (aRv.Failed() || mOffset < 2) {
      return;
    }
  }

  // Run or resume the decoder algorithm of the decoder object's encoder.
  int32_t outLen;
  nsresult rv = mDecoder->GetMaxLength(data, length, &outLen);
  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
    return;
  }
  // Need a fallible allocator because the caller may be a content
  // and the content can specify the length of the string.
  static const fallible_t fallible = fallible_t();
  nsAutoArrayPtr<PRUnichar> buf(new (fallible) PRUnichar[outLen + 1]);
  if (!buf) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  for (;;) {
    int32_t srcLen = length;
    int32_t dstLen = outLen;
    rv = mDecoder->Convert(data, &srcLen, buf, &dstLen);
    // Convert will convert the input partially even if the status
    // indicates a failure.
    buf[dstLen] = 0;
    aOutDecodedString.Append(buf, dstLen);
    if (mFatal || rv != NS_ERROR_ILLEGAL_INPUT) {
      break;
    }
    // Emit a decode error manually because some decoders
    // do not support kOnError_Recover (bug 638379)
    if (srcLen == -1) {
      ResetDecoder();
    } else {
      data += srcLen + 1;
      length -= srcLen + 1;
      aOutDecodedString.Append(kReplacementChar);
    }
  }

  // If the internal streaming flag of the decoder object is not set,
  // then reset the encoding algorithm state to the default values
  if (!aOptions.stream) {
    ResetDecoder();
    if (rv == NS_OK_UDEC_MOREINPUT) {
      if (mFatal) {
        aRv.Throw(NS_ERROR_DOM_ENCODING_DECODE_ERR);
      } else {
        // Need to emit a decode error manually
        // to simulate the EOF handling of the Encoding spec.
        aOutDecodedString.Append(kReplacementChar);
      }
    }
  }

  if (NS_FAILED(rv)) {
    aRv.Throw(NS_ERROR_DOM_ENCODING_DECODE_ERR);
  }
}

void
TextDecoder::HandleBOM(const char*& aData, uint32_t& aLength,
                       const TextDecodeOptions& aOptions,
                       nsAString& aOutString, ErrorResult& aRv)
{
  if (aLength < 2u - mOffset) {
    if (aOptions.stream) {
      memcpy(mInitialBytes + mOffset, aData, aLength);
      mOffset += aLength;
    } else if (mFatal) {
      aRv.Throw(NS_ERROR_DOM_ENCODING_DECODE_ERR);
    } else {
      aOutString.Append(kReplacementChar);
    }
    return;
  }

  memcpy(mInitialBytes + mOffset, aData, 2 - mOffset);
  // copied data will be fed later.
  aData += 2 - mOffset;
  aLength -= 2 - mOffset;
  mOffset = 2;

  nsAutoCString encoding;
  if (!EncodingUtils::IdentifyDataOffset(mInitialBytes, 2, encoding) ||
      !encoding.Equals(mEncoding)) {
    // If the stream doesn't start with BOM or the BOM doesn't match the
    // encoding, feed a BOM to workaround decoder's bug (bug 634541).
    FeedBytes(mEncoding.EqualsLiteral("UTF-16LE") ? "\xFF\xFE" : "\xFE\xFF");
  }
  FeedBytes(mInitialBytes, &aOutString);
}

void
TextDecoder::FeedBytes(const char* aBytes, nsAString* aOutString)
{
  PRUnichar buf[3];
  int32_t srcLen = mOffset;
  int32_t dstLen = mozilla::ArrayLength(buf);
  DebugOnly<nsresult> rv =
    mDecoder->Convert(aBytes, &srcLen, buf, &dstLen);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  MOZ_ASSERT(srcLen == mOffset);
  if (aOutString) {
    aOutString->Assign(buf, dstLen);
  }
}

void
TextDecoder::GetEncoding(nsAString& aEncoding)
{
  CopyASCIItoUTF16(mEncoding, aEncoding);
  nsContentUtils::ASCIIToLower(aEncoding);
}

NS_IMPL_CYCLE_COLLECTING_ADDREF(TextDecoder)
NS_IMPL_CYCLE_COLLECTING_RELEASE(TextDecoder)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(TextDecoder)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_1(TextDecoder, mGlobal)

} // dom
} // mozilla
