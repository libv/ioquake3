#define USE_REAL_GL_CALLS 1

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>

/* for fbdev poking */
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/fb.h>

#include <EGL/egl.h>
#include <GLES/gl.h>

#include "../sys/sys_local.h"
#include "../qcommon/q_shared.h"
#include "egl_glimp.h"
#include "../client/client.h"
#include "../renderer/tr_local.h"

#define QGL_LOG_GL_CALLS 1
#ifdef QGL_LOG_GL_CALLS
void log_frame_new(void);
#endif

EGLContext eglContext = NULL;
EGLDisplay eglDisplay = NULL;
EGLSurface eglSurface = NULL;

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

/*
 * Create an RGB window.
 * Return the window and context handles.
 */
static void
make_window(EGLDisplay eglDisplay, int width, int height,
	    EGLSurface *winRet, EGLContext *ctxRet)
{
	EGLSurface eglSurface = EGL_NO_SURFACE;
	EGLContext eglContext;
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
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT,

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
			      NULL)) == EGL_NO_CONTEXT)
		GLimp_HandleError();

	if (!eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext))
		GLimp_HandleError();

	*winRet = eglSurface;
	*ctxRet = eglContext;
}

static qboolean GLimp_HaveExtension(const char *ext)
{
	const char *ptr = Q_stristr(glConfig.extensions_string, ext);
	if (ptr == NULL)
		return qfalse;
	ptr += strlen(ext);
	return ((*ptr == ' ') || (*ptr == '\0'));  // verify it's complete string.
}

static void qglMultiTexCoord2f(GLenum target, GLfloat s, GLfloat t)
{
	qglMultiTexCoord4f(target,s,t,1,1);
}


/*
===============
GLimp_InitExtensions
===============
*/
static void GLimp_InitExtensions(void)
{
	if (!r_allowExtensions->integer)
	{
		ri.Printf(PRINT_ALL, "* IGNORING OPENGL EXTENSIONS *\n");
		return;
	}

	ri.Printf(PRINT_ALL, "Initializing OpenGL extensions\n");

	glConfig.textureCompression = TC_NONE;

	// GL_EXT_texture_compression_s3tc
	if (GLimp_HaveExtension("GL_ARB_texture_compression") &&
		GLimp_HaveExtension("GL_EXT_texture_compression_s3tc")) {
		if (r_ext_compressed_textures->value) {
			glConfig.textureCompression = TC_S3TC_ARB;
			ri.Printf(PRINT_ALL,
				  "...using GL_EXT_texture_compression_s3tc\n");
		} else
			ri.Printf(PRINT_ALL, "...ignoring "
				  "GL_EXT_texture_compression_s3tc\n");
	} else
		ri.Printf(PRINT_ALL,
			  "...GL_EXT_texture_compression_s3tc not found\n");

	if (glConfig.textureCompression == TC_NONE) {
		if (GLimp_HaveExtension("GL_S3_s3tc")) {
			if (r_ext_compressed_textures->value) {
				glConfig.textureCompression = TC_S3TC;
				ri.Printf(PRINT_ALL, "...using GL_S3_s3tc\n");
			} else
				ri.Printf(PRINT_ALL,
					  "...ignoring GL_S3_s3tc\n");
		} else
			ri.Printf(PRINT_ALL, "...GL_S3_s3tc not found\n");
	}


	// GL_EXT_texture_env_add
	glConfig.textureEnvAddAvailable = qtrue; //qfalse;

	if (r_ext_multitexture->value) {
		qglMultiTexCoord2fARB = qglMultiTexCoord2f;
		qglActiveTextureARB = qglActiveTexture;
		qglClientActiveTextureARB = qglClientActiveTexture;

		if (qglActiveTextureARB) {
			GLint glint = 0;
			qglGetIntegerv(GL_MAX_TEXTURE_UNITS, &glint);
			glConfig.numTextureUnits = (int) glint;
			if (glConfig.numTextureUnits > 1)
				ri.Printf(PRINT_ALL,
					  "...using GL_ARB_multitexture\n");
			else {

				qglMultiTexCoord2fARB = NULL;
				qglActiveTextureARB = NULL;
				qglClientActiveTextureARB = NULL;
				ri.Printf(PRINT_ALL,
					  "...not using GL_ARB_multitexture,"
					  " < 2 texture units\n");
			}
		} else
			ri.Printf(PRINT_ALL,
				  "...ignoring GL_ARB_multitexture\n");
	}

	ri.Printf(PRINT_ALL, "...GL_EXT_compiled_vertex_array not found\n");

	textureFilterAnisotropic = qfalse;


	ri.Printf(PRINT_ALL,
		  "...GL_EXT_texture_filter_anisotropic not found\n");
}

