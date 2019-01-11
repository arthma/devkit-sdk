// Copyright (c) Microsoft. All rights reserved. 
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdio.h>
#include <stdlib.h>

#include "azure_c_shared_utility/threadapi.h"
#include "azure_c_shared_utility/xlogging.h"
#include "azure_c_shared_utility/crt_abstractions.h"
#include "azure_c_shared_utility/gballoc.h"

#include "pnp_client_core.h"
#include "internal/pnp_raw_interface.h"
#include "internal/pnp_interface_core.h"

#include "parson.h"

static const char commandSeparator = '*';

static const char* PNP_PROPERTY_UPDATE_JSON_VERSON = "$version";

static const char* PNP_INTERFACE_INTERNAL_ID_PROPERTY = "iothub-interface-internal-id";
static const char* PNP_INTERFACE_ID_PROPERTY = "iothub-interface-id";
static const char* PNP_MESSAGE_SCHEMA_PROPERTY = "iothub-message-schema";
static const char* PNP_JSON_MESSAGE_CONTENT_TYPE = "application/json";

// PNP_INTERFACE_CLIENT_CORE corresponds to an application level handle (e.g. PNP_INTERFACE_CLIENT_HANDLE).
typedef struct PNP_INTERFACE_CLIENT_CORE_TAG
{
    PNP_LOCK_THREAD_BINDING lockThreadBinding;
    bool registeredWithClient;  // Whether this interface is registered with pnpClientCoreHandle or not.
    bool processingCallback;    // Whether we're in the middle of processing a callback or not.
    bool pendingDestroy;        // Whether a PnP_InterfaceClientCore_Destroy has been called but we can't invoke yet.
    // The remaining fields are read only after PnP_Interface_Create.  Because they can't be modified
    // (other than at create/delete time), they don't require lock when reading them.
    PNP_CLIENT_CORE_HANDLE pnpClientCoreHandle;
    void* userContextCallback;
    char* interfaceName;
    const char* rawInterfaceName;
    size_t rawInterfaceNameLen;
    PNP_CLIENT_READWRITE_PROPERTY_UPDATED_CALLBACK_TABLE* readwritePropertyUpdateCallbackTable;
    PNP_CLIENT_COMMAND_CALLBACK_TABLE* commandCallbackTable;
} PNP_INTERFACE_CLIENT_CORE;

typedef struct PNP_REPORTED_PROPERTIES_UPDATE_CALLBACK_CONTEXT_TAG
{
    PNP_REPORTED_PROPERTY_UPDATED_CALLBACK pnpReportedPropertyCallback;
    void* userContextCallback;
} PNP_REPORT_PROPERTIES_UPDATE_CALLBACK_CONTEXT;

typedef struct PNP_INTERFACE_SEND_TELEMETRY_CALLBACK_CONTEXT_TAG
{
    PNP_CLIENT_TELEMETRY_CONFIRMATION_CALLBACK telemetryConfirmationCallback;
    void* userContextCallback;
} PNP_INTERFACE_SEND_TELEMETRY_CALLBACK_CONTEXT;

DEFINE_ENUM_STRINGS(PNP_CLIENT_RESULT, PNP_CLIENT_RESULT_VALUES);
DEFINE_ENUM_STRINGS(PNP_REPORTED_PROPERTY_STATUS, PNP_REPORTED_PROPERTY_STATUS_VALUES);
DEFINE_ENUM_STRINGS(PNP_SEND_TELEMETRY_STATUS, PNP_SEND_TELEMETRY_STATUS_VALUES);
DEFINE_ENUM_STRINGS(PNP_COMMAND_PROCESSOR_RESULT, PNP_COMMAND_PROCESSOR_RESULT_VALUES);


// Invokes Lock() (for convenience layer based handles) or else a no-op (for _LL_)
static int InvokeBindingInterfaceLock(PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient, bool* lockHeld)
{
    LOCK_RESULT lockResult;
    int result;

    if ((lockResult = pnpInterfaceClient->lockThreadBinding.pnpBindingLock(pnpInterfaceClient->lockThreadBinding.pnpBindingLockHandle)) != LOCK_OK)
    {
        LogError("Unable to grab lock");
        result = __FAILURE__;
    }
    else
    {
        *lockHeld = true;
        result = 0;
    }

    return result;
}

// Invokes Unlock() (for convenience layer based handles) or else a no-op (for _LL_)
static int InvokeBindingInterfaceUnlock(PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient, bool* lockHeld)
{
    LOCK_RESULT lockResult;
    int result;
    
    if (*lockHeld == true)
    {
        lockResult = pnpInterfaceClient->lockThreadBinding.pnpBindingUnlock(pnpInterfaceClient->lockThreadBinding.pnpBindingLockHandle);
        if (lockResult != LOCK_OK)
        {
            LogError("Unlock failed, result = %d", lockResult);
            result = __FAILURE__;
        }
        else
        {
            result = 0;
        }
        // Unconditionally mark lock held as false, since even on failure no point re-invoking pnpBindingUnlock
        *lockHeld = false;
    }
    else
    {
        result = 0;
    }

    return result;
}

// Invokes Lock_Deinit() (for convenience layer based handles) or else a no-op (for _LL_)
static void InvokeBindingInterfaceLockDeinitIfNeeded(PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient)
{
    if (pnpInterfaceClient->lockThreadBinding.pnpBindingLockHandle != NULL)
    {
        pnpInterfaceClient->lockThreadBinding.pnpBindingLockDeinit(pnpInterfaceClient->lockThreadBinding.pnpBindingLockHandle);
    }
}

// Invokes Thread_Sleep (for convenience layer based handles) or else a no-op (for _LL_)
static void InvokeBindingInterfaceSleep(PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient, unsigned int milliseconds)
{
    pnpInterfaceClient->lockThreadBinding.pnpBindingThreadSleep(milliseconds);
}

// Frees PNP_CLIENT_READWRITE_PROPERTY_UPDATED_CALLBACK_TABLE
static void FreeReadWritePropertyCallbackTable(PNP_CLIENT_READWRITE_PROPERTY_UPDATED_CALLBACK_TABLE* readwritePropertyUpdateCallbackTable)
{
    if (readwritePropertyUpdateCallbackTable != NULL)
    {
        for (int i = 0; i < readwritePropertyUpdateCallbackTable->numCallbacks; i++)
        {
            free((char*)readwritePropertyUpdateCallbackTable->propertyNames[i]);
        }
        free((void*)readwritePropertyUpdateCallbackTable->propertyNames);
        free((void*)readwritePropertyUpdateCallbackTable->callbacks);
        free(readwritePropertyUpdateCallbackTable);
    }
}

// Frees PNP_CLIENT_COMMAND_CALLBACK_TABLE
static void FreeCommandPropertyCallbackTable(PNP_CLIENT_COMMAND_CALLBACK_TABLE* commandCallbackTable)
{
    if (commandCallbackTable != NULL)
    {
        for (int i = 0; i < commandCallbackTable->numCallbacks; i++)
        {
            free((char*)commandCallbackTable->commandNames[i]);
        }
        free((void*)commandCallbackTable->commandNames);
        free((void*)commandCallbackTable->callbacks);
        free(commandCallbackTable);
    }
}

// Destroys PNP_INTERFACE_CLIENT_CORE memory.
static void FreePnpInterface(PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient)
{
    InvokeBindingInterfaceLockDeinitIfNeeded(pnpInterfaceClient);
    FreeReadWritePropertyCallbackTable(pnpInterfaceClient->readwritePropertyUpdateCallbackTable);
    FreeCommandPropertyCallbackTable(pnpInterfaceClient->commandCallbackTable);
    free((char*)pnpInterfaceClient->rawInterfaceName);
    free(pnpInterfaceClient->interfaceName);
    free(pnpInterfaceClient);
}

