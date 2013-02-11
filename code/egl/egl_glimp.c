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

//#define QGL_LOG_GL_CALLS 1

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
		   (const char *)qglGetString(GL_VENDOR),
		   sizeof(glConfig.vendor_string));
	Q_strncpyz(glConfig.renderer_string,
		   (const char *)qglGetString(GL_RENDERER),
		   sizeof(glConfig.renderer_string));
	Q_strncpyz(glConfig.version_string,
		   (const char *)qglGetString(GL_VERSION),
		   sizeof(glConfig.version_string));
	Q_strncpyz(glConfig.extensions_string,
		   (const char *)qglGetString(GL_EXTENSIONS),
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
static FILE *qgllog;
static FILE *qgldata;

static int framecount;
static int draw_count;
static int matrix_count;

void QGLLogNew(void)
{
	char buffer[1024];

	if (qgllog && (qgllog != stderr)) {
		printf("Finished dumping replay frame %d\n", framecount);
		fclose(qgllog);
	}

	if (qgldata && (qgldata != stderr))
		fclose(qgldata);

	framecount++;

	snprintf(buffer, sizeof(buffer), "replay_%04d.c", framecount);
	qgllog = fopen(buffer, "w");
	if (!qgllog) {
		fprintf(stderr, "Error opening %s: %s\n",
			buffer, strerror(errno));
		qgllog = stderr;
	}

	snprintf(buffer, sizeof(buffer), "replay_%04d_data.c", framecount);
	qgldata = fopen(buffer, "w");
	if (!qgldata) {
		fprintf(stderr, "Error opening %s: %s\n",
			buffer, strerror(errno));
		qgldata = stderr;
	}

	draw_count = 0;
	matrix_count = 0;
}

FILE *QGLDebugFile(void)
{
	if (!qgllog)
		QGLLogNew();

	return qgllog;
}
#endif

#ifdef QGL_LOG_GL_CALLS
void
log_gl(const char *template, ...)
{
	FILE* log = QGLDebugFile();
	va_list ap;

	va_start(ap, template);
	vfprintf(log, template, ap);
	va_end(ap);
}
#else
#define log_gl(msg, ...)
#endif

void GLimp_EndFrame(void)
{
	log_gl("\t//eglSwapBuffers(eglDisplay, eglSurface);\n");
	eglSwapBuffers(eglDisplay, eglSurface);
}

void GLimp_Shutdown(void)
{
	log_gl("\t/* %s */\n", __func__);

	IN_Shutdown();

	eglDestroyContext(eglDisplay, eglContext);
	eglDestroySurface(eglDisplay, eglSurface);
	eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       EGL_NO_CONTEXT);
	eglTerminate(eglDisplay);
}

