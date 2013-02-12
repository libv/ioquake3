/*
 * Copyright 2011-2012 Luc Verhaegen <libv@skynet.be>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* for fbdev poking */
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/fb.h>

#include <GLES/gl.h>
#include <limare.h>

#undef ALIGN
#define USE_REAL_GL_CALLS 1

#include "../sys/sys_local.h"
#include "../qcommon/q_shared.h"
#include "egl_glimp.h"
#include "../client/client.h"
#include "../renderer/tr_local.h"

#include "glenumstring.c"

struct limare_state *state;
static int frame_count;
static int draw_count;

static void
fbdev_size(int *width, int *height)
{
#define FBDEV_DEV "/dev/fb0"
	int fd = open(FBDEV_DEV, O_RDWR);
	struct fb_var_screeninfo info;

	/* some lame defaults */
	*width = 640;
	*height = 480;

	if (fd == -1) {
		fprintf(stderr, "Error: failed to open %s: %s\n", FBDEV_DEV,
			strerror(errno));
		return;
	}

	if (ioctl(fd, FBIOGET_VSCREENINFO, &info)) {
		fprintf(stderr, "Error: failed to run ioctl on %s: %s\n",
			FBDEV_DEV, strerror(errno));
		close(fd);
		return;
	}

	close(fd);

	if (info.xres && info.yres) {
		*width = info.xres;
		*height = info.yres;
	} else
		fprintf(stderr, "Error: FB claims 0x0 dimensions\n");
}

unsigned int program_single_texture;
unsigned int program_dual_texture;
unsigned int program_current;

int
program_single_texture_load(void)
{
	static const char* vertex_source =
		"uniform mat4 uMatrix;\n"
		"\n"
		"attribute vec4 aPosition;\n"
		"attribute vec4 aColor;\n"
		"attribute vec2 aTexCoord0;\n"
		"\n"
		"varying vec4 vColor;\n"
		"varying vec4 vTexCoord0;\n"
		"\n"
		"void main()\n"
		"{\n"
		"    gl_Position = uMatrix * aPosition;\n"
		"    vColor = aColor;\n"
		"    vTexCoord0 = vec4(aTexCoord0, 1.0, 1.0);\n"
		"}\n";

	static const char* fragment_source =
		"precision mediump float;\n"
		"\n"
		"varying vec4 vColor;\n"
		"varying vec4 vTexCoord0;\n"
		"\n"
		"uniform sampler2D uTexture0;\n"
		"\n"
		"void main()\n"
		"{\n"
		"    gl_FragColor = clamp(vColor * texture2DProj(uTexture0, vTexCoord0), 0.0, 1.0);\n"
		"}\n";
	int ret;

	program_single_texture = limare_program_new(state);

	vertex_shader_attach(state, program_single_texture,
			     vertex_source);
	fragment_shader_attach(state, program_single_texture,
			       fragment_source);

	ret = limare_link(state);
	if (ret)
		return ret;

	return 0;
}

int
program_dual_texture_load(void)
{
	static const char* vertex_source =
		"uniform mat4 uMatrix;\n"
		"\n"
		"attribute vec4 aPosition;\n"
		"attribute vec4 aColor;\n"
		"attribute vec2 aTexCoord0;\n"
		"attribute vec2 aTexCoord1;\n"
		"\n"
		"varying vec4 vColor;\n"
		"varying vec4 vTexCoord0;\n"
		"varying vec4 vTexCoord1;\n"
		"\n"
		"void main()\n"
		"{\n"
		"    gl_Position = uMatrix * aPosition;\n"
		"    vColor = aColor;\n"
		"    vTexCoord0 = vec4(aTexCoord0, 1.0, 1.0);\n"
		"    vTexCoord1 = vec4(aTexCoord1, 1.0, 1.0);\n"
		"}\n";

	static const char* fragment_source =
		"precision mediump float;\n"
		"\n"
		"varying vec4 vColor;\n"
		"varying vec4 vTexCoord0;\n"
		"varying vec4 vTexCoord1;\n"
		"\n"
		"uniform sampler2D uTexture0;\n"
		"uniform sampler2D uTexture1;\n"
		"\n"
		"void main()\n"
		"{\n"
		"    gl_FragColor = clamp(vColor * "
		"texture2DProj(uTexture0, vTexCoord0) *"
		"texture2DProj(uTexture1, vTexCoord1),"
		" 0.0, 1.0);\n"
		"}\n";
	int ret;

	program_dual_texture = limare_program_new(state);

	vertex_shader_attach(state, program_dual_texture,
			     vertex_source);
	fragment_shader_attach(state, program_dual_texture,
			       fragment_source);

	ret = limare_link(state);
	if (ret)
		return ret;

	return 0;
}

