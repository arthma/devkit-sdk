// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "azure_c_shared_utility/xlogging.h"

#include "internal/pnp_interface_core.h"
#include "internal/lock_thread_binding_impl.h"

PNP_INTERFACE_CLIENT_HANDLE PnP_InterfaceClient_Create(PNP_DEVICE_CLIENT_HANDLE pnpDeviceClientHandle, const char* interfaceName, const PNP_CLIENT_READWRITE_PROPERTY_UPDATED_CALLBACK_TABLE* readwritePropertyUpdateCallbackTable, const PNP_CLIENT_COMMAND_CALLBACK_TABLE* commandCallbackTable, void* userContextCallback)
{
    PNP_INTERFACE_CLIENT_CORE_HANDLE interfaceClientCoreHandle;

    if ((pnpDeviceClientHandle == NULL) || (interfaceName == NULL))
    {
        LogError("Invalid parameter(s):  pnpDeviceClientHandle=%p, interfaceName=%p", pnpDeviceClientHandle, interfaceName);
        interfaceClientCoreHandle = NULL;
    }
    else
    {
        PNP_LOCK_THREAD_BINDING lockThreadBinding;
        lockThreadBinding.pnpBindingLockInit = LockBinding_LockInit_Impl;
        lockThreadBinding.pnpBindingLock = LockBinding_Lock_Impl;
        lockThreadBinding.pnpBindingUnlock = LockBinding_Unlock_Impl;
        lockThreadBinding.pnpBindingLockDeinit = LockBinding_LockDeinit_Impl;
        lockThreadBinding.pnpBindingThreadSleep = ThreadBinding_ThreadSleep_Impl;

        if ((interfaceClientCoreHandle = PnP_InterfaceClientCore_Create(&lockThreadBinding, (PNP_CLIENT_CORE_HANDLE)pnpDeviceClientHandle, interfaceName, readwritePropertyUpdateCallbackTable, commandCallbackTable, userContextCallback)) == NULL)
        {
            LogError("Error allocating handle");
        }
    }

    return (PNP_INTERFACE_CLIENT_HANDLE)interfaceClientCoreHandle;
}

PNP_CLIENT_RESULT PnP_InterfaceClient_SendTelemetryAsync(PNP_INTERFACE_CLIENT_HANDLE pnpInterfaceClientHandle, const char* telemetryName, const unsigned char* messageData, size_t messageDataLen, PNP_CLIENT_TELEMETRY_CONFIRMATION_CALLBACK telemetryConfirmationCallback, void* userContextCallback)
{
    return PnP_InterfaceClientCore_SendTelemetryAsync((PNP_INTERFACE_CLIENT_CORE_HANDLE)pnpInterfaceClientHandle, telemetryName, messageData, messageDataLen, telemetryConfirmationCallback, userContextCallback);
}

PNP_CLIENT_RESULT PnP_InterfaceClient_ReportReadOnlyPropertyStatusAsync(PNP_INTERFACE_CLIENT_HANDLE pnpInterfaceClientHandle, const char* propertyName, unsigned const char* propertyData, size_t propertyDataLen, PNP_REPORTED_PROPERTY_UPDATED_CALLBACK pnpReportedPropertyCallback, void* userContextCallback)
{
    return PnP_InterfaceClientCore_ReportReadOnlyPropertyStatusAsync((PNP_INTERFACE_CLIENT_CORE_HANDLE)pnpInterfaceClientHandle, propertyName, propertyData, propertyDataLen, pnpReportedPropertyCallback, userContextCallback);
}

PNP_CLIENT_RESULT PnP_InterfaceClient_ReportReadWritePropertyStatusAsync(PNP_INTERFACE_CLIENT_HANDLE pnpInterfaceClientHandle, const char* propertyName, const PNP_CLIENT_READWRITE_PROPERTY_RESPONSE* pnpResponse, PNP_REPORTED_PROPERTY_UPDATED_CALLBACK pnpReportedPropertyCallback, void* userContextCallback)
{
    return PnP_InterfaceClientCore_ReportReadWritePropertyStatusAsync((PNP_INTERFACE_CLIENT_CORE_HANDLE)pnpInterfaceClientHandle, propertyName, pnpResponse, pnpReportedPropertyCallback, userContextCallback);
}

void PnP_InterfaceClient_Destroy(PNP_INTERFACE_CLIENT_HANDLE pnpInterfaceClientHandle)
{
    PnP_InterfaceClientCore_Destroy((PNP_INTERFACE_CLIENT_CORE_HANDLE)pnpInterfaceClientHandle);
}