// Retrieves a shallow copy of the interface name for caller.
const char* PnP_InterfaceClientCore_GetInterfaceName(PNP_INTERFACE_CLIENT_CORE_HANDLE pnpInterfaceClientHandle)
{
    const char* result;
    PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient = (PNP_INTERFACE_CLIENT_CORE*)pnpInterfaceClientHandle;

    if (pnpInterfaceClientHandle == NULL)
    {
        LogError("Invalid interfaceClient handle passed");
        result = NULL;
    }
    else
    {
        result = pnpInterfaceClient->interfaceName;
    }
    return result;
}

// Retrieves a shallow copy of the raw interface namefor caller.
const char* PnP_InterfaceClientCore_GetRawInterfaceName(PNP_INTERFACE_CLIENT_CORE_HANDLE pnpInterfaceClientHandle)
{
    const char* result;
    PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient = (PNP_INTERFACE_CLIENT_CORE*)pnpInterfaceClientHandle;

    if (pnpInterfaceClientHandle == NULL)
    {
        LogError("Invalid interfaceClient handle passed");
        result = NULL;
    }
    else
    {
        result = pnpInterfaceClient->rawInterfaceName;
    }
    return result;
}

// Creates a copy of the caller passed PNP_CLIENT_READWRITE_PROPERTY_UPDATED_CALLBACK_TABLE
static PNP_CLIENT_READWRITE_PROPERTY_UPDATED_CALLBACK_TABLE* CopyReadWritePropertyCallbackTable(const PNP_CLIENT_READWRITE_PROPERTY_UPDATED_CALLBACK_TABLE* readwritePropertyUpdateCallbackTable)
{
    PNP_CLIENT_READWRITE_PROPERTY_UPDATED_CALLBACK_TABLE* result;
    
    if ((result = calloc(1, sizeof(PNP_CLIENT_READWRITE_PROPERTY_UPDATED_CALLBACK_TABLE))) == NULL)
    {
        LogError("cannot allocate PNP_CLIENT_READWRITE_PROPERTY_UPDATED_CALLBACK_TABLE");
    }
    else if ((result->propertyNames = calloc(readwritePropertyUpdateCallbackTable->numCallbacks, sizeof(char*))) == NULL)
    {
        LogError("cannot allocate PNP_CLIENT_READWRITE_PROPERTY_UPDATED_CALLBACK_TABLE");
        FreeReadWritePropertyCallbackTable(result);
        result = NULL;
    }
    else if ((result->callbacks = calloc(readwritePropertyUpdateCallbackTable->numCallbacks, sizeof(PNP_READWRITE_PROPERTY_UPDATE_CALLBACK*))) == NULL)
    {
        LogError("cannot allocate PNP_CLIENT_READWRITE_PROPERTY_UPDATED_CALLBACK_TABLE");
        FreeReadWritePropertyCallbackTable(result);
        result = NULL;
    }
    else
    {
        result->version = readwritePropertyUpdateCallbackTable->version;
        result->numCallbacks = readwritePropertyUpdateCallbackTable->numCallbacks;
        memcpy((PNP_READWRITE_PROPERTY_UPDATE_CALLBACK*)result->callbacks, readwritePropertyUpdateCallbackTable->callbacks, result->numCallbacks * sizeof(PNP_READWRITE_PROPERTY_UPDATE_CALLBACK));
        
        for (int i = 0; i < readwritePropertyUpdateCallbackTable->numCallbacks; i++)
        {
            if (mallocAndStrcpy_s((char**)&result->propertyNames[i], readwritePropertyUpdateCallbackTable->propertyNames[i]) != 0)
            {
                LogError("cannot allocate PNP_CLIENT_READWRITE_PROPERTY_UPDATED_CALLBACK_TABLE");
                FreeReadWritePropertyCallbackTable(result);
                result = NULL;
                break;
            }
        }
    }

    return result;
}

// Creates a copy of the caller passed PNP_CLIENT_COMMAND_CALLBACK_TABLE
static PNP_CLIENT_COMMAND_CALLBACK_TABLE* CopyCommandCallbackTable(const PNP_CLIENT_COMMAND_CALLBACK_TABLE* commandCallbackTable)
{
    PNP_CLIENT_COMMAND_CALLBACK_TABLE* result;
    
    if ((result = calloc(1, sizeof(PNP_CLIENT_COMMAND_CALLBACK_TABLE))) == NULL)
    {
        LogError("cannot allocate PNP_CLIENT_COMMAND_CALLBACK_TABLE");
    }
    else if ((result->commandNames = calloc(commandCallbackTable->numCallbacks, sizeof(char*))) == NULL)
    {
        LogError("cannot allocate PNP_CLIENT_COMMAND_CALLBACK_TABLE");
        FreeCommandPropertyCallbackTable(result);
        result = NULL;
    }
    else if ((result->callbacks = calloc(commandCallbackTable->numCallbacks, sizeof(PNP_COMMAND_EXECUTE_CALLBACK*))) == NULL)
    {
        LogError("cannot allocate PNP_CLIENT_COMMAND_CALLBACK_TABLE");
        FreeCommandPropertyCallbackTable(result);
        result = NULL;
    }
    else
    {
        result->version = commandCallbackTable->version;
        result->numCallbacks = commandCallbackTable->numCallbacks;
        memcpy((PNP_COMMAND_EXECUTE_CALLBACK*)result->callbacks, commandCallbackTable->callbacks, result->numCallbacks * sizeof(PNP_COMMAND_EXECUTE_CALLBACK));

        for (int i = 0; i < commandCallbackTable->numCallbacks; i++)
        {
            if (mallocAndStrcpy_s((char**)&result->commandNames[i], commandCallbackTable->commandNames[i]) != 0)
            {
                LogError("cannot allocate PNP_CLIENT_COMMAND_CALLBACK_TABLE");
                FreeCommandPropertyCallbackTable(result);
                result = NULL;
                break;
            }
        }
    }

    return result;
}

// Makes sure that the passed in structure of version is correct.
static int VerifyCallbackTableVersions(const PNP_CLIENT_READWRITE_PROPERTY_UPDATED_CALLBACK_TABLE* readwritePropertyUpdateCallbackTable, const PNP_CLIENT_COMMAND_CALLBACK_TABLE* commandCallbackTable)
{
    int result;

    if ((readwritePropertyUpdateCallbackTable != NULL) && (readwritePropertyUpdateCallbackTable->version != PNP_CLIENT_READWRITE_PROPERTY_UPDATE_VERSION_1))
    {
        LogError("readwritePropertyUpdateCallbackTable version = %d, but only 1 is currently supported", readwritePropertyUpdateCallbackTable->version);
        result = __FAILURE__;
    }
    else if ((commandCallbackTable != NULL) && (commandCallbackTable->version != PNP_CLIENT_COMMAND_CALLBACK_VERSION_1))
    {
        LogError("commandCallbackTable version = %d, but only 1 is currently supported", commandCallbackTable->version);
        result = __FAILURE__;
    }
    else
    {
        result = 0;
    }

    return result;
}