/*
 *
 * First, some matrix work.
 *
 */
float matrix_modelview[16];
float matrix_projection[16];
float matrix_transform[16];
int matrix_dirty;
int matrix_mode = GL_MODELVIEW;

void
matrix_identity_set(float *matrix)
{
	matrix[0] = 1.0;
	matrix[1] = 0.0;
	matrix[2] = 0.0;
	matrix[3] = 0.0;

	matrix[4] = 0.0;
	matrix[5] = 1.0;
	matrix[6] = 0.0;
	matrix[7] = 0.0;

	matrix[8] = 0.0;
	matrix[9] = 0.0;
	matrix[10] = 1.0;
	matrix[11] = 0.0;

	matrix[12] = 0.0;
	matrix[13] = 0.0;
	matrix[14] = 0.0;
	matrix[15] = 1.0;
}

void
matrix_upload(void)
{
	int i;

	for (i = 0; i < 16; i += 4) {
		matrix_transform[i + 0] =
			(matrix_modelview[i + 0] * matrix_projection[0]) +
			(matrix_modelview[i + 1] * matrix_projection[4]) +
			(matrix_modelview[i + 2] * matrix_projection[8]) +
			(matrix_modelview[i + 3] * matrix_projection[12]);

		matrix_transform[i + 1] =
			(matrix_modelview[i + 0] * matrix_projection[1]) +
			(matrix_modelview[i + 1] * matrix_projection[5]) +
			(matrix_modelview[i + 2] * matrix_projection[9]) +
			(matrix_modelview[i + 3] * matrix_projection[13]);

		matrix_transform[i + 2] =
			(matrix_modelview[i + 0] * matrix_projection[2]) +
			(matrix_modelview[i + 1] * matrix_projection[6]) +
			(matrix_modelview[i + 2] * matrix_projection[10]) +
			(matrix_modelview[i + 3] * matrix_projection[14]);

		matrix_transform[i + 3] =
			(matrix_modelview[i + 0] * matrix_projection[3]) +
			(matrix_modelview[i + 1] * matrix_projection[7]) +
			(matrix_modelview[i + 2] * matrix_projection[11]) +
			(matrix_modelview[i + 3] * matrix_projection[15]);
	}

	limare_program_current(state, program_single_texture);
	limare_uniform_attach(state, "uMatrix", 16, matrix_transform);

	limare_program_current(state, program_dual_texture);
	limare_uniform_attach(state, "uMatrix", 16, matrix_transform);

	if (program_current != program_dual_texture)
		limare_program_current(state, program_current);

	matrix_dirty = 0;
}

void
matrix_init(void)
{
	matrix_identity_set(matrix_modelview);
	matrix_identity_set(matrix_projection);

	matrix_dirty = 1;
}

void
qglOrthof(float left, float right, float bottom, float top,
	  float nearZ, float farZ)
{
	float width = right - left;
	float height = top - bottom;
	float depth = farZ - nearZ;

	if (!width || !height || !depth) {
		fprintf(stderr, "%s called with wrong values\n", __func__);
		return;
	}

	matrix_projection[0] = 2.0 / width;
	matrix_projection[1] = 0.0;
	matrix_projection[2] = 0.0;
	matrix_projection[3] = 0.0;

	matrix_projection[4] = 0.0;
	matrix_projection[5] = 2.0 / height;
	matrix_projection[6] = 0.0;
	matrix_projection[7] = 0.0;

	matrix_projection[8] = 0.0;
	matrix_projection[9] = 0.0;
	matrix_projection[10] = -2.0 / depth;
	matrix_projection[11] = 0.0;

	matrix_projection[12] = -(right + left) / width;
	matrix_projection[13] = -(top + bottom) / height;
	matrix_projection[14] = -(nearZ + farZ) / depth;
	matrix_projection[15] = 1.0;

	matrix_dirty = 1;
}

void
qglLoadMatrixf(const GLfloat *m)
{
	if (matrix_mode == GL_PROJECTION)
		memcpy(matrix_projection, m, 16 * sizeof(float));
	else
		memcpy(matrix_modelview, m, 16 * sizeof(float));

	matrix_dirty = 1;
}

