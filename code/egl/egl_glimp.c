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

void
GLimp_Init(void)
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
static FILE *log_limare_file;
static FILE *log_header_file;
static FILE *log_draws_file;
static FILE *log_textures_file;
static FILE *log_textures_header_file;

static int framecount;
static int draw_count;
static int matrix_count;

void
log_legal_preamble(FILE *log)
{
	fprintf(log, "/*\n");
	fprintf(log, " *\n");
	fprintf(log, " * This file contains data dumped from the game "
		"Quake 3 Arena. This data is\n");
	fprintf(log, " * therefor property of ID software and may under no circumstances be\n");
	fprintf(log, " * distributed.\n");
	fprintf(log, " *\n");
	fprintf(log, " */\n\n");
}

void
log_main_new(void)
{
	char buffer[1024];
	FILE *log = log_main_file;

	if (log) {
		fprintf(log, "};\n");

		fclose(log);
	}

	snprintf(buffer, sizeof(buffer), "frame_%04d.c", framecount);
	log = fopen(buffer, "w");
	if (!log) {
		fprintf(stderr, "Error opening %s: %s\n", buffer,
			strerror(errno));
		exit(1);
	}

	log_legal_preamble(log);

	fprintf(log, "/*\n");
	fprintf(log, " * Top level gl commands for frame %04d.\n", framecount);
	fprintf(log, " */\n\n");

	fprintf(log, "#include <stdlib.h>\n\n");

	fprintf(log, "#include <GLES/gl.h>\n\n");

	fprintf(log, "#include \"texture.h\"\n");
	fprintf(log, "#include \"textures_all.h\"\n");
	fprintf(log, "#include \"frame_%04d_textures.h\"\n\n", framecount);

	fprintf(log, "#include \"draw.h\"\n");
	fprintf(log, "#include \"frame_%04d.h\"\n\n", framecount);

	fprintf(log, "void\n");
	fprintf(log, "frame_%04d_draw(void)\n", framecount);
	fprintf(log, "{\n");

	log_main_file = log;
}

void
log_limare_new(void)
{
	char buffer[1024];
	FILE *log = log_limare_file;

	if (log) {
		fprintf(log, "};\n");

		fclose(log);
	}

	snprintf(buffer, sizeof(buffer), "frame_%04d_limare.c", framecount);
	log = fopen(buffer, "w");
	if (!log) {
		fprintf(stderr, "Error opening %s: %s\n", buffer,
			strerror(errno));
		exit(1);
	}

	log_legal_preamble(log);

	fprintf(log, "/*\n");
	fprintf(log, " * Top level limare commands for frame %04d.\n",
		framecount);
	fprintf(log, " */\n\n");

	fprintf(log, "#include <stdlib.h>\n\n");

	fprintf(log, "#include <GLES/gl.h>\n\n");
	fprintf(log, "#include <limare.h>\n\n");

	fprintf(log, "#include \"texture.h\"\n");
	fprintf(log, "#include \"textures_all.h\"\n");
	fprintf(log, "#include \"frame_%04d_textures.h\"\n\n", framecount);

	fprintf(log, "#include \"draw.h\"\n");
	fprintf(log, "#include \"frame_%04d.h\"\n\n", framecount);

	fprintf(log, "void\n");
	fprintf(log, "frame_%04d_draw(void)\n", framecount);
	fprintf(log, "{\n");

	log_limare_file = log;
}

void
log_header_new(void)
{
	char buffer[1024];
	FILE *log = log_header_file;

	if (log) {
		fprintf(log, "\n");
		fprintf(log, "void frame_%04d_draw(void);\n", framecount - 1);
		fprintf(log, "\n");
		fprintf(log, "#endif /* FRAME_%04d_H */\n",
			framecount - 1);

		fclose(log);
	}

	snprintf(buffer, sizeof(buffer), "frame_%04d.h", framecount);
	log = fopen(buffer, "w");
	if (!log) {
		fprintf(stderr, "Error opening %s: %s\n", buffer,
			strerror(errno));
		exit(1);
	}

	fprintf(log, "/*\n");
	fprintf(log, " * Top level header for frame %04d.\n",
		framecount);
	fprintf(log, " */\n\n");

	fprintf(log, "#ifndef FRAME_%04d_H\n", framecount);
	fprintf(log, "#define FRAME_%04d_H 1\n\n", framecount);

	log_header_file = log;
}

void
log_draws_new(void)
{
	char buffer[1024];
	FILE *log = log_draws_file;

	if (log) {
		fprintf(log, "/* complete. */\n");
		fclose(log);
	}

	snprintf(buffer, sizeof(buffer), "frame_%04d_draws.c", framecount);
	log = fopen(buffer, "w");
	if (!log) {
		fprintf(stderr, "Error opening %s: %s\n", buffer,
			strerror(errno));
		exit(1);
	}

	log_legal_preamble(log);

	fprintf(log, "/*\n");
	fprintf(log, " * Draws data for frame %04d.\n", framecount);
	fprintf(log, " */\n\n");

	fprintf(log, "#include <GLES/gl.h>\n\n");

	fprintf(log, "#include \"draw.h\"\n");

	log_draws_file = log;
}

