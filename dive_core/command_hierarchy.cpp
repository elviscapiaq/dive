
/*
 Copyright 2019 Google LLC

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

#include "command_hierarchy.h"
#include <assert.h>
#include <algorithm>  // std::transform
#include <cstdint>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include "capture_layer/generated/command_decoder.h"
#include "capture_layer/generated/command_printer.h"
#include "capture_layer/generated/vulkan_metadata.h"
#include "dive_core/common/common.h"
#include "dive_core/common/pm4_packets/me_pm4_packets.h"

#include "pm4_info.h"
#include "dive_strings.h"
#include "log.h"

namespace Dive
{
// =================================================================================================
// Helper Functions
// =================================================================================================
enum class TcCacheOp
{
    kNop = 0,    // Do nothing.
    kWbInvL1L2,  // Flush TCC data and invalidate all TCP and TCC data
    kWbInvL2Nc,  // Flush and invalidate all TCC data that used the non-coherent MTYPE.
    kWbL2Nc,     // Flush all TCC data that used the non-coherent MTYPE.
    kWbL2Wc,     // Flush all TCC data that used the write-combined MTYPE.
    kInvL2Nc,    // Invalidate all TCC data that used the non-coherent MTYPE.
    kInvL2Md,    // Invalidate the TCC's read-only metadata cache.
    kInvL1,      // Invalidate all TCP data.
    kInvL1Vol,   // Invalidate all volatile TCP data.
    kCount
};
const char *TcCacheOpStrings[] = {
    nullptr,      // kNop
    "wbInvL1L2",  // kWbInvL1L2
    "wbInvL2",    // kWbInvL2Nc
    "wbL2",       // kWbL2Nc
    "wbL2Wc",     // kWbL2Wc (Not used)
    "invL2",      // kInvL2Nc
    "invL2Md",    // kInvL2Md
    "invL1",      // kInvL1
    "invL1Vol",   // kInvL1Vol (Not used)
};
TcCacheOp GetCacheOp(uint32_t cp_coher_cntl)
{
    return TcCacheOp::kNop;
}

static const std::string kRenderPassName = "RenderPass";

// =================================================================================================
// Topology
// =================================================================================================
uint64_t Topology::GetNumNodes() const
{
    DIVE_ASSERT(m_node_children.size() == m_node_shared_children.size());
    DIVE_ASSERT(m_node_children.size() == m_node_parent.size());
    DIVE_ASSERT(m_node_children.size() == m_node_child_index.size());
    return m_node_children.size();
}

//--------------------------------------------------------------------------------------------------
uint64_t Topology::GetParentNodeIndex(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_node_parent.size());
    return m_node_parent[node_index];
}
//--------------------------------------------------------------------------------------------------
uint64_t Topology::GetChildIndex(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_node_child_index.size());
    return m_node_child_index[node_index];
}
//--------------------------------------------------------------------------------------------------
uint64_t Topology::GetNumChildren(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_node_children.size());
    return m_node_children[node_index].m_num_children;
}
//--------------------------------------------------------------------------------------------------
uint64_t Topology::GetChildNodeIndex(uint64_t node_index, uint64_t child_index) const
{
    DIVE_ASSERT(node_index < m_node_children.size());
    DIVE_ASSERT(child_index < m_node_children[node_index].m_num_children);
    uint64_t child_list_index = m_node_children[node_index].m_start_index + child_index;
    DIVE_ASSERT(child_list_index < m_children_list.size());
    return m_children_list[child_list_index];
}
//--------------------------------------------------------------------------------------------------
uint64_t Topology::GetNumSharedChildren(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_node_shared_children.size());
    return m_node_shared_children[node_index].m_num_children;
}
//--------------------------------------------------------------------------------------------------
uint64_t Topology::GetSharedChildNodeIndex(uint64_t node_index, uint64_t child_index) const
{
    DIVE_ASSERT(node_index < m_node_shared_children.size());
    DIVE_ASSERT(child_index < m_node_shared_children[node_index].m_num_children);
    uint64_t child_list_index = m_node_shared_children[node_index].m_start_index + child_index;
    DIVE_ASSERT(child_list_index < m_shared_children_list.size());
    return m_shared_children_list[child_list_index];
}

//--------------------------------------------------------------------------------------------------
uint64_t Topology::GetNextNodeIndex(uint64_t node_index) const
{
    uint64_t num_children = GetNumChildren(node_index);
    if (num_children > 0)
        return GetChildNodeIndex(node_index, 0);
    while (true)
    {
        if (node_index == kRootNodeIndex)
            return UINT64_MAX;
        uint64_t parent_node_index = GetParentNodeIndex(node_index);
        uint64_t sibling_index = GetChildIndex(node_index) + 1;
        if (sibling_index < GetNumChildren(parent_node_index))
            return GetChildNodeIndex(parent_node_index, sibling_index);
        node_index = parent_node_index;
    }
}

//--------------------------------------------------------------------------------------------------
void Topology::SetNumNodes(uint64_t num_nodes)
{
    m_node_children.resize(num_nodes);
    m_node_shared_children.resize(num_nodes);
    m_node_parent.resize(num_nodes, UINT64_MAX);
    m_node_child_index.resize(num_nodes, UINT64_MAX);
}

//--------------------------------------------------------------------------------------------------
void Topology::AddChildren(uint64_t node_index, const std::vector<uint64_t> &children)
{
    DIVE_ASSERT(m_node_children.size() == m_node_parent.size());
    DIVE_ASSERT(m_node_children.size() == m_node_child_index.size());

    // Append to m_children_list
    uint64_t prev_size = m_children_list.size();
    m_children_list.resize(m_children_list.size() + children.size());
    std::copy(children.begin(), children.end(), m_children_list.begin() + prev_size);

    // Set "pointer" to children_list
    DIVE_ASSERT(m_node_children[node_index].m_num_children == 0);
    m_node_children[node_index].m_start_index = prev_size;
    m_node_children[node_index].m_num_children = children.size();

    // Set parent pointer and child_index for each child
    for (uint64_t i = 0; i < children.size(); ++i)
    {
        uint64_t child_node_index = children[i];
        DIVE_ASSERT(child_node_index < m_node_children.size());  // Sanity check

        // Each child can have only 1 parent
        DIVE_ASSERT(m_node_parent[child_node_index] == UINT64_MAX);
        DIVE_ASSERT(m_node_child_index[child_node_index] == UINT64_MAX);
        m_node_parent[child_node_index] = node_index;
        m_node_child_index[child_node_index] = i;
    }
}

//--------------------------------------------------------------------------------------------------
void Topology::AddSharedChildren(uint64_t node_index, const std::vector<uint64_t> &children)
{
    DIVE_ASSERT(m_node_shared_children.size() == m_node_parent.size());
    DIVE_ASSERT(m_node_shared_children.size() == m_node_child_index.size());

    // Append to m_shared_children_list
    uint64_t prev_size = m_shared_children_list.size();
    m_shared_children_list.resize(m_shared_children_list.size() + children.size());
    std::copy(children.begin(), children.end(), m_shared_children_list.begin() + prev_size);

    // Set "pointer" to children_list
    DIVE_ASSERT(m_node_shared_children[node_index].m_num_children == 0);
    m_node_shared_children[node_index].m_start_index = prev_size;
    m_node_shared_children[node_index].m_num_children = children.size();
}

// =================================================================================================
// CommandHierarchy
// =================================================================================================
CommandHierarchy::CommandHierarchy() {}

//--------------------------------------------------------------------------------------------------
CommandHierarchy::~CommandHierarchy() {}

//--------------------------------------------------------------------------------------------------
const Topology &CommandHierarchy::GetEngineHierarchyTopology() const
{
    return m_topology[kEngineTopology];
}
//--------------------------------------------------------------------------------------------------
const Topology &CommandHierarchy::GetSubmitHierarchyTopology() const
{
    return m_topology[kSubmitTopology];
}

//--------------------------------------------------------------------------------------------------
const Topology &CommandHierarchy::GetVulkanDrawEventHierarchyTopology() const
{
    return m_topology[kVulkanEventTopology];
}

//--------------------------------------------------------------------------------------------------
const Topology &CommandHierarchy::GetVulkanEventHierarchyTopology() const
{
    return m_topology[kVulkanCallTopology];
}

//--------------------------------------------------------------------------------------------------
const Topology &CommandHierarchy::GetAllEventHierarchyTopology() const
{
    return m_topology[kAllEventTopology];
}

//--------------------------------------------------------------------------------------------------
const Topology &CommandHierarchy::GetRgpHierarchyTopology() const
{
    return m_topology[kRgpTopology];
}

//--------------------------------------------------------------------------------------------------
NodeType CommandHierarchy::GetNodeType(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_nodes.m_node_type.size());
    return m_nodes.m_node_type[node_index];
}

//--------------------------------------------------------------------------------------------------
const char *CommandHierarchy::GetNodeDesc(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_nodes.m_description.size());
    return m_nodes.m_description[node_index].c_str();
}

//--------------------------------------------------------------------------------------------------
const std::vector<uint8_t> &CommandHierarchy::GetMetadata(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_nodes.m_metadata.size());
    return m_nodes.m_metadata[node_index];
}

//--------------------------------------------------------------------------------------------------
Dive::EngineType CommandHierarchy::GetSubmitNodeEngineType(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_nodes.m_aux_info.size());
    DIVE_ASSERT(m_nodes.m_node_type[node_index] == Dive::NodeType::kSubmitNode);
    const AuxInfo &info = m_nodes.m_aux_info[node_index];
    return info.submit_node.m_engine_type;
}

//--------------------------------------------------------------------------------------------------
uint32_t CommandHierarchy::GetSubmitNodeIndex(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_nodes.m_aux_info.size());
    DIVE_ASSERT(m_nodes.m_node_type[node_index] == Dive::NodeType::kSubmitNode);
    const AuxInfo &info = m_nodes.m_aux_info[node_index];
    return info.submit_node.m_submit_index;
}

//--------------------------------------------------------------------------------------------------
uint8_t CommandHierarchy::GetIbNodeIndex(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_nodes.m_aux_info.size());
    DIVE_ASSERT(m_nodes.m_node_type[node_index] == Dive::NodeType::kIbNode);
    const AuxInfo &info = m_nodes.m_aux_info[node_index];
    return info.ib_node.m_ib_index;
}

//--------------------------------------------------------------------------------------------------
IbType CommandHierarchy::GetIbNodeType(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_nodes.m_aux_info.size());
    DIVE_ASSERT(m_nodes.m_node_type[node_index] == Dive::NodeType::kIbNode);
    const AuxInfo &info = m_nodes.m_aux_info[node_index];
    return (IbType)info.ib_node.m_ib_type;
}

//--------------------------------------------------------------------------------------------------
uint32_t CommandHierarchy::GetIbNodeSizeInDwords(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_nodes.m_aux_info.size());
    DIVE_ASSERT(m_nodes.m_node_type[node_index] == Dive::NodeType::kIbNode);
    const AuxInfo &info = m_nodes.m_aux_info[node_index];
    return info.ib_node.m_size_in_dwords;
}

//--------------------------------------------------------------------------------------------------
bool CommandHierarchy::GetIbNodeIsFullyCaptured(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_nodes.m_aux_info.size());
    DIVE_ASSERT(m_nodes.m_node_type[node_index] == Dive::NodeType::kIbNode);
    const AuxInfo &info = m_nodes.m_aux_info[node_index];
    return info.ib_node.m_fully_captured;
}

//--------------------------------------------------------------------------------------------------
CommandHierarchy::MarkerType CommandHierarchy::GetMarkerNodeType(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_nodes.m_aux_info.size());
    DIVE_ASSERT(m_nodes.m_node_type[node_index] == Dive::NodeType::kMarkerNode);
    const AuxInfo &info = m_nodes.m_aux_info[node_index];
    return info.marker_node.m_type;
}

//--------------------------------------------------------------------------------------------------
uint32_t CommandHierarchy::GetMarkerNodeId(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_nodes.m_aux_info.size());
    DIVE_ASSERT(m_nodes.m_node_type[node_index] == Dive::NodeType::kMarkerNode);
    const AuxInfo &info = m_nodes.m_aux_info[node_index];
    return info.marker_node.m_id;
}

//--------------------------------------------------------------------------------------------------
uint32_t CommandHierarchy::GetEventNodeId(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_nodes.m_aux_info.size());
    DIVE_ASSERT(m_nodes.m_node_type[node_index] == Dive::NodeType::kDrawDispatchDmaNode);
    const AuxInfo &info = m_nodes.m_aux_info[node_index];
    return info.event_node.m_event_id;
}

//--------------------------------------------------------------------------------------------------
uint64_t CommandHierarchy::GetPacketNodeAddr(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_nodes.m_aux_info.size());
    DIVE_ASSERT(m_nodes.m_node_type[node_index] == Dive::NodeType::kPacketNode);
    const AuxInfo &info = m_nodes.m_aux_info[node_index];
    return info.packet_node.m_addr;
}

//--------------------------------------------------------------------------------------------------
uint8_t CommandHierarchy::GetPacketNodeOpcode(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_nodes.m_aux_info.size());
    DIVE_ASSERT(m_nodes.m_node_type[node_index] == Dive::NodeType::kPacketNode);
    const AuxInfo &info = m_nodes.m_aux_info[node_index];
    return info.packet_node.m_opcode;
}

//--------------------------------------------------------------------------------------------------
bool CommandHierarchy::GetPacketNodeIsCe(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_nodes.m_aux_info.size());
    DIVE_ASSERT(m_nodes.m_node_type[node_index] == Dive::NodeType::kPacketNode);
    const AuxInfo &info = m_nodes.m_aux_info[node_index];
    return info.packet_node.m_is_ce_packet;
}
//--------------------------------------------------------------------------------------------------
bool CommandHierarchy::GetRegFieldNodeIsCe(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_nodes.m_aux_info.size());
    DIVE_ASSERT(m_nodes.m_node_type[node_index] == Dive::NodeType::kRegNode ||
                m_nodes.m_node_type[node_index] == Dive::NodeType::kFieldNode);
    const AuxInfo &info = m_nodes.m_aux_info[node_index];
    return info.reg_field_node.m_is_ce_packet;
}

//--------------------------------------------------------------------------------------------------
SyncType CommandHierarchy::GetSyncNodeSyncType(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_nodes.m_aux_info.size());
    DIVE_ASSERT(m_nodes.m_node_type[node_index] == Dive::NodeType::kSyncNode);
    const AuxInfo &info = m_nodes.m_aux_info[node_index];
    return (SyncType)info.sync_node.m_sync_type;
}

//--------------------------------------------------------------------------------------------------
SyncInfo CommandHierarchy::GetSyncNodeSyncInfo(uint64_t node_index) const
{
    DIVE_ASSERT(node_index < m_nodes.m_aux_info.size());
    DIVE_ASSERT(m_nodes.m_node_type[node_index] == Dive::NodeType::kSyncNode);
    const AuxInfo &info = m_nodes.m_aux_info[node_index];
    return info.sync_node.m_sync_info;
}

//--------------------------------------------------------------------------------------------------
uint64_t CommandHierarchy::AddNode(NodeType           type,
                                   const std::string &desc,
                                   AuxInfo            aux_info,
                                   char              *metadata_ptr,
                                   uint32_t           metadata_size)
{
    return m_nodes.AddNode(type, desc, aux_info, metadata_ptr, metadata_size);
}

//--------------------------------------------------------------------------------------------------
size_t CommandHierarchy::GetEventIndex(uint64_t node_index) const
{
    const std::vector<uint64_t> &indices = m_nodes.m_event_node_indices;
    auto                         it = std::lower_bound(indices.begin(), indices.end(), node_index);
    if (it == indices.end() || *it != node_index)
    {
        return 0;
    }
    return it - indices.begin() + 1;
}

// =================================================================================================
// CommandHierarchy::Nodes
// =================================================================================================
uint64_t CommandHierarchy::Nodes::AddNode(NodeType           type,
                                          const std::string &desc,
                                          AuxInfo            aux_info,
                                          char              *metadata_ptr,
                                          uint32_t           metadata_size)
{
    DIVE_ASSERT(m_node_type.size() == m_description.size());
    DIVE_ASSERT(m_node_type.size() == m_aux_info.size());
    DIVE_ASSERT(m_node_type.size() == m_metadata.size());

    m_node_type.push_back(type);
    m_description.push_back(desc);
    m_aux_info.push_back(aux_info);

    std::vector<uint8_t> temp(metadata_size);
    if (metadata_ptr != nullptr)
        memcpy(&temp[0], metadata_ptr, metadata_size);
    m_metadata.push_back(std::move(temp));

    return m_node_type.size() - 1;
}

// =================================================================================================
// CommandHierarchy::AuxInfo
// =================================================================================================
CommandHierarchy::AuxInfo::AuxInfo(uint64_t val)
{
    m_u64All = val;
}

//--------------------------------------------------------------------------------------------------
CommandHierarchy::AuxInfo CommandHierarchy::AuxInfo::SubmitNode(Dive::EngineType engine_type,
                                                                uint32_t         submit_index)
{
    AuxInfo info(0);
    info.submit_node.m_engine_type = engine_type;
    info.submit_node.m_submit_index = submit_index;
    return info;
}

//--------------------------------------------------------------------------------------------------
CommandHierarchy::AuxInfo CommandHierarchy::AuxInfo::IbNode(uint32_t ib_index,
                                                            IbType   ib_type,
                                                            uint32_t size_in_dwords,
                                                            bool     fully_captured)
{
    DIVE_ASSERT((ib_index & ((1 << kMaxNumIbsBits) - 1)) == ib_index);
    AuxInfo info(0);
    info.ib_node.m_ib_type = (uint8_t)ib_type;
    info.ib_node.m_ib_index = ib_index & ((1 << kMaxNumIbsBits) - 1);
    info.ib_node.m_size_in_dwords = size_in_dwords;
    info.ib_node.m_fully_captured = (fully_captured == true) ? 1 : 0;
    DIVE_ASSERT((IbType)info.ib_node.m_ib_type == ib_type);
    return info;
}

//--------------------------------------------------------------------------------------------------
CommandHierarchy::AuxInfo CommandHierarchy::AuxInfo::PacketNode(uint64_t addr,
                                                                uint8_t  opcode,
                                                                bool     is_ce_packet)
{
    // Addresses should only be 48-bits
    DIVE_ASSERT(addr == (addr & 0x0000FFFFFFFFFFFF));
    AuxInfo info(0);
    info.packet_node.m_addr = (addr & 0x0000FFFFFFFFFFFF);
    info.packet_node.m_opcode = opcode;
    info.packet_node.m_is_ce_packet = is_ce_packet;
    return info;
}

//--------------------------------------------------------------------------------------------------
CommandHierarchy::AuxInfo CommandHierarchy::AuxInfo::RegFieldNode(bool is_ce_packet)
{
    AuxInfo info(0);
    info.reg_field_node.m_is_ce_packet = is_ce_packet;
    return info;
}

//--------------------------------------------------------------------------------------------------
CommandHierarchy::AuxInfo CommandHierarchy::AuxInfo::EventNode(uint32_t event_id)
{
    AuxInfo info(0);
    info.event_node.m_event_id = event_id;
    return info;
}

//--------------------------------------------------------------------------------------------------
CommandHierarchy::AuxInfo CommandHierarchy::AuxInfo::MarkerNode(MarkerType type, uint32_t id)
{
    AuxInfo info(0);
    info.marker_node.m_type = type;
    info.marker_node.m_id = id;
    return info;
}

//--------------------------------------------------------------------------------------------------
CommandHierarchy::AuxInfo CommandHierarchy::AuxInfo::SyncNode(SyncType type, SyncInfo sync_info)
{
    AuxInfo info(0);
    info.sync_node.m_sync_type = (uint32_t)type;
    info.sync_node.m_sync_info = sync_info;
    return info;
}

// =================================================================================================
// CommandHierarchyCreator
// =================================================================================================
bool CommandHierarchyCreator::CreateTrees(CommandHierarchy  *command_hierarchy_ptr,
                                          const CaptureData &capture_data,
                                          bool               flatten_chain_nodes,
                                          ILog              *log_ptr)
{
    m_log_ptr = log_ptr;

    m_command_hierarchy_ptr = command_hierarchy_ptr;
    m_capture_data_ptr = &capture_data;

    // Clear/Reset internal data structures, just in case
    *m_command_hierarchy_ptr = CommandHierarchy();

    // Add a dummy root node for easier management
    uint64_t root_node_index = AddNode(NodeType::kRootNode, "", 0);
    DIVE_VERIFY(root_node_index == Topology::kRootNodeIndex);

    // Add each engine type to the frame_node
    std::vector<uint64_t> engine_nodes;
    for (uint32_t engine_type = 0; engine_type < (uint32_t)EngineType::kCount; ++engine_type)
    {
        uint64_t node_index = AddNode(NodeType::kEngineNode, kEngineTypeStrings[engine_type], 0);
        AddChild(CommandHierarchy::kEngineTopology, Topology::kRootNodeIndex, node_index);
    }

    m_num_events = 0;
    m_cur_submit_node_index = UINT64_MAX;
    m_dcb_ib_stack.clear();
    m_ccb_ib_stack.clear();
    m_flatten_chain_nodes = flatten_chain_nodes;

    for (uint32_t submit_index = 0; submit_index < capture_data.GetNumSubmits(); ++submit_index)
    {
        const Dive::SubmitInfo &submit_info = capture_data.GetSubmitInfo(submit_index);
        OnSubmitStart(submit_index, submit_info);

        if (submit_info.IsDummySubmit())
        {
            OnSubmitEnd(submit_index, submit_info);
            continue;
        }

        // Only gfx or compute engine types are parsed
        if ((submit_info.GetEngineType() != Dive::EngineType::kUniversal) &&
            (submit_info.GetEngineType() != Dive::EngineType::kCompute) &&
            (submit_info.GetEngineType() != Dive::EngineType::kDma))
        {
            OnSubmitEnd(submit_index, submit_info);
            continue;
        }

        EmulatePM4 emu;
        if (!emu.ExecuteSubmit(*this,
                                capture_data.GetMemoryManager(),
                                submit_index,
                                submit_info.GetNumIndirectBuffers(),
                                submit_info.GetIndirectBufferInfoPtr()))
            return false;

        OnSubmitEnd(submit_index, submit_info);
    }

    m_command_hierarchy_ptr->SetMetadataVersion(m_capture_data_ptr->GetVulkanMetadataVersion());
    // Convert the info in m_node_children into CommandHierarchy's topologies
    CreateTopologies();
    return true;
}

//--------------------------------------------------------------------------------------------------
bool CommandHierarchyCreator::CreateTrees(CommandHierarchy *command_hierarchy_ptr,
                                          EngineType        engine_type,
                                          QueueType         queue_type,
                                          uint32_t         *command_dwords,
                                          uint32_t          size_in_dwords,
                                          ILog             *log_ptr)
{
    // Note: This function is mostly a copy/paste from the main CreateTrees() function, but with
    // workarounds to handle a case where there is no marker_data or capture_data
    class TempMemoryManager : public IMemoryManager
    {
    public:
        TempMemoryManager(uint32_t *command_dwords, uint32_t size_in_dwords) :
            m_command_dwords(command_dwords),
            m_size_in_dwords(size_in_dwords)
        {
        }

        // Copy the given va/size from the memory blocks
        virtual bool CopyMemory(void    *buffer_ptr,
                                uint32_t submit_index,
                                uint64_t va_addr,
                                uint64_t size) const
        {
            if ((va_addr + size) > (m_size_in_dwords * sizeof(uint32_t)))
                return false;

            // Treat the va_addr as an offset
            uint8_t *command_bytes = (uint8_t *)m_command_dwords;
            memcpy(buffer_ptr, &command_bytes[va_addr], size);
            return true;
        }
        virtual bool GetMemoryOfUnknownSizeViaCallback(uint32_t     submit_index,
                                                       uint64_t     va_addr,
                                                       PfnGetMemory data_callback,
                                                       void        *user_ptr) const
        {
            DIVE_ASSERT(false);
            return true;
        }
        virtual uint64_t GetMaxContiguousSize(uint32_t submit_index, uint64_t va_addr) const
        {
            DIVE_ASSERT(false);
            return 0;
        }
        virtual bool IsValid(uint32_t submit_index, uint64_t addr, uint64_t size) const
        {
            DIVE_ASSERT(false);
            return true;
        }

    private:
        uint32_t *m_command_dwords;
        uint32_t  m_size_in_dwords;
    };

    m_log_ptr = log_ptr;

    m_command_hierarchy_ptr = command_hierarchy_ptr;
    m_capture_data_ptr = nullptr;

    // Clear/Reset internal data structures, just in case
    *m_command_hierarchy_ptr = CommandHierarchy();

    // Add a dummy root node for easier management
    uint64_t root_node_index = AddNode(NodeType::kRootNode, "", 0);
    DIVE_VERIFY(root_node_index == Topology::kRootNodeIndex);

    // Add each engine type to the frame_node
    std::vector<uint64_t> engine_nodes;
    {
        uint64_t node_index = AddNode(NodeType::kEngineNode,
                                      kEngineTypeStrings[(uint32_t)engine_type],
                                      0);
        AddChild(CommandHierarchy::kEngineTopology, Topology::kRootNodeIndex, node_index);
    }

    m_num_events = 0;
    m_cur_submit_node_index = UINT64_MAX;
    m_dcb_ib_stack.clear();
    m_ccb_ib_stack.clear();
    m_flatten_chain_nodes = false;

    uint32_t submit_index = 0;
    {
        Dive::IndirectBufferInfo ib_info;
        ib_info.m_va_addr = 0x0;
        ib_info.m_size_in_dwords = size_in_dwords;
        ib_info.m_skip = false;

        std::vector<IndirectBufferInfo> ib_array;
        ib_array.push_back(ib_info);

        const Dive::SubmitInfo submit_info(engine_type, queue_type, 0, false, std::move(ib_array));
        OnSubmitStart(submit_index, submit_info);

        if (submit_info.IsDummySubmit())
        {
            OnSubmitEnd(submit_index, submit_info);
            return false;
        }

        // Only gfx or compute engine types are parsed
        if ((submit_info.GetEngineType() != Dive::EngineType::kUniversal) &&
            (submit_info.GetEngineType() != Dive::EngineType::kCompute) &&
            (submit_info.GetEngineType() != Dive::EngineType::kDma))
        {
            OnSubmitEnd(submit_index, submit_info);
            return false;
        }

        EmulatePM4        emu;
        TempMemoryManager mem_manager(command_dwords, size_in_dwords);
        if (!emu.ExecuteSubmit(*this,
                                mem_manager,
                                submit_index,
                                submit_info.GetNumIndirectBuffers(),
                                submit_info.GetIndirectBufferInfoPtr()))
            return false;

        OnSubmitEnd(submit_index, submit_info);
    }

    // Convert the info in m_node_children into CommandHierarchy's topologies
    CreateTopologies();
    return true;
}

//--------------------------------------------------------------------------------------------------
bool CommandHierarchyCreator::OnIbStart(uint32_t                  submit_index,
                                        uint32_t                  ib_index,
                                        const IndirectBufferInfo &ib_info,
                                        IbType                    type)
{
    // Create IB description string
    std::ostringstream ib_string_stream;
    if (type == IbType::kNormal)
    {
        ib_string_stream << "IB: " << ib_index << ", Address: 0x" << std::hex << ib_info.m_va_addr
                         << ", Size (DWORDS): " << std::dec << ib_info.m_size_in_dwords;
        if (ib_info.m_skip)
            ib_string_stream << ", NOT CAPTURED";
    }
    else if (type == IbType::kCall)
    {
        ib_string_stream << "Call IB"
                         << ", Address: 0x" << std::hex << ib_info.m_va_addr
                         << ", Size (DWORDS): " << std::dec << ib_info.m_size_in_dwords;
        if (ib_info.m_skip)
            ib_string_stream << ", NOT CAPTURED";
    }
    else if (type == IbType::kChain)
    {
        ib_string_stream << "Chain IB"
                         << ", Address: 0x" << std::hex << ib_info.m_va_addr
                         << ", Size (DWORDS): " << std::dec << ib_info.m_size_in_dwords;
        if (ib_info.m_skip)
            ib_string_stream << ", NOT CAPTURED";
    }

    // Create the ib node
    CommandHierarchy::AuxInfo aux_info = CommandHierarchy::AuxInfo::IbNode(ib_index,
                                                                           type,
                                                                           ib_info.m_size_in_dwords,
                                                                           !ib_info.m_skip);
    uint64_t ib_node_index = AddNode(NodeType::kIbNode, ib_string_stream.str(), aux_info);

    // Determine parent node and update m_cur_non_chain_#cb_ib_node_index
    uint64_t parent_node_index = m_cur_submit_node_index;
    if (!m_dcb_ib_stack.empty())
    {
        parent_node_index = m_dcb_ib_stack.back();
    }

    if (m_flatten_chain_nodes && type == IbType::kChain)
    {
        // If flatten enabled, then add to the nearest non-chain node parent
        // Find first previous non-CHAIN parent
        for (size_t i = m_dcb_ib_stack.size() - 1; i != SIZE_MAX; i--)
        {
            uint64_t index = m_dcb_ib_stack[i];
            IbType   cur_type = m_command_hierarchy_ptr->GetIbNodeType(index);
            if (cur_type != IbType::kChain)
            {
                parent_node_index = index;
                break;
            }
        }
    }

    AddChild(CommandHierarchy::kEngineTopology, parent_node_index, ib_node_index);
    AddChild(CommandHierarchy::kSubmitTopology, parent_node_index, ib_node_index);

    m_dcb_ib_stack.push_back(ib_node_index);

    m_cmd_begin_packet_node_indices.clear();
    m_cmd_begin_event_node_indices.clear();
    return true;
}

//--------------------------------------------------------------------------------------------------
bool CommandHierarchyCreator::OnIbEnd(uint32_t                  submit_index,
                                      uint32_t                  ib_index,
                                      const IndirectBufferInfo &ib_info)
{
    DIVE_ASSERT(!m_dcb_ib_stack.empty());

    // Note: This callback is only called for the last CHAIN of a series of daisy-CHAIN IBs,
    // because the emulator does not keep track of IBs in an internal stack. So start by
    // popping all consecutive CHAIN IBs
    IbType type;
    type = m_command_hierarchy_ptr->GetIbNodeType(m_dcb_ib_stack.back());
    while (!m_dcb_ib_stack.empty() && type == IbType::kChain)
    {
        m_dcb_ib_stack.pop_back();
        type = m_command_hierarchy_ptr->GetIbNodeType(m_dcb_ib_stack.back());
    }

    m_dcb_ib_stack.pop_back();
    OnVulkanMarkerEnd();
    m_cmd_begin_packet_node_indices.clear();
    m_cmd_begin_event_node_indices.clear();
    return true;
}

//--------------------------------------------------------------------------------------------------
bool CommandHierarchyCreator::OnPacket(const IMemoryManager &       mem_manager,
                                       uint32_t                     submit_index,
                                       uint32_t                     ib_index,
                                       uint64_t                     va_addr,
                                       Pm4Type                      type,
                                       uint32_t                     header)
{
    // THIS IS TEMPORARY! Only deal with typ4 & type7 packets for now
    if ((type != Pm4Type::kType4) && (type != Pm4Type::kType7))
        return true;

    // Create the packet node and add it as child to the current submit_node and ib_node
    uint64_t packet_node_index = AddPacketNode(mem_manager, submit_index, va_addr, false, type, header);
    AddSharedChild(CommandHierarchy::kEngineTopology, m_cur_submit_node_index, packet_node_index);
    AddSharedChild(CommandHierarchy::kSubmitTopology, m_cur_submit_node_index, packet_node_index);
    AddSharedChild(CommandHierarchy::kAllEventTopology, m_cur_submit_node_index, packet_node_index);
    AddSharedChild(CommandHierarchy::kRgpTopology, m_cur_submit_node_index, packet_node_index);
    AddSharedChild(CommandHierarchy::kEngineTopology, m_dcb_ib_stack.back(), packet_node_index);
    AddSharedChild(CommandHierarchy::kSubmitTopology, m_dcb_ib_stack.back(), packet_node_index);

	uint32_t opcode = UINT32_MAX;
    if (type == Pm4Type::kType7)
    {
        Pm4Type7Header *type7_header = (Pm4Type7Header *)&header;
		opcode = type7_header->opcode;
	}

    // Cache all packets added (will cache until encounter next event/IB)
    m_packets.Add(opcode, va_addr, packet_node_index);

    // Cache packets that may be part of the vkBeginCommandBuffer.
    m_cmd_begin_packet_node_indices.push_back(packet_node_index);
    bool is_marker = false;

    SyncType sync_type = GetSyncType(mem_manager,
                                     submit_index,
                                     m_packets.m_packet_opcodes,
                                     m_packets.m_packet_addrs);
    bool     is_draw_dispatch_dma_event = IsDrawDispatchEvent(opcode);
    if ((sync_type != SyncType::kNone) || is_draw_dispatch_dma_event)
    {
        uint64_t event_node_index = UINT64_MAX;
        uint64_t parent_node_index = m_cur_submit_node_index;
        if (!m_marker_stack.empty())
        {
            parent_node_index = m_marker_stack.back();
        }
        if (sync_type != SyncType::kNone)
        {
            // m_num_events++;
            // auto     barrier_it = m_marker_creator.CurrentBarrier();
            // uint64_t sync_event_node_index = AddSyncEventNode(mem_manager,
            //                                                   submit_index,
            //                                                   va_addr,
            //                                                   sync_type,
            //                                                   barrier_it->id());
            // event_node_index = sync_event_node_index;
            // if (barrier_it->IsValid() && barrier_it->BarrierNode() != UINT64_MAX)
            //     parent_node_index = barrier_it->BarrierNode();
            // this->AppendEventNodeIndex(sync_event_node_index);
        }
        else if (is_draw_dispatch_dma_event)
        {
            std::string draw_dispatch_node_string = GetEventString(mem_manager,
                                                                   submit_index,
                                                                   va_addr,
                                                                   opcode);
            uint32_t    event_id = m_num_events++;
            // auto        marker_it = m_marker_creator.CurrentEvent();
            // if (!(marker_it->IsValid() && marker_it->EventNode() == UINT64_MAX))
            // {
            //     marker_it = nullptr;
            // }
            CommandHierarchy::AuxInfo
                     aux_info = CommandHierarchy::AuxInfo::EventNode(event_id);
            uint64_t draw_dispatch_node_index = AddNode(NodeType::kDrawDispatchDmaNode,
                                                        draw_dispatch_node_string,
                                                        aux_info);
            AppendEventNodeIndex(draw_dispatch_node_index);
            // if (marker_it->IsValid())
            //     m_markers_ptr->SetEventNode(marker_it->id(), draw_dispatch_node_index);

            // auto barrier_it = m_marker_creator.CurrentBarrier();
            // if (barrier_it->IsValid() && barrier_it->BarrierNode() != UINT64_MAX)
            //     parent_node_index = barrier_it->BarrierNode();

            event_node_index = draw_dispatch_node_index;
        }

        // Cache nodes that may be part of the vkBeginCommandBuffer.
        m_cmd_begin_event_node_indices.push_back(event_node_index);

        // Add as children all packets that have been processed since the last event
        // Note: Events only show up in the event topology and internal RGP topology.
        for (uint32_t packet = 0; packet < m_packets.m_packet_node_indices.size(); ++packet)
        {
            uint64_t cur_node_index = m_packets.m_packet_node_indices[packet];
            AddSharedChild(CommandHierarchy::kAllEventTopology, event_node_index, cur_node_index);
            AddSharedChild(CommandHierarchy::kRgpTopology, event_node_index, cur_node_index);
        }
        m_packets.Clear();

        // Add the draw_dispatch_node to the submit_node if currently not inside a marker range.
        // Otherwise append it to the marker at the top of the marker stack.
        // Note: Events only show up in the event topology and internal RGP topology.
        AddChild(CommandHierarchy::kAllEventTopology, parent_node_index, event_node_index);

        m_node_parent_info[CommandHierarchy::kAllEventTopology]
                          [event_node_index] = parent_node_index;

        if (!m_internal_marker_stack.empty())
        {
            parent_node_index = m_internal_marker_stack.back();
        }
        AddChild(CommandHierarchy::kRgpTopology, parent_node_index, event_node_index);
        m_node_parent_info[CommandHierarchy::kRgpTopology][event_node_index] = parent_node_index;
    }
    // vulkan call NOP packages. Currently contains call parameters(except parameters in array),
    // each call is in one NOP packet.
    else if (opcode == Type7Opcodes::CP_NOP)
    {
        /*
        NopVulkanCallHeader nop_header;
        bool                ret = mem_manager.CopyMemory(&nop_header.u32All,
                                          submit_index,
                                          va_addr + sizeof(PM4_PFP_TYPE_3_HEADER),
                                          sizeof(nop_header));
        DIVE_VERIFY(ret);

        if (nop_header.signature == kNopPayloadSignature)
        {
            is_marker = true;
            uint32_t          vulkan_call_data_len = (header.count + 1) * sizeof(uint32_t);
            std::vector<char> vulkan_call_data(vulkan_call_data_len);
            ret = mem_manager.CopyMemory(vulkan_call_data.data(),
                                         submit_index,
                                         va_addr + sizeof(PM4_PFP_TYPE_3_HEADER),
                                         vulkan_call_data_len);
            DIVE_ASSERT(ret);

            ParseVulkanCallMarker(vulkan_call_data.data(),
                                  vulkan_call_data_len,
                                  m_cur_submit_node_index,
                                  packet_node_index);
            is_marker_parsed = true;
            m_command_hierarchy_ptr->m_has_vulkan_marker = true;
        }
		*/
    }

    // This packet is potentially implicit NOP packet for vkBeginCommandBuffer
    // if (is_marker_parsed && !m_is_parsing_cb_start_marker)
    // {
    //     m_cmd_begin_packet_node_indices.clear();
    //     m_cmd_begin_event_node_indices.clear();
    // }

    if (!is_marker)
    {
        // Add it to all markers on stack, if applicable.
        for (auto it = m_marker_stack.begin(); it != m_marker_stack.end(); ++it)
            AddSharedChild(CommandHierarchy::kAllEventTopology, *it, packet_node_index);

        for (auto it = m_internal_marker_stack.begin(); it != m_internal_marker_stack.end(); ++it)
            AddSharedChild(CommandHierarchy::kRgpTopology, *it, packet_node_index);
    }

    // auto cb_id = m_marker_creator.CurrentCommandBuffer()->id();
    // if (cb_id != m_cur_cb)
    // {
    //     const auto &cbs = m_markers_ptr->CommandBuffers();
    //     if (cbs.IsValidId(cb_id))
    //         m_markers_ptr->SetCommandBufferSubmit(cb_id, m_cur_engine_index);
    //     m_cur_cb = cb_id;
    // }
    return true;
}

