/*
 *  TAP-Windows -- A kernel driver to provide virtual tap
 *                 device functionality on Windows.
 *
 *  This code was inspired by the CIPE-Win32 driver by Damion K. Wilson.
 *
 *  This source code is Copyright (C) 2002-2014 OpenVPN Technologies, Inc.,
 *  and is released under the GPL version 2 (see below).
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program (see the file COPYING included with this
 *  distribution); if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

//
// Include files.
//

#include "tap-windows.h"

//======================================================================
// TAP NDIS 6 Miniport Callbacks
//======================================================================

// Returns with reference count initialized to one.
PTAP_ADAPTER_CONTEXT
tapAdapterContextAllocate(
    __in NDIS_HANDLE        MiniportAdapterHandle
)
{
    PTAP_ADAPTER_CONTEXT   adapter = NULL;

    adapter = (PTAP_ADAPTER_CONTEXT )NdisAllocateMemoryWithTagPriority(
        GlobalData.NdisDriverHandle,
        sizeof(TAP_ADAPTER_CONTEXT),
        TAP_TAG_ADAPTER,
        NormalPoolPriority
        );

    if(adapter)
    {
        NdisZeroMemory(adapter,sizeof(TAP_ADAPTER_CONTEXT));

        // Add initial reference. Normally removed in AdapterHalt.
        adapter->RefCount = 1;

        adapter->MiniportAdapterHandle = MiniportAdapterHandle;

        // Safe for multiple removes.
        NdisInitializeListHead(&adapter->AdapterListLink);
    }

    return adapter;
}

VOID
tapReadPermanentAddress(
    __in PTAP_ADAPTER_CONTEXT   Adapter,
    __in NDIS_HANDLE            ConfigurationHandle,
    __out MACADDR               PeranentAddress
    )
{
    NDIS_STATUS status;
    NDIS_CONFIGURATION_PARAMETER *configParameter;
    NDIS_STRING macKey = NDIS_STRING_CONST("MAC");
    ANSI_STRING macString;
    BOOLEAN macFromRegistry;

    // Read MAC parameter from registry.
    NdisReadConfiguration(
        &status,
        &configParameter,
        ConfigurationHandle,
        &macKey,
        NdisParameterString
        );

    if (status == NDIS_STATUS_SUCCESS)
    {
        if (configParameter->ParameterType == NdisParameterString)
        {
            if (RtlUnicodeStringToAnsiString(
                    &macString,
                    &configParameter->ParameterData.StringData,
                    TRUE) == STATUS_SUCCESS
                    )
            {
                macFromRegistry = ParseMAC (Adapter->PermanentAddress, macString.Buffer);
                RtlFreeAnsiString (&macString);
            }
        }
    }

    if(!macFromRegistry)
    {
        //
        // There is no (valid) address stashed in the registry parameter.
        //
        // Make up a dummy mac address.
        //
        GenerateRandomMac(Adapter->PermanentAddress, MINIPORT_ANSI_NAME(Adapter));
    }
}

NDIS_STATUS
tapReadConfiguration(
    __in PTAP_ADAPTER_CONTEXT     Adapter
    )
{
    NDIS_STATUS                 status = NDIS_STATUS_SUCCESS;
    NDIS_CONFIGURATION_OBJECT   configObject;
    NDIS_HANDLE                 configHandle;

    DEBUGP (("[TAP] --> tapReadConfiguration\n"));

    //
    // Setup defaults in case configuration cannot be opened.
    //
    Adapter->MtuSize = ETHERNET_MTU;
    Adapter->MediaStateAlwaysConnected = FALSE;
    Adapter->MediaState = FALSE;

    //
    // Open the registry for this adapter to read advanced
    // configuration parameters stored by the INF file.
    //
    NdisZeroMemory(&configObject, sizeof(configObject));

    {C_ASSERT(sizeof(configObject) >= NDIS_SIZEOF_CONFIGURATION_OBJECT_REVISION_1);}
    configObject.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
    configObject.Header.Size = NDIS_SIZEOF_CONFIGURATION_OBJECT_REVISION_1;
    configObject.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;

    configObject.NdisHandle = Adapter->MiniportAdapterHandle;
    configObject.Flags = 0;

    status = NdisOpenConfigurationEx(
                &configObject,
                &configHandle
                );

    // Read on the opened configuration handle.
    if(status == NDIS_STATUS_SUCCESS)
    {
        NDIS_CONFIGURATION_PARAMETER *configParameter;
        NDIS_STRING mkey = NDIS_STRING_CONST("MiniportName");

        //
        // Read MiniportName from the registry.
        // ------------------------------------
        // MiniportName is required to create device and associated
        // symbolic link for the adapter device.
        //
        NdisReadConfiguration (
            &status,
            &configParameter,
            configHandle,
            &mkey,
            NdisParameterString
            );

        if (status == NDIS_STATUS_SUCCESS)
        {
            if (configParameter->ParameterType == NdisParameterString)
            {
                DEBUGP (("[TAP] NdisReadConfiguration (MiniportName=%zW)\n",
                    configParameter->ParameterData.StringData ));

                // Save MiniportName as UNICODE.
                Adapter->MiniportName.Length = Adapter->MiniportName.MaximumLength
                    = configParameter->ParameterData.StringData.Length;

                Adapter->MiniportName.Buffer = Adapter->MiniportNameBuffer;

                NdisMoveMemory(
                    Adapter->MiniportName.Buffer, 
                    configParameter->ParameterData.StringData.Buffer,
                    Adapter->MiniportName.Length
                    );

                if (RtlUnicodeStringToAnsiString (
                        &Adapter->MiniportNameAnsi,
                        &configParameter->ParameterData.StringData,
                        TRUE) != STATUS_SUCCESS
                    )
                {
                    DEBUGP (("[TAP] MiniportName ANSI name conversion failed\n"));
                    status = NDIS_STATUS_RESOURCES;
                }
            }
            else
            {
                DEBUGP (("[TAP] MiniportName has invalid type\n"));
                status = NDIS_STATUS_INVALID_DATA;
            }
        }
        else
        {
            DEBUGP (("[TAP] MiniportName failed\n"));
            status = NDIS_STATUS_INVALID_DATA;
        }

        if (status == NDIS_STATUS_SUCCESS)
        {
            NDIS_CONFIGURATION_PARAMETER *configParameter;
            NDIS_STRING mtuKey = NDIS_STRING_CONST("MTU");
            NDIS_STRING mediaStatusKey = NDIS_STRING_CONST("MediaStatus");
#if ENABLE_NONADMIN
            NDIS_STRING allowNonAdminKey = NDIS_STRING_CONST("AllowNonAdmin");
#endif

            // Read MTU from the registry.
            NdisReadConfiguration (
                &status,
                &configParameter,
                configHandle,
                &mtuKey,
                NdisParameterInteger
                );

            if (status == NDIS_STATUS_SUCCESS)
            {
                if (configParameter->ParameterType == NdisParameterInteger)
                {
                    int mtu = configParameter->ParameterData.IntegerData;

                    // Sanity check
                    if (mtu < MINIMUM_MTU)
                    {
                        mtu = MINIMUM_MTU;
                    }
                    else if (mtu > MAXIMUM_MTU)
                    {
                        mtu = MAXIMUM_MTU;
                    }

                    Adapter->MtuSize = mtu;
                }
            }

            // Read MediaStatus setting from registry.
            NdisReadConfiguration (
                &status,
                &configParameter,
                configHandle,
                &mediaStatusKey,
                NdisParameterInteger
                );

            if (status == NDIS_STATUS_SUCCESS)
            {
                if (configParameter->ParameterType == NdisParameterInteger)
                {
		          Adapter->MediaStateAlwaysConnected = TRUE;
		          Adapter->MediaState = TRUE;
                }
            }

            // Read MAC PermanentAddress setting from registry.
            tapReadPermanentAddress(
                Adapter,
                configHandle,
                Adapter->PermanentAddress
                );

            // Now seed the current MAC address with the permanent address.
            COPY_MAC(Adapter->CurrentAddress, Adapter->PermanentAddress);

            // Read optional AllowNonAdmin setting from registry.
    #if ENABLE_NONADMIN
            NdisReadConfiguration (
                &status,
                &configParameter,
                configHandle,
                &allowNonAdminKey,
                NdisParameterInteger
                );

            if (status == NDIS_STATUS_SUCCESS)
            {
                if (configParameter->ParameterType == NdisParameterInteger)
                {
                    Adapter->AllowNonAdmin = TRUE;
                }
            }
    #endif
        }

        // Close the configuration handle.
        NdisCloseConfiguration(configHandle);
    }
    else
    {
        DEBUGP (("[TAP] Couldn't open adapter registry\n"));
    }

    DEBUGP (("[TAP] <-- tapReadConfiguration; status = %8.8X\n",status));

    return status;
}

NDIS_STATUS
AdapterCreate(
    __in  NDIS_HANDLE                         MiniportAdapterHandle,
    __in  NDIS_HANDLE                         MiniportDriverContext,
    __in  PNDIS_MINIPORT_INIT_PARAMETERS      MiniportInitParameters
    )
{
    PTAP_ADAPTER_CONTEXT   adapter = NULL;
    NDIS_STATUS    status;

    UNREFERENCED_PARAMETER(MiniportDriverContext);
    UNREFERENCED_PARAMETER(MiniportInitParameters);

    DEBUGP (("[TAP] --> AdapterCreate\n"));

    //
    // Allocate adapter context structure and initialize all the
    // memory resources for sending and receiving packets.
    //
    // Returns with reference count initialized to one.
    //
    adapter = tapAdapterContextAllocate(MiniportAdapterHandle);

    if(adapter == NULL)
    {
        status = NDIS_STATUS_RESOURCES;
    }
    else
    {
        // Read adapter configuration from registry.
        status = tapReadConfiguration(adapter);
    }

    DEBUGP (("[TAP] <-- AdapterCreate; status = %8.8X\n",status));

    return status;
}

VOID
AdapterHalt(
    __in  NDIS_HANDLE             MiniportAdapterContext,
    __in  NDIS_HALT_ACTION        HaltAction
    )
/*++

Routine Description:

    Halt handler is called when NDIS receives IRP_MN_STOP_DEVICE,
    IRP_MN_SUPRISE_REMOVE or IRP_MN_REMOVE_DEVICE requests from the PNP
    manager. Here, the driver should free all the resources acquired in
    MiniportInitialize and stop access to the hardware. NDIS will not submit
    any further request once this handler is invoked.

    1) Free and unmap all I/O resources.
    2) Disable interrupt and deregister interrupt handler.
    3) Deregister shutdown handler regsitered by
        NdisMRegisterAdapterShutdownHandler .
    4) Cancel all queued up timer callbacks.
    5) Finally wait indefinitely for all the outstanding receive
        packets indicated to the protocol to return.

    MiniportHalt runs at IRQL = PASSIVE_LEVEL.


Arguments:

    MiniportAdapterContext  Pointer to the Adapter
    HaltAction  The reason for halting the adapter

Return Value:

    None.

--*/
{
    PTAP_ADAPTER_CONTEXT   adapter = (PTAP_ADAPTER_CONTEXT )MiniportAdapterContext;

    UNREFERENCED_PARAMETER(HaltAction);

    DEBUGP (("[TAP] --> AdapterHalt\n"));

    //
    // Remove Initial Reference Added in AdapterCreate.
    // ------------------------------------------------
    // This should result in freeing adapter context memory
    // and resources allocated in AdapterCreate.
    //
    tapAdapterContextDereference(adapter);
    adapter = NULL;

    DEBUGP (("[TAP] <-- AdapterHalt\n"));
}

