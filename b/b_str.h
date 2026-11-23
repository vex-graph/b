#ifndef B_STR_H
#define B_STR_H

#include "vex.h"
#include "re/primitive/vex-string.h"

/** Compare two NUL-terminated UTF-8 strings for exact equality. */
static inline bool bStrEq(string a, string b)
{
    return string_equals(a, b);
}

/** Lexicographically compare two NUL-terminated UTF-8 strings. */
static inline i32 bStrCmp(string a, string b)
{
    return string_compare(a, b);
}

/** Return true when text begins with the NUL-terminated prefix. */
static inline bool bStrPrefix(string text, string prefix)
{
    return string_startsWith(text, prefix);
}

/** Return true when text begins with the first length bytes of prefix. */
static inline bool bStrPrefixN(string text, string prefix, bytes length)
{
    return string_startsWith(text, prefix, length);
}

/** Find the first occurrence of needle in haystack, or return nullptr. */
static inline string bStrFind(string haystack, string needle)
{
    bytes idx = string_indexOf(haystack, needle);
    return idx == SIZE_MAX ? nullptr : haystack + idx;
}

/** Find the first occurrence of needle in a mutable haystack, or return nullptr. */
static inline utf8 *bStrFindMut(utf8 *haystack, string needle)
{
    bytes idx = string_indexOf(haystack, needle);
    return idx == SIZE_MAX ? nullptr : haystack + idx;
}

/** Find the first occurrence of ch in haystack, or return nullptr. */
static inline string bStrFindChar(string haystack, utf8 ch)
{
    bytes idx = string_findChar(haystack, ch);
    return idx == SIZE_MAX ? nullptr : haystack + idx;
}

/** Find the first occurrence of ch in a mutable haystack, or return nullptr. */
static inline utf8 *bStrFindCharMut(utf8 *haystack, utf8 ch)
{
    bytes idx = string_findChar(haystack, ch);
    return idx == SIZE_MAX ? nullptr : haystack + idx;
}

#endif
