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

typedef struct _TAP_IRP_QUEUE
{
    IO_CSQ          CsqQueue;
    KSPIN_LOCK      CsqQueueLock;
    LIST_ENTRY      Queue;
    ULONG           Count;   // Count of currently queued items
    ULONG           MaxCount;
} TAP_IRP_QUEUE, *PTAP_IRP_QUEUE;

//======================================================================
// TAP Cancel-Safe Queue Callbacks
//======================================================================

VOID
tapCsqInsertReadIrp (
    __in struct _IO_CSQ    *Csq,
    __in PIRP              Irp
    );

VOID
tapCsqRemoveReadIrp(
    __in PIO_CSQ Csq,
    __in PIRP    Irp
    );

PIRP
tapCsqPeekNextReadIrp(
    __in PIO_CSQ Csq,
    __in PIRP    Irp,
    __in PVOID   PeekContext
    );

__drv_raisesIRQL(DISPATCH_LEVEL)
__drv_maxIRQL(DISPATCH_LEVEL)
VOID
tapCsqAcquireReadQueueLock(
     __in PIO_CSQ Csq,
     __out PKIRQL  Irql
    );

__drv_requiresIRQL(DISPATCH_LEVEL)
VOID
tapCsqReleaseReadQueueLock(
     __in PIO_CSQ Csq,
     __in KIRQL   Irql
    );

VOID
tapCsqCompleteCanceledIrp(
    __in  PIO_CSQ             pCsq,
    __in  PIRP                Irp
    );
