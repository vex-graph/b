// R adapter records; borrowed by the suite registry.
#pragma once
#include "annotation.h"
;;DEFINITION
/* Two immutable Rscript records share one parse/run implementation in r.c. */
;;OVERVIEW
/* PUBLIC RECORDS: R_ADAPTER {name="r", extension=".R"} and
 * R_LOWER_ADAPTER {name="R", extension=".r"}; both build=buildR, run=runR.
 * No functions or owned state in this header.
 */
#include "adapters/adapter.h"
extern const Adapter R_ADAPTER;
extern const Adapter R_LOWER_ADAPTER;
