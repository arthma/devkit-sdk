// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef LOCK_THREAD_BINDING_STUB_H
#define LOCK_THREAD_BINDING_STUB_H

#include "azure_c_shared_utility/macro_utils.h"
#include "azure_c_shared_utility/umock_c_prod.h"

#include "azure_c_shared_utility/lock.h"
#include "azure_c_shared_utility/threadapi.h"

#ifdef __cplusplus
extern "C"
{
#endif

MOCKABLE_FUNCTION(, LOCK_HANDLE, LockBinding_LockInit_Stub);
MOCKABLE_FUNCTION(, LOCK_RESULT, LockBinding_Lock_Stub, LOCK_HANDLE, bindingLock);
MOCKABLE_FUNCTION(, LOCK_RESULT, LockBinding_Unlock_Stub, LOCK_HANDLE, bindingLock);
MOCKABLE_FUNCTION(, LOCK_RESULT, LockBinding_LockDeinit_Stub, LOCK_HANDLE, bindingLock);
MOCKABLE_FUNCTION(, void, ThreadBinding_ThreadSleep_Stub, unsigned int, milliseconds);

#ifdef __cplusplus
}
#endif


#endif // LOCK_THREAD_BINDING_STUB_H

