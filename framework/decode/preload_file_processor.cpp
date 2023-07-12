/*
** Copyright (c) 2023 LunarG, Inc.
** Copyright (c) 2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
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

#include "decode/preload_file_processor.h"
#include "util/logging.h"
#include <algorithm>

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(decode)

PreloadFileProcessor::PreloadFileProcessor() : status_(PreloadStatus::kInactive) {}

void PreloadFileProcessor::PreloadNextFrames(size_t count)
{
    // reserve enough memory to cover entire frame range
    size_t total_bytes_needed = GetRequiredByteSizeForFrames(count);
    preload_buffer_.Reserve(total_bytes_needed);
    GFXRECON_LOG_INFO("Preloading reserved %zu bytes", total_bytes_needed);

    status_ = PreloadStatus::kRecord;
    for (preload_frame_number_ = 0; preload_frame_number_ < count; ++preload_frame_number_)
    {
        size_t next_chunk_size = GetNextBufferChunkSize();
        if (next_chunk_size > 0)
        {
            while (next_chunk_size > 1 && !preload_buffer_.Reserve(next_chunk_size))
            {
                next_chunk_size -= next_chunk_size / 10;
            }
            GFXRECON_LOG_INFO("Preloading reserved additional %zu bytes", next_chunk_size);
        }
        ProcessNextFrame();
    }
    status_ = PreloadStatus::kReplay;
}

size_t PreloadFileProcessor::GetRequiredByteSizeForFrames(size_t frame_count)
{
    gfxrecon::decode::FileProcessor file_processor(UINT64_MAX);
    file_processor.Initialize(filename_);

    while (file_processor.GetCurrentFrameNumber() < current_frame_number_ + frame_count)
    {
        file_processor.ProcessNextFrame();
        GFXRECON_LOG_WARNING("Frame %zu size %zu", file_processor.GetCurrentFrameNumber(), file_processor.GetNumBytesRead());
    }
    size_t bytes_needed = file_processor.GetNumBytesRead() - bytes_read_;
    return bytes_needed;
}

size_t PreloadFileProcessor::GetNextBufferChunkSize()
{
    const size_t kMegabyte        = 1 << 20;
    size_t       bytes_to_reserve = 0;
    const size_t current_average_frame_size =
        bytes_read_ / std::max(current_frame_number_ + preload_frame_number_, (size_t)1);
    size_t       average_frame_size   = std::max(current_average_frame_size, kMegabyte);
    const size_t buffer_capacity_left = preload_buffer_.Capacity() - preload_buffer_.Size();

    if (buffer_capacity_left < average_frame_size)
    {
        bytes_to_reserve = average_frame_size;
    }
    return bytes_to_reserve;
}

PreloadFileProcessor::PreloadBuffer::PreloadBuffer() : replay_offset_(0) {}

size_t PreloadFileProcessor::PreloadBuffer::Size()
{
    return container_.size();
}

size_t PreloadFileProcessor::PreloadBuffer::Capacity()
{
    return container_.capacity();
}

bool PreloadFileProcessor::PreloadBuffer::Reserve(size_t size)
{
    bool result = true;
    try
    {
        container_.reserve(container_.size() + size);
    }
    catch (std::exception e)
    {
        result = false;
    }
    return result;
}

size_t PreloadFileProcessor::PreloadBuffer::Read(void* destination, size_t destination_size)
{
    auto remaining_buffer_data = container_.size() - replay_offset_;
    auto read_size             = destination_size > remaining_buffer_data ? remaining_buffer_data : destination_size;
    memcpy(destination, &container_[replay_offset_], read_size);
    replay_offset_ += read_size;
    return read_size;
}

void PreloadFileProcessor::PreloadBuffer::Reset()
{
    container_.clear();
    container_.shrink_to_fit();
    replay_offset_ = 0;
}

bool PreloadFileProcessor::ProcessBlocks()
{
    format::BlockHeader block_header;
    bool                success = true;

    while (success)
    {
        success = ContinueDecoding();

        if (success)
        {
            success = ReadBlockHeader(&block_header);

            if (status_ != PreloadStatus::kRecord)
            {
                for (auto* decoder : decoders_)
                {
                    decoder->SetCurrentBlockIndex(block_index_);
                }
            }

            if (success)
            {
                if (format::RemoveCompressedBlockBit(block_header.type) == format::BlockType::kFunctionCallBlock)
                {
                    format::ApiCallId api_call_id = format::ApiCallId::ApiCall_Unknown;

                    success = ReadBytes(&api_call_id, sizeof(api_call_id));

                    if (success)
                    {
                        const auto is_frame_delimiter = IsFrameDelimiter(api_call_id);
                        if (status_ == PreloadStatus::kRecord)
                        {
                            preload_buffer_.Reserve(sizeof(block_header) + block_header.size);
                            preload_buffer_.Add(&block_header);
                            preload_buffer_.Add(&api_call_id);
                            size_t parameters_size  = block_header.size - sizeof(api_call_id);
                            auto*  parameter_buffer = preload_buffer_.Add(parameters_size);
                            success                 = ReadBytes(parameter_buffer, parameters_size);
                            if (!success)
                            {
                                HandleBlockReadError(kErrorReadingBlockData, "Failed to read function call block data");
                            }

                            if (is_frame_delimiter)
                            {
                                break;
                            }
                        }
                        else
                        {
                            success = ProcessFunctionCall(block_header, api_call_id);
                            if (is_frame_delimiter)
                            {
                                // Make sure to increment the frame number on the way out.
                                ++current_frame_number_;
                                ++block_index_;
                                break;
                            }
                        }
                    }
                    else
                    {
                        HandleBlockReadError(kErrorReadingBlockHeader, "Failed to read function call block header");
                    }
                }
                else if (format::RemoveCompressedBlockBit(block_header.type) == format::BlockType::kMethodCallBlock)
                {
                    format::ApiCallId api_call_id = format::ApiCallId::ApiCall_Unknown;

                    success = ReadBytes(&api_call_id, sizeof(api_call_id));

                    if (success)
                    {
                        const auto is_frame_delimiter = IsFrameDelimiter(api_call_id);
                        if (status_ == PreloadStatus::kRecord)
                        {
                            preload_buffer_.Reserve(sizeof(block_header) + block_header.size);
                            preload_buffer_.Add(&block_header);
                            preload_buffer_.Add(&api_call_id);
                            size_t parameters_size  = block_header.size - sizeof(api_call_id);
                            auto*  parameter_buffer = preload_buffer_.Add(parameters_size);
                            success                 = ReadBytes(parameter_buffer, parameters_size);
                            if (!success)
                            {
                                HandleBlockReadError(kErrorReadingBlockData,
                                                     "Failed to preload method call block data");
                            }
                            if (is_frame_delimiter)
                            {
                                break;
                            }
                        }
                        else
                        {
                            success = ProcessMethodCall(block_header, api_call_id);
                            if (is_frame_delimiter)
                            {
                                // Make sure to increment the frame number on the way out.
                                ++current_frame_number_;
                                ++block_index_;
                                break;
                            }
                        }
                    }
                    else
                    {
                        HandleBlockReadError(kErrorReadingBlockHeader, "Failed to read function call block header");
                    }
                }
                else if (format::RemoveCompressedBlockBit(block_header.type) == format::BlockType::kMetaDataBlock)
                {
                    if (status_ == PreloadStatus::kRecord)
                    {
                        preload_buffer_.Reserve(sizeof(block_header) + block_header.size);
                        preload_buffer_.Add(&block_header);
                        size_t parameters_size  = block_header.size;
                        auto*  parameter_buffer = preload_buffer_.Add(parameters_size);
                        success                 = ReadBytes(parameter_buffer, parameters_size);
                        if (!success)
                        {
                            HandleBlockReadError(kErrorReadingBlockData, "Failed to preload meta-data block");
                        }
                    }
                    else
                    {
                        format::MetaDataId meta_data_id = format::MakeMetaDataId(
                            format::ApiFamilyId::ApiFamily_None, format::MetaDataType::kUnknownMetaDataType);

                        success = ReadBytes(&meta_data_id, sizeof(meta_data_id));

                        if (success)
                        {
                            success = ProcessMetaData(block_header, meta_data_id);
                        }
                        else
                        {
                            HandleBlockReadError(kErrorReadingBlockHeader, "Failed to read meta-data block header");
                        }
                    }
                }
                else if (block_header.type == format::BlockType::kStateMarkerBlock)
                {
                    format::MarkerType marker_type  = format::MarkerType::kUnknownMarker;
                    uint64_t           frame_number = 0;

                    if (status_ == PreloadStatus::kRecord)
                    {
                        preload_buffer_.Reserve(sizeof(block_header) + block_header.size);
                        preload_buffer_.Add(&block_header);
                        size_t parameters_size  = block_header.size;
                        auto*  parameter_buffer = preload_buffer_.Add(parameters_size);
                        success                 = ReadBytes(parameter_buffer, parameters_size);
                        if (!success)
                        {
                            HandleBlockReadError(kErrorReadingBlockData, "Failed to preload state marker block data");
                        }
                    }
                    else
                    {
                        success = ReadBytes(&marker_type, sizeof(marker_type));

                        if (success)
                        {
                            success = ProcessStateMarker(block_header, marker_type);
                        }
                        else
                        {
                            HandleBlockReadError(kErrorReadingBlockHeader, "Failed to read state marker header");
                        }
                    }
                }
                else if (block_header.type == format::BlockType::kAnnotation)
                {
                    if (annotation_handler_ != nullptr)
                    {
                        if (status_ == PreloadStatus::kRecord)
                        {
                            preload_buffer_.Reserve(sizeof(block_header) + block_header.size);
                            preload_buffer_.Add(&block_header);
                            auto* parameter_buffer = preload_buffer_.Add(block_header.size);
                            success                = ReadBytes(parameter_buffer, block_header.size);
                            if (!success)
                            {
                                HandleBlockReadError(kErrorReadingBlockData, "Failed to preload annotation block data");
                            }
                        }
                        else
                        {
                            format::AnnotationType annotation_type = format::AnnotationType::kUnknown;

                            success = ReadBytes(&annotation_type, sizeof(annotation_type));

                            if (success)
                            {
                                success = ProcessAnnotation(block_header, annotation_type);
                            }
                            else
                            {
                                HandleBlockReadError(kErrorReadingBlockHeader,
                                                     "Failed to read annotation block header");
                            }
                        }
                    }
                    else
                    {
                        // If there is no annotation handler to process the annotation, we can skip the annotation
                        // block.
                        GFXRECON_CHECK_CONVERSION_DATA_LOSS(size_t, block_header.size);
                        success = SkipBytes(static_cast<size_t>(block_header.size));
                    }
                }
                else
                {
                    // Unrecognized block type.
                    GFXRECON_LOG_WARNING("Skipping unrecognized file block with type %u", block_header.type);
                    GFXRECON_CHECK_CONVERSION_DATA_LOSS(size_t, block_header.size);
                    success = SkipBytes(static_cast<size_t>(block_header.size));
                }
            }
            else
            {
                if (feof(file_descriptor_) == 0)
                {
                    // No data has been read for the current block, so we don't use 'HandleBlockReadError' here, as
                    // it assumes that the block header has been successfully read and will print an incomplete
                    // block at end of file warning when the file is at EOF without an error. For this case (the
                    // normal EOF case) we print nothing at EOF, or print an error message and set the error code
                    // directly when not at EOF.
                    GFXRECON_LOG_ERROR("Failed to read block header");
                    error_state_ = kErrorReadingBlockHeader;
                }
            }
        }
        if (status_ != PreloadStatus::kRecord)
        {
            ++block_index_;
        }
    }

    return success;
}

bool PreloadFileProcessor::ReadBytes(void* buffer, size_t buffer_size)
{
    size_t bytes_read = 0;
    if (status_ == PreloadStatus::kReplay)
    {
        bytes_read = preload_buffer_.Read(buffer, buffer_size);
        bytes_read_ += bytes_read;
        if (preload_buffer_.ReplayFinished())
        {
            status_ = PreloadStatus::kInactive;
        }
    }
    else
    {
        bytes_read = util::platform::FileRead(buffer, 1, buffer_size, file_descriptor_);
        bytes_read_ += bytes_read;
    }
    return bytes_read == buffer_size;
}

GFXRECON_END_NAMESPACE(decode)
GFXRECON_END_NAMESPACE(gfxrecon)
