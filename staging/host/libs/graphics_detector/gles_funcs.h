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

#include <GLES/gl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl32.h>

typedef const GLubyte* GLconstubyteptr;

// clang-format off
#define FOR_EACH_GLES_COMMON_FUNCTION(X) \
  X(void, glActiveTexture, (GLenum texture), (texture)) \
  X(void, glBindBuffer, (GLenum target, GLuint buffer), (target, buffer)) \
  X(void, glBindTexture, (GLenum target, GLuint texture), (target, texture)) \
  X(void, glBlendFunc, (GLenum sfactor, GLenum dfactor), (sfactor, dfactor)) \
  X(void, glBlendEquation, (GLenum mode), (mode)) \
  X(void, glBlendEquationSeparate, (GLenum modeRGB, GLenum modeAlpha), (modeRGB, modeAlpha)) \
  X(void, glBlendFuncSeparate, (GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha), (srcRGB, dstRGB, srcAlpha, dstAlpha)) \
  X(void, glBufferData, (GLenum target, GLsizeiptr size, const GLvoid * data, GLenum usage), (target, size, data, usage)) \
  X(void, glBufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid * data), (target, offset, size, data)) \
  X(void, glClear, (GLbitfield mask), (mask)) \
  X(void, glClearColor, (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha), (red, green, blue, alpha)) \
  X(void, glClearDepthf, (GLfloat depth), (depth)) \
  X(void, glClearStencil, (GLint s), (s)) \
  X(void, glColorMask, (GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha), (red, green, blue, alpha)) \
  X(void, glCompressedTexImage2D, (GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid * data), (target, level, internalformat, width, height, border, imageSize, data)) \
  X(void, glCompressedTexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid * data), (target, level, xoffset, yoffset, width, height, format, imageSize, data)) \
  X(void, glCopyTexImage2D, (GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border), (target, level, internalFormat, x, y, width, height, border)) \
  X(void, glCopyTexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height), (target, level, xoffset, yoffset, x, y, width, height)) \
  X(void, glCullFace, (GLenum mode), (mode)) \
  X(void, glDeleteBuffers, (GLsizei n, const GLuint * buffers), (n, buffers)) \
  X(void, glDeleteTextures, (GLsizei n, const GLuint * textures), (n, textures)) \
  X(void, glDepthFunc, (GLenum func), (func)) \
  X(void, glDepthMask, (GLboolean flag), (flag)) \
  X(void, glDepthRangef, (GLfloat zNear, GLfloat zFar), (zNear, zFar)) \
  X(void, glDisable, (GLenum cap), (cap)) \
  X(void, glDrawArrays, (GLenum mode, GLint first, GLsizei count), (mode, first, count)) \
  X(void, glDrawElements, (GLenum mode, GLsizei count, GLenum type, const GLvoid * indices), (mode, count, type, indices)) \
  X(void, glEnable, (GLenum cap), (cap)) \
  X(void, glFinish, (), ()) \
  X(void, glFlush, (), ()) \
  X(void, glFrontFace, (GLenum mode), (mode)) \
  X(void, glGenBuffers, (GLsizei n, GLuint * buffers), (n, buffers)) \
  X(void, glGenTextures, (GLsizei n, GLuint * textures), (n, textures)) \
  X(void, glGetBooleanv, (GLenum pname, GLboolean * params), (pname, params)) \
  X(void, glGetBufferParameteriv, (GLenum buffer, GLenum parameter, GLint * value), (buffer, parameter, value)) \
  X(GLenum, glGetError, (), ()) \
  X(void, glGetFloatv, (GLenum pname, GLfloat * params), (pname, params)) \
  X(void, glGetIntegerv, (GLenum pname, GLint * params), (pname, params)) \
  X(GLconstubyteptr, glGetString, (GLenum name), (name)) \
  X(void, glTexParameterf, (GLenum target, GLenum pname, GLfloat param), (target, pname, param)) \
  X(void, glTexParameterfv, (GLenum target, GLenum pname, const GLfloat * params), (target, pname, params)) \
  X(void, glGetTexImage, (GLenum target, GLint level, GLenum format, GLenum type, GLvoid * pixels), (target, level, format, type, pixels)) \
  X(void, glGetTexParameterfv, (GLenum target, GLenum pname, GLfloat * params), (target, pname, params)) \
  X(void, glGetTexParameteriv, (GLenum target, GLenum pname, GLint * params), (target, pname, params)) \
  X(void, glGetTexLevelParameteriv, (GLenum target, GLint level, GLenum pname, GLint * params), (target, level, pname, params)) \
  X(void, glGetTexLevelParameterfv, (GLenum target, GLint level, GLenum pname, GLfloat * params), (target, level, pname, params)) \
  X(void, glHint, (GLenum target, GLenum mode), (target, mode)) \
  X(GLboolean, glIsBuffer, (GLuint buffer), (buffer)) \
  X(GLboolean, glIsEnabled, (GLenum cap), (cap)) \
  X(GLboolean, glIsTexture, (GLuint texture), (texture)) \
  X(void, glLineWidth, (GLfloat width), (width)) \
  X(void, glPolygonOffset, (GLfloat factor, GLfloat units), (factor, units)) \
  X(void, glPixelStorei, (GLenum pname, GLint param), (pname, param)) \
  X(void, glReadPixels, (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid * pixels), (x, y, width, height, format, type, pixels)) \
  X(void, glRenderbufferStorageMultisample, (GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height), (target, samples, internalformat, width, height)) \
  X(void, glSampleCoverage, (GLclampf value, GLboolean invert), (value, invert)) \
  X(void, glScissor, (GLint x, GLint y, GLsizei width, GLsizei height), (x, y, width, height)) \
  X(void, glStencilFunc, (GLenum func, GLint ref, GLuint mask), (func, ref, mask)) \
  X(void, glStencilMask, (GLuint mask), (mask)) \
  X(void, glStencilOp, (GLenum fail, GLenum zfail, GLenum zpass), (fail, zfail, zpass)) \
  X(void, glTexImage2D, (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid * pixels), (target, level, internalformat, width, height, border, format, type, pixels)) \
  X(void, glTexParameteri, (GLenum target, GLenum pname, GLint param), (target, pname, param)) \
  X(void, glTexParameteriv, (GLenum target, GLenum pname, const GLint * params), (target, pname, params)) \
  X(void, glTexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid * pixels), (target, level, xoffset, yoffset, width, height, format, type, pixels)) \
  X(void, glViewport, (GLint x, GLint y, GLsizei width, GLsizei height), (x, y, width, height)) \
  X(void, glPushAttrib, (GLbitfield mask), (mask)) \
  X(void, glPushClientAttrib, (GLbitfield mask), (mask)) \
  X(void, glPopAttrib, (), ()) \
  X(void, glPopClientAttrib, (), ()) \

