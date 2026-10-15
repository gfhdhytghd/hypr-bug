#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <stdio.h>
#include <stdlib.h>
static void fail(const char *s) { fprintf(stderr,"%s: EGL=%x GL=%x\n",s,eglGetError(),glGetError()); exit(1); }
int main(int argc,char **argv) {
 if(argc!=2)return 2;
 FILE *f=fopen(argv[1],"rb");if(!f)fail("open shader");fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);char *src=calloc(n+1,1);if(fread(src,1,n,f)!=(size_t)n)fail("read");fclose(f);
 EGLDisplay d=eglGetPlatformDisplay(0x31DD,EGL_DEFAULT_DISPLAY,NULL);EGLint maj,min;if(!eglInitialize(d,&maj,&min))fail("initialize");if(!eglBindAPI(EGL_OPENGL_ES_API))fail("bind API");
 EGLint attrs[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_NONE};EGLConfig cfg;EGLint count;if(!eglChooseConfig(d,attrs,&cfg,1,&count)||!count)fail("choose config");
 EGLint ctxattrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};EGLContext ctx=eglCreateContext(d,cfg,EGL_NO_CONTEXT,ctxattrs);if(ctx==EGL_NO_CONTEXT)fail("create context");if(!eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,ctx))fail("make current");
 fprintf(stderr,"renderer=%s; version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
 GLuint s=glCreateShader(GL_COMPUTE_SHADER);const char *sp=src;glShaderSource(s,1,&sp,NULL);glCompileShader(s);GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);char log[16384];if(!ok){glGetShaderInfoLog(s,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);fail("compile");}
 GLuint prog=glCreateProgram();glAttachShader(prog,s);glLinkProgram(prog);glGetProgramiv(prog,GL_LINK_STATUS,&ok);if(!ok){glGetProgramInfoLog(prog,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);fail("link");}
 GLuint b;glGenBuffers(1,&b);glBindBuffer(GL_SHADER_STORAGE_BUFFER,b);glBufferData(GL_SHADER_STORAGE_BUFFER,6*sizeof(float),NULL,GL_DYNAMIC_READ);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,b);glUseProgram(prog);glDispatchCompute(1,1,1);glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_BUFFER_UPDATE_BARRIER_BIT);float *out=glMapBufferRange(GL_SHADER_STORAGE_BUFFER,0,6*sizeof(float),GL_MAP_READ_BIT);if(!out)fail("map");printf("[");for(int i=0;i<6;i++)printf("%s%.9g",i?",":"",out[i]);printf("]\n");glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);if(glGetError()!=GL_NO_ERROR)fail("GL result");
 glDeleteBuffers(1,&b);glDeleteProgram(prog);glDeleteShader(s);free(src);eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,ctx);eglTerminate(d);return 0;
}
