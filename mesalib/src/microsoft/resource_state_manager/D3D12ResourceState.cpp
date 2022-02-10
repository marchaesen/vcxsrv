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

#include "D3D12ResourceState.h"

//----------------------------------------------------------------------------------------------------------------------------------
 D3D12_RESOURCE_STATES CDesiredResourceState::GetSubresourceState(UINT SubresourceIndex) const
{
   if (AreAllSubresourcesSame())
   {
      SubresourceIndex = 0;
   }
   return m_spSubresourceStates[SubresourceIndex];
}

//----------------------------------------------------------------------------------------------------------------------------------
void CDesiredResourceState::UpdateSubresourceState(unsigned SubresourceIndex, D3D12_RESOURCE_STATES state)
{
   assert(SubresourceIndex < m_spSubresourceStates.size());
   if (m_spSubresourceStates[SubresourceIndex] == UNKNOWN_RESOURCE_STATE ||
       state == UNKNOWN_RESOURCE_STATE ||
       IsD3D12WriteState(state))
   {
      m_spSubresourceStates[SubresourceIndex] = state;
   }
   else
   {
      // Accumulate read state state bits
      m_spSubresourceStates[SubresourceIndex] |= state;
   }
}

//----------------------------------------------------------------------------------------------------------------------------------
void CDesiredResourceState::SetResourceState(D3D12_RESOURCE_STATES state)
{
   m_bAllSubresourcesSame = true;
   UpdateSubresourceState(0, state);
}
    
//----------------------------------------------------------------------------------------------------------------------------------
void CDesiredResourceState::SetSubresourceState(UINT SubresourceIndex, D3D12_RESOURCE_STATES state)
{
   if (m_bAllSubresourcesSame && m_spSubresourceStates.size() > 1)
   {
      std::fill(m_spSubresourceStates.begin() + 1, m_spSubresourceStates.end(), m_spSubresourceStates[0]);
      m_bAllSubresourcesSame = false;
   }
   if (m_spSubresourceStates.size() == 1)
   {
      SubresourceIndex = 0;
   }
   UpdateSubresourceState(SubresourceIndex, state);
}

//----------------------------------------------------------------------------------------------------------------------------------
void CDesiredResourceState::Reset()
{
   SetResourceState(UNKNOWN_RESOURCE_STATE);
}

//----------------------------------------------------------------------------------------------------------------------------------
void CCurrentResourceState::ConvertToSubresourceTracking()
{
   if (m_bAllSubresourcesSame && m_spLogicalState.size() > 1)
   {
      std::fill(m_spLogicalState.begin() + 1, m_spLogicalState.end(), m_spLogicalState[0]);
      m_bAllSubresourcesSame = false;
   }
}

//----------------------------------------------------------------------------------------------------------------------------------
CCurrentResourceState::CCurrentResourceState(UINT SubresourceCount, bool bSimultaneousAccess)
   : m_bSimultaneousAccess(bSimultaneousAccess)
   , m_spLogicalState(SubresourceCount)
{
   m_spLogicalState[0] = LogicalState{};
}

//----------------------------------------------------------------------------------------------------------------------------------
D3D12_RESOURCE_STATES CCurrentResourceState::StateIfPromoted(D3D12_RESOURCE_STATES State, UINT SubresourceIndex)
{
   D3D12_RESOURCE_STATES Result = D3D12_RESOURCE_STATE_COMMON;

   if (m_bSimultaneousAccess || !!(State & (
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | 
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | 
            D3D12_RESOURCE_STATE_COPY_SOURCE | 
            D3D12_RESOURCE_STATE_COPY_DEST)))
   {
      auto CurState = GetLogicalSubresourceState(SubresourceIndex);

      // If the current state is COMMON...
      if(CurState.State == D3D12_RESOURCE_STATE_COMMON)
      {
         // ...then promotion is allowed
         Result = State;
      }
      // If the current state is a read state resulting from previous promotion...
      else if(CurState.IsPromotedState && !!(CurState.State & D3D12_RESOURCE_STATE_GENERIC_READ))
      {
         // ...then (accumulated) promotion is allowed
         Result = State |= CurState.State;
      }
   }

   return Result;
}



