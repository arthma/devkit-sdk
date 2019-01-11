// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef PNP_RAW_INTERFACE_H
#define PNP_RAW_INTERFACE_H

#include <stdlib.h>

#include "azure_c_shared_utility/macro_utils.h"
#include "azure_c_shared_utility/umock_c_prod.h"

#ifdef __cplusplus
extern "C"
{
#endif

MOCKABLE_FUNCTION(, const char*, PnP_Get_RawInterfaceId, const char*, pnpInterface);

#ifdef __cplusplus
}
#endif


#endif
