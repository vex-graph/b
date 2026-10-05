// R adapter records; borrowed by the suite registry.
#pragma once
#include "annotation.h"
;;DEFINITION
/* Two immutable Rscript records share one parse/run implementation in r.c. */
;;OVERVIEW
/* PUBLIC RECORDS: R_LANGUAGE {name="r", extension=".R"} and
 * R_LOWER_LANGUAGE {name="R", extension=".r"}; both build=buildR, run=runR.
 * No functions or owned state in this header.
 */
#include "languages/language.h"
extern const Language R_LANGUAGE;
extern const Language R_LOWER_LANGUAGE;