// Initializes a PNP_INTERFACE_CLIENT_CORE_HANDLE
PNP_INTERFACE_CLIENT_CORE_HANDLE PnP_InterfaceClientCore_Create(PNP_LOCK_THREAD_BINDING* lockThreadBinding, PNP_CLIENT_CORE_HANDLE pnpClientCoreHandle, const char* interfaceName, const PNP_CLIENT_READWRITE_PROPERTY_UPDATED_CALLBACK_TABLE* readwritePropertyUpdateCallbackTable, const PNP_CLIENT_COMMAND_CALLBACK_TABLE* commandCallbackTable, void* userContextCallback)
{
    PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient;
    PNP_CLIENT_RESULT result;
    LOCK_HANDLE lockHandle = NULL;

    if ((lockThreadBinding == NULL) || (pnpClientCoreHandle == NULL) || (interfaceName == NULL) )
    {
        LogError("Invalid parameter(s): lockThreadBinding=%p, pnpClientCoreHandle=%p, interfaceName=%p", lockThreadBinding, pnpClientCoreHandle, interfaceName);
        pnpInterfaceClient = NULL;
    }
    else if (VerifyCallbackTableVersions(readwritePropertyUpdateCallbackTable, commandCallbackTable) != 0)
    {
        LogError("Versioned structures are not set to supported versions");
        pnpInterfaceClient = NULL;        
    }
    else if ((pnpInterfaceClient = calloc(1, sizeof(PNP_INTERFACE_CLIENT_CORE))) == NULL)
    {
        LogError("Cannot allocate interface client");
    }
    else if ((lockHandle = lockThreadBinding->pnpBindingLockInit()) == NULL)
    {
        LogError("Failed initializing lock");
        FreePnpInterface(pnpInterfaceClient);
        pnpInterfaceClient = NULL;
    }
    else if (mallocAndStrcpy_s(&pnpInterfaceClient->interfaceName, interfaceName) != 0)
    {
        LogError("Cannot allocate interfaceName");
        FreePnpInterface(pnpInterfaceClient);
        pnpInterfaceClient = NULL;
    }
    else if ((pnpInterfaceClient->rawInterfaceName = PnP_Get_RawInterfaceId(interfaceName)) == NULL)
    {
        LogError("Cannot allocate rawInterfaceName");
        FreePnpInterface(pnpInterfaceClient);
        pnpInterfaceClient = NULL;
    }
    else if ((readwritePropertyUpdateCallbackTable != NULL) && (readwritePropertyUpdateCallbackTable->numCallbacks > 0) && ((pnpInterfaceClient->readwritePropertyUpdateCallbackTable = CopyReadWritePropertyCallbackTable(readwritePropertyUpdateCallbackTable)) == NULL))
    {
        LogError("Cannot copy ReadWrite callback table");
        FreePnpInterface(pnpInterfaceClient);
        pnpInterfaceClient = NULL;
    }
    else if ((commandCallbackTable != NULL) && (commandCallbackTable->numCallbacks > 0) && ((pnpInterfaceClient->commandCallbackTable = CopyCommandCallbackTable(commandCallbackTable)) == NULL))
    {
        LogError("Cannot copy Command callback table");
        FreePnpInterface(pnpInterfaceClient);
        pnpInterfaceClient = NULL;
    }
    //  PnP_ClientCore_AddInterfaceReferenceFromInterface should be last call that can fail in this path, since our error cleanup in this function doesn't dereference this.
    else if ((result = PnP_ClientCore_AddInterfaceReferenceFromInterface(pnpClientCoreHandle)) != PNP_CLIENT_OK)
    {
        LogError("PnP_ClientCore_AddInterfaceReferenceFromInterface failed, result = %d", result);
        FreePnpInterface(pnpInterfaceClient);
        pnpInterfaceClient = NULL;
    }
    else
    {
        memcpy(&pnpInterfaceClient->lockThreadBinding, lockThreadBinding, sizeof(pnpInterfaceClient->lockThreadBinding));
        pnpInterfaceClient->pnpClientCoreHandle = pnpClientCoreHandle;
        pnpInterfaceClient->rawInterfaceNameLen = strlen(pnpInterfaceClient->rawInterfaceName);
        pnpInterfaceClient->userContextCallback = userContextCallback;
        pnpInterfaceClient->lockThreadBinding.pnpBindingLockHandle = lockHandle;
        lockHandle = NULL;
    }

    if (lockHandle != NULL)
    {
        lockThreadBinding->pnpBindingLockDeinit(lockHandle);
    }

    return pnpInterfaceClient;
}


// For specified property, generate appropriate json for updating it.  The json returned is 
// of the form { "RawIdForThisInterface" : { "propertyName" : propertyValue }.
static PNP_CLIENT_RESULT CreateJsonForReportedProperty(PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient, const char* propertyName, unsigned const char* propertyData, size_t propertyDataLen, STRING_HANDLE* jsonToSend)
{
    PNP_CLIENT_RESULT result;

    // We need to copy propertyData into propertyDataString because the caller didn't necessarily NULL terminate this string.
    if ((*jsonToSend = STRING_new()) == NULL)
    {
        LogError("Cannot allocate string handle");
        result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
    }
    else
    {
        // We manually build up the reported property to send, instead of using parson to do this.
        // If caller sets propertyData as json that has sub json fields in it, then if we give this to
        // parson as a string it'll escape propertyData's quotes, e.g.  We can in theory parse the caller's
        // string and have parson build dataToSend based on that, but that is inefficient and problematic 
        // since a caller passing in a simple string to assign parson won't parse.

        if ((STRING_sprintf(*jsonToSend, "{\"%s\": {", pnpInterfaceClient->rawInterfaceName) != 0) ||
            (STRING_sprintf(*jsonToSend, "\"%s\": %.*s", propertyName, propertyDataLen, propertyData) != 0) ||
            (STRING_concat(*jsonToSend, " } }") != 0))
        {
            LogError("Unable to construct jsonToSend string");
            result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
        }
        else
        {
            result = PNP_CLIENT_OK;
        }
    }

    return result;
}

// Creates a json blob with the keyName as 'propertyName'
static PNP_CLIENT_RESULT CreateJsonForProperty(const char* propertyName, unsigned const char* propertyData, size_t propertyDataLen, STRING_HANDLE* jsonToSend)
{
    PNP_CLIENT_RESULT result;

    // We need to copy propertyData into propertyDataString because the caller didn't necessarily NULL terminate this string.
    if ((*jsonToSend = STRING_new()) == NULL)
    {
        LogError("Cannot allocate string handle");
        result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
    }
    else
    {
        // See comments in CreateJsonForReportedProperty about why we can't safely use parson to process propertyData.
        if ((STRING_sprintf(*jsonToSend, "{ \"%s\": %.*s } ", propertyName, propertyDataLen, propertyData) != 0))
        {
            LogError("Unable to construct jsonToSend string");
            result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
        }
        else
        {
            result = PNP_CLIENT_OK;
        }
    }

    return result;

}


// Creates a structure for tracking reported properties context.  We need to manually keep this table because
// when we close IoTHub_* handle (and unlike for pending send telemetrys), we do NOT get callbacks for pending
// properties updates that don't get delivered.  That means they must be deleted here.
static PNP_REPORT_PROPERTIES_UPDATE_CALLBACK_CONTEXT* CreateReportedPropertiesUpdateCallbackContext(PNP_REPORTED_PROPERTY_UPDATED_CALLBACK pnpReportedPropertyCallback, void* userContextCallback)
{
    PNP_REPORT_PROPERTIES_UPDATE_CALLBACK_CONTEXT* result;

    if ((result = calloc(1, sizeof(PNP_REPORT_PROPERTIES_UPDATE_CALLBACK_CONTEXT))) == NULL)
    {
        LogError("Cannot allocate context for PNP_REPORT_PROPERTIES_UPDATE_CALLBACK_CONTEXT");
    }
    else
    {
        result->pnpReportedPropertyCallback = pnpReportedPropertyCallback;
        result->userContextCallback = userContextCallback;
    }

    return result;
}

