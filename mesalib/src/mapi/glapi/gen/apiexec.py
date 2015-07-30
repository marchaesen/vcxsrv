# Copyright (C) 2015 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

class exec_info():
    """Information relating GL APIs to a function.

    Each of the four attributes of this class, compatibility, core, es1, and
    es2, specify the minimum API version where a function can possibly exist
    in Mesa.  The version is specified as an integer of (real GL version *
    10).  For example, glCreateProgram was added in OpenGL 2.0, so
    compatibility=20 and core=31.

    If the attribute is None, then it cannot be supported by that
    API.  For example, glNewList was removed from core profiles, so
    compatibility=10 and core=None.

    Each of the attributes that is not None must have a valid value.  The
    valid ranges are:

        compatiblity: [10, 30]
        core: [31, )
        es1: [10, 11]
        es2: [20, )

    These ranges are enforced by the constructor.
    """
    def __init__(self, compatibility=None, core=None, es1=None, es2=None):
        if compatibility is not None:
            assert isinstance(compatibility, int)
            assert compatibility >= 10
            assert compatibility <= 30

        if core is not None:
            assert isinstance(core, int)
            assert core >= 31

        if es1 is not None:
            assert isinstance(es1, int)
            assert es1 == 10 or es1 == 11

        if es2 is not None:
            assert isinstance(es2, int)
            assert es2 >= 20

        self.compatibility = compatibility
        self.core = core
        self.es1 = es1
        self.es2 = es2