void qglCallList(GLuint list)
{
	log_gl("\t%s(%d);\n", __func__, list);
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
#warning enabling GL logging.

static struct {
	GLenum value;
	char *name;
} GLEnumStrings[] = {
	{GL_DEPTH_BUFFER_BIT, "GL_DEPTH_BUFFER_BIT"},
	{GL_STENCIL_BUFFER_BIT, "GL_STENCIL_BUFFER_BIT"},
	{GL_COLOR_BUFFER_BIT, "GL_COLOR_BUFFER_BIT"},
	{GL_FALSE, "GL_FALSE"},
	{GL_TRUE, "GL_TRUE"},
	{GL_POINTS, "GL_POINTS"},
	{GL_LINES, "GL_LINES"},
	{GL_LINE_LOOP, "GL_LINE_LOOP"},
	{GL_LINE_STRIP, "GL_LINE_STRIP"},
	{GL_TRIANGLES, "GL_TRIANGLES"},
	{GL_TRIANGLE_STRIP, "GL_TRIANGLE_STRIP"},
	{GL_TRIANGLE_FAN, "GL_TRIANGLE_FAN"},
	{GL_NEVER, "GL_NEVER"},
	{GL_LESS, "GL_LESS"},
	{GL_EQUAL, "GL_EQUAL"},
	{GL_LEQUAL, "GL_LEQUAL"},
	{GL_GREATER, "GL_GREATER"},
	{GL_NOTEQUAL, "GL_NOTEQUAL"},
	{GL_GEQUAL, "GL_GEQUAL"},
	{GL_ALWAYS, "GL_ALWAYS"},
	{GL_ZERO, "GL_ZERO"},
	{GL_ONE, "GL_ONE"},
	{GL_SRC_COLOR, "GL_SRC_COLOR"},
	{GL_ONE_MINUS_SRC_COLOR, "GL_ONE_MINUS_SRC_COLOR"},
	{GL_SRC_ALPHA, "GL_SRC_ALPHA"},
	{GL_ONE_MINUS_SRC_ALPHA, "GL_ONE_MINUS_SRC_ALPHA"},
	{GL_DST_ALPHA, "GL_DST_ALPHA"},
	{GL_ONE_MINUS_DST_ALPHA, "GL_ONE_MINUS_DST_ALPHA"},
	{GL_DST_COLOR, "GL_DST_COLOR"},
	{GL_ONE_MINUS_DST_COLOR, "GL_ONE_MINUS_DST_COLOR"},
	{GL_SRC_ALPHA_SATURATE, "GL_SRC_ALPHA_SATURATE"},
	{GL_CLIP_PLANE0, "GL_CLIP_PLANE0"},
	{GL_CLIP_PLANE1, "GL_CLIP_PLANE1"},
	{GL_CLIP_PLANE2, "GL_CLIP_PLANE2"},
	{GL_CLIP_PLANE3, "GL_CLIP_PLANE3"},
	{GL_CLIP_PLANE4, "GL_CLIP_PLANE4"},
	{GL_CLIP_PLANE5, "GL_CLIP_PLANE5"},
	{GL_FRONT, "GL_FRONT"},
	{GL_BACK, "GL_BACK"},
	{GL_FRONT_AND_BACK, "GL_FRONT_AND_BACK"},
	{GL_FOG, "GL_FOG"},
	{GL_LIGHTING, "GL_LIGHTING"},
	{GL_TEXTURE_2D, "GL_TEXTURE_2D"},
	{GL_CULL_FACE, "GL_CULL_FACE"},
	{GL_ALPHA_TEST, "GL_ALPHA_TEST"},
	{GL_BLEND, "GL_BLEND"},
	{GL_COLOR_LOGIC_OP, "GL_COLOR_LOGIC_OP"},
	{GL_DITHER, "GL_DITHER"},
	{GL_STENCIL_TEST, "GL_STENCIL_TEST"},
	{GL_DEPTH_TEST, "GL_DEPTH_TEST"},
	{GL_POINT_SMOOTH, "GL_POINT_SMOOTH"},
	{GL_LINE_SMOOTH, "GL_LINE_SMOOTH"},
	{GL_SCISSOR_TEST, "GL_SCISSOR_TEST"},
	{GL_COLOR_MATERIAL, "GL_COLOR_MATERIAL"},
	{GL_NORMALIZE, "GL_NORMALIZE"},
	{GL_RESCALE_NORMAL, "GL_RESCALE_NORMAL"},
	{GL_POLYGON_OFFSET_FILL, "GL_POLYGON_OFFSET_FILL"},
	{GL_VERTEX_ARRAY, "GL_VERTEX_ARRAY"},
	{GL_NORMAL_ARRAY, "GL_NORMAL_ARRAY"},
	{GL_COLOR_ARRAY, "GL_COLOR_ARRAY"},
	{GL_TEXTURE_COORD_ARRAY, "GL_TEXTURE_COORD_ARRAY"},
	{GL_MULTISAMPLE, "GL_MULTISAMPLE"},
	{GL_SAMPLE_ALPHA_TO_COVERAGE, "GL_SAMPLE_ALPHA_TO_COVERAGE"},
	{GL_SAMPLE_ALPHA_TO_ONE, "GL_SAMPLE_ALPHA_TO_ONE"},
	{GL_SAMPLE_COVERAGE, "GL_SAMPLE_COVERAGE"},
	{GL_NO_ERROR, "GL_NO_ERROR"},
	{GL_INVALID_ENUM, "GL_INVALID_ENUM"},
	{GL_INVALID_VALUE, "GL_INVALID_VALUE"},
	{GL_INVALID_OPERATION, "GL_INVALID_OPERATION"},
	{GL_STACK_OVERFLOW, "GL_STACK_OVERFLOW"},
	{GL_STACK_UNDERFLOW, "GL_STACK_UNDERFLOW"},
	{GL_OUT_OF_MEMORY, "GL_OUT_OF_MEMORY"},
	{GL_EXP, "GL_EXP"},
	{GL_EXP2, "GL_EXP2"},
	{GL_FOG_DENSITY, "GL_FOG_DENSITY"},
	{GL_FOG_START, "GL_FOG_START"},
	{GL_FOG_END, "GL_FOG_END"},
	{GL_FOG_MODE, "GL_FOG_MODE"},
	{GL_FOG_COLOR, "GL_FOG_COLOR"},
	{GL_CW, "GL_CW"},
	{GL_CCW, "GL_CCW"},
	{GL_CURRENT_COLOR, "GL_CURRENT_COLOR"},
	{GL_CURRENT_NORMAL, "GL_CURRENT_NORMAL"},
	{GL_CURRENT_TEXTURE_COORDS, "GL_CURRENT_TEXTURE_COORDS"},
	{GL_POINT_SIZE, "GL_POINT_SIZE"},
	{GL_POINT_SIZE_MIN, "GL_POINT_SIZE_MIN"},
	{GL_POINT_SIZE_MAX, "GL_POINT_SIZE_MAX"},
	{GL_POINT_FADE_THRESHOLD_SIZE, "GL_POINT_FADE_THRESHOLD_SIZE"},
	{GL_POINT_DISTANCE_ATTENUATION, "GL_POINT_DISTANCE_ATTENUATION"},
	{GL_SMOOTH_POINT_SIZE_RANGE, "GL_SMOOTH_POINT_SIZE_RANGE"},
	{GL_LINE_WIDTH, "GL_LINE_WIDTH"},
	{GL_SMOOTH_LINE_WIDTH_RANGE, "GL_SMOOTH_LINE_WIDTH_RANGE"},
	{GL_ALIASED_POINT_SIZE_RANGE, "GL_ALIASED_POINT_SIZE_RANGE"},
	{GL_ALIASED_LINE_WIDTH_RANGE, "GL_ALIASED_LINE_WIDTH_RANGE"},
	{GL_CULL_FACE_MODE, "GL_CULL_FACE_MODE"},
	{GL_FRONT_FACE, "GL_FRONT_FACE"},
	{GL_SHADE_MODEL, "GL_SHADE_MODEL"},
	{GL_DEPTH_RANGE, "GL_DEPTH_RANGE"},
	{GL_DEPTH_WRITEMASK, "GL_DEPTH_WRITEMASK"},
	{GL_DEPTH_CLEAR_VALUE, "GL_DEPTH_CLEAR_VALUE"},
	{GL_DEPTH_FUNC, "GL_DEPTH_FUNC"},
	{GL_STENCIL_CLEAR_VALUE, "GL_STENCIL_CLEAR_VALUE"},
	{GL_STENCIL_FUNC, "GL_STENCIL_FUNC"},
	{GL_STENCIL_VALUE_MASK, "GL_STENCIL_VALUE_MASK"},
	{GL_STENCIL_FAIL, "GL_STENCIL_FAIL"},
	{GL_STENCIL_PASS_DEPTH_FAIL, "GL_STENCIL_PASS_DEPTH_FAIL"},
	{GL_STENCIL_PASS_DEPTH_PASS, "GL_STENCIL_PASS_DEPTH_PASS"},
	{GL_STENCIL_REF, "GL_STENCIL_REF"},
	{GL_STENCIL_WRITEMASK, "GL_STENCIL_WRITEMASK"},
	{GL_MATRIX_MODE, "GL_MATRIX_MODE"},
	{GL_VIEWPORT, "GL_VIEWPORT"},
	{GL_MODELVIEW_STACK_DEPTH, "GL_MODELVIEW_STACK_DEPTH"},
	{GL_PROJECTION_STACK_DEPTH, "GL_PROJECTION_STACK_DEPTH"},
	{GL_TEXTURE_STACK_DEPTH, "GL_TEXTURE_STACK_DEPTH"},
	{GL_MODELVIEW_MATRIX, "GL_MODELVIEW_MATRIX"},
	{GL_PROJECTION_MATRIX, "GL_PROJECTION_MATRIX"},
	{GL_TEXTURE_MATRIX, "GL_TEXTURE_MATRIX"},
	{GL_ALPHA_TEST_FUNC, "GL_ALPHA_TEST_FUNC"},
	{GL_ALPHA_TEST_REF, "GL_ALPHA_TEST_REF"},
	{GL_BLEND_DST, "GL_BLEND_DST"},
	{GL_BLEND_SRC, "GL_BLEND_SRC"},
	{GL_LOGIC_OP_MODE, "GL_LOGIC_OP_MODE"},
	{GL_SCISSOR_BOX, "GL_SCISSOR_BOX"},
	{GL_SCISSOR_TEST, "GL_SCISSOR_TEST"},
	{GL_COLOR_CLEAR_VALUE, "GL_COLOR_CLEAR_VALUE"},
	{GL_COLOR_WRITEMASK, "GL_COLOR_WRITEMASK"},
	{GL_UNPACK_ALIGNMENT, "GL_UNPACK_ALIGNMENT"},
	{GL_PACK_ALIGNMENT, "GL_PACK_ALIGNMENT"},
	{GL_MAX_LIGHTS, "GL_MAX_LIGHTS"},
	{GL_MAX_CLIP_PLANES, "GL_MAX_CLIP_PLANES"},
	{GL_MAX_TEXTURE_SIZE, "GL_MAX_TEXTURE_SIZE"},
	{GL_MAX_MODELVIEW_STACK_DEPTH, "GL_MAX_MODELVIEW_STACK_DEPTH"},
	{GL_MAX_PROJECTION_STACK_DEPTH, "GL_MAX_PROJECTION_STACK_DEPTH"},
	{GL_MAX_TEXTURE_STACK_DEPTH, "GL_MAX_TEXTURE_STACK_DEPTH"},
	{GL_MAX_VIEWPORT_DIMS, "GL_MAX_VIEWPORT_DIMS"},
	{GL_MAX_TEXTURE_UNITS, "GL_MAX_TEXTURE_UNITS"},
	{GL_SUBPIXEL_BITS, "GL_SUBPIXEL_BITS"},
	{GL_RED_BITS, "GL_RED_BITS"},
	{GL_GREEN_BITS, "GL_GREEN_BITS"},
	{GL_BLUE_BITS, "GL_BLUE_BITS"},
	{GL_ALPHA_BITS, "GL_ALPHA_BITS"},
	{GL_DEPTH_BITS, "GL_DEPTH_BITS"},
	{GL_STENCIL_BITS, "GL_STENCIL_BITS"},
	{GL_POLYGON_OFFSET_UNITS, "GL_POLYGON_OFFSET_UNITS"},
	{GL_POLYGON_OFFSET_FILL, "GL_POLYGON_OFFSET_FILL"},
	{GL_POLYGON_OFFSET_FACTOR, "GL_POLYGON_OFFSET_FACTOR"},
	{GL_TEXTURE_BINDING_2D, "GL_TEXTURE_BINDING_2D"},
	{GL_VERTEX_ARRAY_SIZE, "GL_VERTEX_ARRAY_SIZE"},
	{GL_VERTEX_ARRAY_TYPE, "GL_VERTEX_ARRAY_TYPE"},
	{GL_VERTEX_ARRAY_STRIDE, "GL_VERTEX_ARRAY_STRIDE"},
	{GL_NORMAL_ARRAY_TYPE, "GL_NORMAL_ARRAY_TYPE"},
	{GL_NORMAL_ARRAY_STRIDE, "GL_NORMAL_ARRAY_STRIDE"},
	{GL_COLOR_ARRAY_SIZE, "GL_COLOR_ARRAY_SIZE"},
	{GL_COLOR_ARRAY_TYPE, "GL_COLOR_ARRAY_TYPE"},
	{GL_COLOR_ARRAY_STRIDE, "GL_COLOR_ARRAY_STRIDE"},
	{GL_TEXTURE_COORD_ARRAY_SIZE, "GL_TEXTURE_COORD_ARRAY_SIZE"},
	{GL_TEXTURE_COORD_ARRAY_TYPE, "GL_TEXTURE_COORD_ARRAY_TYPE"},
	{GL_TEXTURE_COORD_ARRAY_STRIDE, "GL_TEXTURE_COORD_ARRAY_STRIDE"},
	{GL_VERTEX_ARRAY_POINTER, "GL_VERTEX_ARRAY_POINTER"},
	{GL_NORMAL_ARRAY_POINTER, "GL_NORMAL_ARRAY_POINTER"},
	{GL_COLOR_ARRAY_POINTER, "GL_COLOR_ARRAY_POINTER"},
	{GL_TEXTURE_COORD_ARRAY_POINTER, "GL_TEXTURE_COORD_ARRAY_POINTER"},
	{GL_SAMPLE_BUFFERS, "GL_SAMPLE_BUFFERS"},
	{GL_SAMPLES, "GL_SAMPLES"},
	{GL_SAMPLE_COVERAGE_VALUE, "GL_SAMPLE_COVERAGE_VALUE"},
	{GL_SAMPLE_COVERAGE_INVERT, "GL_SAMPLE_COVERAGE_INVERT"},
	{GL_NUM_COMPRESSED_TEXTURE_FORMATS,
	 "GL_NUM_COMPRESSED_TEXTURE_FORMATS"},
	{GL_COMPRESSED_TEXTURE_FORMATS, "GL_COMPRESSED_TEXTURE_FORMATS"},
	{GL_DONT_CARE, "GL_DONT_CARE"},
	{GL_FASTEST, "GL_FASTEST"},
	{GL_NICEST, "GL_NICEST"},
	{GL_PERSPECTIVE_CORRECTION_HINT, "GL_PERSPECTIVE_CORRECTION_HINT"},
	{GL_POINT_SMOOTH_HINT, "GL_POINT_SMOOTH_HINT"},
	{GL_LINE_SMOOTH_HINT, "GL_LINE_SMOOTH_HINT"},
	{GL_FOG_HINT, "GL_FOG_HINT"},
	{GL_GENERATE_MIPMAP_HINT, "GL_GENERATE_MIPMAP_HINT"},
	{GL_LIGHT_MODEL_AMBIENT, "GL_LIGHT_MODEL_AMBIENT"},
	{GL_LIGHT_MODEL_TWO_SIDE, "GL_LIGHT_MODEL_TWO_SIDE"},
	{GL_AMBIENT, "GL_AMBIENT"},
	{GL_DIFFUSE, "GL_DIFFUSE"},
	{GL_SPECULAR, "GL_SPECULAR"},
	{GL_POSITION, "GL_POSITION"},
	{GL_SPOT_DIRECTION, "GL_SPOT_DIRECTION"},
	{GL_SPOT_EXPONENT, "GL_SPOT_EXPONENT"},
	{GL_SPOT_CUTOFF, "GL_SPOT_CUTOFF"},
	{GL_CONSTANT_ATTENUATION, "GL_CONSTANT_ATTENUATION"},
	{GL_LINEAR_ATTENUATION, "GL_LINEAR_ATTENUATION"},
	{GL_QUADRATIC_ATTENUATION, "GL_QUADRATIC_ATTENUATION"},
	{GL_BYTE, "GL_BYTE"},
	{GL_UNSIGNED_BYTE, "GL_UNSIGNED_BYTE"},
	{GL_SHORT, "GL_SHORT"},
	{GL_UNSIGNED_SHORT, "GL_UNSIGNED_SHORT"},
	{GL_FLOAT, "GL_FLOAT"},
	{GL_FIXED, "GL_FIXED"},
	{GL_CLEAR, "GL_CLEAR"},
	{GL_AND, "GL_AND"},
	{GL_AND_REVERSE, "GL_AND_REVERSE"},
	{GL_COPY, "GL_COPY"},
	{GL_AND_INVERTED, "GL_AND_INVERTED"},
	{GL_NOOP, "GL_NOOP"},
	{GL_XOR, "GL_XOR"},
	{GL_OR, "GL_OR"},
	{GL_NOR, "GL_NOR"},
	{GL_EQUIV, "GL_EQUIV"},
	{GL_INVERT, "GL_INVERT"},
	{GL_OR_REVERSE, "GL_OR_REVERSE"},
	{GL_COPY_INVERTED, "GL_COPY_INVERTED"},
	{GL_OR_INVERTED, "GL_OR_INVERTED"},
	{GL_NAND, "GL_NAND"},
	{GL_SET, "GL_SET"},
	{GL_EMISSION, "GL_EMISSION"},
	{GL_SHININESS, "GL_SHININESS"},
	{GL_AMBIENT_AND_DIFFUSE, "GL_AMBIENT_AND_DIFFUSE"},
	{GL_MODELVIEW, "GL_MODELVIEW"},
	{GL_PROJECTION, "GL_PROJECTION"},
	{GL_TEXTURE, "GL_TEXTURE"},
	{GL_ALPHA, "GL_ALPHA"},
	{GL_RGB, "GL_RGB"},
	{GL_RGBA, "GL_RGBA"},
	{GL_LUMINANCE, "GL_LUMINANCE"},
	{GL_LUMINANCE_ALPHA, "GL_LUMINANCE_ALPHA"},
	{GL_UNPACK_ALIGNMENT, "GL_UNPACK_ALIGNMENT"},
	{GL_PACK_ALIGNMENT, "GL_PACK_ALIGNMENT"},
	{GL_UNSIGNED_SHORT_4_4_4_4, "GL_UNSIGNED_SHORT_4_4_4_4"},
	{GL_UNSIGNED_SHORT_5_5_5_1, "GL_UNSIGNED_SHORT_5_5_5_1"},
	{GL_UNSIGNED_SHORT_5_6_5, "GL_UNSIGNED_SHORT_5_6_5"},
	{GL_FLAT, "GL_FLAT"},
	{GL_SMOOTH, "GL_SMOOTH"},
	{GL_KEEP, "GL_KEEP"},
	{GL_REPLACE, "GL_REPLACE"},
	{GL_INCR, "GL_INCR"},
	{GL_DECR, "GL_DECR"},
	{GL_VENDOR, "GL_VENDOR"},
	{GL_RENDERER, "GL_RENDERER"},
	{GL_VERSION, "GL_VERSION"},
	{GL_EXTENSIONS, "GL_EXTENSIONS"},
	{GL_MODULATE, "GL_MODULATE"},
	{GL_DECAL, "GL_DECAL"},
	{GL_ADD, "GL_ADD"},
	{GL_TEXTURE_ENV_MODE, "GL_TEXTURE_ENV_MODE"},
	{GL_TEXTURE_ENV_COLOR, "GL_TEXTURE_ENV_COLOR"},
	{GL_TEXTURE_ENV, "GL_TEXTURE_ENV"},
	{GL_NEAREST, "GL_NEAREST"},
	{GL_LINEAR, "GL_LINEAR"},
	{GL_NEAREST_MIPMAP_NEAREST, "GL_NEAREST_MIPMAP_NEAREST"},
	{GL_LINEAR_MIPMAP_NEAREST, "GL_LINEAR_MIPMAP_NEAREST"},
	{GL_NEAREST_MIPMAP_LINEAR, "GL_NEAREST_MIPMAP_LINEAR"},
	{GL_LINEAR_MIPMAP_LINEAR, "GL_LINEAR_MIPMAP_LINEAR"},
	{GL_TEXTURE_MAG_FILTER, "GL_TEXTURE_MAG_FILTER"},
	{GL_TEXTURE_MIN_FILTER, "GL_TEXTURE_MIN_FILTER"},
	{GL_TEXTURE_WRAP_S, "GL_TEXTURE_WRAP_S"},
	{GL_TEXTURE_WRAP_T, "GL_TEXTURE_WRAP_T"},
	{GL_GENERATE_MIPMAP, "GL_GENERATE_MIPMAP"},
	{GL_TEXTURE0, "GL_TEXTURE0"},
	{GL_TEXTURE1, "GL_TEXTURE1"},
	{GL_TEXTURE2, "GL_TEXTURE2"},
	{GL_TEXTURE3, "GL_TEXTURE3"},
	{GL_TEXTURE4, "GL_TEXTURE4"},
	{GL_TEXTURE5, "GL_TEXTURE5"},
	{GL_TEXTURE6, "GL_TEXTURE6"},
	{GL_TEXTURE7, "GL_TEXTURE7"},
	{GL_TEXTURE8, "GL_TEXTURE8"},
	{GL_TEXTURE9, "GL_TEXTURE9"},
	{GL_TEXTURE10, "GL_TEXTURE10"},
	{GL_TEXTURE11, "GL_TEXTURE11"},
	{GL_TEXTURE12, "GL_TEXTURE12"},
	{GL_TEXTURE13, "GL_TEXTURE13"},
	{GL_TEXTURE14, "GL_TEXTURE14"},
	{GL_TEXTURE15, "GL_TEXTURE15"},
	{GL_TEXTURE16, "GL_TEXTURE16"},
	{GL_TEXTURE17, "GL_TEXTURE17"},
	{GL_TEXTURE18, "GL_TEXTURE18"},
	{GL_TEXTURE19, "GL_TEXTURE19"},
	{GL_TEXTURE20, "GL_TEXTURE20"},
	{GL_TEXTURE21, "GL_TEXTURE21"},
	{GL_TEXTURE22, "GL_TEXTURE22"},
	{GL_TEXTURE23, "GL_TEXTURE23"},
	{GL_TEXTURE24, "GL_TEXTURE24"},
	{GL_TEXTURE25, "GL_TEXTURE25"},
	{GL_TEXTURE26, "GL_TEXTURE26"},
	{GL_TEXTURE27, "GL_TEXTURE27"},
	{GL_TEXTURE28, "GL_TEXTURE28"},
	{GL_TEXTURE29, "GL_TEXTURE29"},
	{GL_TEXTURE30, "GL_TEXTURE30"},
	{GL_TEXTURE31, "GL_TEXTURE31"},
	{GL_ACTIVE_TEXTURE, "GL_ACTIVE_TEXTURE"},
	{GL_CLIENT_ACTIVE_TEXTURE, "GL_CLIENT_ACTIVE_TEXTURE"},
	{GL_REPEAT, "GL_REPEAT"},
	{GL_CLAMP_TO_EDGE, "GL_CLAMP_TO_EDGE"},
	{GL_LIGHT0, "GL_LIGHT0"},
	{GL_LIGHT1, "GL_LIGHT1"},
	{GL_LIGHT2, "GL_LIGHT2"},
	{GL_LIGHT3, "GL_LIGHT3"},
	{GL_LIGHT4, "GL_LIGHT4"},
	{GL_LIGHT5, "GL_LIGHT5"},
	{GL_LIGHT6, "GL_LIGHT6"},
	{GL_LIGHT7, "GL_LIGHT7"},
	{GL_ARRAY_BUFFER, "GL_ARRAY_BUFFER"},
	{GL_ELEMENT_ARRAY_BUFFER, "GL_ELEMENT_ARRAY_BUFFER"},
	{GL_ARRAY_BUFFER_BINDING, "GL_ARRAY_BUFFER_BINDING"},
	{GL_ELEMENT_ARRAY_BUFFER_BINDING, "GL_ELEMENT_ARRAY_BUFFER_BINDING"},
	{GL_VERTEX_ARRAY_BUFFER_BINDING, "GL_VERTEX_ARRAY_BUFFER_BINDING"},
	{GL_NORMAL_ARRAY_BUFFER_BINDING, "GL_NORMAL_ARRAY_BUFFER_BINDING"},
	{GL_COLOR_ARRAY_BUFFER_BINDING, "GL_COLOR_ARRAY_BUFFER_BINDING"},
	{GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING,
	 "GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING"},
	{GL_STATIC_DRAW, "GL_STATIC_DRAW"},
	{GL_DYNAMIC_DRAW, "GL_DYNAMIC_DRAW"},
	{GL_BUFFER_SIZE, "GL_BUFFER_SIZE"},
	{GL_BUFFER_USAGE, "GL_BUFFER_USAGE"},
	{GL_SUBTRACT, "GL_SUBTRACT"},
	{GL_COMBINE, "GL_COMBINE"},
	{GL_COMBINE_RGB, "GL_COMBINE_RGB"},
	{GL_COMBINE_ALPHA, "GL_COMBINE_ALPHA"},
	{GL_RGB_SCALE, "GL_RGB_SCALE"},
	{GL_ADD_SIGNED, "GL_ADD_SIGNED"},
	{GL_INTERPOLATE, "GL_INTERPOLATE"},
	{GL_CONSTANT, "GL_CONSTANT"},
	{GL_PRIMARY_COLOR, "GL_PRIMARY_COLOR"},
	{GL_PREVIOUS, "GL_PREVIOUS"},
	{GL_OPERAND0_RGB, "GL_OPERAND0_RGB"},
	{GL_OPERAND1_RGB, "GL_OPERAND1_RGB"},
	{GL_OPERAND2_RGB, "GL_OPERAND2_RGB"},
	{GL_OPERAND0_ALPHA, "GL_OPERAND0_ALPHA"},
	{GL_OPERAND1_ALPHA, "GL_OPERAND1_ALPHA"},
	{GL_OPERAND2_ALPHA, "GL_OPERAND2_ALPHA"},
	{GL_ALPHA_SCALE, "GL_ALPHA_SCALE"},
	{GL_SRC0_RGB, "GL_SRC0_RGB"},
	{GL_SRC1_RGB, "GL_SRC1_RGB"},
	{GL_SRC2_RGB, "GL_SRC2_RGB"},
	{GL_SRC0_ALPHA, "GL_SRC0_ALPHA"},
	{GL_SRC1_ALPHA, "GL_SRC1_ALPHA"},
	{GL_SRC2_ALPHA, "GL_SRC2_ALPHA"},
	{GL_DOT3_RGB, "GL_DOT3_RGB"},
	{GL_DOT3_RGBA, "GL_DOT3_RGBA"},
	{GL_IMPLEMENTATION_COLOR_READ_TYPE_OES,
	 "GL_IMPLEMENTATION_COLOR_READ_TYPE_OES"},
	{GL_IMPLEMENTATION_COLOR_READ_FORMAT_OES,
	 "GL_IMPLEMENTATION_COLOR_READ_FORMAT_OES"},
	{GL_PALETTE4_RGB8_OES, "GL_PALETTE4_RGB8_OES"},
	{GL_PALETTE4_RGBA8_OES, "GL_PALETTE4_RGBA8_OES"},
	{GL_PALETTE4_R5_G6_B5_OES, "GL_PALETTE4_R5_G6_B5_OES"},
	{GL_PALETTE4_RGBA4_OES, "GL_PALETTE4_RGBA4_OES"},
	{GL_PALETTE4_RGB5_A1_OES, "GL_PALETTE4_RGB5_A1_OES"},
	{GL_PALETTE8_RGB8_OES, "GL_PALETTE8_RGB8_OES"},
	{GL_PALETTE8_RGBA8_OES, "GL_PALETTE8_RGBA8_OES"},
	{GL_PALETTE8_R5_G6_B5_OES, "GL_PALETTE8_R5_G6_B5_OES"},
	{GL_PALETTE8_RGBA4_OES, "GL_PALETTE8_RGBA4_OES"},
	{GL_PALETTE8_RGB5_A1_OES, "GL_PALETTE8_RGB5_A1_OES"},
	{GL_POINT_SIZE_ARRAY_OES, "GL_POINT_SIZE_ARRAY_OES"},
	{GL_POINT_SIZE_ARRAY_TYPE_OES, "GL_POINT_SIZE_ARRAY_TYPE_OES"},
	{GL_POINT_SIZE_ARRAY_STRIDE_OES, "GL_POINT_SIZE_ARRAY_STRIDE_OES"},
	{GL_POINT_SIZE_ARRAY_POINTER_OES, "GL_POINT_SIZE_ARRAY_POINTER_OES"},
	{GL_POINT_SIZE_ARRAY_BUFFER_BINDING_OES,
	 "GL_POINT_SIZE_ARRAY_BUFFER_BINDING_OES"},
	{GL_POINT_SPRITE_OES, "GL_POINT_SPRITE_OES"},
	{GL_COORD_REPLACE_OES, "GL_COORD_REPLACE_OES"},
	{GL_OES_read_format, "GL_OES_read_format"},
	{GL_OES_compressed_paletted_texture,
	 "GL_OES_compressed_paletted_texture"},
	{GL_OES_point_size_array, "GL_OES_point_size_array"},
	{GL_OES_point_sprite, "GL_OES_point_sprite"},
	{0, NULL},
};

char *
QGLEnumString(GLenum val)
{
	int i;

	for (i = 0; GLEnumStrings[i].name; i++)
		if (GLEnumStrings[i].value == val)
			return GLEnumStrings[i].name;

	return NULL;
}

#endif

void
qglDrawBuffer(GLenum mode)
{
#ifdef QGL_LOG_GL_CALLS
	QGLLogNew();
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

	log_gl("\tglTexCoordPointer(%d, %s, %d, TextureCoordinates_%d_%d);\n",
	       size, QGLEnumString(type), stride, draw_count, current_texture);
#endif
	glTexCoordPointer(size, type, stride, pointer);
}

const unsigned char *color_ptr;

void
qglColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
#ifdef QGL_LOG_GL_CALLS
	color_ptr = pointer;

	log_gl("\tglColorPointer(%d, %s, %d, Colors_%d);\n",
		size, QGLEnumString(type), stride, draw_count);
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

	log_gl("\tglVertexPointer(%d, %s, %d, Vertices_%d);\n",
		size, QGLEnumString(type), stride, draw_count);
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

		fprintf(qgldata, "\tunsigned char Colors_%d[%d][4] = {\n",
			draw_count, count);

		for (i = 0; i < count; i++)
			fprintf(qgldata,
				"\t\t{0x%02X, 0x%02X, 0x%02X, 0x%02X},\n",
				colors[4 * i], colors[4 * i + 1],
				colors[4 * i + 2], colors[4 * i + 3]);

		fprintf(qgldata, "\t};\n");
	}

	if (tex_coords_ptr[0]) {
		const float *coords = tex_coords_ptr[0];

		fprintf(qgldata,
			"\tfloat TextureCoordinates_%d_%d[%d][2] = {\n",
			draw_count, 0, count);
		for (i = 0; i < count; i++)
			fprintf(qgldata, "\t\t{%a, %a},\n",
				coords[2 * i], coords[2 * i + 1]);
		fprintf(qgldata, "\t};\n");
	}

	if (tex_coords_ptr[1]) {
		const float *coords = tex_coords_ptr[1];

		fprintf(qgldata,
			"\tfloat TextureCoordinates_%d_%d[%d][2] = {\n",
			draw_count, 1, count);
		for (i = 0; i < count; i++)
			fprintf(qgldata, "\t\t{%a, %a},\n",
				coords[2 * i], coords[2 * i + 1]);
		fprintf(qgldata, "\t};\n");
	}


	if (vertices_new) {
		const float *vertices = vertices_ptr;
		int i;

		if (vertices_stride) {
			fprintf(qgldata, "\tfloat Vertices_%d[%d][4] = {\n",
				draw_count, count);
			for (i = 0; i < count; i++)
				fprintf(qgldata, "\t\t{%a, %a, %a, %a},\n",
					vertices[4 * i], vertices[4 * i + 1],
					vertices[4 * i + 2],
					vertices[4 * i + 3]);
		} else {
			fprintf(qgldata, "\tfloat Vertices_%d[%d][2] = {\n",
				draw_count, count);
			for (i = 0; i < count; i++)
				fprintf(qgldata, "\t\t{%a, %a},\n",
					vertices[2 * i], vertices[2 * i + 1]);
		}
		fprintf(qgldata, "\t};\n");
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

	fprintf(qgldata, "\tunsigned short Indices_%d[%d] = {\n",
		draw_count, count);
	for (i = 0; i < count; i++) {
		if (!(i % 8))
			fprintf(qgldata, "\t\t0x%04X,", indices[i]);
		else if ((i % 8) == 7)
			fprintf(qgldata, " 0x%04X,\n", indices[i]);
		else
			fprintf(qgldata, " 0x%04X,", indices[i]);
	}

	if ((i % 8))
		fprintf(qgldata, "\n");
	fprintf(qgldata, "\t};\n");

	log_gl("\tglDrawElements(%s, %d, %s, Indices_%d);\n",
	       QGLEnumString(mode), count, QGLEnumString(type), draw_count);
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
	log_gl("\tfloat Matrix_%d[16] = {\n", matrix_count);
	log_gl("\t\t%a, %a, %a, %a,\n", m[0], m[1], m[2], m[3]);
	log_gl("\t\t%a, %a, %a, %a,\n", m[4], m[5], m[6], m[7]);
	log_gl("\t\t%a, %a, %a, %a,\n", m[8], m[9], m[10], m[11]);
	log_gl("\t\t%a, %a, %a, %a,\n", m[12], m[13], m[14], m[15]);
	log_gl("\t};\n");
	log_gl("\tglLoadMatrixf(Matrix_%d);\n", matrix_count);
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

	log_gl("\tunsigned int Texture_%d_%d[%d * %d] = {\n",
	       bound_texture, level, width, height);
	for (i = 0; i < (width * height); i++) {
		if (!(i % 4))
			log_gl("\t\t0x%08X,", texture[i]);
		else if ((i % 4) == 3)
			log_gl(" 0x%08X,\n", texture[i]);
		else
			log_gl(" 0x%08X,", texture[i]);
	}

	if ((i % 4))
		log_gl("\n");
	log_gl("\t};\n");

	log_gl("\tglTexImage2D(%s, %d, %s, %d, %d, %d, %s, %s, "
	       "Texture_%d_%d);\n", QGLEnumString(target), level,
	       QGLEnumString(internalformat), width, height, border,
	       QGLEnumString(format), QGLEnumString(type), bound_texture,
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
	log_gl("\tglTexSubImage2D(%s, %d, %d, %d, %d, %d, %s, %s, %p);\n",
	       QGLEnumString(target), level, xoffset, yoffset, width, height,
	       QGLEnumString(format), QGLEnumString(type), pixels);
	glTexSubImage2D(target, level, xoffset, yoffset, width, height, format,
			type, pixels);
}

void
qglDeleteTextures(GLsizei n, const GLuint *textures)
{
	log_gl("\ttmp = %d;\n", textures[0]);
	log_gl("\tglDeleteTextures(%d, &tmp);\n", n);
	glDeleteTextures(n, textures);
}

void
qglBindTexture(GLenum target, GLuint texture)
{
#ifdef QGL_LOG_GL_CALLS
	bound_texture = texture;
	log_gl("\tglBindTexture(%s, %u);\n", QGLEnumString(target), texture);
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
	log_gl("\tglActiveTexture(%s);\n", QGLEnumString(texture));
#endif
	glActiveTexture(texture);
}

void
qglDisableClientState(GLenum array)
{
	log_gl("\tglDisableClientState(%s);\n", QGLEnumString(array));
	glDisableClientState(array);
}

void
qglAlphaFunc(GLenum func, GLclampf ref)
{
	log_gl("\tglAlphaFunc(%s, %f);\n", QGLEnumString(func), ref);
	glAlphaFunc(func, ref);
}

void
qglClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
	log_gl("\tglClearColor(%f, %f, %f, %f);\n", red, green, blue, alpha);
	glClearColor(red, green, blue, alpha);
}

