/*
** Copyright (c) 2023-2025 LunarG, Inc.
**
** Permission is hereby granted, free of charge, to any person obtaining a
** copy of this software and associated documentation files (the "Software"),
** to deal in the Software without restriction, including without limitation
** the rights to use, copy, modify, merge, publish, distribute, sublicense,
** and/or sell copies of the Software, and to permit persons to whom the
** Software is furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in
** all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
** FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
** DEALINGS IN THE SOFTWARE.
*/

#ifndef GFXRECON_DECODE_DX12_PRE_PROCESS_CONSUMER_H
#define GFXRECON_DECODE_DX12_PRE_PROCESS_CONSUMER_H

#include "generated/generated_dx12_consumer.h"

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(decode)

// If TEST_AVAILABLE_ARGS is enabled, it finds the available args that follow the original args, if the original args
// are unavailable.
// 0: disable
// 1: enable. The target could be Draw or Dispatch.
// 2: enable, and the target is Draw, not Dispatch.
// ExecuteIndirect isn't available to check if it's Draw, so it doesn't work for 2.
constexpr int TEST_AVAILABLE_ARGS = 0;

struct ExecuteIndirectInfo
{
    format::HandleId argument_id{ format::kNullHandleId };
    uint64_t         argument_offset{ 0 };
    format::HandleId count_id{ format::kNullHandleId };
    uint64_t         count_offset{ 0 };
};

struct TrackRootParameter
{
    // These are tracked in commandlist bindings.
    D3D12_ROOT_PARAMETER_TYPE   cmd_bind_type{};
    D3D12_GPU_DESCRIPTOR_HANDLE cmd_bind_captured_base_descriptor{ kNullGpuAddress }; // RootDescriptorTable
    D3D12_GPU_VIRTUAL_ADDRESS   cmd_bind_captured_buffer_location; // RootConstantBufferView, RootShaderResourceView,
                                                                   // RootUnorderedAccessView
    // Root32BitConstant has no resources or descriptors info, so no track.

    // These are tracked in Dx12DumpResources::CreateRootSignature.
    D3D12_ROOT_PARAMETER_TYPE            root_signature_type{};
    std::vector<D3D12_DESCRIPTOR_RANGE1> root_signature_descriptor_tables; // D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE

    // The other parameter types have no resources or descriptors info, so no track.
};

enum class DumpDrawCallType
{
    kUnknown,
    kDraw,
    kDispatch,
    kIndirect,
    kBundle
};

struct TrackDumpDrawCall
{
    DumpResourcesTarget dump_resources_target{};
    format::HandleId    command_list_id{ format::kNullHandleId };
    uint64_t            begin_block_index{ 0 };
    uint64_t            close_block_index{ 0 };
    uint64_t            begin_renderpass_block_index{ 0 };
    uint64_t            end_renderpass_block_index{ 0 };
    uint64_t            set_render_targets_block_index{ 0 };
    format::HandleId    compute_root_signature_handle_id{ format::kNullHandleId };
    format::HandleId    graphics_root_signature_handle_id{ format::kNullHandleId };
    DumpDrawCallType    drawcall_type{ DumpDrawCallType::kUnknown };

    // vertex
    std::vector<D3D12_VERTEX_BUFFER_VIEW> captured_vertex_buffer_views;

    // index
    D3D12_INDEX_BUFFER_VIEW captured_index_buffer_view{};

    // descriptor
    std::vector<format::HandleId>                    descriptor_heap_ids;
    std::unordered_map<uint32_t, TrackRootParameter> compute_root_parameters;
    std::unordered_map<uint32_t, TrackRootParameter> graphics_root_parameters;

    // ExecuteIndirect
    ExecuteIndirectInfo execute_indirect_info{};

    // Bundle
    format::HandleId bundle_commandlist_id{ format::kNullHandleId };
    // It couldn't use the structure that is the same to the parent structure, so use std::shared_ptr.
    std::shared_ptr<TrackDumpDrawCall> bundle_target_draw_call;

    uint64_t draw_call_block_index{ 0 }; // It could also be ExecuteIndirect or ExecuteBundle block index.
    uint64_t execute_block_index{ 0 };

