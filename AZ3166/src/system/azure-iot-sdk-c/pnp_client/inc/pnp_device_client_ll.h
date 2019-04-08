// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef PNPDEVICE_CLIENT_LL_H
#define PNPDEVICE_CLIENT_LL_H

#include "azure_c_shared_utility/macro_utils.h"
#include "azure_c_shared_utility/umock_c_prod.h"

#include "iothub_device_client_ll.h"
#include "pnp_client_common.h"


#ifdef __cplusplus
extern "C"
{
#endif

typedef struct PNP_DEVICE_CLIENT_LL* PNP_DEVICE_CLIENT_LL_HANDLE;

MOCKABLE_FUNCTION(, PNP_DEVICE_CLIENT_LL_HANDLE, PnP_DeviceClient_LL_CreateFromDeviceHandle, IOTHUB_DEVICE_CLIENT_LL_HANDLE, deviceHandle);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_DeviceClient_LL_RegisterInterfacesAsync, PNP_DEVICE_CLIENT_LL_HANDLE, pnpDeviceClientHandle, PNP_INTERFACE_CLIENT_CORE_HANDLE*, pnpInterfaces, unsigned int, numPnpInterfaces, PNP_INTERFACE_REGISTERED_CALLBACK, pnpInterfaceRegisteredCallback, void*, userContextCallback);
MOCKABLE_FUNCTION(, void, PnP_DeviceClient_LL_DoWork, PNP_DEVICE_CLIENT_LL_HANDLE, pnpDeviceClientHandle);
MOCKABLE_FUNCTION(, void, PnP_DeviceClient_LL_Destroy, PNP_DEVICE_CLIENT_LL_HANDLE, pnpDeviceClientHandle);


#ifdef __cplusplus
}
#endif


#endif // PNPDEVICE_CLIENT_LL_H