void
qglClearDepthf(GLclampf depth)
{
	log_gl("\tglClearDepthf(%f);\n", depth);
	glClearDepthf(depth);
}

void
qglClipPlanef(GLenum plane, const GLfloat *equation)
{
	log_gl("\tglClipPlanef(%s, %p);\n", QGLEnumString(plane), equation);
	glClipPlanef(plane, equation);
}

void
qglColor4f (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
	log_gl("\tglColor4f(%f, %f, %f, %f);\n", red, green, blue, alpha);
	glColor4f(red, green, blue, alpha);
}

void
qglDepthRangef(GLclampf zNear, GLclampf zFar)
{
	log_gl("\tglDepthRangef(%f, %f);\n", zNear, zFar);
	glDepthRangef(zNear, zFar);
}

void
qglLineWidth(GLfloat width)
{
	log_gl("\tglLineWidth(%f);\n", width);
	glLineWidth(width);
}

void
qglMaterialf(GLenum face, GLenum pname, GLfloat param)
{
	log_gl("\tglMaterialf(%s, %s, %f);\n", QGLEnumString(face),
	       QGLEnumString(pname), param);
	glMaterialf(face, pname, param);
}

void
qglMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
	log_gl("\tglMultiTexCoord4f(%s, %f, %f, %f, %f);\n",
	       QGLEnumString(target), s, t, r, q);
	glMultiTexCoord4f(target, s, t, r, q);
}

