/****************************************************************************
 * Copyright (C) 2014-2018 Intel Corporation.   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * @file tilemgr.h
 *
 * @brief Definitions for Macro Tile Manager which provides the facilities
 *        for threads to work on an macro tile.
 *
 ******************************************************************************/
#pragma once

#include <set>
#include <unordered_map>
#include "common/formats.h"
#include "common/intrin.h"
#include "fifo.hpp"
#include "context.h"
#include "format_traits.h"

//////////////////////////////////////////////////////////////////////////
/// MacroTile - work queue for a tile.
//////////////////////////////////////////////////////////////////////////
struct MacroTileQueue
{
    MacroTileQueue() {}
    ~MacroTileQueue() { destroy(); }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Returns number of work items queued for this tile.
    uint32_t getNumQueued() { return mFifo.getNumQueued(); }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Attempt to lock the work fifo. If already locked then return false.
    bool tryLock() { return mFifo.tryLock(); }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Clear fifo and unlock it.
    template <typename ArenaT>
    void clear(ArenaT& arena)
    {
        mFifo.clear(arena);
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Peek at work sitting at the front of the fifo.
    BE_WORK* peek() { return mFifo.peek(); }

    template <typename ArenaT>
    bool enqueue_try_nosync(ArenaT& arena, const BE_WORK* entry)
    {
        return mFifo.enqueue_try_nosync(arena, entry);
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Move to next work item
    void dequeue() { mFifo.dequeue_noinc(); }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Destroy fifo
    void destroy() { mFifo.destroy(); }

    ///@todo This will all be private.
    uint32_t mWorkItemsFE = 0;
    uint32_t mWorkItemsBE = 0;
    uint32_t mId          = 0;

private:
    QUEUE<BE_WORK> mFifo;
};

//////////////////////////////////////////////////////////////////////////
/// MacroTileMgr - Manages macrotiles for a draw.
//////////////////////////////////////////////////////////////////////////
class MacroTileMgr
{
public:
    MacroTileMgr(CachingArena& arena);
    ~MacroTileMgr()
    {
        for (auto* pTile : mTiles)
        {
            delete pTile;
        }
    }

    INLINE void initialize()
    {
        mWorkItemsProduced = 0;
        mWorkItemsConsumed = 0;

        mDirtyTiles.clear();
    }

    INLINE std::vector<MacroTileQueue*>& getDirtyTiles() { return mDirtyTiles; }
    void                                 markTileComplete(uint32_t id);

    INLINE bool isWorkComplete() { return mWorkItemsProduced == mWorkItemsConsumed; }

    void enqueue(uint32_t x, uint32_t y, BE_WORK* pWork);

    static INLINE void getTileIndices(uint32_t tileID, uint32_t& x, uint32_t& y)
    {
        // Morton / Z order of tiles
        x = pext_u32(tileID, 0x55555555);
        y = pext_u32(tileID, 0xAAAAAAAA);
    }

    static INLINE uint32_t getTileId(uint32_t x, uint32_t y)
    {
        // Morton / Z order of tiles
        return pdep_u32(x, 0x55555555) | pdep_u32(y, 0xAAAAAAAA);
    }

private:
    CachingArena&                mArena;
    std::vector<MacroTileQueue*> mTiles;

    // Any tile that has work queued to it is a dirty tile.
    std::vector<MacroTileQueue*> mDirtyTiles;

    OSALIGNLINE(long) mWorkItemsProduced{0};
    OSALIGNLINE(volatile long) mWorkItemsConsumed{0};
};

typedef void (*PFN_DISPATCH)(DRAW_CONTEXT* pDC,
                             uint32_t      workerId,
                             uint32_t      threadGroupId,
                             void*&        pSpillFillBuffer,
                             void*&        pScratchSpace);

//////////////////////////////////////////////////////////////////////////
/// DispatchQueue - work queue for dispatch
//////////////////////////////////////////////////////////////////////////
class DispatchQueue
{
public:
    DispatchQueue() {}

    //////////////////////////////////////////////////////////////////////////
    /// @brief Setup the producer consumer counts.
    void initialize(uint32_t totalTasks, void* pTaskData, PFN_DISPATCH pfnDispatch)
    {
        // The available and outstanding counts start with total tasks.
        // At the start there are N tasks available and outstanding.
        // When both the available and outstanding counts have reached 0 then all work has
        // completed. When a worker starts on a threadgroup then it decrements the available count.
        // When a worker completes a threadgroup then it decrements the outstanding count.

        mTasksAvailable   = totalTasks;
        mTasksOutstanding = totalTasks;

        mpTaskData   = pTaskData;
        mPfnDispatch = pfnDispatch;
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Returns number of tasks available for this dispatch.
    uint32_t getNumQueued() { return (mTasksAvailable > 0) ? mTasksAvailable : 0; }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Atomically decrement the work available count. If the result
    //         is greater than 0 then we can on the associated thread group.
    //         Otherwise, there is no more work to do.
    bool getWork(uint32_t& groupId)
    {
        long result = InterlockedDecrement(&mTasksAvailable);

        if (result >= 0)
        {
            groupId = result;
            return true;
        }

        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Atomically decrement the outstanding count. A worker is notifying
    ///        us that he just finished some work. Also, return true if we're
    ///        the last worker to complete this dispatch.
    bool finishedWork()
    {
        long result = InterlockedDecrement(&mTasksOutstanding);
        SWR_ASSERT(result >= 0, "Should never oversubscribe work");

        return (result == 0) ? true : false;
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Work is complete once both the available/outstanding counts have reached 0.
    bool isWorkComplete() { return ((mTasksAvailable <= 0) && (mTasksOutstanding <= 0)); }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Return pointer to task data.
    const void* GetTasksData() { return mpTaskData; }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Dispatches a unit of work
    void dispatch(DRAW_CONTEXT* pDC,
                  uint32_t      workerId,
                  uint32_t      threadGroupId,
                  void*&        pSpillFillBuffer,
                  void*&        pScratchSpace)
    {
        SWR_ASSERT(mPfnDispatch != nullptr);
        mPfnDispatch(pDC, workerId, threadGroupId, pSpillFillBuffer, pScratchSpace);
    }

    void* mpTaskData{nullptr}; // The API thread will set this up and the callback task function
                               // will interpet this.
    PFN_DISPATCH mPfnDispatch{nullptr}; // Function to call per dispatch

    OSALIGNLINE(volatile long) mTasksAvailable{0};
    OSALIGNLINE(volatile long) mTasksOutstanding{0};
};

/// @note this enum needs to be kept in sync with SWR_TILE_STATE!
enum HOTTILE_STATE
{
    HOTTILE_INVALID,  // tile is in uninitialized state and should be loaded with surface contents
                      // before rendering
    HOTTILE_CLEAR,    // tile should be cleared
    HOTTILE_DIRTY,    // tile has been rendered to
    HOTTILE_RESOLVED, // tile is consistent with memory (either loaded or stored)
};

struct HOTTILE
{
    uint8_t*      pBuffer;
    HOTTILE_STATE state;
    uint32_t clearData[4]; // May need to change based on pfnClearTile implementation.  Reorder for
                        // alignment?
    uint32_t numSamples;
    uint32_t renderTargetArrayIndex; // current render target array index loaded
};

union HotTileSet
{
    struct
    {
        HOTTILE Color[SWR_NUM_RENDERTARGETS];
        HOTTILE Depth;
        HOTTILE Stencil;
    };
    HOTTILE Attachment[SWR_NUM_ATTACHMENTS];
};

class HotTileMgr
{
public:
    HotTileMgr()
    {
        memset(mHotTiles, 0, sizeof(mHotTiles));

        // cache hottile size
        for (uint32_t i = SWR_ATTACHMENT_COLOR0; i <= SWR_ATTACHMENT_COLOR7; ++i)
        {
            mHotTileSize[i] = KNOB_MACROTILE_X_DIM * KNOB_MACROTILE_Y_DIM *
                              FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8;
        }
        mHotTileSize[SWR_ATTACHMENT_DEPTH] = KNOB_MACROTILE_X_DIM * KNOB_MACROTILE_Y_DIM *
                                             FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8;
        mHotTileSize[SWR_ATTACHMENT_STENCIL] = KNOB_MACROTILE_X_DIM * KNOB_MACROTILE_Y_DIM *
                                               FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8;
    }

    ~HotTileMgr()
    {
        for (int x = 0; x < KNOB_NUM_HOT_TILES_X; ++x)
        {
            for (int y = 0; y < KNOB_NUM_HOT_TILES_Y; ++y)
            {
                for (int a = 0; a < SWR_NUM_ATTACHMENTS; ++a)
                {
                    FreeHotTileMem(mHotTiles[x][y].Attachment[a].pBuffer);
                }
            }
        }
    }

    void InitializeHotTiles(SWR_CONTEXT*  pContext,
                            DRAW_CONTEXT* pDC,
                            uint32_t      workerId,
                            uint32_t      macroID);

    HOTTILE* GetHotTile(SWR_CONTEXT*                pContext,
                        DRAW_CONTEXT*               pDC,
                        HANDLE                      hWorkerData,
                        uint32_t                    macroID,
                        SWR_RENDERTARGET_ATTACHMENT attachment,
                        bool                        create,
                        uint32_t                    numSamples             = 1,
                        uint32_t                    renderTargetArrayIndex = 0);

    HOTTILE* GetHotTileNoLoad(SWR_CONTEXT*                pContext,
                              DRAW_CONTEXT*               pDC,
                              uint32_t                    macroID,
                              SWR_RENDERTARGET_ATTACHMENT attachment,
                              bool                        create,
                              uint32_t                    numSamples = 1);

    static void ClearColorHotTile(const HOTTILE* pHotTile);
    static void ClearDepthHotTile(const HOTTILE* pHotTile);
    static void ClearStencilHotTile(const HOTTILE* pHotTile);

private:
    HotTileSet mHotTiles[KNOB_NUM_HOT_TILES_X][KNOB_NUM_HOT_TILES_Y];
    uint32_t   mHotTileSize[SWR_NUM_ATTACHMENTS];

    void* AllocHotTileMem(size_t size, uint32_t align, uint32_t numaNode)
    {
        void* p = nullptr;
#if defined(_WIN32)
        HANDLE hProcess = GetCurrentProcess();
        p               = VirtualAllocExNuma(
            hProcess, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE, numaNode);
#else
        p = AlignedMalloc(size, align);
#endif

        return p;
    }

    void FreeHotTileMem(void* pBuffer)
    {
        if (pBuffer)
        {
#if defined(_WIN32)
            VirtualFree(pBuffer, 0, MEM_RELEASE);
#else
            AlignedFree(pBuffer);
#endif
        }
    }
};
