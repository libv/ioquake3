extern void QGLCheckError(const char *message);
extern unsigned int QGLBeginStarted;

// This has to be done to avoid infinite recursion between our glGetError wrapper and QGLCheckError()
static inline GLenum _glGetError(void) {
    return glGetError();
}

void qglAlphaFunc(GLenum func, GLclampf ref);
void qglClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void qglClearDepthf(GLclampf depth);
void qglClipPlanef(GLenum plane, const GLfloat *equation);
void qglColor4f (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void qglDepthRangef(GLclampf zNear, GLclampf zFar);
void qglLineWidth(GLfloat width);
void qglLoadMatrixf(const GLfloat *m);
void qglMaterialf(GLenum face, GLenum pname, GLfloat param);
void qglMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t,
			GLfloat r, GLfloat q);
void qglOrthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top,
	       GLfloat zNear, GLfloat zFar);
void qglPolygonOffset(GLfloat factor, GLfloat units);
void qglTexEnvf(GLenum target, GLenum pname, GLfloat param);
void qglTranslatef(GLfloat x, GLfloat y, GLfloat z);
void qglActiveTexture(GLenum texture);
void qglAlphaFuncx(GLenum func, GLclampx ref);
void qglBindTexture(GLenum target, GLuint texture);
void qglBlendFunc(GLenum sfactor, GLenum dfactor);
void qglClear(GLbitfield mask);
void qglClearStencil(GLint s);
void qglClientActiveTexture(GLenum texture);
void qglColorMask(GLboolean red, GLboolean green, GLboolean blue,
		  GLboolean alpha);
void qglColorPointer(GLint size, GLenum type, GLsizei stride,
		     const GLvoid *pointer);
void qglCullFace(GLenum mode);
void qglDeleteTextures(GLsizei n, const GLuint *textures);
void qglDepthFunc(GLenum func);
void qglDepthMask(GLboolean flag);
void qglDisable(GLenum cap);
void qglDisableClientState(GLenum array);
void qglDrawArrays(GLenum mode, GLint first, GLsizei count);
void qglDrawElements(GLenum mode, GLsizei count, GLenum type,
		     const GLvoid *indices);
void qglEnable(GLenum cap);
void qglEnableClientState(GLenum array);
void qglFinish(void);
void qglFlush(void);
void qglGetBooleanv(GLenum pname, GLboolean *params);
GLenum qglGetError(void);
void qglGetIntegerv(GLenum pname, GLint *params);
const GLubyte * qglGetString(GLenum name);
void qglLoadIdentity(void);
void qglMatrixMode(GLenum mode);
void qglPopMatrix(void);
void qglPushMatrix(void);
void qglReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
		   GLenum format, GLenum type, GLvoid *pixels);
void qglScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void qglShadeModel(GLenum mode);
void qglStencilFunc(GLenum func, GLint ref, GLuint mask);
void qglStencilMask(GLuint mask);
void qglStencilOp(GLenum fail, GLenum zfail, GLenum zpass);
void qglTexCoordPointer(GLint size, GLenum type, GLsizei stride,
			const GLvoid *pointer);
void qglTexEnvi(GLenum target, GLenum pname, GLint param);
void qglTexImage2D(GLenum target, GLint level, GLint internalformat,
		   GLsizei width, GLsizei height, GLint border, GLenum format,
		   GLenum type, const GLvoid *pixels);
void qglTexParameteri(GLenum target, GLenum pname, GLint param);
void qglTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
		      GLsizei width, GLsizei height, GLenum format,
		      GLenum type, const GLvoid *pixels);
void qglVertexPointer(GLint size, GLenum type, GLsizei stride,
		      const GLvoid *pointer);
void qglViewport(GLint x, GLint y, GLsizei width, GLsizei height);

