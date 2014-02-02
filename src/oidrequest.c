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
// TAP NDIS 6 OID Request Callbacks
//======================================================================

NDIS_STATUS
tapSetInformation(
    __in PTAP_ADAPTER_CONTEXT       Adapter,
    __in PNDIS_OID_REQUEST  OidRequest
    )
/*++

Routine Description:

    Helper function to perform a set OID request

Arguments:

    Adapter         -
    NdisSetRequest  - The OID to set

Return Value:

    NDIS_STATUS

--*/
{
    NDIS_STATUS    status;

    switch(OidRequest->DATA.SET_INFORMATION.Oid)
    {
        // TODO: Inplement these set information requests.
    case OID_802_3_MULTICAST_LIST:
    case OID_GEN_CURRENT_PACKET_FILTER:
    case OID_GEN_CURRENT_LOOKAHEAD:
    case OID_PNP_SET_POWER:

#if (NDIS_SUPPORT_NDIS61)
    case OID_PNP_ADD_WAKE_UP_PATTERN:
    case OID_PNP_REMOVE_WAKE_UP_PATTERN:
    case OID_PNP_ENABLE_WAKE_UP:
#endif
        ASSERT(!"NIC does not support wake on LAN OIDs"); 
    default:
        //
        // The entry point may by used by other requests
        //
        status = NDIS_STATUS_NOT_SUPPORTED;
        break;
    }

    return status;
}

NDIS_STATUS
tapQueryInformation(
    __in PTAP_ADAPTER_CONTEXT       Adapter,
    __in PNDIS_OID_REQUEST  OidRequest
    )
/*++

Routine Description:

    Helper function to perform a query OID request

Arguments:

    Adapter         -
    OidRequest  - The OID request that is being queried

Return Value:

    NDIS_STATUS

--*/
{
    NDIS_STATUS    status =NDIS_STATUS_SUCCESS;
    NDIS_MEDIUM    Medium = TAP_MEDIUM_TYPE;
    ULONG          ulInfo;
    USHORT         usInfo;
    ULONG64        ulInfo64;

    // Default to returning the ULONG value
    PVOID          pInfo=NULL;
    ULONG          ulInfoLen = sizeof(ulInfo);

    // Dispatch based on object identifier (OID).
    switch(OidRequest->DATA.QUERY_INFORMATION.Oid)
    {
    case OID_802_3_PERMANENT_ADDRESS:
        //
        // Return the MAC address of the NIC burnt in the hardware.
        //
        pInfo = Adapter->PermanentAddress;
        ulInfoLen = MACADDR_SIZE;
        break;

    case OID_802_3_CURRENT_ADDRESS:
        //
        // Return the MAC address the NIC is currently programmed to
        // use. Note that this address could be different from the
        // permananent address as the user can override using
        // registry. Read NdisReadNetworkAddress doc for more info.
        //
        pInfo = Adapter->CurrentAddress;
        ulInfoLen = MACADDR_SIZE;
        break;

    case OID_GEN_MEDIA_SUPPORTED:
        //
        // Return an array of media that are supported by the miniport.
        // This miniport only supports one medium (Ethernet), so the OID
        // returns identical results to OID_GEN_MEDIA_IN_USE.
        //

        __fallthrough;

    case OID_GEN_MEDIA_IN_USE:
        //
        // Return an array of media that are currently in use by the
        // miniport.  This array should be a subset of the array returned
        // by OID_GEN_MEDIA_SUPPORTED.
        //
        pInfo = &Medium;
        ulInfoLen = sizeof(Medium);
        break;

    case OID_GEN_MAXIMUM_TOTAL_SIZE:
        //
        // Specify the maximum total packet length, in bytes, the NIC
        // supports including the header. A protocol driver might use
        // this returned length as a gauge to determine the maximum
        // size packet that a NIC driver could forward to the
        // protocol driver. The miniport driver must never indicate
        // up to the bound protocol driver packets received over the
        // network that are longer than the packet size specified by
        // OID_GEN_MAXIMUM_TOTAL_SIZE.
        //

        __fallthrough;

    case OID_GEN_TRANSMIT_BLOCK_SIZE:
        //
        // The OID_GEN_TRANSMIT_BLOCK_SIZE OID specifies the minimum
        // number of bytes that a single net packet occupies in the
        // transmit buffer space of the NIC. In our case, the transmit
        // block size is identical to its maximum packet size.
        __fallthrough;

    case OID_GEN_RECEIVE_BLOCK_SIZE:
        //
        // The OID_GEN_RECEIVE_BLOCK_SIZE OID specifies the amount of
        // storage, in bytes, that a single packet occupies in the receive
        // buffer space of the NIC.
        //
        ulInfo = (ULONG) TAP_MAX_FRAME_SIZE;
        pInfo = &ulInfo;
        break;

    case OID_GEN_INTERRUPT_MODERATION:
        {
            PNDIS_INTERRUPT_MODERATION_PARAMETERS moderationParams
                = (PNDIS_INTERRUPT_MODERATION_PARAMETERS)OidRequest->DATA.QUERY_INFORMATION.InformationBuffer;

            moderationParams->Header.Type = NDIS_OBJECT_TYPE_DEFAULT; 
            moderationParams->Header.Revision = NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
            moderationParams->Header.Size = NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
            moderationParams->Flags = 0;
            moderationParams->InterruptModeration = NdisInterruptModerationNotSupported;
            ulInfoLen = NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
        }
        break;

    case OID_PNP_QUERY_POWER:
        // simply succeed this.
        break;

    case OID_GEN_VENDOR_DRIVER_VERSION:
        //
        // Specify the vendor-assigned version number of the NIC driver.
        // The low-order half of the return value specifies the minor
        // version; the high-order half specifies the major version.
        //

        ulInfo = TAP_DRIVER_VENDOR_VERSION;
        pInfo = &ulInfo;
        break;

    case OID_GEN_DRIVER_VERSION:
        //
        // Specify the NDIS version in use by the NIC driver. The high
        // byte is the major version number; the low byte is the minor
        // version number.
        //
        usInfo = (USHORT) (TAP_NDIS_MAJOR_VERSION<<8) + TAP_NDIS_MINOR_VERSION;
        pInfo = (PVOID) &usInfo;
        ulInfoLen = sizeof(USHORT);
        break;

        // TODO: Inplement these query information requests.
    case OID_GEN_HARDWARE_STATUS:
    case OID_GEN_RECEIVE_BUFFER_SPACE:
    case OID_GEN_MAXIMUM_SEND_PACKETS:
    case OID_GEN_XMIT_ERROR:
    case OID_GEN_RCV_ERROR:
    case OID_GEN_RCV_DISCARDS:
    case OID_GEN_RCV_NO_BUFFER:
    case OID_GEN_VENDOR_ID:
    case OID_GEN_VENDOR_DESCRIPTION:
    case OID_802_3_MAXIMUM_LIST_SIZE:
    case OID_GEN_XMIT_OK:
    case OID_GEN_RCV_OK:
    case OID_GEN_STATISTICS:
    case OID_GEN_TRANSMIT_QUEUE_LENGTH:
    case OID_802_3_RCV_ERROR_ALIGNMENT:
    case OID_802_3_XMIT_ONE_COLLISION:
    case OID_802_3_XMIT_MORE_COLLISIONS:
    case OID_802_3_XMIT_DEFERRED:
    case OID_802_3_XMIT_MAX_COLLISIONS:
    case OID_802_3_RCV_OVERRUN:
    case OID_802_3_XMIT_UNDERRUN:
    case OID_802_3_XMIT_HEARTBEAT_FAILURE:
    case OID_802_3_XMIT_TIMES_CRS_LOST:
    case OID_802_3_XMIT_LATE_COLLISIONS:

    default:
        //
        // The entry point may by used by other requests
        //
        status = NDIS_STATUS_NOT_SUPPORTED;
        break;
    }

    if (status == NDIS_STATUS_SUCCESS)
    {
        ASSERT(ulInfoLen > 0);

        if (ulInfoLen <= OidRequest->DATA.QUERY_INFORMATION.InformationBufferLength)
        {
            if(pInfo)
            {
                // Copy result into InformationBuffer
                NdisMoveMemory(
                    OidRequest->DATA.QUERY_INFORMATION.InformationBuffer,
                    pInfo,
                    ulInfoLen
                    );
            }

            OidRequest->DATA.QUERY_INFORMATION.BytesWritten = ulInfoLen;
        }
        else
        {
            // too short
            OidRequest->DATA.QUERY_INFORMATION.BytesNeeded = ulInfoLen;
            status = NDIS_STATUS_BUFFER_TOO_SHORT;
        }
    }

    return status;
}