void
qglOrthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top,
	  GLfloat zNear, GLfloat zFar)
{
	log_gl("\tglOrthof(%f, %f, %f, %f, %f, %f);\n",
		left, right, bottom, top, zNear, zFar);
	glOrthof(left, right, bottom, top, zNear, zFar);
}

void
qglPolygonOffset(GLfloat factor, GLfloat units)
{
	log_gl("\tglPolygonOffset(%f, %f);\n", factor, units);
	glPolygonOffset(factor, units);
}

void
qglTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
	log_gl("\tglTexEnvf(%s, %s, %f);\n", QGLEnumString(target),
	       QGLEnumString(pname), param);
	glTexEnvf(target, pname, param);
}

void
qglTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
	log_gl("\tglTranslatef(%f, %f, %f);\n", x, y, z);
	glTranslatef(x, y, z);
}

void
qglAlphaFuncx(GLenum func, GLclampx ref)
{
	log_gl("\tglAlphaFuncx(%s, %d);\n", QGLEnumString(func), ref);
	glAlphaFuncx(func, ref);
}

void
qglBlendFunc(GLenum sfactor, GLenum dfactor)
{
	log_gl("\tglBlendFunc(%s, %s);\n", QGLEnumString(sfactor),
	       QGLEnumString(dfactor));
	glBlendFunc(sfactor, dfactor);
}