void
log_textures_new(void)
{
	char buffer[1024];
	FILE *log = log_textures_file;

	if (log) {
		fprintf(log, "/* complete. */\n");

		fclose(log);
	}

	snprintf(buffer, sizeof(buffer), "frame_%04d_textures.c", framecount);
	log = fopen(buffer, "w");
	if (!log) {
		fprintf(stderr, "Error opening %s: %s\n", buffer,
			strerror(errno));
		exit(1);
	}

	log_legal_preamble(log);

	fprintf(log, "/*\n");
	fprintf(log, " * Texture data for frame %04d.\n", framecount);
	fprintf(log, " */\n\n");

	fprintf(log, "#include <GLES/gl.h>\n\n");

	fprintf(log, "#include \"texture.h\"\n");
	fprintf(log, "#include \"frame_%04d_textures.h\"\n\n", framecount);

	log_textures_file = log;
}

void
log_textures_header_new(void)
{
	char buffer[1024];
	FILE *log = log_textures_header_file;

	if (log) {
		fprintf(log, "\n");
		fprintf(log, "#endif /* FRAME_%04d_TEXTURES_H */\n",
			framecount - 1);

		fclose(log);
	}

	snprintf(buffer, sizeof(buffer), "frame_%04d_textures.h", framecount);
	log = fopen(buffer, "w");
	if (!log) {
		fprintf(stderr, "Error opening %s: %s\n", buffer,
			strerror(errno));
		exit(1);
	}

	fprintf(log, "/*\n");
	fprintf(log, " * Texture data header for frame %04d.\n", framecount);
	fprintf(log, " */\n\n");

	fprintf(log, "#ifndef FRAME_%04d_TEXTURES_H\n", framecount);
	fprintf(log, "#define FRAME_%04d_TEXTURES_H 1\n\n", framecount);

	log_textures_header_file = log;
}