void GLimp_Init(void)
{
	EGLint major, minor;
	int fb_width, fb_height;

#ifdef QGL_LOG_GL_CALLS
	log_frame_new();
#endif

	ri.Printf(PRINT_ALL, "Initializing OpenGL subsystem\n");

	bzero(&glConfig, sizeof(glConfig));

	fbdev_size(&fb_width, &fb_height);

	ri.Printf(PRINT_ALL, "FB dimensions %dx%d\n", fb_width, fb_height);

	eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (!eglInitialize(eglDisplay, &major, &minor))
		GLimp_HandleError();

	make_window(eglDisplay, fb_width, fb_height, &eglSurface, &eglContext);
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

	Q_strncpyz(glConfig.vendor_string,
		   (const char *)glGetString(GL_VENDOR),
		   sizeof(glConfig.vendor_string));
	Q_strncpyz(glConfig.renderer_string,
		   (const char *)glGetString(GL_RENDERER),
		   sizeof(glConfig.renderer_string));
	Q_strncpyz(glConfig.version_string,
		   (const char *)glGetString(GL_VERSION),
		   sizeof(glConfig.version_string));
	Q_strncpyz(glConfig.extensions_string,
		   (const char *)glGetString(GL_EXTENSIONS),
		   sizeof(glConfig.extensions_string));

	qglLockArraysEXT = qglLockArrays;
	qglUnlockArraysEXT = qglUnlockArrays;

	GLimp_InitExtensions();

	IN_Init();

	ri.Printf(PRINT_ALL, "------------------\n");
}

void GLimp_LogComment(char *comment)
{
	//fprintf(stderr, "%s: %s\n", __func__, comment);
}

#ifdef QGL_LOG_GL_CALLS
#warning enabling GL logging.

static FILE *log_main_file;
static FILE *log_draws_file;
static FILE *log_textures_file;
static FILE *log_header_file;

static int framecount;
static int draw_count;
static int matrix_count;

void
log_frame_new(void)
{
	char buffer[1024];

	printf("Finished dumping replay frame %d\n", framecount);

	if (log_main_file)
		fclose(log_main_file);
	if (log_draws_file)
		fclose(log_draws_file);
	if (log_textures_file)
		fclose(log_textures_file);
	if (log_header_file)
		fclose(log_header_file);

	framecount++;

	snprintf(buffer, sizeof(buffer), "frame_%04d.c", framecount);
	log_main_file = fopen(buffer, "w");
	if (!log_main_file) {
		fprintf(stderr, "Error opening %s: %s\n", buffer,
			strerror(errno));
		exit(1);
	}

	snprintf(buffer, sizeof(buffer), "frame_%04d_draws.c", framecount);
	log_draws_file = fopen(buffer, "w");
	if (!log_draws_file) {
		fprintf(stderr, "Error opening %s: %s\n", buffer,
			strerror(errno));
		exit(1);
	}

	snprintf(buffer, sizeof(buffer), "frame_%04d_textures.c", framecount);
	log_textures_file = fopen(buffer, "w");
	if (!log_textures_file) {
		fprintf(stderr, "Error opening %s: %s\n", buffer,
			strerror(errno));
		exit(1);
	}

	snprintf(buffer, sizeof(buffer), "frame_%04d.h", framecount);
	log_header_file = fopen(buffer, "w");
	if (!log_header_file) {
		fprintf(stderr, "Error opening %s: %s\n", buffer,
			strerror(errno));
		exit(1);
	}

	draw_count = 0;
	matrix_count = 0;
}
#endif

#ifdef QGL_LOG_GL_CALLS
void
log_main(const char *template, ...)
{
	va_list ap;

	va_start(ap, template);
	vfprintf(log_main_file, template, ap);
	va_end(ap);
}

void
log_draws(const char *template, ...)
{
	va_list ap;

	va_start(ap, template);
	vfprintf(log_draws_file, template, ap);
	va_end(ap);
}
#else
#define log_main(msg, ...)
#define log_draws(msg, ...)
#endif

