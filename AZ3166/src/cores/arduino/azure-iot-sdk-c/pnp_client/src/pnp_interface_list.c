// Copyright (c) Microsoft. All rights reserved. 
// Licensed under the MIT license. See LICENSE file in the project root for full license information.


#include <stdio.h>
#include <stdlib.h>

#include "azure_c_shared_utility/xlogging.h"
#include "azure_c_shared_utility/crt_abstractions.h"
#include "azure_c_shared_utility/gballoc.h"

#include "internal/pnp_interface_list.h"
#include "internal/pnp_raw_interface.h"
#include "parson.h"

#define PNP_JSON_INTERFACES_NAME_VALUE "__iot:interfaces";
static const char* PNP_JSON_INTERFACES_NAME = PNP_JSON_INTERFACES_NAME_VALUE
static const char* PNP_JSON_INTERFACE_DEFINITION = "@id";
static const char* PNP_JSON_REPORTED_INTERFACES_NAME = "reported." PNP_JSON_INTERFACES_NAME_VALUE;

// PNP_INTERFACE_LIST represents the list of currently registered interfaces.  It also
// tracks the interfaces as registered by the server.
typedef struct PNP_INTERFACE_LIST_TAG
{
    // Interfaces registered from client
    PNP_INTERFACE_CLIENT_CORE_HANDLE* pnpInterfaceClientHandles;
    unsigned int numPnpInterfaceClientHandles;
    // Interfaces as they are registered in twin.  These two lists may be different and need reconciliation.
    char** interfacesRegisteredWithTwin;
    unsigned int numInterfacesRegisteredWithTwin;
} PNP_INTERFACE_LIST;

// Creates a PNP_INTERFACE_LIST_HANDLE.
PNP_INTERFACE_LIST_HANDLE Pnp_InterfaceList_Create()
{
    PNP_INTERFACE_LIST* pnpInterfaceList;

    if ((pnpInterfaceList = calloc(1, sizeof(PNP_INTERFACE_LIST))) == NULL)
    {
        LogError("Cannot allocate interface list");
    }

    return (PNP_INTERFACE_LIST_HANDLE)pnpInterfaceList;
}

// Resets data associated with a registered twin; freeing it and resetting values to NULL / 0.
static void ResetInterfacesRegisteredWithTwin(PNP_INTERFACE_LIST* pnpInterfaceList)
{
    for (unsigned int i = 0; i < pnpInterfaceList->numInterfacesRegisteredWithTwin; i++)
    {
        free(pnpInterfaceList->interfacesRegisteredWithTwin[i]);
    }

    free(pnpInterfaceList->interfacesRegisteredWithTwin);
    pnpInterfaceList->interfacesRegisteredWithTwin = NULL;
    pnpInterfaceList->numInterfacesRegisteredWithTwin = 0;
}

// Destroys a PNP_INTERFACE_LIST_HANDLE .
void PnP_InterfaceList_Destroy(PNP_INTERFACE_LIST_HANDLE pnpInterfaceListHandle)
{
    if (pnpInterfaceListHandle != NULL)
    {
        PNP_INTERFACE_LIST* pnpInterfaceList = (PNP_INTERFACE_LIST*)pnpInterfaceListHandle;
        free(pnpInterfaceList->pnpInterfaceClientHandles);
        ResetInterfacesRegisteredWithTwin(pnpInterfaceList);
        free(pnpInterfaceList);
    }
}

// Marks all interface handles as being unregistered.  This could result from a deletion, but could
// also happens as first step of interface registration.
static void UnregisterExistingInterfaceHandles(PNP_INTERFACE_LIST* pnpInterfaceList)
{
    for (unsigned int i = 0; i < pnpInterfaceList->numPnpInterfaceClientHandles; i++)
    {
        (void)PnP_InterfaceClientCore_MarkUnregistered(pnpInterfaceList->pnpInterfaceClientHandles[i]);
    }
    
    pnpInterfaceList->numPnpInterfaceClientHandles = 0;
    free(pnpInterfaceList->pnpInterfaceClientHandles);
    pnpInterfaceList->pnpInterfaceClientHandles = NULL;
}