void
log_frame_new(void)
{
	printf("Finished dumping replay frame %d\n", framecount);

	framecount++;

	log_main_new();
	log_limare_new();
	log_header_new();
	log_draws_new();
	log_textures_new();
	log_textures_header_new();

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
log_limare(const char *template, ...)
{
	va_list ap;

	va_start(ap, template);
	vfprintf(log_limare_file, template, ap);
	va_end(ap);
}

void
log_all(const char *template, ...)
{
	va_list ap;

	va_start(ap, template);
	vfprintf(log_main_file, template, ap);
	//va_end(ap);

	//va_start(ap, template);
	vfprintf(log_limare_file, template, ap);
	va_end(ap);
}

void
log_header(const char *template, ...)
{
	va_list ap;

	va_start(ap, template);
	vfprintf(log_header_file, template, ap);
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

void
log_textures(const char *template, ...)
{
	va_list ap;

	va_start(ap, template);
	vfprintf(log_textures_file, template, ap);
	va_end(ap);
}

void
log_textures_header(const char *template, ...)
{
	va_list ap;

	va_start(ap, template);
	vfprintf(log_textures_header_file, template, ap);
	va_end(ap);
}
#else
#define log_main(msg, ...)
#define log_limare(msg, ...)
#define log_all(msg, ...)
#define log_header(msg, ...)
#define log_draws(msg, ...)
#define log_textures(msg, ...)
#define log_textures_header(msg, ...)
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

void
qglDrawBuffer(GLenum mode)
{
#ifdef QGL_LOG_GL_CALLS
	log_frame_new();
#endif
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

#ifdef QGL_LOG_GL_CALLS
#include "glenumstring.c"
#endif

#ifdef QGL_LOG_GL_CALLS

/*
 * For tracking textures.
 */
#define TEXTURE_LEVEL_COUNT 12

static int texture_id;

static int texture_dump_start;
static int texture_format;
static struct {
	int width;
	int height;
} texture_levels[TEXTURE_LEVEL_COUNT];
static int texture_level_count;
static int texture_filter_min;
static int texture_filter_mag;
static int texture_wrap_s;
static int texture_wrap_t;

/*
 * For tracking draws.
 */
static int color_active;
static int color_count;
static int color_pitch;
static const unsigned int *color_ptr;

static int texture_current;
static int texture_coord_current;

static int coord0_active;
static int coord0_count;
static int coord0_pitch;
static const unsigned int *coord0_ptr;

static int texture0_active;
static int texture0_id;

static int coord1_active;
static int coord1_count;
static int coord1_pitch;
static const unsigned int *coord1_ptr;

static int texture1_active;
static int texture1_id;

static int vertex_count;
static int vertex_pitch;
static const unsigned int *vertex_ptr;

static void
log_texture_data(int level, int format, int width, int height,
		 const void *pixels)
{
	const unsigned int *data = pixels;
	int size, i;

	if (!texture_dump_start)
		return;

	if (level >= TEXTURE_LEVEL_COUNT) {
		fprintf(stderr, "%s: Too many levels assigned to texture %d\n",
			__func__, texture_id);
		return;
	}

	if (!level)
		texture_format = format;

	texture_levels[level].width = width;
	texture_levels[level].height = height;

	size = width * height;


	log_textures("unsigned int Texture_%d_%d[] = {\n", texture_id, level);
	for (i = 0; i < size; i++) {
		if (!(i % 4))
			log_textures("\t0x%08X,", data[i]);
		else if ((i % 4) == 3)
			log_textures(" 0x%08X,\n", data[i]);
		else
			log_textures(" 0x%08X,", data[i]);
	}
	if ((i % 4))
		log_textures("\n");
	log_textures("};\n");

	level++;
	if (level > texture_level_count)
		texture_level_count = level;

	return;
}

static void
log_texture_parameter(GLenum pname, int value)
{
	if (!texture_dump_start)
		return;

	switch (pname) {
	case GL_TEXTURE_MIN_FILTER:
		texture_filter_min = value;
		return;
	case GL_TEXTURE_MAG_FILTER:
		texture_filter_mag = value;
		return;
	case GL_TEXTURE_WRAP_S:
		texture_wrap_s = value;
		return;
	case GL_TEXTURE_WRAP_T:
		texture_wrap_t = value;
		return;
	default:
		fprintf(stderr, "%s: unknown pname 0x%04X\n", __func__, pname);
		return;
	}
}

static void
log_texture_struct(void)
{
	int i;

	log_textures("struct texture Texture_%d = {\n", texture_id);
	log_textures("\t.id = %d,\n", texture_id);

	for (i = 0; i < texture_level_count; i++) {
		log_textures("\t.levels[%d] = {\n", i);
		log_textures("\t\t.level = %d,\n", i);
		log_textures("\t\t.width = %d,\n", texture_levels[i].width);
		log_textures("\t\t.height = %d,\n", texture_levels[i].height);
		log_textures("\t\t.data = Texture_%d_%d,\n", texture_id, i);
		log_textures("\t},\n");
	}

	log_textures("\t.level_count = %d,\n", texture_level_count);

	log_textures("\t.format = %s,\n", GLEnumString(texture_format));

	log_textures("\t.filter_min = %s,\n", GLEnumString(texture_filter_min));
	log_textures("\t.filter_mag = %s,\n", GLEnumString(texture_filter_mag));
	log_textures("\t.wrap_s = %s,\n", GLEnumString(texture_wrap_s));
	log_textures("\t.wrap_t = %s,\n", GLEnumString(texture_wrap_t));
	log_textures("};\n\n");

	log_textures_header("struct texture Texture_%d;\n", texture_id);
}

static void
log_texture_bind(unsigned int id)
{
	if (texture_format) {
		int i;

		if (id)
			fprintf(stderr, "%s(%d): texture %d was still bound.\n",
				__func__, id, texture_id);

		log_texture_struct();

		log_all("\ttexture_load(&Texture_%d);\n", texture_id);

		texture_id = 0;
		texture_format = 0;
		for (i = 0; i < texture_level_count; i++) {
			texture_levels[i].width = 0;
			texture_levels[i].height = 0;
		}
		texture_level_count = 0;
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

static void
log_texture_delete(unsigned int id)
{
	texture_dump_start = 1;
}

static void
log_enable(GLenum cap)
{
	if (cap == GL_TEXTURE_2D) {
		if (texture_current)
			texture1_active = 1;
		else
			texture0_active = 1;
	} else {
		log_main("\tglEnable(%s);\n", GLEnumString(cap));
		log_limare("\tlimare_enable(state, %s);\n", GLEnumString(cap));
	}
}

static void
log_disable(GLenum cap)
{
	if (cap == GL_TEXTURE_2D) {
		if (texture_current)
			texture1_active = 0;
		else
			texture0_active = 0;
	} else {
		log_main("\tglDisable(%s);\n", GLEnumString(cap));
		log_limare("\tlimare_disable(state, %s);\n", GLEnumString(cap));
	}
}

static void
log_clientstate_disable(GLenum cap)
{
	if (cap == GL_TEXTURE_COORD_ARRAY) {
		if (texture_coord_current) {
			coord1_active = 0;
			coord1_count = 0;
			coord1_pitch = 0;
			coord1_ptr = NULL;
		} else {
			coord0_active = 0;
			coord0_count = 0;
			coord0_pitch = 0;
			coord0_ptr = NULL;
		}
	} else if (cap == GL_COLOR_ARRAY) {
		color_active = 0;
		color_count = 0;
		color_pitch = 0;
		color_ptr = NULL;
	} else if (cap == GL_VERTEX_ARRAY)
		log_main("\tglDisableClientState(GL_VERTEX_ARRAY);\n",
			 __func__);
	else
		fprintf(stderr, "%s: Error: unknown capability: 0x%04X\n",
		       __func__, cap);
}

static void
log_clientstate_enable(GLenum cap)
{
	if (cap == GL_TEXTURE_COORD_ARRAY) {
		if (texture_coord_current)
			coord1_active = 1;
		else
			coord0_active = 1;
	} else if (cap == GL_COLOR_ARRAY)
		color_active = 1;
	else if (cap == GL_VERTEX_ARRAY)
		log_main("\tglEnableClientState(GL_VERTEX_ARRAY);\n", __func__);
	else
		fprintf(stderr, "%s: Error: unknown capability: 0x%04X\n",
			__func__, cap);
}

static void
log_texture_active(GLenum texture)
{
	if (texture > GL_TEXTURE1) {
		printf("Error: %s(%s) not supported\n",
		       __func__, GLEnumString(texture));
		return;
	}
	texture_current = texture - GL_TEXTURE0;
}

static void
log_texture_client_active(GLenum texture)
{
	if (texture > GL_TEXTURE1) {
		printf("Error: %s(%s) not supported\n",
		       __func__, GLEnumString(texture));
		return;
	}
	texture_coord_current = texture - GL_TEXTURE0;
}

static void
log_color_pointer(GLint count, GLenum type, GLsizei stride,
		  const GLvoid *pointer)
{
	if (type != GL_UNSIGNED_BYTE) {
		fprintf(stderr, "%s: Error: unsupported type.\n", __func__);
		return;
	}

	if (color_active) {
		color_count = count;
		color_pitch = stride;
		color_ptr = pointer;
	} else
		fprintf(stderr, "%s: Error: color is not active.\n",
		       __func__);
}

static void
log_texcoord_pointer(GLint count, GLenum type, GLsizei stride,
		     const GLvoid *pointer)
{
	if (type != GL_FLOAT) {
		fprintf(stderr, "%s: Error: unsupported type.\n", __func__);
		return;
	}

	if (texture_coord_current) {
		coord1_count = count;
		coord1_pitch = stride;
		coord1_ptr = pointer;
	} else {
		coord0_count = count;
		coord0_pitch = stride;
		coord0_ptr = pointer;
	}
}

static void
log_vertex_pointer(GLint count, GLenum type, GLsizei stride,
		   const GLvoid *pointer)
{
	if (type != GL_FLOAT) {
		fprintf(stderr, "%s: Error: unsupported type.\n", __func__);
		return;
	}

	vertex_count = count;
	vertex_pitch = stride;
	vertex_ptr = pointer;
}

static void
draw_colors_print(int count)
{
	int i, size;

	log_draws("unsigned int Draw_%d_Colors[] = {\n", draw_count);

	if (color_pitch)
		size = count * color_pitch / 4;
	else
		size = count * color_count / 4;

	for (i = 0; i < size; i++) {
		if (!(i % 4))
			log_draws("\t0x%08X,", color_ptr[i]);
		else if ((i % 4) == 3)
			log_draws(" 0x%08X,\n", color_ptr[i]);
		else
			log_draws(" 0x%08X,", color_ptr[i]);
	}

	if ((i % 4))
		log_draws("\n");
	log_draws("};\n");
}

static void
draw_coord0_print(int count)
{
	int i, size;

	log_draws("unsigned int Draw_%d_Coords_0[] = {\n", draw_count);

	if (coord0_pitch)
		size = count * coord0_pitch / 4;
	else
		size = count * coord0_count;

	for (i = 0; i < size; i++) {
		if (!(i % 4))
			log_draws("\t0x%08X,", coord0_ptr[i]);
		else if ((i % 4) == 3)
			log_draws(" 0x%08X,\n", coord0_ptr[i]);
		else
			log_draws(" 0x%08X,", coord0_ptr[i]);
	}

	if ((i % 4))
		log_draws("\n");
	log_draws("};\n");
}

static void
draw_coord1_print(int count)
{
	int i, size;

	log_draws("unsigned int Draw_%d_Coords_1[] = {\n", draw_count);

	if (coord1_pitch)
		size = count * coord1_pitch / 4;
	else
		size = count * coord1_count;

	for (i = 0; i < size; i++) {
		if (!(i % 4))
			log_draws("\t0x%08X,", coord1_ptr[i]);
		else if ((i % 4) == 3)
			log_draws(" 0x%08X,\n", coord1_ptr[i]);
		else
			log_draws(" 0x%08X,", coord1_ptr[i]);
	}

	if ((i % 4))
		log_draws("\n");
	log_draws("};\n");
}

static void
draw_vertex_print(int count)
{
	int i, size;

	log_draws("unsigned int Draw_%d_Vertices[] = {\n", draw_count);

	if (vertex_pitch)
		size = count * vertex_pitch / 4;
	else
		size = count * vertex_count;

	for (i = 0; i < size; i++) {
		if (!(i % 4))
			log_draws("\t0x%08X,", vertex_ptr[i]);
		else if ((i % 4) == 3)
			log_draws(" 0x%08X,\n", vertex_ptr[i]);
		else
			log_draws(" 0x%08X,", vertex_ptr[i]);
	}

	if ((i % 4))
		log_draws("\n");
	log_draws("};\n");
}

static void
draw_indices_print(const unsigned short *indices, int indices_count)
{
	int i;

	log_draws("unsigned short Draw_%d_Indices[] = {\n", draw_count);

	for (i = 0; i < indices_count; i++) {
		if (!(i % 8))
			log_draws("\t0x%04X,", indices[i]);
		else if ((i % 8) == 7)
			log_draws(" 0x%04X,\n", indices[i]);
		else
			log_draws(" 0x%04X,", indices[i]);
	}

	if ((i % 8))
		log_draws("\n");
	log_draws("};\n");
}

static void
draw_struct_print(int draw_mode, int count, int indices_count)
{
	log_draws("struct draw Draw_%d = {\n", draw_count);
	log_draws("\t.id = %d,\n", draw_count);
	log_draws("\t.mode = %d,\n", draw_mode);
	log_draws("\t.count = %d,\n", count);

	if (color_active) {
		log_draws("\t.color_count = %d,\n", color_count);
		log_draws("\t.color_pitch = %d,\n", color_pitch);
		log_draws("\t.color_type = %s,\n", "GL_UNSIGNED_BYTE");
		log_draws("\t.color_data = Draw_%d_Colors,\n", draw_count);
	}

	if (coord0_active && texture0_active) {
		log_draws("\t.coord0_count = %d,\n", coord0_count);
		log_draws("\t.coord0_pitch = %d,\n", coord0_pitch);
		log_draws("\t.coord0_type = %s,\n", "GL_FLOAT");
		log_draws("\t.coord0_data = Draw_%d_Coords_0,\n", draw_count);
	}

	if (coord1_active && texture1_active) {
		log_draws("\t.coord1_count = %d,\n", coord1_count);
		log_draws("\t.coord1_pitch = %d,\n", coord1_pitch);
		log_draws("\t.coord1_type = %s,\n", "GL_FLOAT");
		log_draws("\t.coord1_data = Draw_%d_Coords_1,\n", draw_count);
	}

	log_draws("\t.vertex_count = %d,\n", vertex_count);
	log_draws("\t.vertex_pitch = %d,\n", vertex_pitch);
	log_draws("\t.vertex_type = %s,\n", "GL_FLOAT");
	log_draws("\t.vertex_data = Draw_%d_Vertices,\n", draw_count);

	log_draws("\t.indices_count = %d,\n", indices_count);
	log_draws("\t.indices = Draw_%d_Indices,\n", draw_count);

	log_draws("};\n\n");

	log_header("struct draw Draw_%d;\n", draw_count);
}

static void
log_elements_draw(GLenum mode, GLsizei indices_count, GLenum type,
		  const unsigned short *indices)
{
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

	if (color_active)
		draw_colors_print(count);

	if (coord0_active && texture0_active)
		draw_coord0_print(count);

	if (coord1_active && texture1_active)
		draw_coord1_print(count);

	if (vertex_ptr)
		draw_vertex_print(count);

	draw_indices_print(indices, indices_count);

	draw_struct_print(mode, count, indices_count);

	if (texture0_active) {
		if (texture1_active)
			log_all("\tdraw_draw(&Draw_%d, &Texture_%d, "
				"&Texture_%d);\n",
				draw_count, texture0_id, texture1_id);
		else
			log_all("\tdraw_draw(&Draw_%d, &Texture_%d, NULL);\n",
				draw_count, texture0_id);
	} else
		log_all("\tdraw_draw(&Draw_%d, NULL, NULL);\n", draw_count);

	draw_count++;
}

#else
#define log_texture_data(a, b, c, d, e)
#define log_texture_parameter(a, b)
#define log_texture_bind(a)
#define log_texture_delete(a)

#define log_enable(a)
#define log_disable(a)
#define log_clientstate_disable(a)
#define log_clientstate_enable(a)
#define log_texture_active(a)
#define log_texture_client_active(a)
#define log_color_pointer(a, b, c, d)
#define log_texcoord_pointer(a, b, c, d)
#define log_vertex_pointer(a, b, c, d)
#define log_elements_draw(a, b, c, d)
#endif /* QGL_LOG_GL_CALLS */

#ifdef QGL_LOG_GL_CALLS
static int matrix_projection;

#define MATRIX_ORTHO_STATE_RESET 0
#define MATRIX_ORTHO_STATE_PROJECTION 1
#define MATRIX_ORTHO_STATE_PROJECTION_IDENTITY 2
#define MATRIX_ORTHO_STATE_PROJECTION_ORTHO 3
#define MATRIX_ORTHO_STATE_MODELVIEW 4
static int matrix_ortho_state;


void
log_matrix_mode(GLenum mode)
{
	log_main("\tglMatrixMode(%s);\n", GLEnumString(mode));

	if (mode == GL_PROJECTION) {
		matrix_projection = 1;

		if (matrix_ortho_state == MATRIX_ORTHO_STATE_RESET)
			matrix_ortho_state = MATRIX_ORTHO_STATE_PROJECTION;
		else {
			log_limare("\t/* %s: Error: check gl code */\n",
				   __func__);
			matrix_ortho_state = MATRIX_ORTHO_STATE_RESET;
		}
	} else {
		matrix_projection = 0;

		if (matrix_ortho_state == MATRIX_ORTHO_STATE_PROJECTION_ORTHO)
			matrix_ortho_state = MATRIX_ORTHO_STATE_MODELVIEW;
		else if (matrix_ortho_state != MATRIX_ORTHO_STATE_RESET) {
			log_limare("\t/* %s: Error: check gl code */\n",
				   __func__);
			matrix_ortho_state = MATRIX_ORTHO_STATE_RESET;
		}
	}
}

void
log_matrix_load_identity(void)
{
	log_main("\tglLoadIdentity();\n");

	if (matrix_projection) {
		if (matrix_ortho_state == MATRIX_ORTHO_STATE_PROJECTION)
			matrix_ortho_state =
				MATRIX_ORTHO_STATE_PROJECTION_IDENTITY;
		else {
			log_limare("\t/* %s: Error: check gl code */\n",
				   __func__);
			matrix_ortho_state = MATRIX_ORTHO_STATE_RESET;
		}
	} else {
		if (matrix_ortho_state != MATRIX_ORTHO_STATE_MODELVIEW)
			log_limare("\t/* %s: Error: check gl code */\n",
				   __func__);
		matrix_ortho_state = MATRIX_ORTHO_STATE_RESET;
	}
}


void
log_matrix_load(const GLfloat *m)
{
	log_all("\tfloat Matrix_%d[16] = {\n", matrix_count);
	log_all("\t\t%a, %a, %a, %a,\n", m[0], m[1], m[2], m[3]);
	log_all("\t\t%a, %a, %a, %a,\n", m[4], m[5], m[6], m[7]);
	log_all("\t\t%a, %a, %a, %a,\n", m[8], m[9], m[10], m[11]);
	log_all("\t\t%a, %a, %a, %a,\n", m[12], m[13], m[14], m[15]);
	log_all("\t};\n");

	log_main("\tglLoadMatrixf(Matrix_%d);\n", matrix_count);

	if (matrix_projection)
		log_limare("\tmatrix_projection_set(Matrix_%d);\n",
			   matrix_count);
	else
		log_limare("\tmatrix_modelview_set(Matrix_%d);\n",
			   matrix_count);

	matrix_ortho_state = MATRIX_ORTHO_STATE_RESET;

	matrix_count++;
}

void
log_matrix_ortho(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top,
		 GLfloat zNear, GLfloat zFar)
{
	log_main("\tglOrthof(%f, %f, %f, %f, %f, %f);\n",
		 left, right, bottom, top, zNear, zFar);
	log_limare("\tmatrix_orthographic_set(%f, %f, %f, %f,\n",
		   left, right, bottom, top);
	log_limare("\t\t\t\t%f, %f);\n", zNear, zFar);

	if (matrix_projection &&
	    (matrix_ortho_state == MATRIX_ORTHO_STATE_PROJECTION_IDENTITY))
		matrix_ortho_state = MATRIX_ORTHO_STATE_PROJECTION_ORTHO;
	else {
		log_limare("\t/* %s: Error: check gl code */\n", __func__);
		matrix_ortho_state = MATRIX_ORTHO_STATE_RESET;
	}
}

#else
#define log_matrix_mode(a)
#define log_matrix_load_identity()
#define log_matrix_load(a)
#define log_matrix_ortho(a, b, c, d, e, f)
#endif /* QGL_LOG_GL_CALLS */



void
qglTexCoordPointer(GLint size, GLenum type, GLsizei stride,
		   const GLvoid *pointer)
{
	log_texcoord_pointer(size, type, stride, pointer);
	glTexCoordPointer(size, type, stride, pointer);
}

void
qglColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	log_color_pointer(size, type, stride, pointer);
	glColorPointer(size, type, stride, pointer);
}

void
qglVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	log_vertex_pointer(size, type, stride, pointer);
	glVertexPointer(size, type, stride, pointer);
}

void
qglDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *ptr)
{
	log_elements_draw(mode, count, type, ptr);
	glDrawElements(mode, count, type, ptr);
}

void
qglLoadMatrixf(const GLfloat *m)
{
	log_matrix_load(m);
	glLoadMatrixf(m);
}

void
qglTexImage2D(GLenum target, GLint level, GLint internalformat,
	      GLsizei width, GLsizei height, GLint border,
	      GLenum format, GLenum type, const GLvoid *pixels)
{
	log_texture_data(level, format, width, height, pixels);
	glTexImage2D(target, level, internalformat, width, height, border,
		     format, type, pixels);
}

void
qglTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
		 GLsizei width, GLsizei height, GLenum format, GLenum type,
		 const GLvoid *pixels)
{
	log_all("\tglTexSubImage2D(%s, %d, %d, %d, %d, %d, %s, %s, %p);\n",
		 GLEnumString(target), level, xoffset, yoffset, width, height,
		 GLEnumString(format), GLEnumString(type), pixels);
	glTexSubImage2D(target, level, xoffset, yoffset, width, height, format,
			type, pixels);
}

void
qglDeleteTextures(GLsizei n, const GLuint *textures)
{
	log_texture_delete(textures[0]);
	glDeleteTextures(n, textures);
}

void
qglBindTexture(GLenum target, GLuint texture)
{
	log_texture_bind(texture);
	glBindTexture(target, texture);
}

void
qglActiveTexture(GLenum texture)
{
	log_texture_active(texture);
	glActiveTexture(texture);
}

void
qglDisableClientState(GLenum array)
{
	log_clientstate_disable(array);
	glDisableClientState(array);
}

void
qglAlphaFunc(GLenum func, GLclampf ref)
{
	log_main("\tglAlphaFunc(%s, %f);\n", GLEnumString(func), ref);
	log_limare("\tlimare_alpha_func(state, %s, %f);\n",
		   GLEnumString(func), ref);

	glAlphaFunc(func, ref);
}

void
qglClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
	log_all("\tglClearColor(%f, %f, %f, %f);\n", red, green, blue, alpha);
	glClearColor(red, green, blue, alpha);
}