#define FOR_EACH_GLES1_ONLY_FUNCTION(X) \
  X(void, glAlphaFunc, (GLenum func, GLclampf ref), (func, ref)) \
  X(void, glBegin, (GLenum mode), (mode)) \
  X(void, glClientActiveTexture, (GLenum texture), (texture)) \
  X(void, glClipPlane, (GLenum plane, const GLdouble * equation), (plane, equation)) \
  X(void, glColor4d, (GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha), (red, green, blue, alpha)) \
  X(void, glColor4f, (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha), (red, green, blue, alpha)) \
  X(void, glColor4fv, (const GLfloat * v), (v)) \
  X(void, glColor4ub, (GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha), (red, green, blue, alpha)) \
  X(void, glColor4ubv, (const GLubyte * v), (v)) \
  X(void, glColorPointer, (GLint size, GLenum type, GLsizei stride, const GLvoid * pointer), (size, type, stride, pointer)) \
  X(void, glDisableClientState, (GLenum array), (array)) \
  X(void, glEnableClientState, (GLenum array), (array)) \
  X(void, glEnd, (), ()) \
  X(void, glFogf, (GLenum pname, GLfloat param), (pname, param)) \
  X(void, glFogfv, (GLenum pname, const GLfloat * params), (pname, params)) \
  X(void, glFrustum, (GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar), (left, right, bottom, top, zNear, zFar)) \
  X(void, glGetClipPlane, (GLenum plane, GLdouble * equation), (plane, equation)) \
  X(void, glGetDoublev, (GLenum pname, GLdouble * params), (pname, params)) \
  X(void, glGetLightfv, (GLenum light, GLenum pname, GLfloat * params), (light, pname, params)) \
  X(void, glGetMaterialfv, (GLenum face, GLenum pname, GLfloat * params), (face, pname, params)) \
  X(void, glGetPointerv, (GLenum pname, GLvoid* * params), (pname, params)) \
  X(void, glGetTexEnvfv, (GLenum target, GLenum pname, GLfloat * params), (target, pname, params)) \
  X(void, glGetTexEnviv, (GLenum target, GLenum pname, GLint * params), (target, pname, params)) \
  X(void, glLightf, (GLenum light, GLenum pname, GLfloat param), (light, pname, param)) \
  X(void, glLightfv, (GLenum light, GLenum pname, const GLfloat * params), (light, pname, params)) \
  X(void, glLightModelf, (GLenum pname, GLfloat param), (pname, param)) \
  X(void, glLightModelfv, (GLenum pname, const GLfloat * params), (pname, params)) \
  X(void, glLoadIdentity, (), ()) \
  X(void, glLoadMatrixf, (const GLfloat * m), (m)) \
  X(void, glLogicOp, (GLenum opcode), (opcode)) \
  X(void, glMaterialf, (GLenum face, GLenum pname, GLfloat param), (face, pname, param)) \
  X(void, glMaterialfv, (GLenum face, GLenum pname, const GLfloat * params), (face, pname, params)) \
  X(void, glMultiTexCoord2fv, (GLenum target, const GLfloat * v), (target, v)) \
  X(void, glMultiTexCoord2sv, (GLenum target, const GLshort * v), (target, v)) \
  X(void, glMultiTexCoord3fv, (GLenum target, const GLfloat * v), (target, v)) \
  X(void, glMultiTexCoord3sv, (GLenum target, const GLshort * v), (target, v)) \
  X(void, glMultiTexCoord4f, (GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q), (target, s, t, r, q)) \
  X(void, glMultiTexCoord4fv, (GLenum target, const GLfloat * v), (target, v)) \
  X(void, glMultiTexCoord4sv, (GLenum target, const GLshort * v), (target, v)) \
  X(void, glMultMatrixf, (const GLfloat * m), (m)) \
  X(void, glNormal3f, (GLfloat nx, GLfloat ny, GLfloat nz), (nx, ny, nz)) \
  X(void, glNormal3fv, (const GLfloat * v), (v)) \
  X(void, glNormal3sv, (const GLshort * v), (v)) \
  X(void, glOrtho, (GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar), (left, right, bottom, top, zNear, zFar)) \
  X(void, glPointParameterf, (GLenum param, GLfloat value), (param, value)) \
  X(void, glPointParameterfv, (GLenum param, const GLfloat * values), (param, values)) \
  X(void, glPointSize, (GLfloat size), (size)) \
  X(void, glRotatef, (GLfloat angle, GLfloat x, GLfloat y, GLfloat z), (angle, x, y, z)) \
  X(void, glScalef, (GLfloat x, GLfloat y, GLfloat z), (x, y, z)) \
  X(void, glTexEnvf, (GLenum target, GLenum pname, GLfloat param), (target, pname, param)) \
  X(void, glTexEnvfv, (GLenum target, GLenum pname, const GLfloat * params), (target, pname, params)) \
  X(void, glMatrixMode, (GLenum mode), (mode)) \
  X(void, glNormalPointer, (GLenum type, GLsizei stride, const GLvoid * pointer), (type, stride, pointer)) \
  X(void, glPopMatrix, (), ()) \
  X(void, glPushMatrix, (), ()) \
  X(void, glShadeModel, (GLenum mode), (mode)) \
  X(void, glTexCoordPointer, (GLint size, GLenum type, GLsizei stride, const GLvoid * pointer), (size, type, stride, pointer)) \
  X(void, glTexEnvi, (GLenum target, GLenum pname, GLint param), (target, pname, param)) \
  X(void, glTexEnviv, (GLenum target, GLenum pname, const GLint * params), (target, pname, params)) \
  X(void, glTranslatef, (GLfloat x, GLfloat y, GLfloat z), (x, y, z)) \
  X(void, glVertexPointer, (GLint size, GLenum type, GLsizei stride, const GLvoid * pointer), (size, type, stride, pointer)) \
  X(void, glClipPlanef, (GLenum plane, const GLfloat * equation), (plane, equation)) \
  X(void, glFrustumf, (GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar), (left, right, bottom, top, zNear, zFar)) \
  X(void, glGetClipPlanef, (GLenum pname, GLfloat eqn[4]), (pname, eqn[4])) \
  X(void, glOrthof, (GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar), (left, right, bottom, top, zNear, zFar)) \
  X(void, glAlphaFuncx, (GLenum func, GLclampx ref), (func, ref)) \
  X(void, glClearColorx, (GLclampx red, GLclampx green, GLclampx blue, GLclampx alpha), (red, green, blue, alpha)) \
  X(void, glClearDepthx, (GLclampx depth), (depth)) \
  X(void, glColor4x, (GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha), (red, green, blue, alpha)) \
  X(void, glDepthRangex, (GLclampx zNear, GLclampx zFar), (zNear, zFar)) \
  X(void, glFogx, (GLenum pname, GLfixed param), (pname, param)) \
  X(void, glFogxv, (GLenum pname, const GLfixed * params), (pname, params)) \
  X(void, glFrustumx, (GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar), (left, right, bottom, top, zNear, zFar)) \
  X(void, glClipPlanex, (GLenum pname, const GLfixed * eqn), (pname, eqn)) \
  X(void, glGetFixedv, (GLenum pname, GLfixed * params), (pname, params)) \
  X(void, glGetLightxv, (GLenum light, GLenum pname, GLfixed * params), (light, pname, params)) \
  X(void, glGetMaterialxv, (GLenum face, GLenum pname, GLfixed * params), (face, pname, params)) \
  X(void, glGetTexEnvxv, (GLenum env, GLenum pname, GLfixed * params), (env, pname, params)) \
  X(void, glGetTexParameterxv, (GLenum target, GLenum pname, GLfixed * params), (target, pname, params)) \
  X(void, glLightModelx, (GLenum pname, GLfixed param), (pname, param)) \
  X(void, glLightModelxv, (GLenum pname, const GLfixed * params), (pname, params)) \
  X(void, glLightx, (GLenum light, GLenum pname, GLfixed param), (light, pname, param)) \
  X(void, glLightxv, (GLenum light, GLenum pname, const GLfixed * params), (light, pname, params)) \
  X(void, glLineWidthx, (GLfixed width), (width)) \
  X(void, glLoadMatrixx, (const GLfixed * m), (m)) \
  X(void, glMaterialx, (GLenum face, GLenum pname, GLfixed param), (face, pname, param)) \
  X(void, glMaterialxv, (GLenum face, GLenum pname, const GLfixed * params), (face, pname, params)) \
  X(void, glMultMatrixx, (const GLfixed * m), (m)) \
  X(void, glMultiTexCoord4x, (GLenum target, GLfixed s, GLfixed t, GLfixed r, GLfixed q), (target, s, t, r, q)) \
  X(void, glNormal3x, (GLfixed nx, GLfixed ny, GLfixed nz), (nx, ny, nz)) \
  X(void, glOrthox, (GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar), (left, right, bottom, top, zNear, zFar)) \
  X(void, glPointParameterx, (GLenum pname, GLfixed param), (pname, param)) \
  X(void, glPointParameterxv, (GLenum pname, const GLfixed * params), (pname, params)) \
  X(void, glPointSizex, (GLfixed size), (size)) \
  X(void, glPolygonOffsetx, (GLfixed factor, GLfixed units), (factor, units)) \
  X(void, glRotatex, (GLfixed angle, GLfixed x, GLfixed y, GLfixed z), (angle, x, y, z)) \
  X(void, glSampleCoveragex, (GLclampx value, GLboolean invert), (value, invert)) \
  X(void, glScalex, (GLfixed x, GLfixed y, GLfixed z), (x, y, z)) \
  X(void, glTexEnvx, (GLenum target, GLenum pname, GLfixed param), (target, pname, param)) \
  X(void, glTexEnvxv, (GLenum target, GLenum pname, const GLfixed * params), (target, pname, params)) \
  X(void, glTexParameterx, (GLenum target, GLenum pname, GLfixed param), (target, pname, param)) \
  X(void, glTexParameterxv, (GLenum target, GLenum pname, const GLfixed * params), (target, pname, params)) \
  X(void, glTranslatex, (GLfixed x, GLfixed y, GLfixed z), (x, y, z)) \
  X(void, glGetClipPlanex, (GLenum pname, GLfixed eqn[4]), (pname, eqn[4])) \

