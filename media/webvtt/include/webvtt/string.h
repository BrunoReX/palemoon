/**
 * Copyright (c) 2013 Mozilla Foundation and Contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __WEBVTT_STRING_H__
# define __WEBVTT_STRING_H__
# include "util.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/**
* webvtt_string - A buffer of utf16 characters
*/
typedef struct webvtt_string_t webvtt_string;
typedef struct webvtt_string_data_t webvtt_string_data;
typedef struct webvtt_stringlist_t webvtt_stringlist;
struct webvtt_string_data_t;

struct
webvtt_string_t {
  webvtt_string_data *d;
};

/**
 * webvtt_init_string
 *
 * initialize a string to point to the empty string
 */
WEBVTT_EXPORT void
webvtt_init_string( webvtt_string *result );

/**
 * webvtt_string_is_empty
 *
 * return whether or not the string is empty
 * qualifications for it being empty are it equaling &empty_string or its
 * length equaling 0
 */
WEBVTT_EXPORT webvtt_uint
webvtt_string_is_empty( const webvtt_string *str );

/**
 * webvtt_create_string
 *
 * allocate a new string object with an initial capacity of 'alloc'
 * (the string data of 'result' is not expected to contain string data,
 * regardless of its value. be sure to release existing strings before using
 * webvtt_create_string)
 */
WEBVTT_EXPORT webvtt_status
webvtt_create_string( webvtt_uint32 alloc, webvtt_string *result );

/**
 * webvtt_create_init_string
 *
 * allocate and initialize a string with the contents of 'init_text' of length
 * 'len' if 'len' < 0, assume init_text to be null-terminated.
 */
WEBVTT_EXPORT webvtt_status
webvtt_create_string_with_text( webvtt_string *out, const char *init_text,
                                int len );

/**
 * webvtt_ref_string
 *
 * increase the reference count of--or retain--a string
 *
 * when the reference count drops to zero, the string is deallocated.
 */
WEBVTT_EXPORT void
webvtt_ref_string( webvtt_string *str );

/**
 * webvtt_release_string
 *
 * decrease the reference count of--or release--a string
 *
 * when the reference count drops to zero, the string is deallocated.
 */
WEBVTT_EXPORT void
webvtt_release_string( webvtt_string *str );

/**
 * webvtt_string_detach
 *
 * ensure that the reference count of a string is exactly 1
 *
 * if the reference count is greater than 1, allocate a new copy of the string
 * and return it.
 */
WEBVTT_EXPORT webvtt_status
webvtt_string_detach( webvtt_string *str );

/**
 * webvtt_copy_string
 *
 * shallow-clone 'right', storing the result in 'left'.
 */
WEBVTT_EXPORT void
webvtt_copy_string( webvtt_string *left, const webvtt_string *right );

/**
 * webvtt_string_text
 *
 * return the text contents of a string
 */
WEBVTT_EXPORT const char *
webvtt_string_text( const webvtt_string *str );

/**
 * webvtt_string_length
 *
 * return the length of a strings text
 */
WEBVTT_EXPORT webvtt_uint32
webvtt_string_length( const webvtt_string *str );

/**
 * webvtt_string_capacity
 *
 * return the current capacity of a string
 */
WEBVTT_EXPORT webvtt_uint32
webvtt_string_capacity( const webvtt_string *str );

/**
 * webvtt_string_getline
 *
 * collect a line of text (terminated by CR/LF/CRLF) from a buffer, without
 * including the terminating character(s)
 */
WEBVTT_EXPORT int
webvtt_string_getline( webvtt_string *str, const char *buffer,
                       webvtt_uint *pos, int len, int *truncate,
                       webvtt_bool finish );

/**
 * webvtt_string_putc
 *
 * append a single byte to a webvtt string
 */
WEBVTT_EXPORT webvtt_status
webvtt_string_putc( webvtt_string *str, char to_append );

/**
 * webvtt_string_replace
 *
 * replace first instance of substring with replacement string.
 */
WEBVTT_EXPORT webvtt_status
webvtt_string_replace( webvtt_string *str, const char *search, int search_len,
                       const char *replace, int replace_len );