//--------------------------------------------------------------------------------------------------
void CommandHierarchyCreator::OnSubmitStart(uint32_t submit_index, const SubmitInfo &submit_info)
{
    uint32_t engine_index = static_cast<uint32_t>(submit_info.GetEngineType());
    uint32_t queue_index = static_cast<uint32_t>(submit_info.GetQueueType());

    std::ostringstream submit_string_stream;
    submit_string_stream << "Submit: " << submit_index;
    submit_string_stream << ", Num IBs: " << submit_info.GetNumIndirectBuffers()
                         << ", Engine: " << kEngineTypeStrings[engine_index]
                         << ", Queue: " << kQueueTypeStrings[queue_index]
                         << ", Engine Index: " << (uint32_t)submit_info.GetEngineIndex()
                         << ", Dummy Submit: " << (uint32_t)submit_info.IsDummySubmit();

    // Create submit node
    Dive::EngineType          engine_type = submit_info.GetEngineType();
    CommandHierarchy::AuxInfo aux_info = CommandHierarchy::AuxInfo::SubmitNode(engine_type,
                                                                               submit_index);
    uint64_t                  submit_node_index = AddNode(NodeType::kSubmitNode,
                                         submit_string_stream.str(),
                                         aux_info);

    // Add submit node as child to the appropriate engine node
    uint64_t engine_node_index = GetChildNodeIndex(CommandHierarchy::kEngineTopology,
                                                   Topology::kRootNodeIndex,
                                                   engine_index);
    AddChild(CommandHierarchy::kEngineTopology, engine_node_index, submit_node_index);

    // Add submit node to the other topologies as children to the root node
    AddChild(CommandHierarchy::kSubmitTopology, Topology::kRootNodeIndex, submit_node_index);
    AddChild(CommandHierarchy::kAllEventTopology, Topology::kRootNodeIndex, submit_node_index);
    AddChild(CommandHierarchy::kRgpTopology, Topology::kRootNodeIndex, submit_node_index);
    m_cur_submit_node_index = submit_node_index;
    m_cur_engine_index = submit_info.GetEngineIndex();
}

