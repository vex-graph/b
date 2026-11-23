#ifndef B_STR_H
#define B_STR_H

#include "vex.h"
#include "re/primitive/char_sequence.h"

/** Compare two NUL-terminated UTF-8 strings for exact equality. */
static inline bool bStrEq(const utf8 *a, const utf8 *b) {
    return CharSequence_equals(CharSequence_cstr(a), CharSequence_cstr(b));
}

/** Lexicographically compare two NUL-terminated UTF-8 strings. */
static inline i32 bStrCmp(const utf8 *a, const utf8 *b) {
    return CharSequence_compare(CharSequence_cstr(a), CharSequence_cstr(b));
}

/** Return true when text begins with the NUL-terminated prefix. */
static inline bool bStrPrefix(const utf8 *text, const utf8 *prefix) {
    return CharSequence_startsWith(CharSequence_cstr(text), CharSequence_cstr(prefix));
}

/** Return true when text begins with the first length bytes of prefix. */
static inline bool bStrPrefixN(const utf8 *text, const utf8 *prefix, bytes length) {
    return CharSequence_startsWith(CharSequence_cstr(text), CharSequence_2(prefix, length));
}

/** Find the first occurrence of needle in haystack, or return nullptr. */
static inline const utf8 *bStrFind(const utf8 *haystack, const utf8 *needle) {
    bytes idx = CharSequence_find(CharSequence_cstr(haystack), CharSequence_cstr(needle));
    return idx == SIZE_MAX ? nullptr : haystack + idx;
}

/** Find the first occurrence of needle in a mutable haystack, or return nullptr. */
static inline utf8 *bStrFindMut(utf8 *haystack, const utf8 *needle) {
    bytes idx = CharSequence_find(CharSequence_cstr(haystack), CharSequence_cstr(needle));
    return idx == SIZE_MAX ? nullptr : haystack + idx;
}

/** Find the first occurrence of ch in haystack, or return nullptr. */
static inline const utf8 *bStrFindChar(const utf8 *haystack, utf8 ch) {
    bytes idx = CharSequence_findChar(CharSequence_cstr(haystack), ch);
    return idx == SIZE_MAX ? nullptr : haystack + idx;
}

/** Find the first occurrence of ch in a mutable haystack, or return nullptr. */
static inline utf8 *bStrFindCharMut(utf8 *haystack, utf8 ch) {
    bytes idx = CharSequence_findChar(CharSequence_cstr(haystack), ch);
    return idx == SIZE_MAX ? nullptr : haystack + idx;
}

#endif
