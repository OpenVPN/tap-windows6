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

#include "tap.h"

//======================================================================
// TAP Receive Path Support
//======================================================================

#ifdef ALLOC_PRAGMA
#pragma alloc_text( PAGE, TapDeviceWrite)
#endif // ALLOC_PRAGMA

//===============================================================
// Used in cases where internally generated packets such as
// ARP or DHCP replies must be returned to the kernel, to be
// seen as an incoming packet "arriving" on the interface.
//===============================================================

VOID
IndicateReceivePacket(
    __in PTAP_ADAPTER_CONTEXT  Adapter,
    __in PUCHAR packetData,
    __in const unsigned int packetLength
    )
{
    PUCHAR  injectBuffer;
    unsigned int paddedPacketLength = packetLength;
    if(paddedPacketLength < TAP_MIN_FRAME_SIZE)
    {
        paddedPacketLength = TAP_MIN_FRAME_SIZE;
    }

    //
    // Handle miniport Pause
    // ---------------------
    // NDIS 6 miniports implement a temporary "Pause" state normally followed
    // by the Restart. While in the Pause state it is forbidden for the miniport
    // to indicate receive NBLs.
    //
    // That is: The device interface may be "up", but the NDIS miniport send/receive
    // interface may be temporarily "down".
    //
    // BUGBUG!!! In the initial implementation of the NDIS 6 TapOas inject path
    // the code below will simply ignore inject packets passed to the driver while
    // the miniport is in the Paused state.
    //
    // The correct implementation is to go ahead and build the NBLs corresponding
    // to the inject packet - but queue them. When Restart is entered the
    // queued NBLs would be dequeued and indicated to the host.
    //
    if(tapAdapterSendAndReceiveReady(Adapter) != NDIS_STATUS_SUCCESS)
    {
        DEBUGP (("[%s] Lying send in IndicateReceivePacket while adapter paused\n",
            MINIPORT_INSTANCE_ID (Adapter)));

        return;
    }

    // Allocate flat buffer for packet data.
    injectBuffer = (PUCHAR )NdisAllocateMemoryWithTagPriority(
                        Adapter->MiniportAdapterHandle,
                        paddedPacketLength,
                        TAP_RX_INJECT_BUFFER_TAG,
                        NormalPoolPriority
                        );

    if( injectBuffer)
    {
        PMDL    mdl;

        // Copy packet data to flat buffer.
        NdisMoveMemory (injectBuffer, packetData, packetLength);
        if(packetLength < paddedPacketLength)
        {
            NdisZeroMemory(injectBuffer + packetLength, paddedPacketLength - packetLength);
        }

        // Allocate MDL for flat buffer.
        mdl = NdisAllocateMdl(
                Adapter->MiniportAdapterHandle,
                injectBuffer,
                paddedPacketLength
                );

        if( mdl )
        {
            PNET_BUFFER_LIST    netBufferList;

            mdl->Next = NULL;   // No next MDL

            // Allocate the NBL and NB. Link MDL chain to NB.
            netBufferList = NdisAllocateNetBufferAndNetBufferList(
                                Adapter->ReceiveNblPool,
                                0,                  // ContextSize
                                0,                  // ContextBackFill
                                mdl,                // MDL chain
                                0,
                                paddedPacketLength
                                );

            if(netBufferList != NULL)
            {
                ULONG       receiveFlags = 0;
                LONG        nblCount;

                NET_BUFFER_LIST_NEXT_NBL(netBufferList) = NULL; // Only one NBL

                if(KeGetCurrentIrql() == DISPATCH_LEVEL)
                {
                    receiveFlags |= NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL;
                }

                // Set flag indicating that this is an injected packet
                TAP_RX_NBL_FLAGS_CLEAR_ALL(netBufferList);
                TAP_RX_NBL_FLAG_SET(netBufferList,TAP_RX_NBL_FLAGS_IS_INJECTED);

                netBufferList->MiniportReserved[0] = NULL;
                netBufferList->MiniportReserved[1] = NULL;

                // Increment in-flight receive NBL count.
                nblCount = NdisInterlockedIncrement(&Adapter->ReceiveNblInFlightCount);
                ASSERT(nblCount > 0 );

                netBufferList->SourceHandle = Adapter->MiniportAdapterHandle;

                //
                // Indicate the packet
                // -------------------
                // Irp->AssociatedIrp.SystemBuffer with length irpSp->Parameters.Write.Length
                // contains the complete packet including Ethernet header and payload.
                //
                NdisMIndicateReceiveNetBufferLists(
                    Adapter->MiniportAdapterHandle,
                    netBufferList,
                    NDIS_DEFAULT_PORT_NUMBER,
                    1,      // NumberOfNetBufferLists
                    receiveFlags
                    );

                return;
            }
            else
            {
                DEBUGP (("[%s] NdisAllocateNetBufferAndNetBufferList failed in IndicateReceivePacket\n",
                    MINIPORT_INSTANCE_ID (Adapter)));
                NOTE_ERROR ();

                NdisFreeMdl(mdl);
                NdisFreeMemory(injectBuffer,0,0);
            }
        }
        else
        {
            DEBUGP (("[%s] NdisAllocateMdl failed in IndicateReceivePacket\n",
                MINIPORT_INSTANCE_ID (Adapter)));
            NOTE_ERROR ();

            NdisFreeMemory(injectBuffer,0,0);
        }
    }
    else
    {
        DEBUGP (("[%s] NdisAllocateMemoryWithTagPriority failed in IndicateReceivePacket\n",
            MINIPORT_INSTANCE_ID (Adapter)));
        NOTE_ERROR ();
    }
}