//----------------------------------------------------------------------------------------------------------------------------------
void CCurrentResourceState::SetLogicalResourceState(LogicalState const& State)
{
   m_bAllSubresourcesSame = true;
   m_spLogicalState[0] = State;
}

//----------------------------------------------------------------------------------------------------------------------------------
void CCurrentResourceState::SetLogicalSubresourceState(UINT SubresourceIndex, LogicalState const& State)
{
   ConvertToSubresourceTracking();
   m_spLogicalState[SubresourceIndex] = State;
}

//----------------------------------------------------------------------------------------------------------------------------------
auto CCurrentResourceState::GetLogicalSubresourceState(UINT SubresourceIndex) const -> LogicalState const&
{
   if (AreAllSubresourcesSame())
   {
      SubresourceIndex = 0;
   }
   return m_spLogicalState[SubresourceIndex];
}

//----------------------------------------------------------------------------------------------------------------------------------
void CCurrentResourceState::Reset()
{
   m_bAllSubresourcesSame = true;
   m_spLogicalState[0] = LogicalState{};
}

//----------------------------------------------------------------------------------------------------------------------------------
ResourceStateManager::ResourceStateManager()
{
   list_inithead(&m_TransitionListHead);
   // Reserve some space in these vectors upfront. Values are arbitrary.
   m_vResourceBarriers.reserve(50);
}

//----------------------------------------------------------------------------------------------------------------------------------
void ResourceStateManager::TransitionResource(TransitionableResourceState& Resource,
                                              D3D12_RESOURCE_STATES state)
{
   Resource.m_DesiredState.SetResourceState(state);
   if (!Resource.IsTransitionPending())
   {
      list_add(&Resource.m_TransitionListEntry, &m_TransitionListHead);
   }
}

//----------------------------------------------------------------------------------------------------------------------------------
void ResourceStateManager::TransitionSubresource(TransitionableResourceState& Resource,
                                                 UINT SubresourceIndex,
                                                 D3D12_RESOURCE_STATES state)
{
   Resource.m_DesiredState.SetSubresourceState(SubresourceIndex, state);
   if (!Resource.IsTransitionPending())
   {
      list_add(&Resource.m_TransitionListEntry, &m_TransitionListHead);
   }
}

//----------------------------------------------------------------------------------------------------------------------------------
void ResourceStateManager::ApplyResourceTransitionsPreamble(bool IsImplicitDispatch)
{
   m_IsImplicitDispatch = IsImplicitDispatch;
   m_vResourceBarriers.clear();
}

//----------------------------------------------------------------------------------------------------------------------------------
/*static*/ bool ResourceStateManager::TransitionRequired(D3D12_RESOURCE_STATES CurrentState, D3D12_RESOURCE_STATES& DestinationState)
{
   // An exact match never needs a transition.
   if (CurrentState == DestinationState)
   {
      return false;
   }

   if (
      CurrentState == D3D12_RESOURCE_STATE_COMMON ||
      DestinationState == D3D12_RESOURCE_STATE_COMMON)
   {
      return true;
   }

   // Current state already contains the destination state, we're good.
   if ((CurrentState & DestinationState) == DestinationState)
   {
      DestinationState = CurrentState;
      return false;
   }

   // If the transition involves a write state, then the destination should just be the requested destination.
   // Otherwise, accumulate read states to minimize future transitions (by triggering the above condition).
   if (!IsD3D12WriteState(DestinationState) && !IsD3D12WriteState(CurrentState))
   {
      DestinationState |= CurrentState;
   }
   return true;
}