// Implements API for reporting properties to caller.
PNP_CLIENT_RESULT PnP_InterfaceClientCore_ReportReadOnlyPropertyStatusAsync(PNP_INTERFACE_CLIENT_CORE_HANDLE pnpInterfaceClientHandle, const char* propertyName, unsigned const char* propertyData, size_t propertyDataLen, PNP_REPORTED_PROPERTY_UPDATED_CALLBACK pnpReportedPropertyCallback, void* userContextCallback)
{
    STRING_HANDLE jsonToSend = NULL;
    PNP_CLIENT_RESULT result;
    PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient = (PNP_INTERFACE_CLIENT_CORE*)pnpInterfaceClientHandle;
    PNP_REPORT_PROPERTIES_UPDATE_CALLBACK_CONTEXT* sendPropertiesUpdateContext = NULL;

    if ((pnpInterfaceClientHandle == NULL) || (propertyName == NULL) || (propertyData == NULL))
    {
        LogError("Invalid parameter, one or more paramaters is NULL. pnpInterfaceClientHandle=%p, propertyName=%p", pnpInterfaceClientHandle, propertyName);
        result = PNP_CLIENT_ERROR_INVALID_ARG;
    }
    else if ((result = CreateJsonForReportedProperty(pnpInterfaceClient, propertyName, propertyData, propertyDataLen, &jsonToSend)) != PNP_CLIENT_OK)
    {
        LogError("Error creating json for reported property %s.  err = %d", propertyName, result);
    }
    else if ((sendPropertiesUpdateContext = CreateReportedPropertiesUpdateCallbackContext(pnpReportedPropertyCallback, userContextCallback)) == NULL)
    {
        LogError("Cannot create update reportied properties callback context");
        result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
    }
    else
    {
        unsigned const char* dataToSend = (unsigned const char*)STRING_c_str(jsonToSend);
        size_t dataToSendLen = strlen((const char*)dataToSend);
        if ((result = PnP_ClientCore_ReportPropertyStatusAsync(pnpInterfaceClient->pnpClientCoreHandle, pnpInterfaceClientHandle, dataToSend, dataToSendLen, sendPropertiesUpdateContext)) != PNP_CLIENT_OK)
        {
            LogError("PnP_ClientCore_ReportPropertyStatusAsync failed, error = %d", result);
        }
        else
        {
            result = PNP_CLIENT_OK;
        }
    }

    if ((result != PNP_CLIENT_OK) && (sendPropertiesUpdateContext != NULL))
    {
        free(sendPropertiesUpdateContext);
    }

    STRING_delete(jsonToSend);
    return result;
}

// Creates json blob for indicating a read/write response
static PNP_CLIENT_RESULT CreateJsonForReadWritePropertyResponse(PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient, const char* propertyName, const PNP_CLIENT_READWRITE_PROPERTY_RESPONSE* pnpResponse, STRING_HANDLE* jsonToSend)
{
    PNP_CLIENT_RESULT result;

    if ((*jsonToSend = STRING_new()) == NULL)
    {
        LogError("Cannot allocate string handle");
        result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
    }
    // See comments in CreateJsonForReportedProperty about why we can't safely use parson to process propertyData.
    else if ((STRING_sprintf(*jsonToSend, "{\"%s\": {", pnpInterfaceClient->rawInterfaceName) != 0) ||
        (STRING_sprintf(*jsonToSend, " \"%s\": { \"value\": { \"Value\": %.*s }, ", propertyName, pnpResponse->propertyDataLen, pnpResponse->propertyData) != 0) ||
        (STRING_sprintf(*jsonToSend, " \"status\": { \"code\": %d, \"description\": \"%s\", \"version\": %d }", pnpResponse->statusCode, pnpResponse->statusDescription, pnpResponse->responseVersion) != 0) ||
        (STRING_concat(*jsonToSend, "  } } }") != 0))
    {
        LogError("Unable to construct jsonToSend string");
        result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
    }
    else
    {
        result = PNP_CLIENT_OK;
    }

    return result;
}

// Invoked when a callback is processed on a PnP Interface.  Makes sure we're not shutting down, then marks callback as in progress.
static int BeginInterfaceCallbackProcessing(PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient)
{
    int result;
    bool lockHeld = false;

    if (InvokeBindingInterfaceLock(pnpInterfaceClient, &lockHeld) != 0)
    {
        LogError("Cannot obtain lock");
        result = __FAILURE__;
    }
    else if (pnpInterfaceClient->pendingDestroy == true)
    {
        LogError("Cannot process callback for interface %s.  It is in process of being destroyed", pnpInterfaceClient->interfaceName);
        result = __FAILURE__;
    }
    else
    {
        pnpInterfaceClient->processingCallback = true;
        result = 0;
    }

    (void)InvokeBindingInterfaceUnlock(pnpInterfaceClient, &lockHeld);
    return result;
}

// Marks callback as having terminated.
static void EndInterfaceCallbackProcessing(PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient)
{
    bool lockHeld = false;

    if (InvokeBindingInterfaceLock(pnpInterfaceClient, &lockHeld) != 0)
    {
        LogError("Cannot obtain lock");
    }

    // Unconditionally set we're out of processing callback, even if lock fails, since so much progress is blocked otherwise.
    pnpInterfaceClient->processingCallback = false;
    (void)InvokeBindingInterfaceUnlock(pnpInterfaceClient, &lockHeld);
}

// Implements API for reporting read/write properties updates.
PNP_CLIENT_RESULT PnP_InterfaceClientCore_ReportReadWritePropertyStatusAsync(PNP_INTERFACE_CLIENT_CORE_HANDLE pnpInterfaceClientHandle, const char* propertyName, const PNP_CLIENT_READWRITE_PROPERTY_RESPONSE* pnpResponse, PNP_REPORTED_PROPERTY_UPDATED_CALLBACK pnpReportedPropertyCallback, void* userContextCallback)
{
    PNP_CLIENT_RESULT result;
    PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient = (PNP_INTERFACE_CLIENT_CORE*)pnpInterfaceClientHandle;
    STRING_HANDLE jsonToSend = NULL;
    PNP_REPORT_PROPERTIES_UPDATE_CALLBACK_CONTEXT* sendPropertiesUpdateContext = NULL;

    if ((pnpInterfaceClientHandle == NULL) || (propertyName == NULL) || (pnpResponse == NULL))
    {
        LogError("Invalid parameter, one or more paramaters is NULL. pnpInterfaceClientHandle=%p, propertyName=%p, pnpResponse=%p", pnpInterfaceClientHandle, propertyName, pnpResponse);
        result = PNP_CLIENT_ERROR_INVALID_ARG;
    }
    else if (pnpResponse->version != PNP_CLIENT_READWRITE_PROPERTY_RESPONSE_VERSION_1)
    {
        LogError("Invalid pnpResponse version (%d) set.  SDK only supports version=PNP_CLIENT_READWRITE_PROPERTY_RESPONSE_VERSION_1", pnpResponse->version);
        result = PNP_CLIENT_ERROR_INVALID_ARG;
    }
    else if ((result = CreateJsonForReadWritePropertyResponse(pnpInterfaceClient, propertyName, pnpResponse, &jsonToSend)) != PNP_CLIENT_OK)
    {
        LogError("Error creating json for reported property %s.  err = %d", propertyName, result);
    }
    else if ((sendPropertiesUpdateContext = CreateReportedPropertiesUpdateCallbackContext(pnpReportedPropertyCallback, userContextCallback)) == NULL)
    {
        LogError("Cannot create update reportied properties callback context");
        result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
    }
    else 
    {
        unsigned const char* dataToSend = (unsigned const char*)STRING_c_str(jsonToSend);
        size_t dataToSendLen = strlen((const char*)dataToSend);

        if ((result = PnP_ClientCore_ReportPropertyStatusAsync(pnpInterfaceClient->pnpClientCoreHandle, pnpInterfaceClientHandle, dataToSend, dataToSendLen, sendPropertiesUpdateContext)) != PNP_CLIENT_OK)
        {
            LogError("PnP_ClientCore_ReportPropertyStatusAsync failed, error = %d", result);
        }
        else
        {
            result = PNP_CLIENT_OK;
        }
    }

    if ((result != PNP_CLIENT_OK) && (sendPropertiesUpdateContext != NULL))
    {
        free(sendPropertiesUpdateContext);
    }

    STRING_delete(jsonToSend);
    return result;
}

