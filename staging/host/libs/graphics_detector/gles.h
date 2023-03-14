/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <optional>

#include <GLES/gl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

#include "host/libs/graphics_detector/egl.h"
#include "host/libs/graphics_detector/gles_funcs.h"
#include "host/libs/graphics_detector/lib.h"

namespace cuttlefish {

#define CHECK_GL_ERROR()                                                      \
  do {                                                                        \
    if (GLenum error = gles->glGetError(); error != GL_NO_ERROR) {            \
      LOG(ERROR) << __FILE__ << ":" << __LINE__ << ":" << __PRETTY_FUNCTION__ \
                 << " found error: " << error;                                \
    }                                                                         \
  } while (0);

class Gles {
 public:
  static std::optional<Gles> Load();
  static std::optional<Gles> LoadFromEgl(Egl* egl);

  Gles(const Gles&) = delete;
  Gles& operator=(const Gles&) = delete;

  Gles(Gles&&) = default;
  Gles& operator=(Gles&&) = default;

  std::optional<GLuint> CreateShader(GLenum shader_type,
                                     const std::string& shader_source);

  std::optional<GLuint> CreateProgram(const std::string& vert_shader_source,
                                      const std::string& frag_shader_source);

#define DECLARE_GLES_FUNCTION_MEMBER_POINTER(return_type, function_name, \
                                             signature, args)            \
  return_type(*function_name) signature = nullptr;

  FOR_EACH_GLES_FUNCTION(DECLARE_GLES_FUNCTION_MEMBER_POINTER);

 private:
  Gles() = default;

  void Init();

  Lib lib_;
};

}  // namespace cuttlefish
