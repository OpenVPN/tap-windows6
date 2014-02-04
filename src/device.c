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
#include <wdmsec.h> // for SDDLs

//======================================================================
// TAP Win32 Device I/O Callbacks
//======================================================================

#ifdef ALLOC_PRAGMA
#pragma alloc_text( PAGE, TapDeviceCreate)
#pragma alloc_text( PAGE, TapDeviceRead)
#pragma alloc_text( PAGE, TapDeviceWrite)
#pragma alloc_text( PAGE, TapDeviceControl)
#pragma alloc_text( PAGE, TapDeviceCleanup)
#pragma alloc_text( PAGE, TapDeviceClose)
#endif // ALLOC_PRAGMA

// IRP_MJ_CREATE
NTSTATUS
TapDeviceCreate(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp
    )
/*++

Routine Description:

    This routine is called by the I/O system when the device is opened.

    No action is performed other than completing the request successfully.

Arguments:

    DeviceObject - a pointer to the object that represents the device
    that I/O is to be done on.

    Irp - a pointer to the I/O Request Packet for this request.

Return Value:

    NT status code

--*/
{
    PTAP_ADAPTER_CONTEXT    adapter = NULL;

    PAGED_CODE();

    //
    // Find adapter context for this device.
    // -------------------------------------
    // Returns with added reference on adapter context.
    //
    adapter = tapAdapterContextFromDeviceObject(DeviceObject);

    ASSERT(adapter);

    // BUGBUG!!! Also check for halt state!!!
    if(adapter == NULL)
    {
    }

    // BUGBUG!!! Just dereference for now...
    tapAdapterContextDereference(adapter);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;

    IoCompleteRequest( Irp, IO_NO_INCREMENT );

    return STATUS_SUCCESS;
}

// IRP_MJ_READ callback.
NTSTATUS
TapDeviceRead(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp
    )
{
    NTSTATUS    ntStatus;

    ntStatus = STATUS_NOT_SUPPORTED;

    return ntStatus;
}

// IRP_MJ_WRITE callback.
NTSTATUS
TapDeviceWrite(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp
    )
{
    NTSTATUS    ntStatus;

    ntStatus = STATUS_NOT_SUPPORTED;

    return ntStatus;
}

//======================================================
// If DHCP mode is used together with tun
// mode, consider the fact that the P2P remote subnet
// might enclose the DHCP masq server address.
//======================================================
VOID
CheckIfDhcpAndTunMode (
    __in PTAP_ADAPTER_CONTEXT   Adapter
    )
{
    if (Adapter->m_tun && Adapter->m_dhcp_enabled)
    {
        if ((Adapter->m_dhcp_server_ip & Adapter->m_remoteNetmask) == Adapter->m_remoteNetwork)
        {
            COPY_MAC (Adapter->m_dhcp_server_mac, Adapter->m_TapToUser.dest);
            Adapter->m_dhcp_server_arp = FALSE;
        }
    }
}

// IRP_MJ_DEVICE_CONTROL callback.
NTSTATUS
TapDeviceControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp
    )

/*++

Routine Description:

    This routine is called by the I/O system to perform a device I/O
    control function.

Arguments:

    DeviceObject - a pointer to the object that represents the device
        that I/O is to be done on.

    Irp - a pointer to the I/O Request Packet for this request.

Return Value:

    NT status code

--*/