// PnP_InterfaceList_RegisterInterfaces clears out any existing interfaces already registered, indicates to underlying
// PNP_INTERFACE_CLIENT_CORE_HANDLE's that they're being registered, and stores list.
PNP_CLIENT_RESULT PnP_InterfaceList_RegisterInterfaces(PNP_INTERFACE_LIST_HANDLE pnpInterfaceListHandle, PNP_INTERFACE_CLIENT_CORE_HANDLE* pnpInterfaces, unsigned int numPnpInterfaces)
{
    PNP_CLIENT_RESULT result;

    if (pnpInterfaceListHandle == NULL)
    {
        LogError("Invalid parameter: pnpInterfaceListHandle=%p", pnpInterfaceListHandle);
        result = PNP_CLIENT_ERROR_INVALID_ARG;
    }
    else
    {
        PNP_INTERFACE_LIST* pnpInterfaceList = (PNP_INTERFACE_LIST*)pnpInterfaceListHandle;
        UnregisterExistingInterfaceHandles(pnpInterfaceList);
        unsigned int i;
        
        if ((numPnpInterfaces > 0) &&  (pnpInterfaceList->pnpInterfaceClientHandles = (PNP_INTERFACE_CLIENT_CORE_HANDLE*)calloc(numPnpInterfaces, sizeof(PNP_INTERFACE_CLIENT_CORE_HANDLE))) == NULL)
        {
            LogError("Cannot allocate interfaces");
            result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
        }
        else
        {
            result = PNP_CLIENT_OK;

            for (i = 0; i < numPnpInterfaces; i++)
            {
                if ((result = PnP_InterfaceClientCore_MarkRegistered(pnpInterfaces[i])) != PNP_CLIENT_OK)
                {
                    LogError("Cannot register PnP interface %d in list", i);
                    break;
                }
                else
                {
                    pnpInterfaceList->pnpInterfaceClientHandles[i] = pnpInterfaces[i];
                    pnpInterfaceList->numPnpInterfaceClientHandles++;
                }
            }

            if (i == numPnpInterfaces)
            {
                result = PNP_CLIENT_OK;
            }
            else
            {
                // Otherwise we're in an error state and we should unregister any interfaces that may have been set to reset them.
                UnregisterExistingInterfaceHandles(pnpInterfaceList);
            }
        }
    }

    return result;
}


// UnregisterExistingInterfaceHandles is used to tell registered interfaces that clientCore no longer
// needs reference to them.  For PnP_ClientCore_Destroy, this is straightforward.
// If PnP_ClientCore_RegisterInterfacesAsync() is called multiple times, we first need to mark the interfaces
// as not in a registered state.  The same PNP_INTERFACE_CLIENT_CORE_HANDLE may be safely passed into multiple
// calls to PnP_ClientCore_RegisterInterfacesAsync; in that case this will just momentarily UnRegister the
// interface until the next stage re-registers it.  If the interface client isn't being re-registered, however,
// this step is required to effectively DeleteReference on the handle so it can be destroyed.
void PnP_InterfaceList_UnregisterHandles(PNP_INTERFACE_LIST_HANDLE pnpInterfaceListHandle)
{
    PNP_INTERFACE_LIST* pnpInterfaceList = (PNP_INTERFACE_LIST*)pnpInterfaceListHandle;

    if (NULL == pnpInterfaceList)
    {
        LogError("Invalid parameter.  pnpInterfaceList=%p", pnpInterfaceList);
    }
    else
    {
        UnregisterExistingInterfaceHandles(pnpInterfaceList);
    }
}

