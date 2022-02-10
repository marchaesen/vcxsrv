/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef D3D12_RESOURCE_STATE_H
#define D3D12_RESOURCE_STATE_H

#ifndef _WIN32
#include <wsl/winadapter.h>
#endif

#include <vector>
#include <assert.h>
#include <directx/d3d12.h>

#include "util/list.h"

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#define UNKNOWN_RESOURCE_STATE (D3D12_RESOURCE_STATES)0x8000u
#define RESOURCE_STATE_VALID_BITS 0x2f3fff
#define RESOURCE_STATE_VALID_INTERNAL_BITS 0x2fffff
constexpr D3D12_RESOURCE_STATES RESOURCE_STATE_ALL_WRITE_BITS =
D3D12_RESOURCE_STATE_RENDER_TARGET          |
D3D12_RESOURCE_STATE_UNORDERED_ACCESS       |
D3D12_RESOURCE_STATE_DEPTH_WRITE            |
D3D12_RESOURCE_STATE_STREAM_OUT             |
D3D12_RESOURCE_STATE_COPY_DEST              |
D3D12_RESOURCE_STATE_RESOLVE_DEST           |
D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE     |
D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE;

//---------------------------------------------------------------------------------------------------------------------------------
inline bool IsD3D12WriteState(UINT State)
{
   return (State & RESOURCE_STATE_ALL_WRITE_BITS) != 0;
}

inline bool SupportsSimultaneousAccess(const D3D12_RESOURCE_DESC &desc)
{
   return D3D12_RESOURCE_DIMENSION_BUFFER == desc.Dimension ||
          !!(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);
}

//==================================================================================================================================
// CDesiredResourceState
// Stores the current desired state of either an entire resource, or each subresource.
//==================================================================================================================================
class CDesiredResourceState
{
private:
   bool m_bAllSubresourcesSame = true;

   std::vector<D3D12_RESOURCE_STATES> m_spSubresourceStates;

public:
   CDesiredResourceState(UINT SubresourceCount) :
      m_spSubresourceStates(SubresourceCount)
   {
   }

   bool AreAllSubresourcesSame() const { return m_bAllSubresourcesSame; }

   D3D12_RESOURCE_STATES GetSubresourceState(UINT SubresourceIndex) const;
   void SetResourceState(D3D12_RESOURCE_STATES state);
   void SetSubresourceState(UINT SubresourceIndex, D3D12_RESOURCE_STATES state);

   void Reset();

private:
   void UpdateSubresourceState(unsigned SubresourceIndex, D3D12_RESOURCE_STATES state);
};

//==================================================================================================================================
// CCurrentResourceState
// Stores the current state of either an entire resource, or each subresource.
// Current state can either be shared read across multiple queues, or exclusive on a single queue.
//==================================================================================================================================
class CCurrentResourceState
{
public:
   struct LogicalState
   {
      D3D12_RESOURCE_STATES State = D3D12_RESOURCE_STATE_COMMON;
      UINT64 ExecutionId = 0;
      bool IsPromotedState = false;
      bool MayDecay = false;
   };

private:
   const bool m_bSimultaneousAccess;
   bool m_bAllSubresourcesSame = true;

   std::vector<LogicalState> m_spLogicalState;

   void ConvertToSubresourceTracking();

public:
   CCurrentResourceState(UINT SubresourceCount, bool bSimultaneousAccess);

   bool SupportsSimultaneousAccess() const { return m_bSimultaneousAccess; }

   // Returns the destination state if the current state is promotable.
   // Returns D3D12_RESOURCE_STATE_COMMON if not.
   D3D12_RESOURCE_STATES StateIfPromoted(D3D12_RESOURCE_STATES state, UINT SubresourceIndex);

   bool AreAllSubresourcesSame() const { return m_bAllSubresourcesSame; }

   void SetLogicalResourceState(LogicalState const& State);
   void SetLogicalSubresourceState(UINT SubresourceIndex, LogicalState const& State);
   LogicalState const& GetLogicalSubresourceState(UINT SubresourceIndex) const;

   void Reset();
};
    
//==================================================================================================================================
// TransitionableResourceState
// A base class that transitionable resources should inherit from.
//==================================================================================================================================
struct TransitionableResourceState
{
   struct list_head m_TransitionListEntry;
   CDesiredResourceState m_DesiredState;

   TransitionableResourceState(ID3D12Resource *pResource, UINT TotalSubresources, bool SupportsSimultaneousAccess) :
      m_DesiredState(TotalSubresources),
      m_TotalSubresources(TotalSubresources),
      m_currentState(TotalSubresources, SupportsSimultaneousAccess),
      m_pResource(pResource)
   {
      list_inithead(&m_TransitionListEntry);
   }

   ~TransitionableResourceState()
   {
      if (IsTransitionPending())
      {
         list_del(&m_TransitionListEntry);
      }
   }

   bool IsTransitionPending() const { return !list_is_empty(&m_TransitionListEntry); }

   UINT NumSubresources() { return m_TotalSubresources; }

   CCurrentResourceState& GetCurrentState() { return m_currentState; }

   inline ID3D12Resource* GetD3D12Resource() const { return m_pResource; }

private:
   unsigned m_TotalSubresources;

   CCurrentResourceState m_currentState;

