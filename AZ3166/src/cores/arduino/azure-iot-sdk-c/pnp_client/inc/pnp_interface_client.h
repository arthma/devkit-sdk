// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

/**  @file pnp_interface_client.h
*    @brief  PnP InterfaceClient functions.
*
*    @details  PNP_INTERFACE_CLIENT_HANDLE is used to represent a PnP interface.  Interfaces may be used 
               for receiving commands, reporting properties, updated read/write properties, and sending telemetry.
               The PNP_INTERFACE_CLIENT_HANDLE must be created first but it is not available for sending or
               receiving data until after the interface has been registered with the appropriate PnP Device client.
*/


#ifndef PNP_INTERFACE_CLIENT_H
#define PNP_INTERFACE_CLIENT_H

#include "azure_c_shared_utility/macro_utils.h"
#include "azure_c_shared_utility/umock_c_prod.h"

#include "pnp_interface_client_ll.h"
#include "pnp_device_client.h"

#ifdef __cplusplus
extern "C"
{
#endif

MOCKABLE_FUNCTION(, PNP_INTERFACE_CLIENT_HANDLE, PnP_InterfaceClient_Create, PNP_DEVICE_CLIENT_HANDLE, pnpDeviceClientHandle, const char*, interfaceName, const PNP_CLIENT_READWRITE_PROPERTY_UPDATED_CALLBACK_TABLE*, readwritePropertyUpdateCallbackTable, const PNP_CLIENT_COMMAND_CALLBACK_TABLE*, commandCallbackTable, void*, userContextCallback);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_InterfaceClient_SendTelemetryAsync, PNP_INTERFACE_CLIENT_HANDLE, pnpInterfaceClientHandle, const char*, telemetryName, const unsigned char*, messageData, size_t, messageDataLen, PNP_CLIENT_TELEMETRY_CONFIRMATION_CALLBACK, telemetryConfirmationCallback, void*, userContextCallback);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_InterfaceClient_ReportReadOnlyPropertyStatusAsync, PNP_INTERFACE_CLIENT_HANDLE, pnpInterfaceClientHandle, const char*, propertyName, unsigned const char*, propertyData, size_t, propertyDataLen, PNP_REPORTED_PROPERTY_UPDATED_CALLBACK, pnpReportedPropertyCallback, void*, userContextCallback);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_InterfaceClient_ReportReadWritePropertyStatusAsync, PNP_INTERFACE_CLIENT_HANDLE, pnpInterfaceClientHandle, const char*, propertyName, const PNP_CLIENT_READWRITE_PROPERTY_RESPONSE*, pnpResponse, PNP_REPORTED_PROPERTY_UPDATED_CALLBACK, pnpReportedPropertyCallback, void*, userContextCallback);
MOCKABLE_FUNCTION(, void, PnP_InterfaceClient_Destroy, PNP_INTERFACE_CLIENT_HANDLE, pnpInterfaceClientHandle);

#ifdef __cplusplus
}
#endif

#endif // PNP_INTERFACE_CLIENT_H
