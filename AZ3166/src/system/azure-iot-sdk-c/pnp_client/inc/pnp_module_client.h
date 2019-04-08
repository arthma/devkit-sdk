// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

/**  @file pnp_module_client.h
*    @brief  PnP ModuleClient handle and functions.
*
*    @details  The PnP ModuleClient is used to interact with PnP when the underlying transport is a module client using IoTHub's convenience
               layer.  (In other words, PNP_MODULE_CLIENT_HANDLE *IS* thread safe and must represented as an IoTHub module.)
               The PnP ModuleClient is primarily used to register an IOTHUB_MODULE_CLIENT_HANDLE and register PNP_INTERFACE_CLIENT_CORE_HANDLE's.
*/


#ifndef PNPMODULE_CLIENT_H
#define PNPMODULE_CLIENT_H

#include "azure_c_shared_utility/macro_utils.h"
#include "azure_c_shared_utility/umock_c_prod.h"

#include "iothub_module_client.h"
#include "pnp_client_common.h"

#ifdef __cplusplus
extern "C"
{
#endif


/**
* @brief    PNP_MODULE_CLIENT_HANDLE is a handle that binds a IOTHUB_MODULE_CLIENT_HANDLE the user has already created
            to PnP functionality.
*/
typedef void* PNP_MODULE_CLIENT_HANDLE;

/**
* @brief    PnP_ModuleClient_CreateFromModuleHandle creates a new PNP_MODULE_CLIENT_HANDLE based on a pre-existing IOTHUB_MODULE_CLIENT_HANDLE.
*
* @remarks  PnP_ModuleClient_CreateFromModuleHandle is used when initially bringing up PnP.  Use PnP_ModuleClient_CreateFromModuleHandle when
            the PnP maps to an an IoTHub module (as opposed to an IoTHub device).  PNP_MODULE_CLIENT_HANDLE also guarantee thread safety at
            the PnP layer and do NOT require the application to explicitly invoke DoWork() to schedule actions.  PnP_ModuleClient_LL_CreateFromModuleHandle 
            is to be used when thread safety is not required (or possible on very small devices) and/or you want explicitly control PnP by DoWork() operations.
*
*           Callers MUST NOT directly access moduleHandle after it is successfully passed to PnP_ModuleClient_CreateFromModuleHandle.  The 
*           returned PNP_MODULE_CLIENT_HANDLE effectivly owns all lifetime operations on the moduleHandle, including destruction.
*
* @param    moduleHandle            An IOTHUB_MODULE_CLIENT_HANDLE that has been already created and bound to a specific connection string (or transport, or DPS handle, or whatever
                                    mechanism is preferred).  See remarks about its lifetime management.
*
* @returns  A PNP_MODULE_CLIENT_HANDLE on success or NULL on failure.
*/
MOCKABLE_FUNCTION(, PNP_MODULE_CLIENT_HANDLE, PnP_ModuleClient_CreateFromModuleHandle, IOTHUB_MODULE_CLIENT_HANDLE, moduleHandle);


/**
* @brief    PnP_ModuleClient_RegisterInterfacesAsync registers the specified PNP_INTERFACE_CLIENT_CORE_HANDLE handles with the PnP Service.
*
* @remarks  PnP_ModuleClient_RegisterInterfacesAsync registers specified pnpInterfaces with the PnP Service.  This registration occurrs
            asychronously.  While registration is in progress, the PNP_INTERFACE_CLIENT_HANDLE's that are being registered are NOT valid for sending telemetry on
            nor will they be able to receive commands.

            PnP_ModuleClient_RegisterInterfacesAsync may not be called multiple times for the same PNP_MODULE_CLIENT_HANDLE.  If a given PnP module
            needs to have its handles re-registered, it needs to PnP_ModuleClient_Destroy the existing PNP_MODULE_CLIENT and create a new one.

            If there are interfaces already registered for a given PnP Module in the PnP service that are not included in the pnpInterfaces, the client will
            automatically delete these interface references from the service so that PnP service clients accurately know the state of the module.
*
* @param    pnpModuleClientHandle            A PNP_MODULE_CLIENT_HANDLE created by PnP_ModuleClient_CreateFromModuleHandle.
* @param    pnpInterfaces                    An array of length numPnpInterfaces of PNP_INTERFACE_CLIENT_CORE_HANDLE's to register with the service.
* @param    numPnpInterfaces                 The number of items in the pnpInterfaces array.
* @param    pnpInterfaceRegisteredCallback   User specified callback that will be invoked on registration completion or failure.  Callers should not begin sending PnP telemetry until this callback is invoked.
* @param    userContextCallback              User context that is provided to the callback.
*
* @returns  An PNP_MODULE_CLIENT_HANDLE on success or NULL on failure.
*/
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_ModuleClient_RegisterInterfacesAsync, PNP_MODULE_CLIENT_HANDLE, pnpModuleClientHandle, PNP_INTERFACE_CLIENT_CORE_HANDLE*, pnpInterfaces, unsigned int, numPnpInterfaces, PNP_INTERFACE_REGISTERED_CALLBACK, pnpInterfaceRegisteredCallback, void*, userContextCallback);

/**
* @brief    PnP_ModuleClient_Destroy destroys resources associated with a PNP_MODULE_CLIENT_HANDLE.
*
* @remarks  PnP_ModuleClient_Destroy will destroy resources created from the PnP_ModuleClient_CreateFromModuleHandle call, including the IOTHUB_MODULE_CLIENT_HANDLE whose
            ownership was transferred during the call to PnP_ModuleClient_CreateFromModuleHandle().

            PnP_ModuleClient_Destroy will block until the PnP dispatcher thread has completed.  After PnP_ModuleClient_Destroy returns, there will be no further callbacks
            on any threads associated with any PNP Interfaces.
*
*/
MOCKABLE_FUNCTION(, void, PnP_ModuleClient_Destroy, PNP_MODULE_CLIENT_HANDLE, pnpModuleClientHandle);

#ifdef __cplusplus
}
#endif

#endif // PNPMODULE_CLIENT_H
