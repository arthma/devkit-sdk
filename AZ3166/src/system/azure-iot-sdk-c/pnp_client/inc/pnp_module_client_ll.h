// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef PNPMODULE_CLIENT_LL_H
#define PNPMODULE_CLIENT_LL_H

#include "azure_c_shared_utility/macro_utils.h"
#include "azure_c_shared_utility/umock_c_prod.h"

#include "iothub_module_client_ll.h"
#include "pnp_client_common.h"


#ifdef __cplusplus
extern "C"
{
#endif

typedef struct PNP_MODULE_CLIENT_LL* PNP_MODULE_CLIENT_LL_HANDLE;

MOCKABLE_FUNCTION(, PNP_MODULE_CLIENT_LL_HANDLE, PnP_ModuleClient_LL_CreateFromModuleHandle, IOTHUB_MODULE_CLIENT_LL_HANDLE, moduleHandle);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_ModuleClient_LL_RegisterInterfacesAsync, PNP_MODULE_CLIENT_LL_HANDLE, pnpModuleClientHandle, PNP_INTERFACE_CLIENT_CORE_HANDLE*, pnpInterfaces, unsigned int, numPnpInterfaces, PNP_INTERFACE_REGISTERED_CALLBACK, pnpInterfaceRegisteredCallback, void*, userContextCallback);
MOCKABLE_FUNCTION(, void, PnP_ModuleClient_LL_DoWork, PNP_MODULE_CLIENT_LL_HANDLE, pnpModuleClientHandle);
MOCKABLE_FUNCTION(, void, PnP_ModuleClient_LL_Destroy, PNP_MODULE_CLIENT_LL_HANDLE, pnpModuleClientHandle);


#ifdef __cplusplus
}
#endif


#endif // PNPMODULE_CLIENT_LL_H