NDIS_STATUS
AdapterPause(
    __in  NDIS_HANDLE                       MiniportAdapterContext,
    __in  PNDIS_MINIPORT_PAUSE_PARAMETERS   PauseParameters
    )
{
    PTAP_ADAPTER_CONTEXT   adapter = (PTAP_ADAPTER_CONTEXT )MiniportAdapterContext;
    NDIS_STATUS    status;

    UNREFERENCED_PARAMETER(PauseParameters);

    status = NDIS_STATUS_SUCCESS;

    return status;
}

NDIS_STATUS
AdapterRestart(
    __in  NDIS_HANDLE                             MiniportAdapterContext,
    __in  PNDIS_MINIPORT_RESTART_PARAMETERS       RestartParameters
    )
{
    PTAP_ADAPTER_CONTEXT   adapter = (PTAP_ADAPTER_CONTEXT )MiniportAdapterContext;
    NDIS_STATUS    status;

    UNREFERENCED_PARAMETER(RestartParameters);

    status = NDIS_STATUS_FAILURE;

    return status;
}

VOID
tapSendNetBufferListsComplete(
    __in PTAP_ADAPTER_CONTEXT       Adapter,
    __in PNET_BUFFER_LIST   NetBufferLists,
    __in NDIS_STATUS        SendCompletionStatus,
    __in BOOLEAN            DispatchLevel
    )
{
    PNET_BUFFER_LIST    netBufferList;
    PNET_BUFFER_LIST    nextNetBufferList = NULL;
    ULONG               sendCompleteFlags = 0;

    for (
        netBufferList = NetBufferLists;
        netBufferList!= NULL;
        netBufferList = nextNetBufferList
        )
    {
        nextNetBufferList = NET_BUFFER_LIST_NEXT_NBL(netBufferList);

        NET_BUFFER_LIST_STATUS(netBufferList) = SendCompletionStatus;

        netBufferList = nextNetBufferList;
    }

    if(DispatchLevel)
    {
        sendCompleteFlags |= NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL;
    }

    // Complete the NBLs
    NdisMSendNetBufferListsComplete(
        Adapter->MiniportAdapterHandle,
        NetBufferLists,
        sendCompleteFlags
        );
}