// Allocates a properly setup IOTHUB_MESSAGE_HANDLE for processing onto IoTHub.
static PNP_CLIENT_RESULT CreateSendTelemetryMessage(PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient, const char* telemetryName, const unsigned char* messageData, size_t messageDataLen, IOTHUB_MESSAGE_HANDLE* telemetryMessageHandle)
{
    PNP_CLIENT_RESULT result;
    IOTHUB_MESSAGE_RESULT iothubMessageResult;
    STRING_HANDLE jsonToSend = NULL;

    if ((result = CreateJsonForProperty(telemetryName, messageData, messageDataLen, &jsonToSend)) != PNP_CLIENT_OK)
    {
        LogError("Error creating json for telemetry message.  telemetryName=%s.  err = %d", telemetryName, result);
    }
    else
    {
        unsigned const char* dataToSend = (unsigned const char*)STRING_c_str(jsonToSend);
        size_t dataToSendLen = strlen((const char*)dataToSend);

        if ((*telemetryMessageHandle = IoTHubMessage_CreateFromByteArray(dataToSend, dataToSendLen)) == NULL)
        {
            LogError("Cannot allocate IoTHubMessage for telemetry");
            result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
        }
        else if ((iothubMessageResult = IoTHubMessage_SetProperty(*telemetryMessageHandle, PNP_INTERFACE_INTERNAL_ID_PROPERTY, pnpInterfaceClient->rawInterfaceName)) != IOTHUB_MESSAGE_OK)
        {
            LogError("Cannot set property %s, error = %d", PNP_INTERFACE_INTERNAL_ID_PROPERTY, iothubMessageResult);
            // While IoTHubMessage_SetProperty most likely failure is probably out of memory, because the IoTHubMessage_* API's don't have an explicit out of memory
            // return code its not safe for the PnP layer to assume this is case.  Return most broad error.
            result = PNP_CLIENT_ERROR;
        }
        else if ((iothubMessageResult = IoTHubMessage_SetProperty(*telemetryMessageHandle, PNP_INTERFACE_ID_PROPERTY, pnpInterfaceClient->interfaceName)) != IOTHUB_MESSAGE_OK)
        {
            LogError("Cannot set property %s, error = %d", PNP_INTERFACE_ID_PROPERTY, iothubMessageResult);
            result = PNP_CLIENT_ERROR;
        }
        else if ((iothubMessageResult = IoTHubMessage_SetProperty(*telemetryMessageHandle, PNP_MESSAGE_SCHEMA_PROPERTY, telemetryName)) != IOTHUB_MESSAGE_OK)
        {
            LogError("Cannot set property %s, error = %d", PNP_MESSAGE_SCHEMA_PROPERTY, iothubMessageResult);
            result = PNP_CLIENT_ERROR;
        }
        else if ((iothubMessageResult = IoTHubMessage_SetContentTypeSystemProperty(*telemetryMessageHandle, PNP_JSON_MESSAGE_CONTENT_TYPE)) != IOTHUB_MESSAGE_OK)
        {
            LogError("Cannot set property %s, error = %d", PNP_MESSAGE_SCHEMA_PROPERTY, iothubMessageResult);
            result = PNP_CLIENT_ERROR;
        }
        else
        {
            result = PNP_CLIENT_OK;
        }
    }

    if (result != PNP_CLIENT_OK)
    {
        if ((telemetryMessageHandle != NULL) && ((*telemetryMessageHandle) != NULL))
        {
            IoTHubMessage_Destroy((*telemetryMessageHandle));
            *telemetryMessageHandle = NULL;
        }
    }

    STRING_delete(jsonToSend);
    return result;
}

// Creates a PNP_INTERFACE_SEND_TELEMETRY_CALLBACK_CONTEXT item.
static PNP_INTERFACE_SEND_TELEMETRY_CALLBACK_CONTEXT* CreateInterfaceSendTelemetryCallbackContext(PNP_CLIENT_TELEMETRY_CONFIRMATION_CALLBACK telemetryConfirmationCallback, void* userContextCallback)
{
    PNP_INTERFACE_SEND_TELEMETRY_CALLBACK_CONTEXT* result;

    if ((result = calloc(1, sizeof(PNP_INTERFACE_SEND_TELEMETRY_CALLBACK_CONTEXT))) == NULL)
    {
        LogError("Cannot allocate context for PNP_INTERFACE_SEND_TELEMETRY_CALLBACK_CONTEXT");
    }
    else
    {
        result->telemetryConfirmationCallback = telemetryConfirmationCallback;
        result->userContextCallback = userContextCallback;
    }

    return result;
}

// PnP_InterfaceClientCore_SendTelemetryAsync sends the specified telemetry to Azure IoTHub in proper PnP data format.
PNP_CLIENT_RESULT PnP_InterfaceClientCore_SendTelemetryAsync(PNP_INTERFACE_CLIENT_CORE_HANDLE pnpInterfaceClientHandle, const char* telemetryName, const unsigned char* messageData, size_t messageDataLen, PNP_CLIENT_TELEMETRY_CONFIRMATION_CALLBACK telemetryConfirmationCallback, void* userContextCallback)
{
    PNP_CLIENT_RESULT result;
    PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient = (PNP_INTERFACE_CLIENT_CORE*)pnpInterfaceClientHandle;
    IOTHUB_MESSAGE_HANDLE telemetryMessageHandle = NULL;
    PNP_INTERFACE_SEND_TELEMETRY_CALLBACK_CONTEXT* sendTelemetryCallbackContext = NULL;

    if ((pnpInterfaceClientHandle == NULL) || (telemetryName == NULL) || (messageData == NULL) || (messageDataLen == 0))
    {
        LogError("Invalid parameter, one or more paramaters is NULL. pnpInterfaceClientHandle=%p, telemetryName=%p", pnpInterfaceClientHandle, telemetryName);
        result = PNP_CLIENT_ERROR_INVALID_ARG;
    }
    else if ((result = CreateSendTelemetryMessage(pnpInterfaceClient, telemetryName, messageData, messageDataLen, &telemetryMessageHandle)) != PNP_CLIENT_OK)
    {
        LogError("Cannot create send telemetry message, error = %d", result);
        result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
    }
    else if ((sendTelemetryCallbackContext = CreateInterfaceSendTelemetryCallbackContext(telemetryConfirmationCallback, userContextCallback)) == NULL)
    {
        LogError("Cannot create send telemetry message callback context");
        result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
    }
    else if ((result = PnP_ClientCore_SendTelemetryAsync(pnpInterfaceClient->pnpClientCoreHandle, pnpInterfaceClientHandle, telemetryMessageHandle, sendTelemetryCallbackContext)) != PNP_CLIENT_OK)
    {
        LogError("PnP_ClientCore_SendTelemetryAsync failed, error = %d", result);
    }

    if ((result != PNP_CLIENT_OK) && (sendTelemetryCallbackContext != NULL))
    {
        free(sendTelemetryCallbackContext);
    }

    if (telemetryMessageHandle != NULL)
    {
        IoTHubMessage_Destroy(telemetryMessageHandle);
    }

    return result;
}

