/*
 * Copyright © 2014 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS
 * AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

/**
***************************************************************************************************
* @file  addrobject.cpp
* @brief Contains the AddrObject base class implementation.
***************************************************************************************************
*/

#include "addrinterface.h"
#include "addrobject.h"

/**
***************************************************************************************************
*   AddrObject::AddrObject
*
*   @brief
*       Constructor for the AddrObject class.
***************************************************************************************************
*/
AddrObject::AddrObject()
{
    m_client.handle = NULL;
    m_client.callbacks.allocSysMem = NULL;
    m_client.callbacks.freeSysMem = NULL;
    m_client.callbacks.debugPrint = NULL;
}

/**
***************************************************************************************************
*   AddrObject::AddrObject
*
*   @brief
*       Constructor for the AddrObject class.
***************************************************************************************************
*/
AddrObject::AddrObject(const AddrClient* pClient)
{
    m_client = *pClient;
}

/**
***************************************************************************************************
*   AddrObject::~AddrObject
*
*   @brief
*       Destructor for the AddrObject class.
***************************************************************************************************
*/
AddrObject::~AddrObject()
{
}

/**
***************************************************************************************************
*   AddrObject::ClientAlloc
*
*   @brief
*       Calls instanced allocSysMem inside AddrClient
***************************************************************************************************
*/
VOID* AddrObject::ClientAlloc(
    size_t             objSize,    ///< [in] Size to allocate
    const AddrClient*  pClient)    ///< [in] Client pointer
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
***************************************************************************************************
*   AddrObject::AddrMalloc
*
*   @brief
*       A wrapper of ClientAlloc
***************************************************************************************************
*/
VOID* AddrObject::AddrMalloc(
    size_t objSize) const   ///< [in] Size to allocate
{
    return ClientAlloc(objSize, &m_client);
}

/**
***************************************************************************************************
*   AddrObject::ClientFree
*
*   @brief
*       Calls freeSysMem inside AddrClient
***************************************************************************************************
*/
VOID AddrObject::ClientFree(
    VOID*              pObjMem,    ///< [in] User virtual address to free.
    const AddrClient*  pClient)    ///< [in] Client pointer
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
***************************************************************************************************
*   AddrObject::AddrFree
*
*   @brief
*       A wrapper of ClientFree
***************************************************************************************************
*/
VOID AddrObject::AddrFree(
    VOID* pObjMem) const                 ///< [in] User virtual address to free.
{
    ClientFree(pObjMem, &m_client);
}

/**
***************************************************************************************************
*   AddrObject::operator new
*
*   @brief
*       Allocates memory needed for AddrObject object. (with ADDR_CLIENT_HANDLE)
*
*   @return
*       Returns NULL if unsuccessful.
***************************************************************************************************
*/
VOID* AddrObject::operator new(
    size_t             objSize,    ///< [in] Size to allocate
    const AddrClient*  pClient)    ///< [in] Client pointer
{
    return ClientAlloc(objSize, pClient);
}


/**
***************************************************************************************************
*   AddrObject::operator delete
*
*   @brief
*       Frees AddrObject object memory.
***************************************************************************************************
*/
VOID AddrObject::operator delete(
    VOID* pObjMem,              ///< [in] User virtual address to free.
    const AddrClient* pClient)  ///< [in] Client handle
{
    ClientFree(pObjMem, pClient);
}

/**
***************************************************************************************************
*   AddrObject::operator delete
*
*   @brief
*       Frees AddrObject object memory.
***************************************************************************************************
*/
VOID AddrObject::operator delete(
    VOID* pObjMem)                  ///< [in] User virtual address to free.
{
    AddrObject* pObj = static_cast<AddrObject*>(pObjMem);
    ClientFree(pObjMem, &pObj->m_client);
}

/**
***************************************************************************************************
*   AddrObject::DebugPrint
*
*   @brief
*       Print debug message
*
*   @return
*       N/A
***************************************************************************************************
*/
VOID AddrObject::DebugPrint(
    const CHAR* pDebugString,     ///< [in] Debug string
    ...) const
{
#if DEBUG
    if (m_client.callbacks.debugPrint != NULL)
    {
        va_list ap;

        va_start(ap, pDebugString);

        ADDR_DEBUGPRINT_INPUT debugPrintInput = {0};

        debugPrintInput.size         = sizeof(ADDR_DEBUGPRINT_INPUT);
        debugPrintInput.pDebugString = const_cast<CHAR*>(pDebugString);
        debugPrintInput.hClient      = m_client.handle;
        va_copy(debugPrintInput.ap, ap);

        m_client.callbacks.debugPrint(&debugPrintInput);

        va_end(ap);
    }
#endif
}