// Validates that the pnpInterfaceClientHandle is still in list of registered interface handles.  It's possible,
// for example, that (A) a request to send telemetry and it was posted, (B) the caller re-ran PnP_ClientCore_RegisterInterfacesAsync() without the 
// given interface, and (C) the response callback for given interface arrives on core layer.  In this case we need to swallow the message.
static bool IsInterfaceHandleValid(PNP_INTERFACE_LIST* pnpInterfaceList, PNP_INTERFACE_CLIENT_CORE_HANDLE pnpInterfaceClientHandle)
{
    bool result = false;

    for (unsigned int i = 0; i < pnpInterfaceList->numPnpInterfaceClientHandles; i++)
    {
        if (pnpInterfaceList->pnpInterfaceClientHandles[i] == pnpInterfaceClientHandle)
        {
            result = true;
            break;
        }
    }

    return result;
}

// Checks to see if interface of given name is in our list.
static bool IsInterfaceNameInRegisteredList(PNP_INTERFACE_LIST* pnpInterfaceList, const char* interfaceNameToQuery)
{
    bool result = false;

    for (unsigned int i = 0; i < pnpInterfaceList->numPnpInterfaceClientHandles; i++)
    {
        const char* interfaceName = PnP_InterfaceClientCore_GetInterfaceName(pnpInterfaceList->pnpInterfaceClientHandles[i]);
        if (0 == strcmp(interfaceName, interfaceNameToQuery))
        {
            result = true;
            break;
        }
    }

    return result;
}

PNP_COMMAND_PROCESSOR_RESULT PnP_InterfaceList_InvokeCommand(PNP_INTERFACE_LIST_HANDLE pnpInterfaceListHandle, const char* method_name, const unsigned char* payload, size_t size, unsigned char** response, size_t* response_size, int* resultFromCommandCallback)
{
    PNP_COMMAND_PROCESSOR_RESULT commandProcessorResult;

    if (pnpInterfaceListHandle == NULL)
    {
        commandProcessorResult = PNP_COMMAND_PROCESSOR_ERROR;
    }
    else
    {
        PNP_INTERFACE_LIST* pnpInterfaceList = (PNP_INTERFACE_LIST*)pnpInterfaceListHandle;
        commandProcessorResult = PNP_COMMAND_PROCESSOR_NOT_APPLICABLE;

        unsigned int i;
        for (i = 0; i < pnpInterfaceList->numPnpInterfaceClientHandles; i++)
        {
            // We visit each registered interface to see if it can process this command.  If it's not for this interface, it just
            // returns PNP_COMMAND_PROCESSOR_NOT_APPLICABLE and we continue search.
            commandProcessorResult = PnP_InterfaceClientCore_InvokeCommandIfSupported(pnpInterfaceList->pnpInterfaceClientHandles[i], method_name, payload, size, response, response_size, resultFromCommandCallback);
            if (commandProcessorResult != PNP_COMMAND_PROCESSOR_NOT_APPLICABLE)
            {
                // The visited interface processed (for failure or success), stop searching.
                break;
            }
        }
    }

    return commandProcessorResult;
}