void
qglLoadIdentity(void)
{
	if (matrix_mode == GL_PROJECTION)
		matrix_identity_set(matrix_projection);
	else
		matrix_identity_set(matrix_modelview);

	matrix_dirty = 1;
}

void
qglMatrixMode(GLenum mode)
{
	matrix_mode = mode;
}

void
GLimp_Init(void)
{
	int fb_width, fb_height;
	int ret;

	ri.Printf(PRINT_ALL, "Initializing Limare backend.\n");

	state = limare_init();
	if (!state)
		return;

	bzero(&glConfig, sizeof(glConfig));

	limare_buffer_clear(state);

	fbdev_size(&fb_width, &fb_height);

	ri.Printf(PRINT_ALL, "FB dimensions %dx%d\n", fb_width, fb_height);

	ret = limare_state_setup(state, fb_width, fb_height, 0x00000000);
	if (ret)
		return;

	glConfig.isFullscreen = qtrue;
	glConfig.vidWidth = fb_width;
	glConfig.vidHeight = fb_height;

	glConfig.windowAspect = (float)glConfig.vidWidth / glConfig.vidHeight;
	// FIXME
	//glConfig.colorBits = 0
	//glConfig.stencilBits = 0;
	//glConfig.depthBits = 0;
	glConfig.textureCompression = TC_NONE;

	// This values force the UI to disable driver selection
	glConfig.driverType = GLDRV_ICD;
	glConfig.hardwareType = GLHW_GENERIC;

	ret = program_single_texture_load();
	if (ret)
		return;

	ret = program_dual_texture_load();
	if (ret)
		return;

	limare_program_current(state, program_single_texture);
	program_current = program_single_texture;

	matrix_init();

	limare_frame_new(state);

	qglLockArraysEXT = qglLockArrays;
	qglUnlockArraysEXT = qglUnlockArrays;

	//GLimp_InitExtensions();

	IN_Init();

	ri.Printf(PRINT_ALL, "------------------\n");
}

void GLimp_LogComment(char *comment)
{
	//fprintf(stderr, "%s: %s\n", __func__, comment);
}



void GLimp_EndFrame(void)
{
	int ret = limare_frame_flush(state);
	if (ret)
		return;

	limare_buffer_swap(state);

	//printf("Finished frame %d\n", frame_count);

	draw_count = 0;
	frame_count++;

	limare_frame_new(state);
}

void GLimp_Shutdown(void)
{
	IN_Shutdown();

	limare_finish(state);
}

void qglCallList(GLuint list)
{
}

void GLimp_SetGamma(unsigned char red[256], unsigned char green[256],
		    unsigned char blue[256])
{
}

qboolean GLimp_SpawnRenderThread(void (*function) (void))
{
	return qfalse;
}

void GLimp_FrontEndSleep(void)
{
}

void *GLimp_RendererSleep(void)
{
	return NULL;
}

void GLimp_RenderThreadWrapper(void *data)
{
}

void GLimp_WakeRenderer(void *data)
{
}

void
qglDrawBuffer(GLenum mode)
{
}

void
qglNumVertices(GLint count)
{
}

void
qglLockArrays(GLint j, GLsizei size)
{
}

void
qglUnlockArrays(void)
{
}

/*
 *
 * Actual GL calls.
 *
 */

/*
 *
 * Simple things first.
 *
 */
void
qglColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
	limare_color_mask(state, red, green, blue, alpha);
}

void
qglClearDepthf(GLclampf depth)
{
	limare_depth_clear_depth(state, depth);
}

void
qglAlphaFunc(GLenum func, GLclampf ref)
{
	limare_alpha_func(state, func, ref);
}

void
qglPolygonOffset(GLfloat factor, GLfloat units)
{
	limare_polygon_offset(state, factor, units);
}

void
qglCullFace(GLenum mode)
{
	limare_cullface(state, mode);
}

void
qglScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
	limare_scissor(state, x, y, width, height);
}

void
qglViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
	limare_viewport(state, x, y, width, height);
}

void
qglDepthRangef(GLclampf zNear, GLclampf zFar)
{
	limare_depth(state, zNear, zFar);
}

void
qglBlendFunc(GLenum sfactor, GLenum dfactor)
{
	limare_blend_func(state, sfactor, dfactor);
}

