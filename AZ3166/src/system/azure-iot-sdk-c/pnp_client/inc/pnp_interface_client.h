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

#include "pnp_client_common.h"

#include "pnp_device_client.h"
#include "pnp_module_client.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define PNP_REPORTED_PROPERTY_STATUS_VALUES        \
    PNP_REPORTED_PROPERTY_OK,                      \
    PNP_REPORTED_PROPERTY_ERROR_HANDLE_DESTROYED,  \
    PNP_REPORTED_PROPERTY_ERROR_OUT_OF_MEMORY,     \
    PNP_REPORTED_PROPERTY_ERROR_TIMEOUT,           \
    PNP_REPORTED_PROPERTY_ERROR                    \

/** @brief Enumeration passed in by PnP client indicating status of a property update.
*/
DEFINE_ENUM(PNP_REPORTED_PROPERTY_STATUS, PNP_REPORTED_PROPERTY_STATUS_VALUES);

#define PNP_SEND_TELEMETRY_STATUS_VALUES              \
    PNP_SEND_TELEMETRY_STATUS_OK,                     \
    PNP_SEND_TELEMETRY_STATUS_ERROR_HANDLE_DESTROYED, \
    PNP_SEND_TELEMETRY_STATUS_ERROR_OUT_OF_MEMORY,    \
    PNP_SEND_TELEMETRY_STATUS_ERROR_TIMEOUT,          \
    PNP_SEND_TELEMETRY_STATUS_ERROR                   \

/** @brief Enumeration passed in by PnP client indicating status of sending telemetry
*/
DEFINE_ENUM(PNP_SEND_TELEMETRY_STATUS, PNP_SEND_TELEMETRY_STATUS_VALUES);

/** @brief Enumeration passed in by PnP client indicating status of sending a telemetry message.
*/


#define PNP_CLIENT_READWRITE_PROPERTY_RESPONSE_VERSION_1  1

/** @brief    PNP_CLIENT_READWRITE_PROPERTY_RESPONSE is to be setup during a readwrite property update by the caller
              to notify the server of the update's status.
*/
typedef struct PNP_CLIENT_READWRITE_PROPERTY_RESPONSE_TAG
{
    /** @brief    The version of this structure (not the server version).  Currently must be PNP_CLIENT_READWRITE_PROPERTY_RESPONSE_VERSION_1. */
    int version;
    /** @brief    Pointer to the byte array of the value of property being updated. */
    unsigned const char* propertyData;
    /** @brief    Length of propertyData. */
    size_t propertyDataLen;
    /** @brief    responseVersion.  This is used for server to disambiguate calls for given property and should just be set to desiredVersion from PNP_READWRITE_PROPERTY_UPDATE_CALLBACK. */
    int responseVersion;
    /** @brief    status - which should map to appropriate HTTP status code - of property update.*/
    int statusCode;
    /** @brief    Friendly description string of current status of update. */
    const char* statusDescription;
} PNP_CLIENT_READWRITE_PROPERTY_RESPONSE;

#define PNP_CLIENT_ASYNC_COMMAND_UPDATE_VERSION_1 1 

typedef struct PNP_CLIENT_ASYNC_COMMAND_UPDATE_TAG
{
    int version; // version of this structure for SDK.  *NOT* tied to service versioning.  Currently PNP_CLIENT_ASYNC_COMMAND_UPDATE_VERSION_1.
    const char* commandName;
    const char* correlationId;
    unsigned const char* propertyData;
    size_t propertyDataLen;
    int statusCode;
} PNP_CLIENT_ASYNC_COMMAND_UPDATE;