//--------------------------------------------------------------------------------------------------
void CommandHierarchyCreator::OnSubmitEnd(uint32_t submit_index, const SubmitInfo &submit_info)
{
    // For the submit topology, the IBs are inserted in emulation order, and are not necessarily in
    // ib-index order. Sort them here so they appear in order of ib-index.
    std::vector<uint64_t> &submit_children = m_node_children[CommandHierarchy::kSubmitTopology][0]
                                                            [m_cur_submit_node_index];
    std::sort(submit_children.begin(),
              submit_children.end(),
              [&](uint64_t lhs, uint64_t rhs) -> bool {
                  uint8_t lhs_index = m_command_hierarchy_ptr->GetIbNodeIndex(lhs);
                  uint8_t rhs_index = m_command_hierarchy_ptr->GetIbNodeIndex(rhs);
                  return lhs_index < rhs_index;
              });

    // If marker stack is not empty, that means those are vkCmdDebugMarkerBeginEXT() calls without
    // the corresponding vkCmdDebugMarkerEndEXT. Clear the market stack for the next submit.
    m_marker_stack.clear();
    m_internal_marker_stack.clear();

    if (!m_packets.m_packet_node_indices.empty())
    {
        uint64_t postamble_state_node_index;
        if (GetChildCount(CommandHierarchy::kAllEventTopology, m_cur_submit_node_index) != 0)
            postamble_state_node_index = AddNode(NodeType::kPostambleStateNode, "State");
        else
            postamble_state_node_index = AddNode(NodeType::kPostambleStateNode, "Postamble State");

        // Add to postamble_state_note all packets that have been processed since the last
        // draw/dispatch
        for (uint32_t packet = 0; packet < m_packets.m_packet_node_indices.size(); ++packet)
        {
            AddSharedChild(CommandHierarchy::kAllEventTopology,
                           postamble_state_node_index,
                           m_packets.m_packet_node_indices[packet]);
            AddSharedChild(CommandHierarchy::kRgpTopology,
                           postamble_state_node_index,
                           m_packets.m_packet_node_indices[packet]);
        }
        m_packets.Clear();

        // Add the postamble_state_node to the submit_node in the event topology
        AddChild(CommandHierarchy::kAllEventTopology,
                 m_cur_submit_node_index,
                 postamble_state_node_index);
        AddChild(CommandHierarchy::kRgpTopology,
                 m_cur_submit_node_index,
                 postamble_state_node_index);
    }

    // Insert present node to event topology, when appropriate
    if (m_capture_data_ptr != nullptr)
    {
        for (uint32_t i = 0; i < m_capture_data_ptr->GetNumPresents(); ++i)
        {
            const PresentInfo &present_info = m_capture_data_ptr->GetPresentInfo(i);

            // Check if present exists right after this submit
            if (submit_index != (present_info.GetSubmitIndex()))
                continue;

            std::ostringstream present_string_stream;
            if (present_info.HasValidData())
            {
                const char *format_string = GetVkFormatString(present_info.GetSurfaceVkFormat());
                DIVE_ASSERT(format_string != nullptr);
                uint32_t    vk_color_space = present_info.GetSurfaceVkColorSpaceKHR();
                const char *color_space_string = GetVkColorSpaceKhrString(vk_color_space);
                DIVE_ASSERT(color_space_string != nullptr);
                present_string_stream
                << "Present: " << i << ", FullScreen: " << present_info.IsFullScreen()
                << ", Engine: " << kEngineTypeStrings[(uint32_t)present_info.GetEngineType()]
                << ", Queue: " << kQueueTypeStrings[(uint32_t)present_info.GetQueueType()]
                << ", SurfaceAddr: 0x" << std::hex << present_info.GetSurfaceAddr() << std::dec
                << ", SurfaceSize: " << present_info.GetSurfaceSize()
                << ", VkFormat: " << format_string << ", VkColorSpaceKHR: " << color_space_string;
            }
            else
            {
                present_string_stream << "Present: " << i;
            }
            uint64_t present_node_index = AddNode(NodeType::kPresentNode,
                                                  present_string_stream.str());
            AddChild(CommandHierarchy::kAllEventTopology,
                     Topology::kRootNodeIndex,
                     present_node_index);
            AddChild(CommandHierarchy::kRgpTopology, Topology::kRootNodeIndex, present_node_index);
        }
    }
    m_cur_submit_node_index = UINT64_MAX;
    m_ccb_ib_stack.clear();
    m_dcb_ib_stack.clear();
}

