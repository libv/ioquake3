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

#include <GLES2/gl2.h>

#define __gl_h_ 1
typedef khronos_int32_t  GLclampx;
#define GL_MODELVIEW                      0x1700
#define GL_PROJECTION                     0x1701
#define GL_VERTEX_ARRAY                   0x8074
#define GL_NORMAL_ARRAY                   0x8075
#define GL_COLOR_ARRAY                    0x8076
#define GL_TEXTURE_COORD_ARRAY            0x8078
#define GL_ALPHA_TEST                     0x0BC0

#undef ALIGN
#define USE_REAL_GL_CALLS 1

#include "../sys/sys_local.h"
#include "../qcommon/q_shared.h"
#include "egl_glimp.h"
#include "../client/client.h"
#include "../renderer/tr_local.h"

#include "glenumstring.c"

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

#define MAX_NUM_CONFIGS 4

static char *GLimp_StringErrors[] = {
	"EGL_SUCCESS",
	"EGL_NOT_INITIALIZED",
	"EGL_BAD_ACCESS",
	"EGL_BAD_ALLOC",
	"EGL_BAD_ATTRIBUTE",
	"EGL_BAD_CONFIG",
	"EGL_BAD_CONTEXT",
	"EGL_BAD_CURRENT_SURFACE",
	"EGL_BAD_DISPLAY",
	"EGL_BAD_MATCH",
	"EGL_BAD_NATIVE_PIXMAP",
	"EGL_BAD_NATIVE_WINDOW",
	"EGL_BAD_PARAMETER",
	"EGL_BAD_SURFACE",
	"EGL_CONTEXT_LOST",
};

static void
GLimp_HandleError(void)
{
	GLint err = eglGetError();

	fprintf(stderr, "%s: 0x%04x: %s\n", __func__, err,
		GLimp_StringErrors[err]); // Cannot work! -- libv
	assert(0);
}

char *
eglStrError(EGLint error)
{
	printf("Error: %d\n", error);

	switch (error) {
	case EGL_SUCCESS:
		return "EGL_SUCCESS";
	case EGL_BAD_ALLOC:
		return "EGL_BAD_ALLOC";
	case EGL_BAD_CONFIG:
		return "EGL_BAD_CONFIG";
	case EGL_BAD_PARAMETER:
		return "EGL_BAD_PARAMETER";
	case EGL_BAD_MATCH:
		return "EGL_BAD_MATCH";
	case EGL_BAD_ATTRIBUTE:
		return "EGL_BAD_ATTRIBUTE";
	default:
		return "UNKNOWN";
	}
}

EGLContext eglContext = NULL;
EGLDisplay eglDisplay = NULL;
EGLSurface eglSurface = NULL;

/*
 * Create an RGB window.
 * Return the window and context handles.
 */
static void
make_window(int width, int height)
{
	EGLConfig configs[MAX_NUM_CONFIGS];
	EGLint config_count;
	static struct mali_native_window window;
	EGLint cfg_attribs[] = {
		EGL_NATIVE_VISUAL_TYPE, 0,

		/* RGB565 */
		EGL_BUFFER_SIZE, 16,
		EGL_RED_SIZE, 5,
		EGL_GREEN_SIZE, 6,
		EGL_BLUE_SIZE, 5,

		EGL_DEPTH_SIZE, 8,

		EGL_SAMPLES, 4,

		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,

		EGL_NONE
	};
	static const EGLint context_attribute_list[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	EGLint i;

	if (!eglGetConfigs(eglDisplay, configs, MAX_NUM_CONFIGS, &config_count))
		GLimp_HandleError();

	if (!eglChooseConfig
	    (eglDisplay, cfg_attribs, configs, MAX_NUM_CONFIGS, &config_count))
		GLimp_HandleError();

	window.width = width;
	window.height = height;

	for (i = 0; i < config_count; i++) {
		if ((eglSurface =
		     eglCreateWindowSurface(eglDisplay, configs[i],
					    (NativeWindowType) &window,
					    NULL)) != EGL_NO_SURFACE)
			break;
	}
	if (eglSurface == EGL_NO_SURFACE)
		GLimp_HandleError();

	if ((eglContext =
	     eglCreateContext(eglDisplay, configs[i], EGL_NO_CONTEXT,
			      context_attribute_list)) == EGL_NO_CONTEXT)
		GLimp_HandleError();

	if (!eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext))
		GLimp_HandleError();
}

