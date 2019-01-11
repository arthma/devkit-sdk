    // Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdio.h>
#include <stdlib.h>

#include "iothub_device_client.h"
#include "iothub_message.h"
#include "azure_c_shared_utility/threadapi.h"
#include "azure_c_shared_utility/singlylinkedlist.h"
#include "azure_c_shared_utility/xlogging.h"
#include "azure_c_shared_utility/gballoc.h"

#include "internal/pnp_interface_list.h"
#include "internal/pnp_interface_core.h"
#include "internal/pnp_client_core.h"

typedef enum PNP_CLIENT_STATE_TAG
{
    PNP_CLIENT_STATE_RUNNING,      // ClientCore will accept requests.  Default state.
    PNP_CLIENT_STATE_SHUTTING_DOWN // The client shutdown the PnPClient handle, but there are interfaces outstanding to it.
} PNP_CLIENT_STATE;

typedef enum PNP_CLIENT_REGISTRATION_STATUS_TAG
{
    // PNP_CLIENT_REGISTRATION_STATUS_IDLE means that no PnP interfaces are registered nor in the state of being registered.
    // We're in this state after PNP registration, and if registration fails (since we can recover from a fail).
    PNP_CLIENT_REGISTRATION_STATUS_IDLE,
    // PNP_CLIENT_REGISTRATION_STATUS_REGISTERING indicates that PnP is in middle of registering its interfaces.
    // Other PnP operations - including trying to register interfaces again - don't happen in this state.
    PNP_CLIENT_REGISTRATION_STATUS_REGISTERING,
    // PNP_CLIENT_REGISTRATION_STATUS_REGISTERED signals that PnP interfaces have been successfully registered
    // with the service and that we're ready for PnP operations.  We can potentially re-register interfaces-
    // once in this state, which will send us to PNP_CLIENT_REGISTRATION_STATUS_REGISTERING again.
    PNP_CLIENT_REGISTRATION_STATUS_REGISTERED
} PNP_CLIENT_REGISTRATION_STATUS;

typedef struct PNP_REGISTER_INTERFACES_CALLBACK_CONTEXT_TAG
{
    PNP_INTERFACE_REGISTERED_CALLBACK pnpInterfaceRegisteredCallback;
    void* userContextCallback;
} PNP_REGISTER_INTERFACES_CALLBACK_CONTEXT;

typedef enum PNP_REPORTED_PROPERTY_CALLBACK_INVOKER_TAG
{
    PNP_REPORTED_PROPERTY_CALLBACK_INVOKER_REGISTER_INTERFACE,
    PNP_REPORTED_PROPERTY_CALLBACK_INVOKER_UPDATE_PROPERTIES
} PNP_REPORTED_PROPERTY_CALLBACK_INVOKER;

// PNP_CLIENT_CORE is the underlying representation of objects exposed up to the caller
// such as PNP_DEVICE_CLIENT_HANDLE, PNP_DEVICE_CLIENT_LL_HANDLE, etc.
typedef struct PNP_CLIENT_CORE_TAG
{
    int refCount;
    bool processingCallback;    // Whether we're in the middle of processing a callback or not.
    PNP_CLIENT_STATE clientState;
    PNP_IOTHUB_BINDING iothubBinding;
    PNP_CLIENT_REGISTRATION_STATUS registrationStatus;
    SINGLYLINKEDLIST_HANDLE reportedPropertyList; // List of PNP_REPORTED_PROPERTY_CALLBACK_CONTEXT's
    bool registeredForDeviceMethod;
    bool registeredForDeviceTwin;
    PNP_REGISTER_INTERFACES_CALLBACK_CONTEXT registerInterfacesCallbackContext;
    PNP_INTERFACE_LIST_HANDLE pnpInterfaceListHandle;
} PNP_CLIENT_CORE;

typedef struct PNP_REPORTED_PROPERTY_CALLBACK_CONTEXT_TAG
{
    PNP_CLIENT_CORE* pnpClientCore;
    PNP_INTERFACE_CLIENT_CORE_HANDLE pnpInterfaceClientHandle;
    PNP_REPORTED_PROPERTY_CALLBACK_INVOKER callbackInvoker;
    void* userContextCallback;
} PNP_REPORTED_PROPERTY_CALLBACK_CONTEXT;

typedef struct PNP_SEND_TELEMETRY_CALLBACK_CONTEXT_TAG
{
    PNP_CLIENT_CORE* pnpClientCore;
    PNP_INTERFACE_CLIENT_CORE_HANDLE pnpInterfaceClientHandle;
    void* userContextCallback;
} PNP_SEND_TELEMETRY_CALLBACK_CONTEXT;


// When we're poll active callbacks to drain while shutting down PnP Client Core,
// this is the amount of time to sleep between poll intervals.
static const unsigned int pollTimeWaitForCallbacksMilliseconds = 10;


// Error codes for commands used when interface callback doesn't process command for whatever reason.
static const int methodErrorStatusCode = 500;
static const char methodNotPresentError[] = "{ \"Response\": \"Method not present\" }";
static const size_t methodNotPresentErrorLen = sizeof(methodNotPresentError) - 1;
static const char methodInternalError[] =  "{ \"Response\": \"Internal error\" }";
static const size_t methodInternalErrorLen = sizeof(methodInternalError) - 1;

DEFINE_ENUM_STRINGS(PNP_REPORTED_INTERFACES_STATUS, PNP_REPORTED_INTERFACES_STATUS_VALUES);