functions = {
    # OpenGL 3.1 / GL_ARB_texture_buffer_object.  Mesa only exposes this
    # extension with core profile.
    "TexBuffer": exec_info(core=31),

    # OpenGL 3.2 / GL_ARB_geometry_shader4.  Mesa does not support
    # GL_ARB_geometry_shader4, so OpenGL 3.2 is required.
    "FramebufferTexture": exec_info(core=32),

    # OpenGL 4.0 / GL_ARB_shader_subroutines. Mesa only exposes this
    # extension with core profile.
    "GetSubroutineUniformLocation": exec_info(core=31),
    "GetSubroutineIndex": exec_info(core=31),
    "GetActiveSubroutineUniformiv": exec_info(core=31),
    "GetActiveSubroutineUniformName": exec_info(core=31),
    "GetActiveSubroutineName": exec_info(core=31),
    "UniformSubroutinesuiv": exec_info(core=31),
    "GetUniformSubroutineuiv": exec_info(core=31),
    "GetProgramStageiv": exec_info(core=31),

    # OpenGL 4.0 / GL_ARB_gpu_shader_fp64.  The extension spec says:
    #
    #     "OpenGL 3.2 and GLSL 1.50 are required."
    "Uniform1d": exec_info(core=32),
    "Uniform2d": exec_info(core=32),
    "Uniform3d": exec_info(core=32),
    "Uniform4d": exec_info(core=32),
    "Uniform1dv": exec_info(core=32),
    "Uniform2dv": exec_info(core=32),
    "Uniform3dv": exec_info(core=32),
    "Uniform4dv": exec_info(core=32),
    "UniformMatrix2dv": exec_info(core=32),
    "UniformMatrix3dv": exec_info(core=32),
    "UniformMatrix4dv": exec_info(core=32),
    "UniformMatrix2x3dv": exec_info(core=32),
    "UniformMatrix2x4dv": exec_info(core=32),
    "UniformMatrix3x2dv": exec_info(core=32),
    "UniformMatrix3x4dv": exec_info(core=32),
    "UniformMatrix4x2dv": exec_info(core=32),
    "UniformMatrix4x3dv": exec_info(core=32),
    "GetUniformdv": exec_info(core=32),

    # OpenGL 4.1 / GL_ARB_vertex_attrib_64bit.  The extension spec says:
    #
    #     "OpenGL 3.0 and GLSL 1.30 are required.
    #
    #     ARB_gpu_shader_fp64 (or equivalent functionality) is required."
    #
    # For Mesa this effectively means OpenGL 3.2 is required.  It seems
    # unlikely that Mesa will ever get support for any of the NV extensions
    # that add "equivalent functionality."
    "VertexAttribL1d": exec_info(core=32),
    "VertexAttribL2d": exec_info(core=32),
    "VertexAttribL3d": exec_info(core=32),
    "VertexAttribL4d": exec_info(core=32),
    "VertexAttribL1dv": exec_info(core=32),
    "VertexAttribL2dv": exec_info(core=32),
    "VertexAttribL3dv": exec_info(core=32),
    "VertexAttribL4dv": exec_info(core=32),
    "VertexAttribLPointer": exec_info(core=32),
    "GetVertexAttribLdv": exec_info(core=32),

    # OpenGL 4.1 / GL_ARB_viewport_array.  The extension spec says:
    #
    #     "OpenGL 3.2 or the EXT_geometry_shader4 or ARB_geometry_shader4
    #     extensions are required."
    #
    # Mesa does not support either of the geometry shader extensions, so
    # OpenGL 3.2 is required.
    "ViewportArrayv": exec_info(core=32),
    "ViewportIndexedf": exec_info(core=32),
    "ViewportIndexedfv": exec_info(core=32),
    "ScissorArrayv": exec_info(core=32),
    "ScissorIndexed": exec_info(core=32),
    "ScissorIndexedv": exec_info(core=32),
    "DepthRangeArrayv": exec_info(core=32),
    "DepthRangeIndexed": exec_info(core=32),
    # GetFloati_v also GL_ARB_shader_atomic_counters
    # GetDoublei_v also GL_ARB_shader_atomic_counters

    # OpenGL 4.3 / GL_ARB_texture_buffer_range.  Mesa can expose the extension
    # with OpenGL 3.1.
    "TexBufferRange": exec_info(core=31),

    # OpenGL 4.3 / GL_ARB_framebuffer_no_attachments.  Mesa can expose the
    # extension with OpenGL 3.0.
    "FramebufferParameteri": exec_info(compatibility=30, core=31),
    "GetFramebufferParameteri": exec_info(compatibility=30, core=31),

    # OpenGL 4.5 / GL_ARB_direct_state_access.   Mesa can expose the extension
    # with core profile.
    "CreateTransformFeedbacks": exec_info(core=31),
    "TransformFeedbackBufferBase": exec_info(core=31),
    "TransformFeedbackBufferRange": exec_info(core=31),
    "GetTransformFeedbackiv": exec_info(core=31),
    "GetTransformFeedbacki_v": exec_info(core=31),
    "GetTransformFeedbacki64_v": exec_info(core=31),
    "CreateBuffers": exec_info(core=31),
    "NamedBufferStorage": exec_info(core=31),
    "NamedBufferData": exec_info(core=31),
    "NamedBufferSubData": exec_info(core=31),
    "CopyNamedBufferSubData": exec_info(core=31),
    "ClearNamedBufferData": exec_info(core=31),
    "ClearNamedBufferSubData": exec_info(core=31),
    "MapNamedBuffer": exec_info(core=31),
    "MapNamedBufferRange": exec_info(core=31),
    "UnmapNamedBuffer": exec_info(core=31),
    "FlushMappedNamedBufferRange": exec_info(core=31),
    "GetNamedBufferParameteriv": exec_info(core=31),
    "GetNamedBufferParameteri64v": exec_info(core=31),
    "GetNamedBufferPointerv": exec_info(core=31),
    "GetNamedBufferSubData": exec_info(core=31),
    "CreateFramebuffers": exec_info(core=31),
    "NamedFramebufferRenderbuffer": exec_info(core=31),
    "NamedFramebufferParameteri": exec_info(core=31),
    "NamedFramebufferTexture": exec_info(core=31),
    "NamedFramebufferTextureLayer": exec_info(core=31),
    "NamedFramebufferDrawBuffer": exec_info(core=31),
    "NamedFramebufferDrawBuffers": exec_info(core=31),
    "NamedFramebufferReadBuffer": exec_info(core=31),
    "InvalidateNamedFramebufferData": exec_info(core=31),
    "InvalidateNamedFramebufferSubData": exec_info(core=31),
    "ClearNamedFramebufferiv": exec_info(core=31),
    "ClearNamedFramebufferuiv": exec_info(core=31),
    "ClearNamedFramebufferfv": exec_info(core=31),
    "ClearNamedFramebufferfi": exec_info(core=31),
    "BlitNamedFramebuffer": exec_info(core=31),
    "CheckNamedFramebufferStatus": exec_info(core=31),
    "GetNamedFramebufferParameteriv": exec_info(core=31),
    "GetNamedFramebufferAttachmentParameteriv": exec_info(core=31),
    "CreateRenderbuffers": exec_info(core=31),
    "NamedRenderbufferStorage": exec_info(core=31),
    "NamedRenderbufferStorageMultisample": exec_info(core=31),
    "GetNamedRenderbufferParameteriv": exec_info(core=31),
    "CreateTextures": exec_info(core=31),
    "TextureBuffer": exec_info(core=31),
    "TextureBufferRange": exec_info(core=31),
    "TextureStorage1D": exec_info(core=31),
    "TextureStorage2D": exec_info(core=31),
    "TextureStorage3D": exec_info(core=31),
    "TextureStorage2DMultisample": exec_info(core=31),
    "TextureStorage3DMultisample": exec_info(core=31),
    "TextureSubImage1D": exec_info(core=31),
    "TextureSubImage2D": exec_info(core=31),
    "TextureSubImage3D": exec_info(core=31),
    "CompressedTextureSubImage1D": exec_info(core=31),
    "CompressedTextureSubImage2D": exec_info(core=31),
    "CompressedTextureSubImage3D": exec_info(core=31),
    "CopyTextureSubImage1D": exec_info(core=31),
    "CopyTextureSubImage2D": exec_info(core=31),
    "CopyTextureSubImage3D": exec_info(core=31),
    "TextureParameterf": exec_info(core=31),
    "TextureParameterfv": exec_info(core=31),
    "TextureParameteri": exec_info(core=31),
    "TextureParameterIiv": exec_info(core=31),
    "TextureParameterIuiv": exec_info(core=31),
    "TextureParameteriv": exec_info(core=31),
    "GenerateTextureMipmap": exec_info(core=31),
    "BindTextureUnit": exec_info(core=31),
    "GetTextureImage": exec_info(core=31),
    "GetCompressedTextureImage": exec_info(core=31),
    "GetTextureLevelParameterfv": exec_info(core=31),
    "GetTextureLevelParameteriv": exec_info(core=31),
    "GetTextureParameterfv": exec_info(core=31),
    "GetTextureParameterIiv": exec_info(core=31),
    "GetTextureParameterIuiv": exec_info(core=31),
    "GetTextureParameteriv": exec_info(core=31),
    "CreateVertexArrays": exec_info(core=31),
    "DisableVertexArrayAttrib": exec_info(core=31),
    "EnableVertexArrayAttrib": exec_info(core=31),
    "VertexArrayElementBuffer": exec_info(core=31),
    "VertexArrayVertexBuffer": exec_info(core=31),
    "VertexArrayVertexBuffers": exec_info(core=31),
    "VertexArrayAttribFormat": exec_info(core=31),
    "VertexArrayAttribIFormat": exec_info(core=31),
    "VertexArrayAttribLFormat": exec_info(core=31),
    "VertexArrayAttribBinding": exec_info(core=31),
    "VertexArrayBindingDivisor": exec_info(core=31),
    "GetVertexArrayiv": exec_info(core=31),
    "GetVertexArrayIndexediv": exec_info(core=31),
    "GetVertexArrayIndexed64iv": exec_info(core=31),
    "CreateSamplers": exec_info(core=31),
    "CreateProgramPipelines": exec_info(core=31),
    "CreateQueries": exec_info(core=31),
    "GetQueryBufferObjectiv": exec_info(core=31),
    "GetQueryBufferObjectuiv": exec_info(core=31),
    "GetQueryBufferObjecti64v": exec_info(core=31),
    "GetQueryBufferObjectui64v": exec_info(core=31),
}