//--------------------------------------------------------------------------------------------------
uint64_t CommandHierarchyCreator::AddPacketNode(const IMemoryManager &       mem_manager,
                                                uint32_t                     submit_index,
                                                uint64_t                     va_addr,
                                                bool                         is_ce_packet,
                                                Pm4Type                      type,
                                                uint32_t                     header)
{
    if (type == Pm4Type::kType7)
    {
        Pm4Type7Header type7_header;
        type7_header.u32All = header;

        std::ostringstream packet_string_stream;
        packet_string_stream << GetOpCodeString(type7_header.opcode);
        packet_string_stream << " 0x" << std::hex << type7_header.u32All << std::dec;

        CommandHierarchy::AuxInfo aux_info = CommandHierarchy::AuxInfo::PacketNode(va_addr,
                                                                                type7_header.opcode,
                                                                                is_ce_packet);

        uint64_t packet_node_index = AddNode(NodeType::kPacketNode,
                                            packet_string_stream.str(),
                                            aux_info);
        /*
        if (type7_header.opcode == Pal::Gfx9::IT_SET_CONTEXT_REG)
        {
            // Note: IT_SET_CONTEXT_REG_INDEX does not appear to be used in the driver
            uint32_t start = Pal::Gfx9::CONTEXT_SPACE_START;
            uint32_t end = Pal::Gfx9::Gfx09_10::CONTEXT_SPACE_END;
            AppendRegNodes(mem_manager, submit_index, va_addr, start, end, header, packet_node_index);
        }
        else if (type7_header.opcode == Pal::Gfx9::IT_CONTEXT_REG_RMW)
        {
            AppendContextRegRmwNodes(mem_manager, submit_index, va_addr, header, packet_node_index);
        }
        else if ((type7_header.opcode == Pal::Gfx9::IT_SET_UCONFIG_REG) ||
                (type7_header.opcode == Pal::Gfx9::IT_SET_UCONFIG_REG_INDEX))
        {
            uint32_t start = Pal::Gfx9::UCONFIG_SPACE_START;
            uint32_t end = Pal::Gfx9::UCONFIG_SPACE_END;
            AppendRegNodes(mem_manager, submit_index, va_addr, start, end, header, packet_node_index);
        }
        else if (type7_header.opcode == Pal::Gfx9::IT_SET_CONFIG_REG)
        {
            uint32_t start = Pal::Gfx9::CONFIG_SPACE_START;
            uint32_t end = Pal::Gfx9::CONFIG_SPACE_END;
            AppendRegNodes(mem_manager, submit_index, va_addr, start, end, header, packet_node_index);
        }
        else if ((type7_header.opcode == Pal::Gfx9::IT_SET_SH_REG) ||
                (type7_header.opcode == Pal::Gfx9::IT_SET_SH_REG_INDEX))
        {
            uint32_t start = Pal::Gfx9::PERSISTENT_SPACE_START;
            uint32_t end = Pal::Gfx9::PERSISTENT_SPACE_END;
            AppendRegNodes(mem_manager, submit_index, va_addr, start, end, header, packet_node_index);
        }
        else if (type7_header.opcode == Pal::Gfx9::IT_INDIRECT_BUFFER_CNST)
        {
            // IT_INDIRECT_BUFFER_CNST aliases IT_COND_INDIRECT_BUFFER_CNST, but have different
            // packet formats. So need to handle them manually.
            AppendIBFieldNodes("INDIRECT_BUFFER_CNST",
                            mem_manager,
                            submit_index,
                            va_addr,
                            is_ce_packet,
                            header,
                            packet_node_index);
        }
        else if (type7_header.opcode == Pal::Gfx9::IT_INDIRECT_BUFFER)
        {
            // IT_INDIRECT_BUFFER aliases IT_COND_INDIRECT_BUFFER, but have different packet
            // formats. So need to handle them manually.
            AppendIBFieldNodes("INDIRECT_BUFFER",
                            mem_manager,
                            submit_index,
                            va_addr,
                            is_ce_packet,
                            header,
                            packet_node_index);
        }
        else if (type7_header.opcode == Pal::Gfx9::IT_LOAD_UCONFIG_REG ||
                type7_header.opcode == Pal::Gfx9::IT_LOAD_CONTEXT_REG ||
                type7_header.opcode == Pal::Gfx9::IT_LOAD_SH_REG)
        {
            uint32_t reg_space_start = Pal::Gfx9::UCONFIG_SPACE_START;
            if (type7_header.opcode == Pal::Gfx9::IT_LOAD_CONTEXT_REG)
                reg_space_start = Pal::Gfx9::CONTEXT_SPACE_START;
            if (type7_header.opcode == Pal::Gfx9::IT_LOAD_SH_REG)
                reg_space_start = Pal::Gfx9::PERSISTENT_SPACE_START;
            AppendLoadRegNodes(mem_manager,
                            submit_index,
                            va_addr,
                            reg_space_start,
                            header,
                            packet_node_index);
        }
        else if (type7_header.opcode == Pal::Gfx9::IT_LOAD_CONTEXT_REG_INDEX ||
                type7_header.opcode == Pal::Gfx9::IT_LOAD_SH_REG_INDEX)
        {
            uint32_t reg_space_start = (type7_header.opcode == Pal::Gfx9::IT_LOAD_CONTEXT_REG_INDEX) ?
                                    Pal::Gfx9::CONTEXT_SPACE_START :
                                    Pal::Gfx9::PERSISTENT_SPACE_START;
            AppendLoadRegIndexNodes(mem_manager,
                                    submit_index,
                                    va_addr,
                                    reg_space_start,
                                    header,
                                    packet_node_index);
        }
        else if (type7_header.opcode == Pal::Gfx9::IT_EVENT_WRITE)
        {
            // Event field is special case because there are 2 or 4 DWORD variants of this packet
            // Also, the event_type field is not enumerated in the header, so have to enumerate
            // manually
            const PacketInfo *packet_info_ptr = GetPacketInfo(type7_header.opcode);
            DIVE_ASSERT(packet_info_ptr != nullptr);
            AppendEventWriteFieldNodes(mem_manager,
                                    submit_index,
                                    va_addr,
                                    header,
                                    packet_info_ptr,
                                    packet_node_index);
        }
        else
        */
        {
            const PacketInfo *packet_info_ptr = GetPacketInfo(type7_header.opcode);
            DIVE_ASSERT(packet_info_ptr != nullptr);
            AppendPacketFieldNodes(mem_manager,
                                submit_index,
                                va_addr,
                                is_ce_packet,
                                type7_header,
                                packet_info_ptr,
                                packet_node_index);
        }
        return packet_node_index;
    }
    else if (type == Pm4Type::kType4)
    {
        Pm4Type4Header type4_header;
        type4_header.u32All = header;

        std::ostringstream packet_string_stream;
        packet_string_stream << "TYPE4 REGWRITE";
        packet_string_stream << " 0x" << std::hex << type4_header.u32All << std::dec;

        CommandHierarchy::AuxInfo aux_info = CommandHierarchy::AuxInfo::PacketNode(va_addr,
                                                                                UINT8_MAX,
                                                                                is_ce_packet);

        uint64_t packet_node_index = AddNode(NodeType::kPacketNode,
                                            packet_string_stream.str(),
                                            aux_info);

        AppendRegNodes(mem_manager, submit_index, va_addr, type4_header, packet_node_index);
        return packet_node_index;

    }
    return UINT32_MAX;  // This is temporary. Shouldn't happen once we properly add the packet node!
}

