/*
 * Structures and static functions for handling asynchronous I/O.
 *
 * Copyright (C) 2002 Mike McCormack,  Martin Wilck
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * This file declares static functions.
 * It should only be included by those source files that implement async I/O requests.
 */

#ifndef __WINE_ASYNC_H
#define __WINE_ASYNC_H

#include "wine/server.h"

struct async_private;

typedef void (*async_handler)(struct async_private *ovp);
typedef void (CALLBACK *async_call_completion_func)(ULONG_PTR data);
typedef DWORD (*async_get_status)(const struct async_private *ovp);
typedef DWORD (*async_get_count)(const struct async_private *ovp);
typedef void (*async_set_status)(struct async_private *ovp, const DWORD status);
typedef void (*async_cleanup)(struct async_private *ovp);

typedef struct async_ops
{
    async_get_status            get_status;
    async_set_status            set_status;
    async_get_count             get_count;
    async_call_completion_func  call_completion;
    async_cleanup               cleanup;
} async_ops;

typedef struct async_private
{
    struct async_ops     *ops;
    HANDLE        handle;
    HANDLE        event;
    int           fd;
    async_handler func;
    int           type;
    struct async_private *next;
    struct async_private *prev;
} async_private;

/* All functions declared static for Dll separation purposes */

inline static void finish_async( async_private *ovp )
{
    if(ovp->prev)
        ovp->prev->next = ovp->next;
    else
        NtCurrentTeb()->pending_list = ovp->next;

    if(ovp->next)
        ovp->next->prev = ovp->prev;

    ovp->next = ovp->prev = NULL;

    close( ovp->fd );
    if( ovp->event != INVALID_HANDLE_VALUE )
        NtSetEvent( ovp->event, NULL );

    if ( ovp->ops->call_completion )
        QueueUserAPC( ovp->ops->call_completion, GetCurrentThread(), (ULONG_PTR)ovp );
    else
        ovp->ops->cleanup ( ovp );
}

inline static BOOL __register_async( async_private *ovp, const DWORD status )
{
    BOOL ret;

    SERVER_START_REQ( register_async )
    {
        req->handle = ovp->handle;
        req->overlapped = ovp;
        req->type = ovp->type;
        req->count = ovp->ops->get_count( ovp );
        req->status = status;
        ret = wine_server_call( req );
    }
    SERVER_END_REQ;

    if ( ret ) {
        SetLastError( RtlNtStatusToDosError(ret) );
        ovp->ops->set_status ( ovp, ret );
    }

    if ( ovp->ops->get_status (ovp) != STATUS_PENDING )
        finish_async (ovp);

    return ret;
}

#define register_old_async(ovp) \
    __register_async (ovp, ovp->ops->get_status( ovp ));

inline static BOOL register_new_async( async_private *ovp )
{
    ovp->ops->set_status ( ovp, STATUS_PENDING );

    ovp->next = NtCurrentTeb()->pending_list;
    ovp->prev = NULL;
    if ( ovp->next ) ovp->next->prev = ovp;
    NtCurrentTeb()->pending_list = ovp;

    return __register_async( ovp, STATUS_PENDING );
}

inline static BOOL cancel_async ( async_private *ovp )
{
     /* avoid multiple cancellations */
     if ( ovp->ops->get_status( ovp ) != STATUS_PENDING )
          return 0;
     ovp->ops->set_status ( ovp, STATUS_CANCELLED );
     return __register_async ( ovp, STATUS_CANCELLED );
}

#endif /* __WINE_ASYNC_H */