//----------------------------------------------------------------------------------------------------------------------------------
void ResourceStateManager::AddCurrentStateUpdate(TransitionableResourceState& Resource,
                                                 CCurrentResourceState& CurrentState,
                                                 UINT SubresourceIndex,
                                                 const CCurrentResourceState::LogicalState &NewLogicalState)
{
   if (SubresourceIndex == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
   {
      CurrentState.SetLogicalResourceState(NewLogicalState);
   }
   else
   {
      CurrentState.SetLogicalSubresourceState(SubresourceIndex, NewLogicalState);
   }
}

//----------------------------------------------------------------------------------------------------------------------------------
void ResourceStateManager::ProcessTransitioningResource(ID3D12Resource* pTransitioningResource,
                                                            TransitionableResourceState& TransitionableResourceState,
                                                            CCurrentResourceState& CurrentState,
                                                            UINT NumTotalSubresources,
                                                            UINT64 ExecutionId)
{
   // Figure out the set of subresources that are transitioning
   auto& DestinationState = TransitionableResourceState.m_DesiredState;
   bool bAllSubresourcesAtOnce = CurrentState.AreAllSubresourcesSame() && DestinationState.AreAllSubresourcesSame();

   D3D12_RESOURCE_BARRIER TransitionDesc;
   memset(&TransitionDesc, 0, sizeof(TransitionDesc));
   TransitionDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
   TransitionDesc.Transition.pResource = pTransitioningResource;

   UINT numSubresources = bAllSubresourcesAtOnce ? 1 : NumTotalSubresources;
   for (UINT i = 0; i < numSubresources; ++i)
   {
      D3D12_RESOURCE_STATES SubresourceDestinationState = DestinationState.GetSubresourceState(i);
      TransitionDesc.Transition.Subresource = bAllSubresourcesAtOnce ? D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES : i;

      // Is this subresource currently being used, or is it just being iterated over?
      D3D12_RESOURCE_STATES after = DestinationState.GetSubresourceState(i);
      if (after == UNKNOWN_RESOURCE_STATE)
      {
            // This subresource doesn't have any transition requested - move on to the next.
            continue;
      }

      // This is a transition into a state that is both write and non-write.
      // This is invalid according to D3D12. We're venturing into undefined behavior
      // land, but let's just pick the write state.
      if (IsD3D12WriteState(after) &&
         (after & ~RESOURCE_STATE_ALL_WRITE_BITS) != 0)
      {
         after &= RESOURCE_STATE_ALL_WRITE_BITS;

         // For now, this is the only way I've seen where this can happen.
         assert(after == D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      }

      ProcessTransitioningSubresourceExplicit(
         CurrentState,
         i,
         SubresourceDestinationState,
         after,
         TransitionableResourceState,
         TransitionDesc,
         ExecutionId); // throw( bad_alloc )
   }

   // Update destination states.
   // Coalesce destination state to ensure that it's set for the entire resource.
   DestinationState.SetResourceState(UNKNOWN_RESOURCE_STATE);

}

//----------------------------------------------------------------------------------------------------------------------------------
void ResourceStateManager::ProcessTransitioningSubresourceExplicit(
   CCurrentResourceState& CurrentState,
   UINT SubresourceIndex,
   D3D12_RESOURCE_STATES state,
   D3D12_RESOURCE_STATES after,
   TransitionableResourceState& TransitionableResourceState,
   D3D12_RESOURCE_BARRIER& TransitionDesc,
   UINT64 ExecutionId)
{
   // Simultaneous access resources currently in the COMMON
   // state can be implicitly promoted to any state other state.
   // Any non-simultaneous-access resources currently in the
   // COMMON state can still be implicitly  promoted to SRV,
   // NON_PS_SRV, COPY_SRC, or COPY_DEST.
   CCurrentResourceState::LogicalState CurrentLogicalState = CurrentState.GetLogicalSubresourceState(SubresourceIndex);

   // If the last time this logical state was set was in a different
   // execution period and is decayable then decay the current state
   // to COMMON
   if(ExecutionId != CurrentLogicalState.ExecutionId && CurrentLogicalState.MayDecay)
   {
      CurrentLogicalState.State = D3D12_RESOURCE_STATE_COMMON;
      CurrentLogicalState.IsPromotedState = false;
   }
   bool MayDecay = false;
   bool IsPromotion = false;

   // If not promotable then StateIfPromoted will be D3D12_RESOURCE_STATE_COMMON
   auto StateIfPromoted = CurrentState.StateIfPromoted(after, SubresourceIndex);

   if ( D3D12_RESOURCE_STATE_COMMON == StateIfPromoted )
   {
      if (CurrentLogicalState.State == D3D12_RESOURCE_STATE_UNORDERED_ACCESS &&
          after == D3D12_RESOURCE_STATE_UNORDERED_ACCESS &&
          m_IsImplicitDispatch)
      {
         D3D12_RESOURCE_BARRIER UAVBarrier = { D3D12_RESOURCE_BARRIER_TYPE_UAV };
         UAVBarrier.UAV.pResource = TransitionDesc.Transition.pResource;
         m_vResourceBarriers.push_back(UAVBarrier);
      }
      else if (TransitionRequired(CurrentLogicalState.State, /*inout*/ after))
      {
         // Insert a single concrete barrier (for non-simultaneous access resources).
         TransitionDesc.Transition.StateBefore = D3D12_RESOURCE_STATES(CurrentLogicalState.State);
         TransitionDesc.Transition.StateAfter = D3D12_RESOURCE_STATES(after);
         assert(TransitionDesc.Transition.StateBefore != TransitionDesc.Transition.StateAfter);
         m_vResourceBarriers.push_back(TransitionDesc); // throw( bad_alloc )

         MayDecay = CurrentState.SupportsSimultaneousAccess() && !IsD3D12WriteState(after);
         IsPromotion = false;
      }
   }
   else
   {
      // Handle identity state transition
      if(after != StateIfPromoted)
      {
         after = StateIfPromoted;
         MayDecay = !IsD3D12WriteState(after);
         IsPromotion = true;
      }
   }

   CCurrentResourceState::LogicalState NewLogicalState{after, ExecutionId, IsPromotion, MayDecay};
   AddCurrentStateUpdate(TransitionableResourceState,
                         CurrentState,
                         TransitionDesc.Transition.Subresource,
                         NewLogicalState);
}

//----------------------------------------------------------------------------------------------------------------------------------
void ResourceStateManager::SubmitResourceTransitions(ID3D12GraphicsCommandList *pCommandList)
{
   // Submit any pending barriers on source command lists that are not the destination.
   if (!m_vResourceBarriers.empty())
   {
      pCommandList->ResourceBarrier((UINT)m_vResourceBarriers.size(), m_vResourceBarriers.data());
   }
}

//----------------------------------------------------------------------------------------------------------------------------------
void ResourceStateManager::TransitionResource(TransitionableResourceState* pResource, D3D12_RESOURCE_STATES State)
{
   ResourceStateManager::TransitionResource(*pResource, State);
}

//----------------------------------------------------------------------------------------------------------------------------------
void ResourceStateManager::TransitionSubresource(TransitionableResourceState* pResource, UINT SubresourceIndex, D3D12_RESOURCE_STATES State)
{
   ResourceStateManager::TransitionSubresource(*pResource, SubresourceIndex, State);
}

//----------------------------------------------------------------------------------------------------------------------------------
void ResourceStateManager::ApplyAllResourceTransitions(ID3D12GraphicsCommandList *pCommandList, UINT64 ExecutionId, bool IsImplicitDispatch)
{
   ApplyResourceTransitionsPreamble(IsImplicitDispatch);

   ForEachTransitioningResource([=](TransitionableResourceState& ResourceBase)
   {
       TransitionableResourceState& CurResource = static_cast<TransitionableResourceState&>(ResourceBase);

       ID3D12Resource *pResource = CurResource.GetD3D12Resource();

       ProcessTransitioningResource(
           pResource,
           CurResource,
           CurResource.GetCurrentState(),
           CurResource.NumSubresources(),
           ExecutionId);
   });

   SubmitResourceTransitions(pCommandList);
}