//--------------------------------------------------------------------------------------------------
uint64_t CommandHierarchyCreator::AddRegisterNode(uint32_t reg,
                                                  uint32_t reg_value)
{
    const RegInfo *reg_info_ptr = GetRegInfo(reg);

    RegInfo temp;
    temp.m_name = "Unknown";
    if (reg_info_ptr == nullptr)
    {
        reg_info_ptr = &temp;
    }

    // Should never have an "unknown register" unless something is seriously wrong!
    DIVE_ASSERT(reg_info_ptr != nullptr);

    // Reg item
    std::ostringstream reg_string_stream;
    reg_string_stream << reg_info_ptr->m_name << ": 0x" << std::hex << reg_value << std::dec;
    CommandHierarchy::AuxInfo aux_info = CommandHierarchy::AuxInfo::RegFieldNode(false);
    uint64_t reg_node_index = AddNode(NodeType::kRegNode, reg_string_stream.str(), aux_info);

    // Go through each field of this register, create a FieldNode out of it and append as child
    // to reg_node_ptr
    for (uint32_t field = 0; field < reg_info_ptr->m_fields.size(); ++field)
    {
        const RegField &reg_field = reg_info_ptr->m_fields[field];
        uint32_t        field_value = (reg_value & reg_field.m_mask) >> reg_field.m_shift;

        // Field item
        std::ostringstream field_string_stream;
        field_string_stream << reg_field.m_name << ": 0x" << std::hex << field_value << std::dec;
        uint64_t field_node_index = AddNode(NodeType::kFieldNode,
                                            field_string_stream.str(),
                                            aux_info);

        // Add it as child to reg_node
        AddChild(CommandHierarchy::kEngineTopology, reg_node_index, field_node_index);
        AddChild(CommandHierarchy::kSubmitTopology, reg_node_index, field_node_index);
        AddChild(CommandHierarchy::kAllEventTopology, reg_node_index, field_node_index);
        AddChild(CommandHierarchy::kRgpTopology, reg_node_index, field_node_index);
    }
    return reg_node_index;
}