{
    PIO_STACK_LOCATION      irpSp;// Pointer to current stack location
    NTSTATUS                ntStatus = STATUS_SUCCESS;// Assume success
    ULONG                   inBufLength; // Input buffer length
    ULONG                   outBufLength; // Output buffer length
    PCHAR                   inBuf, outBuf; // pointer to Input and output buffer
    PMDL                    mdl = NULL;
    PCHAR                   buffer = NULL;
    PTAP_ADAPTER_CONTEXT    adapter = NULL;

    PAGED_CODE();

    //
    // Find adapter context for this device.
    // -------------------------------------
    // Returns with added reference on adapter context.
    //
    adapter = tapAdapterContextFromDeviceObject(DeviceObject);

    ASSERT(adapter);

    // BUGBUG!!! Also check for halt state!!!
    if(adapter == NULL)
    {
    }

    irpSp = IoGetCurrentIrpStackLocation( Irp );
    inBufLength = irpSp->Parameters.DeviceIoControl.InputBufferLength;
    outBufLength = irpSp->Parameters.DeviceIoControl.OutputBufferLength;

    if (!inBufLength || !outBufLength)
    {
        ntStatus = STATUS_INVALID_PARAMETER;
        goto End;
    }

    //
    // Determine which I/O control code was specified.
    //
    switch ( irpSp->Parameters.DeviceIoControl.IoControlCode )
    {
    case TAP_WIN_IOCTL_GET_MAC:
        {
            if (outBufLength >= MACADDR_SIZE )
            {
                COPY_MAC(
                    Irp->AssociatedIrp.SystemBuffer,
                    adapter->CurrentAddress
                    );

                Irp->IoStatus.Information = MACADDR_SIZE;
            }
            else
            {
                // BUGBUG!!! Fixme!!!
                //NOTE_ERROR();
                Irp->IoStatus.Status = ntStatus = STATUS_BUFFER_TOO_SMALL;
            }
        }
        break;

    case TAP_WIN_IOCTL_GET_VERSION:
        {
            const ULONG size = sizeof (ULONG) * 3;

            if (outBufLength >= size)
            {
                ((PULONG) (Irp->AssociatedIrp.SystemBuffer))[0]
                    = TAP_DRIVER_MAJOR_VERSION;

                ((PULONG) (Irp->AssociatedIrp.SystemBuffer))[1]
                    = TAP_DRIVER_MINOR_VERSION;

                ((PULONG) (Irp->AssociatedIrp.SystemBuffer))[2]
#if DBG
                    = 1;
#else
                    = 0;
#endif
                Irp->IoStatus.Information = size;
            }
            else
            {
                // BUGBUG!!! Fixme!!!
                //NOTE_ERROR();
                Irp->IoStatus.Status = ntStatus = STATUS_BUFFER_TOO_SMALL;
            }
        }
        break;

    case TAP_WIN_IOCTL_GET_MTU:
        {
            const ULONG size = sizeof (ULONG) * 1;

            if (outBufLength >= size)
            {
                ((PULONG) (Irp->AssociatedIrp.SystemBuffer))[0]
                    = adapter->MtuSize;

                Irp->IoStatus.Information = size;
            }
            else
            {
                // BUGBUG!!! Fixme!!!
                //NOTE_ERROR();
                Irp->IoStatus.Status = ntStatus = STATUS_BUFFER_TOO_SMALL;
            }
        }
        break;

    case TAP_WIN_IOCTL_CONFIG_TUN:
        {
            if(inBufLength >= sizeof(IPADDR)*3)
            {
                MACADDR dest;

                adapter->m_tun = FALSE;

                GenerateRelatedMAC (dest, adapter->CurrentAddress, 1);

                adapter->m_localIP =       ((IPADDR*) (Irp->AssociatedIrp.SystemBuffer))[0];
                adapter->m_remoteNetwork = ((IPADDR*) (Irp->AssociatedIrp.SystemBuffer))[1];
                adapter->m_remoteNetmask = ((IPADDR*) (Irp->AssociatedIrp.SystemBuffer))[2];

                // Sanity check on network/netmask
                if ((adapter->m_remoteNetwork & adapter->m_remoteNetmask) != adapter->m_remoteNetwork)
                {
                    // BUGBUG!!! Fixme!!!
                    //NOTE_ERROR();
                    Irp->IoStatus.Status = ntStatus = STATUS_INVALID_PARAMETER;
                    break;
                }

                COPY_MAC (adapter->m_TapToUser.src, adapter->CurrentAddress);
                COPY_MAC (adapter->m_TapToUser.dest, dest);
                COPY_MAC (adapter->m_UserToTap.src, dest);
                COPY_MAC (adapter->m_UserToTap.dest, adapter->CurrentAddress);

                adapter->m_TapToUser.proto = adapter->m_UserToTap.proto = htons (ETH_P_IP);
                adapter->m_UserToTap_IPv6 = adapter->m_UserToTap;
                adapter->m_UserToTap_IPv6.proto = htons(ETH_P_IPV6);

                adapter->m_tun = TRUE;

                CheckIfDhcpAndTunMode (adapter);

                Irp->IoStatus.Information = 1; // Simple boolean value
            }
            else
            {
                // BUGBUG!!! Fixme!!!
                //NOTE_ERROR();
                Irp->IoStatus.Status = ntStatus = STATUS_INVALID_PARAMETER;
            }
        }
        break;

    case TAP_WIN_IOCTL_CONFIG_DHCP_MASQ:
        {
            if(inBufLength >= sizeof(IPADDR)*4)
            {
                adapter->m_dhcp_enabled = FALSE;
                adapter->m_dhcp_server_arp = FALSE;
                adapter->m_dhcp_user_supplied_options_buffer_len = 0;

                // Adapter IP addr / netmask
                adapter->m_dhcp_addr =
                    ((IPADDR*) (Irp->AssociatedIrp.SystemBuffer))[0];
                adapter->m_dhcp_netmask =
                    ((IPADDR*) (Irp->AssociatedIrp.SystemBuffer))[1];

                // IP addr of DHCP masq server
                adapter->m_dhcp_server_ip =
                    ((IPADDR*) (Irp->AssociatedIrp.SystemBuffer))[2];

                // Lease time in seconds
                adapter->m_dhcp_lease_time =
                    ((IPADDR*) (Irp->AssociatedIrp.SystemBuffer))[3];

                GenerateRelatedMAC(
                    adapter->m_dhcp_server_mac,
                    adapter->CurrentAddress,
                    2
                    );

                adapter->m_dhcp_enabled = TRUE;
                adapter->m_dhcp_server_arp = TRUE;

                CheckIfDhcpAndTunMode (adapter);

                Irp->IoStatus.Information = 1; // Simple boolean value
            }
            else
            {
                // BUGBUG!!! Fixme!!!
                //NOTE_ERROR();
                Irp->IoStatus.Status = ntStatus = STATUS_INVALID_PARAMETER;
            }
        }
        break;

    case TAP_WIN_IOCTL_CONFIG_DHCP_SET_OPT:
        {
            if (inBufLength <=  DHCP_USER_SUPPLIED_OPTIONS_BUFFER_SIZE
                && adapter->m_dhcp_enabled)
            {
                adapter->m_dhcp_user_supplied_options_buffer_len = 0;

                NdisMoveMemory(
                    adapter->m_dhcp_user_supplied_options_buffer,
                    Irp->AssociatedIrp.SystemBuffer,
                    inBufLength
                    );

                adapter->m_dhcp_user_supplied_options_buffer_len = 
                    inBufLength;

                Irp->IoStatus.Information = 1; // Simple boolean value
            }
            else
            {
                // BUGBUG!!! Fixme!!!
                //NOTE_ERROR();
                Irp->IoStatus.Status = ntStatus = STATUS_INVALID_PARAMETER;
            }
        }
        break;

    case TAP_WIN_IOCTL_GET_INFO:
    case TAP_WIN_IOCTL_CONFIG_POINT_TO_POINT:
    case TAP_WIN_IOCTL_SET_MEDIA_STATUS:
    case TAP_WIN_IOCTL_GET_LOG_LINE:
    default:

        //
        // The specified I/O control code is unrecognized by this driver.
        //
        ntStatus = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

End:

    // Remove reference added by tapAdapterContextFromDeviceObject,
    if(adapter != NULL)
    {
        tapAdapterContextDereference(adapter);
    }

    //
    // Finish the I/O operation by simply completing the packet and returning
    // the same status as in the packet itself.
    //
    Irp->IoStatus.Status = ntStatus;

    IoCompleteRequest( Irp, IO_NO_INCREMENT );

    return ntStatus;
}

// IRP_MJ_CLEANUP
NTSTATUS
TapDeviceCleanup(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp
    )
/*++

Routine Description:

    Receipt of this request indicates that the last handle for a file
    object that is associated with the target device object has been closed
    (but, due to outstanding I/O requests, might not have been released).

Arguments:

    DeviceObject - a pointer to the object that represents the device
    to be cleaned up.

    Irp - a pointer to the I/O Request Packet for this request.

Return Value:

    NT status code

--*/

{
    UNREFERENCED_PARAMETER(DeviceObject);

    PAGED_CODE();

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;

    IoCompleteRequest( Irp, IO_NO_INCREMENT );

    return STATUS_SUCCESS;
}

// IRP_MJ_CLOSE
NTSTATUS
TapDeviceClose(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp
    )
/*++

Routine Description:

    Receipt of this request indicates that the last handle of the file
    object that is associated with the target device object has been closed
    and released.
    
    All outstanding I/O requests have been completed or canceled.

Arguments:

    DeviceObject - a pointer to the object that represents the device
    to be closed.

    Irp - a pointer to the I/O Request Packet for this request.

Return Value:

    NT status code

--*/

{
    UNREFERENCED_PARAMETER(DeviceObject);

    PAGED_CODE();

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;

    IoCompleteRequest( Irp, IO_NO_INCREMENT );

    return STATUS_SUCCESS;
}

NTSTATUS
tapConcatenateNdisStrings(
    __inout     PNDIS_STRING    DestinationString,
    __in_opt    PNDIS_STRING    SourceString1,
    __in_opt    PNDIS_STRING    SourceString2,
    __in_opt    PNDIS_STRING    SourceString3
    )
{
    NTSTATUS status;

    ASSERT(SourceString1 && SourceString2 && SourceString3);

    status = RtlAppendUnicodeStringToString(
                DestinationString,
                SourceString1
                );

    if(status == STATUS_SUCCESS)
    {
        status = RtlAppendUnicodeStringToString(
                    DestinationString,
                    SourceString2
                    );

        if(status == STATUS_SUCCESS)
        {
            status = RtlAppendUnicodeStringToString(
                        DestinationString,
                        SourceString3
                        );
        }
    }

    return status;
}

NTSTATUS
tapMakeDeviceNames(
    __in PTAP_ADAPTER_CONTEXT   Adapter
    )
{
    NDIS_STATUS     status;
    NDIS_STRING     deviceNamePrefix = NDIS_STRING_CONST("\\Device\\");
    NDIS_STRING     tapNameSuffix = NDIS_STRING_CONST(".tap");

    // Generate DeviceName from NetCfgInstanceId.
    Adapter->DeviceName.Buffer = Adapter->DeviceNameBuffer;
    Adapter->DeviceName.MaximumLength = sizeof(Adapter->DeviceNameBuffer);

    status = tapConcatenateNdisStrings(
                &Adapter->DeviceName,
                &deviceNamePrefix,
                &Adapter->NetCfgInstanceId,
                &tapNameSuffix
                );

    if(status == STATUS_SUCCESS)
    {
        NDIS_STRING     linkNamePrefix = NDIS_STRING_CONST("\\DosDevices\\Global\\");

        Adapter->LinkName.Buffer = Adapter->LinkNameBuffer;
        Adapter->LinkName.MaximumLength = sizeof(Adapter->LinkNameBuffer);

        status = tapConcatenateNdisStrings(
                    &Adapter->LinkName,
                    &linkNamePrefix,
                    &Adapter->NetCfgInstanceId,
                    &tapNameSuffix
                    );
    }

    return status;
}

NDIS_STATUS
CreateTapDevice(
    __in PTAP_ADAPTER_CONTEXT   Adapter
   )
{
    NDIS_STATUS                     status;
    NDIS_DEVICE_OBJECT_ATTRIBUTES   deviceAttribute;
    PDRIVER_DISPATCH                dispatchTable[IRP_MJ_MAXIMUM_FUNCTION+1];

    DEBUGP (("[TAP] version [%d.%d] creating tap device: %wZ\n",
        TAP_DRIVER_MAJOR_VERSION,
        TAP_DRIVER_MINOR_VERSION,
        &Adapter->NetCfgInstanceId));

    // Generate DeviceName and LinkName from NetCfgInstanceId.
    status = tapMakeDeviceNames(Adapter);

    if (NT_SUCCESS(status))
    {
        DEBUGP (("[TAP] DeviceName: %wZ\n",&Adapter->DeviceName));
        DEBUGP (("[TAP] LinkName: %wZ\n",&Adapter->LinkName));

        // Initialize dispatch table.
        NdisZeroMemory(dispatchTable, (IRP_MJ_MAXIMUM_FUNCTION+1) * sizeof(PDRIVER_DISPATCH));

        dispatchTable[IRP_MJ_CREATE] = TapDeviceCreate;
        dispatchTable[IRP_MJ_CLEANUP] = TapDeviceCleanup;
        dispatchTable[IRP_MJ_CLOSE] = TapDeviceClose;
        dispatchTable[IRP_MJ_READ] = TapDeviceRead;
        dispatchTable[IRP_MJ_WRITE] = TapDeviceWrite;
        dispatchTable[IRP_MJ_DEVICE_CONTROL] = TapDeviceControl;

        //
        // Create a device object and register dispatch handlers
        //
        NdisZeroMemory(&deviceAttribute, sizeof(NDIS_DEVICE_OBJECT_ATTRIBUTES));

        deviceAttribute.Header.Type = NDIS_OBJECT_TYPE_DEVICE_OBJECT_ATTRIBUTES;
        deviceAttribute.Header.Revision = NDIS_DEVICE_OBJECT_ATTRIBUTES_REVISION_1;
        deviceAttribute.Header.Size = sizeof(NDIS_DEVICE_OBJECT_ATTRIBUTES);

        deviceAttribute.DeviceName = &Adapter->DeviceName;
        deviceAttribute.SymbolicName = &Adapter->LinkName;
        deviceAttribute.MajorFunctions = &dispatchTable[0];
        //deviceAttribute.ExtensionSize = sizeof(FILTER_DEVICE_EXTENSION);

#if ENABLE_NONADMIN
        if(Adapter->AllowNonAdmin)
        {
            //
            // SDDL_DEVOBJ_SYS_ALL_WORLD_RWX_RES_RWX allows the kernel and system complete
            // control over the device. By default the admin can access the entire device,
            // but cannot change the ACL (the admin must take control of the device first)
            //
            // Everyone else, including "restricted" or "untrusted" code can read or write
            // to the device. Traversal beneath the device is also granted (removing it
            // would only effect storage devices, except if the "bypass-traversal"
            // privilege was revoked).
            //
            deviceAttribute.DefaultSDDLString = &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RWX_RES_RWX;
        }
#endif

        status = NdisRegisterDeviceEx(
                    Adapter->MiniportAdapterHandle,
                    &deviceAttribute,
                    &Adapter->DeviceObject,
                    &Adapter->DeviceHandle
                    );
    }

    ASSERT(NT_SUCCESS(status));

    if (NT_SUCCESS(status))
    {
        // Set TAP device flags.
        (Adapter->DeviceObject)->Flags &= ~DO_BUFFERED_IO;
        (Adapter->DeviceObject)->Flags |= DO_DIRECT_IO;;
    }

    DEBUGP (("[TAP] <-- CreateTapDevice; status = %8.8X\n",status));

    return status;
}

VOID
DestroyTapDevice(
    __in PTAP_ADAPTER_CONTEXT   Adapter
   )
{
    DEBUGP (("[TAP] --> DestroyTapDevice; Adapter: %wZ\n",
        &Adapter->NetCfgInstanceId));

    // Flush IRP queues. Wait for pending I/O. Etc.

    // Deregister the Win32 device.
    if(Adapter->DeviceHandle)
    {
        DEBUGP (("[TAP] Calling NdisDeregisterDeviceEx\n"));
        NdisDeregisterDeviceEx(Adapter->DeviceHandle);
    }

    Adapter->DeviceHandle = NULL;

    DEBUGP (("[TAP] <-- DestroyTapDevice\n"));
}

//======================================================================
// TAP Cancel-Safe Queue Callbacks
//======================================================================

VOID
tapCsqInsertReadIrp (
    __in struct _IO_CSQ    *Csq,
    __in PIRP              Irp
    )
{
    PTAP_ADAPTER_CONTEXT    adapter;

    adapter = CONTAINING_RECORD(
                Csq,
                TAP_ADAPTER_CONTEXT,
                PendingReadCsqQueue
                );

    InsertTailList(
        &adapter->PendingReadIrpQueue,
        &Irp->Tail.Overlay.ListEntry
        );
}

VOID
tapCsqRemoveReadIrp(
    __in PIO_CSQ Csq,
    __in PIRP    Irp
    )
{
    UNREFERENCED_PARAMETER(Csq);

    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
}


PIRP
tapCsqPeekNextReadIrp(
    __in PIO_CSQ Csq,
    __in PIRP    Irp,
    __in PVOID   PeekContext
    )
{
    PTAP_ADAPTER_CONTEXT    adapter;
    PIRP                    nextIrp = NULL;
    PLIST_ENTRY             nextEntry;
    PLIST_ENTRY             listHead;
    PIO_STACK_LOCATION     irpStack;

    adapter = CONTAINING_RECORD(
                Csq,
                TAP_ADAPTER_CONTEXT,
                PendingReadCsqQueue
                );

    listHead = &adapter->PendingReadIrpQueue;

    //
    // If the IRP is NULL, we will start peeking from the listhead, else
    // we will start from that IRP onwards. This is done under the
    // assumption that new IRPs are always inserted at the tail.
    //

    if (Irp == NULL)
    {
        nextEntry = listHead->Flink;
    }
    else
    {
        nextEntry = Irp->Tail.Overlay.ListEntry.Flink;
    }

    while(nextEntry != listHead)
    {
        nextIrp = CONTAINING_RECORD(nextEntry, IRP, Tail.Overlay.ListEntry);

        irpStack = IoGetCurrentIrpStackLocation(nextIrp);

        //
        // If context is present, continue until you find a matching one.
        // Else you break out as you got next one.
        //
        if (PeekContext)
        {
            if (irpStack->FileObject == (PFILE_OBJECT) PeekContext)
            {
                break;
            }
        }
        else
        {
            break;
        }

        nextIrp = NULL;
        nextEntry = nextEntry->Flink;
    }

    return nextIrp;
}

//
// tapCsqAcquireReadQueueLock modifies the execution level of the current processor.
// 
// KeAcquireSpinLock raises the execution level to Dispatch Level and stores
// the current execution level in the Irql parameter to be restored at a later
// time.  KeAcqurieSpinLock also requires us to be running at no higher than
// Dispatch level when it is called.
//
// The annotations reflect these changes and requirments.
//

VOID
tapCsqAcquireReadQueueLock(
     __in PIO_CSQ Csq,
     __out PKIRQL  Irql
    )
{
    PTAP_ADAPTER_CONTEXT    adapter;

    adapter = CONTAINING_RECORD(
                Csq,
                TAP_ADAPTER_CONTEXT,
                PendingReadCsqQueue
                );

    //
    // Suppressing because the address below csq is valid since it's
    // part of TAP_ADAPTER_CONTEXT structure.
    //
#pragma prefast(suppress: __WARNING_BUFFER_UNDERFLOW, "Underflow using expression 'adapter->PendingReadCsqQueueLock'")
    KeAcquireSpinLock(&adapter->PendingReadCsqQueueLock, Irql);
}

//
// tapCsqReleaseReadQueueLock modifies the execution level of the current processor.
// 
// KeReleaseSpinLock assumes we already hold the spin lock and are therefore
// running at Dispatch level.  It will use the Irql parameter saved in a
// previous call to KeAcquireSpinLock to return the thread back to it's original
// execution level.
//
// The annotations reflect these changes and requirments.
//

VOID
tapCsqReleaseReadQueueLock(
     __in PIO_CSQ Csq,
     __in KIRQL   Irql
    )
{
    PTAP_ADAPTER_CONTEXT    adapter;

    adapter = CONTAINING_RECORD(
                Csq,
                TAP_ADAPTER_CONTEXT,
                PendingReadCsqQueue
                );

    //
    // Suppressing because the address below csq is valid since it's
    // part of TAP_ADAPTER_CONTEXT structure.
    //
#pragma prefast(suppress: __WARNING_BUFFER_UNDERFLOW, "Underflow using expression 'adapter->PendingReadCsqQueueLock'")
    KeReleaseSpinLock(&adapter->PendingReadCsqQueueLock, Irql);
}

VOID
tapCsqCompleteCanceledIrp(
    __in  PIO_CSQ             pCsq,
    __in  PIRP                Irp
    )
{
    UNREFERENCED_PARAMETER(pCsq);

    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}