VOID
tapCompleteIrpAndFreeReceiveNetBufferList(
    __in  PTAP_ADAPTER_CONTEXT  Adapter,
    __in  PNET_BUFFER_LIST      NetBufferList,  // Only one NB here...
    __in  NTSTATUS              IoCompletionStatus
    )
{
    PIRP    irp;
    ULONG   frameType, netBufferCount, byteCount;
    LONG    nblCount;

    // Fetch NB frame type.
    frameType = tapGetNetBufferFrameType(NET_BUFFER_LIST_FIRST_NB(NetBufferList));

    // Fetch statistics for all NBs linked to the NB.
    netBufferCount = tapGetNetBufferCountsFromNetBufferList(
                        NetBufferList,
                        &byteCount
                        );

    // Update statistics by frame type
    if(IoCompletionStatus == STATUS_SUCCESS)
    {
        switch(frameType)
        {
        case NDIS_PACKET_TYPE_DIRECTED:
            Adapter->FramesRxDirected += netBufferCount;
            Adapter->BytesRxDirected += byteCount;
            break;

        case NDIS_PACKET_TYPE_BROADCAST:
            Adapter->FramesRxBroadcast += netBufferCount;
            Adapter->BytesRxBroadcast += byteCount;
            break;

        case NDIS_PACKET_TYPE_MULTICAST:
            Adapter->FramesRxMulticast += netBufferCount;
            Adapter->BytesRxMulticast += byteCount;
            break;

        default:
            ASSERT(FALSE);
            break;
        }
    }

    //
    // Handle P2P Packet
    // -----------------
    // Free MDL allocated for P2P Ethernet header.
    //
    if(TAP_RX_NBL_FLAG_TEST(NetBufferList,TAP_RX_NBL_FLAGS_IS_P2P))
    {
        PNET_BUFFER     netBuffer;
        PMDL            mdl;

        netBuffer = NET_BUFFER_LIST_FIRST_NB(NetBufferList);
        mdl = NET_BUFFER_FIRST_MDL(netBuffer);
        mdl->Next = NULL;

        NdisFreeMdl(mdl);
    }

    //
    // Handle Injected Packet
    // -----------------------
    // Free MDL and data buffer allocated for injected packet.
    //
    if(TAP_RX_NBL_FLAG_TEST(NetBufferList,TAP_RX_NBL_FLAGS_IS_INJECTED))
    {
        ULONG           pagePriority;
        PNET_BUFFER     netBuffer;
        PMDL            mdl;
        PUCHAR          injectBuffer;

        netBuffer = NET_BUFFER_LIST_FIRST_NB(NetBufferList);
        mdl = NET_BUFFER_FIRST_MDL(netBuffer);

        //
        // On Windows versions 8 and above, the MDL can be marked as not executable.
        // This is required for the driver to function under HyperVisor-enforced
        // Code Integrity (HVCI).
        //

        pagePriority = NormalPagePriority;

        if (GlobalData.RunningWindows8OrGreater != FALSE) {
            pagePriority |= MdlMappingNoExecute;
        }

        injectBuffer = (PUCHAR )MmGetSystemAddressForMdlSafe(mdl,pagePriority);

        if(injectBuffer)
        {
            NdisFreeMemory(injectBuffer,0,0);
        }

        NdisFreeMdl(mdl);
    }

    //
    // Complete the IRP
    //
    irp = (PIRP )NetBufferList->MiniportReserved[0];

    if(irp)
    {
        irp->IoStatus.Status = IoCompletionStatus;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
    }

    // Decrement in-flight receive NBL count.
    nblCount = NdisInterlockedDecrement(&Adapter->ReceiveNblInFlightCount);
    ASSERT(nblCount >= 0 );
    if (0 == nblCount)
    {
        NdisSetEvent(&Adapter->ReceiveNblInFlightCountZeroEvent);
    }

    // Free the NBL
    NdisFreeNetBufferList(NetBufferList);
}