unsigned int program_single_texture;
unsigned int program_dual_texture;
unsigned int program_current;
int program_single_texture_matrix;
int program_dual_texture_matrix;

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
		"varying vec2 vTexCoord0;\n"
		"\n"
		"void main()\n"
		"{\n"
		"    gl_Position = uMatrix * aPosition;\n"
		"    vColor = aColor;\n"
		"    vTexCoord0 = aTexCoord0;\n"
		"}\n";

	static const char* fragment_source =
		"precision mediump float;\n"
		"\n"
		"varying vec4 vColor;\n"
		"varying vec2 vTexCoord0;\n"
		"\n"
		"uniform sampler2D uTexture0;\n"
		"\n"
		"void main()\n"
		"{\n"
		"    gl_FragColor = clamp(vColor * texture2D(uTexture0, vTexCoord0), 0.0, 1.0);\n"
		"}\n";
	int ret;

	int vertex_shader = glCreateShader(GL_VERTEX_SHADER);
	if (!vertex_shader) {
		printf("Error: glCreateShader(GL_VERTEX_SHADER) failed: %d (%s)\n",
		       eglGetError(), eglStrError(eglGetError()));
		return -1;
	}


	glShaderSource(vertex_shader, 1, &vertex_source, NULL);
	glCompileShader(vertex_shader);

	glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &ret);
	if (!ret) {
		char *log;

		printf("Error: vertex shader compilation failed!:\n");
		glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &ret);

		if (ret > 1) {
			log = malloc(ret);
			glGetShaderInfoLog(vertex_shader, ret, NULL, log);
			printf("%s", log);
		}
		return -1;
	} else
		printf("Vertex shader compilation succeeded!\n");

	int fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
	if (!fragment_shader) {
		printf("Error: glCreateShader(GL_FRAGMENT_SHADER) failed: %d (%s)\n",
		       eglGetError(), eglStrError(eglGetError()));
		return -1;
	}


	glShaderSource(fragment_shader, 1, &fragment_source, NULL);
	glCompileShader(fragment_shader);

	glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &ret);
	if (!ret) {
		char *log;

		printf("Error: fragment shader compilation failed!:\n");
		glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &ret);

		if (ret > 1) {
			log = malloc(ret);
			glGetShaderInfoLog(fragment_shader, ret, NULL, log);
			printf("%s", log);
		}
		return -1;
	} else
		printf("Fragment shader compilation succeeded!\n");

	int program = glCreateProgram();
	if (!program) {
		printf("Error: failed to create program!\n");
		return -1;
	}

	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);

	glBindAttribLocation(program, 0, "aPosition");
	glBindAttribLocation(program, 1, "aColor");
	glBindAttribLocation(program, 2, "aTexCoord0");

	glLinkProgram(program);

	glGetProgramiv(program, GL_LINK_STATUS, &ret);
	if (!ret) {
		char *log;

		printf("Error: program linking failed!:\n");
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &ret);

		if (ret > 1) {
			log = malloc(ret);
			glGetProgramInfoLog(program, ret, NULL, log);
			printf("%s", log);
		}
		return -1;
	} else
		printf("program linking succeeded!\n");

	glUseProgram(program);

	int handle = glGetUniformLocation(program, "uMatrix");

	GLint texture_loc = glGetUniformLocation(program, "uTexture0");
	glUniform1i(texture_loc, 0); // 0 -> GL_TEXTURE0 in glActiveTexture

	program_single_texture = program;
	program_single_texture_matrix = handle;

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
		"varying vec2 vTexCoord0;\n"
		"varying vec2 vTexCoord1;\n"
		"\n"
		"void main()\n"
		"{\n"
		"    gl_Position = uMatrix * aPosition;\n"
		"    vColor = aColor;\n"
		"    vTexCoord0 = aTexCoord0;\n"
		"    vTexCoord1 = aTexCoord1;\n"
		"}\n";

	static const char* fragment_source =
		"precision mediump float;\n"
		"\n"
		"varying vec4 vColor;\n"
		"varying vec2 vTexCoord0;\n"
		"varying vec2 vTexCoord1;\n"
		"\n"
		"uniform sampler2D uTexture0;\n"
		"uniform sampler2D uTexture1;\n"
		"\n"
		"void main()\n"
		"{\n"
		"    gl_FragColor = clamp(vColor * "
		"texture2D(uTexture0, vTexCoord0) *"
		"texture2D(uTexture1, vTexCoord1),"
		" 0.0, 1.0);\n"
		"}\n";
	int ret;

	int vertex_shader = glCreateShader(GL_VERTEX_SHADER);
	if (!vertex_shader) {
		printf("Error: glCreateShader(GL_VERTEX_SHADER) failed: %d (%s)\n",
		       eglGetError(), eglStrError(eglGetError()));
		return -1;
	}


	glShaderSource(vertex_shader, 1, &vertex_source, NULL);
	glCompileShader(vertex_shader);

	glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &ret);
	if (!ret) {
		char *log;

		printf("Error: vertex shader compilation failed!:\n");
		glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &ret);

		if (ret > 1) {
			log = malloc(ret);
			glGetShaderInfoLog(vertex_shader, ret, NULL, log);
			printf("%s", log);
		}
		return -1;
	} else
		printf("Vertex shader compilation succeeded!\n");

	int fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
	if (!fragment_shader) {
		printf("Error: glCreateShader(GL_FRAGMENT_SHADER) failed: %d (%s)\n",
		       eglGetError(), eglStrError(eglGetError()));
		return -1;
	}


	glShaderSource(fragment_shader, 1, &fragment_source, NULL);
	glCompileShader(fragment_shader);

	glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &ret);
	if (!ret) {
		char *log;

		printf("Error: fragment shader compilation failed!:\n");
		glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &ret);

		if (ret > 1) {
			log = malloc(ret);
			glGetShaderInfoLog(fragment_shader, ret, NULL, log);
			printf("%s", log);
		}
		return -1;
	} else
		printf("Fragment shader compilation succeeded!\n");

	int program = glCreateProgram();
	if (!program) {
		printf("Error: failed to create program!\n");
		return -1;
	}

	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);

	glBindAttribLocation(program, 0, "aPosition");
	glBindAttribLocation(program, 1, "aColor");
	glBindAttribLocation(program, 2, "aTexCoord0");
	glBindAttribLocation(program, 3, "aTexCoord1");

	glLinkProgram(program);

	glGetProgramiv(program, GL_LINK_STATUS, &ret);
	if (!ret) {
		char *log;

		printf("Error: program linking failed!:\n");
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &ret);

		if (ret > 1) {
			log = malloc(ret);
			glGetProgramInfoLog(program, ret, NULL, log);
			printf("%s", log);
		}
		return -1;
	} else
		printf("program linking succeeded!\n");

	glUseProgram(program);

	int handle = glGetUniformLocation(program, "uMatrix");

	GLint texture_loc = glGetUniformLocation(program, "uTexture0");
	glUniform1i(texture_loc, 0); // 0 -> GL_TEXTURE0 in glActiveTexture
	texture_loc = glGetUniformLocation(program, "uTexture1");
	glUniform1i(texture_loc, 1); // 1 -> GL_TEXTURE1 in glActiveTexture

	program_dual_texture = program;
	program_dual_texture_matrix = handle;

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

	glUseProgram(program_single_texture);
	glUniformMatrix4fv(program_single_texture_matrix, 1, GL_FALSE,
			   matrix_transform);

	glUseProgram(program_dual_texture);
	glUniformMatrix4fv(program_dual_texture_matrix, 1, GL_FALSE,
			   matrix_transform);

	if (program_current != program_dual_texture)
		glUseProgram(program_current);

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

