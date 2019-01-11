// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

/**  @file pnp_device_client.h
*    @brief  PnP DeviceClient handle and functions.
*
*    @details  The PnP DeviceClient is used to interact with PnP when the underlying transport is a device client using IoTHub's convenience
               layer.  (In other words, PNP_DEVICE_CLIENT_HANDLE *IS* thread safe and must represented as an IoTHub device, not a module.)
               The PnP DeviceClient is primarily used to register an IOTHUB_DEVICE_CLIENT_HANDLE and register PNP_INTERFACE_CLIENT_CORE_HANDLE's.
*/


#ifndef PNPDEVICE_CLIENT_H 
#define PNPDEVICE_CLIENT_H

#include "azure_c_shared_utility/macro_utils.h"
#include "azure_c_shared_utility/umock_c_prod.h"

#include "iothub_device_client.h"
#include "pnp_client_common.h"

#ifdef __cplusplus
extern "C"
{
#endif


/**
* @brief    PNP_DEVICE_CLIENT_HANDLE is a handle that binds a IOTHUB_DEVICE_CLIENT_HANDLE the user has already created
            to PnP functionality.
*/
typedef struct PNP_DEVICE_CLIENT* PNP_DEVICE_CLIENT_HANDLE;

/**
* @brief    PnP_DeviceClient_CreateFromDeviceHandle creates a new PNP_DEVICE_CLIENT_HANDLE based on a pre-existing IOTHUB_DEVICE_CLIENT_HANDLE.
*
* @remarks  PnP_DeviceClient_CreateFromDeviceHandle is used when initially bringing up PnP.  Use PnP_DeviceClient_CreateFromDeviceHandle when
            the PnP maps to an an IoTHub device (as opposed to an IoTHub module).  PNP_DEVICE_CLIENT_HANDLE also guarantee thread safety at
            the PnP layer and do NOT require the application to explicitly invoke DoWork() to schedule actions.  PnP_DeviceClient_LL_CreateFromDeviceHandle 
            is to be used when thread safety is not required (or possible on very small devices) and/or you want explicitly control PnP by DoWork() operations.
*
*           Callers MUST NOT directly access deviceHandle after it is successfully passed to PnP_DeviceClient_CreateFromDeviceHandle.  The 
*           returned PNP_DEVICE_CLIENT_HANDLE effectivly owns all lifetime operations on the deviceHandle, including destruction.
*
* @param    deviceHandle            An IOTHUB_DEVICE_CLIENT_HANDLE that has been already created and bound to a specific connection string (or transport, or DPS handle, or whatever
                                    mechanism is preferred).  See remarks about its lifetime management.
*
* @returns  A PNP_DEVICE_CLIENT_HANDLE on success or NULL on failure.
*/
MOCKABLE_FUNCTION(, PNP_DEVICE_CLIENT_HANDLE, PnP_DeviceClient_CreateFromDeviceHandle, IOTHUB_DEVICE_CLIENT_HANDLE, deviceHandle);


/**
* @brief    PnP_DeviceClient_RegisterInterfacesAsync registers the specified PNP_INTERFACE_CLIENT_CORE_HANDLE handles with the PnP Service.
*
* @remarks  PnP_DeviceClient_RegisterInterfacesAsync registers (or re-registers) specified pnpInterfaces with the PnP Service.  This registration occurrs
            asychronously.  While registration is in progress, the PNP_INTERFACE_CLIENT_CORE_HANDLE that are being registered are NOT valid for sending telemetry on
            nor will they be able to receive commands.

            PnP_DeviceClient_RegisterInterfacesAsync may be invoked multiple times.  This is NOT additive.  If a PNP_INTERFACE_CLIENT_CORE_HANDLE has been registered
            on an initial call but is not passed in the pnpInterfaces subsequently, that interface will no longer be valid.

            If there are interfaces already registered for a given PnP Device in the PnP service that are not included in the pnpInterfaces, the client will
            automatically delete these interface references from the service so that PnP service clients accurately know the state of the device.
*
* @param    pnpDeviceClientHandle            A PNP_DEVICE_CLIENT_HANDLE created by PnP_DeviceClient_CreateFromDeviceHandle.
* @param    pnpInterfaces                    An array of length numPnpInterfaces of PNP_INTERFACE_CLIENT_CORE_HANDLE's to register with the service.
* @param    numPnpInterfaces                 The number of items in the pnpInterfaces array.
* @param    pnpInterfaceRegisteredCallback   User specified callback that will be invoked on registration completion or failure.  Callers should not begin sending PnP telemetry until this callback is invoked.
* @param    userContextCallback              User context that is provided to the callback.
*
* @returns  An PNP_DEVICE_CLIENT_HANDLE on success or NULL on failure.
*/
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_DeviceClient_RegisterInterfacesAsync, PNP_DEVICE_CLIENT_HANDLE, pnpDeviceClientHandle, PNP_INTERFACE_CLIENT_CORE_HANDLE*, pnpInterfaces, unsigned int, numPnpInterfaces, PNP_INTERFACE_REGISTERED_CALLBACK, pnpInterfaceRegisteredCallback, void*, userContextCallback);

/**
* @brief    PnP_DeviceClient_Destroy destroys resources associated with a PNP_DEVICE_CLIENT_HANDLE.
*
* @remarks  PnP_DeviceClient_Destroy will destroy resources created from the PnP_DeviceClient_CreateFromDeviceHandle call, including the IOTHUB_DEVICE_CLIENT_HANDLE whose
            ownership was transferred during the call to PnP_DeviceClient_CreateFromDeviceHandle().

            PnP_DeviceClient_Destroy will block until the PnP dispatcher thread has completed.  After PnP_DeviceClient_Destroy returns, there will be no further callbacks
            on any threads associated with any PNP Interfaces.
*
*/
MOCKABLE_FUNCTION(, void, PnP_DeviceClient_Destroy, PNP_DEVICE_CLIENT_HANDLE, pnpDeviceClientHandle);

#ifdef __cplusplus
}
#endif

#endif // PNPDEVICE_CLIENT_H