/**
 * webvtt_string_replace_all
 *
 * replace all instances of substring with replacement string
 */
WEBVTT_EXPORT webvtt_status
webvtt_string_replace_all( webvtt_string *str, const char *search,
                           int search_len, const char *replace,
                           int replace_len );

/**
 * webvtt_string_is_equal
 *
 * compare a string's text to a byte array
 *
 */
WEBVTT_EXPORT webvtt_bool
webvtt_string_is_equal( const webvtt_string *str, const char *to_compare,
                        int len );

/**
 * webvtt_string_append
 *
 * append a stream of bytes to the string.
 *
 * if 'len' is < 0, then buffer is expected to be null-terminated.
 */
WEBVTT_EXPORT webvtt_status
webvtt_string_append( webvtt_string *str, const char *buffer, int len );

/**
 * webvtt_string_appendstr
 *
 * if 'len' is < 0 then the max length of the string will be taken to be the
 * first occurence of a null byte character
 */
WEBVTT_EXPORT webvtt_status
webvtt_string_append_string( webvtt_string *str, const webvtt_string *other );

/**
 * basic dynamic array of strings
 */
struct
webvtt_stringlist_t {
  struct webvtt_refcount_t refs;
  webvtt_uint alloc;
  webvtt_uint length;
  webvtt_string *items;
};

/**
 * webvtt_create_stringlist
 *
 * allocate a new, empty stringlist
 */
WEBVTT_EXPORT webvtt_status
webvtt_create_stringlist( webvtt_stringlist **result );

/**
 * webvtt_ref_stringlist
 *
 * Increase the ref count of the stringlist
 */
WEBVTT_EXPORT void
webvtt_ref_stringlist( webvtt_stringlist *list );

/**
 * webvtt_copy_stringlist
 *
 * create a copy shallow of right from left
 */
WEBVTT_EXPORT void
webvtt_copy_stringlist( webvtt_stringlist **left, webvtt_stringlist *right );

/**
 * webvtt_release_stringlist
 *
 * Decrease the ref count of the stringlist and delete it if the ref count is 0
 */
WEBVTT_EXPORT void
webvtt_release_stringlist( webvtt_stringlist **list );

/**
 * webvtt_stringlist_push
 *
 * add a new string to the end of the stringlist
 */
WEBVTT_EXPORT webvtt_status
webvtt_stringlist_push( webvtt_stringlist *list, webvtt_string *str );

/**
 * webvtt_stringlist_pop
 *
 * pop the top of the string list
 */
WEBVTT_EXPORT webvtt_bool
webvtt_stringlist_pop( webvtt_stringlist *list, webvtt_string *out );

/**
 * Helper functions
 */

/**
 * webvtt_next_utf8
 *
 * move the 'begin' pointer to the beginning of the next utf8 character
 * sequence.
 */
WEBVTT_EXPORT webvtt_bool
webvtt_next_utf8( const char **begin, const char *end );

/**
 * webvtt_skip_utf8
 *
 * move the 'begin' pointer to the beginning of the utf8 character
 * 'n_chars' away.
 *
 * if 'end' is less than 'begin', will seek backwards.
 */
WEBVTT_EXPORT webvtt_bool
webvtt_skip_utf8( const char **begin, const char *end, int n_chars );

/**
 * webvtt_utf8_to_utf16
 *
 * return the utf16 value of a given character
 */
WEBVTT_EXPORT webvtt_uint16
webvtt_utf8_to_utf16( const char *utf8, const char *end, webvtt_uint16 *high );

/**
 * webvtt_utf8_chcount
 *
 * return the number of Unicode characters (as opposed to units)
 * in a utf8 string
 */
WEBVTT_EXPORT int
webvtt_utf8_chcount( const char *utf8, const char *end );

/**
 * webvtt_utf8_length
 *
 * if 'utf8' points to a lead byte, return the length of the sequence.
 * if 'utf8' is null, return 0.
 * if 'utf8' points to a trail byte, return -1
 */
WEBVTT_EXPORT int
webvtt_utf8_length( const char *utf8 );

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif
#endif