// Polls for callbacks being updated.  Enters and leaves with lock held.
void BlockOnActiveInterfaceCallbacks(PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient, bool *lockHeld)
{
    while (pnpInterfaceClient->processingCallback == true)
    {
        (void)InvokeBindingInterfaceUnlock(pnpInterfaceClient, lockHeld);
        InvokeBindingInterfaceSleep(pnpInterfaceClient, 10);
        (void)InvokeBindingInterfaceLock(pnpInterfaceClient, lockHeld);
    }
}

// Destroys PNP_INTERFACE_CLIENT_CORE_HANDLE object.  Note that if there are active callbacks into this interface,
// we will (in fact must) block here until the interfaces are done processing.
void PnP_InterfaceClientCore_Destroy(PNP_INTERFACE_CLIENT_CORE_HANDLE pnpInterfaceClientHandle)
{
    PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient = (PNP_INTERFACE_CLIENT_CORE*)pnpInterfaceClientHandle;
    bool lockHeld = false;

    if (pnpInterfaceClientHandle == NULL)
    {
        LogError("Invalid parameter. interfaceClientHandle=%p", pnpInterfaceClientHandle);
    }
    else if (InvokeBindingInterfaceLock(pnpInterfaceClientHandle, &lockHeld) != 0)
    {
        LogError("bindingLock failed");
    }
    else
    {
        pnpInterfaceClient->pendingDestroy = true;
        BlockOnActiveInterfaceCallbacks(pnpInterfaceClient, &lockHeld);
        (void)InvokeBindingInterfaceUnlock(pnpInterfaceClientHandle, &lockHeld);

        PnP_ClientCore_RemoveInterfaceReference(pnpInterfaceClient->pnpClientCoreHandle);

        if (pnpInterfaceClient->registeredWithClient == false)
        {
            // If the PnPDevice doesn't have a reference to this interface, we can delete it now.
            // Otherwise we'll properly delete this object once PnP Device marks us as unregistered.
            FreePnpInterface(pnpInterfaceClient);
        }
    }
}

// For given propertyName, find the callback function associated with it.
static PNP_READWRITE_PROPERTY_UPDATE_CALLBACK GetCallbackForProperty(PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient, const char* propertyName)
{
    PNP_READWRITE_PROPERTY_UPDATE_CALLBACK propertyCallback = NULL;

    if (pnpInterfaceClient->readwritePropertyUpdateCallbackTable != NULL)
    {
        for (int i = 0; i < pnpInterfaceClient->readwritePropertyUpdateCallbackTable->numCallbacks; i++)
        {
            if (0 == strcmp(pnpInterfaceClient->readwritePropertyUpdateCallbackTable->propertyNames[i], propertyName))
            {
                propertyCallback = pnpInterfaceClient->readwritePropertyUpdateCallbackTable->callbacks[i];
                break;
            }
        }
    }

    return propertyCallback;
}

// If available, retrieves a serialized json for given property.
static char* GetPayloadFromProperty(JSON_Object* pnpInterfaceJson, const char* propertyName)
{
    char* payLoadForProperty;

    JSON_Value* jsonPropertyValue = json_object_get_value(pnpInterfaceJson, propertyName);
    if (jsonPropertyValue == NULL)
    {
        // Don't log errors in this case, is caller may invoke us not knowing if propertyName is present or not.
        payLoadForProperty = NULL;
    }
    else if ((payLoadForProperty = json_serialize_to_string(jsonPropertyValue)) == NULL)
    {
        LogError("json_serialize_to_string fails");
    }

    return payLoadForProperty;
}

// For each high-level property associated with this interface, determine if there is a callback function associated with it.
static void ProcessReadWritePropertyIfNeededFromDesired(PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient, JSON_Object* pnpInterfaceDesiredJson, JSON_Object* pnpInterfaceReportedJson, size_t propertyNumber, int jsonVersion)
{
    PNP_READWRITE_PROPERTY_UPDATE_CALLBACK propertyCallback;
    
    const char* propertyName = json_object_get_name(pnpInterfaceDesiredJson, propertyNumber);

    if ((propertyCallback = GetCallbackForProperty(pnpInterfaceClient, propertyName)) == NULL)
    {
        // If a property arrives we don't have a callback for, there is not a mechanism to report this back to the server.  PnP interfaces
        // are fixed, so hitting this means either incorrect json was sent from server or the client is missing a callback and is an error
        // that needs logging at least.
        LogError("Property %s does not have a callback associated with it.  Silently ignoring", propertyName);
    }
    else
    {
        char* payloadForDesiredProperty = NULL; 
        char* payloadForReportedProperty = NULL;

        payloadForDesiredProperty = GetPayloadFromProperty(pnpInterfaceDesiredJson, propertyName);
        if (pnpInterfaceReportedJson)
        {
            payloadForReportedProperty = GetPayloadFromProperty(pnpInterfaceReportedJson, propertyName);
        }

        size_t payloadForDesiredPropertyLen = strlen(payloadForDesiredProperty);
        size_t payloadForReportedPropertyLen = payloadForReportedProperty ?  strlen(payloadForReportedProperty) : 0;
            
        propertyCallback((unsigned const char*)payloadForReportedProperty, payloadForReportedPropertyLen, (unsigned const char*)payloadForDesiredProperty, payloadForDesiredPropertyLen, jsonVersion, pnpInterfaceClient->userContextCallback);

        free(payloadForDesiredProperty);
        free(payloadForReportedProperty);
    }
}

// Process the JSON_Object that represents a device twin's contents and invoke appropriate callbacks.
static void ProcessPropertiesForTwin(PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient, JSON_Object* root_object, bool fullTwin)
{   
    char temp[1024];
    char temp2[1024];
    char temp3[1024];
    // TODO: Clean this up once the server schema side changes have stabilized.
    sprintf(temp, "%s%s", fullTwin ? "desired." : "", pnpInterfaceClient->rawInterfaceName);
    sprintf(temp2, "%s%s", fullTwin ? "reported." : "", pnpInterfaceClient->rawInterfaceName);
    sprintf(temp3, "%s%s", fullTwin ? "desired." : "", PNP_PROPERTY_UPDATE_JSON_VERSON);
    
    JSON_Object* pnpInterfaceDesiredJson;
    JSON_Object* pnpInterfaceReportedJson;
    
    pnpInterfaceDesiredJson = json_object_dotget_object(root_object, temp);
    if (fullTwin)
    {
        // If we're getting full twin, then it will include reported properties, too.
        pnpInterfaceReportedJson = json_object_dotget_object(root_object, temp2);
    }
    else
    {
        pnpInterfaceReportedJson = NULL;
    }

    if ((pnpInterfaceDesiredJson== NULL) && (pnpInterfaceReportedJson == NULL))
    {
        ; // Not being able to find this interface's name is json is not an error.
    }
    else
    {
        int jsonVersion = (int)json_object_dotget_number(root_object, temp3);
    
        size_t numDesiredChildrenOnInterface = pnpInterfaceDesiredJson ? json_object_get_count(pnpInterfaceDesiredJson) : 0;
        //size_t numReportedChildrenOnInterface = pnpInterfaceReportedJson ? json_object_get_count(pnpInterfaceReportedJson) : 0;
    
        for (size_t i = 0; i < numDesiredChildrenOnInterface; i++)
        {
            ProcessReadWritePropertyIfNeededFromDesired(pnpInterfaceClient, pnpInterfaceDesiredJson, pnpInterfaceReportedJson, i, jsonVersion);
        }
    }
}