static void qglMultiTexCoord2f(GLenum target, GLfloat s, GLfloat t)
{
	qglMultiTexCoord4f(target,s,t,1,1);
}

void
GLimp_Init(void)
{
	EGLint major, minor;
	int fb_width, fb_height;
	int ret;

	ri.Printf(PRINT_ALL, "Initializing GLESv2 backend.\n");

	bzero(&glConfig, sizeof(glConfig));

	fbdev_size(&fb_width, &fb_height);

	ri.Printf(PRINT_ALL, "FB dimensions %dx%d\n", fb_width, fb_height);

	eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (!eglInitialize(eglDisplay, &major, &minor))
		GLimp_HandleError();

	make_window(fb_width, fb_height);

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

	glUseProgram(program_single_texture);
	program_current = program_single_texture;

	qglMultiTexCoord2fARB = qglMultiTexCoord2f;
	qglActiveTextureARB = qglActiveTexture;
	qglClientActiveTextureARB = qglClientActiveTexture;
	glConfig.numTextureUnits = 2;

	matrix_init();

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
	eglSwapBuffers(eglDisplay, eglSurface);

	//if (!(frame_count % 0x07))
	//	printf("Finished frame %d\n", frame_count);

	draw_count = 0;
	frame_count++;
}

void GLimp_Shutdown(void)
{
	IN_Shutdown();

	glFinish();
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
	glColorMask(red, green, blue, alpha);
}

void
qglClearDepthf(GLclampf depth)
{
	glClearDepthf(depth);
}

void
qglAlphaFunc(GLenum func, GLclampf ref)
{
	//glAlphaFunc(func, ref);
}

void
qglPolygonOffset(GLfloat factor, GLfloat units)
{
	glPolygonOffset(factor, units);
}

void
qglCullFace(GLenum mode)
{
	glCullFace(mode);
}

void
qglScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
	glScissor(x, y, width, height);
}

void
qglViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
	glViewport(x, y, width, height);
}