void
qglDepthMask(GLboolean flag)
{
	limare_depth_mask(state, flag);
}

void
qglDepthFunc(GLenum func)
{
	limare_depth_func(state, func);
}

void
qglClear(GLbitfield mask)
{
	if (mask == GL_DEPTH_BUFFER_BIT)
		limare_depth_buffer_clear(state);
}

/*
 *
 * Now we add in some texture stuff.
 *
 */
/*
 * For tracking textures.
 */
#define TEXTURE_LEVEL_COUNT 12

static int texture_id;

#define TEXTURE_HANDLE_COUNT 512
static int texture_handles[TEXTURE_HANDLE_COUNT];

static int texture_dump_start;
static int texture_filter_min;
static int texture_filter_mag;
static int texture_wrap_s;
static int texture_wrap_t;

/*
 * For tracking draws.
 */
static int color_active;
static int color_size;
static int color_pitch;
static const unsigned int *color_ptr;

static int texture_current;
static int texture_coord_current;

static int coord0_active;
static int coord0_size;
static int coord0_pitch;
static const unsigned int *coord0_ptr;

static int texture0_active;
static int texture0_id;

static int coord1_active;
static int coord1_size;
static int coord1_pitch;
static const unsigned int *coord1_ptr;

static int texture1_active;
static int texture1_id;

static int vertex_size;
static int vertex_pitch;
static const unsigned int *vertex_ptr;

/*
 *
 * Handle textures.
 *
 */
void
texture_handle_store(int handle)
{
	if ((texture_id < 1024) ||
	    (texture_id >= (1024 + TEXTURE_HANDLE_COUNT))) {
		fprintf(stderr, "%s: invalid active texture specified: %d\n",
			__func__, texture_id);
		return;
	}

	texture_handles[texture_id - 1024] = handle;
}

int
texture_handle_get(int id)
{
	if ((id < 1024) ||
	    (id >= (1024 + TEXTURE_HANDLE_COUNT))) {
		fprintf(stderr, "%s: invalid active texture specified: %d\n",
			__func__, id);
		return 0;
	}

	return texture_handles[id - 1024];
}

void
qglEnable(GLenum cap)
{
	if (cap == GL_TEXTURE_2D) {
		if (texture_current)
			texture1_active = 1;
		else
			texture0_active = 1;
	} else
		limare_enable(state, cap);
}

void
qglDisable(GLenum cap)
{
	if (cap == GL_TEXTURE_2D) {
		if (texture_current)
			texture1_active = 0;
		else
			texture0_active = 0;
	} else
		limare_disable(state, cap);
}

void
qglDeleteTextures(GLsizei n, const GLuint *textures)
{
	texture_dump_start = 1;
}

void
qglBindTexture(GLenum target, GLuint id)
{
	if (texture_dump_start && texture_filter_min &&
	    texture_filter_mag && texture_wrap_s && texture_wrap_t) {
		int handle = texture_handle_get(texture_id);

		limare_texture_parameters(state, handle,
					  texture_filter_min,
					  texture_filter_mag,
					  texture_wrap_s,
					  texture_wrap_t);
		texture_filter_min = 0;
		texture_filter_mag = 0;
		texture_wrap_s = 0;
		texture_wrap_t = 0;
	}

	texture_id = id;

	if (texture_current)
		texture1_id = id;
	else
		texture0_id = id;
}

void
qglTexImage2D(GLenum target, GLint level, GLint internalformat,
	      GLsizei width, GLsizei height, GLint border,
	      GLenum format, GLenum type, const GLvoid *pixels)
{
	int handle;

	if (!texture_dump_start)
		return;

	if (level >= TEXTURE_LEVEL_COUNT) {
		fprintf(stderr, "%s: Too many levels assigned to texture %d\n",
			__func__, texture_id);
		return;
	}

#define LIMA_TEXEL_FORMAT_RGBA_8888		0x16

	if (!level) {
		handle = limare_texture_upload(state, pixels, width, height,
					       LIMA_TEXEL_FORMAT_RGBA_8888, 0);

		texture_handle_store(handle);
	} else {
		handle = texture_handle_get(texture_id);

		limare_texture_mipmap_upload(state, handle, level, pixels);
	}
}