VOID
AdapterSendNetBufferLists(
    __in  NDIS_HANDLE             MiniportAdapterContext,
    __in  PNET_BUFFER_LIST        NetBufferLists,
    __in  NDIS_PORT_NUMBER        PortNumber,
    __in  ULONG                   SendFlags
    )
/*++

Routine Description:

    Send Packet Array handler. Called by NDIS whenever a protocol
    bound to our miniport sends one or more packets.

    The input packet descriptor pointers have been ordered according
    to the order in which the packets should be sent over the network
    by the protocol driver that set up the packet array. The NDIS
    library preserves the protocol-determined ordering when it submits
    each packet array to MiniportSendPackets

    As a deserialized driver, we are responsible for holding incoming send
    packets in our internal queue until they can be transmitted over the
    network and for preserving the protocol-determined ordering of packet
    descriptors incoming to its MiniportSendPackets function.
    A deserialized miniport driver must complete each incoming send packet
    with NdisMSendComplete, and it cannot call NdisMSendResourcesAvailable.

    Runs at IRQL <= DISPATCH_LEVEL

Arguments:

    MiniportAdapterContext      Pointer to our adapter
    NetBufferLists              Head of a list of NBLs to send
    PortNumber                  A miniport adapter port.  Default is 0.
    SendFlags                   Additional flags for the send operation

Return Value:

    None.  Write status directly into each NBL with the NET_BUFFER_LIST_STATUS
    macro.

--*/
{
    PTAP_ADAPTER_CONTEXT   adapter = (PTAP_ADAPTER_CONTEXT )MiniportAdapterContext;

    UNREFERENCED_PARAMETER(NetBufferLists);
    UNREFERENCED_PARAMETER(PortNumber);
    UNREFERENCED_PARAMETER(SendFlags);

    ASSERT(PortNumber == 0); // Only the default port is supported

    // For now just complete all NBLs...
    tapSendNetBufferListsComplete(
        adapter,
        NetBufferLists,
        NDIS_STATUS_SUCCESS,
        (SendFlags & NDIS_SEND_FLAGS_DISPATCH_LEVEL)
        );
}