void
qglClearDepthf(GLclampf depth)
{
	log_main("\tglClearDepthf(%f);\n", depth);
	log_limare("\tlimare_depth_clear_depth(state, %f);\n", depth);

	glClearDepthf(depth);
}

void
qglClipPlanef(GLenum plane, const GLfloat *equation)
{
	log_all("\tglClipPlanef(%s, %p);\n", GLEnumString(plane), equation);
	glClipPlanef(plane, equation);
}

void
qglColor4f (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
	log_main("\tglColor4f(%f, %f, %f, %f);\n", red, green, blue, alpha);
	log_limare("\t//glColor4f(%f, %f, %f, %f);\n", red, green, blue, alpha);
	glColor4f(red, green, blue, alpha);
}

void
qglDepthRangef(GLclampf zNear, GLclampf zFar)
{
	log_main("\tglDepthRangef(%f, %f);\n", zNear, zFar);
	log_limare("\tlimare_depth(state, %f, %f);\n", zNear, zFar);

	glDepthRangef(zNear, zFar);
}

void
qglLineWidth(GLfloat width)
{
	log_all("\tglLineWidth(%f);\n", width);
	glLineWidth(width);
}

void
qglMaterialf(GLenum face, GLenum pname, GLfloat param)
{
	log_all("\tglMaterialf(%s, %s, %f);\n", GLEnumString(face),
		 GLEnumString(pname), param);
	glMaterialf(face, pname, param);
}

