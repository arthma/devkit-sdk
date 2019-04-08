// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef PNP_CLIENT_VERSION_H
#define PNP_CLIENT_VERSION_H

#include "azure_c_shared_utility/macro_utils.h"
#include "azure_c_shared_utility/umock_c_prod.h"

#define PNP_CLIENT_SDK_VERSION "0.0.1"

#include "azure_c_shared_utility/umock_c_prod.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief   Returns a pointer to a null terminated string containing the
     *          current PnP Client SDK version.
     *
     * @return  Pointer to a null terminated string containing the
     *          current PnP  Client SDK version.
     */
    MOCKABLE_FUNCTION(, const char*, PnP_Client_Version);

#ifdef __cplusplus
}
#endif

#endif // PNP_CLIENT_VERSION_H
