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
    PIO_STACK_LOCATION  irpSp;// Pointer to current stack location
    NTSTATUS            ntStatus = STATUS_SUCCESS;// Assume success
    ULONG               inBufLength; // Input buffer length
    ULONG               outBufLength; // Output buffer length
    PCHAR               inBuf, outBuf; // pointer to Input and output buffer
    PMDL                mdl = NULL;
    PCHAR               buffer = NULL;

    UNREFERENCED_PARAMETER(DeviceObject);

    PAGED_CODE();

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
    case TAP_WIN_IOCTL_GET_VERSION:
    case TAP_WIN_IOCTL_GET_MTU:
    case TAP_WIN_IOCTL_GET_INFO:
    case TAP_WIN_IOCTL_CONFIG_POINT_TO_POINT:
    case TAP_WIN_IOCTL_SET_MEDIA_STATUS:
    case TAP_WIN_IOCTL_CONFIG_DHCP_MASQ:
    case TAP_WIN_IOCTL_GET_LOG_LINE:
    case TAP_WIN_IOCTL_CONFIG_DHCP_SET_OPT:
    case TAP_WIN_IOCTL_CONFIG_TUN:
    default:

        //
        // The specified I/O control code is unrecognized by this driver.
        //
        ntStatus = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

End:
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