void
qglTexParameteri(GLenum target, GLenum pname, GLint param)
{
	if (!texture_dump_start)
		return;

	switch (pname) {
	case GL_TEXTURE_MIN_FILTER:
		texture_filter_min = param;
		return;
	case GL_TEXTURE_MAG_FILTER:
		texture_filter_mag = param;
		return;
	case GL_TEXTURE_WRAP_S:
		texture_wrap_s = param;
		return;
	case GL_TEXTURE_WRAP_T:
		texture_wrap_t = param;
		return;
	default:
		fprintf(stderr, "%s: unknown pname 0x%04X\n", __func__, pname);
		return;
	}
}

void
qglActiveTexture(GLenum texture)
{
	if (texture > GL_TEXTURE1) {
		printf("Error: %s(%s) not supported\n",
		       __func__, GLEnumString(texture));
		return;
	}
	texture_current = texture - GL_TEXTURE0;
}

void
qglClientActiveTexture(GLenum texture)
{
	if (texture > GL_TEXTURE1) {
		printf("Error: %s(%s) not supported\n",
		       __func__, GLEnumString(texture));
		return;
	}
	texture_coord_current = texture - GL_TEXTURE0;
}

void
qglDisableClientState(GLenum array)
{
	if (array == GL_TEXTURE_COORD_ARRAY) {
		if (texture_coord_current) {
			coord1_active = 0;
			coord1_size = 0;
			coord1_pitch = 0;
			coord1_ptr = NULL;
		} else {
			coord0_active = 0;
			coord0_size = 0;
			coord0_pitch = 0;
			coord0_ptr = NULL;
		}
	} else if (array == GL_COLOR_ARRAY) {
		color_active = 0;
		color_size = 0;
		color_pitch = 0;
		color_ptr = NULL;
	} else if (array != GL_VERTEX_ARRAY)
		fprintf(stderr, "%s: Error: unknown array: 0x%04X\n",
		       __func__, array);
}

void
qglEnableClientState(GLenum array)
{
	if (array == GL_TEXTURE_COORD_ARRAY) {
		if (texture_coord_current)
			coord1_active = 1;
		else
			coord0_active = 1;
	} else if (array == GL_COLOR_ARRAY)
		color_active = 1;
	else if (array != GL_VERTEX_ARRAY)
		fprintf(stderr, "%s: Error: unknown array: 0x%04X\n",
			__func__, array);
}

void
qglTexCoordPointer(GLint size, GLenum type, GLsizei stride,
		   const GLvoid *pointer)
{
	if (type != GL_FLOAT) {
		fprintf(stderr, "%s: Error: unsupported type.\n", __func__);
		return;
	}

	if (texture_coord_current) {
		coord1_size = size;
		coord1_pitch = stride;
		coord1_ptr = pointer;
	} else {
		coord0_size = size;
		coord0_pitch = stride;
		coord0_ptr = pointer;
	}
}

void
qglColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	if (type != GL_UNSIGNED_BYTE) {
		fprintf(stderr, "%s: Error: unsupported type.\n", __func__);
		return;
	}

	if (color_active) {
		color_size = size;
		color_pitch = stride;
		color_ptr = pointer;
	} else
		fprintf(stderr, "%s: Error: color is not active.\n",
		       __func__);
}

void
qglVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	if (type != GL_FLOAT) {
		fprintf(stderr, "%s: Error: unsupported type.\n", __func__);
		return;
	}

	vertex_size = size;
	vertex_pitch = stride;
	vertex_ptr = pointer;
}

