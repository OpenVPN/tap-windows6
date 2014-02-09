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

#ifndef TAP_PROTOTYPES_DEFINED
#define TAP_PROTOTYPES_DEFINED

DRIVER_INITIALIZE   DriverEntry;

//VOID AdapterFreeResources
//   (
//    TapAdapterPointer p_Adapter
//   );
//

//
//NTSTATUS TapDeviceHook
//   (
//    IN PDEVICE_OBJECT p_DeviceObject,
//    IN PIRP p_IRP
//   );
//

NDIS_STATUS
CreateTapDevice(
    __in PTAP_ADAPTER_CONTEXT   Adapter
   );

VOID
DestroyTapDevice(
    __in PTAP_ADAPTER_CONTEXT   Adapter
   );

//VOID TapDeviceFreeResources
//   (
//    TapExtensionPointer p_Extension
//    );
//
//NTSTATUS CompleteIRP
//   (
//    IN PIRP p_IRP,
//    IN PTAP_PACKET p_PacketBuffer,
//    IN CCHAR PriorityBoost
//   );
//
//VOID CancelIRPCallback
//   (
//    IN PDEVICE_OBJECT p_DeviceObject,
//    IN PIRP p_IRP
//   );
//
//VOID CancelIRP
//   (
//    TapExtensionPointer p_Extension,
//    IN PIRP p_IRP,
//    BOOLEAN callback
//   );
//
//VOID FlushQueues
//   (
//    TapExtensionPointer p_Extension
//   );
//
//VOID ResetTapAdapterState
//   (
//    TapAdapterPointer p_Adapter
//   );
//
//BOOLEAN ProcessARP
//   (
//    TapAdapterPointer p_Adapter,
//    const PARP_PACKET src,
//    const IPADDR adapter_ip,
//    const IPADDR ip_network,
//    const IPADDR ip_netmask,
//    const MACADDR mac
//   );
//
//VOID SetMediaStatus
//   (
//    TapAdapterPointer p_Adapter,
//    BOOLEAN state
//   );
//
//VOID InjectPacketDeferred
//   (
//    TapAdapterPointer p_Adapter,
//    UCHAR *packet,
//    const unsigned int len
//   );
//
//VOID InjectPacketNow
//   (
//    TapAdapterPointer p_Adapter,
//    UCHAR *packet,
//    const unsigned int len
//   );
//
//// for KDEFERRED_ROUTINE and Static Driver Verifier
////#include <wdm.h>
////KDEFERRED_ROUTINE InjectPacketDpc;
//
//VOID InjectPacketDpc
//   (
//    KDPC *Dpc,
//    PVOID DeferredContext,
//    PVOID SystemArgument1,
//    PVOID SystemArgument2
//    );
//
//VOID CheckIfDhcpAndTunMode
//   (
//    TapAdapterPointer p_Adapter
//   );
//
//VOID HookDispatchFunctions();
//
//#if ENABLE_NONADMIN
//
//#if defined(DDKVER_MAJOR) && DDKVER_MAJOR < 5600
///*
// * Better solution for use on Vista DDK, but possibly not compatible with
// * earlier DDKs:
// *
// * Eliminate the definition of SECURITY_DESCRIPTOR (and even ZwSetSecurityObject),
// * and at the top of tapdrv.c change:
// *
// * #include <ndis.h>
// * #include <ntstrsafe.h>
// * #include <ntddk.h>
// *
// * To
// *
// * #include <ntifs.h>
// * #include <ndis.h>
// * #include <ntstrsafe.h>
// */
//typedef struct _SECURITY_DESCRIPTOR {
//  unsigned char opaque[64];
//} SECURITY_DESCRIPTOR;
//
//NTSYSAPI
//NTSTATUS
//NTAPI
//ZwSetSecurityObject (
//  IN HANDLE  Handle,
//  IN SECURITY_INFORMATION  SecurityInformation,
//  IN PSECURITY_DESCRIPTOR  SecurityDescriptor);
//
//#endif
//
//VOID AllowNonAdmin (TapExtensionPointer p_Extension);
//
//#endif
//
//struct WIN2K_NDIS_MINIPORT_BLOCK
//{
//  unsigned char  opaque[16];
//  UNICODE_STRING MiniportName;       // how mini-port refers to us
//};
//
//#if PACKET_TRUNCATION_CHECK
//
//VOID IPv4PacketSizeVerify
//   (
//    const UCHAR *data,
//    ULONG length,
//    BOOLEAN tun,
//    const char *prefix,
//    LONG *counter
//   );
//
//#endif

#endif
