/*
** Copyright (c) 2021 LunarG, Inc.
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

#include "decode/dx12_replay_consumer_base.h"

#include "util/platform.h"

#include <cassert>

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(decode)

constexpr int32_t kDefaultWindowPositionX = 0;
constexpr int32_t kDefaultWindowPositionY = 0;

Dx12ReplayConsumerBase::Dx12ReplayConsumerBase(WindowFactory* window_factory) : window_factory_(window_factory) {}

Dx12ReplayConsumerBase::~Dx12ReplayConsumerBase()
{
    DestroyActiveWindows();
}

void Dx12ReplayConsumerBase::ProcessFillMemoryCommand(uint64_t       memory_id,
                                                      uint64_t       offset,
                                                      uint64_t       size,
                                                      const uint8_t* data)
{
    auto entry = mapped_memory_.find(memory_id);

    if (entry != mapped_memory_.end())
    {
        GFXRECON_CHECK_CONVERSION_DATA_LOSS(size_t, size);

        auto copy_size      = static_cast<size_t>(size);
        auto mapped_pointer = reinterpret_cast<uint8_t*>(entry->second) + offset;

        util::platform::MemoryCopy(mapped_pointer, copy_size, data, copy_size);
    }
    else
    {
        GFXRECON_LOG_WARNING("Skipping memory fill for unrecognized mapped memory object (ID = %" PRIu64 ")",
                             memory_id);
    }
}

void Dx12ReplayConsumerBase::MapCpuDescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE& handle)
{
    object_mapping::MapCpuDescriptorHandle(handle, descriptor_cpu_addresses_);
}

void Dx12ReplayConsumerBase::MapCpuDescriptorHandles(D3D12_CPU_DESCRIPTOR_HANDLE* handles, size_t handles_len)
{
    object_mapping::MapCpuDescriptorHandles(handles, handles_len, descriptor_cpu_addresses_);
}

void Dx12ReplayConsumerBase::MapGpuDescriptorHandle(D3D12_GPU_DESCRIPTOR_HANDLE& handle)
{
    object_mapping::MapGpuDescriptorHandle(handle, descriptor_gpu_addresses_);
}

void Dx12ReplayConsumerBase::MapGpuDescriptorHandles(D3D12_GPU_DESCRIPTOR_HANDLE* handles, size_t handles_len)
{
    object_mapping::MapGpuDescriptorHandles(handles, handles_len, descriptor_gpu_addresses_);
}

void Dx12ReplayConsumerBase::MapGpuVirtualAddress(D3D12_GPU_VIRTUAL_ADDRESS& address)
{
    object_mapping::MapGpuVirtualAddress(address, gpu_va_map_);
}

void Dx12ReplayConsumerBase::MapGpuVirtualAddresses(D3D12_GPU_VIRTUAL_ADDRESS* addresses, size_t addresses_len)
{
    object_mapping::MapGpuVirtualAddresses(addresses, addresses_len, gpu_va_map_);
}

void Dx12ReplayConsumerBase::RemoveObject(DxObjectInfo* info)
{
    if (info != nullptr)
    {
        if (info->extra_info != nullptr)
        {
            if (info->extra_info_type == DxObjectInfoType::kID3D12ResourceInfo)
            {
                auto resource_info = reinterpret_cast<D3D12ResourceInfo*>(info->extra_info);

                if (resource_info->capture_address_ != 0)
                {
                    gpu_va_map_.Remove(static_cast<ID3D12Resource*>(info->object));
                }

                for (const auto& entry : resource_info->mapped_memory_info)
                {
                    auto& mapped_info = entry.second;
                    mapped_memory_.erase(mapped_info.memory_id);
                }

                delete resource_info;
            }
            else if (info->extra_info_type == DxObjectInfoType::kID3D12DescriptorHeapInfo)
            {
                auto heap_info = reinterpret_cast<D3D12DescriptorHeapInfo*>(info->extra_info);
                descriptor_cpu_addresses_.erase(heap_info->capture_cpu_addr_begin);
                descriptor_gpu_addresses_.erase(heap_info->capture_gpu_addr_begin);
                delete heap_info;
            }
            else if (info->extra_info_type == DxObjectInfoType::kID3D12DeviceInfo)
            {
                auto device_info = reinterpret_cast<D3D12DeviceInfo*>(info->extra_info);
                delete device_info;
            }
            else if (info->extra_info_type == DxObjectInfoType::kIDxgiSwapchainInfo)
            {
                auto swapchain_info = reinterpret_cast<DxgiSwapchainInfo*>(info->extra_info);
                window_factory_->Destroy(swapchain_info->window);
                active_windows_.erase(swapchain_info->window);
                delete swapchain_info;
            }
            else
            {
                GFXRECON_LOG_ERROR("Failed to destroy extra object info for unrecognized object info type %d",
                                   info->extra_info_type);
            }

            info->extra_info_type = DxObjectInfoType::kUnused;
            info->extra_info      = nullptr;
        }

        object_mapping::RemoveObject(info->capture_id, &object_info_table_);
    }
}

void Dx12ReplayConsumerBase::CheckReplayResult(const char* call_name, HRESULT capture_result, HRESULT replay_result)
{
    if (capture_result != replay_result)
    {
        GFXRECON_LOG_ERROR("%s returned %d, which does not match the value returned at capture",
                           call_name,
                           replay_result,
                           capture_result);
    }
}

void* Dx12ReplayConsumerBase::PreProcessExternalObject(uint64_t          object_id,
                                                       format::ApiCallId call_id,
                                                       const char*       call_name)
{
    void* object = nullptr;
    switch (call_id)
    {
        case format::ApiCallId::ApiCall_IDXGIFactory2_CreateSwapChainForHwnd:
            break;

        default:
            GFXRECON_LOG_WARNING("Skipping object handle mapping for unsupported external object type processed by %s",
                                 call_name);
            break;
    }
    return object;
}

void Dx12ReplayConsumerBase::PostProcessExternalObject(
    HRESULT replay_result, void* object, uint64_t* object_id, format::ApiCallId call_id, const char* call_name)
{
    GFXRECON_UNREFERENCED_PARAMETER(replay_result);
    GFXRECON_UNREFERENCED_PARAMETER(object_id);
    GFXRECON_UNREFERENCED_PARAMETER(object);

    switch (call_id)
    {
        case format::ApiCallId::ApiCall_IDXGISurface1_GetDC:
        case format::ApiCallId::ApiCall_IDXGIFactory_GetWindowAssociation:
        case format::ApiCallId::ApiCall_IDXGISwapChain1_GetHwnd:
            break;

        default:
            GFXRECON_LOG_WARNING("Skipping object handle mapping for unsupported external object type processed by %s",
                                 call_name);
            break;
    }
}

ULONG Dx12ReplayConsumerBase::OverrideAddRef(DxObjectInfo* replay_object_info, ULONG original_result)
{
    assert((replay_object_info != nullptr) && (replay_object_info->object != nullptr));

    auto object = replay_object_info->object;

    ++(replay_object_info->ref_count);

    return object->AddRef();
}

ULONG Dx12ReplayConsumerBase::OverrideRelease(DxObjectInfo* replay_object_info, ULONG original_result)
{
    assert((replay_object_info != nullptr) && (replay_object_info->object != nullptr) &&
           (replay_object_info->ref_count > 0));

    auto object = replay_object_info->object;

    --(replay_object_info->ref_count);
    if (replay_object_info->ref_count == 0)
    {
        RemoveObject(replay_object_info);
    }

    return object->Release();
}

HRESULT Dx12ReplayConsumerBase::OverrideCreateSwapChainForHwnd(
    DxObjectInfo*                                                  replay_object_info,
    HRESULT                                                        original_result,
    DxObjectInfo*                                                  device_info,
    uint64_t                                                       hwnd_id,
    StructPointerDecoder<Decoded_DXGI_SWAP_CHAIN_DESC1>*           desc,
    StructPointerDecoder<Decoded_DXGI_SWAP_CHAIN_FULLSCREEN_DESC>* full_screen_desc,
    DxObjectInfo*                                                  restrict_to_output_info,
    HandlePointerDecoder<IDXGISwapChain1*>*                        swapchain)
{
    return CreateSwapChainForHwnd(replay_object_info,
                                  original_result,
                                  device_info,
                                  hwnd_id,
                                  desc,
                                  full_screen_desc,
                                  restrict_to_output_info,
                                  swapchain);
}

HRESULT
Dx12ReplayConsumerBase::OverrideCreateSwapChain(DxObjectInfo*                                       replay_object_info,
                                                HRESULT                                             original_result,
                                                DxObjectInfo*                                       device_info,
                                                StructPointerDecoder<Decoded_DXGI_SWAP_CHAIN_DESC>* desc,
                                                HandlePointerDecoder<IDXGISwapChain*>*              swapchain)
{
    assert(desc != nullptr);

    auto    desc_pointer = desc->GetPointer();
    HRESULT result       = E_FAIL;
    Window* window       = nullptr;

    if (desc_pointer != nullptr)
    {
        window = window_factory_->Create(kDefaultWindowPositionX,
                                         kDefaultWindowPositionY,
                                         desc_pointer->BufferDesc.Width,
                                         desc_pointer->BufferDesc.Height);
    }

    if (window != nullptr)
    {
        if (window->GetNativeHandle(Window::kWin32HWnd, reinterpret_cast<void**>(&desc_pointer->OutputWindow)))
        {
            assert((replay_object_info != nullptr) && (replay_object_info->object != nullptr) &&
                   (swapchain != nullptr));

            auto      replay_object = static_cast<IDXGIFactory*>(replay_object_info->object);
            IUnknown* device        = nullptr;

            if (device_info != nullptr)
            {
                device = device_info->object;
            }

            result = replay_object->CreateSwapChain(device, desc_pointer, swapchain->GetHandlePointer());

            if (SUCCEEDED(result))
            {
                auto object_info = reinterpret_cast<DxObjectInfo*>(swapchain->GetConsumerData(0));
                SetSwapchainInfoWindow(object_info, window);
            }
            else
            {
                window_factory_->Destroy(window);
            }
        }
        else
        {
            GFXRECON_LOG_FATAL("Failed to retrieve handle from window");
            window_factory_->Destroy(window);
        }
    }
    else
    {
        GFXRECON_LOG_FATAL("Failed to create a window.  Replay cannot continue.");
    }

    return result;
}

HRESULT
Dx12ReplayConsumerBase::OverrideCreateSwapChainForCoreWindow(DxObjectInfo* replay_object_info,
                                                             HRESULT       original_result,
                                                             DxObjectInfo* device_info,
                                                             DxObjectInfo* window_info,
                                                             StructPointerDecoder<Decoded_DXGI_SWAP_CHAIN_DESC1>* desc,
                                                             DxObjectInfo* restrict_to_output_info,
                                                             HandlePointerDecoder<IDXGISwapChain1*>* swapchain)
{
    GFXRECON_UNREFERENCED_PARAMETER(window_info);

    return CreateSwapChainForHwnd(
        replay_object_info, original_result, device_info, 0, desc, nullptr, restrict_to_output_info, swapchain);
}

HRESULT
Dx12ReplayConsumerBase::OverrideCreateSwapChainForComposition(DxObjectInfo* replay_object_info,
                                                              HRESULT       original_result,
                                                              DxObjectInfo* device_info,
                                                              StructPointerDecoder<Decoded_DXGI_SWAP_CHAIN_DESC1>* desc,
                                                              DxObjectInfo* restrict_to_output_info,
                                                              HandlePointerDecoder<IDXGISwapChain1*>* swapchain)
{
    return CreateSwapChainForHwnd(
        replay_object_info, original_result, device_info, 0, desc, nullptr, restrict_to_output_info, swapchain);
}

HRESULT Dx12ReplayConsumerBase::OverrideD3D12CreateDevice(HRESULT                      original_result,
                                                          DxObjectInfo*                adapter_info,
                                                          D3D_FEATURE_LEVEL            minimum_feature_level,
                                                          Decoded_GUID                 riid,
                                                          HandlePointerDecoder<void*>* device)
{
    GFXRECON_UNREFERENCED_PARAMETER(original_result);

    assert(device != nullptr);

    IUnknown* adapter = nullptr;
    if (adapter_info != nullptr)
    {
        adapter = adapter_info->object;
    }

    auto replay_result =
        D3D12CreateDevice(adapter, minimum_feature_level, *riid.decoded_value, device->GetHandlePointer());

    if (SUCCEEDED(replay_result) && !device->IsNull())
    {
        auto object_info = reinterpret_cast<DxObjectInfo*>(device->GetConsumerData(0));
        assert(object_info != nullptr);

        object_info->extra_info_type = DxObjectInfoType::kID3D12DeviceInfo;
        object_info->extra_info      = new D3D12DeviceInfo;
    }

    return replay_result;
}

HRESULT
Dx12ReplayConsumerBase::OverrideCreateDescriptorHeap(DxObjectInfo* replay_object_info,
                                                     HRESULT       original_result,
                                                     StructPointerDecoder<Decoded_D3D12_DESCRIPTOR_HEAP_DESC>* desc,
                                                     Decoded_GUID                                              riid,
                                                     HandlePointerDecoder<void*>*                              heap)
{
    GFXRECON_UNREFERENCED_PARAMETER(original_result);

    assert((replay_object_info != nullptr) && (replay_object_info->object != nullptr) && (desc != nullptr) &&
           (heap != nullptr));

    auto replay_object = static_cast<ID3D12Device*>(replay_object_info->object);
    auto desc_pointer  = desc->GetPointer();

    auto replay_result =
        replay_object->CreateDescriptorHeap(desc_pointer, *riid.decoded_value, heap->GetHandlePointer());

    if (SUCCEEDED(replay_result) && (desc_pointer != nullptr))
    {
        auto heap_info              = new D3D12DescriptorHeapInfo;
        heap_info->descriptor_type  = desc_pointer->Type;
        heap_info->descriptor_count = desc_pointer->NumDescriptors;

        if ((replay_object_info->extra_info != nullptr) &&
            (replay_object_info->extra_info_type == DxObjectInfoType::kID3D12DeviceInfo))
        {
            auto device_info              = reinterpret_cast<D3D12DeviceInfo*>(replay_object_info->extra_info);
            heap_info->capture_increments = device_info->capture_increments;
            heap_info->replay_increments  = device_info->replay_increments;
        }
        else
        {
            GFXRECON_LOG_FATAL("ID3D12Device object does not have an associated info structure");
        }

        auto object_info = reinterpret_cast<DxObjectInfo*>(heap->GetConsumerData(0));
        assert(object_info != nullptr);

        object_info->extra_info_type = DxObjectInfoType::kID3D12DescriptorHeapInfo;
        object_info->extra_info      = heap_info;
    }

    return replay_result;
}

UINT Dx12ReplayConsumerBase::OverrideGetDescriptorHandleIncrementSize(DxObjectInfo*              replay_object_info,
                                                                      UINT                       original_result,
                                                                      D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type)
{
    assert((replay_object_info != nullptr) && (replay_object_info->object != nullptr));

    auto replay_object = static_cast<ID3D12Device*>(replay_object_info->object);
    auto replay_result = replay_object->GetDescriptorHandleIncrementSize(descriptor_heap_type);

    if ((replay_object_info->extra_info != nullptr) &&
        (replay_object_info->extra_info_type == DxObjectInfoType::kID3D12DeviceInfo))
    {
        auto device_info = reinterpret_cast<D3D12DeviceInfo*>(replay_object_info->extra_info);
        (*device_info->capture_increments)[descriptor_heap_type] = original_result;
        (*device_info->replay_increments)[descriptor_heap_type]  = replay_result;
    }
    else
    {
        GFXRECON_LOG_FATAL("ID3D12Device object does not have an associated info structure");
    }

    return replay_result;
}

D3D12_CPU_DESCRIPTOR_HANDLE
Dx12ReplayConsumerBase::OverrideGetCPUDescriptorHandleForHeapStart(
    DxObjectInfo* replay_object_info, const Decoded_D3D12_CPU_DESCRIPTOR_HANDLE& original_result)
{
    assert((replay_object_info != nullptr) && (replay_object_info->object != nullptr));

    auto replay_object = static_cast<ID3D12DescriptorHeap*>(replay_object_info->object);

    auto replay_result = replay_object->GetCPUDescriptorHandleForHeapStart();

    if ((replay_object_info->extra_info != nullptr) &&
        (replay_object_info->extra_info_type == DxObjectInfoType::kID3D12DescriptorHeapInfo))
    {
        auto heap_info = reinterpret_cast<D3D12DescriptorHeapInfo*>(replay_object_info->extra_info);

        // Only initialize on the first call.
        if (heap_info->capture_cpu_addr_begin == 0)
        {
            heap_info->capture_cpu_addr_begin = original_result.decoded_value->ptr;
            heap_info->replay_cpu_addr_begin  = replay_result.ptr;

            descriptor_cpu_addresses_[heap_info->capture_cpu_addr_begin] = heap_info;
        }
    }
    else
    {
        GFXRECON_LOG_FATAL("ID3D12DescriptorHeap object does not have an associated info structure");
    }

    return replay_result;
}

D3D12_GPU_DESCRIPTOR_HANDLE
Dx12ReplayConsumerBase::OverrideGetGPUDescriptorHandleForHeapStart(
    DxObjectInfo* replay_object_info, const Decoded_D3D12_GPU_DESCRIPTOR_HANDLE& original_result)
{
    assert((replay_object_info != nullptr) && (replay_object_info->object != nullptr));

    auto replay_object = static_cast<ID3D12DescriptorHeap*>(replay_object_info->object);

    auto replay_result = replay_object->GetGPUDescriptorHandleForHeapStart();

    if ((replay_object_info->extra_info != nullptr) &&
        (replay_object_info->extra_info_type == DxObjectInfoType::kID3D12DescriptorHeapInfo))
    {
        auto heap_info = reinterpret_cast<D3D12DescriptorHeapInfo*>(replay_object_info->extra_info);

        // Only initialize on the first call.
        if (heap_info->capture_gpu_addr_begin == 0)
        {
            heap_info->capture_gpu_addr_begin = original_result.decoded_value->ptr;
            heap_info->replay_gpu_addr_begin  = replay_result.ptr;

            descriptor_gpu_addresses_[heap_info->capture_gpu_addr_begin] = heap_info;
        }
    }
    else
    {
        GFXRECON_LOG_FATAL("ID3D12DescriptorHeap object does not have an associated info structure");
    }

    return replay_result;
}

D3D12_GPU_VIRTUAL_ADDRESS
Dx12ReplayConsumerBase::OverrideGetGpuVirtualAddress(DxObjectInfo*             replay_object_info,
                                                     D3D12_GPU_VIRTUAL_ADDRESS original_result)
{
    assert((replay_object_info != nullptr) && (replay_object_info->object != nullptr));

    auto replay_object = static_cast<ID3D12Resource*>(replay_object_info->object);

    auto replay_result = replay_object->GetGPUVirtualAddress();

    if ((original_result != 0) && (replay_result != 0))
    {
        auto resource_info = reinterpret_cast<D3D12ResourceInfo*>(replay_object_info->extra_info);

        if (resource_info == nullptr)
        {
            resource_info = new D3D12ResourceInfo;

            replay_object_info->extra_info_type = DxObjectInfoType::kID3D12ResourceInfo;
            replay_object_info->extra_info      = resource_info;
        }

        assert(replay_object_info->extra_info_type == DxObjectInfoType::kID3D12ResourceInfo);

        // Only initialize on the first call.
        if (resource_info->capture_address_ == 0)
        {
            resource_info->capture_address_ = original_result;
            resource_info->replay_address_  = replay_result;

            auto desc = replay_object->GetDesc();
            gpu_va_map_.Add(replay_object, original_result, replay_result, &desc);
        }
    }

    return replay_result;
}

HRESULT Dx12ReplayConsumerBase::OverrideResourceMap(DxObjectInfo*                              replay_object_info,
                                                    HRESULT                                    original_result,
                                                    UINT                                       subresource,
                                                    StructPointerDecoder<Decoded_D3D12_RANGE>* read_range,
                                                    PointerDecoder<uint64_t, void*>*           data)
{
    assert((replay_object_info != nullptr) && (replay_object_info->object != nullptr) && (read_range != nullptr) &&
           (data != nullptr));

    auto id_pointer    = data->GetPointer();
    auto data_pointer  = data->GetOutputPointer();
    auto replay_object = static_cast<ID3D12Resource*>(replay_object_info->object);

    auto result = replay_object->Map(subresource, read_range->GetPointer(), data_pointer);

    if (SUCCEEDED(result) && (id_pointer != nullptr) && (data_pointer != nullptr) && (*data_pointer != nullptr))
    {
        auto resource_info = reinterpret_cast<D3D12ResourceInfo*>(replay_object_info->extra_info);

        if (resource_info == nullptr)
        {
            resource_info = new D3D12ResourceInfo;

            replay_object_info->extra_info_type = DxObjectInfoType::kID3D12ResourceInfo;
            replay_object_info->extra_info      = resource_info;
        }

        assert(replay_object_info->extra_info_type == DxObjectInfoType::kID3D12ResourceInfo);

        auto& memory_info     = resource_info->mapped_memory_info[subresource];
        memory_info.memory_id = *id_pointer;
        ++(memory_info.count);

        mapped_memory_[*id_pointer] = *data_pointer;
    }

    return result;
}

void Dx12ReplayConsumerBase::OverrideResourceUnmap(DxObjectInfo*                              replay_object_info,
                                                   UINT                                       subresource,
                                                   StructPointerDecoder<Decoded_D3D12_RANGE>* written_range)
{
    assert((replay_object_info != nullptr) && (replay_object_info->object != nullptr) && (written_range != nullptr));

    auto replay_object = static_cast<ID3D12Resource*>(replay_object_info->object);
    auto resource_info = reinterpret_cast<D3D12ResourceInfo*>(replay_object_info->extra_info);

    if (resource_info != nullptr)
    {
        assert(replay_object_info->extra_info_type == DxObjectInfoType::kID3D12ResourceInfo);

        auto entry = resource_info->mapped_memory_info.find(subresource);
        if (entry != resource_info->mapped_memory_info.end())
        {
            auto& memory_info = entry->second;

            assert(memory_info.count > 0);

            --(memory_info.count);
            if (memory_info.count == 0)
            {
                mapped_memory_.erase(memory_info.memory_id);
                resource_info->mapped_memory_info.erase(entry);
            }
        }
    }

    replay_object->Unmap(subresource, written_range->GetPointer());
}

HRESULT
Dx12ReplayConsumerBase::OverrideWriteToSubresource(DxObjectInfo*                            replay_object_info,
                                                   HRESULT                                  original_result,
                                                   UINT                                     dst_subresource,
                                                   StructPointerDecoder<Decoded_D3D12_BOX>* dst_box,
                                                   uint64_t                                 src_data,
                                                   UINT                                     src_row_pitch,
                                                   UINT                                     src_depth_pitch)
{
    GFXRECON_UNREFERENCED_PARAMETER(replay_object_info);
    GFXRECON_UNREFERENCED_PARAMETER(original_result);
    GFXRECON_UNREFERENCED_PARAMETER(dst_subresource);
    GFXRECON_UNREFERENCED_PARAMETER(dst_box);
    GFXRECON_UNREFERENCED_PARAMETER(src_data);
    GFXRECON_UNREFERENCED_PARAMETER(src_row_pitch);
    GFXRECON_UNREFERENCED_PARAMETER(src_depth_pitch);

    // TODO(GH-71): Implement function
    return E_FAIL;
}

HRESULT
Dx12ReplayConsumerBase::OverrideReadFromSubresource(DxObjectInfo*                            replay_object_info,
                                                    HRESULT                                  original_result,
                                                    uint64_t                                 dst_data,
                                                    UINT                                     dst_row_pitch,
                                                    UINT                                     dst_depth_pitch,
                                                    UINT                                     src_subresource,
                                                    StructPointerDecoder<Decoded_D3D12_BOX>* src_box)
{
    GFXRECON_UNREFERENCED_PARAMETER(replay_object_info);
    GFXRECON_UNREFERENCED_PARAMETER(original_result);
    GFXRECON_UNREFERENCED_PARAMETER(dst_data);
    GFXRECON_UNREFERENCED_PARAMETER(dst_row_pitch);
    GFXRECON_UNREFERENCED_PARAMETER(dst_depth_pitch);
    GFXRECON_UNREFERENCED_PARAMETER(src_subresource);
    GFXRECON_UNREFERENCED_PARAMETER(src_box);

    // TODO(GH-71): Implement function
    return E_FAIL;
}

HRESULT Dx12ReplayConsumerBase::CreateSwapChainForHwnd(
    DxObjectInfo*                                                  replay_object_info,
    HRESULT                                                        original_result,
    DxObjectInfo*                                                  device_info,
    uint64_t                                                       hwnd_id,
    StructPointerDecoder<Decoded_DXGI_SWAP_CHAIN_DESC1>*           desc,
    StructPointerDecoder<Decoded_DXGI_SWAP_CHAIN_FULLSCREEN_DESC>* full_screen_desc,
    DxObjectInfo*                                                  restrict_to_output_info,
    HandlePointerDecoder<IDXGISwapChain1*>*                        swapchain)
{
    GFXRECON_UNREFERENCED_PARAMETER(hwnd_id);

    assert(desc != nullptr);

    auto    desc_pointer = desc->GetPointer();
    HRESULT result       = E_FAIL;
    Window* window       = nullptr;

    if (desc_pointer != nullptr)
    {
        window = window_factory_->Create(
            kDefaultWindowPositionX, kDefaultWindowPositionY, desc_pointer->Width, desc_pointer->Height);
    }

    if (window != nullptr)
    {
        HWND hwnd{};
        if (window->GetNativeHandle(Window::kWin32HWnd, reinterpret_cast<void**>(&hwnd)))
        {
            assert((replay_object_info != nullptr) && (replay_object_info->object != nullptr) &&
                   (full_screen_desc != nullptr) && (swapchain != nullptr));

            auto         replay_object      = static_cast<IDXGIFactory2*>(replay_object_info->object);
            IUnknown*    device             = nullptr;
            IDXGIOutput* restrict_to_output = nullptr;

            if (device_info != nullptr)
            {
                device = device_info->object;
            }

            if (restrict_to_output_info != nullptr)
            {
                restrict_to_output = static_cast<IDXGIOutput*>(restrict_to_output_info->object);
            }

            result = replay_object->CreateSwapChainForHwnd(device,
                                                           hwnd,
                                                           desc_pointer,
                                                           full_screen_desc->GetPointer(),
                                                           restrict_to_output,
                                                           swapchain->GetHandlePointer());

            if (SUCCEEDED(result))
            {
                auto object_info = reinterpret_cast<DxObjectInfo*>(swapchain->GetConsumerData(0));
                SetSwapchainInfoWindow(object_info, window);
            }
            else
            {
                window_factory_->Destroy(window);
            }
        }
        else
        {
            GFXRECON_LOG_FATAL("Failed to retrieve handle from window");
            window_factory_->Destroy(window);
        }
    }
    else
    {
        GFXRECON_LOG_FATAL("Failed to create a window.  Replay cannot continue.");
    }

    return result;
}

void Dx12ReplayConsumerBase::SetSwapchainInfoWindow(DxObjectInfo* info, Window* window)
{
    if (window != nullptr)
    {
        if (info != nullptr)
        {
            assert(info->extra_info == nullptr);

            auto swapchain_info    = new DxgiSwapchainInfo;
            swapchain_info->window = window;

            info->extra_info_type = DxObjectInfoType::kIDxgiSwapchainInfo;
            info->extra_info      = swapchain_info;
        }

        active_windows_.insert(window);
    }
}

void Dx12ReplayConsumerBase::DestroyActiveWindows()
{
    for (auto window : active_windows_)
    {
        window_factory_->Destroy(window);
    }

    active_windows_.clear();
}

GFXRECON_END_NAMESPACE(decode)
GFXRECON_END_NAMESPACE(gfxrecon)