void
qglMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
	log_all("\tglMultiTexCoord4f(%s, %f, %f, %f, %f);\n",
		 GLEnumString(target), s, t, r, q);
	glMultiTexCoord4f(target, s, t, r, q);
}

void
qglOrthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top,
	  GLfloat zNear, GLfloat zFar)
{
	log_matrix_ortho(left, right, bottom, top, zNear, zFar);
	glOrthof(left, right, bottom, top, zNear, zFar);
}

void
qglPolygonOffset(GLfloat factor, GLfloat units)
{
	log_main("\tglPolygonOffset(%f, %f);\n", factor, units);
	log_limare("\tlimare_polygon_offset(state, %f, %f);\n", factor, units);

	glPolygonOffset(factor, units);
}

void
qglTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
	log_main("\tglTexEnvf(%s, %s, %f);\n", GLEnumString(target),
		 GLEnumString(pname), param);
	log_limare("\t//glTexEnvf(%s, %s, %f);\n", GLEnumString(target),
		   GLEnumString(pname), param);

	glTexEnvf(target, pname, param);
}

void
qglTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
	log_all("\tglTranslatef(%f, %f, %f);\n", x, y, z);
	glTranslatef(x, y, z);
}

void
qglAlphaFuncx(GLenum func, GLclampx ref)
{
	log_all("\tglAlphaFuncx(%s, %d);\n", GLEnumString(func), ref);
	glAlphaFuncx(func, ref);
}