NDIS_STATUS
AdapterOidRequest(
    __in  NDIS_HANDLE             MiniportAdapterContext,
    __in  PNDIS_OID_REQUEST       OidRequest
    )
/*++

Routine Description:

    Entry point called by NDIS to get or set the value of a specified OID.

Arguments:

    MiniportAdapterContext  - Our adapter handle
    NdisRequest             - The OID request to handle

Return Value:

    Return code from the NdisRequest below.

--*/
{
    PTAP_ADAPTER_CONTEXT   adapter = (PTAP_ADAPTER_CONTEXT )MiniportAdapterContext;
    NDIS_STATUS    status;

    // Dispatch based on request type.
    switch (OidRequest->RequestType)
    {
    case NdisRequestSetInformation:
        status = tapSetInformation(adapter,OidRequest);
        break;

    case NdisRequestQueryInformation:
    case NdisRequestQueryStatistics:
        status = tapQueryInformation(adapter,OidRequest);
        break;

    case NdisRequestMethod: // TAP doesn't need to respond to this request type.
    default:
        //
        // The entry point may by used by other requests
        //
        status = NDIS_STATUS_NOT_SUPPORTED;
        break;
    }

    return status;
}

VOID
AdapterCancelOidRequest(
    __in NDIS_HANDLE              MiniportAdapterContext,
    __in PVOID                    RequestId
    )
{
    PTAP_ADAPTER_CONTEXT   adapter = (PTAP_ADAPTER_CONTEXT )MiniportAdapterContext;

    UNREFERENCED_PARAMETER(RequestId);

    //
    // This miniport sample does not pend any OID requests, so we don't have
    // to worry about cancelling them.
    //
}

