#!/usr/bin/env python3
#
# Copyright (c) 2021 LunarG, Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to
# deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
# sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

import sys
from base_generator import *
from dx12_base_generator import *
from base_struct_decoders_header_generator import *


class Dx12DecoderHeaderGenerator(
        Dx12BaseGenerator, BaseStructDecodersHeaderGenerator):
    """Generates C++ functions responsible for decoding Dx12 API calls."""

    def __init__(self, source_dict, dx12_prefix_strings,
                 errFile=sys.stderr,
                 warnFile=sys.stderr,
                 diagFile=sys.stdout):
        Dx12BaseGenerator.__init__(
            self, source_dict, dx12_prefix_strings,
            errFile, warnFile, diagFile)

    def beginFile(self, gen_opts):
        """Methond override."""
        BaseGenerator.beginFile(self, gen_opts)

        self.write_include()
        write('GFXRECON_BEGIN_NAMESPACE(gfxrecon)', file=self.outFile)
        write('GFXRECON_BEGIN_NAMESPACE(decode)', file=self.outFile)
        self.newline()

    def generateFeature(self):
        """Methond override."""
        Dx12BaseGenerator.generateFeature(self)
        self.write_dx12_decoder_class()

    def write_include(self):
        """Methond override."""
        code = ("\n"
                "#include \"decode/dx12_decoder_base.h\"\n"
                "\n")
        write(code, file=self.outFile)

    def get_decoder_method_body(self, params):
        return ';'

    def get_decode_function_call_body(self):
        return ';'

    def get_decode_method_call_body(self):
        return ';'

    def get_decoder_function(self, class_name, method_info,
                             indent, function_class):
        object_param = ''
        if class_name:
            class_name = '_' + class_name
            object_param = 'format::HandleId object_id, '

        return ('{}size_t {}Decode{}_{}({}const uint8_t* parameter_buffer, size_t buffer_size){}\n'  # noqa
                .format(indent, function_class, class_name,
                        method_info['name'], object_param,
                        self.get_decoder_method_body(method_info['parameters'])
                        ))

    def get_decoder_class_define(self):
        declaration = ("class Dx12Decoder : public Dx12DecoderBase\n"
                       "{{\n"
                       "  public:\n"
                       "    Dx12Decoder(){{}}\n"
                       "    virtual ~Dx12Decoder() override {{}}\n"
                       "\n"
                       "    virtual void DecodeFunctionCall(format::ApiCallId  call_id,\n"  # noqa
                       "                                    const ApiCallInfo& call_options,\n"  # noqa
                       "                                    const uint8_t*     parameter_buffer,\n"  # noqa
                       "                                    size_t             buffer_size) override{}\n"  # noqa
                       "\n"
                       "    virtual void DecodeMethodCall(format::ApiCallId  call_id,\n"  # noqa
                       "                                  format::HandleId   object_id,\n"  # noqa
                       "                                  const ApiCallInfo& call_options,\n"  # noqa
                       "                                  const uint8_t*     parameter_buffer,\n"  # noqa
                       "                                  size_t             buffer_size) override{}\n"  # noqa
                       "  private:\n".format(
                          self.get_decode_function_call_body(),
                          self.get_decode_method_call_body()))

        indent = '    '
        function_class = ''
        class_end = '};\n'
        return (declaration, indent, function_class, class_end)

    def write_dx12_decoder_class(self):
        declaration, indent, function_class, class_end =\
            self.get_decoder_class_define()
        code = declaration

        header_dict = self.source_dict['header_dict']
        for k, v in header_dict.items():
            code_length = len(code)

            for m in v.functions:
                if self.is_required_function_data(m):
                    code += self.get_decoder_function(
                        '', m, indent, function_class)

            for k2, v2 in v.classes.items():
                if self.is_required_class_data(v2):
                    for m in v2['methods']['public']:
                        code += self.get_decoder_function(
                            k2, m, indent, function_class)

            code_length2 = len(code)
            if code_length2 > code_length:
                code = code[:code_length] + '\n' + \
                    self.dx12_prefix_strings.format(
                        k) + '\n' + code[code_length:]

        code += class_end
        write(code, file=self.outFile)

    def endFile(self):
        """Methond override."""
        self.newline()
        write('GFXRECON_END_NAMESPACE(decode)', file=self.outFile)
        write('GFXRECON_END_NAMESPACE(gfxrecon)', file=self.outFile)

        # Finish processing in superclass
        BaseGenerator.endFile(self)
