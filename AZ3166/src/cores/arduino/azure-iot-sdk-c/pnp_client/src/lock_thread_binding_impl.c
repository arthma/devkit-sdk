// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "internal/lock_thread_binding_impl.h"

LOCK_HANDLE LockBinding_LockInit_Impl()
{
    return Lock_Init();
}

LOCK_RESULT LockBinding_Lock_Impl(LOCK_HANDLE bindingLock)
{
    return Lock(bindingLock);
}

LOCK_RESULT LockBinding_Unlock_Impl(LOCK_HANDLE bindingLock)
{
    return Unlock(bindingLock);
}

LOCK_RESULT LockBinding_LockDeinit_Impl(LOCK_HANDLE bindingLock)
{
    return Lock_Deinit(bindingLock);
}

void ThreadBinding_ThreadSleep_Impl(unsigned int milliseconds)
{
    ThreadAPI_Sleep(milliseconds);
}

