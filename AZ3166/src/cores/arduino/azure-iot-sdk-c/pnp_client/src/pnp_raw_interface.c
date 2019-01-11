// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdio.h>
#include <stdlib.h>

#include "azure_c_shared_utility/xlogging.h"
#include "azure_c_shared_utility/gballoc.h"

#include "pnp_raw_interface.h"

static const char http_prefix[] = "http://";
static const int http_prefix_len = sizeof(http_prefix) - 1;
static const char https_prefix[] = "https://";
static const int https_prefix_len = sizeof(https_prefix) - 1;

static const char dot = '.';
static const char star = '*';
static const char slash = '/';
static const char caret = '^';

// PnP_Get_RawInterfaceId maps the app's pnpInterface to raw interface name for Network.
// Mapping is equivalent to <text.Replace("http://", "").Replace("https://", "").Replace('.', '*').Replace('/', '^');>
const char* PnP_Get_RawInterfaceId(const char* pnpInterface)
{
    char* pnpRawInterface;

    if (pnpInterface == NULL)
    {
        LogError("Interface passed is NULL");
        pnpRawInterface = NULL;        
    }
    else if ((pnpRawInterface = malloc(strlen(pnpInterface) + 1)) == NULL)
    {
        LogError("Allocation failed");
    }
    else
    {
        const char* read = pnpInterface;
        char* write = pnpRawInterface;

        while (*read != 0)
        {
            if (strncmp(read, http_prefix, http_prefix_len) == 0)
            {
                read += http_prefix_len;
                continue;
            }
            else if (strncmp(read, https_prefix, https_prefix_len) == 0)
            {
                read += https_prefix_len;
                continue;
            }
            else if (*read == dot)
            {
                *write = star;
                write++;
            }
            else if (*read == slash)
            {
                *write = caret;
                write++;
            }
            else
            {
                *write = *read;
                write++;
            }
            read++;
        }   

        *write = 0;
    } 

    return pnpRawInterface;
}

