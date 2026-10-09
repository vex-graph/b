// GLSL build-only adapter: shaderc glslc emits Vulkan SPIR-V, never host execution.
#pragma once
#include "adapters/adapter.h"
;;DEFINITION
/* Immutable build-only shader records. Directory inputs are borrowed; successful
 * output is owned by the caller. GLSLC selects shaderc; GLSL_BACKEND=glslang
 * explicitly selects GLSLANG_VALIDATOR instead. No automatic tool installation.
 */
;;OVERVIEW
/* MODULE: GLSL adapter exports; each record supplies name, extension, build, run,
 * tools and capabilities. All stage records use the same directory build callback.
 * Generic .glsl stage admission follows the selected compiler's native contract.
 */
extern const Adapter GLSL_ADAPTER;
extern const Adapter GLSL_VERT_ADAPTER;
extern const Adapter GLSL_FRAG_ADAPTER;
extern const Adapter GLSL_COMP_ADAPTER;
extern const Adapter GLSL_GEOM_ADAPTER;
extern const Adapter GLSL_TESC_ADAPTER;
extern const Adapter GLSL_TESE_ADAPTER;
