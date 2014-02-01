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
    NDIS_STATUS    status;
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

        // TODO: Inplement these query information requests.
    case OID_GEN_HARDWARE_STATUS:
    case OID_GEN_MAXIMUM_TOTAL_SIZE:

        __fallthrough;

    case OID_GEN_TRANSMIT_BLOCK_SIZE:
    case OID_GEN_RECEIVE_BLOCK_SIZE:
    case OID_GEN_RECEIVE_BUFFER_SPACE:
    case OID_GEN_MEDIA_SUPPORTED:
    case OID_GEN_MEDIA_IN_USE:
    case OID_GEN_MAXIMUM_SEND_PACKETS:
    case OID_GEN_XMIT_ERROR:
    case OID_GEN_RCV_ERROR:
    case OID_GEN_RCV_DISCARDS:
    case OID_GEN_RCV_NO_BUFFER:
    case OID_GEN_VENDOR_ID:
    case OID_GEN_VENDOR_DESCRIPTION:
    case OID_GEN_VENDOR_DRIVER_VERSION:
    case OID_GEN_DRIVER_VERSION:
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
    case OID_GEN_INTERRUPT_MODERATION:
    case OID_PNP_QUERY_POWER:

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

