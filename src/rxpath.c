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
// TAP Receive Path Support
//======================================================================

#ifdef ALLOC_PRAGMA
#pragma alloc_text( PAGE, TapDeviceWrite)
#endif // ALLOC_PRAGMA

VOID
AdapterReturnNetBufferLists(
    __in  NDIS_HANDLE             MiniportAdapterContext,
    __in  PNET_BUFFER_LIST        NetBufferLists,
    __in  ULONG                   ReturnFlags
    )
{
    PTAP_ADAPTER_CONTEXT    adapter = (PTAP_ADAPTER_CONTEXT )MiniportAdapterContext;
    PNET_BUFFER_LIST        currentNbl, nextNbl;

    UNREFERENCED_PARAMETER(ReturnFlags);

    //
    // Process each NBL individually
    //
    currentNbl = NetBufferLists;
    while (currentNbl)
    {
        PNET_BUFFER_LIST    nextNbl;
        PIRP                irp;
        NTSTATUS            status;

        nextNbl = NET_BUFFER_LIST_NEXT_NBL(currentNbl);
        NET_BUFFER_LIST_NEXT_NBL(currentNbl) = NULL;

        //
        // Free MDL allocated for P2P Ethernet header if necesary.
        //
        if(TAP_RX_NBL_FLAG_TEST(currentNbl,TAP_RX_NBL_FLAGS_IS_P2P))
        {
            PNET_BUFFER     netBuffer;
            PMDL            mdl;

            netBuffer = NET_BUFFER_LIST_FIRST_NB(currentNbl);
            mdl = NET_BUFFER_FIRST_MDL(netBuffer);
            mdl->Next = NULL;

            NdisFreeMdl(mdl);
        }

        //
        // Complete the IRP
        //
        irp = (PIRP )currentNbl->MiniportReserved[0];

        status = STATUS_SUCCESS;    // NET_BUFFER_LIST_STATUS is not meaningful here.

        irp->IoStatus.Status = status;
        IoCompleteRequest(irp, IO_NO_INCREMENT);

        // Update in-flight statistics, etc.

        // Free the NBL
        NdisFreeNetBufferList(currentNbl);

        // Move to next NBL
        currentNbl = nextNbl;
    }
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
        DEBUGP (("[%s] Interface is down in IRP_MJ_WRITE\n",
            MINIPORT_INSTANCE_ID (adapter)));

        NOTE_ERROR();
        Irp->IoStatus.Status = ntStatus = STATUS_UNSUCCESSFUL;
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

    if (!adapter->m_tun && ((irpSp->Parameters.Write.Length) >= ETHERNET_HEADER_SIZE))
    {
        PNET_BUFFER_LIST    netBufferList;

		DUMP_PACKET ("IRP_MJ_WRITE ETH",
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
			FALSE,
			"RX",
			&adapter->m_RxTrunc
            );
#endif
        (Irp->MdlAddress)->Next = NULL; // No next MDL

        //
        // Allocate the NBL
        //
        netBufferList = NdisAllocateNetBufferAndNetBufferList(
            adapter->ReceiveNblPool,
            0,                  // ContextSize
            0,                  // ContextBackFill
            Irp->MdlAddress,    // MDL chain
            0,
            dataLength
            );

        if(netBufferList != NULL)
        {
            // BUGBUG!!! Increment in-flight statistics!!!
            // BUGBUG!!! Queue the NBL if adapter paused!!!

            // Stash IRP pointer in NBL MiniportReserved[0] field.
            netBufferList->MiniportReserved[0] = Irp;

            TAP_RX_NBL_FLAGS_CLEAR_ALL(netBufferList);

            //
            // Indicate the packet
            // -------------------
            // Irp->AssociatedIrp.SystemBuffer with length irpSp->Parameters.Write.Length
            // contains the complete packet including Ethernet header and payload.
            //
            NdisMIndicateReceiveNetBufferLists(
                adapter->MiniportAdapterHandle,
                netBufferList,
                NDIS_DEFAULT_PORT_NUMBER,
                1,      // NumberOfNetBufferLists
                0       // ReceiveFlags
                );

            ntStatus = STATUS_PENDING;
        }
        else
        {
            // Fail the IRP
		    ntStatus = STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    else if (adapter->m_tun && ((irpSp->Parameters.Write.Length) >= IP_HEADER_SIZE))
    {
		PETH_HEADER         p_UserToTap = &adapter->m_UserToTap;
        PMDL                mdl;    // Head of MDL chain.

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
			FALSE,
			"RX",
			&adapter->m_RxTrunc
            );
#endif

        //
        // Allocate MDL for Ethernet header
        // --------------------------------
        // Irp->AssociatedIrp.SystemBuffer with length irpSp->Parameters.Write.Length
        // contains the only the Ethernet payload. Prepend the user-mode provided
        // payload with the Ethernet header pointed to by p_UserToTap.
        //
        mdl = NdisAllocateMdl(
                adapter->MiniportAdapterHandle,
                p_UserToTap,
                sizeof(ETH_HEADER)
                );

        if(mdl != NULL)
        {
            PNET_BUFFER_LIST    netBufferList;

            // Chain user's Ethernet payload behind Ethernet header.
            mdl->Next = Irp->MdlAddress;
            (Irp->MdlAddress)->Next = NULL; // No next MDL

            //
            // Allocate the NBL
            //
            netBufferList = NdisAllocateNetBufferAndNetBufferList(
                adapter->ReceiveNblPool,
                0,          // ContextSize
                0,          // ContextBackFill
                mdl,        // MDL chain
                0,
                sizeof(ETH_HEADER) + dataLength
                );

            if(netBufferList != NULL)
            {
                // BUGBUG!!! Increment in-flight statistics!!!
                // BUGBUG!!! Queue the NBL if adapter paused!!!

                // Stash IRP pointer in NBL MiniportReserved[0] field.
                netBufferList->MiniportReserved[0] = Irp;

                // Set flag indicating that this is P2P packet
                TAP_RX_NBL_FLAGS_CLEAR_ALL(netBufferList);
                TAP_RX_NBL_FLAG_SET(netBufferList,TAP_RX_NBL_FLAGS_IS_P2P);

                //
                // Indicate the packet
                //
                NdisMIndicateReceiveNetBufferLists(
                    adapter->MiniportAdapterHandle,
                    netBufferList,
                    NDIS_DEFAULT_PORT_NUMBER,
                    1,      // NumberOfNetBufferLists
                    0       // ReceiveFlags
                    );

                ntStatus = STATUS_PENDING;
            }
            else
            {
                mdl->Next = NULL;
                NdisFreeMdl(mdl);

                // Fail the IRP
		        ntStatus = STATUS_INSUFFICIENT_RESOURCES;
            }
        }
        else
        {
            // Fail the IRP
		    ntStatus = STATUS_INSUFFICIENT_RESOURCES;
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

    if (ntStatus != STATUS_PENDING)
    {
		Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = ntStatus;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    return ntStatus;
}

