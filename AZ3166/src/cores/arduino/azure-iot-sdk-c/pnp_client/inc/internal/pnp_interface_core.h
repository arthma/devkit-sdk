// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef PNP_INTERFACE_CLIENT_CORE_H 
#define PNP_INTERFACE_CLIENT_CORE_H 

#include "azure_c_shared_utility/macro_utils.h"
#include "azure_c_shared_utility/umock_c_prod.h"

#include "pnp_interface_client.h"
#include "lock_thread_binding.h"

#ifdef __cplusplus
#include <cstddef>
extern "C"
{
#include <stdbool.h>
#endif

#ifndef PNP_CLIENT_CORE_HANDLE_TYPE_DEFINED
#define PNP_CLIENT_CORE_HANDLE_TYPE_DEFINED
typedef struct PNP_CLIENT_CORE* PNP_CLIENT_CORE_HANDLE;
#endif

typedef struct PNP_LOCK_THREAD_BINDING_TAG
{
    // Lock and callbacks for interacting with the binding lock logic.  For _LL_ layer, these are no-ops.        
    LOCK_HANDLE pnpBindingLockHandle;
    PNP_BINDING_LOCK_INIT pnpBindingLockInit;
    PNP_BINDING_LOCK pnpBindingLock;
    PNP_BINDING_UNLOCK pnpBindingUnlock;
    PNP_BINDING_LOCK_DEINIT pnpBindingLockDeinit;
    PNP_BINDING_THREAD_SLEEP pnpBindingThreadSleep;
} PNP_LOCK_THREAD_BINDING;

#define PNP_COMMAND_PROCESSOR_RESULT_VALUES  \
    PNP_COMMAND_PROCESSOR_ERROR,              \
    PNP_COMMAND_PROCESSOR_NOT_APPLICABLE,     \
    PNP_COMMAND_PROCESSOR_COMMAND_NOT_FOUND,  \
    PNP_COMMAND_PROCESSOR_PROCESSED

DEFINE_ENUM(PNP_COMMAND_PROCESSOR_RESULT, PNP_COMMAND_PROCESSOR_RESULT_VALUES);

MOCKABLE_FUNCTION(, PNP_INTERFACE_CLIENT_CORE_HANDLE, PnP_InterfaceClientCore_Create, PNP_LOCK_THREAD_BINDING*, lockThreadBinding,  PNP_CLIENT_CORE_HANDLE, pnpClientCoreHandle, const char*, interfaceName, const PNP_CLIENT_READWRITE_PROPERTY_UPDATED_CALLBACK_TABLE*, readwritePropertyUpdateCallbackTable, const PNP_CLIENT_COMMAND_CALLBACK_TABLE*, commandCallbackTable, void*, userContextCallback);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_InterfaceClientCore_SendTelemetryAsync, PNP_INTERFACE_CLIENT_CORE_HANDLE, pnpInterfaceClientHandle, const char*, telemetryName, const unsigned char*, messageData, size_t, messageDataLen, PNP_CLIENT_TELEMETRY_CONFIRMATION_CALLBACK, telemetryConfirmationCallback, void*, userContextCallback);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_InterfaceClientCore_ReportReadOnlyPropertyStatusAsync, PNP_INTERFACE_CLIENT_CORE_HANDLE, pnpInterfaceClientHandle, const char*, propertyName, unsigned const char*, propertyData, size_t, propertyDataLen, PNP_REPORTED_PROPERTY_UPDATED_CALLBACK, pnpReportedPropertyCallback, void*, userContextCallback);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_InterfaceClientCore_ReportReadWritePropertyStatusAsync, PNP_INTERFACE_CLIENT_CORE_HANDLE, pnpInterfaceClientHandle, const char*, propertyName, const PNP_CLIENT_READWRITE_PROPERTY_RESPONSE*, pnpResponse, PNP_REPORTED_PROPERTY_UPDATED_CALLBACK, pnpReportedPropertyCallback, void*, userContextCallback);
MOCKABLE_FUNCTION(, void, PnP_InterfaceClientCore_Destroy, PNP_INTERFACE_CLIENT_CORE_HANDLE, pnpInterfaceClientHandle);

MOCKABLE_FUNCTION(, const char*, PnP_InterfaceClientCore_GetInterfaceName, PNP_INTERFACE_CLIENT_CORE_HANDLE, pnpInterfaceClientHandle);
MOCKABLE_FUNCTION(, const char*, PnP_InterfaceClientCore_GetRawInterfaceName, PNP_INTERFACE_CLIENT_CORE_HANDLE, pnpInterfaceClientHandle);

MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_InterfaceClientCore_ProcessTwinCallback, PNP_INTERFACE_CLIENT_CORE_HANDLE, pnpInterfaceClientHandle, bool, fullTwin, const unsigned char*, payLoad, size_t, size);
MOCKABLE_FUNCTION(, PNP_COMMAND_PROCESSOR_RESULT, PnP_InterfaceClientCore_InvokeCommandIfSupported, PNP_INTERFACE_CLIENT_CORE_HANDLE, pnpInterfaceClientHandle, const char*, method_name, const unsigned char*, payload, size_t, size, unsigned char**, response, size_t*, response_size, int*, responseCode);

MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_InterfaceClientCore_MarkRegistered, PNP_INTERFACE_CLIENT_CORE_HANDLE, pnpInterfaceClientHandle);
MOCKABLE_FUNCTION(, void, PnP_InterfaceClientCore_MarkUnregistered, PNP_INTERFACE_CLIENT_CORE_HANDLE, pnpInterfaceClientHandle);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_InterfaceClientCore_ProcessTelemetryCallback, PNP_INTERFACE_CLIENT_CORE_HANDLE, pnpInterfaceClientHandle, PNP_SEND_TELEMETRY_STATUS, pnpSendTelemetryStatus, void*, userContextCallback);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_InterfaceClientCore_ProcessReportedPropertiesUpdateCallback, PNP_INTERFACE_CLIENT_CORE_HANDLE, pnpInterfaceClientHandle, PNP_REPORTED_PROPERTY_STATUS, pnpReportedStatus, void*, userContextCallback);

#ifdef __cplusplus
}
#endif

#endif // PNP_INTERFACE_CLIENT_CORE_H 