void
qglClear(GLbitfield mask)
{
	log_gl("\tglClear(%s);\n", QGLEnumString(mask));
	glClear(mask);
}

void
qglClearStencil(GLint s)
{
	log_gl("\tglClearStencil(%d);\n", s);
	glClearStencil(s);
}

void
qglClientActiveTexture(GLenum texture)
{
	log_gl("\tglClientActiveTexture(%s);\n", QGLEnumString(texture));
	glClientActiveTexture(texture);
}

void
qglColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
	log_gl("\tglColorMask(%u, %u, %u, %u);\n", red, green, blue, alpha);
	glColorMask(red, green, blue, alpha);
}

void
qglCullFace(GLenum mode)
{
	log_gl("\tglCullFace(%s);\n", QGLEnumString(mode));
	glCullFace(mode);
}

void
qglDepthFunc(GLenum func)
{
	log_gl("\tglDepthFunc(%s);\n", QGLEnumString(func));
	glDepthFunc(func);
}

void
qglDepthMask(GLboolean flag)
{
	log_gl("\tglDepthMask(%u);\n", flag);
	glDepthMask(flag);
}

void
qglDisable(GLenum cap)
{
	log_gl("\tglDisable(%s);\n", QGLEnumString(cap));
	glDisable(cap);
}