    void Clear()
    {
        captured_vertex_buffer_views.clear();
        descriptor_heap_ids.clear();
        compute_root_parameters.clear();
        graphics_root_parameters.clear();
        compute_root_signature_handle_id  = format::kNullHandleId;
        graphics_root_signature_handle_id = format::kNullHandleId;
        bundle_commandlist_id             = format::kNullHandleId;
        bundle_target_draw_call           = nullptr;
        drawcall_type                     = DumpDrawCallType::kUnknown;
    }

    std::string GetString()
    {
        std::string info;
        info = "BlockIndices: ";

        info += "ExecuteCommandLists:";
        info += std::to_string(execute_block_index);
        info += ", ";

        info += "BeginCommandList:";
        info += std::to_string(begin_block_index);
        info += ", ";

        info += "CloseCommandList:";
        info += std::to_string(close_block_index);
        info += ", ";

        if (begin_renderpass_block_index != 0)
        {
            info += "BeginRenderPass:";
            info += std::to_string(begin_renderpass_block_index);
            info += ", ";

            info += "EndRenderPass:";
            info += std::to_string(end_renderpass_block_index);
            info += ", ";
        }

        if (set_render_targets_block_index != 0)
        {
            info += "SetRenderTargets:";
            info += std::to_string(set_render_targets_block_index);
            info += ", ";
        }

        info += "DrawCall:";
        info += std::to_string(draw_call_block_index);

        if (bundle_target_draw_call != nullptr)
        {
            info += ", ";
            info += "Bundle-BeginCommandList:";
            info += std::to_string(bundle_target_draw_call->begin_block_index);
            info += ", ";

            info += "Bundle-CloseCommandList:";
            info += std::to_string(bundle_target_draw_call->close_block_index);
            info += ", ";

            if (bundle_target_draw_call->begin_renderpass_block_index != 0)
            {
                info += "Bundle-BeginRenderPass:";
                info += std::to_string(bundle_target_draw_call->begin_renderpass_block_index);
                info += ", ";

                info += "Bundle-EndRenderPass:";
                info += std::to_string(bundle_target_draw_call->end_renderpass_block_index);
                info += ", ";
            }

            if (bundle_target_draw_call->set_render_targets_block_index != 0)
            {
                info += "Bundle-SetRenderTargets:";
                info += std::to_string(bundle_target_draw_call->set_render_targets_block_index);
                info += ", ";
            }

            info += "Bundle-DrawCall:";
            info += std::to_string(bundle_target_draw_call->draw_call_block_index);
        }
        return info;
    }
};

struct TrackDumpCommandList
{
    uint64_t         begin_block_index{ 0 };
    uint64_t         current_begin_renderpass_block_index{ 0 };
    uint64_t         current_set_render_targets_block_index{ 0 };
    format::HandleId current_compute_root_signature_handle_id{ format::kNullHandleId };
    format::HandleId current_graphics_root_signature_handle_id{ format::kNullHandleId };

    // vertex
    std::vector<D3D12_VERTEX_BUFFER_VIEW> current_captured_vertex_buffer_views;

    // index
    D3D12_INDEX_BUFFER_VIEW current_captured_index_buffer_view{};

    // descriptor
    std::vector<format::HandleId>                    current_descriptor_heap_ids;
    std::unordered_map<uint32_t, TrackRootParameter> current_compute_root_parameters;
    std::unordered_map<uint32_t, TrackRootParameter> current_graphics_root_parameters;

    // render target
    // Track render target info in replay, not here.
    // Because the useful info is replay cpuDescriptor. It's only available in replay.

    std::vector<std::shared_ptr<TrackDumpDrawCall>> track_dump_draw_calls;

    void Clear()
    {
        begin_block_index                         = 0;
        current_begin_renderpass_block_index      = 0;
        current_set_render_targets_block_index    = 0;
        current_compute_root_signature_handle_id  = format::kNullHandleId;
        current_graphics_root_signature_handle_id = format::kNullHandleId;
        current_captured_vertex_buffer_views.clear();
        current_captured_index_buffer_view = {};
        current_descriptor_heap_ids.clear();
        current_compute_root_parameters.clear();
        current_graphics_root_parameters.clear();
        track_dump_draw_calls.clear();
    }
};

// It runs tasks that need to be completed before replay.
class Dx12PreProcessConsumer : public Dx12Consumer
{
#define CHECK_DX12_CONSUMER_USAGE() \
    if (!dx12_consumer_usage_)      \
    {                               \
        return;                     \
    }