// When a twin arrives from IoTHub_* twin callback layer, this function is invoked per interface to see if there are read-write properties
// that need to be notified of change.  Note that this function is called for each PNP_INTERFACE_CLIENT_CORE_HANDLE, as the caller does not parse
// the twin payload but instead relies on PnP_InterfaceClientCore_ProcessTwinCallback to process it or silently ignore.
PNP_CLIENT_RESULT PnP_InterfaceClientCore_ProcessTwinCallback(PNP_INTERFACE_CLIENT_CORE_HANDLE pnpInterfaceClientHandle, bool fullTwin, const unsigned char* payLoad, size_t size)
{
    PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient = (PNP_INTERFACE_CLIENT_CORE*)pnpInterfaceClientHandle;
    PNP_CLIENT_RESULT result;
    STRING_HANDLE jsonStringHandle = NULL;
    const char* jsonString;

    if ((pnpInterfaceClientHandle == NULL) || (payLoad == NULL) || (size == 0))
    {
        LogError("Invalid parameter. pnpInterfaceClientHandle=%p, payLoad=%p, size=%lu", pnpInterfaceClientHandle, payLoad, (unsigned long)size);
        result = PNP_CLIENT_ERROR_INVALID_ARG;
    }
    else if (BeginInterfaceCallbackProcessing(pnpInterfaceClient) != 0)
    {   
        LogError("Cannot process callback for interface");
        result = PNP_CLIENT_ERROR_SHUTTING_DOWN;
    }
    else
    {
        JSON_Value* root_value = NULL;
        JSON_Object* root_object = NULL;

        // TODO: Should do a smallish refactor where we parse once and then invoke interface twins to avoid pre-parsing, instead of re-parse per interface visitor.
        //       Similar pattern could apply with visiting commands as well; move visitation logic into this .c file.
        if ((jsonStringHandle = STRING_from_byte_array(payLoad, size)) == NULL) 
        {
            LogError("STRING_construct_n failed");
            result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
        }
        else if ((jsonString = STRING_c_str(jsonStringHandle)) == NULL)
        {
            LogError("STRING_c_str failed");
            result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
        }
        else if ((root_value = json_parse_string(jsonString)) == NULL)
        {
            LogError("json_parse_string failed");
            result = PNP_CLIENT_ERROR;
        }
        else if ((root_object = json_value_get_object(root_value)) == NULL)
        {
            LogError("json_value_get_object failed");
            result = PNP_CLIENT_ERROR;
        }
        else
        {
            ProcessPropertiesForTwin(pnpInterfaceClient, root_object, fullTwin);
            result = PNP_CLIENT_OK;
        }

        if (root_object != NULL)
        {
            json_object_clear(root_object);
        }
        
        if (root_value != NULL)
        {
            json_value_free(root_value);
        }

        EndInterfaceCallbackProcessing(pnpInterfaceClient);
    }

    STRING_delete(jsonStringHandle);
    return result;
}

// Indicates whether given command is processed by this interface.  Commands arrive in the format
// "<interfaceName>*<commandOnInterfaceToInvoke>", so we need to match "<interfaceName>*" here.
static bool IsCommandMatchForInterface(PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient, const char* methodName)
{
    bool result;

    if (strncmp(pnpInterfaceClient->rawInterfaceName, methodName, pnpInterfaceClient->rawInterfaceNameLen) == 0)
    {
        // If we match on length, still need to check that what's being queried has a command separator as character immediately following.
        result = (methodName[pnpInterfaceClient->rawInterfaceNameLen] == commandSeparator);
    }
    else
    {
        result = false;
    }

    return result;
}

// Searches for given commandName registered for this interfaces.
static PNP_COMMAND_EXECUTE_CALLBACK FindCallbackInTable(PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient, const char* commandName)
{
    PNP_COMMAND_EXECUTE_CALLBACK commandCallback = NULL;

    if (pnpInterfaceClient->commandCallbackTable != NULL)
    {
        for (int i = 0; i < pnpInterfaceClient->commandCallbackTable->numCallbacks; i++)
        {
            if (strcmp(commandName, pnpInterfaceClient->commandCallbackTable->commandNames[i]) == 0)
            {
                commandCallback = pnpInterfaceClient->commandCallbackTable->callbacks[i];
                break;
            }
        }
    }

    return commandCallback;
}

// PnP_InterfaceClientCore_InvokeCommandIfSupported is invoked whenever a device_method is received by the calling layer.  Note that 
// it may be invoked even if it's not for the given interface the command is arriving on (because caller has no parsing logic), in 
// which case it will return PNP_COMMAND_PROCESSOR_NOT_APPLICABLE to tell the caller to move onto the next PNP_INTERFACE_CLIENT_CORE_HANDLE.
PNP_COMMAND_PROCESSOR_RESULT PnP_InterfaceClientCore_InvokeCommandIfSupported(PNP_INTERFACE_CLIENT_CORE_HANDLE pnpInterfaceClientHandle, const char* methodName, const unsigned char* payload, size_t size, unsigned char** response, size_t* response_size, int* responseCode)
{
    PNP_COMMAND_PROCESSOR_RESULT result;
    PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient = (PNP_INTERFACE_CLIENT_CORE*)pnpInterfaceClientHandle;

    if ((pnpInterfaceClientHandle == NULL) || (methodName == NULL) || (response == NULL) || (response_size == NULL) || (responseCode == NULL))
    {
        LogError("Invalid parameter(s).  pnpInterfaceClientHandle=%p, methodName=%p, response=%p, response_size=%p, responseCode=%p", pnpInterfaceClientHandle, methodName, response, response_size, responseCode);
        result = PNP_COMMAND_PROCESSOR_ERROR;
    }
    else if (BeginInterfaceCallbackProcessing(pnpInterfaceClient) != 0)
    {   
        LogError("Cannot process callback for interface");
        // Don't return an error here, because this interface going down is not fatal.
        result = PNP_COMMAND_PROCESSOR_NOT_APPLICABLE;
    }
    else if (IsCommandMatchForInterface(pnpInterfaceClient, methodName) == false)
    {
        // The method name is coming for a different interface.  There's nothing for us to do but silently ignore in this case.
        EndInterfaceCallbackProcessing(pnpInterfaceClient);
        result = PNP_COMMAND_PROCESSOR_NOT_APPLICABLE;
    }
    else
    {
        // Skip past the <interfaceName>* preamble to get the actual command name from PnP layer to map back to
        const char* commandName = methodName + pnpInterfaceClient->rawInterfaceNameLen + 1;
        PNP_COMMAND_EXECUTE_CALLBACK commandCallback;

        if ((commandCallback = FindCallbackInTable(pnpInterfaceClient, commandName)) == NULL)
        {
            LogError("Command %s sent to interface %s but not registered by this interface", commandName, pnpInterfaceClient->interfaceName);
            result = PNP_COMMAND_PROCESSOR_COMMAND_NOT_FOUND;
        }
        else
        {
            PNP_CLIENT_COMMAND_REQUEST pnpClientCommandRequest;
            pnpClientCommandRequest.version = PNP_CLIENT_COMMAND_REQUEST_VERSION_1;
            pnpClientCommandRequest.requestData = payload;
            pnpClientCommandRequest.requestDataLen = size;
            
            PNP_CLIENT_COMMAND_RESPONSE pnpClientResponse;
            memset(&pnpClientResponse, 0, sizeof(pnpClientResponse));
            pnpClientResponse.version = PNP_CLIENT_COMMAND_RESPONSE_VERSION_1;
            
            commandCallback(&pnpClientCommandRequest, &pnpClientResponse, pnpInterfaceClient->userContextCallback);
            *response = pnpClientResponse.responseData;
            *response_size = pnpClientResponse.responseDataLen;
            *responseCode = pnpClientResponse.status;

            // Indicate to caller we've processed this command and it can stop searching, regardless of what the callback function did.
            result = PNP_COMMAND_PROCESSOR_PROCESSED;
        }

        EndInterfaceCallbackProcessing(pnpInterfaceClient);
    }

    return result;
}