// Converts codes from IoTHub* API's into corresponding PNP error codes.
static PNP_SEND_TELEMETRY_STATUS GetPnPSendStatusCodeFromIoTHubStatus(IOTHUB_CLIENT_CONFIRMATION_RESULT iothubResult)
{
    PNP_SEND_TELEMETRY_STATUS pnpSendTelemetryStatus;
    switch (iothubResult)
    {
        case IOTHUB_CLIENT_CONFIRMATION_OK:
            pnpSendTelemetryStatus = PNP_SEND_TELEMETRY_STATUS_OK;
            break;

        case IOTHUB_CLIENT_CONFIRMATION_BECAUSE_DESTROY:
            pnpSendTelemetryStatus = PNP_SEND_TELEMETRY_STATUS_ERROR_HANDLE_DESTROYED;
            break;

        case IOTHUB_CLIENT_CONFIRMATION_MESSAGE_TIMEOUT:
            pnpSendTelemetryStatus = PNP_SEND_TELEMETRY_STATUS_ERROR_TIMEOUT;
            break;

        default:
            pnpSendTelemetryStatus = PNP_SEND_TELEMETRY_STATUS_ERROR;
            break;
    }

    return pnpSendTelemetryStatus;
}

// Invokes appropriate IoTHub*_SendEventAsync function for given handle type.
static int InvokeBindingSendEventAsync(PNP_CLIENT_CORE* pnpClientCore, IOTHUB_MESSAGE_HANDLE eventMessageHandle, IOTHUB_CLIENT_EVENT_CONFIRMATION_CALLBACK eventConfirmationCallback, PNP_SEND_TELEMETRY_CALLBACK_CONTEXT* pnpSendTelemetryCallbackContext)
{
    return pnpClientCore->iothubBinding.pnpDeviceSendEventAsync(pnpClientCore->iothubBinding.iothubClientHandle, eventMessageHandle, eventConfirmationCallback, pnpSendTelemetryCallbackContext);
}

// Invokes appropriate IoTHub*_DoWork function for given handle type.
static void InvokeBindingDoWork(PNP_CLIENT_CORE* pnpClientCore)
{
    // DoWork by definition is only for single-threaded scenarios where there is no expectation (or even implementation)
    // of locking logic.  So no need to InvokeBindingLock() or unlock here.
    pnpClientCore->iothubBinding.pnpDeviceClientDoWork(pnpClientCore->iothubBinding.iothubClientHandle);
}