// ProcessInterfacesAlreadyRegisteredByTwin scans the twin data for any registered interfaces - if any - and stores them.
// We later use this list to see if the application is removing any interfaces (and hence we'd need to NULL out unset ones in json).
static PNP_CLIENT_RESULT ProcessInterfacesAlreadyRegisteredByTwin(PNP_INTERFACE_LIST* pnpInterfaceList, JSON_Object* root_object)
{
    JSON_Object* pnp_interfaces = NULL;
    unsigned int numInterfacesRegisteredWithTwin;
    PNP_CLIENT_RESULT result = PNP_CLIENT_ERROR;

    ResetInterfacesRegisteredWithTwin(pnpInterfaceList);

    if ((pnp_interfaces = json_object_dotget_object(root_object, PNP_JSON_REPORTED_INTERFACES_NAME)) == NULL)
    {
        // Not having interfaces set is not an error
        result = PNP_CLIENT_OK;
    }
    else if ((numInterfacesRegisteredWithTwin = (unsigned int)json_object_get_count(pnp_interfaces)) == 0)
    {
        // Not having any interfaces registered already isn't an error and will be our state on initial registration.
        result = PNP_CLIENT_OK;
    }
    else if ((pnpInterfaceList->interfacesRegisteredWithTwin = calloc(1, sizeof(char*) * numInterfacesRegisteredWithTwin)) == NULL)
    {
        LogError("Cannot Map_Create registeredInterfaces");
        result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
    }
    else
    {
        unsigned int i;

        for (i = 0; i < numInterfacesRegisteredWithTwin; i++)
        {
            JSON_Value* pnp_interface_value = NULL;
            JSON_Object* pnp_interface_obj = NULL;
            const char* interfaceName; // friendly interface that will map back to application.

            if ((pnp_interface_value = json_object_get_value_at(pnp_interfaces, i)) == NULL)
            {
                LogError("Failed retrieving existing index element %d", i);
                result = PNP_CLIENT_ERROR;
                break;
            }
            else if ((pnp_interface_obj = json_value_get_object(pnp_interface_value)) == NULL)
            {
                LogError("Failed retrieving existing index element %d", i);
                result = PNP_CLIENT_ERROR;
                break;
            }
            else if ((interfaceName = json_object_get_string(pnp_interface_obj, PNP_JSON_INTERFACE_DEFINITION)) == NULL)
            {
                LogError("Failed gettind %s field for existing index element %d", PNP_JSON_INTERFACE_DEFINITION, i);
                result = PNP_CLIENT_ERROR;
                break;
            }
            else if (mallocAndStrcpy_s(&pnpInterfaceList->interfacesRegisteredWithTwin[i], interfaceName) != 0)
            {
                LogError("mallocAndStrcpy_s fails");
                result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
                break;
            }

            pnpInterfaceList->numInterfacesRegisteredWithTwin++;

            // Do not "json free" pnp_interface_obj or pnp_interface_value; these are unreferenced
            // pointers still owned by pnp_interfaces object tree.
        }       

        if (i == numInterfacesRegisteredWithTwin)
        {
            result = PNP_CLIENT_OK;
        }
        
    }

    return result;
}

// Processes twin, looking explicitly for interface registration.
PNP_CLIENT_RESULT PnP_InterfaceList_ProcessTwinCallbackForRegistration(PNP_INTERFACE_LIST_HANDLE pnpInterfaceListHandle, bool fullTwin, const unsigned char* payLoad, size_t size)
{
    PNP_INTERFACE_LIST* pnpInterfaceList = (PNP_INTERFACE_LIST*)pnpInterfaceListHandle;
    STRING_HANDLE jsonStringHandle = NULL;
    const char* jsonString;

    (void)fullTwin; // TODO: Need to respect this when querying.

    PNP_CLIENT_RESULT result;
    JSON_Value* root_value = NULL;
    JSON_Object* root_object = NULL;

    if ((pnpInterfaceListHandle == NULL) || (payLoad == NULL) || (size == 0))
    {
        LogError("Invalid parameter(s): pnpInterfaceListHandle=%p, payLoad=%p, size=%lu", pnpInterfaceListHandle, payLoad, (unsigned long)size);
        result = PNP_CLIENT_ERROR_INVALID_ARG;
    }
    else if ((jsonStringHandle = STRING_from_byte_array(payLoad, size)) == NULL)
    {
        LogError("STRING_from_byte_array failed");
        result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
    }
    else 
    {
        jsonString = STRING_c_str(jsonStringHandle);

        if ((root_value = json_parse_string(jsonString)) == NULL)
        {
            LogError("Unable to parse json string %.*s", (int)size, payLoad);
            result = PNP_CLIENT_ERROR;
        }
        else if ((root_object = json_value_get_object(root_value)) == NULL)
        {
            LogError("json_value_get_object failed");
            result = PNP_CLIENT_ERROR;
        }
        else if ((result = ProcessInterfacesAlreadyRegisteredByTwin(pnpInterfaceList, root_object)) != PNP_CLIENT_OK)
        {
            LogError("ProcessInterfacesAlreadyRegisteredByTwin fails, err=%d", result);
        }
        else
        {
            result = PNP_CLIENT_OK;
        }
    }

    STRING_delete(jsonStringHandle);

    if (root_object != NULL)
    {
        json_object_clear(root_object);
    }
    
    if (root_value != NULL)
    {
        json_value_free(root_value);
    }

    return result;
}