void GLimp_EndFrame(void)
{
	log_main("\t//eglSwapBuffers(eglDisplay, eglSurface);\n");
	eglSwapBuffers(eglDisplay, eglSurface);
}

void GLimp_Shutdown(void)
{
	log_main("\t/* %s */\n", __func__);

	IN_Shutdown();

	eglDestroyContext(eglDisplay, eglContext);
	eglDestroySurface(eglDisplay, eglSurface);
	eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       EGL_NO_CONTEXT);
	eglTerminate(eglDisplay);
}

void qglCallList(GLuint list)
{
	log_main("\t%s(%d);\n", __func__, list);
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

#ifdef QGL_LOG_GL_CALLS
#include "glenumstring.c"
#endif

void
qglDrawBuffer(GLenum mode)
{
#ifdef QGL_LOG_GL_CALLS
	log_frame_new();
#endif
}

static int vertices_count;
int bound_texture;

void
qglNumVertices(GLint count)
{
	vertices_count = count;
}

void
qglLockArrays(GLint j, GLsizei size)
{
}

void
qglUnlockArrays(void)
{
}

const float *tex_coords_ptr[2];
int current_texture;

void
qglTexCoordPointer(GLint size, GLenum type, GLsizei stride,
		   const GLvoid *pointer)
{
#ifdef QGL_LOG_GL_CALLS
	tex_coords_ptr[current_texture] = pointer;

	log_main("\tglTexCoordPointer(%d, %s, %d, TextureCoordinates_%d_%d);\n",
		 size, GLEnumString(type), stride, draw_count,
		 current_texture);
#endif
	glTexCoordPointer(size, type, stride, pointer);
}

const unsigned char *color_ptr;

void
qglColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
#ifdef QGL_LOG_GL_CALLS
	color_ptr = pointer;

	log_main("\tglColorPointer(%d, %s, %d, Colors_%d);\n",
		 size, GLEnumString(type), stride, draw_count);
#endif
    glColorPointer(size, type, stride, pointer);
}


const float *vertices_ptr;
int vertices_stride;
int vertices_new;

void
qglVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
#ifdef QGL_LOG_GL_CALLS
	vertices_ptr = pointer;
	vertices_stride = stride;
	vertices_new = 1;

	log_main("\tglVertexPointer(%d, %s, %d, Vertices_%d);\n",
		 size, GLEnumString(type), stride, draw_count);
#endif
	glVertexPointer(size, type, stride, pointer);
}

#ifdef QGL_LOG_GL_CALLS
void
data_print(int count)
{
	int i;

	if (color_ptr) {
		const unsigned char *colors = color_ptr;

		log_draws("\tunsigned char Colors_%d[%d][4] = {\n",
			draw_count, count);

		for (i = 0; i < count; i++)
			fprintf(log_draws_file,
				"\t\t{0x%02X, 0x%02X, 0x%02X, 0x%02X},\n",
				colors[4 * i], colors[4 * i + 1],
				colors[4 * i + 2], colors[4 * i + 3]);

		log_draws("\t};\n");
	}

	if (tex_coords_ptr[0]) {
		const float *coords = tex_coords_ptr[0];

		fprintf(log_draws_file,
			"\tfloat TextureCoordinates_%d_%d[%d][2] = {\n",
			draw_count, 0, count);
		for (i = 0; i < count; i++)
			log_draws("\t\t{%a, %a},\n",
				coords[2 * i], coords[2 * i + 1]);
		log_draws("\t};\n");
	}

	if (tex_coords_ptr[1]) {
		const float *coords = tex_coords_ptr[1];

		fprintf(log_draws_file,
			"\tfloat TextureCoordinates_%d_%d[%d][2] = {\n",
			draw_count, 1, count);
		for (i = 0; i < count; i++)
			log_draws("\t\t{%a, %a},\n",
				coords[2 * i], coords[2 * i + 1]);
		log_draws("\t};\n");
	}


	if (vertices_new) {
		const float *vertices = vertices_ptr;
		int i;

		if (vertices_stride) {
			log_draws("\tfloat Vertices_%d[%d][4] = {\n",
				draw_count, count);
			for (i = 0; i < count; i++)
				log_draws("\t\t{%a, %a, %a, %a},\n",
					vertices[4 * i], vertices[4 * i + 1],
					vertices[4 * i + 2],
					vertices[4 * i + 3]);
		} else {
			log_draws("\tfloat Vertices_%d[%d][2] = {\n",
				draw_count, count);
			for (i = 0; i < count; i++)
				log_draws("\t\t{%a, %a},\n",
					vertices[2 * i], vertices[2 * i + 1]);
		}
		log_draws("\t};\n");
		vertices_new = 0;
	}
}
#endif