//--------------------------------------------------------------------------------------------------
uint64_t CommandHierarchyCreator::AddSyncEventNode(const IMemoryManager        &mem_manager,
                                                   uint32_t                     submit_index,
                                                   uint64_t                     va_addr,
                                                   SyncType                     sync_type)
{
    return UINT64_MAX;
}

//--------------------------------------------------------------------------------------------------
uint32_t CommandHierarchyCreator::GetMarkerSize(const uint8_t *marker_ptr, size_t num_dwords)
{
    return UINT32_MAX;
}

//--------------------------------------------------------------------------------------------------
void CommandHierarchyCreator::ParseVulkanCmdBeginMarker(char *   marker_ptr,
                                                        uint32_t marker_size,
                                                        uint64_t submit_node_index,
                                                        uint64_t packet_node_index)
{
    uint64_t marker_node_index = UINT64_MAX;
    uint64_t parent_node_index = submit_node_index;

    const auto *marker = reinterpret_cast<const NopVulkanCallHeader *>(marker_ptr);
    DIVE_ASSERT(marker->signature == kNopPayloadSignature);
    DIVE_ASSERT(marker->cmdID == static_cast<uint8_t>(VKCmdID::vkBeginCommandBufferCmdID));

    std::stringstream cmd_args_ss;
    char             *args = marker_ptr + sizeof(NopVulkanCallHeader);
    PrintCommandParametersBrief(cmd_args_ss,
                                static_cast<VKCmdID>(marker->cmdID),
                                args,
                                marker_size,
                                m_capture_data_ptr->GetVulkanMetadataVersion());

    CommandHierarchy::AuxInfo
    aux_info = CommandHierarchy::AuxInfo::MarkerNode(CommandHierarchy::MarkerType::kDiveMetadata,
                                                     marker->cmdID);
    marker_node_index = AddNode(NodeType::kMarkerNode,
                                VulkanCmdList[static_cast<uint32_t>(marker->cmdID)] +
                                cmd_args_ss.str(),
                                aux_info,
                                args,
                                marker_size);

    size_t num_nodes = m_cmd_begin_event_node_indices.size();
    if (num_nodes > 0)
    {
        parent_node_index = m_node_parent_info[CommandHierarchy::kAllEventTopology]
                                              [m_cmd_begin_event_node_indices[0]];

        // Remove the event nodes (which belongs to vkBeginCommandBuffer) that have already been
        // added to the hierarchy and add them as children of vkBeginCommandBuffer
        RemoveListOfChildren(CommandHierarchy::kAllEventTopology,
                             parent_node_index,
                             m_cmd_begin_event_node_indices);

        for (size_t i = 0; i < num_nodes; i++)
        {
            AddChild(CommandHierarchy::kAllEventTopology,
                     marker_node_index,
                     m_cmd_begin_event_node_indices[i]);
        }

        parent_node_index = m_node_parent_info[CommandHierarchy::kRgpTopology]
                                              [m_cmd_begin_event_node_indices[0]];

        RemoveListOfChildren(CommandHierarchy::kRgpTopology,
                             parent_node_index,
                             m_cmd_begin_event_node_indices);
    }

    parent_node_index = submit_node_index;
    // If this is the start of secondary command buffer, add another indent level.
    if (m_is_secondary_cmdbuf_started)
    {
        DIVE_ASSERT(m_secondary_cmdbuf_root_index != UINT64_MAX);
        parent_node_index = m_secondary_cmdbuf_root_index;
        m_marker_stack.push_back(m_secondary_cmdbuf_root_index);
        m_internal_marker_stack.push_back(m_secondary_cmdbuf_root_index);
    }

    AddChild(CommandHierarchy::kAllEventTopology, parent_node_index, marker_node_index);
    AddChild(CommandHierarchy::kRgpTopology, parent_node_index, marker_node_index);

    for (size_t i = 0; i < num_nodes; i++)
    {
        AddChild(CommandHierarchy::kRgpTopology,
                 marker_node_index,
                 m_cmd_begin_event_node_indices[i]);
    }

    for (auto i : m_cmd_begin_packet_node_indices)
    {
        AddSharedChild(CommandHierarchy::kAllEventTopology, marker_node_index, i);
        AddSharedChild(CommandHierarchy::kRgpTopology, marker_node_index, i);
    }

    m_vulkan_cmd_stack.push_back(marker_node_index);
    m_cmd_begin_event_node_indices.clear();
    m_cmd_begin_packet_node_indices.clear();
    m_node_parent_info.clear();
}

// Called on the implicit end of the vulkan command marker.
void CommandHierarchyCreator::OnVulkanMarkerEnd()
{
    if (m_has_unended_vulkan_marker)
    {
        DIVE_ASSERT(!m_marker_stack.empty());
        DIVE_ASSERT(!m_internal_marker_stack.empty());
        m_marker_stack.pop_back();
        m_internal_marker_stack.pop_back();

        m_has_unended_vulkan_marker = false;
    }

    // If we are the end of secondary commandary buffer, we have another level of indention need to
    // be reduced.
    if (m_cur_vulkan_cmd_id == static_cast<uint32_t>(VKCmdID::vkEndCommandBufferCmdID) &&
        m_is_secondary_cmdbuf_started)
    {
        if (!m_marker_stack.empty())
        {
            m_marker_stack.pop_back();
        }

        if (!m_internal_marker_stack.empty())
        {
            m_internal_marker_stack.pop_back();
        }
        m_cur_vulkan_cmd_id = UINT32_MAX;
    }
}

//--------------------------------------------------------------------------------------------------
bool CommandHierarchyCreator::IsBeginDebugMarkerNode(uint64_t node_index)
{
    NodeType node_type = m_command_hierarchy_ptr->GetNodeType(node_index);

    if (node_type == NodeType::kMarkerNode && m_last_user_push_parent_node != UINT64_MAX)
    {
        CommandHierarchy::MarkerType marker_type = m_command_hierarchy_ptr->GetMarkerNodeType(
        node_index);
        if (marker_type == CommandHierarchy::MarkerType::kBeginEnd)
        {
            return true;
        }
    }
    return false;
}

//--------------------------------------------------------------------------------------------------
void CommandHierarchyCreator::ParseVulkanCallMarker(char    *marker_ptr,
                                                    uint32_t marker_size,
                                                    uint64_t submit_node_index,
                                                    uint64_t packet_node_index)
{
    return;
}