#define FOR_EACH_GLES2_ONLY_FUNCTION(X) \
  X(void, glBlendColor, (GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha), (red, green, blue, alpha)) \
  X(void, glStencilFuncSeparate, (GLenum face, GLenum func, GLint ref, GLuint mask), (face, func, ref, mask)) \
  X(void, glStencilMaskSeparate, (GLenum face, GLuint mask), (face, mask)) \
  X(void, glStencilOpSeparate, (GLenum face, GLenum fail, GLenum zfail, GLenum zpass), (face, fail, zfail, zpass)) \
  X(GLboolean, glIsProgram, (GLuint program), (program)) \
  X(GLboolean, glIsShader, (GLuint shader), (shader)) \
  X(void, glVertexAttrib1f, (GLuint indx, GLfloat x), (indx, x)) \
  X(void, glVertexAttrib1fv, (GLuint indx, const GLfloat* values), (indx, values)) \
  X(void, glVertexAttrib2f, (GLuint indx, GLfloat x, GLfloat y), (indx, x, y)) \
  X(void, glVertexAttrib2fv, (GLuint indx, const GLfloat* values), (indx, values)) \
  X(void, glVertexAttrib3f, (GLuint indx, GLfloat x, GLfloat y, GLfloat z), (indx, x, y, z)) \
  X(void, glVertexAttrib3fv, (GLuint indx, const GLfloat* values), (indx, values)) \
  X(void, glVertexAttrib4f, (GLuint indx, GLfloat x, GLfloat y, GLfloat z, GLfloat w), (indx, x, y, z, w)) \
  X(void, glVertexAttrib4fv, (GLuint indx, const GLfloat* values), (indx, values)) \
  X(void, glVertexAttribPointer, (GLuint indx, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid* ptr), (indx, size, type, normalized, stride, ptr)) \
  X(void, glDisableVertexAttribArray, (GLuint index), (index)) \
  X(void, glEnableVertexAttribArray, (GLuint index), (index)) \
  X(void, glGetVertexAttribfv, (GLuint index, GLenum pname, GLfloat* params), (index, pname, params)) \
  X(void, glGetVertexAttribiv, (GLuint index, GLenum pname, GLint* params), (index, pname, params)) \
  X(void, glGetVertexAttribPointerv, (GLuint index, GLenum pname, GLvoid** pointer), (index, pname, pointer)) \
  X(void, glUniform1f, (GLint location, GLfloat x), (location, x)) \
  X(void, glUniform1fv, (GLint location, GLsizei count, const GLfloat* v), (location, count, v)) \
  X(void, glUniform1i, (GLint location, GLint x), (location, x)) \
  X(void, glUniform1iv, (GLint location, GLsizei count, const GLint* v), (location, count, v)) \
  X(void, glUniform2f, (GLint location, GLfloat x, GLfloat y), (location, x, y)) \
  X(void, glUniform2fv, (GLint location, GLsizei count, const GLfloat* v), (location, count, v)) \
  X(void, glUniform2i, (GLint location, GLint x, GLint y), (location, x, y)) \
  X(void, glUniform2iv, (GLint location, GLsizei count, const GLint* v), (location, count, v)) \
  X(void, glUniform3f, (GLint location, GLfloat x, GLfloat y, GLfloat z), (location, x, y, z)) \
  X(void, glUniform3fv, (GLint location, GLsizei count, const GLfloat* v), (location, count, v)) \
  X(void, glUniform3i, (GLint location, GLint x, GLint y, GLint z), (location, x, y, z)) \
  X(void, glUniform3iv, (GLint location, GLsizei count, const GLint* v), (location, count, v)) \
  X(void, glUniform4f, (GLint location, GLfloat x, GLfloat y, GLfloat z, GLfloat w), (location, x, y, z, w)) \
  X(void, glUniform4fv, (GLint location, GLsizei count, const GLfloat* v), (location, count, v)) \
  X(void, glUniform4i, (GLint location, GLint x, GLint y, GLint z, GLint w), (location, x, y, z, w)) \
  X(void, glUniform4iv, (GLint location, GLsizei count, const GLint* v), (location, count, v)) \
  X(void, glUniformMatrix2fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value), (location, count, transpose, value)) \
  X(void, glUniformMatrix3fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value), (location, count, transpose, value)) \
  X(void, glUniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value), (location, count, transpose, value)) \
  X(void, glAttachShader, (GLuint program, GLuint shader), (program, shader)) \
  X(void, glBindAttribLocation, (GLuint program, GLuint index, const GLchar* name), (program, index, name)) \
  X(void, glCompileShader, (GLuint shader), (shader)) \
  X(GLuint, glCreateProgram, (), ()) \
  X(GLuint, glCreateShader, (GLenum type), (type)) \
  X(void, glDeleteProgram, (GLuint program), (program)) \
  X(void, glDeleteShader, (GLuint shader), (shader)) \
  X(void, glDetachShader, (GLuint program, GLuint shader), (program, shader)) \
  X(void, glLinkProgram, (GLuint program), (program)) \
  X(void, glUseProgram, (GLuint program), (program)) \
  X(void, glValidateProgram, (GLuint program), (program)) \
  X(void, glGetActiveAttrib, (GLuint program, GLuint index, GLsizei bufsize, GLsizei* length, GLint* size, GLenum* type, GLchar* name), (program, index, bufsize, length, size, type, name)) \
  X(void, glGetActiveUniform, (GLuint program, GLuint index, GLsizei bufsize, GLsizei* length, GLint* size, GLenum* type, GLchar* name), (program, index, bufsize, length, size, type, name)) \
  X(void, glGetAttachedShaders, (GLuint program, GLsizei maxcount, GLsizei* count, GLuint* shaders), (program, maxcount, count, shaders)) \
  X(int, glGetAttribLocation, (GLuint program, const GLchar* name), (program, name)) \
  X(void, glGetProgramiv, (GLuint program, GLenum pname, GLint* params), (program, pname, params)) \
  X(void, glGetProgramInfoLog, (GLuint program, GLsizei bufsize, GLsizei* length, GLchar* infolog), (program, bufsize, length, infolog)) \
  X(void, glGetShaderiv, (GLuint shader, GLenum pname, GLint* params), (shader, pname, params)) \
  X(void, glGetShaderInfoLog, (GLuint shader, GLsizei bufsize, GLsizei* length, GLchar* infolog), (shader, bufsize, length, infolog)) \
  X(void, glGetShaderSource, (GLuint shader, GLsizei bufsize, GLsizei* length, GLchar* source), (shader, bufsize, length, source)) \
  X(void, glGetUniformfv, (GLuint program, GLint location, GLfloat* params), (program, location, params)) \
  X(void, glGetUniformiv, (GLuint program, GLint location, GLint* params), (program, location, params)) \
  X(int, glGetUniformLocation, (GLuint program, const GLchar* name), (program, name)) \
  X(void, glShaderSource, (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length), (shader, count, string, length)) \
  X(void, glBindFramebuffer, (GLenum target, GLuint framebuffer), (target, framebuffer)) \
  X(void, glGenFramebuffers, (GLsizei n, GLuint* framebuffers), (n, framebuffers)) \
  X(void, glFramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level), (target, attachment, textarget, texture, level)) \
  X(GLenum, glCheckFramebufferStatus, (GLenum target), (target)) \
  X(GLboolean, glIsFramebuffer, (GLuint framebuffer), (framebuffer)) \
  X(void, glDeleteFramebuffers, (GLsizei n, const GLuint* framebuffers), (n, framebuffers)) \
  X(GLboolean, glIsRenderbuffer, (GLuint renderbuffer), (renderbuffer)) \
  X(void, glBindRenderbuffer, (GLenum target, GLuint renderbuffer), (target, renderbuffer)) \
  X(void, glDeleteRenderbuffers, (GLsizei n, const GLuint * renderbuffers), (n, renderbuffers)) \
  X(void, glGenRenderbuffers, (GLsizei n, GLuint * renderbuffers), (n, renderbuffers)) \
  X(void, glRenderbufferStorage, (GLenum target, GLenum internalformat, GLsizei width, GLsizei height), (target, internalformat, width, height)) \
  X(void, glGetRenderbufferParameteriv, (GLenum target, GLenum pname, GLint * params), (target, pname, params)) \
  X(void, glFramebufferRenderbuffer, (GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer), (target, attachment, renderbuffertarget, renderbuffer)) \
  X(void, glGetFramebufferAttachmentParameteriv, (GLenum target, GLenum attachment, GLenum pname, GLint * params), (target, attachment, pname, params)) \
  X(void, glGenerateMipmap, (GLenum target), (target)) \