void
qglDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *ptr)
{
#ifdef QGL_LOG_GL_CALLS
	const unsigned short *indices = ptr;
	int i;

	data_print(count);

	log_draws("\tunsigned short Indices_%d[%d] = {\n",
		draw_count, count);
	for (i = 0; i < count; i++) {
		if (!(i % 8))
			log_draws("\t\t0x%04X,", indices[i]);
		else if ((i % 8) == 7)
			log_draws(" 0x%04X,\n", indices[i]);
		else
			log_draws(" 0x%04X,", indices[i]);
	}

	if ((i % 8))
		log_draws("\n");
	log_draws("\t};\n");

	log_main("\tglDrawElements(%s, %d, %s, Indices_%d);\n",
		 GLEnumString(mode), count, GLEnumString(type), draw_count);
	draw_count++;

	current_texture = 0;
	tex_coords_ptr[0] = NULL;
	tex_coords_ptr[1] = NULL;
#endif
	glDrawElements(mode, count, type, ptr);
}

void
qglLoadMatrixf(const GLfloat *m)
{
#ifdef QGL_LOG_GL_CALLS
	log_main("\tfloat Matrix_%d[16] = {\n", matrix_count);
	log_main("\t\t%a, %a, %a, %a,\n", m[0], m[1], m[2], m[3]);
	log_main("\t\t%a, %a, %a, %a,\n", m[4], m[5], m[6], m[7]);
	log_main("\t\t%a, %a, %a, %a,\n", m[8], m[9], m[10], m[11]);
	log_main("\t\t%a, %a, %a, %a,\n", m[12], m[13], m[14], m[15]);
	log_main("\t};\n");
	log_main("\tglLoadMatrixf(Matrix_%d);\n", matrix_count);
	matrix_count++;
#endif
	glLoadMatrixf(m);
}

void
qglTexImage2D(GLenum target, GLint level, GLint internalformat,
	      GLsizei width, GLsizei height, GLint border,
	      GLenum format, GLenum type, const GLvoid *pixels)
{
#ifdef QGL_LOG_GL_CALLS
	const unsigned int *texture = pixels;
	int i;

	if (internalformat == GL_RGB) {
		printf("Texture %d is flagged as an RGB texture!\n",
		       bound_texture);
		internalformat = GL_RGBA;
	}

	log_main("\tunsigned int Texture_%d_%d[%d * %d] = {\n",
		 bound_texture, level, width, height);
	for (i = 0; i < (width * height); i++) {
		if (!(i % 4))
			log_main("\t\t0x%08X,", texture[i]);
		else if ((i % 4) == 3)
			log_main(" 0x%08X,\n", texture[i]);
		else
			log_main(" 0x%08X,", texture[i]);
	}

	if ((i % 4))
		log_main("\n");
	log_main("\t};\n");

	log_main("\tglTexImage2D(%s, %d, %s, %d, %d, %d, %s, %s, "
		 "Texture_%d_%d);\n", GLEnumString(target), level,
		 GLEnumString(internalformat), width, height, border,
		 GLEnumString(format), GLEnumString(type), bound_texture,
		 level);
#endif
	glTexImage2D(target, level, internalformat, width, height, border,
		     format, type, pixels);
}

void
qglTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
		 GLsizei width, GLsizei height, GLenum format, GLenum type,
		 const GLvoid *pixels)
{
	log_main("\tglTexSubImage2D(%s, %d, %d, %d, %d, %d, %s, %s, %p);\n",
		 GLEnumString(target), level, xoffset, yoffset, width, height,
		 GLEnumString(format), GLEnumString(type), pixels);
	glTexSubImage2D(target, level, xoffset, yoffset, width, height, format,
			type, pixels);
}

void
qglDeleteTextures(GLsizei n, const GLuint *textures)
{
	log_main("\ttmp = %d;\n", textures[0]);
	log_main("\tglDeleteTextures(%d, &tmp);\n", n);
	glDeleteTextures(n, textures);
}