// Processes twin, looking explicitly property handling.
PNP_CLIENT_RESULT PnP_InterfaceList_ProcessTwinCallbackForProperties(PNP_INTERFACE_LIST_HANDLE pnpInterfaceListHandle, bool fullTwin, const unsigned char* payLoad, size_t size)
{
    PNP_INTERFACE_LIST* pnpInterfaceList = (PNP_INTERFACE_LIST*)pnpInterfaceListHandle;
    PNP_CLIENT_RESULT result;

    if (pnpInterfaceListHandle == NULL)
    {
        LogError("Invalid parameter.  pnpInterfaceListHandle=%p", pnpInterfaceListHandle);
        result = PNP_CLIENT_ERROR_INVALID_ARG;
    }
    else
    {
        // Invoke interface callbacks for any new/updated fields.
        for (unsigned int i = 0; i < pnpInterfaceList->numPnpInterfaceClientHandles; i++)
        {
            PnP_InterfaceClientCore_ProcessTwinCallback(pnpInterfaceList->pnpInterfaceClientHandles[i], fullTwin, payLoad, size);
        }

        result = PNP_CLIENT_OK;
    }

    return result;
}

// Invoked when PnP Core layer processes has a property update callback.
PNP_CLIENT_RESULT PnP_InterfaceList_ProcessReportedPropertiesUpdateCallback(PNP_INTERFACE_LIST_HANDLE pnpInterfaceListHandle, PNP_INTERFACE_CLIENT_HANDLE pnpInterfaceClientHandle, PNP_REPORTED_PROPERTY_STATUS pnpReportedStatus, void* userContextCallback)
{
    PNP_INTERFACE_LIST* pnpInterfaceList = (PNP_INTERFACE_LIST*)pnpInterfaceListHandle;
    PNP_CLIENT_RESULT result;

    if (pnpInterfaceListHandle == NULL)
    {
        LogError("Invalid parameter.  pnpInterfaceListHandle=%p", pnpInterfaceListHandle);
        result = PNP_CLIENT_ERROR_INVALID_ARG;
    }
    else if (IsInterfaceHandleValid(pnpInterfaceList, pnpInterfaceClientHandle) == false)
    {
        // Even though the interface is always valid at time of the reported property dispatch, this can happen if
        // the caller destroyed the interface before the callback completed.
        LogError("Interface for handle %p is no longer valid", pnpInterfaceClientHandle);
        result = PNP_CLIENT_ERROR_INTERFACE_NOT_PRESENT;
    }
    else
    {
        (void)PnP_InterfaceClientCore_ProcessReportedPropertiesUpdateCallback(pnpInterfaceClientHandle, pnpReportedStatus, userContextCallback);
        result = PNP_CLIENT_OK;
    }

    return result;
}

// Invoked when a SendTelemetry callback has arrived.
PNP_CLIENT_RESULT PnP_InterfaceList_ProcessTelemetryCallback(PNP_INTERFACE_LIST_HANDLE pnpInterfaceListHandle, PNP_INTERFACE_CLIENT_HANDLE pnpInterfaceClientHandle, PNP_SEND_TELEMETRY_STATUS pnpSendTelemetryStatus, void* userContextCallback)
{
    PNP_INTERFACE_LIST* pnpInterfaceList = (PNP_INTERFACE_LIST*)pnpInterfaceListHandle;
    PNP_CLIENT_RESULT result;

    if (pnpInterfaceListHandle == NULL)
    {
        LogError("Invalid parameter.  pnpInterfaceListHandle=%p", pnpInterfaceListHandle);
        result = PNP_CLIENT_ERROR_INVALID_ARG;
    }
    else if (IsInterfaceHandleValid(pnpInterfaceList, pnpInterfaceClientHandle) == false)
    {
        // See comments in PnP_InterfaceList_ProcessReportedPropertiesUpdateCallback why this can fail
        LogError("Interface handle %p is no longer valid; swallowing callback for telemetry", pnpInterfaceClientHandle);
        result = PNP_CLIENT_ERROR_INTERFACE_NOT_PRESENT;
    }
    else
    {
        (void)PnP_InterfaceClientCore_ProcessTelemetryCallback(pnpInterfaceClientHandle, pnpSendTelemetryStatus, userContextCallback);
        result = PNP_CLIENT_OK;
    }

    return result;
}