void
qglDepthRangef(GLclampf zNear, GLclampf zFar)
{
	glDepthRangef(zNear, zFar);
}

void
qglBlendFunc(GLenum sfactor, GLenum dfactor)
{
	glBlendFunc(sfactor, dfactor);
}

void
qglDepthMask(GLboolean flag)
{
	glDepthMask(flag);
}

void
qglDepthFunc(GLenum func)
{
	glDepthFunc(func);
}

void
qglClear(GLbitfield mask)
{
	glClear(mask);
}

/*
 * For tracking draws.
 */
static int color_active;
static int color_size;
static int color_pitch;
static const unsigned int *color_ptr;

static int texture_current;
static int texture_coord_current;

static int texture0_active;
static int coord0_active;
static int coord0_size;
static int coord0_pitch;
static const unsigned int *coord0_ptr;

static int texture1_active;
static int coord1_active;
static int coord1_size;
static int coord1_pitch;
static const unsigned int *coord1_ptr;

static int vertex_size;
static int vertex_pitch;
static const unsigned int *vertex_ptr;

void
qglEnable(GLenum cap)
{
	if (cap == GL_TEXTURE_2D) {
		if (texture_current)
			texture1_active = 1;
		else
			texture0_active = 1;
	} else if (cap == GL_ALPHA_TEST)
		;
	else
		glEnable(cap);
}

void
qglDisable(GLenum cap)
{
	if (cap == GL_TEXTURE_2D) {
		if (texture_current)
			texture1_active = 0;
		else
			texture0_active = 0;
	} else if (cap == GL_ALPHA_TEST)
		;
	else
		glDisable(cap);
}

void
qglDeleteTextures(GLsizei n, const GLuint *textures)
{
	glDeleteTextures(n, textures);
}

void
qglBindTexture(GLenum target, GLuint id)
{
	glBindTexture(target, id);
}

void
qglTexImage2D(GLenum target, GLint level, GLint internalformat,
	      GLsizei width, GLsizei height, GLint border,
	      GLenum format, GLenum type, const GLvoid *pixels)
{
	glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, width, height, 0,
		     format, GL_UNSIGNED_BYTE, pixels);
}

void
qglTexParameteri(GLenum target, GLenum pname, GLint param)
{
	glTexParameteri(target, pname, param);
}

void
qglActiveTexture(GLenum texture)
{
	//printf("%s(%s);\n", __func__, GLEnumString(texture));

	glActiveTexture(texture);

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
	//printf("%s(%s);\n", __func__, GLEnumString(texture));

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
	//printf("%s(%s);\n", __func__, GLEnumString(array));

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
	//printf("%s(%s);\n", __func__, GLEnumString(array));

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
#if 0
	const unsigned short *indices = ptr;
	int count = 0;
	int i;

	if (type != GL_UNSIGNED_SHORT) {
		fprintf(stderr, "%s: Error: unsupported type.\n", __func__);
		return;
	}

	for (i = 0; i < indices_count; i++)
		if (indices[i] > count)
			count = indices[i];
	count++;
#endif

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
		//printf("%s: coord1 is not active!\n", __func__);

		if (program_current != program_single_texture) {
			glUseProgram(program_single_texture);
			program_current = program_single_texture;
		}
	} else {
		//printf("%s: coord1 is active!\n", __func__);

		if (program_current != program_dual_texture) {
			glUseProgram(program_dual_texture);
			program_current = program_dual_texture;
		}
	}

	glVertexAttribPointer(0, vertex_size, GL_FLOAT,
			      GL_FALSE, vertex_pitch, vertex_ptr);
	glEnableVertexAttribArray(0);

	if (color_active) {
		glVertexAttribPointer(1, color_size, GL_UNSIGNED_BYTE,
				      GL_TRUE, color_pitch, color_ptr);
		glEnableVertexAttribArray(1);
	} else
		printf("Color is not active!\n");

	if (coord0_active && texture0_active) {
		//printf("coord0: %d, %d, %p\n", coord0_size, coord0_pitch, coord0_ptr);
		glVertexAttribPointer(2, coord0_size, GL_FLOAT,
				      GL_FALSE, coord0_pitch, coord0_ptr);
		glEnableVertexAttribArray(2);
	}

	if (coord1_active && texture1_active) {
		//printf("coord1: %d, %d, %p\n", coord1_size, coord1_pitch, coord1_ptr);
		glVertexAttribPointer(3, coord1_size, GL_FLOAT,
				      GL_FALSE, coord1_pitch, coord1_ptr);
		glEnableVertexAttribArray(3);
	}

	glDrawElements(GL_TRIANGLES, indices_count,
		       GL_UNSIGNED_SHORT, ptr);
	//GLTestError("draw_draw()");

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
