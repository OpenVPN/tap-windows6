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

NDIS_OID TAPSupportedOids[] =
{
        OID_GEN_HARDWARE_STATUS,
        OID_GEN_TRANSMIT_BUFFER_SPACE,
        OID_GEN_RECEIVE_BUFFER_SPACE,
        OID_GEN_TRANSMIT_BLOCK_SIZE,
        OID_GEN_RECEIVE_BLOCK_SIZE,
        OID_GEN_VENDOR_ID,
        OID_GEN_VENDOR_DESCRIPTION,
        OID_GEN_VENDOR_DRIVER_VERSION,
        OID_GEN_CURRENT_PACKET_FILTER,
        OID_GEN_CURRENT_LOOKAHEAD,
        OID_GEN_DRIVER_VERSION,
        OID_GEN_MAXIMUM_TOTAL_SIZE,
        OID_GEN_XMIT_OK,
        OID_GEN_RCV_OK,
        OID_GEN_STATISTICS,
#ifdef IMPLEMENT_OPTIONAL_OIDS
        OID_GEN_TRANSMIT_QUEUE_LENGTH,       // Optional
#endif // IMPLEMENT_OPTIONAL_OIDS
        OID_GEN_LINK_PARAMETERS,
        OID_GEN_INTERRUPT_MODERATION,
        OID_GEN_MEDIA_SUPPORTED,
        OID_GEN_MEDIA_IN_USE,
        OID_GEN_MAXIMUM_SEND_PACKETS,
        OID_GEN_XMIT_ERROR,
        OID_GEN_RCV_ERROR,
        OID_GEN_RCV_NO_BUFFER,
        OID_802_3_PERMANENT_ADDRESS,
        OID_802_3_CURRENT_ADDRESS,
        OID_802_3_MULTICAST_LIST,
        OID_802_3_MAXIMUM_LIST_SIZE,
        OID_802_3_RCV_ERROR_ALIGNMENT,
        OID_802_3_XMIT_ONE_COLLISION,
        OID_802_3_XMIT_MORE_COLLISIONS,
#ifdef IMPLEMENT_OPTIONAL_OIDS
        OID_802_3_XMIT_DEFERRED,             // Optional
        OID_802_3_XMIT_MAX_COLLISIONS,       // Optional
        OID_802_3_RCV_OVERRUN,               // Optional
        OID_802_3_XMIT_UNDERRUN,             // Optional
        OID_802_3_XMIT_HEARTBEAT_FAILURE,    // Optional
        OID_802_3_XMIT_TIMES_CRS_LOST,       // Optional
        OID_802_3_XMIT_LATE_COLLISIONS,      // Optional
        OID_PNP_CAPABILITIES,                // Optional
#endif // IMPLEMENT_OPTIONAL_OIDS
};

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

        // Initialize cancel-safe IRP queue
        tapIrpCsqInitialize(&adapter->PendingReadIrpQueue);

        // Initialize TAP send packet queue.
        tapPacketQueueInitialize(&adapter->SendPacketQueue);

        // Allocate the adapter lock.
        NdisAllocateSpinLock(&adapter->AdapterLock);

        // Add initial reference. Normally removed in AdapterHalt.
        adapter->RefCount = 1;

        adapter->MiniportAdapterHandle = MiniportAdapterHandle;

        // Safe for multiple removes.
        NdisInitializeListHead(&adapter->AdapterListLink);

        //
        // The miniport adapter is initially powered up
        //
        adapter->CurrentPowerState = NdisDeviceStateD0;
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
    BOOLEAN macFromRegistry = FALSE;

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
        if( (configParameter->ParameterType == NdisParameterString)
            && (configParameter->ParameterData.StringData.Length >= 12)
            )
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
        // Make up a dummy mac address based on the ANSI representation of the
        // NetCfgInstanceId GUID.
        //
        GenerateRandomMac(Adapter->PermanentAddress, MINIPORT_INSTANCE_ID(Adapter));
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
    Adapter->LogicalMediaState = FALSE;
    Adapter->AllowNonAdmin = FALSE;
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
        NDIS_STRING mkey = NDIS_STRING_CONST("NetCfgInstanceId");

        //
        // Read NetCfgInstanceId from the registry.
        // ------------------------------------
        // NetCfgInstanceId is required to create device and associated
        // symbolic link for the adapter device.
        //
        // NetCfgInstanceId is  a GUID string provided by NDIS that identifies
        // the adapter instance. An example is:
        // 
        //    NetCfgInstanceId={410EB49D-2381-4FE7-9B36-498E22619DF0}
        //
        // Other names are derived from NetCfgInstanceId. For example, MiniportName:
        //
        //    MiniportName=\DEVICE\{410EB49D-2381-4FE7-9B36-498E22619DF0}
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
                DEBUGP (("[TAP] NdisReadConfiguration (NetCfgInstanceId=%wZ)\n",
                    &configParameter->ParameterData.StringData ));

                // Save NetCfgInstanceId as UNICODE_STRING.
                Adapter->NetCfgInstanceId.Length = Adapter->NetCfgInstanceId.MaximumLength
                    = configParameter->ParameterData.StringData.Length;

                Adapter->NetCfgInstanceId.Buffer = Adapter->NetCfgInstanceIdBuffer;

                NdisMoveMemory(
                    Adapter->NetCfgInstanceId.Buffer, 
                    configParameter->ParameterData.StringData.Buffer,
                    Adapter->NetCfgInstanceId.Length
                    );

                // Save NetCfgInstanceId as ANSI_STRING as well.
                if (RtlUnicodeStringToAnsiString (
                        &Adapter->NetCfgInstanceIdAnsi,
                        &configParameter->ParameterData.StringData,
                        TRUE) != STATUS_SUCCESS
                    )
                {
                    DEBUGP (("[TAP] NetCfgInstanceId ANSI name conversion failed\n"));
                    status = NDIS_STATUS_RESOURCES;
                }
            }
            else
            {
                DEBUGP (("[TAP] NetCfgInstanceId has invalid type\n"));
                status = NDIS_STATUS_INVALID_DATA;
            }
        }
        else
        {
            DEBUGP (("[TAP] NetCfgInstanceId failed\n"));
            status = NDIS_STATUS_INVALID_DATA;
        }

        if (status == NDIS_STATUS_SUCCESS)
        {
            NDIS_STATUS localStatus;    // Use default if these fail.
            NDIS_CONFIGURATION_PARAMETER *configParameter;
            NDIS_STRING mtuKey = NDIS_STRING_CONST("MTU");
            NDIS_STRING mediaStatusKey = NDIS_STRING_CONST("MediaStatus");
#if ENABLE_NONADMIN
            NDIS_STRING allowNonAdminKey = NDIS_STRING_CONST("AllowNonAdmin");
#endif

            // Read MTU from the registry.
            NdisReadConfiguration (
                &localStatus,
                &configParameter,
                configHandle,
                &mtuKey,
                NdisParameterInteger
                );

            if (localStatus == NDIS_STATUS_SUCCESS)
            {
                if (configParameter->ParameterType == NdisParameterInteger)
                {
                    int mtu = configParameter->ParameterData.IntegerData;

                    if(mtu == 0)
                    {
                        mtu = ETHERNET_MTU;
                    }

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

            DEBUGP (("[%s] Using MTU %d\n",
                MINIPORT_INSTANCE_ID (Adapter),
                Adapter->MtuSize
                ));

            // Read MediaStatus setting from registry.
            NdisReadConfiguration (
                &localStatus,
                &configParameter,
                configHandle,
                &mediaStatusKey,
                NdisParameterInteger
                );

            if (localStatus == NDIS_STATUS_SUCCESS)
            {
                if (configParameter->ParameterType == NdisParameterInteger)
                {
                    if(configParameter->ParameterData.IntegerData == 0)
                    {
                        // Connect state is appplication controlled.
                        DEBUGP(("[%s] Initial MediaConnectState: Application Controlled\n",
                            MINIPORT_INSTANCE_ID (Adapter)));

                        Adapter->MediaStateAlwaysConnected = FALSE;
                        Adapter->LogicalMediaState = FALSE;
                    }
                    else
                    {
                        // Connect state is always connected.
                        DEBUGP(("[%s] Initial MediaConnectState: Always Connected\n",
                            MINIPORT_INSTANCE_ID (Adapter)));

                        Adapter->MediaStateAlwaysConnected = TRUE;
                        Adapter->LogicalMediaState = TRUE;
                    }
                }
            }

            // Read MAC PermanentAddress setting from registry.
            tapReadPermanentAddress(
                Adapter,
                configHandle,
                Adapter->PermanentAddress
                );

            DEBUGP (("[%s] Using MAC PermanentAddress %x:%x:%x:%x:%x:%x\n",
                MINIPORT_INSTANCE_ID (Adapter),
                Adapter->PermanentAddress[0],
                Adapter->PermanentAddress[1],
                Adapter->PermanentAddress[2],
                Adapter->PermanentAddress[3],
                Adapter->PermanentAddress[4],
                Adapter->PermanentAddress[5])
                );

            // Now seed the current MAC address with the permanent address.
            COPY_MAC(Adapter->CurrentAddress, Adapter->PermanentAddress);

            DEBUGP (("[%s] Using MAC CurrentAddress %x:%x:%x:%x:%x:%x\n",
                MINIPORT_INSTANCE_ID (Adapter),
                Adapter->CurrentAddress[0],
                Adapter->CurrentAddress[1],
                Adapter->CurrentAddress[2],
                Adapter->CurrentAddress[3],
                Adapter->CurrentAddress[4],
                Adapter->CurrentAddress[5])
                );

            // Read optional AllowNonAdmin setting from registry.
#if ENABLE_NONADMIN
            NdisReadConfiguration (
                &localStatus,
                &configParameter,
                configHandle,
                &allowNonAdminKey,
                NdisParameterInteger
                );

            if (localStatus == NDIS_STATUS_SUCCESS)
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

VOID
tapAdapterContextAddToGlobalList(
    __in PTAP_ADAPTER_CONTEXT       Adapter
    )
{
    LOCK_STATE      lockState;
    PLIST_ENTRY     listEntry = &Adapter->AdapterListLink;

    // Acquire global adapter list lock.
    NdisAcquireReadWriteLock(
        &GlobalData.Lock,
        TRUE,      // Acquire for write
        &lockState
        );

    // Adapter context should NOT be in any list.
    ASSERT( (listEntry->Flink == listEntry) && (listEntry->Blink == listEntry ) );

    // Add reference to persist until after removal.
    tapAdapterContextReference(Adapter);

    // Add the adapter context to the global list.
    InsertTailList(&GlobalData.AdapterList,&Adapter->AdapterListLink);

    // Release global adapter list lock.
    NdisReleaseReadWriteLock(&GlobalData.Lock,&lockState);
}

VOID
tapAdapterContextRemoveFromGlobalList(
    __in PTAP_ADAPTER_CONTEXT       Adapter
    )
{
    LOCK_STATE              lockState;

    // Acquire global adapter list lock.
    NdisAcquireReadWriteLock(
        &GlobalData.Lock,
        TRUE,      // Acquire for write
        &lockState
        );

    // Remove the adapter context from the global list.
    RemoveEntryList(&Adapter->AdapterListLink);

    // Safe for multiple removes.
    NdisInitializeListHead(&Adapter->AdapterListLink);

    // Remove reference added in tapAdapterContextAddToGlobalList.
    tapAdapterContextDereference(Adapter);

    // Release global adapter list lock.
    NdisReleaseReadWriteLock(&GlobalData.Lock,&lockState);
}

// Returns with added reference on adapter context.
PTAP_ADAPTER_CONTEXT
tapAdapterContextFromDeviceObject(
    __in PDEVICE_OBJECT DeviceObject
    )
{
    LOCK_STATE              lockState;

    // Acquire global adapter list lock.
    NdisAcquireReadWriteLock(
        &GlobalData.Lock,
        FALSE,      // Acquire for read
        &lockState
        );

    if (!IsListEmpty(&GlobalData.AdapterList))
    {
        PLIST_ENTRY             entry = GlobalData.AdapterList.Flink;
        PTAP_ADAPTER_CONTEXT    adapter;

        while (entry != &GlobalData.AdapterList)
        {
            adapter = CONTAINING_RECORD(entry, TAP_ADAPTER_CONTEXT, AdapterListLink);

            // Match on DeviceObject
            if(adapter->DeviceObject == DeviceObject )
            {
                // Add reference to adapter context.
                tapAdapterContextReference(adapter);

                // Release global adapter list lock.
                NdisReleaseReadWriteLock(&GlobalData.Lock,&lockState);

                return adapter;
            }
        }
    }

    // Release global adapter list lock.
    NdisReleaseReadWriteLock(&GlobalData.Lock,&lockState);

    return (PTAP_ADAPTER_CONTEXT )NULL;
}

NDIS_STATUS
AdapterSetOptions(
    __in  NDIS_HANDLE             NdisDriverHandle,
    __in  NDIS_HANDLE             DriverContext
    )
/*++
Routine Description:

    The MiniportSetOptions function registers optional handlers.  For each
    optional handler that should be registered, this function makes a call
    to NdisSetOptionalHandlers.

    MiniportSetOptions runs at IRQL = PASSIVE_LEVEL.

Arguments:

    DriverContext  The context handle

Return Value:

    NDIS_STATUS_xxx code

--*/
{
    NDIS_STATUS status;

    DEBUGP (("[TAP] --> AdapterSetOptions\n"));

    //
    // Set any optional handlers by filling out the appropriate struct and
    // calling NdisSetOptionalHandlers here.
    //

    status = NDIS_STATUS_SUCCESS;

    DEBUGP (("[TAP] <-- AdapterSetOptions; status = %8.8X\n",status));

    return status;
}

NDIS_STATUS
AdapterCreate(
    __in  NDIS_HANDLE                         MiniportAdapterHandle,
    __in  NDIS_HANDLE                         MiniportDriverContext,
    __in  PNDIS_MINIPORT_INIT_PARAMETERS      MiniportInitParameters
    )
{
    PTAP_ADAPTER_CONTEXT    adapter = NULL;
    NDIS_STATUS             status;

    UNREFERENCED_PARAMETER(MiniportDriverContext);
    UNREFERENCED_PARAMETER(MiniportInitParameters);

    DEBUGP (("[TAP] --> AdapterCreate\n"));

    do
    {
        NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES regAttributes = {0};
        NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES genAttributes = {0};
        NDIS_PNP_CAPABILITIES pnpCapabilities = {0};

        //
        // Allocate adapter context structure and initialize all the
        // memory resources for sending and receiving packets.
        //
        // Returns with reference count initialized to one.
        //
        adapter = tapAdapterContextAllocate(MiniportAdapterHandle);

        if(adapter == NULL)
        {
            DEBUGP (("[TAP] Couldn't allocate adapter memory\n"));
            status = NDIS_STATUS_RESOURCES;
            break;
        }

        // Enter the Initializing state.
        DEBUGP (("[TAP] Miniport State: Initializing\n"));

        tapAdapterAcquireLock(adapter,FALSE);
        adapter->Locked.AdapterState = MiniportInitializingState;
        tapAdapterReleaseLock(adapter,FALSE);

        //
        // First read adapter configuration from registry.
        // -----------------------------------------------
        // Subsequent device registration will fail if NetCfgInstanceId
        // has not been successfully read.
        //
        status = tapReadConfiguration(adapter);

        //
        // Set the registration attributes.
        //
        {C_ASSERT(sizeof(regAttributes) >= NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1);}
        regAttributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
        regAttributes.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
        regAttributes.Header.Revision = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;

        regAttributes.MiniportAdapterContext = adapter;
        regAttributes.AttributeFlags = TAP_ADAPTER_ATTRIBUTES_FLAGS;

        regAttributes.CheckForHangTimeInSeconds = TAP_ADAPTER_CHECK_FOR_HANG_TIME_IN_SECONDS;
        regAttributes.InterfaceType = TAP_INTERFACE_TYPE;

        //NDIS_DECLARE_MINIPORT_ADAPTER_CONTEXT(TAP_ADAPTER_CONTEXT);
        status = NdisMSetMiniportAttributes(
                    MiniportAdapterHandle,
                    (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&regAttributes
                    );

        if (status != NDIS_STATUS_SUCCESS)
        {
            DEBUGP (("[TAP] NdisSetOptionalHandlers failed; Status 0x%08x\n",status));
            break;
        }

        //
        // Next, set the general attributes.
        //
        {C_ASSERT(sizeof(genAttributes) >= NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1);}
        genAttributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
        genAttributes.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
        genAttributes.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;

        //
        // Specify the medium type that the NIC can support but not
        // necessarily the medium type that the NIC currently uses.
        //
        genAttributes.MediaType = TAP_MEDIUM_TYPE;

        //
        // Specifiy medium type that the NIC currently uses.
        //
        genAttributes.PhysicalMediumType = TAP_PHYSICAL_MEDIUM;

        //
        // Specifiy the maximum network frame size, in bytes, that the NIC
        // supports excluding the header.
        //
        genAttributes.MtuSize = TAP_FRAME_MAX_DATA_SIZE;
        genAttributes.MaxXmitLinkSpeed = TAP_XMIT_SPEED;
        genAttributes.XmitLinkSpeed = TAP_XMIT_SPEED;
        genAttributes.MaxRcvLinkSpeed = TAP_RECV_SPEED;
        genAttributes.RcvLinkSpeed = TAP_RECV_SPEED;

        if(adapter->MediaStateAlwaysConnected)
        {
            DEBUGP(("[%s] Initial MediaConnectState: Connected\n",
                MINIPORT_INSTANCE_ID (adapter)));

            genAttributes.MediaConnectState = MediaConnectStateConnected;
        }
        else
        {
            DEBUGP(("[%s] Initial MediaConnectState: Disconnected\n",
                MINIPORT_INSTANCE_ID (adapter)));

            genAttributes.MediaConnectState = MediaConnectStateDisconnected;
        }

        genAttributes.MediaDuplexState = MediaDuplexStateFull;

        //
        // The maximum number of bytes the NIC can provide as lookahead data.
        // If that value is different from the size of the lookahead buffer
        // supported by bound protocols, NDIS will call MiniportOidRequest to
        // set the size of the lookahead buffer provided by the miniport driver
        // to the minimum of the miniport driver and protocol(s) values. If the
        // driver always indicates up full packets with
        // NdisMIndicateReceiveNetBufferLists, it should set this value to the
        // maximum total frame size, which excludes the header.
        //
        // Upper-layer drivers examine lookahead data to determine whether a
        // packet that is associated with the lookahead data is intended for
        // one or more of their clients. If the underlying driver supports
        // multipacket receive indications, bound protocols are given full net
        // packets on every indication. Consequently, this value is identical
        // to that returned for OID_GEN_RECEIVE_BLOCK_SIZE.
        //
        genAttributes.LookaheadSize = TAP_MAX_LOOKAHEAD;
        genAttributes.MacOptions = TAP_MAC_OPTIONS;
        genAttributes.SupportedPacketFilters = TAP_SUPPORTED_FILTERS;

        //
        // The maximum number of multicast addresses the NIC driver can manage.
        // This list is global for all protocols bound to (or above) the NIC.
        // Consequently, a protocol can receive NDIS_STATUS_MULTICAST_FULL from
        // the NIC driver when attempting to set the multicast address list,
        // even if the number of elements in the given list is less than the
        // number originally returned for this query.
        //
        genAttributes.MaxMulticastListSize = TAP_MAX_MCAST_LIST;
        genAttributes.MacAddressLength = MACADDR_SIZE;

        //
        // Return the MAC address of the NIC burnt in the hardware.
        //
        COPY_MAC(genAttributes.PermanentMacAddress, adapter->PermanentAddress);

        //
        // Return the MAC address the NIC is currently programmed to use. Note
        // that this address could be different from the permananent address as
        // the user can override using registry. Read NdisReadNetworkAddress
        // doc for more info.
        //
        COPY_MAC(genAttributes.CurrentMacAddress, adapter->CurrentAddress);

        genAttributes.RecvScaleCapabilities = NULL;
        genAttributes.AccessType = TAP_ACCESS_TYPE;
        genAttributes.DirectionType = TAP_DIRECTION_TYPE;
        genAttributes.ConnectionType = TAP_CONNECTION_TYPE;
        genAttributes.IfType = TAP_IFTYPE;
        genAttributes.IfConnectorPresent = TAP_HAS_PHYSICAL_CONNECTOR;
        genAttributes.SupportedStatistics = TAP_SUPPORTED_STATISTICS;
        genAttributes.SupportedPauseFunctions = NdisPauseFunctionsUnsupported; // IEEE 802.3 pause frames 
        genAttributes.DataBackFillSize = 0;
        genAttributes.ContextBackFillSize = 0;

        //
        // The SupportedOidList is an array of OIDs for objects that the
        // underlying driver or its NIC supports.  Objects include general,
        // media-specific, and implementation-specific objects. NDIS forwards a
        // subset of the returned list to protocols that make this query. That
        // is, NDIS filters any supported statistics OIDs out of the list
        // because protocols never make statistics queries.
        //
        genAttributes.SupportedOidList = TAPSupportedOids;
        genAttributes.SupportedOidListLength = sizeof(TAPSupportedOids);
        genAttributes.AutoNegotiationFlags = NDIS_LINK_STATE_DUPLEX_AUTO_NEGOTIATED;

        //
        // Set power management capabilities
        //
        NdisZeroMemory(&pnpCapabilities, sizeof(pnpCapabilities));
        pnpCapabilities.WakeUpCapabilities.MinMagicPacketWakeUp = NdisDeviceStateUnspecified;
        pnpCapabilities.WakeUpCapabilities.MinPatternWakeUp = NdisDeviceStateUnspecified;
        genAttributes.PowerManagementCapabilities = &pnpCapabilities;

        status = NdisMSetMiniportAttributes(
                    MiniportAdapterHandle,
                    (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&genAttributes
                    );

        if (status != NDIS_STATUS_SUCCESS)
        {
            DEBUGP (("[TAP] NdisMSetMiniportAttributes failed; Status 0x%08x\n",status));
            break;
        }

        //
        // Create the Win32 device I/O interface.
        //
        status = CreateTapDevice(adapter);

        if (status == NDIS_STATUS_SUCCESS)
        {
            // Add this adapter to the global adapter list.
            tapAdapterContextAddToGlobalList(adapter);
        }
        else
        {
            DEBUGP (("[TAP] CreateTapDevice failed; Status 0x%08x\n",status));
            break;
        }
    } while(FALSE);

    if(status == NDIS_STATUS_SUCCESS)
    {
        // Enter the Paused state if initialization is complete.
        DEBUGP (("[TAP] Miniport State: Paused\n"));

        tapAdapterAcquireLock(adapter,FALSE);
        adapter->Locked.AdapterState = MiniportPausedState;
        tapAdapterReleaseLock(adapter,FALSE);
    }
    else
    {
        if(adapter != NULL)
        {
            DEBUGP (("[TAP] Miniport State: Halted\n"));

            //
            // Remove reference when adapter context was allocated
            // ---------------------------------------------------
            // This should result in freeing adapter context memory
            // and assiciated resources.
            //
            tapAdapterContextDereference(adapter);
            adapter = NULL;
        }
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

    // Enter the Halted state.
    DEBUGP (("[TAP] Miniport State: Halted\n"));

    tapAdapterAcquireLock(adapter,FALSE);
    adapter->Locked.AdapterState = MiniportHaltedState;
    tapAdapterReleaseLock(adapter,FALSE);

    // Remove this adapter from the global adapter list.
    tapAdapterContextRemoveFromGlobalList(adapter);

    // BUGBUG!!! Call AdapterShutdownEx to do some of the work of stopping.

    // TODO!!! More...

    //
    // Destroy the TAP Win32 device.
    //
    DestroyTapDevice(adapter);

    //
    // Remove initial reference added in AdapterCreate.
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
/*++

Routine Description:

    When a miniport receives a pause request, it enters into a Pausing state.
    The miniport should not indicate up any more network data.  Any pending
    send requests must be completed, and new requests must be rejected with
    NDIS_STATUS_PAUSED.

    Once all sends have been completed and all recieve NBLs have returned to
    the miniport, the miniport enters the Paused state.

    While paused, the miniport can still service interrupts from the hardware
    (to, for example, continue to indicate NDIS_STATUS_MEDIA_CONNECT
    notifications).

    The miniport must continue to be able to handle status indications and OID
    requests.  MiniportPause is different from MiniportHalt because, in
    general, the MiniportPause operation won't release any resources.
    MiniportPause must not attempt to acquire any resources where allocation
    can fail, since MiniportPause itself must not fail.


    MiniportPause runs at IRQL = PASSIVE_LEVEL.

Arguments:

    MiniportAdapterContext  Pointer to the Adapter
    MiniportPauseParameters  Additional information about the pause operation

Return Value:

    If the miniport is able to immediately enter the Paused state, it should
    return NDIS_STATUS_SUCCESS.

    If the miniport must wait for send completions or pending receive NBLs, it
    should return NDIS_STATUS_PENDING now, and call NDISMPauseComplete when the
    miniport has entered the Paused state.

    No other return value is permitted.  The pause operation must not fail.

--*/
{
    PTAP_ADAPTER_CONTEXT   adapter = (PTAP_ADAPTER_CONTEXT )MiniportAdapterContext;
    NDIS_STATUS    status;

    UNREFERENCED_PARAMETER(PauseParameters);

    DEBUGP (("[TAP] --> AdapterPause\n"));

    // Enter the Pausing state.
    DEBUGP (("[TAP] Miniport State: Pausing\n"));

    tapAdapterAcquireLock(adapter,FALSE);
    adapter->Locked.AdapterState = MiniportPausingState;
    tapAdapterReleaseLock(adapter,FALSE);

    status = NDIS_STATUS_SUCCESS;

    // Enter the Paused state.
    DEBUGP (("[TAP] Miniport State: Paused\n"));

    tapAdapterAcquireLock(adapter,FALSE);
    adapter->Locked.AdapterState = MiniportPausedState;
    tapAdapterReleaseLock(adapter,FALSE);

    DEBUGP (("[TAP] <-- AdapterPause; status = %8.8X\n",status));

    return status;
}

NDIS_STATUS
AdapterRestart(
    __in  NDIS_HANDLE                             MiniportAdapterContext,
    __in  PNDIS_MINIPORT_RESTART_PARAMETERS       RestartParameters
    )
/*++

Routine Description:

    When a miniport receives a restart request, it enters into a Restarting
    state.  The miniport may begin indicating received data (e.g., using
    NdisMIndicateReceiveNetBufferLists), handling status indications, and
    processing OID requests in the Restarting state.  However, no sends will be
    requested while the miniport is in the Restarting state.

    Once the miniport is ready to send data, it has entered the Running state.
    The miniport informs NDIS that it is in the Running state by returning
    NDIS_STATUS_SUCCESS from this MiniportRestart function; or if this function
    has already returned NDIS_STATUS_PENDING, by calling NdisMRestartComplete.


    MiniportRestart runs at IRQL = PASSIVE_LEVEL.

Arguments:

    MiniportAdapterContext  Pointer to the Adapter
    RestartParameters  Additional information about the restart operation

Return Value:

    If the miniport is able to immediately enter the Running state, it should
    return NDIS_STATUS_SUCCESS.

    If the miniport is still in the Restarting state, it should return
    NDIS_STATUS_PENDING now, and call NdisMRestartComplete when the miniport
    has entered the Running state.

    Other NDIS_STATUS codes indicate errors.  If an error is encountered, the
    miniport must return to the Paused state (i.e., stop indicating receives).

--*/
{
    PTAP_ADAPTER_CONTEXT   adapter = (PTAP_ADAPTER_CONTEXT )MiniportAdapterContext;
    NDIS_STATUS    status;

    UNREFERENCED_PARAMETER(RestartParameters);

    DEBUGP (("[TAP] --> AdapterRestart\n"));

    // Enter the Restarting state.
    DEBUGP (("[TAP] Miniport State: Restarting\n"));

    tapAdapterAcquireLock(adapter,FALSE);
    adapter->Locked.AdapterState = MiniportRestartingState;
    tapAdapterReleaseLock(adapter,FALSE);

    status = NDIS_STATUS_SUCCESS;

    if(status == NDIS_STATUS_SUCCESS)
    {
        // Enter the Running state.
        DEBUGP (("[TAP] Miniport State: Running\n"));

        tapAdapterAcquireLock(adapter,FALSE);
        adapter->Locked.AdapterState = MiniportRunning;
        tapAdapterReleaseLock(adapter,FALSE);
    }
    else
    {
        // Enter the Paused state if restart failed.
        DEBUGP (("[TAP] Miniport State: Paused\n"));

        tapAdapterAcquireLock(adapter,FALSE);
        adapter->Locked.AdapterState = MiniportPausedState;
        tapAdapterReleaseLock(adapter,FALSE);
    }

    DEBUGP (("[TAP] <-- AdapterRestart; status = %8.8X\n",status));

    return status;
}

ULONG
tapGetNetBufferFrameType(
    __in PNET_BUFFER       NetBuffer
    )
/*++

Routine Description:

    Reads the network frame's destination address to determine the type
    (broadcast, multicast, etc)

    Runs at IRQL <= DISPATCH_LEVEL.

Arguments:

    NetBuffer                 The NB to examine

Return Value:

    NDIS_PACKET_TYPE_BROADCAST
    NDIS_PACKET_TYPE_MULTICAST
    NDIS_PACKET_TYPE_DIRECTED

--*/
{
    PETH_HEADER ethernetHeader;

    ethernetHeader = (PETH_HEADER )NdisGetDataBuffer(
                        NetBuffer,
                        sizeof(ETH_HEADER),
                        NULL,
                        1,
                        0
                        );

    ASSERT(ethernetHeader);

    if (ETH_IS_BROADCAST(ethernetHeader->dest))
    {
        return NDIS_PACKET_TYPE_BROADCAST;
    }
    else if(ETH_IS_MULTICAST(ethernetHeader->dest))
    {
        return NDIS_PACKET_TYPE_MULTICAST;
    }
    else
    {
        return NDIS_PACKET_TYPE_DIRECTED;
    }

}

ULONG
tapGetNetBufferCountsFromNetBufferList(
    __in PNET_BUFFER_LIST   NetBufferList,
    __inout_opt PULONG      TotalByteCount      // Of all linked NBs
    )
/*++

Routine Description:

    Returns the number of net buffers linked to the net buffer list.

    Optionally retuens the total byte count of all net buffers linked
    to the net buffer list

    Runs at IRQL <= DISPATCH_LEVEL.

Arguments:

    NetBufferList                 The NBL to examine

Return Value:

    The number of net buffers linked to the net buffer list.

--*/
{
    ULONG       netBufferCount = 0;
    PNET_BUFFER currentNb;

    if(TotalByteCount)
    {
        *TotalByteCount = 0;
    }

    currentNb = NET_BUFFER_LIST_FIRST_NB(NetBufferList);

    while(currentNb)
    {
        ++netBufferCount;

        if(TotalByteCount)
        {
            *TotalByteCount += NET_BUFFER_DATA_LENGTH(currentNb);
        }

        // Move to next NB
        currentNb = NET_BUFFER_NEXT_NB(currentNb);
    }

    return netBufferCount;
}

VOID
tapSendNetBufferListsComplete(
    __in PTAP_ADAPTER_CONTEXT   Adapter,
    __in PNET_BUFFER_LIST       NetBufferLists,
    __in NDIS_STATUS            SendCompletionStatus,
    __in BOOLEAN                DispatchLevel
    )
{
    PNET_BUFFER_LIST    currentNbl;
    PNET_BUFFER_LIST    nextNbl = NULL;
    ULONG               sendCompleteFlags = 0;

    for (
        currentNbl = NetBufferLists;
        currentNbl != NULL;
        currentNbl = nextNbl
        )
    {
        ULONG       frameType;
        ULONG       netBufferCount;
        ULONG       byteCount;

        nextNbl = NET_BUFFER_LIST_NEXT_NBL(currentNbl);

        // Set NBL completion status.
        NET_BUFFER_LIST_STATUS(currentNbl) = SendCompletionStatus;

        // Fetch first NBs frame type. All linked NBs will have same type.
        frameType = tapGetNetBufferFrameType(NET_BUFFER_LIST_FIRST_NB(currentNbl));

        // Fetch statistics for all NBs linked to the NB.
        netBufferCount = tapGetNetBufferCountsFromNetBufferList(
                            currentNbl,
                            &byteCount
                            );

        // Update statistics by frame type
        if(SendCompletionStatus == NDIS_STATUS_SUCCESS)
        {
            switch(frameType)
            {
            case NDIS_PACKET_TYPE_DIRECTED:
                Adapter->FramesTxDirected += netBufferCount;
                Adapter->BytesTxDirected += byteCount;
                break;

            case NDIS_PACKET_TYPE_BROADCAST:
                Adapter->FramesTxBroadcast += netBufferCount;
                Adapter->BytesTxBroadcast += byteCount;
                break;

            case NDIS_PACKET_TYPE_MULTICAST:
                Adapter->FramesTxMulticast += netBufferCount;
                Adapter->BytesTxMulticast += byteCount;
                break;

            default:
                ASSERT(FALSE);
                break;
            }
        }
        else
        {
            // Transmit error.
            Adapter->TransmitFailuresOther += netBufferCount;
        }

        currentNbl = nextNbl;
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

BOOLEAN
tapAdapterReadAndWriteReady(
    __in PTAP_ADAPTER_CONTEXT     Adapter
    )
/*++

Routine Description:

    This routine determines whether the adapter device interface can
    accept read and write operations.

Arguments:

    Adapter              Pointer to our adapter context

Return Value:

    Returns TRUE if the adapter state allows it to queue IRPs passed to
    the device read and write callbacks.
--*/
{
    if(!Adapter->TapDeviceCreated)
    {
        // TAP device not created or is being destroyed.
        return FALSE;
    }

    if(Adapter->TapFileObject == NULL)
    {
        // TAP application file object not open.
        return FALSE;
    }

    if(!Adapter->TapFileIsOpen)
    {
        // TAP application file object may be closing.
        return FALSE;
    }

    if(!Adapter->LogicalMediaState)
    {
        // Don't handle read/write if media not connected.
        return FALSE;
    }

    if(Adapter->CurrentPowerState != NdisDeviceStateD0)
    {
        // Don't handle read/write if device is not fully powered.
        return FALSE;
    }

    return TRUE;
}

NDIS_STATUS
tapAdapterSendAndReceiveReady(
    __in PTAP_ADAPTER_CONTEXT     Adapter
    )
/*++

Routine Description:

    This routine determines whether the adapter NDIS send and receive
    paths are ready.

    This routine examines various adapter state variables and returns
    a value that indicates whether the adapter NDIS interfaces can
    accept send packets or indicate receive packets.

    In normal operation the adapter may temporarily enter and then exit
    a not-ready condition. In particular, the adapter becomes not-ready
    when in the Pausing/Paused states, but may become ready again when
    Restarted.

    Runs at IRQL <= DISPATCH_LEVEL

Arguments:

    Adapter              Pointer to our adapter context

Return Value:

    Returns NDIS_STATUS_SUCCESS if the adapter state allows it to
    accept send packets and indicate receive packets.

    Otherwise it returns a NDIS_STATUS value other than NDIS_STATUS_SUCCESS.
    These status values can be used directly as the completion status for
    packets that must be completed immediatly in the send path.
--*/
{
    NDIS_STATUS status = NDIS_STATUS_SUCCESS;

    //
    // Check various state variables to insure adapter is ready.
    //
    tapAdapterAcquireLock(Adapter,FALSE);

    if(!Adapter->LogicalMediaState)
    {
        status = NDIS_STATUS_MEDIA_DISCONNECTED;
    }
    else if(Adapter->CurrentPowerState != NdisDeviceStateD0)
    {
        status = NDIS_STATUS_LOW_POWER_STATE;
    }
    else if(Adapter->ResetInProgress)
    {
        status = NDIS_STATUS_RESET_IN_PROGRESS;
    }
    else
    {
        switch(Adapter->Locked.AdapterState)
        {
        case MiniportPausingState:
        case MiniportPausedState:
            status = NDIS_STATUS_PAUSED;
            break;

        case MiniportHaltedState:
            status = NDIS_STATUS_INVALID_STATE;
            break;

        default:
            status = NDIS_STATUS_SUCCESS;
            break;
        }
    }

    tapAdapterReleaseLock(Adapter,FALSE);

    return status;
}

BOOLEAN
tapNetBufferListNetBufferLengthsValid(
    __in PTAP_ADAPTER_CONTEXT   Adapter,
    __in  PNET_BUFFER_LIST      NetBufferLists
    )
/*++

Routine Description:

    Scan all NBLs and their linked NBs for valid lengths.

    Fairly absurd to find and packets with bogus lengths, but wise
    to check anyway. If ANY packet has a bogus length, then abort the
    entire send.

    The only time that one might see this check fail might be during
    HCK driver testing. The HKC test might send oversize packets to
    determine if the miniport can gracefully deal with them.

    This check is fairly fast. Unlike NDIS 5 packets, fetching NDIS 6
    packets lengths do not require any computation.

Arguments:

    Adapter                 Pointer to our adapter context
    NetBufferLists          Head of a list of NBLs to examine

Return Value:

    Returns TRUE if all NBs have reasonable lengths.
    Otherwise, returns FALSE.

--*/
{
    PNET_BUFFER_LIST        currentNbl;

    currentNbl = NetBufferLists;

    while (currentNbl)
    {
        PNET_BUFFER_LIST    nextNbl;
        PNET_BUFFER         currentNb;

        // Locate next NBL
        nextNbl = NET_BUFFER_LIST_NEXT_NBL(currentNbl);

        // Locate first NB (aka "packet")
        currentNb = NET_BUFFER_LIST_FIRST_NB(currentNbl);

        //
        // Process all NBs linked to this NBL
        //
        while(currentNb)
        {
            PNET_BUFFER nextNb;
            ULONG       packetLength;

            // Locate next NB
            nextNb = NET_BUFFER_NEXT_NB(currentNb);

            packetLength = NET_BUFFER_DATA_LENGTH(currentNb);

            // Minimum packet size is size of Ethernet plus IPv4 headers.
            ASSERT(packetLength >= (ETHERNET_HEADER_SIZE + IP_HEADER_SIZE));

            if(packetLength < (ETHERNET_HEADER_SIZE + IP_HEADER_SIZE))
            {
                return FALSE;
            }

            // Maximum size should be Ethernet header size plus MTU plus modest pad for
            // VLAN tag.
            ASSERT( packetLength <= (ETHERNET_HEADER_SIZE + VLAN_TAG_SIZE + Adapter->MtuSize));

            if(packetLength > (ETHERNET_HEADER_SIZE + VLAN_TAG_SIZE + Adapter->MtuSize))
            {
                return FALSE;
            }

            // Move to next NB
            currentNb = nextNb;
        }

        // Move to next NBL
        currentNbl = nextNbl;
    }

    return TRUE;
}

VOID
tapAdapterTransmit(
    __in PTAP_ADAPTER_CONTEXT   Adapter,
    __in PNET_BUFFER            NetBuffer,
    __in  BOOLEAN               DispatchLevel
    )
/*++

Routine Description:

    This routine is called to transmit an individual net buffer using a
    style similar to the previous NDIS 5 AdapterTransmit function.

    In this implementation adapter state and NB length checks have already
    been done before this function has been called.

    The net buffer will be completed by the calling routine after this
    routine exits. So, under this design it is necessary to make a deep
    copy of frame data in the net buffer.

    This routine creates a flat buffer copy of NB frame data. This is an
    unnecessary performance bottleneck. However, the bottleneck is probably
    not significant or measurable except for adapters running at 1Gbps or
    greater speeds. Since this adapter is currently running at 100Mbps this
    defect can be ignored.

    Runs at IRQL <= DISPATCH_LEVEL

Arguments:

    Adapter                     Pointer to our adapter context
    NetBuffer                   Pointer to the net buffer to transmit
    DispatchLevel               TRUE if called at IRQL == DISPATCH_LEVEL

Return Value:

    None.

    In the Microsoft NDIS 6 architecture there is no per-packet status.

--*/
{
    NDIS_STATUS     status;
    ULONG           packetLength;
    PTAP_PACKET     tapPacket;
    PVOID           packetData;

    packetLength = NET_BUFFER_DATA_LENGTH(NetBuffer);

    // Allocate TAP packet memory
    tapPacket = (PTAP_PACKET )NdisAllocateMemoryWithTagPriority(
                    Adapter->MiniportAdapterHandle,
                    TAP_PACKET_SIZE (packetLength),
                    TAP_PACKET_TAG,
                    NormalPoolPriority
                    );

    if(tapPacket == NULL)
    {
        DEBUGP (("[TAP] tapAdapterTransmit: TAP packet allocation failed\n"));
        return;
    }

    tapPacket->m_SizeFlags = (packetLength & TP_SIZE_MASK);

    //
    // Reassemble packet contents
    // --------------------------
    // NdisGetDataBuffer does most of the work. There are two cases:
    //
    //    1.) If the NB data was not contiguous it will copy the entire
    //        NB's data to m_data and return pointer to m_data.
    //    2.) If the NB data was contiguous it returns a pointer to the
    //        first byte of the contiguous data instead of a pointer to m_Data.
    //        In this case the data will not have been copied to m_Data. Copy
    //        to m_Data will need to be done in an extra step.
    //
    // Case 1.) is the most likely in normal operation.
    //
    packetData = NdisGetDataBuffer(NetBuffer,packetLength,tapPacket->m_Data,1,0);

    if(packetData == NULL)
    {
        DEBUGP (("[TAP] tapAdapterTransmit: Could not get packet data\n"));

        NdisFreeMemory(tapPacket,0,0);

        return;
    }

    if(packetData != tapPacket->m_Data)
    {
        // Packet data was contiguous and not yet copied to m_Data.
        NdisMoveMemory(tapPacket->m_Data,packetData,packetLength);
    }
    
    DUMP_PACKET ("AdapterTransmit", tapPacket->m_Data, packetLength);

    //=====================================================
    // If IPv4 packet, check whether or not packet
    // was truncated.
    //=====================================================
#if PACKET_TRUNCATION_CHECK
    IPv4PacketSizeVerify (tapPacket->m_Data, packetLength, FALSE, "TX", &Adapter->m_TxTrunc);
#endif

/*
    //=====================================================
    // Are we running in DHCP server masquerade mode?
    //
    // If so, catch both DHCP requests and ARP queries
    // to resolve the address of our virtual DHCP server.
    //=====================================================
    if (Adapter->m_dhcp_enabled)
    {
        const ETH_HEADER *eth = (ETH_HEADER *) tapPacket->m_Data;
        const IPHDR *ip = (IPHDR *) (tapPacket->m_Data + sizeof (ETH_HEADER));
        const UDPHDR *udp = (UDPHDR *) (tapPacket->m_Data + sizeof (ETH_HEADER) + sizeof (IPHDR));

        // ARP packet?
        if (packetLength == sizeof (ARP_PACKET)
            && eth->proto == htons (ETH_P_ARP)
            && Adapter->m_dhcp_server_arp
            )
        {
            if (ProcessARP (Adapter,
                (PARP_PACKET) tapPacket->m_Data,
                Adapter->m_dhcp_addr,
                Adapter->m_dhcp_server_ip,
                ~0,
                Adapter->m_dhcp_server_mac))
                goto no_queue;
        }

        // DHCP packet?
        else if (packetLength >= sizeof (ETH_HEADER) + sizeof (IPHDR) + sizeof (UDPHDR) + sizeof (DHCP)
            && eth->proto == htons (ETH_P_IP)
            && ip->version_len == 0x45 // IPv4, 20 byte header
            && ip->protocol == IPPROTO_UDP
            && udp->dest == htons (BOOTPS_PORT)
            )
        {
            const DHCP *dhcp = (DHCP *) (tapPacket->m_Data
                + sizeof (ETH_HEADER)
                + sizeof (IPHDR)
                + sizeof (UDPHDR));

            const int optlen = packetLength
                - sizeof (ETH_HEADER)
                - sizeof (IPHDR)
                - sizeof (UDPHDR)
                - sizeof (DHCP);

            if (optlen > 0) // we must have at least one DHCP option
            {
                if (ProcessDHCP (Adapter, eth, ip, udp, dhcp, optlen))
                    goto no_queue;
            }
            else
                goto no_queue;
        }
    }

    //===============================================
    // In Point-To-Point mode, check to see whether
    // packet is ARP (handled) or IPv4 (sent to app).
    // IPv6 packets are inspected for neighbour discovery
    // (to be handled locally), and the rest is forwarded
    // all other protocols are dropped
    //===============================================
    if (Adapter->m_tun)
    {
        ETH_HEADER *e;

        e = (ETH_HEADER *) tapPacket->m_Data;

        switch (ntohs (e->proto))
        {
        case ETH_P_ARP:

            // Make sure that packet is the right size for ARP.
            if (packetLength != sizeof (ARP_PACKET))
                goto no_queue;

            ProcessARP (
                Adapter,
                (PARP_PACKET) tapPacket->m_Data,
                Adapter->m_localIP,
                Adapter->m_remoteNetwork,
                Adapter->m_remoteNetmask,
                Adapter->m_TapToUser.dest
                );

        default:
            goto no_queue;

        case ETH_P_IP:

            // Make sure that packet is large enough to be IPv4.
            if (packetLength < (ETHERNET_HEADER_SIZE + IP_HEADER_SIZE))
            {
                goto no_queue;
            }

            // Only accept directed packets, not broadcasts.
            if (memcmp (e, &Adapter->m_TapToUser, ETHERNET_HEADER_SIZE))
            {
                goto no_queue;
            }

            // Packet looks like IPv4, queue it. :-)
            tapPacket->m_SizeFlags |= TP_TUN;
            break;

        case ETH_P_IPV6:
            // Make sure that packet is large enough to be IPv6.
            if (packetLength < (ETHERNET_HEADER_SIZE + IPV6_HEADER_SIZE))
            {
                goto no_queue;
            }

            // Broadcasts and multicasts are handled specially
            // (to be implemented)

            // Neighbor discovery packets to fe80::8 are special
            // OpenVPN sets this next-hop to signal "handled by tapdrv"
            if ( HandleIPv6NeighborDiscovery(Adapter,tapPacket->m_Data) )
            {
                goto no_queue;
            }

            // Packet looks like IPv6, queue it. :-)
            tapPacket->m_SizeFlags |= TP_TUN;
        }
    }
*/

    //===============================================
    // Push packet onto queue to wait for read from
    // userspace.
    //===============================================
    if(tapAdapterReadAndWriteReady(Adapter))
    {
        tapPacketQueueInsertTail(&Adapter->SendPacketQueue,tapPacket);
    }
    else
    {
        //
        // Tragedy. All this work and the packet is of no use... 
        //
        NdisFreeMemory(tapPacket,0,0);
    }

    // Return after queuing or freeing TAP packet.
    return;

    // Free TAP packet without queuing.
no_queue:
    if(tapPacket != NULL )
    {
        NdisFreeMemory(tapPacket,0,0);
    }
  
exit_success:
    return;
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
    NDIS_STATUS             status;
    PTAP_ADAPTER_CONTEXT    adapter = (PTAP_ADAPTER_CONTEXT )MiniportAdapterContext;
    BOOLEAN                 DispatchLevel = (SendFlags & NDIS_SEND_FLAGS_DISPATCH_LEVEL);
    PNET_BUFFER_LIST        currentNbl;
    BOOLEAN                 validNbLengths;

    UNREFERENCED_PARAMETER(NetBufferLists);
    UNREFERENCED_PARAMETER(PortNumber);
    UNREFERENCED_PARAMETER(SendFlags);

    ASSERT(PortNumber == 0); // Only the default port is supported

    //
    // Can't process sends if TAP device is not open.
    // ----------------------------------------------
    // Just perform a "lying send" and return packets as if they
    // were successfully sent.
    //
    if(adapter->TapFileObject == NULL)
    {
        //
        // Complete all NBLs and return if adapter not ready.
        //
        tapSendNetBufferListsComplete(
            adapter,
            NetBufferLists,
            NDIS_STATUS_SUCCESS,
            DispatchLevel
            );

        return;
    }

    //
    // Check Adapter send/receive ready state.
    //
    status = tapAdapterSendAndReceiveReady(adapter);

    if(status != NDIS_STATUS_SUCCESS)
    {
        //
        // Complete all NBLs and return if adapter not ready.
        //
        tapSendNetBufferListsComplete(
            adapter,
            NetBufferLists,
            status,
            DispatchLevel
            );

        return;
    }

    //
    // Scan all NBLs and linked packets for valid lengths.
    // ---------------------------------------------------
    // If _ANY_ NB length is invalid, then fail the entire send operation.
    //
    //    BUGBUG!!! Perhaps this should be less agressive. Fail only individual
    //    NBLs...
    //    
    // If length check is valid, then TAP_PACKETS can be safely allocated
    // and processed for all NBs being sent.
    //
    validNbLengths = tapNetBufferListNetBufferLengthsValid(
                        adapter,
                        NetBufferLists
                        );

    if(!validNbLengths)
    {
        //
        // Complete all NBLs and return if and NB length is invalid.
        //
        tapSendNetBufferListsComplete(
            adapter,
            NetBufferLists,
            NDIS_STATUS_INVALID_LENGTH,
            DispatchLevel
            );

        return;
    }

    //
    // Process each NBL individually
    //
    currentNbl = NetBufferLists;

    while (currentNbl)
    {
        PNET_BUFFER_LIST    nextNbl;
        PNET_BUFFER         currentNb;

        // Locate next NBL
        nextNbl = NET_BUFFER_LIST_NEXT_NBL(currentNbl);

        // Locate first NB (aka "packet")
        currentNb = NET_BUFFER_LIST_FIRST_NB(currentNbl);

        // Transmit all NBs linked to this NBL
        while(currentNb)
        {
            PNET_BUFFER nextNb;

            // Locate next NB
            nextNb = NET_BUFFER_NEXT_NB(currentNb);

            // Transmit the NB
            tapAdapterTransmit(adapter,currentNb,DispatchLevel);

            // Move to next NB
            currentNb = nextNb;
        }

        // Move to next NBL
        currentNbl = nextNbl;
    }

    // Complete all NBLs
    tapSendNetBufferListsComplete(
        adapter,
        NetBufferLists,
        NDIS_STATUS_SUCCESS,
        DispatchLevel
        );
}

VOID
AdapterCancelSend(
    __in  NDIS_HANDLE             MiniportAdapterContext,
    __in  PVOID                   CancelId
    )
{
    PTAP_ADAPTER_CONTEXT   adapter = (PTAP_ADAPTER_CONTEXT )MiniportAdapterContext;

    //
    // This miniport completes its sends quickly, so it isn't strictly
    // neccessary to implement MiniportCancelSend.
    //
    // If we did implement it, we'd have to walk the Adapter->SendWaitList
    // and look for any NB that points to a NBL where the CancelId matches
    // NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(Nbl).  For any NB that so matches,
    // we'd remove the NB from the SendWaitList and set the NBL's status to
    // NDIS_STATUS_SEND_ABORTED, then complete the NBL.
    //
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

    //DEBUGP (("[TAP] --> AdapterCheckForHangEx\n"));

    //DEBUGP (("[TAP] <-- AdapterCheckForHangEx; status = FALSE\n"));

    return FALSE;   // Everything is fine
}

NDIS_STATUS
AdapterReset(
    __in   NDIS_HANDLE            MiniportAdapterContext,
    __out PBOOLEAN                AddressingReset
    )
/*++

Routine Description:

    MiniportResetEx is a required to issue a hardware reset to the NIC
    and/or to reset the driver's software state.

    1) The miniport driver can optionally complete any pending
        OID requests. NDIS will submit no further OID requests
        to the miniport driver for the NIC being reset until
        the reset operation has finished. After the reset,
        NDIS will resubmit to the miniport driver any OID requests
        that were pending but not completed by the miniport driver
        before the reset.

    2) A deserialized miniport driver must complete any pending send
        operations. NDIS will not requeue pending send packets for
        a deserialized driver since NDIS does not maintain the send
        queue for such a driver.

    3) If MiniportReset returns NDIS_STATUS_PENDING, the driver must
        complete the original request subsequently with a call to
        NdisMResetComplete.

    MiniportReset runs at IRQL <= DISPATCH_LEVEL.

Arguments:

AddressingReset - If multicast or functional addressing information
                  or the lookahead size, is changed by a reset,
                  MiniportReset must set the variable at AddressingReset
                  to TRUE before it returns control. This causes NDIS to
                  call the MiniportSetInformation function to restore
                  the information.

MiniportAdapterContext - Pointer to our adapter

Return Value:

    NDIS_STATUS

--*/
{
    PTAP_ADAPTER_CONTEXT   adapter = (PTAP_ADAPTER_CONTEXT )MiniportAdapterContext;
    NDIS_STATUS    status;

    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(AddressingReset);

    DEBUGP (("[TAP] --> AdapterReset\n"));

    // Indicate that adapter reset is in progress.
    adapter->ResetInProgress = TRUE;

    // See note above...
    *AddressingReset = FALSE;

    // BUGBUG!!! TODO!!! Lots of work here...

    // Indicate that adapter reset has completed.
    adapter->ResetInProgress = FALSE;

    status = NDIS_STATUS_SUCCESS;

    DEBUGP (("[TAP] <-- AdapterReset; status = %8.8X\n",status));

    return status;
}

VOID
AdapterDevicePnpEventNotify(
    __in  NDIS_HANDLE             MiniportAdapterContext,
    __in  PNET_DEVICE_PNP_EVENT   NetDevicePnPEvent
    )
{
    PTAP_ADAPTER_CONTEXT   adapter = (PTAP_ADAPTER_CONTEXT )MiniportAdapterContext;

    DEBUGP (("[TAP] --> AdapterDevicePnpEventNotify\n"));

/*
    switch (NetDevicePnPEvent->DevicePnPEvent)
    {
        case NdisDevicePnPEventSurpriseRemoved:
            //
            // Called when NDIS receives IRP_MN_SUPRISE_REMOVAL.
            // NDIS calls MiniportHalt function after this call returns.
            //
            MP_SET_FLAG(Adapter, fMP_ADAPTER_SURPRISE_REMOVED);
            DEBUGP(MP_INFO, "[%p] MPDevicePnpEventNotify: NdisDevicePnPEventSurpriseRemoved\n", Adapter);
            break;

        case NdisDevicePnPEventPowerProfileChanged:
            //
            // After initializing a miniport driver and after miniport driver
            // receives an OID_PNP_SET_POWER notification that specifies
            // a device power state of NdisDeviceStateD0 (the powered-on state),
            // NDIS calls the miniport's MiniportPnPEventNotify function with
            // PnPEvent set to NdisDevicePnPEventPowerProfileChanged.
            //
            DEBUGP(MP_INFO, "[%p] MPDevicePnpEventNotify: NdisDevicePnPEventPowerProfileChanged\n", Adapter);

            if (NetDevicePnPEvent->InformationBufferLength == sizeof(ULONG))
            {
                ULONG NdisPowerProfile = *((PULONG)NetDevicePnPEvent->InformationBuffer);

                if (NdisPowerProfile == NdisPowerProfileBattery)
                {
                    DEBUGP(MP_INFO, "[%p] The host system is running on battery power\n", Adapter);
                }
                if (NdisPowerProfile == NdisPowerProfileAcOnLine)
                {
                    DEBUGP(MP_INFO, "[%p] The host system is running on AC power\n", Adapter);
                }
            }
            break;

        default:
            DEBUGP(MP_ERROR, "[%p] MPDevicePnpEventNotify: unknown PnP event 0x%x\n", Adapter, NetDevicePnPEvent->DevicePnPEvent);
    }
*/
    DEBUGP (("[TAP] <-- AdapterDevicePnpEventNotify\n"));
}

VOID
AdapterShutdownEx(
    __in  NDIS_HANDLE             MiniportAdapterContext,
    __in  NDIS_SHUTDOWN_ACTION    ShutdownAction
    )
/*++

Routine Description:

    The MiniportShutdownEx handler restores hardware to its initial state when
    the system is shut down, whether by the user or because an unrecoverable
    system error occurred. This is to ensure that the NIC is in a known
    state and ready to be reinitialized when the machine is rebooted after
    a system shutdown occurs for any reason, including a crash dump.

    Here just disable the interrupt and stop the DMA engine.  Do not free
    memory resources or wait for any packet transfers to complete.  Do not call
    into NDIS at this time.

    This can be called at aribitrary IRQL, including in the context of a
    bugcheck.

Arguments:

    MiniportAdapterContext  Pointer to our adapter
    ShutdownAction  The reason why NDIS called the shutdown function

Return Value:

    None.

--*/
{
    PTAP_ADAPTER_CONTEXT   adapter = (PTAP_ADAPTER_CONTEXT )MiniportAdapterContext;

    UNREFERENCED_PARAMETER(ShutdownAction);
    UNREFERENCED_PARAMETER(MiniportAdapterContext);

    DEBUGP (("[TAP] --> AdapterShutdownEx\n"));

    // Enter the Shutdown state.
    DEBUGP (("[TAP] Miniport State: Shutdown\n"));

    tapAdapterAcquireLock(adapter,FALSE);
    adapter->Locked.AdapterState = MiniportShutdownState;
    tapAdapterReleaseLock(adapter,FALSE);

    //
    // BUGBUG!!! FlushIrpQueues???
    //

    DEBUGP (("[TAP] <-- AdapterShutdownEx\n"));
}


// Free adapter context memory and associated resources.
VOID
tapAdapterContextFree(
    IN PTAP_ADAPTER_CONTEXT     Adapter
    )
{
    PLIST_ENTRY listEntry = &Adapter->AdapterListLink;

    DEBUGP (("[TAP] --> tapAdapterContextFree\n"));

    // Adapter context should already be removed.
    ASSERT( (listEntry->Flink == listEntry) && (listEntry->Blink == listEntry ) );

    // Insure that adapter context has been removed from global adapter list.
    RemoveEntryList(&Adapter->AdapterListLink);

    // Free the adapter lock.
    NdisFreeSpinLock(&Adapter->AdapterLock);

    // Free the ANSI NetCfgInstanceId buffer.
    if(Adapter->NetCfgInstanceIdAnsi.Buffer != NULL)
    {
        RtlFreeAnsiString(&Adapter->NetCfgInstanceIdAnsi);
    }

    Adapter->NetCfgInstanceIdAnsi.Buffer = NULL;

    NdisFreeMemory(Adapter,0,0);

    DEBUGP (("[TAP] <-- tapAdapterContextFree\n"));
}