//--------------------------------------------------------------------------------------------------
std::string CommandHierarchyCreator::GetEventString(const IMemoryManager &       mem_manager,
                                                    uint32_t                     submit_index,
                                                    uint64_t                     va_addr,
                                                    uint32_t                     opcode)
{
    std::ostringstream string_stream;
    DIVE_ASSERT(IsDrawDispatchEvent(opcode));

    if (opcode == CP_DRAW_INDX_OFFSET)
    {
        string_stream << "DrawIndexOffset";
    }
    else if (opcode == CP_DRAW_INDIRECT)
    {
        string_stream << "DrawIndirect";
    }
    else if (opcode == CP_DRAW_INDX_INDIRECT)
    {
        string_stream << "DrawIndexIndirect";
    }
    else if (opcode == CP_DRAW_INDIRECT_MULTI)
    {
        string_stream << "DrawIndirectMulti";
    }
    else if (opcode == CP_DRAW_AUTO)
    {
        string_stream << "DrawAuto";
    }
    return string_stream.str();
}

//--------------------------------------------------------------------------------------------------
void CommandHierarchyCreator::AppendRegNodes(const IMemoryManager        &mem_manager,
                                             uint32_t                     submit_index,
                                             uint64_t                     va_addr,
                                             Pm4Type4Header               header,
                                             uint64_t                     packet_node_index)
{
    uint32_t reg_addr = header.offset;

    // Go through each register set by this packet
    for (uint32_t i = 0; i < header.count; ++i)
    {
        uint64_t reg_va_addr = va_addr + sizeof(header) + i * sizeof(uint32_t);
        uint32_t reg_value;
        bool ret = mem_manager.CopyMemory(&reg_value, submit_index, reg_va_addr, sizeof(uint32_t));
        DIVE_ASSERT(ret);  // This should never fail!

        // Create the register node, as well as all its children nodes that describe the various
        // fields set in the single 32-bit register
        uint64_t reg_node_index = AddRegisterNode(reg_addr, reg_value);

        // Add it as child to packet node
        AddChild(CommandHierarchy::kEngineTopology, packet_node_index, reg_node_index);
        AddChild(CommandHierarchy::kSubmitTopology, packet_node_index, reg_node_index);
        AddChild(CommandHierarchy::kAllEventTopology, packet_node_index, reg_node_index);
        AddChild(CommandHierarchy::kRgpTopology, packet_node_index, reg_node_index);

        reg_addr++;
    }
}

//--------------------------------------------------------------------------------------------------
void CommandHierarchyCreator::AppendContextRegRmwNodes(const IMemoryManager        &mem_manager,
                                                       uint32_t                     submit_index,
                                                       uint64_t                     va_addr,
                                                       const PM4_PFP_TYPE_3_HEADER &header,
                                                       uint64_t packet_node_index)
{
    return;
}

//------------------------------------------------------------------------------------------------------
void CommandHierarchyCreator::AppendIBFieldNodes(const char                  *suffix,
                                                 const IMemoryManager        &mem_manager,
                                                 uint32_t                     submit_index,
                                                 uint64_t                     va_addr,
                                                 bool                         is_ce_packet,
                                                 const PM4_PFP_TYPE_3_HEADER &header,
                                                 uint64_t                     packet_node_index)
{
    return;
}

//--------------------------------------------------------------------------------------------------
void CommandHierarchyCreator::AppendLoadRegNodes(const IMemoryManager        &mem_manager,
                                                 uint32_t                     submit_index,
                                                 uint64_t                     va_addr,
                                                 uint32_t                     reg_space_start,
                                                 const PM4_PFP_TYPE_3_HEADER &header,
                                                 uint64_t                     packet_node_index)
{
    return;
}

//--------------------------------------------------------------------------------------------------
void CommandHierarchyCreator::AppendLoadRegIndexNodes(const IMemoryManager        &mem_manager,
                                                      uint32_t                     submit_index,
                                                      uint64_t                     va_addr,
                                                      uint32_t                     reg_space_start,
                                                      const PM4_PFP_TYPE_3_HEADER &header,
                                                      uint64_t packet_node_index)
{
    return;
}

//--------------------------------------------------------------------------------------------------
void CommandHierarchyCreator::AppendEventWriteFieldNodes(const IMemoryManager        &mem_manager,
                                                         uint32_t                     submit_index,
                                                         uint64_t                     va_addr,
                                                         const PM4_PFP_TYPE_3_HEADER &header,
                                                         const PacketInfo *packet_info_ptr,
                                                         uint64_t          packet_node_index)
{
    return;
}

//--------------------------------------------------------------------------------------------------
void CommandHierarchyCreator::AppendPacketFieldNodes(const IMemoryManager        &mem_manager,
                                                     uint32_t                     submit_index,
                                                     uint64_t                     va_addr,
                                                     bool                         is_ce_packet,
                                                     Pm4Type7Header               type7_header,
                                                     const PacketInfo *           packet_info_ptr,
                                                     uint64_t                     packet_node_index,
                                                     size_t                       field_start,
                                                     size_t                       field_last)
{
    // Do a min(), since field_last defaults to UINT64_MAX
    size_t end_field = (packet_info_ptr->m_fields.size() < field_last + 1) ?
                       (uint32_t)packet_info_ptr->m_fields.size() :
                       field_last + 1;

    // Loop through each field and append it to packet
    uint32_t end_dword = UINT32_MAX;
    for (size_t field = field_start; field < end_field; ++field)
    {
        const PacketField &packet_field = packet_info_ptr->m_fields[field];
        end_dword = packet_field.m_dword;

        // Some packets end early sometimes and do not use all fields (e.g. CP_EVENT_WRITE with CACHE_CLEAN)
        if (packet_field.m_dword > type7_header.count)
            break;

        uint32_t dword_value = 0;
        uint64_t dword_va_addr = va_addr + packet_field.m_dword * sizeof(uint32_t);
        bool     ret = mem_manager.CopyMemory(&dword_value,
                                          submit_index,
                                          dword_va_addr,
                                          sizeof(uint32_t));
        DIVE_VERIFY(ret);  // This should never fail!

        uint32_t field_value = (dword_value & packet_field.m_mask) >> packet_field.m_shift;

        // Field item
        std::ostringstream field_string_stream;
        if (packet_field.m_enum_handle != UINT32_MAX)
        {
            const char *enum_str = GetEnumString(packet_field.m_enum_handle, field_value);
            DIVE_ASSERT(enum_str != nullptr);
            field_string_stream << packet_field.m_name << ": " << enum_str;
        }
        else
            field_string_stream << packet_field.m_name << ": 0x" << std::hex << field_value
                                << std::dec;

        CommandHierarchy::AuxInfo aux_info = CommandHierarchy::AuxInfo::RegFieldNode(
        is_ce_packet);
        uint64_t field_node_index = AddNode(NodeType::kFieldNode,
                                            field_string_stream.str(),
                                            aux_info);

        // Add it as child to packet_node
        AddChild(CommandHierarchy::kEngineTopology, packet_node_index, field_node_index);
        AddChild(CommandHierarchy::kSubmitTopology, packet_node_index, field_node_index);
        AddChild(CommandHierarchy::kAllEventTopology, packet_node_index, field_node_index);
        AddChild(CommandHierarchy::kRgpTopology, packet_node_index, field_node_index);
    }

    // If there are missing packet fields, then output the raw DWORDS directly
    if (end_dword < type7_header.count)
    {
        for (size_t i = end_dword+1; i <= type7_header.count; i++)
        {
            uint32_t dword_value = 0;
            uint64_t dword_va_addr = va_addr + i * sizeof(uint32_t);
            bool     ret = mem_manager.CopyMemory(&dword_value,
                                            submit_index,
                                            dword_va_addr,
                                            sizeof(uint32_t));
            DIVE_VERIFY(ret);  // This should never fail!

            std::ostringstream field_string_stream;
            field_string_stream << "(DWORD " << i << "): 0x" << std::hex << dword_value;

            CommandHierarchy::AuxInfo aux_info = CommandHierarchy::AuxInfo::RegFieldNode(
            is_ce_packet);
            uint64_t field_node_index = AddNode(NodeType::kFieldNode,
                                                field_string_stream.str(),
                                                aux_info);

            // Add it as child to packet_node
            AddChild(CommandHierarchy::kEngineTopology, packet_node_index, field_node_index);
            AddChild(CommandHierarchy::kSubmitTopology, packet_node_index, field_node_index);
            AddChild(CommandHierarchy::kAllEventTopology, packet_node_index, field_node_index);
            AddChild(CommandHierarchy::kRgpTopology, packet_node_index, field_node_index);
        }
    }
}

//--------------------------------------------------------------------------------------------------
uint64_t CommandHierarchyCreator::AddNode(NodeType                  type,
                                          const std::string        &desc,
                                          CommandHierarchy::AuxInfo aux_info,
                                          char                     *metadata_ptr,
                                          uint32_t                  metadata_size)
{
    uint64_t node_index = m_command_hierarchy_ptr->AddNode(type,
                                                           desc,
                                                           aux_info,
                                                           metadata_ptr,
                                                           metadata_size);
    for (uint32_t i = 0; i < CommandHierarchy::kTopologyTypeCount; ++i)
    {
        DIVE_ASSERT(m_node_children[i][0].size() == node_index);
        DIVE_ASSERT(m_node_children[i][1].size() == node_index);
        m_node_children[i][0].resize(m_node_children[i][0].size() + 1);
        m_node_children[i][1].resize(m_node_children[i][1].size() + 1);
    }
    return node_index;
}

//--------------------------------------------------------------------------------------------------
void CommandHierarchyCreator::AppendEventNodeIndex(uint64_t node_index)
{
    m_command_hierarchy_ptr->m_nodes.m_event_node_indices.push_back(node_index);
}

//--------------------------------------------------------------------------------------------------
void CommandHierarchyCreator::AddChild(CommandHierarchy::TopologyType type,
                                       uint64_t                       node_index,
                                       uint64_t                       child_node_index)
{
    // Store children info into the temporary m_node_children
    // Use this to create the appropriate topology later
    DIVE_ASSERT(node_index < m_node_children[type][0].size());
    m_node_children[type][0][node_index].push_back(child_node_index);
}

//--------------------------------------------------------------------------------------------------
void CommandHierarchyCreator::AddSharedChild(CommandHierarchy::TopologyType type,
                                             uint64_t                       node_index,
                                             uint64_t                       child_node_index)
{
    // Store children info into the temporary m_node_children
    // Use this to create the appropriate topology later
    DIVE_ASSERT(node_index < m_node_children[type][1].size());
    m_node_children[type][1][node_index].push_back(child_node_index);
}

