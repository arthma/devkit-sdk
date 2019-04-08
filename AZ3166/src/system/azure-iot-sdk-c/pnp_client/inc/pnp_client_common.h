// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef PNP_CLIENT_COMMON_H 
#define PNP_CLIENT_COMMON_H 

#include "azure_c_shared_utility/macro_utils.h"
#include "azure_c_shared_utility/umock_c_prod.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef void* PNP_INTERFACE_CLIENT_LL_HANDLE;
typedef void* PNP_INTERFACE_CLIENT_HANDLE;
typedef void* PNP_INTERFACE_CLIENT_CORE_HANDLE;

#define PNP_CLIENT_RESULT_VALUES                        \
        PNP_CLIENT_OK,                                  \
        PNP_CLIENT_ERROR_INVALID_ARG,                   \
        PNP_CLIENT_ERROR_OUT_OF_MEMORY,                 \
        PNP_CLIENT_ERROR_REGISTRATION_PENDING,          \
        PNP_CLIENT_ERROR_INTERFACE_ALREADY_REGISTERED,  \
        PNP_CLIENT_ERROR_INTERFACE_NOT_REGISTERED,      \
        PNP_CLIENT_ERROR_COMMAND_NOT_PRESENT,           \
        PNP_CLIENT_ERROR_SHUTTING_DOWN,                 \
        PNP_CLIENT_ERROR                                \
    
DEFINE_ENUM(PNP_CLIENT_RESULT, PNP_CLIENT_RESULT_VALUES);

#define PNP_REPORTED_INTERFACES_STATUS_VALUES            \
        PNP_REPORTED_INTERFACES_OK,                      \
        PNP_REPORTED_INTERFACES_ERROR_HANDLE_DESTROYED,  \
        PNP_REPORTED_INTERFACES_ERROR_OUT_OF_MEMORY,     \
        PNP_REPORTED_INTERFACES_ERROR_TIMEOUT,           \
        PNP_REPORTED_INTERFACES_ERROR                    \

DEFINE_ENUM(PNP_REPORTED_INTERFACES_STATUS, PNP_REPORTED_INTERFACES_STATUS_VALUES);

#define PNP_CLIENT_COMMAND_REQUEST_VERSION_1   1

typedef struct PNP_CLIENT_COMMAND_REQUEST_TAG
{
    int version;  // version of this structure for SDK.  *NOT* tied to service versioning.  Currently PNP_CLIENT_COMMAND_REQUEST_VERSION_1.
    const unsigned char* requestData;
    size_t requestDataLen;
}
PNP_CLIENT_COMMAND_REQUEST;

#define PNP_CLIENT_ASYNC_COMMAND_REQUEST_VERSION_1 1

typedef struct PNP_CLIENT_ASYNC_COMMAND_REQUEST_TAG
{
    int version;  // version of this structure for SDK.  *NOT* tied to service versioning.  Currently PNP_CLIENT_ASYNC_COMMAND_REQUEST_VERSION_1.
    const unsigned char* requestData;
    size_t requestDataLen;
}
PNP_CLIENT_ASYNC_COMMAND_REQUEST;


#define PNP_CLIENT_COMMAND_RESPONSE_VERSION_1  1

typedef struct PNP_CLIENT_COMMAND_RESPONSE_TAG
{
    int version; // version of this structure for SDK.  *NOT* tied to service versioning.  Currently PNP_CLIENT_COMMAND_RESPONSE_VERSION_1.
    int status;
    unsigned char* responseData;
    size_t responseDataLen;
}
PNP_CLIENT_COMMAND_RESPONSE;

#define PNP_ASYNC_STATUS_CODE_PENDING 202

#define PNP_CLIENT_ASYNC_COMMAND_RESPONSE_VERSION_1  1

typedef struct PNP_CLIENT_ASYNC_COMMAND_RESPONSE_TAG
{
    int version; // version of this structure for SDK.  *NOT* tied to service versioning.  Currently PNP_CLIENT_ASYNC_COMMAND_RESPONSE_VERSION_1.
    int status;
    unsigned char* responseData;
    size_t responseDataLen;
    char* correlationId;
}
PNP_CLIENT_ASYNC_COMMAND_RESPONSE;


typedef void(*PNP_INTERFACE_REGISTERED_CALLBACK)(PNP_REPORTED_INTERFACES_STATUS pnpInterfaceStatus, void* userContextCallback);
typedef void(*PNP_COMMAND_EXECUTE_CALLBACK)(const PNP_CLIENT_COMMAND_REQUEST* pnpClientCommandContext, PNP_CLIENT_COMMAND_RESPONSE* pnpClientCommandResponseContext, void* userContextCallback);
typedef void(*PNP_ASYNC_COMMAND_EXECUTE_CALLBACK)(const PNP_CLIENT_ASYNC_COMMAND_REQUEST* pnpClientAsyncCommandContext, PNP_CLIENT_ASYNC_COMMAND_RESPONSE* pnpClientAsyncCommandResponseContext, void* userContextCallback);


#ifdef PNP_LOGGING_ENABLED
#define PnPLogInfo LogInfo
#else
#define PnPLogInfo(...)
#endif

#ifdef __cplusplus
}
#endif


#endif // PNP_CLIENT_COMMON_H 