#ifndef USE_REAL_GL_CALLS
// Prevent calls to the 'normal' GL functions
#define glAlphaFunc CALL_THE_QGL_VERSION_OF_glAlphaFunc
#define glClearColor CALL_THE_QGL_VERSION_OF_glClearColor
#define glClearDepthf CALL_THE_QGL_VERSION_OF_glClearDepthf
#define glClipPlanef CALL_THE_QGL_VERSION_OF_glClipPlanef
#define glDepthRangef CALL_THE_QGL_VERSION_OF_glDepthRangef
#define glFogf CALL_THE_QGL_VERSION_OF_glFogf
#define glFogfv CALL_THE_QGL_VERSION_OF_glFogfv
#define glFrustumf CALL_THE_QGL_VERSION_OF_glFrustumf
#define glGetClipPlanef CALL_THE_QGL_VERSION_OF_glGetClipPlanef
#define glGetFloatv CALL_THE_QGL_VERSION_OF_glGetFloatv
#define glGetLightfv CALL_THE_QGL_VERSION_OF_glGetLightfv
#define glGetMaterialfv CALL_THE_QGL_VERSION_OF_glGetMaterialfv
#define glGetTexEnvfv CALL_THE_QGL_VERSION_OF_glGetTexEnvfv
#define glGetTexParameterfv CALL_THE_QGL_VERSION_OF_glGetTexParameterfv
#define glLightModelf CALL_THE_QGL_VERSION_OF_glLightModelf
#define glLightModelfv CALL_THE_QGL_VERSION_OF_glLightModelfv
#define glLightf CALL_THE_QGL_VERSION_OF_glLightf
#define glLightfv CALL_THE_QGL_VERSION_OF_glLightfv
#define glLineWidth CALL_THE_QGL_VERSION_OF_glLineWidth
#define glLoadMatrixf CALL_THE_QGL_VERSION_OF_glLoadMatrixf
#define glMaterialf CALL_THE_QGL_VERSION_OF_glMaterialf
#define glMaterialfv CALL_THE_QGL_VERSION_OF_glMaterialfv
#define glMultMatrixf CALL_THE_QGL_VERSION_OF_glMultMatrixf
#define glMultiTexCoord4f CALL_THE_QGL_VERSION_OF_glMultiTexCoord4f
#define glNormal3f CALL_THE_QGL_VERSION_OF_glNormal3f
#define glOrthof CALL_THE_QGL_VERSION_OF_glOrthof
#define glPointParameterf CALL_THE_QGL_VERSION_OF_glPointParameterf
#define glPointParameterfv CALL_THE_QGL_VERSION_OF_glPointParameterfv
#define glPointSize CALL_THE_QGL_VERSION_OF_glPointSize
#define glPolygonOffset CALL_THE_QGL_VERSION_OF_glPolygonOffset
#define glRotatef CALL_THE_QGL_VERSION_OF_glRotatef
#define glScalef CALL_THE_QGL_VERSION_OF_glScalef
#define glTexEnvf CALL_THE_QGL_VERSION_OF_glTexEnvf
#define glTexEnvfv CALL_THE_QGL_VERSION_OF_glTexEnvfv
#define glTexParameterf CALL_THE_QGL_VERSION_OF_glTexParameterf
#define glTexParameterfv CALL_THE_QGL_VERSION_OF_glTexParameterfv
#define glTranslatef CALL_THE_QGL_VERSION_OF_glTranslatef
#define glActiveTexture CALL_THE_QGL_VERSION_OF_glActiveTexture
#define glAlphaFuncx CALL_THE_QGL_VERSION_OF_glAlphaFuncx
#define glBindBuffer CALL_THE_QGL_VERSION_OF_glBindBuffer
#define glBindTexture CALL_THE_QGL_VERSION_OF_glBindTexture
#define glBlendFunc CALL_THE_QGL_VERSION_OF_glBlendFunc
#define glBufferData CALL_THE_QGL_VERSION_OF_glBufferData
#define glBufferSubData CALL_THE_QGL_VERSION_OF_glBufferSubData
#define glClear CALL_THE_QGL_VERSION_OF_glClear
#define glClearColorx CALL_THE_QGL_VERSION_OF_glClearColorx
#define glClearDepthx CALL_THE_QGL_VERSION_OF_glClearDepthx
#define glClearStencil CALL_THE_QGL_VERSION_OF_glClearStencil
#define glClientActiveTexture CALL_THE_QGL_VERSION_OF_glClientActiveTexture
#define glClipPlanex CALL_THE_QGL_VERSION_OF_glClipPlanex
#define glColor4ub CALL_THE_QGL_VERSION_OF_glColor4ub
#define glColor4x CALL_THE_QGL_VERSION_OF_glColor4x
#define glColorMask CALL_THE_QGL_VERSION_OF_glColorMask
#define glColorPointer CALL_THE_QGL_VERSION_OF_glColorPointer
#define glCompressedTexImage2D CALL_THE_QGL_VERSION_OF_glCompressedTexImage2D
#define glCompressedTexSubImage2D CALL_THE_QGL_VERSION_OF_glCompressedTexSubImage2D
#define glCopyTexImage2D CALL_THE_QGL_VERSION_OF_glCopyTexImage2D
#define glCopyTexSubImage2D CALL_THE_QGL_VERSION_OF_glCopyTexSubImage2D
#define glCullFace CALL_THE_QGL_VERSION_OF_glCullFace
#define glDeleteBuffers CALL_THE_QGL_VERSION_OF_glDeleteBuffers
#define glDeleteTextures CALL_THE_QGL_VERSION_OF_glDeleteTextures
#define glDepthFunc CALL_THE_QGL_VERSION_OF_glDepthFunc
#define glDepthMask CALL_THE_QGL_VERSION_OF_glDepthMask
#define glDepthRangex CALL_THE_QGL_VERSION_OF_glDepthRangex
#define glDisable CALL_THE_QGL_VERSION_OF_glDisable
#define glDisableClientState CALL_THE_QGL_VERSION_OF_glDisableClientState
#define glDrawArrays CALL_THE_QGL_VERSION_OF_glDrawArrays
#define glDrawElements CALL_THE_QGL_VERSION_OF_glDrawElements
#define glEnable CALL_THE_QGL_VERSION_OF_glEnable
#define glEnableClientState CALL_THE_QGL_VERSION_OF_glEnableClientState
#define glFinish CALL_THE_QGL_VERSION_OF_glFinish
#define glFlush CALL_THE_QGL_VERSION_OF_glFlush
#define glFogx CALL_THE_QGL_VERSION_OF_glFogx
#define glFogxv CALL_THE_QGL_VERSION_OF_glFogxv
#define glFrontFace CALL_THE_QGL_VERSION_OF_glFrontFace
#define glFrustumx CALL_THE_QGL_VERSION_OF_glFrustumx
#define glGetBooleanv CALL_THE_QGL_VERSION_OF_glGetBooleanv
#define glGetBufferParameteriv CALL_THE_QGL_VERSION_OF_glGetBufferParameteriv
#define glGetClipPlanex CALL_THE_QGL_VERSION_OF_glGetClipPlanex
#define glGenBuffers CALL_THE_QGL_VERSION_OF_glGenBuffers
#define glGenTextures CALL_THE_QGL_VERSION_OF_glGenTextures
#define glGetError CALL_THE_QGL_VERSION_OF_glGetError
#define glGetFixedv CALL_THE_QGL_VERSION_OF_glGetFixedv
#define glGetIntegerv CALL_THE_QGL_VERSION_OF_glGetIntegerv
#define glGetLightxv CALL_THE_QGL_VERSION_OF_glGetLightxv
#define glGetMaterialxv CALL_THE_QGL_VERSION_OF_glGetMaterialxv
#define glGetPointerv CALL_THE_QGL_VERSION_OF_glGetPointerv
#define glGetString CALL_THE_QGL_VERSION_OF_glGetString
#define glGetTexEnviv CALL_THE_QGL_VERSION_OF_glGetTexEnviv
#define glGetTexEnvxv CALL_THE_QGL_VERSION_OF_glGetTexEnvxv
#define glGetTexParameteriv CALL_THE_QGL_VERSION_OF_glGetTexParameteriv
#define glGetTexParameterxv CALL_THE_QGL_VERSION_OF_glGetTexParameterxv
#define glHint CALL_THE_QGL_VERSION_OF_glHint
#define glIsBuffer CALL_THE_QGL_VERSION_OF_glIsBuffer
#define glIsEnabled CALL_THE_QGL_VERSION_OF_glIsEnabled
#define glIsTexture CALL_THE_QGL_VERSION_OF_glIsTexture
#define glLightModelx CALL_THE_QGL_VERSION_OF_glLightModelx
#define glLightModelxv CALL_THE_QGL_VERSION_OF_glLightModelxv
#define glLightx CALL_THE_QGL_VERSION_OF_glLightx
#define glLightxv CALL_THE_QGL_VERSION_OF_glLightxv
#define glLineWidthx CALL_THE_QGL_VERSION_OF_glLineWidthx
#define glLoadIdentity CALL_THE_QGL_VERSION_OF_glLoadIdentity
#define glLoadMatrixx CALL_THE_QGL_VERSION_OF_glLoadMatrixx
#define glLogicOp CALL_THE_QGL_VERSION_OF_glLogicOp
#define glMaterialx CALL_THE_QGL_VERSION_OF_glMaterialx
#define glMaterialxv CALL_THE_QGL_VERSION_OF_glMaterialxv
#define glMatrixMode CALL_THE_QGL_VERSION_OF_glMatrixMode
#define glMultMatrixx CALL_THE_QGL_VERSION_OF_glMultMatrixx
#define glMultiTexCoord4x CALL_THE_QGL_VERSION_OF_glMultiTexCoord4x
#define glNormal3x CALL_THE_QGL_VERSION_OF_glNormal3x
#define glNormalPointer CALL_THE_QGL_VERSION_OF_glNormalPointer
#define glOrthox CALL_THE_QGL_VERSION_OF_glOrthox
#define glPixelStorei CALL_THE_QGL_VERSION_OF_glPixelStorei
#define glPointParameterx CALL_THE_QGL_VERSION_OF_glPointParameterx
#define glPointParameterxv CALL_THE_QGL_VERSION_OF_glPointParameterxv
#define glPointSizex CALL_THE_QGL_VERSION_OF_glPointSizex
#define glPolygonOffsetx CALL_THE_QGL_VERSION_OF_glPolygonOffsetx
#define glPopMatrix CALL_THE_QGL_VERSION_OF_glPopMatrix
#define glPushMatrix CALL_THE_QGL_VERSION_OF_glPushMatrix
#define glReadPixels CALL_THE_QGL_VERSION_OF_glReadPixels
#define glRotatex CALL_THE_QGL_VERSION_OF_glRotatex
#define glSampleCoverage CALL_THE_QGL_VERSION_OF_glSampleCoverage
#define glSampleCoveragex CALL_THE_QGL_VERSION_OF_glSampleCoveragex
#define glScalex CALL_THE_QGL_VERSION_OF_glScalex
#define glScissor CALL_THE_QGL_VERSION_OF_glScissor
#define glShadeModel CALL_THE_QGL_VERSION_OF_glShadeModel
#define glStencilFunc CALL_THE_QGL_VERSION_OF_glStencilFunc
#define glStencilMask CALL_THE_QGL_VERSION_OF_glStencilMask
#define glStencilOp CALL_THE_QGL_VERSION_OF_glStencilOp
#define glTexCoordPointer CALL_THE_QGL_VERSION_OF_glTexCoordPointer
#define glTexEnvi CALL_THE_QGL_VERSION_OF_glTexEnvi
#define glTexEnvx CALL_THE_QGL_VERSION_OF_glTexEnvx
#define glTexEnviv CALL_THE_QGL_VERSION_OF_glTexEnviv
#define glTexEnvxv CALL_THE_QGL_VERSION_OF_glTexEnvxv
#define glTexImage2D CALL_THE_QGL_VERSION_OF_glTexImage2D
#define glTexParameteri CALL_THE_QGL_VERSION_OF_glTexParameteri
#define glTexParameterx CALL_THE_QGL_VERSION_OF_glTexParameterx
#define glTexParameteriv CALL_THE_QGL_VERSION_OF_glTexParameteriv
#define glTexParameterxv CALL_THE_QGL_VERSION_OF_glTexParameterxv
#define glTexSubImage2D CALL_THE_QGL_VERSION_OF_glTexSubImage2D
#define glTranslatex CALL_THE_QGL_VERSION_OF_glTranslatex
#define glVertexPointer CALL_THE_QGL_VERSION_OF_glVertexPointer
#define glViewport CALL_THE_QGL_VERSION_OF_glViewport
#define glPointSizePointerOES CALL_THE_QGL_VERSION_OF_glPointSizePointerOES
#endif