void
qglBindTexture(GLenum target, GLuint texture)
{
#ifdef QGL_LOG_GL_CALLS
	bound_texture = texture;
	log_main("\tglBindTexture(%s, %u);\n", GLEnumString(target), texture);
#endif
	glBindTexture(target, texture);
}

void
qglActiveTexture(GLenum texture)
{
#ifdef QGL_LOG_GL_CALLS
	if (texture == GL_TEXTURE1)
		current_texture = 1;
	else
		current_texture = 0;
	log_main("\tglActiveTexture(%s);\n", GLEnumString(texture));
#endif
	glActiveTexture(texture);
}

void
qglDisableClientState(GLenum array)
{
	log_main("\tglDisableClientState(%s);\n", GLEnumString(array));
	glDisableClientState(array);
}

void
qglAlphaFunc(GLenum func, GLclampf ref)
{
	log_main("\tglAlphaFunc(%s, %f);\n", GLEnumString(func), ref);
	glAlphaFunc(func, ref);
}

void
qglClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
	log_main("\tglClearColor(%f, %f, %f, %f);\n", red, green, blue, alpha);
	glClearColor(red, green, blue, alpha);
}

void
qglClearDepthf(GLclampf depth)
{
	log_main("\tglClearDepthf(%f);\n", depth);
	glClearDepthf(depth);
}

void
qglClipPlanef(GLenum plane, const GLfloat *equation)
{
	log_main("\tglClipPlanef(%s, %p);\n", GLEnumString(plane), equation);
	glClipPlanef(plane, equation);
}

void
qglColor4f (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
	log_main("\tglColor4f(%f, %f, %f, %f);\n", red, green, blue, alpha);
	glColor4f(red, green, blue, alpha);
}

void
qglDepthRangef(GLclampf zNear, GLclampf zFar)
{
	log_main("\tglDepthRangef(%f, %f);\n", zNear, zFar);
	glDepthRangef(zNear, zFar);
}

void
qglLineWidth(GLfloat width)
{
	log_main("\tglLineWidth(%f);\n", width);
	glLineWidth(width);
}

void
qglMaterialf(GLenum face, GLenum pname, GLfloat param)
{
	log_main("\tglMaterialf(%s, %s, %f);\n", GLEnumString(face),
		 GLEnumString(pname), param);
	glMaterialf(face, pname, param);
}

void
qglMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
	log_main("\tglMultiTexCoord4f(%s, %f, %f, %f, %f);\n",
		 GLEnumString(target), s, t, r, q);
	glMultiTexCoord4f(target, s, t, r, q);
}

void
qglOrthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top,
	  GLfloat zNear, GLfloat zFar)
{
	log_main("\tglOrthof(%f, %f, %f, %f, %f, %f);\n",
		 left, right, bottom, top, zNear, zFar);
	glOrthof(left, right, bottom, top, zNear, zFar);
}

void
qglPolygonOffset(GLfloat factor, GLfloat units)
{
	log_main("\tglPolygonOffset(%f, %f);\n", factor, units);
	glPolygonOffset(factor, units);
}

void
qglTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
	log_main("\tglTexEnvf(%s, %s, %f);\n", GLEnumString(target),
		 GLEnumString(pname), param);
	glTexEnvf(target, pname, param);
}

void
qglTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
	log_main("\tglTranslatef(%f, %f, %f);\n", x, y, z);
	glTranslatef(x, y, z);
}

void
qglAlphaFuncx(GLenum func, GLclampx ref)
{
	log_main("\tglAlphaFuncx(%s, %d);\n", GLEnumString(func), ref);
	glAlphaFuncx(func, ref);
}

void
qglBlendFunc(GLenum sfactor, GLenum dfactor)
{
	log_main("\tglBlendFunc(%s, %s);\n", GLEnumString(sfactor),
		 GLEnumString(dfactor));
	glBlendFunc(sfactor, dfactor);
}

void
qglClear(GLbitfield mask)
{
	log_main("\tglClear(%s);\n", GLEnumString(mask));
	glClear(mask);
}

void
qglClearStencil(GLint s)
{
	log_main("\tglClearStencil(%d);\n", s);
	glClearStencil(s);
}

void
qglClientActiveTexture(GLenum texture)
{
	log_main("\tglClientActiveTexture(%s);\n", GLEnumString(texture));
	glClientActiveTexture(texture);
}

void
qglColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
	log_main("\tglColorMask(%u, %u, %u, %u);\n", red, green, blue, alpha);
	glColorMask(red, green, blue, alpha);
}

