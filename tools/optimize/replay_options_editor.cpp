/*
** Copyright (c) 2020 LunarG, Inc.
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

#include "replay_options_editor.h"

GFXRECON_BEGIN_NAMESPACE(gfxrecon)

bool ReplayOptionsEditor::Process()
{
    bool success = true;
    if (!replay_options_.empty())
    {
        success = WriteAnnotation(
            gfxrecon::format::AnnotationType::kText, gfxrecon::format::kAnnotationLabelReplayOptions, replay_options_);
    }
    if (success)
    {
        success = success && FileTransformer::Process();
    }
    return success;
}

void ReplayOptionsEditor::SetReplayOptions(std::string replay_options)
{
    replay_options_ = replay_options;
}

bool ReplayOptionsEditor::ProcessAnnotation(const format::BlockHeader& block_header,
                                            format::AnnotationType     annotation_type,
                                            std::string                label,
                                            std::string                data)
{
    bool success = true;
    // skip existing annotations
    if (label != gfxrecon::format::kAnnotationLabelReplayOptions)
    {
        success = FileTransformer::ProcessAnnotation(block_header, annotation_type, label, data);
    }
    return success;
}

GFXRECON_END_NAMESPACE(gfxrecon)