void
qglBlendFunc(GLenum sfactor, GLenum dfactor)
{
	log_main("\tglBlendFunc(%s, %s);\n", GLEnumString(sfactor),
		 GLEnumString(dfactor));
	log_limare("\tlimare_blend_func(state, %s, %s);\n",
		   GLEnumString(sfactor), GLEnumString(dfactor));
	glBlendFunc(sfactor, dfactor);
}

void
qglClear(GLbitfield mask)
{
	log_main("\tglClear(%s);\n", GLEnumString(mask));
	if (mask == GL_DEPTH_BUFFER_BIT)
		log_limare("\tlimare_depth_buffer_clear(state);\n");

	glClear(mask);
}

void
qglClearStencil(GLint s)
{
	log_all("\tglClearStencil(%d);\n", s);
	glClearStencil(s);
}

void
qglClientActiveTexture(GLenum texture)
{
	log_texture_client_active(texture);
	glClientActiveTexture(texture);
}

void
qglColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
	log_main("\tglColorMask(%u, %u, %u, %u);\n", red, green, blue, alpha);
	log_limare("\tlimare_color_mask(state, %u, %u, %u, %u);\n",
		   red, green, blue, alpha);

	glColorMask(red, green, blue, alpha);
}

void
qglCullFace(GLenum mode)
{
	log_main("\tglCullFace(%s);\n", GLEnumString(mode));
	log_limare("\tlimare_cullface(state, %s);\n", GLEnumString(mode));

	glCullFace(mode);
}

