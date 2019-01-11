// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "internal/lock_thread_binding_stub.h"

LOCK_HANDLE LockBinding_LockInit_Stub()
{
	return Lock_Init();
}

LOCK_RESULT LockBinding_Lock_Stub(LOCK_HANDLE bindingLock)
{
    (void)bindingLock;
    return LOCK_OK;
}

LOCK_RESULT LockBinding_Unlock_Stub(LOCK_HANDLE bindingLock)
{
    (void)bindingLock;
    return LOCK_OK;
}

LOCK_RESULT LockBinding_LockDeinit_Stub(LOCK_HANDLE bindingLock)
{
    (void)bindingLock;
    return LOCK_OK;
}

void ThreadBinding_ThreadSleep_Stub(unsigned int milliseconds)
{
    (void)milliseconds;
}