VOID
AdapterReturnNetBufferLists(
    __in  NDIS_HANDLE             MiniportAdapterContext,
    __in  PNET_BUFFER_LIST        NetBufferLists,
    __in  ULONG                   ReturnFlags
    )
{
    PTAP_ADAPTER_CONTEXT   adapter = (PTAP_ADAPTER_CONTEXT )MiniportAdapterContext;

    UNREFERENCED_PARAMETER(NetBufferLists);
    UNREFERENCED_PARAMETER(ReturnFlags);
}

BOOLEAN
AdapterCheckForHangEx(
    __in  NDIS_HANDLE MiniportAdapterContext
    )
/*++

Routine Description:

    The MiniportCheckForHangEx handler is called to report the state of the
    NIC, or to monitor the responsiveness of an underlying device driver.
    This is an optional function. If this handler is not specified, NDIS
    judges the driver unresponsive when the driver holds
    MiniportQueryInformation or MiniportSetInformation requests for a
    time-out interval (deafult 4 sec), and then calls the driver's
    MiniportReset function. A NIC driver's MiniportInitialize function can
    extend NDIS's time-out interval by calling NdisMSetAttributesEx to
    avoid unnecessary resets.

    MiniportCheckForHangEx runs at IRQL <= DISPATCH_LEVEL.

Arguments:

    MiniportAdapterContext  Pointer to our adapter

Return Value:

    TRUE    NDIS calls the driver's MiniportReset function.
    FALSE   Everything is fine

--*/
{
    PTAP_ADAPTER_CONTEXT   adapter = (PTAP_ADAPTER_CONTEXT )MiniportAdapterContext;

    DEBUGP (("[TAP] --> AdapterCheckForHangEx\n"));

    DEBUGP (("[TAP] <-- AdapterCheckForHangEx; status = FALSE\n"));

    return FALSE;   // Everything is fine
}

NDIS_STATUS
AdapterReset(
    __in   NDIS_HANDLE            MiniportAdapterContext,
    __out PBOOLEAN                AddressingReset
    )
{
    PTAP_ADAPTER_CONTEXT   adapter = (PTAP_ADAPTER_CONTEXT )MiniportAdapterContext;
    NDIS_STATUS    status;

    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(AddressingReset);

    status = NDIS_STATUS_HARD_ERRORS;

    return status;
}

// Free adapter context memory and associated resources.
VOID
tapAdapterContextFree(
    IN PTAP_ADAPTER_CONTEXT     Adapter
    )
{
    PLIST_ENTRY listEntry = &Adapter->AdapterListLink;

    // Adapter contrxt should already be removed.
    ASSERT( (listEntry->Flink == listEntry) && (listEntry->Blink == listEntry ) );

    // Insure that adapter context has been removed from global adapter list.
    RemoveEntryList(&Adapter->AdapterListLink);

    NdisFreeMemory(Adapter,0,0);
}