void
qglDepthFunc(GLenum func)
{
	log_main("\tglDepthFunc(%s);\n", GLEnumString(func));
	log_limare("\tlimare_depth_func(state, %s);\n", GLEnumString(func));

	glDepthFunc(func);
}

void
qglDepthMask(GLboolean flag)
{
	log_main("\tglDepthMask(%u);\n", flag);
	log_limare("\tlimare_depth_mask(state, %u);\n", flag);
	glDepthMask(flag);
}

void
qglDisable(GLenum cap)
{
	log_disable(cap);
	glDisable(cap);
}

void
qglDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	log_all("\tglDrawArrays(%s, %d, %d);\n", GLEnumString(mode),
		 first, count);
	glDrawArrays(mode, first, count);
}

void
qglEnable(GLenum cap)
{
	log_enable(cap);
	glEnable(cap);
}

void
qglEnableClientState(GLenum array)
{
	log_clientstate_enable(array);
	glEnableClientState(array);
}

void
qglFinish(void)
{
	log_main("\tglFinish();\n");
	log_limare("\t//glFinish();\n");
	glFinish();
}

void
qglFlush(void)
{
	log_all("\tglFlush();\n");
	glFlush();
}

void
qglGetBooleanv(GLenum pname, GLboolean *params)
{
	log_all("\tglGetBooleanv(%s, %p);\n", GLEnumString(pname), params);
	glGetBooleanv(pname, params);
}

