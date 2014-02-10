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

//------------------
// Memory Management
//------------------

PVOID
MemAlloc (ULONG p_Size, BOOLEAN zero);

VOID
MemFree (PVOID p_Addr, ULONG p_Size);

//----------------------
// Cancel-Safe IRP Queue
//----------------------

typedef struct _TAP_IRP_CSQ
{
    IO_CSQ          CsqQueue;
    KSPIN_LOCK      CsqQueueLock;
    LIST_ENTRY      Queue;
    ULONG           Count;   // Count of currently queued items
    ULONG           MaxCount;
} TAP_IRP_CSQ, *PTAP_IRP_CSQ;

VOID
tapIrpCsqInitialize(
    __in PTAP_IRP_CSQ  TapIrpCsq
    );

VOID
tapIrpCsqFlush(
    __in PTAP_IRP_CSQ  TapIrpCsq
    );