void
qglDrawElements(GLenum mode, GLsizei indices_count, GLenum type,
		const GLvoid *ptr)
{
	const unsigned short *indices = ptr;
	int count = 0, handle;
	int i;

	if (type != GL_UNSIGNED_SHORT) {
		fprintf(stderr, "%s: Error: unsupported type.\n", __func__);
		return;
	}

	for (i = 0; i < indices_count; i++)
		if (indices[i] > count)
			count = indices[i];
	count++;

	if (matrix_dirty)
		matrix_upload();

	/* Switch programs. */
	if (!color_active) {
		printf("%s: Error: draw %d has no colors\n",
		       __func__, draw_count);
		return;
	} else if (!coord0_active || !texture0_active) {
		printf("%s: Error: draw %d has no textures\n",
		       __func__, draw_count);
		return;
	} else if (!coord1_active || !texture1_active) {
		if (program_current != program_single_texture) {
			limare_program_current(state, program_single_texture);
			program_current = program_single_texture;
		}
	} else {
		if (program_current != program_dual_texture) {
			limare_program_current(state, program_dual_texture);
			program_current = program_dual_texture;
		}
	}

	limare_attribute_pointer(state, "aPosition", LIMARE_ATTRIB_FLOAT,
				 vertex_size, vertex_pitch, count,
				 (void *) vertex_ptr);

	if (color_active)
		limare_attribute_pointer(state, "aColor", LIMARE_ATTRIB_U8N,
					 color_size, color_pitch, count,
					 (void *) color_ptr);

	if (coord0_active)
		limare_attribute_pointer(state, "aTexCoord0",
					 LIMARE_ATTRIB_FLOAT,
					 coord0_size, coord0_pitch, count,
					 (void *) coord0_ptr);

	if (texture0_active) {
		handle = texture_handle_get(texture0_id);

		limare_texture_attach(state, "uTexture0", handle);
	}

	if (coord1_active)
		limare_attribute_pointer(state, "aTexCoord1",
					 LIMARE_ATTRIB_FLOAT,
					 coord1_size, coord1_pitch, count,
					 (void *) coord1_ptr);

	if (texture1_active) {
		handle = texture_handle_get(texture1_id);

		limare_texture_attach(state, "uTexture1", handle);
	}

	limare_draw_elements(state, mode, indices_count, (void *) indices,
			     type);

	draw_count++;
}

/*
 *
 * Not implemented.
 *
 */

void
qglPopMatrix(void)
{
	fprintf(stderr, "Error: %s() called!\n", __func__);
}

void
qglPushMatrix(void)
{
	fprintf(stderr, "Error: %s() called!\n", __func__);
}

#define log_main(msg, ...)
#define log_limare(msg, ...)
#define log_all(msg, ...)

void
qglTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
		 GLsizei width, GLsizei height, GLenum format, GLenum type,
		 const GLvoid *pixels)
{
	fprintf(stderr, "Error: %s() called!\n", __func__);
}

void
qglClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
	fprintf(stderr, "Error: %s() called!\n", __func__);
}

void
qglClipPlanef(GLenum plane, const GLfloat *equation)
{
	log_all("\tglClipPlanef(%s, %p);\n", GLEnumString(plane), equation);
}

void
qglColor4f (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
	fprintf(stderr, "Error: %s() called!\n", __func__);
}

void
qglLineWidth(GLfloat width)
{
	fprintf(stderr, "Error: %s() called!\n", __func__);
}

void
qglMaterialf(GLenum face, GLenum pname, GLfloat param)
{
	fprintf(stderr, "Error: %s() called!\n", __func__);
}

void
qglMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
	fprintf(stderr, "Error: %s() called!\n", __func__);
}

void
qglTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
	fprintf(stderr, "Error: %s() called!\n", __func__);
}

void
qglTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
	fprintf(stderr, "Error: %s() called!\n", __func__);
}

void
qglAlphaFuncx(GLenum func, GLclampx ref)
{
	fprintf(stderr, "Error: %s() called!\n", __func__);
}

void
qglClearStencil(GLint s)
{
	log_all("\tglClearStencil(%d);\n", s);
}

void
qglDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	fprintf(stderr, "Error: %s() called!\n", __func__);
}

void
qglFinish(void)
{
}

void
qglFlush(void)
{
	fprintf(stderr, "Error: %s() called!\n", __func__);
}

void
qglGetBooleanv(GLenum pname, GLboolean *params)
{
	fprintf(stderr, "Error: %s() called!\n", __func__);
}

GLenum
qglGetError(void)
{
	return 0;
}

void
qglGetIntegerv(GLenum pname, GLint *params)
{
	fprintf(stderr, "Error: %s() called!\n", __func__);
}

void
qglReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format,
	      GLenum type, GLvoid *pixels)
{
	fprintf(stderr, "Error: %s() called!\n", __func__);
}

void
qglShadeModel(GLenum mode)
{
	fprintf(stderr, "Error: %s() called!\n", __func__);
}

void
qglStencilFunc(GLenum func, GLint ref, GLuint mask)
{
	fprintf(stderr, "Error: %s() called!\n", __func__);
}

void
qglStencilMask(GLuint mask)
{
	fprintf(stderr, "Error: %s() called!\n", __func__);
}

void
qglStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
	fprintf(stderr, "Error: %s() called!\n", __func__);
}

void
qglTexEnvi(GLenum target, GLenum pname, GLint param)
{
	fprintf(stderr, "Error: %s() called!\n", __func__);
}