// Invokes Lock() (for convenience layer based handles) or else a no-op (for _LL_)
static int InvokeBindingLock(PNP_CLIENT_CORE* pnpClientCore, bool* lockHeld)
{
    LOCK_RESULT lockResult = pnpClientCore->iothubBinding.pnpBindingLock(pnpClientCore->iothubBinding.pnpBindingLockHandle);
    int result;

    if (lockResult != LOCK_OK)
    {
        LogError("Failed to grab lock, result = %d", lockResult);
        result = __FAILURE__;
    }
    else
    {
        // Setting whether lock is held here helps caller on cleanup.
        *lockHeld = true;
        result = 0;        
    }

    return result;
}
// Invokes Unlock() (for convenience layer based handles) or else a no-op (for _LL_)
static int InvokeBindingUnlock(PNP_CLIENT_CORE* pnpClientCore, bool* lockHeld)
{
    LOCK_RESULT lockResult;
    int result;
    
    if (*lockHeld == true)
    {
        lockResult = pnpClientCore->iothubBinding.pnpBindingUnlock(pnpClientCore->iothubBinding.pnpBindingLockHandle);
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
static void InvokeBindingLockDeinitIfNeeded(PNP_CLIENT_CORE* pnpClientCore)
{
    if (pnpClientCore->iothubBinding.pnpBindingLockHandle != NULL)
    {
        pnpClientCore->iothubBinding.pnpBindingLockDeinit(pnpClientCore->iothubBinding.pnpBindingLockHandle);
    }
}

// Invokes Thread_Sleep (for convenience layer based handles) or else a no-op (for _LL_)
static void InvokeBindingSleep(PNP_CLIENT_CORE* pnpClientCore, unsigned int milliseconds)
{
    pnpClientCore->iothubBinding.pnpBindingThreadSleep(milliseconds);
}

// Invokes IoTHub*_Destroy() for given handle
static void InvokeBindingDeviceClientDestroyIfNeeded(PNP_CLIENT_CORE* pnpClientCore)
{
    if (pnpClientCore->iothubBinding.iothubClientHandle != NULL)
    {
        pnpClientCore->iothubBinding.pnpDeviceClientDestroy(pnpClientCore->iothubBinding.iothubClientHandle);
        pnpClientCore->iothubBinding.iothubClientHandle = NULL;
    }
}

// Invokes IoTHub*_SetDeviceTwinCallback for given handle (if not already registered)
static int InvokeBindingSetDeviceTwinCallbackIfNeeded(PNP_CLIENT_CORE* pnpClientCore, IOTHUB_CLIENT_DEVICE_TWIN_CALLBACK deviceTwinCallback)
{
    int result;

    if (pnpClientCore->registeredForDeviceTwin)
    {
        // We've already registered for twin notifications.  No need to do so again.
        result = 0;
    }
    else
    {
        result = pnpClientCore->iothubBinding.pnpDeviceSetDeviceTwinCallback(pnpClientCore->iothubBinding.iothubClientHandle, deviceTwinCallback, pnpClientCore);
        if (result == 0)
        {
            pnpClientCore->registeredForDeviceTwin = true;
        }
    }

    return result;
}


// BeginClientCoreCallbackProcessing is invoked as the first step when PNP_CLIENT_CORE receives
// a callback from the IoTHub_* layer.  If client is shutting down it will immediately
// exit out of the callback.  Otherwise mark our state as processing callback, as the
// primary state change API's (PnP_ClientCore_RegisterInterfacesAsync and PnP_ClientCore_Destroy) respect this.
static int BeginClientCoreCallbackProcessing(PNP_CLIENT_CORE* pnpClientCore)
{
    int result;
    bool lockHeld = false;

    if (InvokeBindingLock(pnpClientCore, &lockHeld) != 0)
    {
        LogError("Unable to obtain lock");
        result = __FAILURE__;
    }
    else if (pnpClientCore->clientState == PNP_CLIENT_STATE_SHUTTING_DOWN)
    {
        LogError("Cannot process callback for clientCore.  It is in process of being destroyed");
        result = __FAILURE__;
    }
    else
    {
        pnpClientCore->processingCallback = true;
        result = 0;
    }

    (void)InvokeBindingUnlock(pnpClientCore, &lockHeld);
    return result;
}

// EndClientCoreCallbackProcessing is invoked on completion of a callback function
// to change PNP_CLIENT_CORE's state such that it's not processing the callback anymore.
static void EndClientCoreCallbackProcessing(PNP_CLIENT_CORE* pnpClientCore)
{
    bool lockHeld = false;

    if (InvokeBindingLock(pnpClientCore, &lockHeld) != 0)
    {
        LogError("Unable to obtain lock");
    }

    // Even if we can't grab the lock, unconditionally set this.
    pnpClientCore->processingCallback = false;
    (void)InvokeBindingUnlock(pnpClientCore, &lockHeld);
}

// Invokes appropriate IoTHub*_SetDeviceMethodCallback for given handle type.
static int InvokeBindingSetDeviceMethodCallbackIfNeeded(PNP_CLIENT_CORE* pnpClientCore, IOTHUB_CLIENT_DEVICE_METHOD_CALLBACK_ASYNC deviceMethodCallback)
{
    int result;

    if (pnpClientCore->registeredForDeviceMethod == true)
    {
        result = 0;
    }
    else
    {
        result = pnpClientCore->iothubBinding.pnpDeviceSetDeviceMethodCallback((void*)pnpClientCore->iothubBinding.iothubClientHandle, deviceMethodCallback, pnpClientCore);
        if (result == 0)
        {
            pnpClientCore->registeredForDeviceMethod = true;
        }
    }

    return result;
}

// Unconditionally PNP_REPORTED_PROPERTY_CALLBACK_CONTEXT item during list destruction.
static bool RemoveReportedPropertyCallbackContext(const void* item, const void* match_context, bool* continue_processing)
{
    (void)match_context;
    free((void*)item);
    *continue_processing = true;
    return true;
}

// When we can't invoke a user defined command callback, the PnP framework 
// itself is responsible for reporting errors to the caller.
// MapResultToMethodError errors into (roughly) corresponding HTTP error codes.
// Note that PnP caller does not free memory allocated here; instead IoTHub_* layer free()'s after use.
static int MapResultToMethodError(PNP_CLIENT_RESULT pnpClientResult, unsigned char** response, size_t* response_size)
{
    int result;

    switch (pnpClientResult)
    {
        case PNP_CLIENT_ERROR_COMMAND_NOT_PRESENT:
            if (mallocAndStrcpy_s((char**)response, methodNotPresentError) != 0)
            {
                LogError("Cannot malloc error string for return to caller");
                *response = NULL;
                *response_size = 0;
            }
            else
            {
                *response_size = methodNotPresentErrorLen;
            }
            result = 404;
            break;

        default:
            if (mallocAndStrcpy_s((char**)response, methodInternalError) != 0)
            {
                LogError("Cannot malloc error string for return to caller");
                *response = NULL;
                *response_size = 0;
            }
            else
            {
                *response_size = methodInternalErrorLen;
            }
            result = 500;
            break;
    }

    return result;
}


// Layer invoked when a device method for PnP is received.
static int PnPDeviceMethod_Callback(const char* method_name, const unsigned char* payload, size_t size, unsigned char** response, size_t* response_size, void* userContextCallback)
{
    PNP_CLIENT_CORE* pnpClientCore = (PNP_CLIENT_CORE*)userContextCallback;
    int result = methodErrorStatusCode;

    if (BeginClientCoreCallbackProcessing(pnpClientCore) != 0)
    {
        LogError("Skipping callback processing");
        result = MapResultToMethodError(PNP_CLIENT_ERROR, response, response_size);
    }
    else
    {
        PNP_COMMAND_PROCESSOR_RESULT commandProcessorResult = PnP_InterfaceList_InvokeCommand(pnpClientCore->pnpInterfaceListHandle, method_name, payload, size, response, response_size, &result);
        if (commandProcessorResult != PNP_COMMAND_PROCESSOR_PROCESSED)
        {
            LogError("Command %s is not handled by any interface registered interface", method_name);
            result = MapResultToMethodError(PNP_CLIENT_ERROR_COMMAND_NOT_PRESENT, response, response_size);
        }

        EndClientCoreCallbackProcessing(pnpClientCore);
    }

    return result;
}

// Removes resources associated with PNP_CLIENT_CORE.  The lock must not be held at this point and ref count should be 0.
static void FreePnPDeviceClientCore(PNP_CLIENT_CORE* pnpClientCore)
{
    // The destruction of the IoTHub binding handle MUST be the first action we take.  For convenience layer, this call will
    // block until IoTHub's dispatcher thread routine has completed.  By invoking this first, we ensure that there's
    // no possibility that a callback will arrive after we've deleted underlying data structure in PNP_CLIENT_CORE.
    InvokeBindingDeviceClientDestroyIfNeeded(pnpClientCore);
    
    if (pnpClientCore->reportedPropertyList != NULL)
    {
        singlylinkedlist_remove_if(pnpClientCore->reportedPropertyList, RemoveReportedPropertyCallbackContext, NULL);
        singlylinkedlist_destroy(pnpClientCore->reportedPropertyList);
    }
    
    PnP_InterfaceList_Destroy(pnpClientCore->pnpInterfaceListHandle);
    InvokeBindingLockDeinitIfNeeded(pnpClientCore);
    free(pnpClientCore);
}

// Polls for whether there are any active callbacks, because certain operations (like destroying a PnP client)
// cannot proceed if there are active workers.  Enters and leaves with lock held.
static void BlockOnActiveCallbacks(PNP_CLIENT_CORE* pnpClientCore, bool *lockHeld)
{
    while (pnpClientCore->processingCallback == true)
    {
        (void)InvokeBindingUnlock(pnpClientCore, lockHeld);
        InvokeBindingSleep(pnpClientCore, pollTimeWaitForCallbacksMilliseconds);
        (void)InvokeBindingLock(pnpClientCore, lockHeld);
    }
}

// Destroys a PNP_CLIENT_CORE object once ref count is 0. We can't immediately delete the data, because interfaces may be pointing at it.  
// However, the ClientCore should be considered invalid at this state (interfaces trying to use it should fail).
// Furthermore we block until all worker threads have completed.
void PnP_ClientCore_Destroy(PNP_CLIENT_CORE_HANDLE pnpClientCoreHandle)
{
    PNP_CLIENT_CORE* pnpClientCore = (PNP_CLIENT_CORE*)pnpClientCoreHandle;
    if (pnpClientCore == NULL)
    {
        LogError("Invalid parameter.  pnpClientCore=%p", pnpClientCore);
    }
    else
    {
        bool lockHeld = false;
        
        if (InvokeBindingLock(pnpClientCore, &lockHeld) != 0)
        {
            LogError("bindingLock failed");
        }
        else
        {
            // Even though there maybe interface pointers with references to this device still, 
            // destroying the PNP_CLIENT_CORE_HANDLE means that subsequent calls they make as 
            // well as any pending IoTHub* callbacks should immediately fail.
            pnpClientCore->clientState = PNP_CLIENT_STATE_SHUTTING_DOWN;
            // Unregistering handles dereferences theme
            PnP_InterfaceList_UnregisterHandles(pnpClientCore->pnpInterfaceListHandle);

            // We must poll until all callback threads have been processed.  Once we return from destroy, 
            // the caller is allowed to free any resources / interfaces associated with ClientCore so
            // we must be sure that they can never be invoked via a callback again.
            BlockOnActiveCallbacks(pnpClientCore, &lockHeld);

            pnpClientCore->refCount--;

            (void)InvokeBindingUnlock(pnpClientCore, &lockHeld);

            if (pnpClientCore->refCount == 0)
            {
                FreePnPDeviceClientCore(pnpClientCore);
            }
        }

    }

}

// When a PNP_INTERFACE_* is destroyed, it release the reference PNP_CLIENT_CORE_HANDLE holds on it through this
// function.  Like standard Release() semantics, on last reference removal this will delete the object.
// Unlike PnP_ClientCore_Destroy, however, we don't mark the PNP_CLIENT_CORE_HANDLE as in a destroy state.
void PnP_ClientCore_RemoveInterfaceReference(PNP_CLIENT_CORE_HANDLE pnpClientCoreHandle)
{
    if (pnpClientCoreHandle == NULL)
    {
        LogError("Invalid parameter, pnpClientCore=%p", pnpClientCoreHandle);
    }
    else 
    {
        PNP_CLIENT_CORE* pnpClientCore = (PNP_CLIENT_CORE*)pnpClientCoreHandle;
        bool lockHeld = false;
        
        if (InvokeBindingLock(pnpClientCore, &lockHeld) != 0)
        {
            LogError("bindingLock failed");
        }
        else
        {
            pnpClientCore->refCount--;
            (void)InvokeBindingUnlock(pnpClientCore, &lockHeld);
        }
        
        if (pnpClientCore->refCount == 0)
        {
            FreePnPDeviceClientCore(pnpClientCore);
        }
    }
}

// When an interface created it is bound to a PNP_CLIENT_CORE_HANDLE, so increase ref count on PNP_CLIENT_CORE_HANDLE.
PNP_CLIENT_RESULT PnP_ClientCore_AddInterfaceReferenceFromInterface(PNP_CLIENT_CORE_HANDLE pnpClientCoreHandle)
{
    PNP_CLIENT_CORE* pnpClientCore = (PNP_CLIENT_CORE*)pnpClientCoreHandle;
    PNP_CLIENT_RESULT result;
    bool lockHeld = false;

    if (pnpClientCoreHandle == NULL)
    {
        LogError("PnP_ClientCore_AddInterfaceReferenceFromInterface failed, NULL pnpClientCoreHandle");
        result = PNP_CLIENT_ERROR_INVALID_ARG;
    }
    else if (InvokeBindingLock(pnpClientCore, &lockHeld) != 0)
    {
        LogError("bindingLock failed");
        result = PNP_CLIENT_ERROR;
    }
    else
    {
        pnpClientCore->refCount++;
        result = PNP_CLIENT_OK;
    }

    (void)InvokeBindingUnlock(pnpClientCore, &lockHeld);
    return result;
}

// PnP_ClientCore_Create creates the PNP_CLIENT_CORE_HANDLE object, which the upper layer
// will typecast directly for application (e.g. to a PNP_DEVICE_CLIENT_HANDLE).  Also stores
// the iothubBunding list for given handle type.
PNP_CLIENT_CORE_HANDLE PnP_ClientCore_Create(PNP_IOTHUB_BINDING* iotHubBinding)
{
    int result = __FAILURE__;
    PNP_CLIENT_CORE* pnpClientCore;
    LOCK_HANDLE lockHandle = NULL;

    if (iotHubBinding == NULL)
    {
        pnpClientCore = NULL;
        LogError("iotHubBinding is NULL");
    }
    else if ((pnpClientCore = (PNP_CLIENT_CORE*)calloc(1, sizeof(PNP_CLIENT_CORE))) == NULL)
    {
        LogError("Failed allocating client core handle");
    }
    else if ((lockHandle =  iotHubBinding->pnpBindingLockInit()) == NULL)
    {
        LogError("Failed initializing lock");
    }
    else if ((pnpClientCore->pnpInterfaceListHandle = Pnp_InterfaceList_Create()) == NULL)
    {
        LogError("Failed allocating pnpInterfaceListHandle");
    }
    else if ((pnpClientCore->reportedPropertyList = singlylinkedlist_create()) == NULL)
    {
        LogError("Failed allocating reportedPropertyList");
    }
    else
    {
        pnpClientCore->clientState = PNP_CLIENT_STATE_RUNNING;
        pnpClientCore->registrationStatus = PNP_CLIENT_REGISTRATION_STATUS_IDLE;
        pnpClientCore->refCount = 1;
        memcpy(&pnpClientCore->iothubBinding, iotHubBinding, sizeof(pnpClientCore->iothubBinding));
        pnpClientCore->iothubBinding.pnpBindingLockHandle = lockHandle;
        lockHandle = NULL;
        result = 0;
    }

    if ((result != 0) && (pnpClientCore != NULL))
    {
        FreePnPDeviceClientCore(pnpClientCore);
        pnpClientCore = NULL;
    }

    if (lockHandle != NULL)
    {    
        iotHubBinding->pnpBindingLockDeinit(lockHandle);
    }

    return (PNP_CLIENT_CORE_HANDLE)pnpClientCore;
}

// After PnP interfaces registration initiated by PnP_ClientCore_RegisterInterfacesAsync has completed,
// either successfully or on failure, InvokeUserRegisterInterfaceCallback will invoke the application's callback function.
static void InvokeUserRegisterInterfaceCallback(PNP_CLIENT_CORE* pnpClientCore, PNP_REPORTED_INTERFACES_STATUS pnpInterfaceStatus)
{
    if (pnpInterfaceStatus == PNP_REPORTED_INTERFACES_OK)
    {
        // On success, we're ready to process additional PnP operations.
        pnpClientCore->registrationStatus = PNP_CLIENT_REGISTRATION_STATUS_REGISTERED; 
    }
    else
    {
        // Failure is not permanent - the caller can attempt to re-register interfaces.
        pnpClientCore->registrationStatus = PNP_CLIENT_REGISTRATION_STATUS_IDLE;
    }

    if (pnpClientCore->registerInterfacesCallbackContext.pnpInterfaceRegisteredCallback != NULL)
    {
        pnpClientCore->registerInterfacesCallbackContext.pnpInterfaceRegisteredCallback(pnpInterfaceStatus, pnpClientCore->registerInterfacesCallbackContext.userContextCallback);
    }
}

// Invokes appropriate IoTHub*_SendReportedState function for given handle type.
static int InvokeBindingSendReportedStateAsync(PNP_CLIENT_CORE* pnpClientCore, const unsigned char* reportedState, size_t reportedStateLen, IOTHUB_CLIENT_REPORTED_STATE_CALLBACK reportedStateCallback, void* userContext)
{
    return pnpClientCore->iothubBinding.pnpSendReportedState(pnpClientCore->iothubBinding.iothubClientHandle, reportedState, reportedStateLen, reportedStateCallback, userContext);
}

// CreateReportedPropertyCallbackContext allocates and fills out a context structure that is used to correlate
// reported property callbacks into the PnP_Client_Core and ultimately API caller layer.
static PNP_REPORTED_PROPERTY_CALLBACK_CONTEXT* CreateReportedPropertyCallbackContext(PNP_CLIENT_CORE* pnpClientCore, PNP_INTERFACE_CLIENT_CORE_HANDLE pnpInterfaceClientHandle, PNP_REPORTED_PROPERTY_CALLBACK_INVOKER callbackInvoker, void* userContextCallback)
{
    PNP_REPORTED_PROPERTY_CALLBACK_CONTEXT* result;
    
    if ((result = calloc(1, sizeof(PNP_REPORTED_PROPERTY_CALLBACK_CONTEXT))) == NULL)
    {
        LogError("Unable to allocate PNP_REPORTED_PROPERTY_CALLBACK_CONTEXT strecture");
    }
    else if (singlylinkedlist_add(pnpClientCore->reportedPropertyList, result) == 0)
    {
        LogError("Unable to add to list");
        free(result);
        result = NULL;
    }
    else
    {
        result->pnpClientCore = pnpClientCore;
        result->pnpInterfaceClientHandle = pnpInterfaceClientHandle;
        result->callbackInvoker = callbackInvoker;
        result->userContextCallback = userContextCallback;
    }

    return result;
}

// Returns true when the match_context is equal to the current pointer traversing through the list.
static bool RemoveReportedPropertyCallbackContext_Visitor_IfMatch(const void* item, const void* match_context, bool* continue_processing)
{
    bool result;

    if (item == match_context)
    {
        *continue_processing = false;
        result = true;
    }
    else
    {
        *continue_processing = true;
        result = false;
    }

    return result;
}

// Frees a PNP_REPORTED_PROPERTY_CALLBACK_CONTEXT structure and removes it from the linked list.
static void FreeReportedPropertyCallbackContext(PNP_CLIENT_CORE* pnpClientCore, PNP_REPORTED_PROPERTY_CALLBACK_CONTEXT* pnpReportedPropertyCallbackContext)
{
    singlylinkedlist_remove_if(pnpClientCore->reportedPropertyList, RemoveReportedPropertyCallbackContext_Visitor_IfMatch, pnpReportedPropertyCallbackContext);
    free(pnpReportedPropertyCallbackContext);
}

// Reported states are used for both interface updates completions as well as properties of these interfaces.
// ReportedPnPStateUpdate_Callback is invoked for either case (because underlying IoTHub_* API's only allow one callback for *all* twin
// reported callback operations).  This determines which state we're in, invokes appropriate callbacks, and frees the context we've allocated.
static void ReportedPnPStateUpdate_Callback(int status_code, void* userContextCallback)
{
    PNP_REPORTED_PROPERTY_CALLBACK_CONTEXT* pnpReportedPropertyCallbackContext = (PNP_REPORTED_PROPERTY_CALLBACK_CONTEXT*)userContextCallback;
    PNP_CLIENT_CORE* pnpClientCore = pnpReportedPropertyCallbackContext->pnpClientCore;

    if (BeginClientCoreCallbackProcessing(pnpClientCore) != 0)
    {
        LogError("Skipping callback processing");
    }
    else
    {
        if (pnpReportedPropertyCallbackContext->callbackInvoker == PNP_REPORTED_PROPERTY_CALLBACK_INVOKER_REGISTER_INTERFACE)
        {
            PNP_REPORTED_INTERFACES_STATUS pnpInterfaceStatus = (status_code < 300) ? PNP_REPORTED_INTERFACES_OK : PNP_REPORTED_INTERFACES_ERROR;
            InvokeUserRegisterInterfaceCallback(pnpClientCore, pnpInterfaceStatus);

            if (pnpInterfaceStatus == PNP_REPORTED_INTERFACES_OK)
            {
                // Only after we've registered our interfaces should we start listening for incoming commands
                InvokeBindingSetDeviceMethodCallbackIfNeeded(pnpClientCore, PnPDeviceMethod_Callback);
            }
        }
        else if (pnpReportedPropertyCallbackContext->callbackInvoker == PNP_REPORTED_PROPERTY_CALLBACK_INVOKER_UPDATE_PROPERTIES)
        {
            PNP_REPORTED_PROPERTY_STATUS pnpReportedStatus = (status_code < 300) ? PNP_REPORTED_PROPERTY_OK : PNP_REPORTED_PROPERTY_ERROR;
            (void)PnP_InterfaceList_ProcessReportedPropertiesUpdateCallback(pnpClientCore->pnpInterfaceListHandle, pnpReportedPropertyCallbackContext->pnpInterfaceClientHandle, pnpReportedStatus, pnpReportedPropertyCallbackContext->userContextCallback);
        }
        else
        {
            LogError("Unknown state %d.  Callback will be ignored", pnpReportedPropertyCallbackContext->callbackInvoker);
        }
        EndClientCoreCallbackProcessing(pnpClientCore);
    }

    FreeReportedPropertyCallbackContext(pnpClientCore, pnpReportedPropertyCallbackContext);
}

// Retrieve Json to send from interface registrar and update the device's reported state for which interfaces it has registered.
static int SendPnPInterfaces(PNP_CLIENT_CORE* pnpClientCore)
{
    char* jsonToSend = NULL;
    size_t jsonToSendLen;
    int result;
    PNP_CLIENT_RESULT pnpInterfaceListResult;
    PNP_REPORTED_PROPERTY_CALLBACK_CONTEXT* reportedPropertyCallbackContext;

    if ((pnpInterfaceListResult = PnP_InterfaceList_GetInterface_Data(pnpClientCore->pnpInterfaceListHandle, &jsonToSend, &jsonToSendLen)) != PNP_CLIENT_OK)
    {
        LogError("PnP_InterfaceList_GetInterface_Data failed, error = %d", pnpInterfaceListResult);
        result = __FAILURE__;
    }
    else if ((reportedPropertyCallbackContext = CreateReportedPropertyCallbackContext(pnpClientCore, NULL, PNP_REPORTED_PROPERTY_CALLBACK_INVOKER_REGISTER_INTERFACE, NULL)) == NULL)
    {
        LogError("CreateReportedPropertyCallbackContext failed");
        result = __FAILURE__;
    }
    else if (InvokeBindingSendReportedStateAsync(pnpClientCore, (const unsigned char*)jsonToSend, jsonToSendLen, ReportedPnPStateUpdate_Callback, reportedPropertyCallbackContext) != 0)
    {
        // Free reportedPropertyCallbackContext here; after we register callback, the callback will be responsible for freeing this memory.
        FreeReportedPropertyCallbackContext(pnpClientCore, reportedPropertyCallbackContext);
        LogError("InvokeBindingSendReportedState failed");
        result = __FAILURE__;
    }
    else
    {
        result = 0;
    }
    
    free(jsonToSend);
    return result;
}

// DeviceTwinPnP_Callback is invoked when the device twin is updated.  DeviceTwins come (for private preview) for interface updates
// and also for property updates per interface.
static void DeviceTwinPnP_Callback(DEVICE_TWIN_UPDATE_STATE update_state, const unsigned char* payLoad, size_t size, void* userContextCallback)
{
    PNP_CLIENT_CORE* pnpClientCore = (PNP_CLIENT_CORE*)userContextCallback;

    if (BeginClientCoreCallbackProcessing(pnpClientCore) != 0)
    {
        LogError("Skipping callback processing");
    }
    else
    {
        int result = 0;
        bool fullTwin = (update_state == DEVICE_TWIN_UPDATE_COMPLETE);
        PNP_CLIENT_RESULT pnpClientResult;

        if ((pnpClientResult = PnP_InterfaceList_ProcessTwinCallbackForRegistration(pnpClientCore->pnpInterfaceListHandle, fullTwin, payLoad, size)) != PNP_CLIENT_OK)
        {
            LogError("PnP_InterfaceList_ProcessTwinCallbackForRegistration failed, error = %d", pnpClientResult);
            result = __FAILURE__;
        }
        else if (pnpClientCore->registrationStatus == PNP_CLIENT_REGISTRATION_STATUS_REGISTERING)
        {
            // If we're in the middle of a registration process, we continue the next step after receiving twin data (so we know if we need to delete any interfaces in Cloud).
            if (SendPnPInterfaces(pnpClientCore) != 0)
            {
                LogError("sendPnpInterfaces failed");
                result = __FAILURE__;
            }
        }

        if (result != 0)
        {
            if (pnpClientCore->registrationStatus == PNP_CLIENT_REGISTRATION_STATUS_REGISTERING)
            {
                // Because we couldn't send PnP interfaces via the SendReportedState, the app's callback will not be invoked to indicate this error state via IoTHub* callbacks.
                // Invoke callback here to make sure callback invoked and internal state is updated.
                InvokeUserRegisterInterfaceCallback(pnpClientCore, PNP_REPORTED_INTERFACES_ERROR);
            }
        }
        else
        {
            PnP_InterfaceList_ProcessTwinCallbackForProperties(pnpClientCore->pnpInterfaceListHandle, fullTwin, payLoad, size);
        }

        EndClientCoreCallbackProcessing(pnpClientCore);
    }
}

// Helper that allocates PNP_SEND_TELEMETRY_CALLBACK_CONTEXT.
static PNP_SEND_TELEMETRY_CALLBACK_CONTEXT* CreatePnPSendTelemetryCallbackContext(PNP_CLIENT_CORE* pnpClientCore, PNP_INTERFACE_CLIENT_CORE_HANDLE pnpInterfaceClientHandle, void* userContextCallback)
{
    PNP_SEND_TELEMETRY_CALLBACK_CONTEXT* pnpSendTelemetryCallbackContext;

    if ((pnpSendTelemetryCallbackContext = (PNP_SEND_TELEMETRY_CALLBACK_CONTEXT*)calloc(1, sizeof(PNP_SEND_TELEMETRY_CALLBACK_CONTEXT))) == NULL)
    {
        LogError("Cannot allocate PNP_SEND_TELEMETRY_CALLBACK_CONTEXT");
    }
    else
    {
        pnpSendTelemetryCallbackContext->pnpClientCore = pnpClientCore;
        pnpSendTelemetryCallbackContext->pnpInterfaceClientHandle = pnpInterfaceClientHandle;
        pnpSendTelemetryCallbackContext->userContextCallback = userContextCallback;
    }

    return pnpSendTelemetryCallbackContext;
}

// PnP_ClientCore_RegisterInterfacesAsync updates the list of interfaces we're supporting and begins 
// protocol update of server.
PNP_CLIENT_RESULT PnP_ClientCore_RegisterInterfacesAsync(PNP_CLIENT_CORE_HANDLE pnpClientCoreHandle, PNP_INTERFACE_CLIENT_CORE_HANDLE* pnpInterfaces, unsigned int numPnpInterfaces, PNP_INTERFACE_REGISTERED_CALLBACK pnpInterfaceRegisteredCallback, void* userContextCallback)
{
    PNP_CLIENT_RESULT result;
    PNP_CLIENT_CORE* pnpClientCore = (PNP_CLIENT_CORE*)pnpClientCoreHandle;
    bool lockHeld = false;

    if (pnpClientCoreHandle == NULL)
    {
        LogError("Invalid parameter(s). pnpClientCoreHandle=%p", pnpClientCoreHandle);
        result = PNP_CLIENT_ERROR_INVALID_ARG;
    }
    else if (InvokeBindingLock(pnpClientCore, &lockHeld) != 0)
    {
        LogError("Lock failed");
        result = PNP_CLIENT_ERROR;
    }
    else if (pnpClientCore->registrationStatus == PNP_CLIENT_REGISTRATION_STATUS_REGISTERING)
    {
        LogError("Cannot register because status is %d but must be idle", pnpClientCore->registrationStatus);
        result = PNP_CLIENT_ERROR_REGISTRATION_PENDING;
    }
    else
    {
        BlockOnActiveCallbacks(pnpClientCore, &lockHeld);
        if ((result = PnP_InterfaceList_RegisterInterfaces(pnpClientCore->pnpInterfaceListHandle, pnpInterfaces, numPnpInterfaces)) != PNP_CLIENT_OK)
        {
            LogError("PnP_InterfaceList_RegisterInterfaces failed, result = %d", result);
        }
        else if (InvokeBindingSetDeviceTwinCallbackIfNeeded(pnpClientCore, DeviceTwinPnP_Callback) != 0)
        {
            LogError("InvokeBindingSetDeviceTwinCallback failed");
            result = PNP_CLIENT_ERROR;
        }
        else
        {
            pnpClientCore->registerInterfacesCallbackContext.pnpInterfaceRegisteredCallback = pnpInterfaceRegisteredCallback;
            pnpClientCore->registerInterfacesCallbackContext.userContextCallback = userContextCallback;
        
            // Once we're processing an interface update, no other caller initiated operations on this device client can occur.
            pnpClientCore->registrationStatus = PNP_CLIENT_REGISTRATION_STATUS_REGISTERING;
            result = PNP_CLIENT_OK;
        }
    }

    (void)InvokeBindingUnlock(pnpClientCore, &lockHeld);
    return result;
}

// SendPnPTelemetry_Callback is hooked into IoTHub Client API's callback on SendEvent completion and translates this to the PnP telemetry callback.
static void SendPnPTelemetry_Callback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* userContextCallback)
{
    PNP_SEND_TELEMETRY_CALLBACK_CONTEXT* pnpSendTelemetryCallbackContext = (PNP_SEND_TELEMETRY_CALLBACK_CONTEXT*)userContextCallback;
    PNP_CLIENT_CORE* pnpClientCore = pnpSendTelemetryCallbackContext->pnpClientCore;

    if (BeginClientCoreCallbackProcessing(pnpClientCore) != 0)
    {
        LogError("Skipping callback processing");
    }
    else
    {
        PNP_SEND_TELEMETRY_STATUS pnpSendTelemetryStatus = GetPnPSendStatusCodeFromIoTHubStatus(result);

        (void)PnP_InterfaceList_ProcessTelemetryCallback(pnpClientCore->pnpInterfaceListHandle, pnpSendTelemetryCallbackContext->pnpInterfaceClientHandle, pnpSendTelemetryStatus, pnpSendTelemetryCallbackContext->userContextCallback);
        EndClientCoreCallbackProcessing(pnpClientCore);
    }

    free(pnpSendTelemetryCallbackContext);
}

// PnP_ClientCore_SendTelemetryAsync sends the specified telemetry to Azure IoTHub in proper PnP data format.
PNP_CLIENT_RESULT PnP_ClientCore_SendTelemetryAsync(PNP_CLIENT_CORE_HANDLE pnpClientCoreHandle, PNP_INTERFACE_CLIENT_CORE_HANDLE pnpInterfaceClientHandle, IOTHUB_MESSAGE_HANDLE telemetryMessageHandle, void* userContextCallback)
{
    PNP_CLIENT_CORE* pnpClientCore = (PNP_CLIENT_CORE*)pnpClientCoreHandle;
    PNP_CLIENT_RESULT result;
    PNP_SEND_TELEMETRY_CALLBACK_CONTEXT* pnpSendTelemetryCallbackContext = NULL;
    bool lockHeld = false;

    if ((pnpClientCoreHandle == NULL) || (pnpInterfaceClientHandle == NULL) || (telemetryMessageHandle == NULL))
    {
        LogError("Invalid parameter, one or more paramaters is invalid. pnpClientCoreHandle=%p, pnpInterfaceClientHandle=%p, telemetryMessageHandle=%p", pnpClientCoreHandle, pnpInterfaceClientHandle, telemetryMessageHandle);
        result = PNP_CLIENT_ERROR_INVALID_ARG;
    }
    else if (InvokeBindingLock(pnpClientCore, &lockHeld) != 0)
    {
        LogError("Lock failed");
        result = PNP_CLIENT_ERROR;
    }
    else if (pnpClientCore->clientState == PNP_CLIENT_STATE_SHUTTING_DOWN)
    {
        LogError("Client is shutting down");
        result = PNP_CLIENT_ERROR_SHUTTING_DOWN;
    }
    else if ((pnpSendTelemetryCallbackContext = CreatePnPSendTelemetryCallbackContext(pnpClientCore, pnpInterfaceClientHandle, userContextCallback)) == NULL)
    {
        LogError("Cannot allocate PNP_SEND_TELEMETRY_CALLBACK_CONTEXT");
        result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
    }
    else if (InvokeBindingSendEventAsync(pnpClientCore, telemetryMessageHandle, SendPnPTelemetry_Callback, pnpSendTelemetryCallbackContext) != 0)
    {
        LogError("SendPnPTelemetry failed");
        result = PNP_CLIENT_ERROR;
    }
    // No events that can fail should come after we've sent the event, or else our state will be considerably more complicated (e.g. this function could
    // return an error but there would still be a send callback pending for it that would arrive later).
    else
    {
        result = PNP_CLIENT_OK;
    }

    if (result != PNP_CLIENT_OK)
    {
        free(pnpSendTelemetryCallbackContext);
    }

    (void)InvokeBindingUnlock(pnpClientCore, &lockHeld);
    return result;
}

// PnP_ClientCore_ReportPropertyStatusAsync is an API apps ultimately invoke through to send data.  The data is already serilaized by caller,
// so this layer just interacts with IoTHub* layer.
PNP_CLIENT_RESULT PnP_ClientCore_ReportPropertyStatusAsync(PNP_CLIENT_CORE_HANDLE pnpClientCoreHandle, PNP_INTERFACE_CLIENT_CORE_HANDLE pnpInterfaceClientHandle, const unsigned char* dataToSend, size_t dataToSendLen, void* userContextCallback)
{
    PNP_CLIENT_CORE* pnpClientCore = (PNP_CLIENT_CORE*)pnpClientCoreHandle;
    PNP_CLIENT_RESULT result;
    bool lockHeld = false;
    PNP_REPORTED_PROPERTY_CALLBACK_CONTEXT* reportedPropertyCallbackContext;

    if ((pnpClientCoreHandle == NULL) || (pnpInterfaceClientHandle == NULL) || (dataToSend == NULL) || (dataToSendLen == 0))
    {
        LogError("Invalid parameter, one or more paramaters is NULL. pnpClientCoreHandle=%p, pnpInterfaceClientHandle=%p, dataToSend=%p, dataToSendLen=%lu", pnpClientCoreHandle, pnpInterfaceClientHandle, dataToSend, (unsigned long)dataToSendLen);
        result = PNP_CLIENT_ERROR_INVALID_ARG;
    }
    else if (InvokeBindingLock(pnpClientCore, &lockHeld) != 0)
    {
        result = PNP_CLIENT_ERROR;
    }
    else if (pnpClientCore->clientState == PNP_CLIENT_STATE_SHUTTING_DOWN)
    {
        LogError("Client is shutting down");
        result = PNP_CLIENT_ERROR_SHUTTING_DOWN;
    }
    else if ((reportedPropertyCallbackContext = CreateReportedPropertyCallbackContext(pnpClientCore, pnpInterfaceClientHandle, PNP_REPORTED_PROPERTY_CALLBACK_INVOKER_UPDATE_PROPERTIES, userContextCallback)) == NULL)
    {
        LogError("Cannot allocate callback context");
        result = PNP_CLIENT_ERROR_OUT_OF_MEMORY;
    }
    else if (InvokeBindingSendReportedStateAsync(pnpClientCore, dataToSend, dataToSendLen, ReportedPnPStateUpdate_Callback, reportedPropertyCallbackContext) != 0)
    {
        // Free reportedPropertyCallbackContext here; after we register callback, the callback will be responsible for freeing this memory.
        FreeReportedPropertyCallbackContext(pnpClientCore, reportedPropertyCallbackContext);
        LogError("Invoking binding for SendReportedStatu");
        result = PNP_CLIENT_ERROR;
    }
    else
    {
        result = PNP_CLIENT_OK;
    }

    (void)InvokeBindingUnlock(pnpClientCore, &lockHeld);
    return result;
}

// For _LL_ clients specifically, invokes back to the binding handle that implements IoTHub*_DoWork.
void PnP_ClientCore_DoWork(PNP_CLIENT_CORE_HANDLE pnpClientCoreHandle)
{
    if (pnpClientCoreHandle == NULL)
    {
        LogError("Invalid parameter: pnpClientCoreHandle=NULL");
    }
    else
    {
        InvokeBindingDoWork((PNP_CLIENT_CORE*)pnpClientCoreHandle);
    }
}