void
qglDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	log_gl("\tglDrawArrays(%s, %d, %d);\n", QGLEnumString(mode),
	       first, count);
	glDrawArrays(mode, first, count);
}

void
qglEnable(GLenum cap)
{
	log_gl("\tglEnable(%s);\n", QGLEnumString(cap));
	glEnable(cap);
}

void
qglEnableClientState(GLenum array)
{
	log_gl("\tglEnableClientState(%s);\n", QGLEnumString(array));
	glEnableClientState(array);
}

void
qglFinish(void)
{
	log_gl("\tglFinish();\n");
	glFinish();
}

void
qglFlush(void)
{
	log_gl("\tglFlush();\n");
	glFlush();
}

void
qglGetBooleanv(GLenum pname, GLboolean *params)
{
	log_gl("\tglGetBooleanv(%s, %p);\n", QGLEnumString(pname), params);
	glGetBooleanv(pname, params);
}

GLenum
qglGetError(void)
{
	log_gl("\tglGetError();\n");
	return glGetError();
}

void
qglGetIntegerv(GLenum pname, GLint *params)
{
	log_gl("\tglGetIntegerv(%s, %p);\n", QGLEnumString(pname), params);
	glGetIntegerv(pname, params);
}

const GLubyte *
qglGetString(GLenum name)
{
	log_gl("\tglGetString(%s);\n", QGLEnumString(name));
	return glGetString(name);
}

