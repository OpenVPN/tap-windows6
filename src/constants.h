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

//====================================================================
//                        Product and Version public settings
//====================================================================

#define PRODUCT_STRING PRODUCT_TAP_DEVICE_DESCRIPTION


//
// Update the driver version number every time you release a new driver
// The high word is the major version. The low word is the minor version.
// Also make sure that VER_FILEVERSION specified in the .RC file also
// matches with the driver version because NDISTESTER checks for that.
//
#ifndef TAP_DRIVER_MAJOR_VERSION

#define TAP_DRIVER_MAJOR_VERSION           0x04
#define TAP_DRIVER_MINOR_VERSION           0x02

#endif

#define TAP_DRIVER_VENDOR_VERSION          ((TAP_DRIVER_MAJOR_VERSION << 16) | TAP_DRIVER_MINOR_VERSION)

//
// Define the NDIS miniport interface version that this driver targets.
//
#if defined(NDIS60_MINIPORT)
#  define TAP_NDIS_MAJOR_VERSION        6
#  define TAP_NDIS_MINOR_VERSION        0
#elif defined(NDIS620_MINIPORT)
#  define TAP_NDIS_MAJOR_VERSION        6
#  define TAP_NDIS_MINOR_VERSION        20
#elif defined(NDIS630_MINIPORT)
#  define TAP_NDIS_MAJOR_VERSION        6
#  define TAP_NDIS_MINOR_VERSION        30
#else
#define TAP_NDIS_MAJOR_VERSION          5
#define TAP_NDIS_MINOR_VERSION          0
#endif

//===========================================================
// Driver constants
//===========================================================

#define ETHERNET_HEADER_SIZE     (sizeof (ETH_HEADER))
#define ETHERNET_MTU             1500
#define ETHERNET_PACKET_SIZE     (ETHERNET_MTU + ETHERNET_HEADER_SIZE)
#define DEFAULT_PACKET_LOOKAHEAD (ETHERNET_PACKET_SIZE)

#define NIC_MAX_MCAST_LIST 32  // Max length of multicast address list

#define MINIMUM_MTU 576        // USE TCP Minimum MTU
#define MAXIMUM_MTU 65536      // IP maximum MTU

#define PACKET_QUEUE_SIZE   64 // tap -> userspace queue size
#define IRP_QUEUE_SIZE      16 // max number of simultaneous i/o operations from userspace
#define INJECT_QUEUE_SIZE   16 // DHCP/ARP -> tap injection queue

#define TAP_LITTLE_ENDIAN      // affects ntohs, htonl, etc. functions