VOID
AdapterReturnNetBufferLists(
    __in  NDIS_HANDLE             MiniportAdapterContext,
    __in  PNET_BUFFER_LIST        NetBufferLists,
    __in  ULONG                   ReturnFlags
    )
{
    PTAP_ADAPTER_CONTEXT    adapter = (PTAP_ADAPTER_CONTEXT )MiniportAdapterContext;
    PNET_BUFFER_LIST        currentNbl;

    UNREFERENCED_PARAMETER(ReturnFlags);

    //
    // Process each NBL individually
    //
    currentNbl = NetBufferLists;
    while (currentNbl)
    {
        PNET_BUFFER_LIST    nextNbl;

        nextNbl = NET_BUFFER_LIST_NEXT_NBL(currentNbl);
        NET_BUFFER_LIST_NEXT_NBL(currentNbl) = NULL;

        // Complete write IRP and free NBL and associated resources.
        tapCompleteIrpAndFreeReceiveNetBufferList(
            adapter,
            currentNbl,
            STATUS_SUCCESS
            );

        // Move to next NBL
        currentNbl = nextNbl;
    }
}

static PVOID 
TapStrip8021Q(
    __inout unsigned char ** PacketBuffer,
    __inout ULONG *PacketLength
    )
{
    NDIS_NET_BUFFER_LIST_8021Q_INFO priorityInfo;
    unsigned char* buffer = *PacketBuffer;
    ULONG length = *PacketLength;
    priorityInfo.Value = 0;

    if(length >= ETHERNET_HEADER_SIZE + VLAN_TAG_SIZE)
    {
        PETH_HEADER frameHeader = (PETH_HEADER) buffer;

        if(frameHeader->proto == htons(ETHERTYPE_8021Q))
        {
            // Decode 802.1Q header
            PETH_8021Q_HEADER tag = (PETH_8021Q_HEADER)(frameHeader+1);

            priorityInfo.TagHeader.UserPriority = (tag->Tag>>13);
            priorityInfo.TagHeader.VlanId = (tag->Tag & 0x0FFF);

            // Copy the first part of the ethernet header up and over the protocol and 802.1Q data
            // Don't copy the ethernet header's protocol, leave the inner value from the 802.1Q header.
            RtlMoveMemory(buffer+4, buffer, ETHERNET_HEADER_SIZE-2);

            // Update pointer/length to reflect this change.
            *PacketBuffer = buffer+4;
            *PacketLength = length-4;
        }

    }
    return priorityInfo.Value;
}

