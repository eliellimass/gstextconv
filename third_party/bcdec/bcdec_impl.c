/* SPDX-License-Identifier: MIT
 * Translation unit that instantiates the bcdec single-header implementation.
 * Keeping it isolated here stops the implementation macros from leaking into
 * any other compilation unit that includes bcdec.h purely for the function
 * declarations.
 */

#define BCDEC_IMPLEMENTATION
#include "bcdec.h"