void
qglCullFace(GLenum mode)
{
	log_main("\tglCullFace(%s);\n", GLEnumString(mode));
	glCullFace(mode);
}

void
qglDepthFunc(GLenum func)
{
	log_main("\tglDepthFunc(%s);\n", GLEnumString(func));
	glDepthFunc(func);
}

void
qglDepthMask(GLboolean flag)
{
	log_main("\tglDepthMask(%u);\n", flag);
	glDepthMask(flag);
}

void
qglDisable(GLenum cap)
{
	log_main("\tglDisable(%s);\n", GLEnumString(cap));
	glDisable(cap);
}

void
qglDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	log_main("\tglDrawArrays(%s, %d, %d);\n", GLEnumString(mode),
		 first, count);
	glDrawArrays(mode, first, count);
}

void
qglEnable(GLenum cap)
{
	log_main("\tglEnable(%s);\n", GLEnumString(cap));
	glEnable(cap);
}

void
qglEnableClientState(GLenum array)
{
	log_main("\tglEnableClientState(%s);\n", GLEnumString(array));
	glEnableClientState(array);
}

void
qglFinish(void)
{
	log_main("\tglFinish();\n");
	glFinish();
}

void
qglFlush(void)
{
	log_main("\tglFlush();\n");
	glFlush();
}

void
qglGetBooleanv(GLenum pname, GLboolean *params)
{
	log_main("\tglGetBooleanv(%s, %p);\n", GLEnumString(pname), params);
	glGetBooleanv(pname, params);
}

GLenum
qglGetError(void)
{
	log_main("\tglGetError();\n");
	return glGetError();
}

void
qglGetIntegerv(GLenum pname, GLint *params)
{
	log_main("\tglGetIntegerv(%s, %p);\n", GLEnumString(pname), params);
	glGetIntegerv(pname, params);
}

void
qglLoadIdentity(void)
{
	log_main("\tglLoadIdentity();\n");
	glLoadIdentity();
}

void
qglMatrixMode(GLenum mode)
{
	log_main("\tglMatrixMode(%s);\n", GLEnumString(mode));
	glMatrixMode(mode);
}

void
qglPopMatrix(void)
{
	log_main("\tglPopMatrix();\n");
	glPopMatrix();
}

void
qglPushMatrix(void)
{
	log_main("\tglPushMatrix();\n");
	glPushMatrix();
}

void
qglReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format,
	      GLenum type, GLvoid *pixels)
{
	log_main("\tglReadPixels(%d, %d, %d, %d, %s, %s, %p);\n", x, y, width,
		 height, GLEnumString(format), GLEnumString(type), pixels);
	glReadPixels(x, y, width, height, format, type, pixels);
}

void
qglScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
	log_main("\tglScissor(%d, %d, %d, %d);\n", x, y, width, height);
	glScissor(x, y, width, height);
}

void
qglShadeModel(GLenum mode)
{
	log_main("\tglShadeModel(%s);\n", GLEnumString(mode));
	glShadeModel(mode);
}

void
qglStencilFunc(GLenum func, GLint ref, GLuint mask)
{
	log_main("\tglStencilFunc(%s, %d, %s);\n", GLEnumString(func), ref,
		 GLEnumString(mask));
	glStencilFunc(func, ref, mask);
}

void
qglStencilMask(GLuint mask)
{
	log_main("\tglStencilMask(%s);\n", GLEnumString(mask));
	glStencilMask(mask);
}

void
qglStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
	log_main("\tglStencilOp(%s, %s, %s);\n", GLEnumString(fail),
		 GLEnumString(zfail), GLEnumString(zpass));
	glStencilOp(fail, zfail, zpass);
}

void
qglTexEnvi(GLenum target, GLenum pname, GLint param)
{
	log_main("\tglTexEnvi(%s, %s, %d);\n", GLEnumString(target),
		 GLEnumString(pname), param);
	glTexEnvi(target, pname, param);
}

void
qglTexParameteri(GLenum target, GLenum pname, GLint param)
{
	log_main("\tglTexParameteri(%s, %s, %d);\n", GLEnumString(target),
		 GLEnumString(pname), param);
	glTexParameteri(target, pname, param);
}

void
qglViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
	log_main("\tglViewport(%d, %d, %d, %d);\n", x, y, width, height);
	glViewport(x, y, width, height);
}
