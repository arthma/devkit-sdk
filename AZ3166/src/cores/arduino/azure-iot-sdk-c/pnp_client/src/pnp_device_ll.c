// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdio.h>
#include <stdlib.h>

#include "iothub_device_client_ll.h"
#include "azure_c_shared_utility/xlogging.h"
#include "azure_c_shared_utility/gballoc.h"

#include "pnp_device_client_ll.h"

#include "internal/lock_thread_binding_stub.h"
#include "internal/pnp_interface_core.h"
#include "internal/pnp_client_core.h"

static int DeviceClient_LL_SendEventAsync(void* iothubClientHandle, IOTHUB_MESSAGE_HANDLE eventMessageHandle, IOTHUB_CLIENT_EVENT_CONFIRMATION_CALLBACK eventConfirmationCallback, void* userContextCallback)
{
    IOTHUB_CLIENT_RESULT iothubClientResult;
    int result;

    if ((iothubClientResult = IoTHubDeviceClient_LL_SendEventAsync((IOTHUB_DEVICE_CLIENT_LL_HANDLE)iothubClientHandle, eventMessageHandle, eventConfirmationCallback, userContextCallback)) != IOTHUB_CLIENT_OK)
    {
        LogError("IoTHubDeviceClient_LL_SendEventAsync failed, error=%d", iothubClientResult);
        result = __FAILURE__;
    }
    else
    {
        result = 0;
    }
   
    return result;
}

static int DeviceClientSetDeviceLLTwinCallback(void* iothubClientHandle, IOTHUB_CLIENT_DEVICE_TWIN_CALLBACK deviceTwinCallback, void* userContextCallback)
{
    IOTHUB_CLIENT_RESULT iothubClientResult;
    int result;

    if ((iothubClientResult = IoTHubDeviceClient_LL_SetDeviceTwinCallback((IOTHUB_DEVICE_CLIENT_LL_HANDLE)iothubClientHandle, deviceTwinCallback, userContextCallback)) != IOTHUB_CLIENT_OK)
    {
        LogError("IoTHubDeviceClient_LL_SetDeviceTwinCallback failed, error = %d", iothubClientResult);
        result = __FAILURE__;
    }
    else
    {
        result = 0;
    }

    return result;
}

static int DeviceClient_LL_SendReportedState(void* iothubClientHandle, const unsigned char* reportedState, size_t size, IOTHUB_CLIENT_REPORTED_STATE_CALLBACK reportedStateCallback, void* userContextCallback)
{
    IOTHUB_CLIENT_RESULT iothubClientResult;
    int result;

    if ((iothubClientResult = IoTHubDeviceClient_LL_SendReportedState((IOTHUB_DEVICE_CLIENT_LL_HANDLE)iothubClientHandle, reportedState, size, reportedStateCallback, userContextCallback)) != IOTHUB_CLIENT_OK)
    {
        LogError("DeviceClient_LL_SendReportedState failed, error = %d", iothubClientResult);
        result = __FAILURE__;
    }
    else
    {
        result = 0;
    }

    return result;
}

static int DeviceClient_LL_SetDeviceMethodCallback(void* iothubClientHandle, IOTHUB_CLIENT_DEVICE_METHOD_CALLBACK_ASYNC deviceMethodCallback, void* userContextCallback)
{
    IOTHUB_CLIENT_RESULT iothubClientResult;
    int result;
    if ((iothubClientResult = IoTHubDeviceClient_LL_SetDeviceMethodCallback((IOTHUB_DEVICE_CLIENT_LL_HANDLE)iothubClientHandle, deviceMethodCallback, userContextCallback)) != IOTHUB_CLIENT_OK)
    {
        LogError("IoTHubDeviceClient_LL_SetDeviceMethodCallback failed, error = %d", iothubClientResult);
        result = __FAILURE__;
    }
    else
    {
        result = 0;
    }

    return result;
}

static void DeviceClient_LL_Destroy(void* iothubClientHandle)
{
    IoTHubDeviceClient_LL_Destroy((IOTHUB_DEVICE_CLIENT_LL_HANDLE)iothubClientHandle);
}

static void DeviceClient_LL_DoWork(void* iothubClientHandle)
{
    IoTHubDeviceClient_LL_DoWork((IOTHUB_DEVICE_CLIENT_LL_HANDLE)iothubClientHandle);
}

PNP_DEVICE_CLIENT_LL_HANDLE PnP_DeviceClient_LL_CreateFromDeviceHandle(IOTHUB_DEVICE_CLIENT_LL_HANDLE deviceLLHandle)
{
    PNP_CLIENT_CORE_HANDLE pnpClientCoreHandle;

    if (deviceLLHandle == NULL)
    {
        LogError("DeviceLLHandle is NULL");
        pnpClientCoreHandle = NULL;
    }
    else 
    {
        PNP_IOTHUB_BINDING iothubBinding;
        iothubBinding.iothubClientHandle = deviceLLHandle;
        iothubBinding.pnpDeviceSendEventAsync = DeviceClient_LL_SendEventAsync;
        iothubBinding.pnpDeviceSetDeviceTwinCallback = DeviceClientSetDeviceLLTwinCallback;
        iothubBinding.pnpSendReportedState = DeviceClient_LL_SendReportedState;
        iothubBinding.pnpDeviceClientDestroy = DeviceClient_LL_Destroy;
        iothubBinding.pnpDeviceClientDoWork = DeviceClient_LL_DoWork;
        iothubBinding.pnpBindingLockHandle = NULL;
		iothubBinding.pnpBindingLockInit = LockBinding_LockInit_Stub;
        iothubBinding.pnpBindingLock = LockBinding_Lock_Stub;
        iothubBinding.pnpBindingUnlock = LockBinding_Unlock_Stub;
        iothubBinding.pnpBindingLockDeinit = LockBinding_LockDeinit_Stub;
        iothubBinding.pnpBindingThreadSleep = ThreadBinding_ThreadSleep_Stub;
        iothubBinding.pnpDeviceSetDeviceMethodCallback = DeviceClient_LL_SetDeviceMethodCallback;

        if ((pnpClientCoreHandle = PnP_ClientCore_Create(&iothubBinding)) == NULL)
        {
            LogError("Failed allocating PnP device client");
        }
    }

    return (PNP_DEVICE_CLIENT_LL_HANDLE)pnpClientCoreHandle;
}

PNP_CLIENT_RESULT PnP_DeviceClient_LL_RegisterInterfacesAsync(PNP_DEVICE_CLIENT_LL_HANDLE pnpDeviceClientHandle, PNP_INTERFACE_CLIENT_HANDLE* pnpInterfaces, unsigned int numPnpInterfaces, PNP_INTERFACE_REGISTERED_CALLBACK pnpInterfaceRegisteredCallback, void* userContextCallback)
{
    return PnP_ClientCore_RegisterInterfacesAsync((PNP_CLIENT_CORE_HANDLE)pnpDeviceClientHandle, pnpInterfaces, numPnpInterfaces, pnpInterfaceRegisteredCallback, userContextCallback);
}

void PnP_DeviceClient_LL_DoWork(PNP_DEVICE_CLIENT_LL_HANDLE pnpDeviceClientHandle)
{
    PnP_ClientCore_DoWork((PNP_CLIENT_CORE_HANDLE)pnpDeviceClientHandle);
}

void PnP_DeviceClient_LL_Destroy(PNP_DEVICE_CLIENT_LL_HANDLE pnpDeviceClientHandle)
{
    PnP_ClientCore_Destroy((PNP_CLIENT_CORE_HANDLE)pnpDeviceClientHandle);
}

