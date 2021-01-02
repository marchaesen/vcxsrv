/****************************************************************************
 * Copyright (C) 2016 Intel Corporation.   All Rights Reserved.
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
 * @file archrast.cpp
 *
 * @brief Implementation for archrast.
 *
 ******************************************************************************/
#include <sys/stat.h>

#include <atomic>
#include <map>

#include "common/os.h"
#include "archrast/archrast.h"
#include "archrast/eventmanager.h"
#include "gen_ar_event.hpp"
#include "gen_ar_eventhandlerfile.hpp"

namespace ArchRast
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief struct that keeps track of depth and stencil event information
    struct DepthStencilStats
    {
        uint32_t earlyZTestPassCount       = 0;
        uint32_t earlyZTestFailCount       = 0;
        uint32_t lateZTestPassCount        = 0;
        uint32_t lateZTestFailCount        = 0;
        uint32_t earlyStencilTestPassCount = 0;
        uint32_t earlyStencilTestFailCount = 0;
        uint32_t lateStencilTestPassCount  = 0;
        uint32_t lateStencilTestFailCount  = 0;
    };

    struct CStats
    {
        uint32_t trivialRejectCount;
        uint32_t trivialAcceptCount;
        uint32_t mustClipCount;
    };

    struct TEStats
    {
        uint32_t inputPrims = 0;
        //@todo:: Change this to numPatches. Assumed: 1 patch per prim. If holds, its fine.
    };

    struct GSStateInfo
    {
        uint32_t inputPrimCount;
        uint32_t primGeneratedCount;
        uint32_t vertsInput;
    };

    struct RastStats
    {
        uint32_t rasterTiles = 0;
    };

    struct CullStats
    {
        uint32_t degeneratePrimCount = 0;
        uint32_t backfacePrimCount   = 0;
    };

    struct AlphaStats
    {
        uint32_t alphaTestCount  = 0;
        uint32_t alphaBlendCount = 0;
    };


    //////////////////////////////////////////////////////////////////////////
    /// @brief Event handler that handles API thread events. This is shared
    ///        between the API and its caller (e.g. driver shim) but typically
    ///        there is only a single API thread per context. So you can save
    ///        information in the class to be used for other events.
    class EventHandlerApiStats : public EventHandlerFile
    {
    public:
        EventHandlerApiStats(uint32_t id) : EventHandlerFile(id)
        {
#if defined(_WIN32)
            // Attempt to copy the events.proto file to the ArchRast output dir. It's common for
            // tools to place the events.proto file in the DEBUG_OUTPUT_DIR when launching AR. If it
            // exists, this will attempt to copy it the first time we get here to package it with
            // the stats. Otherwise, the user would need to specify the events.proto location when
            // parsing the stats in post.
            std::stringstream eventsProtoSrcFilename, eventsProtoDstFilename;
            eventsProtoSrcFilename << KNOB_DEBUG_OUTPUT_DIR << "\\events.proto" << std::ends;
            eventsProtoDstFilename << mOutputDir.substr(0, mOutputDir.size() - 1)
                                   << "\\events.proto" << std::ends;

            // If event.proto already exists, we're done; else do the copy
            struct stat buf; // Use a Posix stat for file existence check
            if (!stat(eventsProtoDstFilename.str().c_str(), &buf) == 0)
            {
                // Now check to make sure the events.proto source exists
                if (stat(eventsProtoSrcFilename.str().c_str(), &buf) == 0)
                {
                    std::ifstream srcFile;
                    srcFile.open(eventsProtoSrcFilename.str().c_str(), std::ios::binary);
                    if (srcFile.is_open())
                    {
                        // Just do a binary buffer copy
                        std::ofstream dstFile;
                        dstFile.open(eventsProtoDstFilename.str().c_str(), std::ios::binary);
                        dstFile << srcFile.rdbuf();
                        dstFile.close();
                    }
                    srcFile.close();
                }
            }
#endif
        }

        virtual void Handle(const DrawInstancedEvent& event)
        {
            DrawInfoEvent e(event.data.drawId,
                            ArchRast::Instanced,
                            event.data.topology,
                            event.data.numVertices,
                            0,
                            0,
                            event.data.startVertex,
                            event.data.numInstances,
                            event.data.startInstance,
                            event.data.tsEnable,
                            event.data.gsEnable,
                            event.data.soEnable,
                            event.data.soTopology,
                            event.data.splitId);

            EventHandlerFile::Handle(e);
        }

        virtual void Handle(const DrawIndexedInstancedEvent& event)
        {
            DrawInfoEvent e(event.data.drawId,
                            ArchRast::IndexedInstanced,
                            event.data.topology,
                            0,
                            event.data.numIndices,
                            event.data.indexOffset,
                            event.data.baseVertex,
                            event.data.numInstances,
                            event.data.startInstance,
                            event.data.tsEnable,
                            event.data.gsEnable,
                            event.data.soEnable,
                            event.data.soTopology,
                            event.data.splitId);

            EventHandlerFile::Handle(e);
        }
    };

    //////////////////////////////////////////////////////////////////////////
    /// @brief Event handler that handles worker thread events. There is one
    ///        event handler per thread. The python script will need to sum
    ///        up counters across all of the threads.
    class EventHandlerWorkerStats : public EventHandlerFile
    {
    public:
        EventHandlerWorkerStats(uint32_t id) : EventHandlerFile(id), mNeedFlush(false)
        {
            memset(mShaderStats, 0, sizeof(mShaderStats));
        }

        virtual void Handle(const EarlyDepthStencilInfoSingleSample& event)
        {
            // earlyZ test compute
            mDSSingleSample.earlyZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            mDSSingleSample.earlyZTestFailCount +=
                _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);

            // earlyStencil test compute
            mDSSingleSample.earlyStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            mDSSingleSample.earlyStencilTestFailCount +=
                _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);

            // earlyZ test single and multi sample
            mDSCombined.earlyZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            mDSCombined.earlyZTestFailCount +=
                _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);

            // earlyStencil test single and multi sample
            mDSCombined.earlyStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            mDSCombined.earlyStencilTestFailCount +=
                _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);

            mNeedFlush = true;
        }

        virtual void Handle(const EarlyDepthStencilInfoSampleRate& event)
        {
            // earlyZ test compute
            mDSSampleRate.earlyZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            mDSSampleRate.earlyZTestFailCount +=
                _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);

            // earlyStencil test compute
            mDSSampleRate.earlyStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            mDSSampleRate.earlyStencilTestFailCount +=
                _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);

            // earlyZ test single and multi sample
            mDSCombined.earlyZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            mDSCombined.earlyZTestFailCount +=
                _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);

            // earlyStencil test single and multi sample
            mDSCombined.earlyStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            mDSCombined.earlyStencilTestFailCount +=
                _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);

            mNeedFlush = true;
        }

        virtual void Handle(const EarlyDepthStencilInfoNullPS& event)
        {
            // earlyZ test compute
            mDSNullPS.earlyZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            mDSNullPS.earlyZTestFailCount +=
                _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);

            // earlyStencil test compute
            mDSNullPS.earlyStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            mDSNullPS.earlyStencilTestFailCount +=
                _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);
            mNeedFlush = true;
        }

        virtual void Handle(const LateDepthStencilInfoSingleSample& event)
        {
            // lateZ test compute
            mDSSingleSample.lateZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            mDSSingleSample.lateZTestFailCount +=
                _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);

            // lateStencil test compute
            mDSSingleSample.lateStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            mDSSingleSample.lateStencilTestFailCount +=
                _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);

            // lateZ test single and multi sample
            mDSCombined.lateZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            mDSCombined.lateZTestFailCount +=
                _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);

            // lateStencil test single and multi sample
            mDSCombined.lateStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            mDSCombined.lateStencilTestFailCount +=
                _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);

            mNeedFlush = true;
        }

        virtual void Handle(const LateDepthStencilInfoSampleRate& event)
        {
            // lateZ test compute
            mDSSampleRate.lateZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            mDSSampleRate.lateZTestFailCount +=
                _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);

            // lateStencil test compute
            mDSSampleRate.lateStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            mDSSampleRate.lateStencilTestFailCount +=
                _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);

            // lateZ test single and multi sample
            mDSCombined.lateZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            mDSCombined.lateZTestFailCount +=
                _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);

            // lateStencil test single and multi sample
            mDSCombined.lateStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            mDSCombined.lateStencilTestFailCount +=
                _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);

            mNeedFlush = true;
        }

        virtual void Handle(const LateDepthStencilInfoNullPS& event)
        {
            // lateZ test compute
            mDSNullPS.lateZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            mDSNullPS.lateZTestFailCount +=
                _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);

            // lateStencil test compute
            mDSNullPS.lateStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            mDSNullPS.lateStencilTestFailCount +=
                _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);
            mNeedFlush = true;
        }

        virtual void Handle(const EarlyDepthInfoPixelRate& event)
        {
            // earlyZ test compute
            mDSPixelRate.earlyZTestPassCount += event.data.depthPassCount;
            mDSPixelRate.earlyZTestFailCount +=
                (_mm_popcnt_u32(event.data.activeLanes) - event.data.depthPassCount);
            mNeedFlush = true;
        }


        virtual void Handle(const LateDepthInfoPixelRate& event)
        {
            // lateZ test compute
            mDSPixelRate.lateZTestPassCount += event.data.depthPassCount;
            mDSPixelRate.lateZTestFailCount +=
                (_mm_popcnt_u32(event.data.activeLanes) - event.data.depthPassCount);
            mNeedFlush = true;
        }


        virtual void Handle(const ClipInfoEvent& event)
        {
            mClipper.mustClipCount += _mm_popcnt_u32(event.data.clipMask);
            mClipper.trivialRejectCount +=
                event.data.numInvocations - _mm_popcnt_u32(event.data.validMask);
            mClipper.trivialAcceptCount +=
                _mm_popcnt_u32(event.data.validMask & ~event.data.clipMask);
        }

        void UpdateStats(SWR_SHADER_STATS* pStatTotals, const SWR_SHADER_STATS* pStatUpdate)
        {
            pStatTotals->numInstExecuted += pStatUpdate->numInstExecuted;
            pStatTotals->numSampleExecuted += pStatUpdate->numSampleExecuted;
            pStatTotals->numSampleLExecuted += pStatUpdate->numSampleLExecuted;
            pStatTotals->numSampleBExecuted += pStatUpdate->numSampleBExecuted;
            pStatTotals->numSampleCExecuted += pStatUpdate->numSampleCExecuted;
            pStatTotals->numSampleCLZExecuted += pStatUpdate->numSampleCLZExecuted;
            pStatTotals->numSampleCDExecuted += pStatUpdate->numSampleCDExecuted;
            pStatTotals->numGather4Executed += pStatUpdate->numGather4Executed;
            pStatTotals->numGather4CExecuted += pStatUpdate->numGather4CExecuted;
            pStatTotals->numGather4CPOExecuted += pStatUpdate->numGather4CPOExecuted;
            pStatTotals->numGather4CPOCExecuted += pStatUpdate->numGather4CPOCExecuted;
            pStatTotals->numLodExecuted += pStatUpdate->numLodExecuted;
        }

        virtual void Handle(const VSStats& event)
        {
            SWR_SHADER_STATS* pStats = (SWR_SHADER_STATS*)event.data.hStats;
            UpdateStats(&mShaderStats[SHADER_VERTEX], pStats);
        }

        virtual void Handle(const GSStats& event)
        {
            SWR_SHADER_STATS* pStats = (SWR_SHADER_STATS*)event.data.hStats;
            UpdateStats(&mShaderStats[SHADER_GEOMETRY], pStats);
        }

        virtual void Handle(const DSStats& event)
        {
            SWR_SHADER_STATS* pStats = (SWR_SHADER_STATS*)event.data.hStats;
            UpdateStats(&mShaderStats[SHADER_DOMAIN], pStats);
        }

        virtual void Handle(const HSStats& event)
        {
            SWR_SHADER_STATS* pStats = (SWR_SHADER_STATS*)event.data.hStats;
            UpdateStats(&mShaderStats[SHADER_HULL], pStats);
        }

        virtual void Handle(const PSStats& event)
        {
            SWR_SHADER_STATS* pStats = (SWR_SHADER_STATS*)event.data.hStats;
            UpdateStats(&mShaderStats[SHADER_PIXEL], pStats);
            mNeedFlush = true;
        }

        virtual void Handle(const CSStats& event)
        {
            SWR_SHADER_STATS* pStats = (SWR_SHADER_STATS*)event.data.hStats;
            UpdateStats(&mShaderStats[SHADER_COMPUTE], pStats);
            mNeedFlush = true;
        }

        // Flush cached events for this draw
        virtual void FlushDraw(uint32_t drawId)
        {
            if (mNeedFlush == false)
                return;

            EventHandlerFile::Handle(PSInfo(drawId,
                                            mShaderStats[SHADER_PIXEL].numInstExecuted,
                                            mShaderStats[SHADER_PIXEL].numSampleExecuted,
                                            mShaderStats[SHADER_PIXEL].numSampleLExecuted,
                                            mShaderStats[SHADER_PIXEL].numSampleBExecuted,
                                            mShaderStats[SHADER_PIXEL].numSampleCExecuted,
                                            mShaderStats[SHADER_PIXEL].numSampleCLZExecuted,
                                            mShaderStats[SHADER_PIXEL].numSampleCDExecuted,
                                            mShaderStats[SHADER_PIXEL].numGather4Executed,
                                            mShaderStats[SHADER_PIXEL].numGather4CExecuted,
                                            mShaderStats[SHADER_PIXEL].numGather4CPOExecuted,
                                            mShaderStats[SHADER_PIXEL].numGather4CPOCExecuted,
                                            mShaderStats[SHADER_PIXEL].numLodExecuted));
            EventHandlerFile::Handle(CSInfo(drawId,
                                            mShaderStats[SHADER_COMPUTE].numInstExecuted,
                                            mShaderStats[SHADER_COMPUTE].numSampleExecuted,
                                            mShaderStats[SHADER_COMPUTE].numSampleLExecuted,
                                            mShaderStats[SHADER_COMPUTE].numSampleBExecuted,
                                            mShaderStats[SHADER_COMPUTE].numSampleCExecuted,
                                            mShaderStats[SHADER_COMPUTE].numSampleCLZExecuted,
                                            mShaderStats[SHADER_COMPUTE].numSampleCDExecuted,
                                            mShaderStats[SHADER_COMPUTE].numGather4Executed,
                                            mShaderStats[SHADER_COMPUTE].numGather4CExecuted,
                                            mShaderStats[SHADER_COMPUTE].numGather4CPOExecuted,
                                            mShaderStats[SHADER_COMPUTE].numGather4CPOCExecuted,
                                            mShaderStats[SHADER_COMPUTE].numLodExecuted));

            // singleSample
            EventHandlerFile::Handle(EarlyZSingleSample(
                drawId, mDSSingleSample.earlyZTestPassCount, mDSSingleSample.earlyZTestFailCount));
            EventHandlerFile::Handle(LateZSingleSample(
                drawId, mDSSingleSample.lateZTestPassCount, mDSSingleSample.lateZTestFailCount));
            EventHandlerFile::Handle(
                EarlyStencilSingleSample(drawId,
                                         mDSSingleSample.earlyStencilTestPassCount,
                                         mDSSingleSample.earlyStencilTestFailCount));
            EventHandlerFile::Handle(
                LateStencilSingleSample(drawId,
                                        mDSSingleSample.lateStencilTestPassCount,
                                        mDSSingleSample.lateStencilTestFailCount));

            // sampleRate
            EventHandlerFile::Handle(EarlyZSampleRate(
                drawId, mDSSampleRate.earlyZTestPassCount, mDSSampleRate.earlyZTestFailCount));
            EventHandlerFile::Handle(LateZSampleRate(
                drawId, mDSSampleRate.lateZTestPassCount, mDSSampleRate.lateZTestFailCount));
            EventHandlerFile::Handle(
                EarlyStencilSampleRate(drawId,
                                       mDSSampleRate.earlyStencilTestPassCount,
                                       mDSSampleRate.earlyStencilTestFailCount));
            EventHandlerFile::Handle(LateStencilSampleRate(drawId,
                                                           mDSSampleRate.lateStencilTestPassCount,
                                                           mDSSampleRate.lateStencilTestFailCount));

            // combined
            EventHandlerFile::Handle(
                EarlyZ(drawId, mDSCombined.earlyZTestPassCount, mDSCombined.earlyZTestFailCount));
            EventHandlerFile::Handle(
                LateZ(drawId, mDSCombined.lateZTestPassCount, mDSCombined.lateZTestFailCount));
            EventHandlerFile::Handle(EarlyStencil(drawId,
                                                  mDSCombined.earlyStencilTestPassCount,
                                                  mDSCombined.earlyStencilTestFailCount));
            EventHandlerFile::Handle(LateStencil(drawId,
                                                 mDSCombined.lateStencilTestPassCount,
                                                 mDSCombined.lateStencilTestFailCount));

            // pixelRate
            EventHandlerFile::Handle(EarlyZPixelRate(
                drawId, mDSPixelRate.earlyZTestPassCount, mDSPixelRate.earlyZTestFailCount));
            EventHandlerFile::Handle(LateZPixelRate(
                drawId, mDSPixelRate.lateZTestPassCount, mDSPixelRate.lateZTestFailCount));


            // NullPS
            EventHandlerFile::Handle(
                EarlyZNullPS(drawId, mDSNullPS.earlyZTestPassCount, mDSNullPS.earlyZTestFailCount));
            EventHandlerFile::Handle(EarlyStencilNullPS(
                drawId, mDSNullPS.earlyStencilTestPassCount, mDSNullPS.earlyStencilTestFailCount));

            // Rasterized Subspans
            EventHandlerFile::Handle(RasterTiles(drawId, rastStats.rasterTiles));

            // Alpha Subspans
            EventHandlerFile::Handle(
                AlphaEvent(drawId, mAlphaStats.alphaTestCount, mAlphaStats.alphaBlendCount));

            // Primitive Culling
            EventHandlerFile::Handle(
                CullEvent(drawId, mCullStats.backfacePrimCount, mCullStats.degeneratePrimCount));

            mDSSingleSample = {};
            mDSSampleRate   = {};
            mDSCombined     = {};
            mDSPixelRate    = {};
            mDSNullPS = {};

            rastStats   = {};
            mCullStats  = {};
            mAlphaStats = {};

            mShaderStats[SHADER_PIXEL]   = {};
            mShaderStats[SHADER_COMPUTE] = {};

            mNeedFlush = false;
        }

        virtual void Handle(const FrontendDrawEndEvent& event)
        {
            // Clipper
            EventHandlerFile::Handle(ClipperEvent(event.data.drawId,
                                                  mClipper.trivialRejectCount,
                                                  mClipper.trivialAcceptCount,
                                                  mClipper.mustClipCount));

            // Tesselator
            EventHandlerFile::Handle(TessPrims(event.data.drawId, mTS.inputPrims));

            // Geometry Shader
            EventHandlerFile::Handle(GSInputPrims(event.data.drawId, mGS.inputPrimCount));
            EventHandlerFile::Handle(GSPrimsGen(event.data.drawId, mGS.primGeneratedCount));
            EventHandlerFile::Handle(GSVertsInput(event.data.drawId, mGS.vertsInput));

            EventHandlerFile::Handle(VSInfo(event.data.drawId,
                                            mShaderStats[SHADER_VERTEX].numInstExecuted,
                                            mShaderStats[SHADER_VERTEX].numSampleExecuted,
                                            mShaderStats[SHADER_VERTEX].numSampleLExecuted,
                                            mShaderStats[SHADER_VERTEX].numSampleBExecuted,
                                            mShaderStats[SHADER_VERTEX].numSampleCExecuted,
                                            mShaderStats[SHADER_VERTEX].numSampleCLZExecuted,
                                            mShaderStats[SHADER_VERTEX].numSampleCDExecuted,
                                            mShaderStats[SHADER_VERTEX].numGather4Executed,
                                            mShaderStats[SHADER_VERTEX].numGather4CExecuted,
                                            mShaderStats[SHADER_VERTEX].numGather4CPOExecuted,
                                            mShaderStats[SHADER_VERTEX].numGather4CPOCExecuted,
                                            mShaderStats[SHADER_VERTEX].numLodExecuted));
            EventHandlerFile::Handle(HSInfo(event.data.drawId,
                                            mShaderStats[SHADER_HULL].numInstExecuted,
                                            mShaderStats[SHADER_HULL].numSampleExecuted,
                                            mShaderStats[SHADER_HULL].numSampleLExecuted,
                                            mShaderStats[SHADER_HULL].numSampleBExecuted,
                                            mShaderStats[SHADER_HULL].numSampleCExecuted,
                                            mShaderStats[SHADER_HULL].numSampleCLZExecuted,
                                            mShaderStats[SHADER_HULL].numSampleCDExecuted,
                                            mShaderStats[SHADER_HULL].numGather4Executed,
                                            mShaderStats[SHADER_HULL].numGather4CExecuted,
                                            mShaderStats[SHADER_HULL].numGather4CPOExecuted,
                                            mShaderStats[SHADER_HULL].numGather4CPOCExecuted,
                                            mShaderStats[SHADER_HULL].numLodExecuted));
            EventHandlerFile::Handle(DSInfo(event.data.drawId,
                                            mShaderStats[SHADER_DOMAIN].numInstExecuted,
                                            mShaderStats[SHADER_DOMAIN].numSampleExecuted,
                                            mShaderStats[SHADER_DOMAIN].numSampleLExecuted,
                                            mShaderStats[SHADER_DOMAIN].numSampleBExecuted,
                                            mShaderStats[SHADER_DOMAIN].numSampleCExecuted,
                                            mShaderStats[SHADER_DOMAIN].numSampleCLZExecuted,
                                            mShaderStats[SHADER_DOMAIN].numSampleCDExecuted,
                                            mShaderStats[SHADER_DOMAIN].numGather4Executed,
                                            mShaderStats[SHADER_DOMAIN].numGather4CExecuted,
                                            mShaderStats[SHADER_DOMAIN].numGather4CPOExecuted,
                                            mShaderStats[SHADER_DOMAIN].numGather4CPOCExecuted,
                                            mShaderStats[SHADER_DOMAIN].numLodExecuted));
            EventHandlerFile::Handle(GSInfo(event.data.drawId,
                                            mShaderStats[SHADER_GEOMETRY].numInstExecuted,
                                            mShaderStats[SHADER_GEOMETRY].numSampleExecuted,
                                            mShaderStats[SHADER_GEOMETRY].numSampleLExecuted,
                                            mShaderStats[SHADER_GEOMETRY].numSampleBExecuted,
                                            mShaderStats[SHADER_GEOMETRY].numSampleCExecuted,
                                            mShaderStats[SHADER_GEOMETRY].numSampleCLZExecuted,
                                            mShaderStats[SHADER_GEOMETRY].numSampleCDExecuted,
                                            mShaderStats[SHADER_GEOMETRY].numGather4Executed,
                                            mShaderStats[SHADER_GEOMETRY].numGather4CExecuted,
                                            mShaderStats[SHADER_GEOMETRY].numGather4CPOExecuted,
                                            mShaderStats[SHADER_GEOMETRY].numGather4CPOCExecuted,
                                            mShaderStats[SHADER_GEOMETRY].numLodExecuted));

            mShaderStats[SHADER_VERTEX]   = {};
            mShaderStats[SHADER_HULL]     = {};
            mShaderStats[SHADER_DOMAIN]   = {};
            mShaderStats[SHADER_GEOMETRY] = {};

            // Reset Internal Counters
            mClipper = {};
            mTS      = {};
            mGS      = {};
        }

        virtual void Handle(const GSPrimInfo& event)
        {
            mGS.inputPrimCount += event.data.inputPrimCount;
            mGS.primGeneratedCount += event.data.primGeneratedCount;
            mGS.vertsInput += event.data.vertsInput;
        }

        virtual void Handle(const TessPrimCount& event) { mTS.inputPrims += event.data.primCount; }

        virtual void Handle(const RasterTileCount& event)
        {
            rastStats.rasterTiles += event.data.rasterTiles;
        }

        virtual void Handle(const CullInfoEvent& event)
        {
            mCullStats.degeneratePrimCount += _mm_popcnt_u32(
                event.data.validMask ^ (event.data.validMask & ~event.data.degeneratePrimMask));
            mCullStats.backfacePrimCount += _mm_popcnt_u32(
                event.data.validMask ^ (event.data.validMask & ~event.data.backfacePrimMask));
        }

        virtual void Handle(const AlphaInfoEvent& event)
        {
            mAlphaStats.alphaTestCount += event.data.alphaTestEnable;
            mAlphaStats.alphaBlendCount += event.data.alphaBlendEnable;
        }

    protected:
        bool mNeedFlush;
        // Per draw stats
        DepthStencilStats mDSSingleSample = {};
        DepthStencilStats mDSSampleRate   = {};
        DepthStencilStats mDSPixelRate    = {};
        DepthStencilStats mDSCombined     = {};
        DepthStencilStats mDSNullPS       = {};
        DepthStencilStats mDSOmZ          = {};
        CStats            mClipper        = {};
        TEStats           mTS             = {};
        GSStateInfo       mGS             = {};
        RastStats         rastStats       = {};
        CullStats         mCullStats      = {};
        AlphaStats        mAlphaStats     = {};

        SWR_SHADER_STATS mShaderStats[NUM_SHADER_TYPES];

    };

    static EventManager* FromHandle(HANDLE hThreadContext)
    {
        return reinterpret_cast<EventManager*>(hThreadContext);
    }

    // Construct an event manager and associate a handler with it.
    HANDLE CreateThreadContext(AR_THREAD type)
    {
        // Can we assume single threaded here?
        static std::atomic<uint32_t> counter(0);
        uint32_t                     id = counter.fetch_add(1);

        EventManager* pManager = new EventManager();

        if (pManager)
        {
            EventHandlerFile* pHandler = nullptr;

            if (type == AR_THREAD::API)
            {
                pHandler = new EventHandlerApiStats(id);
                pManager->Attach(pHandler);
                pHandler->Handle(ThreadStartApiEvent());
            }
            else
            {
                pHandler = new EventHandlerWorkerStats(id);
                pManager->Attach(pHandler);
                pHandler->Handle(ThreadStartWorkerEvent());
            }

            pHandler->MarkHeader();

            return pManager;
        }

        SWR_INVALID("Failed to register thread.");
        return nullptr;
    }

    void DestroyThreadContext(HANDLE hThreadContext)
    {
        EventManager* pManager = FromHandle(hThreadContext);
        SWR_ASSERT(pManager != nullptr);

        delete pManager;
    }

    // Dispatch event for this thread.
    void Dispatch(HANDLE hThreadContext, const Event& event)
    {
        if (event.IsEnabled())
        {
            EventManager* pManager = reinterpret_cast<EventManager*>(hThreadContext);
            SWR_ASSERT(pManager != nullptr);
            pManager->Dispatch(event);
        }
    }

    // Flush for this thread.
    void FlushDraw(HANDLE hThreadContext, uint32_t drawId)
    {
        EventManager* pManager = FromHandle(hThreadContext);
        SWR_ASSERT(pManager != nullptr);

        pManager->FlushDraw(drawId);
    }
} // namespace ArchRast
