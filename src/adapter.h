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
#ifndef __TAP_ADAPTER_CONTEXT_H_
#define __TAP_ADAPTER_CONTEXT_H_

// Memory allocation tags.
#define TAP_TAG_ADAPTER     ((ULONG)'ApaT')     // "TapA

#define TAP_MAX_NDIS_NAME_LENGTH     260

//
// Each adapter managed by this driver has a TapAdapter struct.
// ------------------------------------------------------------
// Since there is a one-to-one relationship between adapter instances
// and device instances this structure is the device extension as well.
//
typedef struct _TAP_ADAPTER_CONTEXT
{
    LIST_ENTRY              AdapterListLink;

    volatile LONG           RefCount;

    NDIS_HANDLE             MiniportAdapterHandle;

    // Miniport name as UNICODE
    NDIS_STRING             MiniportName;
    WCHAR                   MiniportNameBuffer[TAP_MAX_NDIS_NAME_LENGTH];

# define MINIPORT_ANSI_NAME(a) ((a)->MiniportNameAnsi.Buffer)
    ANSI_STRING             MiniportNameAnsi;   // Used occasionally

    ULONG                   MtuSize;        // 1500 byte (typical)

    // TRUE if adapter should always be
    // "connected" even when device node
    // is not open by a userspace process.
    BOOLEAN                 MediaStateAlwaysConnected;

    // TRUE if device is "connected"
    BOOLEAN                 MediaState;

    BOOLEAN                 AllowNonAdmin;

    MACADDR                 PermanentAddress;   // From registry, if available
    MACADDR                 CurrentAddress;


    // Device registration parameters from NdisRegisterDeviceEx.
    NDIS_HANDLE             DeviceHandle;
    PDEVICE_OBJECT          DeviceObject;




  BOOLEAN m_InterfaceIsRunning;
  LONG m_Rx, m_Tx, m_RxErr, m_TxErr;
#if PACKET_TRUNCATION_CHECK
  LONG m_RxTrunc, m_TxTrunc;
#endif
  NDIS_MEDIUM m_Medium;
  ULONG m_Lookahead;

  // Adapter power state
  char m_DeviceState;

  // Info for point-to-point mode
  BOOLEAN m_tun;
  IPADDR m_localIP;
  IPADDR m_remoteNetwork;
  IPADDR m_remoteNetmask;
  ETH_HEADER m_TapToUser;
  ETH_HEADER m_UserToTap;
  ETH_HEADER m_UserToTap_IPv6;		// same as UserToTap but proto=ipv6
  MACADDR m_MAC_Broadcast;

  // Used for DHCP server masquerade
  BOOLEAN m_dhcp_enabled;
  IPADDR m_dhcp_addr;
  ULONG m_dhcp_netmask;
  IPADDR m_dhcp_server_ip;
  BOOLEAN m_dhcp_server_arp;
  MACADDR m_dhcp_server_mac;
  ULONG m_dhcp_lease_time;
  UCHAR m_dhcp_user_supplied_options_buffer[DHCP_USER_SUPPLIED_OPTIONS_BUFFER_SIZE];
  ULONG m_dhcp_user_supplied_options_buffer_len;
  BOOLEAN m_dhcp_received_discover;
  ULONG m_dhcp_bad_requests;

  // Help to tear down the adapter by keeping
  // some state information on allocated
  // resources.
  BOOLEAN m_CalledAdapterFreeResources;
  BOOLEAN m_RegisteredAdapterShutdownHandler;

  // Multicast list info
  NDIS_SPIN_LOCK m_MCLock;
  BOOLEAN m_MCLockAllocated;
  ULONG m_MCListSize;
  MC_LIST m_MCList;

} TAP_ADAPTER_CONTEXT, *PTAP_ADAPTER_CONTEXT;

FORCEINLINE
LONG
tapAdapterContextReference(
    __in PTAP_ADAPTER_CONTEXT   Adapter
    )
{
    LONG    refCount = NdisInterlockedIncrement(&Adapter->RefCount);

    ASSERT(refCount>1);     // Cannot dereference a zombie.

    return refCount;
}

VOID
tapAdapterContextFree(
    IN PTAP_ADAPTER_CONTEXT     Adapter
    );

FORCEINLINE
LONG
tapAdapterContextDereference(
    IN PTAP_ADAPTER_CONTEXT     Adapter
    )
{
    LONG    refCount = NdisInterlockedDecrement(&Adapter->RefCount);
    ASSERT(refCount >= 0);
    if (!refCount)
    {
        tapAdapterContextFree(Adapter);
    }

    return refCount;
}

// Returns with added reference on adapter context.
PTAP_ADAPTER_CONTEXT
tapAdapterContextFromDeviceObject(
    __in PDEVICE_OBJECT DeviceObject
    );

// Prototypes for standard NDIS miniport entry points
MINIPORT_INITIALIZE                 AdapterCreate;
MINIPORT_HALT                       AdapterHalt;
MINIPORT_UNLOAD                     TapDriverUnload;
MINIPORT_PAUSE                      AdapterPause;
MINIPORT_RESTART                    AdapterRestart;
MINIPORT_OID_REQUEST                AdapterOidRequest;
MINIPORT_SEND_NET_BUFFER_LISTS      AdapterSendNetBufferLists;
MINIPORT_RETURN_NET_BUFFER_LISTS    AdapterReturnNetBufferLists;
//MINIPORT_CANCEL_SEND                MPCancelSend;
MINIPORT_CHECK_FOR_HANG             AdapterCheckForHangEx;
MINIPORT_RESET                      AdapterReset;
//MINIPORT_DEVICE_PNP_EVENT_NOTIFY    MPDevicePnpEventNotify;
//MINIPORT_SHUTDOWN                   MPShutdownEx;
MINIPORT_CANCEL_OID_REQUEST         AdapterCancelOidRequest;

#endif // __TAP_ADAPTER_CONTEXT_H_