  public:
    Dx12PreProcessConsumer() {}

    bool WasD3D12APIDetected() { return dx12_consumer_usage_; }

    virtual void Process_D3D12CreateDevice(const gfxrecon::decode::ApiCallInfo&           call_info,
                                           HRESULT                                        return_value,
                                           gfxrecon::format::HandleId                     pAdapter,
                                           D3D_FEATURE_LEVEL                              MinimumFeatureLevel,
                                           gfxrecon::decode::Decoded_GUID                 riid,
                                           gfxrecon::decode::HandlePointerDecoder<void*>* ppDevice)
    {
        dx12_consumer_usage_                = true;
        check_dx12_consumer_usage_complete_ = true;
    }

    void EnableDumpResources(const DumpResourcesTarget& dump_resources_target)
    {
        enable_dump_resources_         = true;
        check_dump_resources_complete_ = false;
        dump_resources_target_         = dump_resources_target;
    }

    TrackDumpDrawCall* GetTrackDumpTarget()
    {
        if (track_submit_index_ <= dump_resources_target_.submit_index)
        {
            GFXRECON_LOG_FATAL("The target submit index(%d) of dump resources is out of range(%d).",
                               dump_resources_target_.submit_index,
                               track_submit_index_);
            if (TEST_AVAILABLE_ARGS > 0)
            {
                GFXRECON_LOG_FATAL("Although TEST_AVAILABLE_ARGS is enabled, it cann't find the available args that "
                                   "follow the original args.");
            }
            GFXRECON_ASSERT(track_submit_index_ > dump_resources_target_.submit_index);
        }

        auto it = track_commandlist_infos_.find(target_command_list_);
        if (it != track_commandlist_infos_.end())
        {
            auto draw_call_size = it->second.track_dump_draw_calls.size();
            GFXRECON_ASSERT(draw_call_size > target_draw_call_index_);

            if (is_modified_args)
            {
                GFXRECON_LOG_INFO("TEST_AVAILABLE_ARGS is enabled, it finds the available args(%d,%d,%d) that follow "
                                  "the original args.",
                                  dump_resources_target_.submit_index,
                                  dump_resources_target_.command_index,
                                  dump_resources_target_.draw_call_index);
            }
            auto& target                  = it->second.track_dump_draw_calls[target_draw_call_index_];
            target->dump_resources_target = dump_resources_target_;
            GFXRECON_LOG_INFO("Dump resources info: %s", target->GetString().c_str());
            return target.get();
        }
        return nullptr;
    }