GLenum
qglGetError(void)
{
	return glGetError();
}

void
qglGetIntegerv(GLenum pname, GLint *params)
{
	log_main("\t//glGetIntegerv(%s, %p);\n", GLEnumString(pname), params);
	log_limare("\t//glGetIntegerv(%s, %p);\n", GLEnumString(pname), params);
	glGetIntegerv(pname, params);
}

void
qglLoadIdentity(void)
{
	log_matrix_load_identity();
	glLoadIdentity();
}

void
qglMatrixMode(GLenum mode)
{
	log_matrix_mode(mode);
	glMatrixMode(mode);
}

void
qglPopMatrix(void)
{
	log_all("\tglPopMatrix();\n");
	glPopMatrix();
}

void
qglPushMatrix(void)
{
	log_all("\tglPushMatrix();\n");
	glPushMatrix();
}

void
qglReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format,
	      GLenum type, GLvoid *pixels)
{
	log_all("\tglReadPixels(%d, %d, %d, %d, %s, %s, %p);\n", x, y, width,
		 height, GLEnumString(format), GLEnumString(type), pixels);
	glReadPixels(x, y, width, height, format, type, pixels);
}

void
qglScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
	log_main("\tglScissor(%d, %d, %d, %d);\n", x, y, width, height);
	log_limare("\tlimare_scissor(state, %d, %d, %d, %d);\n",
		   x, y, width, height);
	glScissor(x, y, width, height);
}

void
qglShadeModel(GLenum mode)
{
	log_main("\tglShadeModel(%s);\n", GLEnumString(mode));
	log_limare("\t//glShadeModel(%s);\n", GLEnumString(mode));

	glShadeModel(mode);
}

void
qglStencilFunc(GLenum func, GLint ref, GLuint mask)
{
	log_all("\tglStencilFunc(%s, %d, %s);\n", GLEnumString(func), ref,
		 GLEnumString(mask));
	glStencilFunc(func, ref, mask);
}

void
qglStencilMask(GLuint mask)
{
	log_all("\tglStencilMask(%s);\n", GLEnumString(mask));
	glStencilMask(mask);
}

void
qglStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
	log_all("\tglStencilOp(%s, %s, %s);\n", GLEnumString(fail),
		 GLEnumString(zfail), GLEnumString(zpass));
	glStencilOp(fail, zfail, zpass);
}

void
qglTexEnvi(GLenum target, GLenum pname, GLint param)
{
	log_all("\tglTexEnvi(%s, %s, %d);\n", GLEnumString(target),
		 GLEnumString(pname), param);
	glTexEnvi(target, pname, param);
}

void
qglTexParameteri(GLenum target, GLenum pname, GLint param)
{
	log_texture_parameter(pname, param);
	glTexParameteri(target, pname, param);
}

void
qglViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
	log_main("\tglViewport(%d, %d, %d, %d);\n", x, y, width, height);
	log_limare("\tlimare_viewport(state, %d, %d, %d, %d);\n",
		   x, y, width, height);

	glViewport(x, y, width, height);
}