// Creates a json blob representing the interfaces that we should register.
static PNP_CLIENT_RESULT CreateJsonForInterfacesToSet(PNP_INTERFACE_LIST* pnpInterfaceList, JSON_Object* interfaces_arrayObj)
{
    PNP_CLIENT_RESULT result = PNP_CLIENT_OK;

    for (unsigned int i = 0; i < pnpInterfaceList->numPnpInterfaceClientHandles; i++)
    {
        JSON_Object* interfaceMapObj = NULL;
        JSON_Value* interfaceMapValue = NULL;
        const char* interfaceName = PnP_InterfaceClientCore_GetInterfaceName(pnpInterfaceList->pnpInterfaceClientHandles[i]);
        const char* rawInterfaceName = PnP_InterfaceClientCore_GetRawInterfaceName(pnpInterfaceList->pnpInterfaceClientHandles[i]);

        if ((interfaceMapValue = json_value_init_object()) == NULL)
        {
            LogError("json_value_init_object failed");
            result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
        }
        else if ((interfaceMapObj = json_value_get_object(interfaceMapValue)) == NULL)
        {
            LogError("json_value_get_object failed");
            result = PNP_CLIENT_ERROR;
        }
        else if (json_object_dotset_string(interfaceMapObj, PNP_JSON_INTERFACE_DEFINITION, interfaceName)  != JSONSuccess)
        {
            LogError("json_object_dotset_string failed");
            result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
        }
        else if (json_object_set_value(interfaces_arrayObj, rawInterfaceName, interfaceMapValue) != JSONSuccess)
        {
            LogError("json_object_set_value failed");
            result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
        }
        else
        {
            result = PNP_CLIENT_OK;
        }

        if (result != PNP_CLIENT_OK)
        {
            if (interfaceMapObj != NULL)
            {
                json_object_clear(interfaceMapObj);
            }

            if (interfaceMapValue != NULL)
            {
                json_value_free(interfaceMapValue);
            }

            break;
        }
    }
    return result;
}

// If there are already interfaces registered {A, B, C} in the Cloud but the current API call doesn't set them all {A, B, but NOT C},
// then this function appends appropriate Json ({c : NULL}) to passed interfaces_arrayObj.
static PNP_CLIENT_RESULT CreateJsonForInterfacesToRemove(PNP_INTERFACE_LIST* pnpInterfaceList, JSON_Object* interfaces_arrayObj)
{
    PNP_CLIENT_RESULT result = PNP_CLIENT_ERROR;
    unsigned int i;

    for (i = 0; i < pnpInterfaceList->numInterfacesRegisteredWithTwin; i++)
    {
        if (IsInterfaceNameInRegisteredList(pnpInterfaceList, pnpInterfaceList->interfacesRegisteredWithTwin[i]) == false)
        {
            // The Cloud registered interface isn't in our list of interfaces we're about to register.  Indicate it should be removed.
            const char* rawInterfaceId = PnP_Get_RawInterfaceId(pnpInterfaceList->interfacesRegisteredWithTwin[i]);
            if (rawInterfaceId == NULL)
            {
                LogError("Cannot get raw interface for interfaceId to delete %s", pnpInterfaceList->interfacesRegisteredWithTwin[i]);
                result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
                break;
            }
            else if (json_object_set_null(interfaces_arrayObj, rawInterfaceId) != JSONSuccess)
            {
                free((char*)rawInterfaceId);
                LogError("Cannot json_object_set_value for interfaceId to delete %s", pnpInterfaceList->interfacesRegisteredWithTwin[i]);
                result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
                break;
            }
            else
            {
                free((char*)rawInterfaceId);
            }
        }
    }

    if (i == pnpInterfaceList->numInterfacesRegisteredWithTwin)
    {
        result = PNP_CLIENT_OK;
    }

    return result;
}