#define FOR_EACH_GLES3_ONLY_FUNCTION(X) \
  X(GLconstubyteptr, glGetStringi, (GLenum name, GLint index), (name, index)) \
  X(void, glGenVertexArrays, (GLsizei n, GLuint* arrays), (n, arrays)) \
  X(void, glBindVertexArray, (GLuint array), (array)) \
  X(void, glDeleteVertexArrays, (GLsizei n, const GLuint * arrays), (n, arrays)) \
  X(GLboolean, glIsVertexArray, (GLuint array), (array)) \
  X(void *, glMapBufferRange, (GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access), (target, offset, length, access)) \
  X(GLboolean, glUnmapBuffer, (GLenum target), (target)) \
  X(void, glFlushMappedBufferRange, (GLenum target, GLintptr offset, GLsizeiptr length), (target, offset, length)) \
  X(void, glBindBufferRange, (GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size), (target, index, buffer, offset, size)) \
  X(void, glBindBufferBase, (GLenum target, GLuint index, GLuint buffer), (target, index, buffer)) \
  X(void, glCopyBufferSubData, (GLenum readtarget, GLenum writetarget, GLintptr readoffset, GLintptr writeoffset, GLsizeiptr size), (readtarget, writetarget, readoffset, writeoffset, size)) \
  X(void, glClearBufferiv, (GLenum buffer, GLint drawBuffer, const GLint * value), (buffer, drawBuffer, value)) \
  X(void, glClearBufferuiv, (GLenum buffer, GLint drawBuffer, const GLuint * value), (buffer, drawBuffer, value)) \
  X(void, glClearBufferfv, (GLenum buffer, GLint drawBuffer, const GLfloat * value), (buffer, drawBuffer, value)) \
  X(void, glClearBufferfi, (GLenum buffer, GLint drawBuffer, GLfloat depth, GLint stencil), (buffer, drawBuffer, depth, stencil)) \
  X(void, glGetBufferParameteri64v, (GLenum target, GLenum value, GLint64 * data), (target, value, data)) \
  X(void, glGetBufferPointerv, (GLenum target, GLenum pname, GLvoid ** params), (target, pname, params)) \
  X(void, glUniformBlockBinding, (GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding), (program, uniformBlockIndex, uniformBlockBinding)) \
  X(GLuint, glGetUniformBlockIndex, (GLuint program, const GLchar * uniformBlockName), (program, uniformBlockName)) \
  X(void, glGetUniformIndices, (GLuint program, GLsizei uniformCount, const GLchar ** uniformNames, GLuint * uniformIndices), (program, uniformCount, uniformNames, uniformIndices)) \
  X(void, glGetActiveUniformBlockiv, (GLuint program, GLuint uniformBlockIndex, GLenum pname, GLint * params), (program, uniformBlockIndex, pname, params)) \
  X(void, glGetActiveUniformBlockName, (GLuint program, GLuint uniformBlockIndex, GLsizei bufSize, GLsizei * length, GLchar * uniformBlockName), (program, uniformBlockIndex, bufSize, length, uniformBlockName)) \
  X(void, glUniform1ui, (GLint location, GLuint v0), (location, v0)) \
  X(void, glUniform2ui, (GLint location, GLuint v0, GLuint v1), (location, v0, v1)) \
  X(void, glUniform3ui, (GLint location, GLuint v0, GLuint v1, GLuint v2), (location, v0, v1, v2)) \
  X(void, glUniform4ui, (GLint location, GLint v0, GLuint v1, GLuint v2, GLuint v3), (location, v0, v1, v2, v3)) \
  X(void, glUniform1uiv, (GLint location, GLsizei count, const GLuint * value), (location, count, value)) \
  X(void, glUniform2uiv, (GLint location, GLsizei count, const GLuint * value), (location, count, value)) \
  X(void, glUniform3uiv, (GLint location, GLsizei count, const GLuint * value), (location, count, value)) \
  X(void, glUniform4uiv, (GLint location, GLsizei count, const GLuint * value), (location, count, value)) \
  X(void, glUniformMatrix2x3fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat * value), (location, count, transpose, value)) \
  X(void, glUniformMatrix3x2fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat * value), (location, count, transpose, value)) \
  X(void, glUniformMatrix2x4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat * value), (location, count, transpose, value)) \
  X(void, glUniformMatrix4x2fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat * value), (location, count, transpose, value)) \
  X(void, glUniformMatrix3x4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat * value), (location, count, transpose, value)) \
  X(void, glUniformMatrix4x3fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat * value), (location, count, transpose, value)) \
  X(void, glGetUniformuiv, (GLuint program, GLint location, GLuint * params), (program, location, params)) \
  X(void, glGetActiveUniformsiv, (GLuint program, GLsizei uniformCount, const GLuint * uniformIndices, GLenum pname, GLint * params), (program, uniformCount, uniformIndices, pname, params)) \
  X(void, glVertexAttribI4i, (GLuint index, GLint v0, GLint v1, GLint v2, GLint v3), (index, v0, v1, v2, v3)) \
  X(void, glVertexAttribI4ui, (GLuint index, GLuint v0, GLuint v1, GLuint v2, GLuint v3), (index, v0, v1, v2, v3)) \
  X(void, glVertexAttribI4iv, (GLuint index, const GLint * v), (index, v)) \
  X(void, glVertexAttribI4uiv, (GLuint index, const GLuint * v), (index, v)) \
  X(void, glVertexAttribIPointer, (GLuint index, GLint size, GLenum type, GLsizei stride, const GLvoid * pointer), (index, size, type, stride, pointer)) \
  X(void, glGetVertexAttribIiv, (GLuint index, GLenum pname, GLint * params), (index, pname, params)) \
  X(void, glGetVertexAttribIuiv, (GLuint index, GLenum pname, GLuint * params), (index, pname, params)) \
  X(void, glVertexAttribDivisor, (GLuint index, GLuint divisor), (index, divisor)) \
  X(void, glDrawArraysInstanced, (GLenum mode, GLint first, GLsizei count, GLsizei primcount), (mode, first, count, primcount)) \
  X(void, glDrawElementsInstanced, (GLenum mode, GLsizei count, GLenum type, const void * indices, GLsizei primcount), (mode, count, type, indices, primcount)) \
  X(void, glDrawRangeElements, (GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid * indices), (mode, start, end, count, type, indices)) \
  X(GLsync, glFenceSync, (GLenum condition, GLbitfield flags), (condition, flags)) \
  X(GLenum, glClientWaitSync, (GLsync wait_on, GLbitfield flags, GLuint64 timeout), (wait_on, flags, timeout)) \
  X(void, glWaitSync, (GLsync wait_on, GLbitfield flags, GLuint64 timeout), (wait_on, flags, timeout)) \
  X(void, glDeleteSync, (GLsync to_delete), (to_delete)) \
  X(GLboolean, glIsSync, (GLsync sync), (sync)) \
  X(void, glGetSynciv, (GLsync sync, GLenum pname, GLsizei bufSize, GLsizei * length, GLint * values), (sync, pname, bufSize, length, values)) \
  X(void, glDrawBuffers, (GLsizei n, const GLenum * bufs), (n, bufs)) \
  X(void, glReadBuffer, (GLenum src), (src)) \
  X(void, glBlitFramebuffer, (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter), (srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter)) \
  X(void, glInvalidateFramebuffer, (GLenum target, GLsizei numAttachments, const GLenum * attachments), (target, numAttachments, attachments)) \
  X(void, glInvalidateSubFramebuffer, (GLenum target, GLsizei numAttachments, const GLenum * attachments, GLint x, GLint y, GLsizei width, GLsizei height), (target, numAttachments, attachments, x, y, width, height)) \
  X(void, glFramebufferTextureLayer, (GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer), (target, attachment, texture, level, layer)) \
  X(void, glGetInternalformativ, (GLenum target, GLenum internalformat, GLenum pname, GLsizei bufSize, GLint * params), (target, internalformat, pname, bufSize, params)) \
  X(void, glTexStorage2D, (GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height), (target, levels, internalformat, width, height)) \
  X(void, glBeginTransformFeedback, (GLenum primitiveMode), (primitiveMode)) \
  X(void, glEndTransformFeedback, (), ()) \
  X(void, glGenTransformFeedbacks, (GLsizei n, GLuint * ids), (n, ids)) \
  X(void, glDeleteTransformFeedbacks, (GLsizei n, const GLuint * ids), (n, ids)) \
  X(void, glBindTransformFeedback, (GLenum target, GLuint id), (target, id)) \
  X(void, glPauseTransformFeedback, (), ()) \
  X(void, glResumeTransformFeedback, (), ()) \
  X(GLboolean, glIsTransformFeedback, (GLuint id), (id)) \
  X(void, glTransformFeedbackVaryings, (GLuint program, GLsizei count, const char ** varyings, GLenum bufferMode), (program, count, varyings, bufferMode)) \
  X(void, glGetTransformFeedbackVarying, (GLuint program, GLuint index, GLsizei bufSize, GLsizei * length, GLsizei * size, GLenum * type, char * name), (program, index, bufSize, length, size, type, name)) \
  X(void, glGenSamplers, (GLsizei n, GLuint * samplers), (n, samplers)) \
  X(void, glDeleteSamplers, (GLsizei n, const GLuint * samplers), (n, samplers)) \
  X(void, glBindSampler, (GLuint unit, GLuint sampler), (unit, sampler)) \
  X(void, glSamplerParameterf, (GLuint sampler, GLenum pname, GLfloat param), (sampler, pname, param)) \
  X(void, glSamplerParameteri, (GLuint sampler, GLenum pname, GLint param), (sampler, pname, param)) \
  X(void, glSamplerParameterfv, (GLuint sampler, GLenum pname, const GLfloat * params), (sampler, pname, params)) \
  X(void, glSamplerParameteriv, (GLuint sampler, GLenum pname, const GLint * params), (sampler, pname, params)) \
  X(void, glGetSamplerParameterfv, (GLuint sampler, GLenum pname, GLfloat * params), (sampler, pname, params)) \
  X(void, glGetSamplerParameteriv, (GLuint sampler, GLenum pname, GLint * params), (sampler, pname, params)) \
  X(GLboolean, glIsSampler, (GLuint sampler), (sampler)) \
  X(void, glGenQueries, (GLsizei n, GLuint * queries), (n, queries)) \
  X(void, glDeleteQueries, (GLsizei n, const GLuint * queries), (n, queries)) \
  X(void, glBeginQuery, (GLenum target, GLuint query), (target, query)) \
  X(void, glEndQuery, (GLenum target), (target)) \
  X(void, glGetQueryiv, (GLenum target, GLenum pname, GLint * params), (target, pname, params)) \
  X(void, glGetQueryObjectuiv, (GLuint query, GLenum pname, GLuint * params), (query, pname, params)) \
  X(GLboolean, glIsQuery, (GLuint query), (query)) \
  X(void, glProgramParameteri, (GLuint program, GLenum pname, GLint value), (program, pname, value)) \
  X(void, glProgramBinary, (GLuint program, GLenum binaryFormat, const void * binary, GLsizei length), (program, binaryFormat, binary, length)) \
  X(void, glGetProgramBinary, (GLuint program, GLsizei bufsize, GLsizei * length, GLenum * binaryFormat, void * binary), (program, bufsize, length, binaryFormat, binary)) \
  X(GLint, glGetFragDataLocation, (GLuint program, const char * name), (program, name)) \
  X(void, glGetInteger64v, (GLenum pname, GLint64 * data), (pname, data)) \
  X(void, glGetIntegeri_v, (GLenum target, GLuint index, GLint * data), (target, index, data)) \
  X(void, glGetInteger64i_v, (GLenum target, GLuint index, GLint64 * data), (target, index, data)) \
  X(void, glTexImage3D, (GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid * data), (target, level, internalFormat, width, height, depth, border, format, type, data)) \
  X(void, glTexStorage3D, (GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth), (target, levels, internalformat, width, height, depth)) \
  X(void, glTexSubImage3D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid * data), (target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, data)) \
  X(void, glCompressedTexImage3D, (GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const GLvoid * data), (target, level, internalformat, width, height, depth, border, imageSize, data)) \
  X(void, glCompressedTexSubImage3D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const GLvoid * data), (target, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, data)) \
  X(void, glCopyTexSubImage3D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height), (target, level, xoffset, yoffset, zoffset, x, y, width, height)) \
  X(void, glDebugMessageControl, (GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint * ids, GLboolean enabled), (source, type, severity, count, ids, enabled)) \
  X(void, glDebugMessageInsert, (GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar * buf), (source, type, id, severity, length, buf)) \
  X(void, glDebugMessageCallback, (GLDEBUGPROC callback, const void * userParam), (callback, userParam)) \
  X(GLuint, glGetDebugMessageLog, (GLuint count, GLsizei bufSize, GLenum * sources, GLenum * types, GLuint * ids, GLenum * severities, GLsizei * lengths, GLchar * messageLog), (count, bufSize, sources, types, ids, severities, lengths, messageLog)) \
  X(void, glPushDebugGroup, (GLenum source, GLuint id, GLsizei length, const GLchar* message), (source, id, length, message)) \
  X(void, glPopDebugGroup, (), ()) \

