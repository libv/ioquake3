#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>

#include <EGL/egl.h>
#include <GLES/gl.h>

#include "../sys/sys_local.h"
#include "../qcommon/q_shared.h"
#include "egl_glimp.h"
#include "../client/client.h"
#include "../renderer/tr_local.h"

#define FBDEV_WIDTH 1280
#define FBDEV_HEIGHT 720

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

static void GLimp_HandleError(void)
{
	GLint err = eglGetError();

	fprintf(stderr, "%s: 0x%04x: %s\n", __func__, err,
		GLimp_StringErrors[err]); // Cannot work! -- libv
	assert(0);
}

#define MAX_NUM_CONFIGS 4

/*
 * Create an RGB window.
 * Return the window and context handles.
 */
static void make_window(EGLDisplay eglDisplay, int width, int height,
			EGLSurface * winRet, EGLContext * ctxRet)
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
	const char *ptr = Q_stristr( glConfig.extensions_string, ext );
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
static void GLimp_InitExtensions( void )
{
	if ( !r_allowExtensions->integer )
	{
		ri.Printf( PRINT_ALL, "* IGNORING OPENGL EXTENSIONS *\n" );
		return;
	}

	ri.Printf( PRINT_ALL, "Initializing OpenGL extensions\n" );

	glConfig.textureCompression = TC_NONE;

	// GL_EXT_texture_compression_s3tc
	if ( GLimp_HaveExtension( "GL_ARB_texture_compression" ) &&
	     GLimp_HaveExtension( "GL_EXT_texture_compression_s3tc" ) )
	{
		if ( r_ext_compressed_textures->value )
		{
			glConfig.textureCompression = TC_S3TC_ARB;
			ri.Printf( PRINT_ALL, "...using GL_EXT_texture_compression_s3tc\n" );
		}
		else
		{
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_compression_s3tc\n" );
		}
	}
	else
	{
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_compression_s3tc not found\n" );
	}

	// GL_S3_s3tc ... legacy extension before GL_EXT_texture_compression_s3tc.
	if (glConfig.textureCompression == TC_NONE)
	{
		if ( GLimp_HaveExtension( "GL_S3_s3tc" ) )
		{
			if ( r_ext_compressed_textures->value )
			{
				glConfig.textureCompression = TC_S3TC;
				ri.Printf( PRINT_ALL, "...using GL_S3_s3tc\n" );
			}
			else
			{
				ri.Printf( PRINT_ALL, "...ignoring GL_S3_s3tc\n" );
			}
		}
		else
		{
			ri.Printf( PRINT_ALL, "...GL_S3_s3tc not found\n" );
		}
	}


	// GL_EXT_texture_env_add
	glConfig.textureEnvAddAvailable = qtrue; //qfalse;
#if 0
	if ( GLimp_HaveExtension( "EXT_texture_env_add" ) )
	{
		if ( r_ext_texture_env_add->integer )
		{
			glConfig.textureEnvAddAvailable = qtrue;
			ri.Printf( PRINT_ALL, "...using GL_EXT_texture_env_add\n" );
		}
		else
		{
			glConfig.textureEnvAddAvailable = qfalse;
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_env_add\n" );
		}
	}
	else
	{
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_env_add not found\n" );
	}
#endif

	// GL_ARB_multitexture
	/*
	qglMultiTexCoord2fARB = NULL;
	qglActiveTextureARB = NULL;
	qglClientActiveTextureARB = NULL;
	*/
	//if ( GLimp_HaveExtension( "GL_ARB_multitexture" ) )
	{
		if ( r_ext_multitexture->value )
		{
			qglMultiTexCoord2fARB = qglMultiTexCoord2f;
			qglActiveTextureARB = qglActiveTexture;
			qglClientActiveTextureARB = qglClientActiveTexture;

			if ( qglActiveTextureARB )
			{
				GLint glint = 0;
				qglGetIntegerv( GL_MAX_TEXTURE_UNITS, &glint );
				glConfig.numTextureUnits = (int) glint;
				if ( glConfig.numTextureUnits > 1 )
				{
					ri.Printf( PRINT_ALL, "...using GL_ARB_multitexture\n" );
				}
				else
				{
					qglMultiTexCoord2fARB = NULL;
					qglActiveTextureARB = NULL;
					qglClientActiveTextureARB = NULL;
					ri.Printf( PRINT_ALL, "...not using GL_ARB_multitexture, < 2 texture units\n" );
				}
			}
		}
		else
		{
			ri.Printf( PRINT_ALL, "...ignoring GL_ARB_multitexture\n" );
		}
	}
#if 0
	else
	{
		ri.Printf( PRINT_ALL, "...GL_ARB_multitexture not found\n" );
	}
#endif

#if 0
	// GL_EXT_compiled_vertex_array
	if ( GLimp_HaveExtension( "GL_EXT_compiled_vertex_array" ) )
	{
		if ( r_ext_compiled_vertex_array->value )
		{
			ri.Printf( PRINT_ALL, "...using GL_EXT_compiled_vertex_array\n" );
			qglLockArraysEXT = ( void ( APIENTRY * )( GLint, GLint ) ) SDL_GL_GetProcAddress( "glLockArraysEXT" );
			qglUnlockArraysEXT = ( void ( APIENTRY * )( void ) ) SDL_GL_GetProcAddress( "glUnlockArraysEXT" );
			if (!qglLockArraysEXT || !qglUnlockArraysEXT)
			{
				ri.Error (ERR_FATAL, "bad getprocaddress");
			}
		}
		else
		{
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_compiled_vertex_array\n" );
		}
	}
	else
#endif
	{
		ri.Printf( PRINT_ALL, "...GL_EXT_compiled_vertex_array not found\n" );
	}

	textureFilterAnisotropic = qfalse;
#if 0
	if ( GLimp_HaveExtension( "GL_EXT_texture_filter_anisotropic" ) )
	{
		if ( r_ext_texture_filter_anisotropic->integer ) {
			qglGetIntegerv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, (GLint *)&maxAnisotropy );
			if ( maxAnisotropy <= 0 ) {
				ri.Printf( PRINT_ALL, "...GL_EXT_texture_filter_anisotropic not properly supported!\n" );
				maxAnisotropy = 0;
			}
			else
			{
				ri.Printf( PRINT_ALL, "...using GL_EXT_texture_filter_anisotropic (max: %i)\n", maxAnisotropy );
				textureFilterAnisotropic = qtrue;
			}
		}
		else
		{
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_filter_anisotropic\n" );
		}
	}
	else
