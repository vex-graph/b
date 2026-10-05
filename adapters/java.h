#pragma once
#include "annotation.h"
;;DEFINITION
/* Immutable JDK source/class adapter contract; behavior lives in java.c. */
;;OVERVIEW
/* PUBLIC RECORD: JAVA_ADAPTER; fields name="java", extension=".java",
 * build=buildJava, run=runJava. No functions or owned state in this header.
 */
#include "adapters/adapter.h"
extern const Adapter JAVA_ADAPTER;