// Mark the given interface as registered.  This means that the calling PnPClientCore intends on using it and 
// serves as a reference count.  We can fail if another caller has already destroyed this client.
PNP_CLIENT_RESULT PnP_InterfaceClientCore_MarkRegistered(PNP_INTERFACE_CLIENT_CORE_HANDLE pnpInterfaceClientHandle)
{
    PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient = (PNP_INTERFACE_CLIENT_CORE*)pnpInterfaceClientHandle;
    PNP_CLIENT_RESULT result;
    bool lockHeld = false;

    if (pnpInterfaceClientHandle == NULL)
    {
        LogError("Invalid parameter, pnpInterfaceClientHandle=%p", pnpInterfaceClientHandle);
        result = PNP_CLIENT_ERROR_INVALID_ARG;
    }
    else if (InvokeBindingInterfaceLock(pnpInterfaceClientHandle, &lockHeld) != 0)
    {
        LogError("bindingLock failed");
        result = PNP_CLIENT_ERROR;
    }
    else if (pnpInterfaceClient->pendingDestroy == true)
    {
        LogError("Interface %s is trying to be registered, but it has already been deleted", pnpInterfaceClient->interfaceName);
        result = PNP_CLIENT_ERROR_SHUTTING_DOWN;
    }
    else
    {   
        pnpInterfaceClient->registeredWithClient = true;
        result = PNP_CLIENT_OK;
    }

    (void)InvokeBindingInterfaceUnlock(pnpInterfaceClientHandle, &lockHeld);
    return result;
}

// Marks the interface as unregistered; namely that the caller is through using it and it is
// safe to be deleted, either now (if it's already been destroyed) or later (when interface destroy comes)
void PnP_InterfaceClientCore_MarkUnregistered(PNP_INTERFACE_CLIENT_CORE_HANDLE pnpInterfaceClientHandle)
{
    PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient = (PNP_INTERFACE_CLIENT_CORE*)pnpInterfaceClientHandle;
    bool lockHeld = false;

    if (pnpInterfaceClientHandle == NULL)
    {
        LogError("Invalid parameter, pnpInterfaceClientHandle=%p", pnpInterfaceClientHandle);
    }
    else if (InvokeBindingInterfaceLock(pnpInterfaceClientHandle, &lockHeld) != 0)
    {
        LogError("bindingLock failed");
    }
    else if (pnpInterfaceClient->pendingDestroy == true)
    {
        // Freeing pnpInterfaceClient destroys lock and assumes its not already held.
        (void)InvokeBindingInterfaceUnlock(pnpInterfaceClientHandle, &lockHeld);
        // This state means that the caller has already invoked PnP_InterfaceClientCore_Destroy but that we couldn't
        // delete because the DeviceClient still held a reference.  As this is last reference, now we free
        FreePnpInterface(pnpInterfaceClient);
    }
    else
    {   
        pnpInterfaceClient->registeredWithClient = false;
    }

    (void)InvokeBindingInterfaceUnlock(pnpInterfaceClientHandle, &lockHeld);
}

// Processes callback for Sending a PnP Telemetry.
PNP_CLIENT_RESULT PnP_InterfaceClientCore_ProcessTelemetryCallback(PNP_INTERFACE_CLIENT_CORE_HANDLE pnpInterfaceClientHandle, PNP_SEND_TELEMETRY_STATUS pnpSendTelemetryStatus, void* userContextCallback)
{
    PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient = (PNP_INTERFACE_CLIENT_CORE*)pnpInterfaceClientHandle;
    PNP_CLIENT_RESULT result;

    if ((pnpInterfaceClientHandle == NULL) || (userContextCallback == NULL))
    {
        LogError("Invalid parameter, pnpInterfaceClientHandle=%p", pnpInterfaceClientHandle);
        result = PNP_CLIENT_ERROR_INVALID_ARG;
    }
    else
    {
        PNP_INTERFACE_SEND_TELEMETRY_CALLBACK_CONTEXT* sendTelemetryCallbackContext = (PNP_INTERFACE_SEND_TELEMETRY_CALLBACK_CONTEXT*)userContextCallback;

        if (sendTelemetryCallbackContext->telemetryConfirmationCallback == NULL)
        {
            // Caller did not register a callback for this telemetry.  Not an error, no logging needed.
            result = PNP_CLIENT_OK;
        }
        else if (BeginInterfaceCallbackProcessing(pnpInterfaceClient) != 0)
        {
            LogError("Cannot process callback for interface");
            result = PNP_CLIENT_ERROR_SHUTTING_DOWN;
        }
        else
        {
            sendTelemetryCallbackContext->telemetryConfirmationCallback(pnpSendTelemetryStatus, sendTelemetryCallbackContext->userContextCallback);
            EndInterfaceCallbackProcessing(pnpInterfaceClient);
            result = PNP_CLIENT_OK;
        }
        free(sendTelemetryCallbackContext);
    }

    return result;
}

// Processes callback for reported property callback.
PNP_CLIENT_RESULT PnP_InterfaceClientCore_ProcessReportedPropertiesUpdateCallback(PNP_INTERFACE_CLIENT_CORE_HANDLE pnpInterfaceClientHandle, PNP_REPORTED_PROPERTY_STATUS pnpReportedStatus, void* userContextCallback)
{
    PNP_INTERFACE_CLIENT_CORE* pnpInterfaceClient = (PNP_INTERFACE_CLIENT_CORE*)pnpInterfaceClientHandle;
    PNP_CLIENT_RESULT result;

    if ((pnpInterfaceClientHandle == NULL) || (userContextCallback == NULL))
    {
        LogError("Invalid parameter, pnpInterfaceClientHandle=%p, userContextCallback=%p", pnpInterfaceClientHandle, userContextCallback);
        result = PNP_CLIENT_ERROR_INVALID_ARG;
    }
    else
    {
        PNP_REPORT_PROPERTIES_UPDATE_CALLBACK_CONTEXT* pnpReportedPropertyCallback = (PNP_REPORT_PROPERTIES_UPDATE_CALLBACK_CONTEXT*)userContextCallback;

        if (pnpReportedPropertyCallback->pnpReportedPropertyCallback == NULL)
        {
            // Caller did not register a callback for this property.  Not an error, no logging needed.
            result = PNP_CLIENT_OK;
        }
        else if (BeginInterfaceCallbackProcessing(pnpInterfaceClient) != 0)
        {
            LogError("Cannot process callback for interface");
            result = PNP_CLIENT_ERROR_SHUTTING_DOWN;
        }
        else
        {
            pnpReportedPropertyCallback->pnpReportedPropertyCallback(pnpReportedStatus, pnpReportedPropertyCallback->userContextCallback);           
            EndInterfaceCallbackProcessing(pnpInterfaceClient);
            result = PNP_CLIENT_OK;
        }

        free(pnpReportedPropertyCallback);
    }

    return result;
}
