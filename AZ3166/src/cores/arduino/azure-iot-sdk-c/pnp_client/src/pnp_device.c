// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdio.h>
#include <stdlib.h>

#include "iothub_device_client.h"
#include "azure_c_shared_utility/xlogging.h"
#include "azure_c_shared_utility/gballoc.h"
#include "pnp_device_client.h"

#include "internal/lock_thread_binding_impl.h"
#include "internal/pnp_interface_core.h"
#include "internal/pnp_client_core.h"

static int DeviceClientSendEventAsync(void* iothubClientHandle, IOTHUB_MESSAGE_HANDLE eventMessageHandle, IOTHUB_CLIENT_EVENT_CONFIRMATION_CALLBACK eventConfirmationCallback, void* userContextCallback)
{
    IOTHUB_CLIENT_RESULT iothubClientResult;
    int result;

    if ((iothubClientResult = IoTHubDeviceClient_SendEventAsync((IOTHUB_DEVICE_HANDLE)iothubClientHandle, eventMessageHandle, eventConfirmationCallback, userContextCallback)) != IOTHUB_CLIENT_OK)
    {
        LogError("IoTHubDeviceClient_SendEventAsync failed, error=%d", iothubClientResult);
        result = __FAILURE__;
    }
    else
    {
        result = 0;
    }
   
    return result;
}

static int DeviceClientSetDeviceTwinCallback(void* iothubClientHandle, IOTHUB_CLIENT_DEVICE_TWIN_CALLBACK deviceTwinCallback, void* userContextCallback)
{
    IOTHUB_CLIENT_RESULT iothubClientResult;
    int result;

    if ((iothubClientResult = IoTHubDeviceClient_SetDeviceTwinCallback((IOTHUB_DEVICE_HANDLE)iothubClientHandle, deviceTwinCallback, userContextCallback)) != IOTHUB_CLIENT_OK)
    {
        LogError("IoTHubDeviceClient_SetDeviceTwinCallback failed, error = %d", iothubClientResult);
        result = __FAILURE__;
    }
    else
    {
        result = 0;
    }

    return result;
}

static int DeviceClientSendReportedState(void* iothubClientHandle, const unsigned char* reportedState, size_t size, IOTHUB_CLIENT_REPORTED_STATE_CALLBACK reportedStateCallback, void* userContextCallback)
{
    IOTHUB_CLIENT_RESULT iothubClientResult;
    int result;

    if ((iothubClientResult = IoTHubDeviceClient_SendReportedState((IOTHUB_DEVICE_HANDLE)iothubClientHandle, reportedState, size, reportedStateCallback, userContextCallback)) != IOTHUB_CLIENT_OK)
    {
        LogError("DeviceClientSendReportedState failed, error = %d", iothubClientResult);
        result = __FAILURE__;
    }
    else
    {
        result = 0;
    }

    return result;
}

static int DeviceClientSetDeviceMethodCallback(void* iothubClientHandle, IOTHUB_CLIENT_DEVICE_METHOD_CALLBACK_ASYNC deviceMethodCallback, void* userContextCallback)
{
    IOTHUB_CLIENT_RESULT iothubClientResult;
    int result;
    if ((iothubClientResult = IoTHubDeviceClient_SetDeviceMethodCallback((IOTHUB_DEVICE_HANDLE)iothubClientHandle, deviceMethodCallback, userContextCallback)) != IOTHUB_CLIENT_OK)
    {
        LogError("IoTHubDeviceClient_SetDeviceMethodCallback failed, error = %d", iothubClientResult);
        result = __FAILURE__;
    }
    else
    {
        result = 0;
    }

    return result;
}

static void DeviceClientDoWork(void* iothubClientHandle)
{
    (void)iothubClientHandle;
    // This should never be called, because pnp_device never exposes out the wiring to reach this.
    LogError("DoWork is not supported for convenience layer");
}

static void DeviceClientDestroy(void* iothubClientHandle)
{
    IoTHubDeviceClient_Destroy((IOTHUB_DEVICE_CLIENT_HANDLE)iothubClientHandle);
}

PNP_DEVICE_CLIENT_HANDLE PnP_DeviceClient_CreateFromDeviceHandle(IOTHUB_DEVICE_CLIENT_HANDLE deviceHandle)
{
    PNP_CLIENT_CORE_HANDLE pnpClientCoreHandle;

    if (deviceHandle == NULL)
    {
        LogError("DeviceHandle is NULL");
        pnpClientCoreHandle = NULL;
    }
    else 
    {
        PNP_IOTHUB_BINDING iothubBinding;
        iothubBinding.iothubClientHandle = deviceHandle;
        iothubBinding.pnpDeviceSendEventAsync = DeviceClientSendEventAsync;
        iothubBinding.pnpDeviceSetDeviceTwinCallback = DeviceClientSetDeviceTwinCallback;
        iothubBinding.pnpSendReportedState = DeviceClientSendReportedState;
        iothubBinding.pnpDeviceClientDestroy = DeviceClientDestroy;
        iothubBinding.pnpDeviceClientDoWork = DeviceClientDoWork;
        iothubBinding.pnpBindingLockHandle = NULL;
        iothubBinding.pnpBindingLockInit = LockBinding_LockInit_Impl;
        iothubBinding.pnpBindingLock = LockBinding_Lock_Impl;
        iothubBinding.pnpBindingUnlock = LockBinding_Unlock_Impl;
        iothubBinding.pnpBindingLockDeinit = LockBinding_LockDeinit_Impl;
        iothubBinding.pnpBindingThreadSleep = ThreadBinding_ThreadSleep_Impl;
        iothubBinding.pnpDeviceSetDeviceMethodCallback = DeviceClientSetDeviceMethodCallback;

        if ((pnpClientCoreHandle = PnP_ClientCore_Create(&iothubBinding)) == NULL)
        {
            LogError("Failed allocating PnP device client");
        }
    }

    return (PNP_DEVICE_CLIENT_HANDLE)pnpClientCoreHandle;
}

PNP_CLIENT_RESULT PnP_DeviceClient_RegisterInterfacesAsync(PNP_DEVICE_CLIENT_HANDLE pnpDeviceClientHandle, PNP_INTERFACE_CLIENT_HANDLE* pnpInterfaces, unsigned int numPnpInterfaces, PNP_INTERFACE_REGISTERED_CALLBACK pnpInterfaceRegisteredCallback, void* userContextCallback)
{
    return PnP_ClientCore_RegisterInterfacesAsync((PNP_CLIENT_CORE_HANDLE)pnpDeviceClientHandle, pnpInterfaces, numPnpInterfaces, pnpInterfaceRegisteredCallback, userContextCallback);
}

void PnP_DeviceClient_Destroy(PNP_DEVICE_CLIENT_HANDLE pnpDeviceClientHandle)
{
    PnP_ClientCore_Destroy((PNP_CLIENT_CORE_HANDLE)pnpDeviceClientHandle);
}

