// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "azure_c_shared_utility/xlogging.h"

#include "internal/pnp_interface_core.h"
#include "internal/lock_thread_binding_stub.h"

PNP_INTERFACE_CLIENT_LL_HANDLE PnP_InterfaceClient_LL_Create(PNP_DEVICE_CLIENT_LL_HANDLE pnpDeviceClientLLHandle, const char* interfaceName, const PNP_CLIENT_READWRITE_PROPERTY_UPDATED_CALLBACK_TABLE* readwritePropertyUpdateCallbackTable, const PNP_CLIENT_COMMAND_CALLBACK_TABLE* commandCallbackTable, void* userContextCallback)
{
    PNP_INTERFACE_CLIENT_CORE_HANDLE interfaceClientCoreHandle;

    if ((pnpDeviceClientLLHandle == NULL) || (interfaceName == NULL))
    {
        LogError("Invalid parameter(s):  pnpDeviceClientLLHandle=%p, interfaceName=%p", pnpDeviceClientLLHandle, interfaceName);
        interfaceClientCoreHandle = NULL;
    }
    else
    {
        PNP_LOCK_THREAD_BINDING lockThreadBinding;
        lockThreadBinding.pnpBindingLockInit = LockBinding_LockInit_Stub;
        lockThreadBinding.pnpBindingLock = LockBinding_Lock_Stub;
        lockThreadBinding.pnpBindingUnlock = LockBinding_Unlock_Stub;
        lockThreadBinding.pnpBindingLockDeinit = LockBinding_LockDeinit_Stub;
        lockThreadBinding.pnpBindingThreadSleep = ThreadBinding_ThreadSleep_Stub;

        if ((interfaceClientCoreHandle = PnP_InterfaceClientCore_Create(&lockThreadBinding, (PNP_CLIENT_CORE_HANDLE)pnpDeviceClientLLHandle, interfaceName, readwritePropertyUpdateCallbackTable, commandCallbackTable, userContextCallback)) == NULL)
        {
            LogError("Error allocating handle");
        }
    }

    return (PNP_INTERFACE_CLIENT_LL_HANDLE)interfaceClientCoreHandle;
}

PNP_CLIENT_RESULT PnP_InterfaceClient_LL_SendTelemetryAsync(PNP_INTERFACE_CLIENT_LL_HANDLE pnpInterfaceClientLLHandle, const char* telemetryName, const unsigned char* messageData, size_t messageDataLen, PNP_CLIENT_TELEMETRY_CONFIRMATION_CALLBACK telemetryConfirmationCallback, void* userContextCallback)
{
    return PnP_InterfaceClientCore_SendTelemetryAsync((PNP_INTERFACE_CLIENT_CORE_HANDLE)pnpInterfaceClientLLHandle, telemetryName, messageData, messageDataLen, telemetryConfirmationCallback, userContextCallback);
}

PNP_CLIENT_RESULT PnP_InterfaceClient_LL_ReportReadOnlyPropertyStatusAsync(PNP_INTERFACE_CLIENT_LL_HANDLE pnpInterfaceClientLLHandle, const char* propertyName, unsigned const char* propertyData, size_t propertyDataLen, PNP_REPORTED_PROPERTY_UPDATED_CALLBACK pnpReportedPropertyCallback, void* userContextCallback)
{
    return PnP_InterfaceClientCore_ReportReadOnlyPropertyStatusAsync((PNP_INTERFACE_CLIENT_CORE_HANDLE)pnpInterfaceClientLLHandle, propertyName, propertyData, propertyDataLen, pnpReportedPropertyCallback, userContextCallback);
}

PNP_CLIENT_RESULT PnP_InterfaceClient_LL_ReportReadWritePropertyStatusAsync(PNP_INTERFACE_CLIENT_LL_HANDLE pnpInterfaceClientLLHandle, const char* propertyName, const PNP_CLIENT_READWRITE_PROPERTY_RESPONSE* pnpResponse, PNP_REPORTED_PROPERTY_UPDATED_CALLBACK pnpReportedPropertyCallback, void* userContextCallback)
{
    return PnP_InterfaceClientCore_ReportReadWritePropertyStatusAsync((PNP_INTERFACE_CLIENT_CORE_HANDLE)pnpInterfaceClientLLHandle, propertyName, pnpResponse, pnpReportedPropertyCallback, userContextCallback);
}

void PnP_InterfaceClient_LL_Destroy(PNP_INTERFACE_CLIENT_LL_HANDLE pnpInterfaceClientLLHandle)
{
    PnP_InterfaceClientCore_Destroy((PNP_INTERFACE_CLIENT_CORE_HANDLE)pnpInterfaceClientLLHandle);
}