void
qglLoadIdentity(void)
{
	log_gl("\tglLoadIdentity();\n");
	glLoadIdentity();
}

void
qglMatrixMode(GLenum mode)
{
	log_gl("\tglMatrixMode(%s);\n", QGLEnumString(mode));
	glMatrixMode(mode);
}

void
qglPopMatrix(void)
{
	log_gl("\tglPopMatrix();\n");
	glPopMatrix();
}

void
qglPushMatrix(void)
{
	log_gl("\tglPushMatrix();\n");
	glPushMatrix();
}

void
qglReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format,
	      GLenum type, GLvoid *pixels)
{
	log_gl("\tglReadPixels(%d, %d, %d, %d, %s, %s, %p);\n", x, y, width,
	       height, QGLEnumString(format), QGLEnumString(type), pixels);
	glReadPixels(x, y, width, height, format, type, pixels);
}

void
qglScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
	log_gl("\tglScissor(%d, %d, %d, %d);\n", x, y, width, height);
	glScissor(x, y, width, height);
}

void
qglShadeModel(GLenum mode)
{
	log_gl("\tglShadeModel(%s);\n", QGLEnumString(mode));
	glShadeModel(mode);
}

void
qglStencilFunc(GLenum func, GLint ref, GLuint mask)
{
	log_gl("\tglStencilFunc(%s, %d, %s);\n", QGLEnumString(func), ref,
	       QGLEnumString(mask));
	glStencilFunc(func, ref, mask);
}

void
qglStencilMask(GLuint mask)
{
	log_gl("\tglStencilMask(%s);\n", QGLEnumString(mask));
	glStencilMask(mask);
}

void
qglStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
	log_gl("\tglStencilOp(%s, %s, %s);\n", QGLEnumString(fail),
	       QGLEnumString(zfail), QGLEnumString(zpass));
	glStencilOp(fail, zfail, zpass);
}

void
qglTexEnvi(GLenum target, GLenum pname, GLint param)
{
	log_gl("\tglTexEnvi(%s, %s, %d);\n", QGLEnumString(target),
	       QGLEnumString(pname), param);
	glTexEnvi(target, pname, param);
}

void
qglTexParameteri(GLenum target, GLenum pname, GLint param)
{
	log_gl("\tglTexParameteri(%s, %s, %d);\n", QGLEnumString(target),
	       QGLEnumString(pname), param);
	glTexParameteri(target, pname, param);
}

void
qglViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
	log_gl("\tglViewport(%d, %d, %d, %d);\n", x, y, width, height);
	glViewport(x, y, width, height);
}