#define FOR_EACH_GLES_EXTENSION_FUNCTION(X) \
  X(void, glImportMemoryFdEXT, (GLuint memory, GLuint64 size, GLenum handleType, GLint fd), (memory, size, handleType, fd)) \
  X(void, glImportMemoryWin32HandleEXT, (GLuint memory, GLuint64 size, GLenum handleType, void* handle), (memory, size, handleType, handle)) \
  X(void, glDeleteMemoryObjectsEXT, (GLsizei n, const GLuint * memoryObjects), (n, memoryObjects)) \
  X(GLboolean, glIsMemoryObjectEXT, (GLuint memoryObject), (memoryObject)) \
  X(void, glCreateMemoryObjectsEXT, (GLsizei n, GLuint * memoryObjects), (n, memoryObjects)) \
  X(void, glMemoryObjectParameterivEXT, (GLuint memoryObject, GLenum pname, const GLint * params), (memoryObject, pname, params)) \
  X(void, glGetMemoryObjectParameterivEXT, (GLuint memoryObject, GLenum pname, GLint * params), (memoryObject, pname, params)) \
  X(void, glTexStorageMem2DEXT, (GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLuint memory, GLuint64 offset), (target, levels, internalFormat, width, height, memory, offset)) \
  X(void, glTexStorageMem2DMultisampleEXT, (GLenum target, GLsizei samples, GLenum internalFormat, GLsizei width, GLsizei height, GLboolean fixedSampleLocations, GLuint memory, GLuint64 offset), (target, samples, internalFormat, width, height, fixedSampleLocations, memory, offset)) \
  X(void, glTexStorageMem3DEXT, (GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLuint memory, GLuint64 offset), (target, levels, internalFormat, width, height, depth, memory, offset)) \
  X(void, glTexStorageMem3DMultisampleEXT, (GLenum target, GLsizei samples, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedSampleLocations, GLuint memory, GLuint64 offset), (target, samples, internalFormat, width, height, depth, fixedSampleLocations, memory, offset)) \
  X(void, glBufferStorageMemEXT, (GLenum target, GLsizeiptr size, GLuint memory, GLuint64 offset), (target, size, memory, offset)) \
  X(void, glTexParameteriHOST, (GLenum target, GLenum pname, GLint param), (target, pname, param)) \
  X(void, glImportSemaphoreFdEXT, (GLuint semaphore, GLenum handleType, GLint fd), (semaphore, handleType, fd)) \
  X(void, glImportSemaphoreWin32HandleEXT, (GLuint semaphore, GLenum handleType, void* handle), (semaphore, handleType, handle)) \
  X(void, glGenSemaphoresEXT, (GLsizei n, GLuint * semaphores), (n, semaphores)) \
  X(void, glDeleteSemaphoresEXT, (GLsizei n, const GLuint * semaphores), (n, semaphores)) \
  X(GLboolean, glIsSemaphoreEXT, (GLuint semaphore), (semaphore)) \
  X(void, glSemaphoreParameterui64vEXT, (GLuint semaphore, GLenum pname, const GLuint64 * params), (semaphore, pname, params)) \
  X(void, glGetSemaphoreParameterui64vEXT, (GLuint semaphore, GLenum pname, GLuint64 * params), (semaphore, pname, params)) \
  X(void, glWaitSemaphoreEXT, (GLuint semaphore, GLuint numBufferBarriers, const GLuint * buffers, GLuint numTextureBarriers, const GLuint * textures, const GLenum * srcLayouts), (semaphore, numBufferBarriers, buffers, numTextureBarriers, textures, srcLayouts)) \
  X(void, glSignalSemaphoreEXT, (GLuint semaphore, GLuint numBufferBarriers, const GLuint * buffers, GLuint numTextureBarriers, const GLuint * textures, const GLenum * dstLayouts), (semaphore, numBufferBarriers, buffers, numTextureBarriers, textures, dstLayouts)) \
  X(void, glGetUnsignedBytevEXT, (GLenum pname, GLubyte* data), (pname, data)) \
  X(void, glGetUnsignedBytei_vEXT, (GLenum target, GLuint index, GLubyte* data), (target, index, data)) \

#define FOR_EACH_GLES_FUNCTION(X) \
    FOR_EACH_GLES_COMMON_FUNCTION(X) \
    FOR_EACH_GLES_EXTENSION_FUNCTION(X) \
    FOR_EACH_GLES1_ONLY_FUNCTION(X) \
    FOR_EACH_GLES2_ONLY_FUNCTION(X) \
    FOR_EACH_GLES3_ONLY_FUNCTION(X)

// clang-format on
