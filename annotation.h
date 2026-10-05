#pragma once

// Standalone, zero-runtime blueprint markers. These preserve the ecosystem's
// Two-Semicolon Annotation Style Law without a library/build dependency.
#define DEFINITION _Static_assert(1, "@Definition");
#define OVERVIEW _Static_assert(1, "@Overview");

;;DEFINITION
/* These markers expose source contracts without linking to a framework.
 * Each is a compile-time assertion, never runtime work or a behavioral proof.
 */
;;OVERVIEW
/* MODULE: standalone blueprint annotations.
 * PUBLIC MACROS: DEFINITION — architectural contract marker;
 * OVERVIEW — capability, field and function-registry marker.
 * Usage: two semicolons on the left, followed by the macro and blueprint prose.
 * No functions, class fields, allocations or process state.
 */