   ID3D12Resource* m_pResource;
};

//==================================================================================================================================
// ResourceStateManager
// The main business logic for handling resource transitions, including multi-queue sync and shared/exclusive state changes.
//
// Requesting a resource to transition simply updates destination state, and ensures it's in a list to be processed later.
//
// When processing ApplyAllResourceTransitions, we build up sets of vectors.
// There's a source one for each command list type, and a single one for the dest because we are applying
// the resource transitions for a single operation.
// There's also a vector for "tentative" barriers, which are merged into the destination vector if
// no flushing occurs as a result of submitting the final barrier operation.
// 99% of the time, there will only be the source being populated, but sometimes there will be a destination as well.
// If the source and dest of a transition require different types, we put a (source->COMMON) in the approriate source vector,
// and a (COMMON->dest) in the destination vector.
//
// Once all resources are processed, we:
// 1. Submit all source barriers, except ones belonging to the destination queue.
// 2. Flush all source command lists, except ones belonging to the destination queue.
// 3. Determine if the destination queue is going to be flushed.
//    If so: Submit source barriers on that command list first, then flush it.
//    If not: Accumulate source, dest, and tentative barriers so they can be sent to D3D12 in a single API call.
// 4. Insert waits on the destination queue - deferred waits, and waits for work on other queues.
// 5. Insert destination barriers.
//
// Only once all of this has been done do we update the "current" state of resources,
// because this is the only way that we know whether or not the destination queue has been flushed,
// and therefore, we can get the correct fence values to store in the subresources.
//==================================================================================================================================
class ResourceStateManager
{
protected:

   struct list_head m_TransitionListHead;

   std::vector<D3D12_RESOURCE_BARRIER> m_vResourceBarriers;
   bool m_IsImplicitDispatch;

public:
   ResourceStateManager();

   ~ResourceStateManager()
   {
      // All resources should be gone by this point, and each resource ensures it is no longer in this list.
      assert(list_is_empty(&m_TransitionListHead));
   }

   // Call the D3D12 APIs to perform the resource barriers, command list submission, and command queue sync
   // that was determined by previous calls to ProcessTransitioningResource.
   void SubmitResourceTransitions(ID3D12GraphicsCommandList *pCommandList);

   // Transition the entire resource to a particular destination state on a particular command list.
   void TransitionResource(TransitionableResourceState* pResource,
                           D3D12_RESOURCE_STATES State);
   // Transition a single subresource to a particular destination state.
   void TransitionSubresource(TransitionableResourceState* pResource,
                              UINT SubresourceIndex,
                              D3D12_RESOURCE_STATES State);

   // Submit all barriers and queue sync.
   void ApplyAllResourceTransitions(ID3D12GraphicsCommandList *pCommandList, UINT64 ExecutionId, bool IsImplicitDispatch);

private:
   // These methods set the destination state of the resource/subresources and ensure it's in the transition list.
   void TransitionResource(TransitionableResourceState& Resource,
                           D3D12_RESOURCE_STATES State);
   void TransitionSubresource(TransitionableResourceState& Resource,
                              UINT SubresourceIndex,
                              D3D12_RESOURCE_STATES State);

   // Clear out any state from previous iterations.
   void ApplyResourceTransitionsPreamble(bool IsImplicitDispatch);

   // What to do with the resource, in the context of the transition list, after processing it.
   enum class TransitionResult
   {
      // There are no more pending transitions that may be processed at a later time (i.e. draw time),
      // so remove it from the pending transition list.
      Remove,
      // There are more transitions to be done, so keep it in the list.
      Keep
   };

   // For every entry in the transition list, call a routine.
   // This routine must return a TransitionResult which indicates what to do with the list.
   template <typename TFunc>
   void ForEachTransitioningResource(TFunc&& func)
   {
      list_for_each_entry_safe(TransitionableResourceState, pResource, &m_TransitionListHead, m_TransitionListEntry)
      {
            func(*pResource);
            list_delinit(&pResource->m_TransitionListEntry);
      }
   }

   // Updates vectors with the operations that should be applied to the requested resource.
   // May update the destination state of the resource.
   void ProcessTransitioningResource(ID3D12Resource* pTransitioningResource,
                                     TransitionableResourceState& TransitionableResourceState,
                                     CCurrentResourceState& CurrentState,
                                     UINT NumTotalSubresources,
                                     UINT64 ExecutionId);

private:
   // Helpers
   static bool TransitionRequired(D3D12_RESOURCE_STATES CurrentState, D3D12_RESOURCE_STATES& DestinationState);
   void AddCurrentStateUpdate(TransitionableResourceState& Resource,
                              CCurrentResourceState& CurrentState,
                              UINT SubresourceIndex,
                              const CCurrentResourceState::LogicalState &NewLogicalState);
   void ProcessTransitioningSubresourceExplicit(CCurrentResourceState& CurrentState,
                                                UINT i,
                                                D3D12_RESOURCE_STATES state,
                                                D3D12_RESOURCE_STATES after,
                                                TransitionableResourceState& TransitionableResourceState,
                                                D3D12_RESOURCE_BARRIER& TransitionDesc,
                                                UINT64 ExecutionId);
};

#endif // D3D12_RESOURCE_STATE_H
