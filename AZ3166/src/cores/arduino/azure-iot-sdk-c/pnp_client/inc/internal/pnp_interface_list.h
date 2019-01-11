// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef PNP_INTERFACE_LIST_H
#define PNP_INTERFACE_LIST_H

#include "azure_c_shared_utility/macro_utils.h"
#include "azure_c_shared_utility/umock_c_prod.h"

#include "internal/pnp_interface_core.h"

#ifdef __cplusplus
#include <cstddef>
extern "C"
{
#else
#include <stddef.h>
#include <stdbool.h>
#endif

typedef struct PNP_INTERFACE_LIST* PNP_INTERFACE_LIST_HANDLE;

MOCKABLE_FUNCTION(, PNP_INTERFACE_LIST_HANDLE, Pnp_InterfaceList_Create);
MOCKABLE_FUNCTION(, void, PnP_InterfaceList_Destroy, PNP_INTERFACE_LIST_HANDLE, pnpInterfaceListHandle);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_InterfaceList_RegisterInterfaces, PNP_INTERFACE_LIST_HANDLE, pnpInterfaceListHandle, PNP_INTERFACE_CLIENT_CORE_HANDLE*, pnpInterfaces, unsigned int, numPnpInterfaces);
MOCKABLE_FUNCTION(, void, PnP_InterfaceList_UnregisterHandles, PNP_INTERFACE_LIST_HANDLE, pnpInterfaceListHandle);
MOCKABLE_FUNCTION(, PNP_COMMAND_PROCESSOR_RESULT, PnP_InterfaceList_InvokeCommand, PNP_INTERFACE_LIST_HANDLE, pnpInterfaceListHandle, const char*, method_name, const unsigned char*, payload, size_t, size, unsigned char**, response, size_t*, response_size, int*, resultFromCommandCallback);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_InterfaceList_ProcessTwinCallbackForRegistration, PNP_INTERFACE_LIST_HANDLE, pnpInterfaceListHandle, bool, fullTwin, const unsigned char*, payLoad, size_t, size);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_InterfaceList_ProcessTwinCallbackForProperties, PNP_INTERFACE_LIST_HANDLE, pnpInterfaceClientHandle, bool, fullTwin, const unsigned char*, payLoad, size_t, size);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_InterfaceList_ProcessTelemetryCallback, PNP_INTERFACE_LIST_HANDLE, pnpInterfaceListHandle, PNP_INTERFACE_CLIENT_HANDLE, pnpInterfaceClientHandle, PNP_SEND_TELEMETRY_STATUS, pnpSendTelemetryStatus, void*, userContextCallback);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_InterfaceList_GetInterface_Data, PNP_INTERFACE_LIST_HANDLE, pnpInterfaceListHandle, char**, jsonToSend, size_t*, jsonToSendLen);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_InterfaceList_ProcessReportedPropertiesUpdateCallback, PNP_INTERFACE_LIST_HANDLE, pnpInterfaceListHandle, PNP_INTERFACE_CLIENT_HANDLE, pnpInterfaceClientHandle, PNP_REPORTED_PROPERTY_STATUS, pnpReportedStatus, void*, userContextCallback);

#ifdef __cplusplus
}
#endif

#endif // PNP_INTERFACE_LIST_H

