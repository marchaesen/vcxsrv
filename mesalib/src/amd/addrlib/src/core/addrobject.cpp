/*
************************************************************************************************************************
*
*  Copyright (C) 2007-2024 Advanced Micro Devices, Inc. All rights reserved.
*  SPDX-License-Identifier: MIT
*
***********************************************************************************************************************/


/**
****************************************************************************************************
* @file  addrobject.cpp
* @brief Contains the Object base class implementation.
****************************************************************************************************
*/

#include "addrinterface.h"
#include "addrobject.h"

#if DEBUG
#include <stdio.h>
#endif

namespace Addr
{

#if DEBUG && ADDR_ALLOW_TLS
thread_local ADDR_CLIENT_HANDLE g_clientHandle = nullptr;
thread_local ADDR_DEBUGPRINT    g_pfnDebugPrint = nullptr;
#endif

/**
****************************************************************************************************
*   Object::Object
*
*   @brief
*       Constructor for the Object class.
****************************************************************************************************
*/
Object::Object()
{
    m_client.handle = NULL;
    m_client.callbacks.allocSysMem = NULL;
    m_client.callbacks.freeSysMem = NULL;
    m_client.callbacks.debugPrint = NULL;
}

/**
****************************************************************************************************
*   Object::Object
*
*   @brief
*       Constructor for the Object class.
****************************************************************************************************
*/
Object::Object(const Client* pClient)
{
    m_client = *pClient;
}

/**
****************************************************************************************************
*   Object::~Object
*
*   @brief
*       Destructor for the Object class.
****************************************************************************************************
*/
Object::~Object()
{
}

/**
****************************************************************************************************
*   Object::ClientAlloc
*
*   @brief
*       Calls instanced allocSysMem inside Client
****************************************************************************************************
*/
VOID* Object::ClientAlloc(
    size_t         objSize,    ///< [in] Size to allocate
    const Client*  pClient)    ///< [in] Client pointer
{
    VOID* pObjMem = NULL;

    if (pClient->callbacks.allocSysMem != NULL)
    {
        ADDR_ALLOCSYSMEM_INPUT allocInput = {0};

        allocInput.size        = sizeof(ADDR_ALLOCSYSMEM_INPUT);
        allocInput.flags.value = 0;
        allocInput.sizeInBytes = static_cast<UINT_32>(objSize);
        allocInput.hClient     = pClient->handle;

        pObjMem = pClient->callbacks.allocSysMem(&allocInput);
    }

    return pObjMem;
}

/**
****************************************************************************************************
*   Object::Alloc
*
*   @brief
*       A wrapper of ClientAlloc
****************************************************************************************************
*/
VOID* Object::Alloc(
    size_t objSize      ///< [in] Size to allocate
    ) const
{
    return ClientAlloc(objSize, &m_client);;
}

/**
****************************************************************************************************
*   Object::ClientFree
*
*   @brief
*       Calls freeSysMem inside Client
****************************************************************************************************
*/
VOID Object::ClientFree(
    VOID*          pObjMem,    ///< [in] User virtual address to free.
    const Client*  pClient)    ///< [in] Client pointer
{
    if (pClient->callbacks.freeSysMem != NULL)
    {
        if (pObjMem != NULL)
        {
            ADDR_FREESYSMEM_INPUT freeInput = {0};

            freeInput.size      = sizeof(ADDR_FREESYSMEM_INPUT);
            freeInput.hClient   = pClient->handle;
            freeInput.pVirtAddr = pObjMem;

            pClient->callbacks.freeSysMem(&freeInput);
        }
    }
}

/**
****************************************************************************************************
*   Object::Free
*
*   @brief
*       A wrapper of ClientFree
****************************************************************************************************
*/
VOID Object::Free(
    VOID* pObjMem       ///< [in] User virtual address to free.
    ) const
{
    ClientFree(pObjMem, &m_client);
}

/**
****************************************************************************************************
*   Object::operator new
*
*   @brief
*       Placement new operator. (with pre-allocated memory pointer)
*
*   @return
*       Returns pre-allocated memory pointer.
****************************************************************************************************
*/
VOID* Object::operator new(
    size_t objSize,     ///< [in] Size to allocate
    VOID*  pMem        ///< [in] Pre-allocated pointer
    ) noexcept
{
    return pMem;
}

/**
****************************************************************************************************
*   Object::operator delete
*
*   @brief
*       Frees Object object memory.
****************************************************************************************************
*/
VOID Object::operator delete(
    VOID* pObjMem)      ///< [in] User virtual address to free.
{
    Object* pObj = static_cast<Object*>(pObjMem);
    ClientFree(pObjMem, &pObj->m_client);
}


#if DEBUG
/**
****************************************************************************************************
*   ApplyDebugPrinters
*
*   @brief
*       Sets the debug printers via TLS
*
*   @return
*       N/A
****************************************************************************************************
*/
VOID ApplyDebugPrinters(
    ADDR_DEBUGPRINT    pfnDebugPrint,
    ADDR_CLIENT_HANDLE pClientHandle)
{
#if ADDR_ALLOW_TLS
    g_clientHandle  = pClientHandle;
    g_pfnDebugPrint = pfnDebugPrint;
#endif
}

/**
****************************************************************************************************
*   DebugPrint
*
*   @brief
*       Print debug message
*
*   @return
*       N/A
****************************************************************************************************
*/
VOID DebugPrint(
    const CHAR* pDebugString,     ///< [in] Debug string
    ...
    )
{
    va_list ap;
    va_start(ap, pDebugString);

#if ADDR_ALLOW_TLS
    if (g_pfnDebugPrint != NULL)
    {
        ADDR_DEBUGPRINT_INPUT debugPrintInput = {0};

        debugPrintInput.size         = sizeof(ADDR_DEBUGPRINT_INPUT);
        debugPrintInput.pDebugString = const_cast<CHAR*>(pDebugString);
        debugPrintInput.hClient      = g_clientHandle;
        va_copy(debugPrintInput.ap, ap);

        g_pfnDebugPrint(&debugPrintInput);

        va_end(debugPrintInput.ap);
    }
    else
#endif
    {
#if ADDR_ALLOW_STDIO
        fprintf(stderr, "Warning: Addrlib assert function called without corresponding 'ApplyDebugPrinters'\n");
        vfprintf(stderr, pDebugString, ap);
#endif
    }
    va_end(ap);
}
#endif

} // Addr