    virtual void Process_ID3D12Device_CreateCommandList(const ApiCallInfo&           call_info,
                                                        format::HandleId             object_id,
                                                        HRESULT                      return_value,
                                                        UINT                         nodeMask,
                                                        D3D12_COMMAND_LIST_TYPE      type,
                                                        format::HandleId             pCommandAllocator,
                                                        format::HandleId             pInitialState,
                                                        Decoded_GUID                 riid,
                                                        HandlePointerDecoder<void*>* ppCommandList) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        InitializeTracking(call_info, *ppCommandList->GetPointer());
    }

    virtual void Process_ID3D12GraphicsCommandList_Reset(const ApiCallInfo& call_info,
                                                         format::HandleId   object_id,
                                                         HRESULT            return_value,
                                                         format::HandleId   pAllocator,
                                                         format::HandleId   pInitialState) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        InitializeTracking(call_info, object_id);
    }

    virtual void Process_ID3D12GraphicsCommandList4_BeginRenderPass(
        const ApiCallInfo&                                                  call_info,
        format::HandleId                                                    object_id,
        UINT                                                                NumRenderTargets,
        StructPointerDecoder<Decoded_D3D12_RENDER_PASS_RENDER_TARGET_DESC>* pRenderTargets,
        StructPointerDecoder<Decoded_D3D12_RENDER_PASS_DEPTH_STENCIL_DESC>* pDepthStencil,
        D3D12_RENDER_PASS_FLAGS                                             Flags) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                it->second.current_begin_renderpass_block_index   = call_info.index;
                it->second.current_set_render_targets_block_index = 0;
            }
        }
    }

    virtual void Process_ID3D12GraphicsCommandList4_EndRenderPass(const ApiCallInfo& call_info,
                                                                  format::HandleId   object_id) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                for (auto& draw_call : it->second.track_dump_draw_calls)
                {
                    if (draw_call->begin_renderpass_block_index != 0 && draw_call->end_renderpass_block_index == 0)
                    {
                        draw_call->end_renderpass_block_index = call_info.index;
                    }
                }
            }
        }
    }

    virtual void Process_ID3D12GraphicsCommandList_OMSetRenderTargets(
        const ApiCallInfo&                                         call_info,
        format::HandleId                                           object_id,
        UINT                                                       NumRenderTargetDescriptors,
        StructPointerDecoder<Decoded_D3D12_CPU_DESCRIPTOR_HANDLE>* pRenderTargetDescriptors,
        BOOL                                                       RTsSingleHandleToDescriptorRange,
        StructPointerDecoder<Decoded_D3D12_CPU_DESCRIPTOR_HANDLE>* pDepthStencilDescriptor) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                it->second.current_set_render_targets_block_index = call_info.index;
                it->second.current_begin_renderpass_block_index   = 0;
            }
        }
    }

    virtual void Process_ID3D12GraphicsCommandList_SetComputeRootSignature(const ApiCallInfo& call_info,
                                                                           format::HandleId   object_id,
                                                                           format::HandleId   pRootSignature) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                it->second.current_compute_root_signature_handle_id = pRootSignature;
            }
        }
    }

    virtual void Process_ID3D12GraphicsCommandList_SetGraphicsRootSignature(const ApiCallInfo& call_info,
                                                                            format::HandleId   object_id,
                                                                            format::HandleId   pRootSignature) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                it->second.current_graphics_root_signature_handle_id = pRootSignature;
            }
        }
    }

    virtual void Process_ID3D12GraphicsCommandList_IASetVertexBuffers(
        const ApiCallInfo&                                      call_info,
        format::HandleId                                        object_id,
        UINT                                                    StartSlot,
        UINT                                                    NumViews,
        StructPointerDecoder<Decoded_D3D12_VERTEX_BUFFER_VIEW>* pViews) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                it->second.current_captured_vertex_buffer_views.resize(NumViews);
                auto views = pViews->GetMetaStructPointer();
                for (uint32_t i = 0; i < NumViews; ++i)
                {
                    it->second.current_captured_vertex_buffer_views[i] = *(views[i].decoded_value);
                }
            }
        }
    }

    virtual void Process_ID3D12GraphicsCommandList_IASetIndexBuffer(
        const ApiCallInfo&                                     call_info,
        format::HandleId                                       object_id,
        StructPointerDecoder<Decoded_D3D12_INDEX_BUFFER_VIEW>* pView) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                auto view = pView->GetMetaStructPointer();
                if (view != nullptr)
                {
                    it->second.current_captured_index_buffer_view = *(view->decoded_value);
                }
                else
                {
                    it->second.current_captured_index_buffer_view = D3D12_INDEX_BUFFER_VIEW();
                }
            }
        }
    }

    virtual void Process_ID3D12GraphicsCommandList_SetDescriptorHeaps(
        const ApiCallInfo&                           call_info,
        format::HandleId                             object_id,
        UINT                                         NumDescriptorHeaps,
        HandlePointerDecoder<ID3D12DescriptorHeap*>* ppDescriptorHeaps) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                it->second.current_descriptor_heap_ids.resize(NumDescriptorHeaps);
                auto heap_ids = ppDescriptorHeaps->GetPointer();
                for (uint32_t i = 0; i < NumDescriptorHeaps; ++i)
                {
                    it->second.current_descriptor_heap_ids[i] = heap_ids[i];
                }
            }
        }
    }

    virtual void Process_ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(
        const ApiCallInfo&                  call_info,
        format::HandleId                    object_id,
        UINT                                RootParameterIndex,
        Decoded_D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                TrackRootParameter param                = {};
                param.cmd_bind_type                     = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                param.cmd_bind_captured_base_descriptor = (*BaseDescriptor.decoded_value);
                it->second.current_compute_root_parameters[RootParameterIndex] = param;
            }
        }
    }

    virtual void Process_ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(
        const ApiCallInfo&                  call_info,
        format::HandleId                    object_id,
        UINT                                RootParameterIndex,
        Decoded_D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                TrackRootParameter param                = {};
                param.cmd_bind_type                     = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                param.cmd_bind_captured_base_descriptor = (*BaseDescriptor.decoded_value);
                it->second.current_graphics_root_parameters[RootParameterIndex] = param;
            }
        }
    }

    virtual void Process_ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(const ApiCallInfo& call_info,
                                                                               format::HandleId   object_id,
                                                                               UINT               RootParameterIndex,
                                                                               UINT               SrcData,
                                                                               UINT DestOffsetIn32BitValues) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                TrackRootParameter param = {};
                param.cmd_bind_type      = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                it->second.current_compute_root_parameters[RootParameterIndex] = param;
            }
        }
    }

    virtual void Process_ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstant(const ApiCallInfo& call_info,
                                                                                format::HandleId   object_id,
                                                                                UINT               RootParameterIndex,
                                                                                UINT               SrcData,
                                                                                UINT DestOffsetIn32BitValues) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                TrackRootParameter param = {};
                param.cmd_bind_type      = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                it->second.current_graphics_root_parameters[RootParameterIndex] = param;
            }
        }  
    }

    virtual void Process_ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(const ApiCallInfo& call_info,
                                                                                format::HandleId   object_id,
                                                                                UINT               RootParameterIndex,
                                                                                UINT               Num32BitValuesToSet,
                                                                                PointerDecoder<uint8_t>* pSrcData,
                                                                                UINT DestOffsetIn32BitValues) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                TrackRootParameter param = {};
                param.cmd_bind_type      = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                it->second.current_compute_root_parameters[RootParameterIndex] = param;
            }
        }
    }

    virtual void Process_ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(const ApiCallInfo& call_info,
                                                                                 format::HandleId   object_id,
                                                                                 UINT               RootParameterIndex,
                                                                                 UINT               Num32BitValuesToSet,
                                                                                 PointerDecoder<uint8_t>* pSrcData,
                                                                                 UINT DestOffsetIn32BitValues) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                TrackRootParameter param = {};
                param.cmd_bind_type      = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                it->second.current_graphics_root_parameters[RootParameterIndex] = param;
            }
        }  
    }

    virtual void Process_ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(
        const ApiCallInfo&        call_info,
        format::HandleId          object_id,
        UINT                      RootParameterIndex,
        D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                TrackRootParameter param                                       = {};
                param.cmd_bind_type                                            = D3D12_ROOT_PARAMETER_TYPE_CBV;
                param.cmd_bind_captured_buffer_location                        = BufferLocation;
                it->second.current_compute_root_parameters[RootParameterIndex] = param;
            }
        }  
    }

    virtual void Process_ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(
        const ApiCallInfo&        call_info,
        format::HandleId          object_id,
        UINT                      RootParameterIndex,
        D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                TrackRootParameter param                                        = {};
                param.cmd_bind_type                                             = D3D12_ROOT_PARAMETER_TYPE_CBV;
                param.cmd_bind_captured_buffer_location                         = BufferLocation;
                it->second.current_graphics_root_parameters[RootParameterIndex] = param;
            }
        }  
    }

    virtual void Process_ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(
        const ApiCallInfo&        call_info,
        format::HandleId          object_id,
        UINT                      RootParameterIndex,
        D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                TrackRootParameter param                                       = {};
                param.cmd_bind_type                                            = D3D12_ROOT_PARAMETER_TYPE_SRV;
                param.cmd_bind_captured_buffer_location                        = BufferLocation;
                it->second.current_compute_root_parameters[RootParameterIndex] = param;
            }
        } 
    }

    virtual void Process_ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(
        const ApiCallInfo&        call_info,
        format::HandleId          object_id,
        UINT                      RootParameterIndex,
        D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                TrackRootParameter param                                        = {};
                param.cmd_bind_type                                             = D3D12_ROOT_PARAMETER_TYPE_SRV;
                param.cmd_bind_captured_buffer_location                         = BufferLocation;
                it->second.current_graphics_root_parameters[RootParameterIndex] = param;
            }
        }  
    }

    virtual void Process_ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(
        const ApiCallInfo&        call_info,
        format::HandleId          object_id,
        UINT                      RootParameterIndex,
        D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                TrackRootParameter param                                        = {};
                param.cmd_bind_type                                             = D3D12_ROOT_PARAMETER_TYPE_UAV;
                param.cmd_bind_captured_buffer_location                         = BufferLocation;
                it->second.current_compute_root_parameters[RootParameterIndex]  = param;
            }
        } 
    }

    virtual void Process_ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(
        const ApiCallInfo&        call_info,
        format::HandleId          object_id,
        UINT                      RootParameterIndex,
        D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                TrackRootParameter param                                        = {};
                param.cmd_bind_type                                             = D3D12_ROOT_PARAMETER_TYPE_UAV;
                param.cmd_bind_captured_buffer_location                         = BufferLocation;
                it->second.current_graphics_root_parameters[RootParameterIndex] = param;
            }
        } 
    }

    virtual void Process_ID3D12GraphicsCommandList_DrawInstanced(const ApiCallInfo& call_info,
                                                                 format::HandleId   object_id,
                                                                 UINT               VertexCountPerInstance,
                                                                 UINT               InstanceCount,
                                                                 UINT               StartVertexLocation,
                                                                 UINT               StartInstanceLocation) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        TrackTargetDrawCall(call_info, object_id, DumpDrawCallType::kDraw);
    }

    virtual void Process_ID3D12GraphicsCommandList_DrawIndexedInstanced(const ApiCallInfo& call_info,
                                                                        format::HandleId   object_id,
                                                                        UINT               IndexCountPerInstance,
                                                                        UINT               InstanceCount,
                                                                        UINT               StartIndexLocation,
                                                                        INT                BaseVertexLocation,
                                                                        UINT StartInstanceLocation) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        TrackTargetDrawCall(call_info, object_id, DumpDrawCallType::kDraw);
    }

    virtual void Process_ID3D12GraphicsCommandList_Dispatch(const ApiCallInfo& call_info,
                                                            format::HandleId   object_id,
                                                            UINT               ThreadGroupCountX,
                                                            UINT               ThreadGroupCountY,
                                                            UINT               ThreadGroupCountZ) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        TrackTargetDrawCall(call_info, object_id, DumpDrawCallType::kDispatch);
    }

    virtual void Process_ID3D12GraphicsCommandList_ExecuteIndirect(const ApiCallInfo& call_info,
                                                                   format::HandleId   object_id,
                                                                   format::HandleId   pCommandSignature,
                                                                   UINT               MaxCommandCount,
                                                                   format::HandleId   pArgumentBuffer,
                                                                   UINT64             ArgumentBufferOffset,
                                                                   format::HandleId   pCountBuffer,
                                                                   UINT64             CountBufferOffset) override
    {
        CHECK_DX12_CONSUMER_USAGE()
        TrackTargetDrawCall(call_info,
                            object_id,
                            DumpDrawCallType::kIndirect,
                            pArgumentBuffer,
                            ArgumentBufferOffset,
                            pCountBuffer,
                            CountBufferOffset);
    }

    virtual void Process_ID3D12GraphicsCommandList_ExecuteBundle(const ApiCallInfo& call_info,
                                                                 format::HandleId   object_id,
                                                                 format::HandleId   pCommandList) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        TrackTargetDrawCall(call_info,
                            object_id,
                            DumpDrawCallType::kBundle,
                            format::kNullHandleId,
                            0,
                            format::kNullHandleId,
                            0,
                            pCommandList);
    }

    virtual void Process_ID3D12GraphicsCommandList_Close(const ApiCallInfo& call_info,
                                                         format::HandleId   object_id,
                                                         HRESULT            return_value) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                for (auto& draw_call : it->second.track_dump_draw_calls)
                {
                    draw_call->close_block_index = call_info.index;
                }
            }
        }
    }

    virtual void
    Process_ID3D12CommandQueue_ExecuteCommandLists(const ApiCallInfo&                        call_info,
                                                   format::HandleId                          object_id,
                                                   UINT                                      NumCommandLists,
                                                   HandlePointerDecoder<ID3D12CommandList*>* ppCommandLists) override
    {
        CHECK_DX12_CONSUMER_USAGE();
        if (target_command_list_ == format::kNullHandleId)
        {
            if (track_submit_index_ == dump_resources_target_.submit_index)
            {
                if (NumCommandLists <= dump_resources_target_.command_index)
                {
                    if (TEST_AVAILABLE_ARGS > 0)
                    {
                        ++track_submit_index_;
                        ++dump_resources_target_.submit_index;
                        dump_resources_target_.command_index   = 0;
                        dump_resources_target_.draw_call_index = 0;
                        is_modified_args                       = true;
                        return;
                    }
                    else
                    {
                        GFXRECON_LOG_FATAL("The target command index(%d) of dump resources is out of range(%d).",
                                           dump_resources_target_.command_index,
                                           NumCommandLists);
                        GFXRECON_ASSERT(NumCommandLists > dump_resources_target_.command_index);
                    }
                }

                auto command_lists = ppCommandLists->GetPointer();
                for (uint32_t cmd_index = dump_resources_target_.command_index; cmd_index < NumCommandLists;
                     ++cmd_index)
                {
                    auto cmd_list = command_lists[cmd_index];
                    auto it       = track_commandlist_infos_.find(cmd_list);
                    GFXRECON_ASSERT(it != track_commandlist_infos_.end());

                    uint32_t all_draw_call_count = 0; // Include normal draw call and bundle draw call.
                    uint32_t draw_call_index     = 0;
                    for (auto& draw_call : it->second.track_dump_draw_calls)
                    {
                        if (draw_call->bundle_commandlist_id != format::kNullHandleId)
                        {
                            auto bundle_it = track_commandlist_infos_.find(draw_call->bundle_commandlist_id);
                            if (bundle_it != track_commandlist_infos_.end())
                            {
                                for (auto& bundle_draw_call : bundle_it->second.track_dump_draw_calls)
                                {
                                    ++all_draw_call_count;
                                    if (all_draw_call_count > dump_resources_target_.draw_call_index)
                                    {
                                        if (TEST_AVAILABLE_ARGS == 2 &&
                                            bundle_draw_call->drawcall_type != DumpDrawCallType::kDraw)
                                        {
                                            // Finding the target in the following draw call.
                                            is_modified_args = true;
                                            ++dump_resources_target_.draw_call_index;
                                        }
                                        else
                                        {
                                            // Found the target.
                                            draw_call->bundle_target_draw_call = bundle_draw_call;

                                            draw_call->execute_block_index = call_info.index;
                                            check_dump_resources_complete_ = true;
                                            target_command_list_           = cmd_list;
                                            target_draw_call_index_        = draw_call_index;
                                            break;
                                        }
                                    }
                                }

                                // Found the target.
                                if (target_command_list_ != format::kNullHandleId)
                                {
                                    break;
                                }
                            }
                        }
                        else
                        {
                            ++all_draw_call_count;
                            if (all_draw_call_count > dump_resources_target_.draw_call_index)
                            {
                                if (TEST_AVAILABLE_ARGS == 2 && draw_call->drawcall_type != DumpDrawCallType::kDraw)
                                {
                                    // Finding the target in the following draw call.
                                    is_modified_args = true;
                                    ++dump_resources_target_.draw_call_index;
                                }
                                else
                                {
                                    // Found the target.
                                    draw_call->execute_block_index = call_info.index;
                                    check_dump_resources_complete_ = true;
                                    target_command_list_           = cmd_list;
                                    target_draw_call_index_        = draw_call_index;
                                    break;
                                }
                            }
                        }
                        ++draw_call_index;
                    }

                    // Found the target.
                    if (target_command_list_ != format::kNullHandleId)
                    {
                        break;
                    }
                    else
                    {
                        if (TEST_AVAILABLE_ARGS > 0)
                        {
                            // Finding the target in the following command list.
                            is_modified_args = true;
                            ++dump_resources_target_.command_index;
                            dump_resources_target_.draw_call_index = 0;
                        }
                        else
                        {
                            GFXRECON_LOG_FATAL("The target draw call index(%d) of dump resources is out of range(%d).",
                                               dump_resources_target_.draw_call_index,
                                               all_draw_call_count);
                            GFXRECON_ASSERT(all_draw_call_count > dump_resources_target_.draw_call_index);
                            break;
                        }
                    }
                }

                // It didn't find the target draw call.
                if (TEST_AVAILABLE_ARGS > 0 && target_command_list_ == format::kNullHandleId)
                {
                    // Find a draw call in the following submit.
                    is_modified_args = true;
                    ++dump_resources_target_.submit_index;
                    dump_resources_target_.command_index   = 0;
                    dump_resources_target_.draw_call_index = 0;
                }
            }
            ++track_submit_index_;
        }
    }

    virtual bool IsComplete(uint64_t block_index) override
    {
        return check_dx12_consumer_usage_complete_ && (check_dump_resources_complete_ || !enable_dump_resources_);
    }

  private:
    bool dx12_consumer_usage_{ false };
    bool check_dx12_consumer_usage_complete_{ false };

    bool                enable_dump_resources_{ false };
    bool                check_dump_resources_complete_{ false };
    bool                is_modified_args{ false };
    DumpResourcesTarget dump_resources_target_{};
    uint32_t            track_submit_index_{ 0 };
    format::HandleId    target_command_list_{ format::kNullHandleId };
    uint32_t            target_draw_call_index_{ 0 };

    // Key is commandlist_id. We need to know the commandlist of the info because in a commandlist block
    // between reset and close, it might have the other commandlist's commands.
    std::map<format::HandleId, TrackDumpCommandList> track_commandlist_infos_;

    void InitializeTracking(const ApiCallInfo& call_info, format::HandleId object_id)
    {
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                it->second.Clear();
                it->second.begin_block_index = call_info.index;
            }
            else
            {
                TrackDumpCommandList info = {};
                info.begin_block_index    = call_info.index;
                track_commandlist_infos_.insert({ object_id, std::move(info) });
            }
        }
    }

    void TrackTargetDrawCall(const ApiCallInfo& call_info,
                             format::HandleId   object_id,
                             DumpDrawCallType   drawcall_type,
                             format::HandleId   exe_indirect_argument_id     = format::kNullHandleId,
                             uint64_t           exe_indirect_argument_offset = 0,
                             format::HandleId   exe_indirect_count_id        = format::kNullHandleId,
                             uint64_t           exe_indirect_count_offset    = 0,
                             format::HandleId   bundle_commandlist_id        = format::kNullHandleId)
    {
        if (target_command_list_ == format::kNullHandleId)
        {
            auto it = track_commandlist_infos_.find(object_id);
            if (it != track_commandlist_infos_.end())
            {
                TrackDumpDrawCall track_draw_call                 = {};
                track_draw_call.command_list_id                   = object_id;
                track_draw_call.draw_call_block_index             = call_info.index;
                track_draw_call.drawcall_type                     = drawcall_type;
                track_draw_call.begin_block_index                 = it->second.begin_block_index;
                track_draw_call.begin_renderpass_block_index      = it->second.current_begin_renderpass_block_index;
                track_draw_call.set_render_targets_block_index    = it->second.current_set_render_targets_block_index;
                track_draw_call.captured_vertex_buffer_views      = it->second.current_captured_vertex_buffer_views;
                track_draw_call.captured_index_buffer_view        = it->second.current_captured_index_buffer_view;
                track_draw_call.descriptor_heap_ids               = it->second.current_descriptor_heap_ids;
                track_draw_call.execute_indirect_info.argument_id = exe_indirect_argument_id;
                track_draw_call.execute_indirect_info.argument_offset = exe_indirect_argument_offset;
                track_draw_call.execute_indirect_info.count_id        = exe_indirect_count_id;
                track_draw_call.execute_indirect_info.count_offset    = exe_indirect_count_offset;
                track_draw_call.bundle_commandlist_id                 = bundle_commandlist_id;
                track_draw_call.graphics_root_signature_handle_id =
                    it->second.current_graphics_root_signature_handle_id;
                track_draw_call.graphics_root_parameters         = it->second.current_graphics_root_parameters;
                track_draw_call.compute_root_signature_handle_id = it->second.current_compute_root_signature_handle_id;
                track_draw_call.compute_root_parameters          = it->second.current_compute_root_parameters;
 

                it->second.track_dump_draw_calls.emplace_back(
                    std::make_shared<TrackDumpDrawCall>(std::move(track_draw_call)));
            }
        }
    }
};

GFXRECON_END_NAMESPACE(decode)
GFXRECON_END_NAMESPACE(gfxrecon)

#endif // GFXRECON_DECODE_DX12_PRE_PROCESS_CONSUMER_H