/** @brief PNP_REPORTED_PROPERTY_UPDATED_CALLBACK is invoked when a property - either readonly or readwrite - in processed by the server.
*   @param pnpReportedStatus    The result of the property update.
*   @param userContextCallback  User specified context that will be provided to the callback.
*/
typedef void(*PNP_REPORTED_PROPERTY_UPDATED_CALLBACK)(PNP_REPORTED_PROPERTY_STATUS pnpReportedStatus, void* userContextCallback);
typedef void(*PNP_READWRITE_PROPERTY_UPDATE_CALLBACK)(unsigned const char* propertyInitial, size_t propertyInitialLen, unsigned const char* propertyDataUpdated, size_t propertyDataUpdatedLen, int desiredVersion, void* userContextCallback);
typedef void(*PNP_CLIENT_TELEMETRY_CONFIRMATION_CALLBACK)(PNP_SEND_TELEMETRY_STATUS pnpTelemetryStatus, void* userContextCallback);

#define PNP_CLIENT_READWRITE_PROPERTY_UPDATE_VERSION_1  1

typedef struct PNP_CLIENT_READWRITE_PROPERTY_UPDATED_CALLBACK_TABLE_TAG
{
    int version;
    int numCallbacks;
    const char** propertyNames;
    const PNP_READWRITE_PROPERTY_UPDATE_CALLBACK* callbacks;
} PNP_CLIENT_READWRITE_PROPERTY_UPDATED_CALLBACK_TABLE;

#define PNP_CLIENT_COMMAND_CALLBACK_VERSION_1  1

typedef struct PNP_CLIENT_COMMAND_CALLBACK_TABLE_TAG
{
    int version;
    int numCommandCallbacks;
    const char** commandNames; 
    const PNP_COMMAND_EXECUTE_CALLBACK* commandCallbacks;
    int numAsyncCommandCallbacks;
    const char** asyncCommandNames;
    const PNP_ASYNC_COMMAND_EXECUTE_CALLBACK* asyncCommandCallbacks;
} PNP_CLIENT_COMMAND_CALLBACK_TABLE;



MOCKABLE_FUNCTION(, PNP_INTERFACE_CLIENT_HANDLE, PnP_InterfaceClient_Create, const char*, interfaceName, const PNP_CLIENT_READWRITE_PROPERTY_UPDATED_CALLBACK_TABLE*, readwritePropertyUpdateCallbackTable, const PNP_CLIENT_COMMAND_CALLBACK_TABLE*, commandCallbackTable, void*, userContextCallback);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_InterfaceClient_SendTelemetryAsync, PNP_INTERFACE_CLIENT_HANDLE, pnpInterfaceClientHandle, const char*, telemetryName, const unsigned char*, messageData, size_t, messageDataLen, PNP_CLIENT_TELEMETRY_CONFIRMATION_CALLBACK, telemetryConfirmationCallback, void*, userContextCallback);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_InterfaceClient_ReportReadOnlyPropertyStatusAsync, PNP_INTERFACE_CLIENT_HANDLE, pnpInterfaceClientHandle, const char*, propertyName, unsigned const char*, propertyData, size_t, propertyDataLen, PNP_REPORTED_PROPERTY_UPDATED_CALLBACK, pnpReportedPropertyCallback, void*, userContextCallback);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_InterfaceClient_ReportReadWritePropertyStatusAsync, PNP_INTERFACE_CLIENT_HANDLE, pnpInterfaceClientHandle, const char*, propertyName, const PNP_CLIENT_READWRITE_PROPERTY_RESPONSE*, pnpResponse, PNP_REPORTED_PROPERTY_UPDATED_CALLBACK, pnpReportedPropertyCallback, void*, userContextCallback);
MOCKABLE_FUNCTION(, PNP_CLIENT_RESULT, PnP_InterfaceClient_UpdateAsyncCommandStatus, PNP_INTERFACE_CLIENT_CORE_HANDLE, pnpInterfaceClientHandle, const PNP_CLIENT_ASYNC_COMMAND_UPDATE*, pnpClientAsyncCommandUpdate);
MOCKABLE_FUNCTION(, void, PnP_InterfaceClient_Destroy, PNP_INTERFACE_CLIENT_HANDLE, pnpInterfaceClientHandle);

#ifdef __cplusplus
}
#endif

#endif // PNP_INTERFACE_CLIENT_H