static NTSTATUS
TapSharedSendPacket(
    __in PTAP_ADAPTER_CONTEXT Adapter,
    __in PIRP Irp,
    __in unsigned char * PacketBuffer,
    __in ULONG PacketLength,
    __in_opt PVOID PacketPriority,
    __in_opt const PUCHAR PrefixData,
    __in const unsigned int PrefixLength
    )
{
    PIO_STACK_LOCATION      irpSp;
    unsigned int            fullLength;
    PNET_BUFFER_LIST        netBufferList = NULL;
    PMDL                    mdl = NULL;    // Head of MDL chain.
    LONG                    nblCount;


    irpSp = IoGetCurrentIrpStackLocation( Irp );
    fullLength = PacketLength + PrefixLength;

    if(fullLength < TAP_MIN_FRAME_SIZE)
    {
        // Consolidate all the incoming data into a new single minimum-length allocation.
        // This is simpler than additionally allocating another tiny MDL to tack on to the end
        // (and then having to remove it on the cleanup path)
        PUCHAR          allocBuffer = NULL;
        unsigned int    paddedLength = TAP_MIN_FRAME_SIZE;

        // Allocate flat buffer for packet data.
        allocBuffer = (PUCHAR )NdisAllocateMemoryWithTagPriority(
                            Adapter->MiniportAdapterHandle,
                            paddedLength,
                            TAP_RX_INJECT_BUFFER_TAG,
                            NormalPoolPriority
                            );

        if(allocBuffer == NULL)
        {
            DEBUGP (("[%s] NdisAllocateMemoryWithTagPriority failed in IRP_MJ_WRITE\n",
                MINIPORT_INSTANCE_ID (Adapter)));
            NOTE_ERROR ();

            // Fail the IRP
            Irp->IoStatus.Information = 0;
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        // Copy packet data to flat buffer.
        if(PrefixLength > 0)
        {
            NdisMoveMemory(allocBuffer, PrefixData, PrefixLength);
        }
        NdisMoveMemory (allocBuffer + PrefixLength, PacketBuffer, PacketLength);
        NdisZeroMemory(allocBuffer + fullLength, paddedLength - fullLength);

        // Allocate MDL for flat buffer.
        mdl = NdisAllocateMdl(
                Adapter->MiniportAdapterHandle,
                allocBuffer,
                paddedLength
                );

        if(mdl == NULL)
        {
            DEBUGP (("[%s] NdisAllocateMdl failed in IRP_MJ_WRITE\n",
                MINIPORT_INSTANCE_ID (Adapter)));
            NOTE_ERROR ();

            NdisFreeMemory(allocBuffer,0,0);
           
            // Fail the IRP
            Irp->IoStatus.Information = 0;
            return STATUS_INSUFFICIENT_RESOURCES; 
        }

        mdl->Next = NULL;   // No next MDL

        // Allocate the NBL and NB. Link MDL chain to NB.
        netBufferList = NdisAllocateNetBufferAndNetBufferList(
                            Adapter->ReceiveNblPool,
                            0,                  // ContextSize
                            0,                  // ContextBackFill
                            mdl,                // MDL chain
                            0,
                            paddedLength
                            );

        if(netBufferList == NULL)
        {
            DEBUGP (("[%s] NdisAllocateNetBufferAndNetBufferList failed in IRP_MJ_WRITE\n",
                MINIPORT_INSTANCE_ID (Adapter)));
            NOTE_ERROR ();

            NdisFreeMdl(mdl);
            NdisFreeMemory(allocBuffer,0,0);
           
            // Fail the IRP
            Irp->IoStatus.Information = 0;
            return STATUS_INSUFFICIENT_RESOURCES; 
        }            

        // Set flag indicating that this is an injected packet
        // In particular, it has the same cleanup path, and that is all the flag is used for currently.
        TAP_RX_NBL_FLAGS_CLEAR_ALL(netBufferList);
        TAP_RX_NBL_FLAG_SET(netBufferList,TAP_RX_NBL_FLAGS_IS_INJECTED);
    }
    else
    {       
        if(PrefixLength > 0)
        {
            //
            // Allocate MDL for Ethernet header
            // --------------------------------
            // Irp->AssociatedIrp.SystemBuffer with length irpSp->Parameters.Write.Length
            // contains the only the Ethernet payload. Prepend the user-mode provided
            // payload with the Ethernet header pointed to by p_UserToTap.
            //
            mdl = NdisAllocateMdl(
                Adapter->MiniportAdapterHandle,
                PrefixData,
                PrefixLength
                );

            if(mdl == NULL)            
            {
                DEBUGP (("[%s] NdisAllocateMdl failed in IRP_MJ_WRITE\n",
                    MINIPORT_INSTANCE_ID (Adapter)));
                NOTE_ERROR ();

                // Fail the IRP
                Irp->IoStatus.Information = 0;
                return STATUS_INSUFFICIENT_RESOURCES;
            }


            // Chain user's Ethernet payload behind Ethernet header.
            mdl->Next = Irp->MdlAddress;
            (Irp->MdlAddress)->Next = NULL; // No next MDL
        }

        // Allocate the NBL and NB. Link MDL chain to NB.
        netBufferList = NdisAllocateNetBufferAndNetBufferList(
            Adapter->ReceiveNblPool,
            0,                              // ContextSize
            0,                              // ContextBackFill
            mdl==NULL?Irp->MdlAddress:mdl,  // MDL chain
            // PacketBuffer will always be from the Irp's SystemBuffer, but may be offset beyond the start.
            // This will only be the case if there is not a prefix (and mdl == NULL).
            mdl==NULL?(ULONG)(PacketBuffer-((unsigned char *)Irp->AssociatedIrp.SystemBuffer)):0,
            fullLength
            );

        if(netBufferList == NULL)
        {
            if(mdl != NULL)
            {
                mdl->Next = NULL;
                NdisFreeMdl(mdl);
            }

            DEBUGP (("[%s] NdisAllocateNetBufferAndNetBufferList failed in IRP_MJ_WRITE\n",
                MINIPORT_INSTANCE_ID (Adapter)));
            NOTE_ERROR ();

            // Fail the IRP
            Irp->IoStatus.Information = 0;
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        TAP_RX_NBL_FLAGS_CLEAR_ALL(netBufferList);
        if(PrefixLength > 0)
        {
            TAP_RX_NBL_FLAG_SET(netBufferList,TAP_RX_NBL_FLAGS_IS_P2P);
        }
    }

    NET_BUFFER_LIST_NEXT_NBL(netBufferList) = NULL; // Only one NBL

    // This IRP is pended.
    IoMarkIrpPending(Irp);

    // This IRP cannot be cancelled while in-flight.
    IoSetCancelRoutine(Irp,NULL);

    // Stash IRP pointer in NBL MiniportReserved[0] field.
    netBufferList->MiniportReserved[0] = Irp;
    netBufferList->MiniportReserved[1] = NULL;

    NET_BUFFER_LIST_INFO(netBufferList, Ieee8021QNetBufferListInfo) = PacketPriority;

    // Increment in-flight receive NBL count.
    nblCount = NdisInterlockedIncrement(&Adapter->ReceiveNblInFlightCount);
    ASSERT(nblCount > 0 );

    //
    // Indicate the packet
    // -------------------
    // This NBL contains the complete packet including Ethernet header and payload.
    //
    NdisMIndicateReceiveNetBufferLists(
        Adapter->MiniportAdapterHandle,
        netBufferList,
        NDIS_DEFAULT_PORT_NUMBER,
        1,      // NumberOfNetBufferLists
        0       // ReceiveFlags
        );

    return STATUS_PENDING;
}

// IRP_MJ_WRITE callback.
NTSTATUS
TapDeviceWrite(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp
    )
{
    NTSTATUS                ntStatus = STATUS_SUCCESS;// Assume success
    PIO_STACK_LOCATION      irpSp;// Pointer to current stack location
    PTAP_ADAPTER_CONTEXT    adapter = NULL;
    ULONG                   dataLength;

    PAGED_CODE();

    irpSp = IoGetCurrentIrpStackLocation( Irp );

    //
    // Fetch adapter context for this device.
    // --------------------------------------
    // Adapter pointer was stashed in FsContext when handle was opened.
    //
    adapter = (PTAP_ADAPTER_CONTEXT )(irpSp->FileObject)->FsContext;

    ASSERT(adapter);

    //
    // Sanity checks on state variables
    //
    if (!tapAdapterReadAndWriteReady(adapter))
    {
        //DEBUGP (("[%s] Interface is down in IRP_MJ_WRITE\n",
        //    MINIPORT_INSTANCE_ID (adapter)));
        //NOTE_ERROR();

        Irp->IoStatus.Status = ntStatus = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest (Irp, IO_NO_INCREMENT);

        return ntStatus;
    }

    // Save IRP-accessible copy of buffer length
    Irp->IoStatus.Information = irpSp->Parameters.Write.Length;

    if (Irp->MdlAddress == NULL)
    {
        DEBUGP (("[%s] MdlAddress is NULL for IRP_MJ_WRITE\n",
            MINIPORT_INSTANCE_ID (adapter)));

        NOTE_ERROR();
        Irp->IoStatus.Status = ntStatus = STATUS_INVALID_PARAMETER;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest (Irp, IO_NO_INCREMENT);

        return ntStatus;
    }

    //
    // Try to get a virtual address for the MDL.
    //
    NdisQueryMdl(
        Irp->MdlAddress,
        &Irp->AssociatedIrp.SystemBuffer,
        &dataLength,
        NormalPagePriority
        );

    if (Irp->AssociatedIrp.SystemBuffer == NULL)
    {
        DEBUGP (("[%s] Could not map address in IRP_MJ_WRITE\n",
            MINIPORT_INSTANCE_ID (adapter)));

        NOTE_ERROR();
        Irp->IoStatus.Status = ntStatus = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest (Irp, IO_NO_INCREMENT);

        return ntStatus;
    }

    ASSERT(dataLength == irpSp->Parameters.Write.Length);

    Irp->IoStatus.Information = irpSp->Parameters.Write.Length;

    //
    // Handle miniport Pause
    // ---------------------
    // NDIS 6 miniports implement a temporary "Pause" state normally followed
    // by the Restart. While in the Pause state it is forbidden for the miniport
    // to indicate receive NBLs.
    //
    // That is: The device interface may be "up", but the NDIS miniport send/receive
    // interface may be temporarily "down".
    //
    // BUGBUG!!! In the initial implementation of the NDIS 6 TapOas receive path
    // the code below will perform a "lying send" for write IRPs passed to the
    // driver while the miniport is in the Paused state.
    //
    // The correct implementation is to go ahead and build the NBLs corresponding
    // to the user-mode write - but queue them. When Restart is entered the
    // queued NBLs would be dequeued and indicated to the host.
    //
    if(tapAdapterSendAndReceiveReady(adapter) == NDIS_STATUS_SUCCESS)
    {
        if (!adapter->m_tun && ((irpSp->Parameters.Write.Length) >= ETHERNET_HEADER_SIZE))
        {
            // TAP mode - Send raw ethernet frame received.
            unsigned char* packetBuffer = (unsigned char *) Irp->AssociatedIrp.SystemBuffer;
            ULONG packetLength = irpSp->Parameters.Write.Length;
            PVOID packetPriority = 0;

            DUMP_PACKET ("IRP_MJ_WRITE ETH",
                packetBuffer,
                packetLength);

            //=====================================================
            // Check incoming packet for an 802.1Q VLAN/Priority header
            // If one exists, remove it in place.
            // This may change the packet buffer pointer and length.
            //=====================================================

            packetPriority = TapStrip8021Q(&packetBuffer, &packetLength);


            //=====================================================
            // If IPv4 packet, check whether or not packet
            // was truncated.
            //=====================================================
#if PACKET_TRUNCATION_CHECK
            IPv4PacketSizeVerify (
                packetBuffer,
                packetLength,
                FALSE,
                "RX",
                &adapter->m_RxTrunc
                );
#endif
            (Irp->MdlAddress)->Next = NULL; // No next MDL

            // Determine frame type for packet filtering
            ULONG frameType = 0;

            if(!(adapter->PacketFilter & NDIS_PACKET_TYPE_PROMISCUOUS))
            {
                // Only determine the frame type if we need to check it.
                frameType = tapGetRawPacketFrameType(
                                adapter,
                                packetBuffer,
                                packetLength);
            }

            if((adapter->PacketFilter & NDIS_PACKET_TYPE_PROMISCUOUS) ||  
               (frameType & adapter->PacketFilter))
            {
                // frame type bit is enabled in the packet filter.

                ntStatus = TapSharedSendPacket(
                    adapter,
                    Irp,
                    packetBuffer,
                    packetLength,
                    packetPriority,
                    NULL,
                    0
                    );

            }
            else
            {
                DEBUGP (("[%s] Filtered send in IRP_MJ_WRITE frameType 0x%x, PacketFilter 0x%x\n",
                    MINIPORT_INSTANCE_ID (adapter), frameType, adapter->PacketFilter));

                ntStatus = STATUS_SUCCESS;
            }




        }
        else if (adapter->m_tun && ((irpSp->Parameters.Write.Length) >= IP_HEADER_SIZE))
        {
            // TUN mode - Prepend an ethernet header 
            PETH_HEADER         p_UserToTap = &adapter->m_UserToTap;

            // For IPv6, need to use Ethernet header with IPv6 proto
            if ( IPH_GET_VER( ((IPHDR*) Irp->AssociatedIrp.SystemBuffer)->version_len) == 6 )
            {
                p_UserToTap = &adapter->m_UserToTap_IPv6;
            }

            DUMP_PACKET2 ("IRP_MJ_WRITE P2P",
                p_UserToTap,
                (unsigned char *) Irp->AssociatedIrp.SystemBuffer,
                irpSp->Parameters.Write.Length);

            //=====================================================
            // If IPv4 packet, check whether or not packet
            // was truncated.
            //=====================================================
#if PACKET_TRUNCATION_CHECK
            IPv4PacketSizeVerify (
                (unsigned char *) Irp->AssociatedIrp.SystemBuffer,
                irpSp->Parameters.Write.Length,
                TRUE,
                "RX",
                &adapter->m_RxTrunc
                );
#endif

            if(adapter->PacketFilter & (NDIS_PACKET_TYPE_DIRECTED | NDIS_PACKET_TYPE_PROMISCUOUS))
            {
                // All packets are directed - only send directed packets if the packet filter enables this.

                ntStatus = TapSharedSendPacket(
                    adapter,
                    Irp,
                    (unsigned char *) Irp->AssociatedIrp.SystemBuffer,
                    irpSp->Parameters.Write.Length,
                    NULL,
                    (PUCHAR)p_UserToTap,
                    sizeof(ETH_HEADER)
                    );
            }
            else
            {
                DEBUGP (("[%s] Filtered send in IRP_MJ_WRITE while directed packets are disabled\n",
                    MINIPORT_INSTANCE_ID (adapter)));

                ntStatus = STATUS_SUCCESS;
            }
        }
        else
        {
            DEBUGP (("[%s] Bad buffer size in IRP_MJ_WRITE, len=%d\n",
                MINIPORT_INSTANCE_ID (adapter),
                irpSp->Parameters.Write.Length));
            NOTE_ERROR ();

            Irp->IoStatus.Information = 0;	// ETHERNET_HEADER_SIZE;
            Irp->IoStatus.Status = ntStatus = STATUS_BUFFER_TOO_SMALL;
        }
    }
    else
    {
        DEBUGP (("[%s] Lying send in IRP_MJ_WRITE while adapter paused\n",
            MINIPORT_INSTANCE_ID (adapter)));

        ntStatus = STATUS_SUCCESS;
    }

    if (ntStatus != STATUS_PENDING)
    {
        Irp->IoStatus.Status = ntStatus;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    return ntStatus;
}

