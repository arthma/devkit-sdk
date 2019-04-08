// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef PNP_CLIENT_CORE_H
#define PNP_CLIENT_CORE_H

#include "azure_c_shared_utility/macro_utils.h"
#include "azure_c_shared_utility/umock_c_prod.h"

#include "azure_c_shared_utility/lock.h"

#include "iothub_client_core_common.h"
#include "pnp_client_common.h"
#include "lock_thread_binding.h"

#ifdef __cplusplus
#include <cstddef>
extern "C"
{
#include <stdbool.h>
#endif

// PNP_CLIENT_CORE implements the core logic of PnP that is agnostic to the type of handle
// (e.g. _LL_ versus convenience, and devices verus module) that the caller is processing.
#ifndef PNP_CLIENT_CORE_HANDLE_TYPE_DEFINED
#define PNP_CLIENT_CORE_HANDLE_TYPE_DEFINED
typedef struct PNP_CLIENT_CORE* PNP_CLIENT_CORE_HANDLE;
#endif

// Callbacks used by PNP_IOTHUB_BINDING.
typedef int(*PNP_CLIENT_SEND_EVENT_ASYNC)(void* iothubClientHandle, IOTHUB_MESSAGE_HANDLE eventMessageHandle, IOTHUB_CLIENT_EVENT_CONFIRMATION_CALLBACK eventConfirmationCallback, void* userContextCallback);
typedef int(*PNP_CLIENT_SET_TWIN_CALLBACK)(void* iothubClientHandle, IOTHUB_CLIENT_DEVICE_TWIN_CALLBACK deviceTwinCallback, void* userContextCallback);
typedef int(*PNP_CLIENT_SEND_REPORTED_STATE)(void* iothubClientHandle, const unsigned char* reportedState, size_t size, IOTHUB_CLIENT_REPORTED_STATE_CALLBACK reportedStateCallback, void* userContextCallback);
typedef int(*PNP_CLIENT_SET_METHOD_CALLBACK)(void* iothubClientHandle, IOTHUB_CLIENT_DEVICE_METHOD_CALLBACK_ASYNC deviceMethodCallback, void* userContextCallback);
typedef void(*PNP_CLIENT_DESTROY)(void* iothubClientHandle);
typedef void(*PNP_CLIENT_DOWORK)(void* iothubClientHandle);

// PnP uses dependency injection via the PNP_IOTHUB_BINDING structure.  The upper layer (e.g. PNP_DEVICE_CLIENT_HANDLE) will pass in callbacks to
// interact with its specific type of handle (e.g. IOTHUB_DEVICE_CLIENT_HANDLE's) and the PnPClientCore invokes these to interact with IoTHub.
//
// We use this paradigm because PnPClientCore can't take link-time dependencies on any of these specific device handles.  Otherwise a application building
// an _LL_ based PnP client would end up having the IoTHub convenience layer brought in.  This also makes it easier to abstract
// whether we're using a Device or (to be implemented later) a Module.
typedef struct PNP_IOTHUB_BINDING_TAG
{
    // Context and callbacks for interacting with IoTHub client
    void* iothubClientHandle;
    PNP_CLIENT_SEND_EVENT_ASYNC pnpClientSendEventAsync;
    PNP_CLIENT_SET_TWIN_CALLBACK pnpClientSetTwinCallback;
    PNP_CLIENT_SEND_REPORTED_STATE pnpClientSendReportedState;
    PNP_CLIENT_SET_METHOD_CALLBACK pnpClientSetMethodCallback;
    PNP_CLIENT_DOWORK pnpClientDoWork;
    PNP_CLIENT_DESTROY pnpClientDestroy;
} PNP_IOTHUB_BINDING;

// Functions that upper layers invoke to use PnPClientCore.  
// The actual application itself never accesses a PNP_CLIENT_CORE_HANDLE directly, but instead gets to it
// via a public API handle they create (e.g. PNP_DEVICE_CLIENT_HANDLE).
MOCKABLE_FUNCTION(, PNP_CLIENT_CORE_HANDLE, PnP_ClientCore_Create, PNP_IOTHUB_BINDING*, iotHubBinding, PNP_LOCK_THREAD_BINDING*, lockThreadBinding);
MOCKABLE_FUNCTION(, void, PnP_ClientCore_Destroy, PNP_CLIENT_CORE_HANDLE, pnpClientCoreHandle);
MOCKABLE_FUNCTION(, void, PnP_ClientCore_DoWork, PNP_CLIENT_CORE_HANDLE, pnpClientCoreHandle);

MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_ClientCore_RegisterInterfacesAsync, PNP_CLIENT_CORE_HANDLE, pnpClientCoreHandle, PNP_INTERFACE_CLIENT_CORE_HANDLE*, pnpInterfaces , unsigned int, numPnpInterfaces, PNP_INTERFACE_REGISTERED_CALLBACK, pnpInterfaceRegisteredCallback, void*, userContextCallback);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_ClientCore_SendTelemetryAsync, PNP_CLIENT_CORE_HANDLE, pnpClientCoreHandle, PNP_INTERFACE_CLIENT_CORE_HANDLE, pnpInterfaceClientHandle, IOTHUB_MESSAGE_HANDLE, telemetryMessageHandle, void*, userContextCallback);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_ClientCore_ReportPropertyStatusAsync, PNP_CLIENT_CORE_HANDLE, pnpClientCoreHandle, PNP_INTERFACE_CLIENT_CORE_HANDLE, pnpInterfaceClientHandle, const unsigned char*, dataToSend, size_t, dataToSendLen, void*, userContextCallback);

#ifdef __cplusplus
}
#endif

#endif // PNP_CLIENT_CORE_H