#endif
	{
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_filter_anisotropic not found\n" );
	}
}

void GLimp_Init(void)
{
	EGLint major, minor;

	ri.Printf(PRINT_ALL, "Initializing OpenGL subsystem\n");

	bzero(&glConfig, sizeof(glConfig));

	eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (!eglInitialize(eglDisplay, &major, &minor))
		GLimp_HandleError();

	make_window(eglDisplay, FBDEV_WIDTH, FBDEV_HEIGHT, &eglSurface, &eglContext);
	glConfig.isFullscreen = qtrue;
	glConfig.vidWidth = FBDEV_WIDTH;
	glConfig.vidHeight = FBDEV_HEIGHT;

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

	IN_Init( );

	ri.Printf(PRINT_ALL, "------------------\n");
}

void GLimp_LogComment(char *comment)
{
	//fprintf(stderr, "%s: %s\n", __func__, comment);
}

void GLimp_EndFrame(void)
{
	eglSwapBuffers(eglDisplay, eglSurface);
}

void GLimp_Shutdown(void)
{
	IN_Shutdown();

	eglDestroyContext(eglDisplay, eglContext);
	eglDestroySurface(eglDisplay, eglSurface);
	eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglTerminate(eglDisplay);
}

#if 1
void qglCallList(GLuint list)
{
}

void qglDrawBuffer(GLenum mode)
{
}

void qglLockArrays(GLint i, GLsizei size)
{
}

void qglUnlockArrays(void)
{
}
#endif

#if 1
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
#endif