//--------------------------------------------------------------------------------------------------
// Remove all children listed in |children_node_indices|.
void CommandHierarchyCreator::RemoveListOfChildren(
CommandHierarchy::TopologyType type,
uint64_t                       node_index,
const std::vector<uint64_t>   &children_node_indices)
{
    if (children_node_indices.empty())
    {
        return;
    }

    auto  &vec = m_node_children[type][0][node_index];
    size_t j = 0;
    size_t k = 0;
    for (size_t i = 0; i < vec.size(); i++)
    {
        if (j < children_node_indices.size() && vec[i] == children_node_indices[j])
        {
            j++;
        }
        else
        {
            vec[k++] = vec[i];
        }
    }

    DIVE_ASSERT(j == children_node_indices.size());

    vec.erase(vec.begin() + vec.size() - children_node_indices.size(), vec.end());
}

//--------------------------------------------------------------------------------------------------
uint64_t CommandHierarchyCreator::GetChildNodeIndex(CommandHierarchy::TopologyType type,
                                                    uint64_t                       node_index,
                                                    uint64_t child_index) const
{
    DIVE_ASSERT(node_index < m_node_children[type][0].size());
    DIVE_ASSERT(child_index < m_node_children[type][0][node_index].size());
    return m_node_children[type][0][node_index][child_index];
}

//--------------------------------------------------------------------------------------------------
uint64_t CommandHierarchyCreator::GetChildCount(CommandHierarchy::TopologyType type,
                                                uint64_t                       node_index) const
{
    DIVE_ASSERT(node_index < m_node_children[type][0].size());
    return m_node_children[type][0][node_index].size();
}

//--------------------------------------------------------------------------------------------------
void CommandHierarchyCreator::CreateTopologies()
{
    // A kVulkanCallTopology is a kAllEventTopology without the following:
    //  kDrawDispatchDmaNode, kSyncNode, kPostambleStateNode, kMarkerNode-kBarrier
    auto FilterOut = [&](size_t node_index) {
        NodeType type = m_command_hierarchy_ptr->GetNodeType(node_index);
        // Filter out all these node types
        if (type == NodeType::kDrawDispatchDmaNode || type == NodeType::kSyncNode ||
            type == NodeType::kPostambleStateNode)
            return true;
        // Also filter out kMarkerNode-kBarrier nodes
        if (type == NodeType::kMarkerNode)
        {
            auto marker_type = m_command_hierarchy_ptr->GetMarkerNodeType(node_index);
            if (marker_type == CommandHierarchy::MarkerType::kBarrier)
                return true;
        }
        return false;
    };
    CommandHierarchy::TopologyType src_topology = CommandHierarchy::kAllEventTopology;
    CommandHierarchy::TopologyType dst_topology = CommandHierarchy::kVulkanCallTopology;
    size_t                         num_nodes = m_node_children[src_topology][0].size();
    DIVE_ASSERT(num_nodes == m_node_children[src_topology][1].size());
    for (size_t node_index = 0; node_index < num_nodes; ++node_index)
    {
        // Ensure topology was not previously filled-in
        DIVE_ASSERT(m_node_children[dst_topology][0][node_index].empty());
        DIVE_ASSERT(m_node_children[dst_topology][1][node_index].empty());

        // Ignore all these node types
        if (FilterOut(node_index))
            continue;

        // Go through primary children of a particular node, and only add non-ignored nodes
        const std::vector<uint64_t> &children = m_node_children[src_topology][0][node_index];
        for (size_t child = 0; child < children.size(); ++child)
        {
            if (!FilterOut(children[child]))
                AddChild(dst_topology, node_index, children[child]);
        }

        // Shared children should remain the same
        const std::vector<uint64_t> &shared = m_node_children[src_topology][1][node_index];
        m_node_children[CommandHierarchy::kVulkanCallTopology][1][node_index] = shared;
    }

    // A kVulkanEventTopology is a kVulkanCallTopology without non-Event Vulkan kMarkerNodes.
    // The shared-children of the non-Event Vulkan kMarkerNodes will be inherited by the "next"
    // Vulkan kMarkerNode encountered
    src_topology = CommandHierarchy::kVulkanCallTopology;
    dst_topology = CommandHierarchy::kVulkanEventTopology;
    num_nodes = m_node_children[src_topology][0].size();
    DIVE_ASSERT(num_nodes == m_node_children[src_topology][1].size());

    for (size_t node_index = 0; node_index < num_nodes; ++node_index)
    {
        // Skip over all Vulkan non-Event nodes
        if (IsVulkanNonEventNode(node_index))
            continue;

        // Go through primary children of a particular node, and only add non-ignored nodes
        const std::vector<uint64_t> &children = m_node_children[src_topology][0][node_index];
        std::vector<uint64_t>        acc_shared;
        for (size_t child = 0; child < children.size(); ++child)
        {
            // Accumulate shared packets from the child node
            uint64_t                     child_index = children[child];
            const std::vector<uint64_t> &shared = m_node_children[src_topology][1][child_index];
            acc_shared.insert(acc_shared.end(), shared.begin(), shared.end());
            if (!IsVulkanNonEventNode(child_index))
            {
                // If it isn't a Vulkan Event node or a Vulkan Non-Event node (ie. a non-Vulkan
                // node, such as a normal marker node, a submit node, etc), then throw away the
                // previous accumulation. For example, the beginning of a submit sometimes has a
                // vkCmdBegin followed by a debug-marker. The PM4 contents of the vkCmdBegin is
                // thrown away, since it isn't part of the debug-marker.
                if (!IsVulkanEventNode(child_index))
                    acc_shared.clear();

                AddChild(dst_topology, node_index, child_index);

                if (acc_shared.empty())
                    m_node_children[dst_topology][1][child_index] = shared;
                else
                    m_node_children[dst_topology][1][child_index] = acc_shared;
                acc_shared.clear();
            }
        }
    }

    // Convert the m_node_children temporary structure into CommandHierarchy's topologies
    for (uint32_t topology = 0; topology < CommandHierarchy::kTopologyTypeCount; ++topology)
    {
        num_nodes = m_node_children[topology][0].size();
        Topology &cur_topology = m_command_hierarchy_ptr->m_topology[topology];
        cur_topology.SetNumNodes(num_nodes);
        for (uint64_t node_index = 0; node_index < num_nodes; ++node_index)
        {
            DIVE_ASSERT(m_node_children[topology][0].size() == m_node_children[topology][1].size());
            cur_topology.AddChildren(node_index, m_node_children[topology][0][node_index]);
            cur_topology.AddSharedChildren(node_index, m_node_children[topology][1][node_index]);
        }
    }
}

//--------------------------------------------------------------------------------------------------
bool CommandHierarchyCreator::EventNodeHelper(uint64_t                      node_index,
                                              std::function<bool(uint32_t)> callback) const
{
    NodeType node_type = m_command_hierarchy_ptr->GetNodeType(node_index);
    if (node_type == NodeType::kMarkerNode)
    {
        CommandHierarchy::MarkerType type = m_command_hierarchy_ptr->GetMarkerNodeType(node_index);
        if (type == CommandHierarchy::MarkerType::kDiveMetadata)
            return callback(m_command_hierarchy_ptr->GetMarkerNodeId(node_index));
    }
    return false;
}

//--------------------------------------------------------------------------------------------------
bool CommandHierarchyCreator::IsVulkanEventNode(uint64_t node_index) const
{
    auto fp = std::bind(&CommandHierarchyCreator::IsVulkanEvent, this, std::placeholders::_1);
    return EventNodeHelper(node_index, fp);
}

//--------------------------------------------------------------------------------------------------
bool CommandHierarchyCreator::IsVulkanNonEventNode(uint64_t node_index) const
{
    auto fp = std::bind(&CommandHierarchyCreator::IsNonVulkanEvent, this, std::placeholders::_1);
    return EventNodeHelper(node_index, fp);
}

//--------------------------------------------------------------------------------------------------
bool CommandHierarchyCreator::IsVulkanEvent(uint32_t cmd_id) const
{
    switch (static_cast<VKCmdID>(cmd_id))
    {
    // Draw & Dispatch
    case VKCmdID::vkCmdDrawCmdID:
    case VKCmdID::vkCmdDrawIndexedCmdID:
    case VKCmdID::vkCmdDrawIndirectCmdID:
    case VKCmdID::vkCmdDrawIndexedIndirectCmdID:
    case VKCmdID::vkCmdDispatchCmdID:
    case VKCmdID::vkCmdDispatchIndirectCmdID:
    case VKCmdID::vkCmdDrawIndirectCountAMDCmdID:
    case VKCmdID::vkCmdDrawIndexedIndirectCountAMDCmdID:
    case VKCmdID::vkCmdDispatchBaseKHRCmdID:
    case VKCmdID::vkCmdDispatchBaseCmdID:
    case VKCmdID::vkCmdDrawIndirectCountKHRCmdID:
    case VKCmdID::vkCmdDrawIndexedIndirectCountKHRCmdID:

    // Pipeline barrier
    case VKCmdID::vkCmdPipelineBarrierCmdID:

    // Render pass
    case VKCmdID::vkCmdBeginRenderPassCmdID:
    case VKCmdID::vkCmdEndRenderPassCmdID:

    // Clear Cmds
    case VKCmdID::vkCmdClearAttachmentsCmdID:
    case VKCmdID::vkCmdClearColorImageCmdID:
    case VKCmdID::vkCmdClearDepthStencilImageCmdID:

    // Buffer and Image
    case VKCmdID::vkCmdFillBufferCmdID:
    case VKCmdID::vkCmdCopyImageCmdID:
    case VKCmdID::vkCmdCopyBufferToImageCmdID:
    case VKCmdID::vkCmdCopyBufferCmdID:
    case VKCmdID::vkCmdCopyImageToBufferCmdID:

    // Query pool
    case VKCmdID::vkCmdResetQueryPoolCmdID:
    case VKCmdID::vkCmdCopyQueryPoolResultsCmdID: return true;

    // Secondary command buffers
    case VKCmdID::vkCmdExecuteCommandsCmdID: return true;

    case VKCmdID::vkQueueSubmit: return true;

    default: break;
    }
    return false;
}

}  // namespace Dive