// For the current set of pnpInterfaces (already registered with this handle), PnP_InterfaceList_GetInterface_Data returns json for caller to return in Twin.
PNP_CLIENT_RESULT PnP_InterfaceList_GetInterface_Data(PNP_INTERFACE_LIST_HANDLE pnpInterfaceListHandle, char** jsonToSend, size_t* jsonToSendLen)
{
    PNP_INTERFACE_LIST* pnpInterfaceList = (PNP_INTERFACE_LIST*)pnpInterfaceListHandle;
    PNP_CLIENT_RESULT result;

    JSON_Value* root_value = NULL;
    JSON_Object* root_object = NULL;
    JSON_Value* interfaces_array = NULL;
    JSON_Object* interfaces_arrayObj = NULL;

    if ((pnpInterfaceListHandle == NULL) || (jsonToSend == NULL) || (jsonToSendLen == NULL))
    {
        LogError("Invalid parameter(s).  pnpInterfaceListHandle=%p, jsonToSend=%p, jsonToSendLen=%p", pnpInterfaceListHandle, jsonToSend, jsonToSendLen);
        result = PNP_CLIENT_ERROR_INVALID_ARG;
    }
    else if ((root_value = json_value_init_object()) == NULL)
    {
        LogError("json_value_init_object failed");
        result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
    }
    else if ((root_object = json_value_get_object(root_value)) == NULL)
    {
        LogError("json_value_get_object failed");
        result = PNP_CLIENT_ERROR;
    }
    else if ((interfaces_array = json_value_init_object()) == NULL)
    {
        LogError("json_value_init_object failed");
        result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
    }
    else if ((interfaces_arrayObj = json_value_get_object(interfaces_array)) == NULL)
    {
        LogError("json_value_get_object failed");
        result = PNP_CLIENT_ERROR;
    }
    else if (json_object_set_value(root_object, PNP_JSON_INTERFACES_NAME, interfaces_array) != JSONSuccess)
    {
        LogError("json_object_set_value failed");
        result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
    }
    else
    {
        // After json_object_set_value root_object,..,interfaces_array call, the root_object 
        // takes responsibility for cleaning up interfaces_array.
        interfaces_array = NULL;
       
        if ((result = CreateJsonForInterfacesToSet(pnpInterfaceList, interfaces_arrayObj)) != PNP_CLIENT_OK)
        {
            LogError("createJsonForInterfacesToSet failed %d", result);
        }
        else if ((result = CreateJsonForInterfacesToRemove(pnpInterfaceList, interfaces_arrayObj)) != PNP_CLIENT_OK)
        {
            LogError("createJsonForInterfacesToRemove failed %d", result);
        }
        else if ((*jsonToSend = json_serialize_to_string(root_value)) == NULL)
        {
            LogError("json_serialize_to_string failed");
            result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
        }
        else
        {
            *jsonToSendLen = strlen(*jsonToSend);
            result = PNP_CLIENT_OK;
        }
    }
    
    if (interfaces_arrayObj != NULL)
    {
        json_object_clear(interfaces_arrayObj);
    }
    if (interfaces_array != NULL)
    {
        json_value_free(interfaces_array);
    }
    if (root_object != NULL)
    {
        json_object_clear(root_object);
    }
    if (root_value != NULL)
    {
        json_value_free(root_value);
    }
   
    return result;
}

