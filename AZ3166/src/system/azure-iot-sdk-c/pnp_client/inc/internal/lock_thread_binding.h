// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef LOCK_THREAD_BINDING__H
#define LOCK_THREAD_BINDING__H

#include "azure_c_shared_utility/macro_utils.h"
#include "azure_c_shared_utility/umock_c_prod.h"

#include "azure_c_shared_utility/lock.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef LOCK_HANDLE(*PNP_BINDING_LOCK_INIT)(void);
typedef LOCK_RESULT(*PNP_BINDING_LOCK)(LOCK_HANDLE bindingLock);
typedef LOCK_RESULT(*PNP_BINDING_UNLOCK)(LOCK_HANDLE bindingLock);
typedef LOCK_RESULT(*PNP_BINDING_LOCK_DEINIT)(LOCK_HANDLE bindingLock);
typedef void(*PNP_BINDING_THREAD_SLEEP)(unsigned int milliseconds);

typedef struct PNP_LOCK_THREAD_BINDING_TAG
{
    // Lock and callbacks for interacting with the binding lock logic.  For _LL_ layer, these are no-ops.        
    LOCK_HANDLE pnpBindingLockHandle;
    PNP_BINDING_LOCK_INIT pnpBindingLockInit;
    PNP_BINDING_LOCK pnpBindingLock;
    PNP_BINDING_UNLOCK pnpBindingUnlock;
    PNP_BINDING_LOCK_DEINIT pnpBindingLockDeinit;
    PNP_BINDING_THREAD_SLEEP pnpBindingThreadSleep;
} PNP_LOCK_THREAD_BINDING;

#ifdef __cplusplus
}
#endif

#endif // LOCK_THREAD_BINDING__H

