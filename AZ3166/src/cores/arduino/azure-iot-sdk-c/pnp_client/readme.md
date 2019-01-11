# Microsoft Azure IoT PnP Client SDK for C

This folder contains the headers, source, samples, and internal testing for the PnP Client for the C SDK.

These instructions and samples assume basic familiarity with PnP concepts.  To learn more background information, see PLACEHOLDER.

<a name="pnpInitialDevSetup"></a>
## Initial development environment setup
To get setup to build applications that enable PnP for C:
* Clone the private GitHub repo containing the PnP private preview SDK.  Note that this is a private repo; if you need access contact PLACEHOLDER@microsoft.com.
```
 git clone --recursive https://github.com/Azure/azure-iot-sdk-c-pnp
 git checkout private-preview
```

* Follow the setup instructions for [compiling the IoTHub client](../iothub_client/readme.md#compile) for setting up your build environment for your platform.
  * There is no plan to distribute the PnP C SDK via packaging technologies such as Nuget or apt-get through PnP preview.

* The C SDK **does not** contain a PnP Service SDK for interacting with devices, nor does it contain an SDK for interacting with PnP repos (which are distinct from GitHub repos).  To fully simulate end-to-end PnP interactions between devices, service controller, and repo, you will need to use.  
  * The C# Service SDK for PnP, which is available from https://github.com/Azure/azure-iot-sdk-csharp-pnp
  * The command line plugin for the az tool, available from PLACEHOLDER.

## Getting started sample
The [samples directory](./samples) contains PnP samples demonstrating creation of interfaces and basic operation.  **It may be easiest to just jump in here for a guided tour of PnP.**

## If something is broken through preview
If you hit an issue with the C SDK PnP, please contact PLACEHOLDER or else open a GitHub issue in https://github.com/Azure/azure-iot-sdk-c-pnp.


## PnP C SDK core concepts
<a name="publicHeaders"></a>
### Public header file layout
The headers for including PnP are available in the [inc](./inc) folder.
  * Through preview, these headers are subject to change with little notice.  **This also means if you're unhappy with the API, please let us know since we have the ability to fix it much more easily than after we release!**  
  * Once PnP is officially released, the API will be locked down following standard semantic versioning.  PnP will be considered released for the C SDK when it is merged into the master branch of the official C SDK repo, https://github.com/Azure/azure-iot-sdk-c.
  * Files under the [./inc/internal](./inc/internal) are for internal use of the SDK only.  Even after PnP for C is officially released, these can and will change.  Do not reference internal headers.

### PnP C SDK and the \_LL\_ (lower layer) API

Analogous to the IoTHub API's, there are two distinct but closely related sets of API's for PnP.
Applications that want to run on a single thread should use the \_LL\_ (aka lower level) API.  Devices with very limited resources frequently prefer using the \_LL\_ layer; e.g. the hardware/OS can only support one thread at a time or spinning additional threads is expensive.  Applications on more capable hardware may also use the \_LL\_ layer for greater control over scheduling.

Calls to the PnP API's in \_LL\_ do not cause the PnP operation to execute immediately.  Instead, the work - both for sending data across the network and receiving data - is queued.  The application must manually schedule the work by calling `PnP_DeviceClient_LL_DoWork`.

Rough code demonstrating this pattern:

```c
#include <pnp_device_client_ll.h>
#include <pnp_interface_client_ll.h>
#include <iothub_device_client_ll.h>

IOTHUB_DEVICE_CLIENT_LL_HANDLE devLLHandle = IoTHubDeviceClient_LL_CreateFromConnectionString(...);
PNP_DEVICE_CLIENT_LL_HANDLE pnpDeviceLLHandle = PnP_DeviceClient_LL_CreateFromDeviceHandle(devLLHandle);
PNP_INTERFACE_CLIENT_LL_HANDLE pnpInterfaceLLHandle = PnP_InterfaceClient_LL_Create(pnpDeviceLLHandle,...);

// Queue a telemetry item to be sent.  NOTE: this does not send data across the network at this stage.
PnP_InterfaceClient_LL_SendEventAsync(...);
   
// Run through pending work

while (isActive)
{
    // Send any pending data, and call network recv() and invoke application callbacks if necessary
    PnP_DeviceClient_LL_DoWork(pnpDeviceLLHandle);
    Sleep(...)
}

```

**Even if you are not sending data while using \_LL\_ layer, you still need to periodically invoke PnP_DeviceClient_LL_DoWork to make the client accept data from the network.**  There's no other thread listening for it, after all!  When data arrives from the network and the PnP SDK invokes a callback you have registered to process it (e.g. handling a command), the callback will happen on this same thread.

### PnP C SDK and the convenience layer
Applications on devices where spinning a thread is not burdensome frequently prefer to use PnP's convenience layer.  In this mode, PnP will spin a worker thread to handle any queued operations[*].  So when a caller invokes `PnP_InterfaceClient_SendEventAsync`, the work is queued on the calling thread and the background worker thread immediately takes the message off the queue and automatically sends it.  

The PnP worker thread also listens for incoming data from the network and will invoke application's callbacks.  These callbacks run in the context of PnP's worker thread.  Callbacks that are very long running should themselves spin worker threads, because PnP's other send and receive operations are blocked while a user callback is operating.

The convenience layer looks very much like the lower layer from a programming perspective.  Most of the function signatures are effectively identical, as is expected behavior.  The differences are:
* The handles do not have the \_LL\_ infix.
* The header files included do not have the \_LL\_ infix.
* The caller does not call `PnP_DeviceClient_LL_DoWork`, because the worker thread is handling this function.

Rough code demonstrating this pattern:

```c
#include <pnp_device_client.h>
#include <pnp_interface_client.h>
#include <iothub_device_client.h>

IOTHUB_DEVICE_CLIENT_HANDLE devHandle = IoTHubDeviceClient_CreateFromConnectionString(...);
PNP_DEVICE_CLIENT_HANDLE pnpDeviceHandle = PnP_DeviceClient_CreateFromDeviceHandle(devHandle);
PNP_INTERFACE_CLIENT_HANDLE pnpInterfaceHandle = PnP_InterfaceClient_Create(pnpDeviceHandle,...);

// Queue a telemetry item to be sent.  This will be sent across network with no further user action.
PnP_InterfaceClient_LL_SendEventAsync(...);
   
// Caller does not do DoWork since worker thread handles it.
```

[*]The thread is technically spun by the IoTHub layer, not PnP. But from user perspective it's PnP doing the work.

## PnP C SDK Directory Layout
* The public API for PnP for C is available in the [inc](./inc) folder.  
* Samples live in the [samples](./samples) folder.
* Tests for PnP are in the [test](./test) folder.  
* The source code for PnP is in the [src](./src) folder.

## Contributing?
PLACEHOLDER - let's discuss how much we want to open up, especially for December preview.

If you would like to contribute to PnP through preview, the same contribution guidelines for the public C SDK apply.  Details are available [here](../.github/CONTRIBUTING.md).  Please run the tests in the [pnp test](./test) folder and add additional tests as necessary prior to submitting a pull request.


