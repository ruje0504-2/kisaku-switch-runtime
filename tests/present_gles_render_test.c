/* Real GLES2 pbuffer regression: filtered story followed by a cached,
   VBO-backed UI shader drawing native-resolution font pixels and menu art.
   This tests GL execution; it does not stand in for Switch hardware. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "font.h"
typedef struct {int unused;} SDL_Renderer;
typedef struct {const char *name;} SDL_RendererInfo;
typedef struct {int x,y,w,h;} SDL_Rect;
static int SDL_GetRendererInfo(SDL_Renderer *r,SDL_RendererInfo *i){(void)r;i->name="opengles2";return 0;}
static int SDL_GetRendererOutputSize(SDL_Renderer *r,int *w,int *h){(void)r;*w=1280;*h=720;return 0;}
static int SDL_RenderFlush(SDL_Renderer *r){(void)r;glFlush();return 0;}
#define KPRESENT_GLES_TEST
#include "../tools/present_gles.inc"
static GLuint ui_shader,ui_vbo,ui_texture;
static void ui_setup(void){
    const char *vs="attribute vec2 a_pos;attribute vec2 a_uv;varying vec2 v_uv;void main(){gl_Position=vec4(a_pos,0.,1.);v_uv=a_uv;}";
    const char *fs="precision mediump float;uniform sampler2D u_tex;varying vec2 v_uv;void main(){gl_FragColor=texture2D(u_tex,v_uv);}";
    GLuint v=present_gles_shader(GL_VERTEX_SHADER,vs),f=present_gles_shader(GL_FRAGMENT_SHADER,fs);assert(v&&f);
    ui_shader=glCreateProgram();glAttachShader(ui_shader,v);glAttachShader(ui_shader,f);glBindAttribLocation(ui_shader,0,"a_pos");glBindAttribLocation(ui_shader,1,"a_uv");glLinkProgram(ui_shader);
    GLint linked=0;glGetProgramiv(ui_shader,GL_LINK_STATUS,&linked);assert(linked);glDeleteShader(v);glDeleteShader(f);glUseProgram(ui_shader);glUniform1i(glGetUniformLocation(ui_shader,"u_tex"),0);
    const GLfloat vertices[]={-1,-1,0,1, 1,-1,1,1, -1,1,0,0, 1,1,1,0};
    glGenBuffers(1,&ui_vbo);glBindBuffer(GL_ARRAY_BUFFER,ui_vbo);glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),NULL);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void *)(uintptr_t)(2*sizeof(float)));glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
    glActiveTexture(GL_TEXTURE0);glGenTextures(1,&ui_texture);glBindTexture(GL_TEXTURE_2D,ui_texture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glViewport(160,0,960,720);glEnable(GL_SCISSOR_TEST);glScissor(160,0,960,720);
    glEnable(GL_BLEND);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
}
static void ui_upload(const KImage *im){
    uint8_t *rgba=malloc((size_t)im->width*im->height*4);assert(rgba);
    for(unsigned y=0;y<im->height;y++)for(unsigned x=0;x<im->width;x++){
        const uint8_t *a=im->pixels+y*im->stride+x*4;uint8_t *b=rgba+((size_t)y*im->width+x)*4;b[0]=a[2];b[1]=a[1];b[2]=a[0];b[3]=a[3];
    }
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,(GLsizei)im->width,(GLsizei)im->height,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);free(rgba);assert(glGetError()==GL_NO_ERROR);
}
static void read_frame(uint8_t *pixels){glFinish();glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,pixels);assert(glGetError()==GL_NO_ERROR);}
int main(int argc,char **argv){
    assert(argc==2);EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);EGLint major,minor;assert(eglInitialize(d,&major,&minor));assert(eglBindAPI(EGL_OPENGL_ES_API));
    EGLint config_attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};EGLConfig config;EGLint n;assert(eglChooseConfig(d,config_attrs,&config,1,&n)&&n==1);
    EGLint surface_attrs[]={EGL_WIDTH,1280,EGL_HEIGHT,720,EGL_NONE},context_attrs[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(d,config,surface_attrs);EGLContext context=eglCreateContext(d,config,EGL_NO_CONTEXT,context_attrs);assert(surface!=EGL_NO_SURFACE&&context!=EGL_NO_CONTEXT&&eglMakeCurrent(d,surface,surface,context));
    printf("Actual GLES renderer: %s\n",glGetString(GL_RENDERER));ui_setup();
    KImage source={0,0,640,480,2560,calloc(640*480,4)},letters={0,0,960,720,3840,calloc(960*720,4)};assert(source.pixels&&letters.pixels);
    for(unsigned i=0;i<640*480;i++){source.pixels[i*4]=24;source.pixels[i*4+1]=60;source.pixels[i*4+2]=208;source.pixels[i*4+3]=255;}
    KFont *font=kfont_open(argv[1],0);assert(font);const uint32_t text[]={0x65e5,0x672c,0x8a9e,0x6587,0x5b57,0x5c65,0x6b74};
    for(unsigned i=0;i<7;i++)assert(!kfont_draw(font,&letters,text[i],48+(int)i*30,610,24,24,0xffffff));
    unsigned ink=0;for(unsigned i=0;i<960*720;i++)ink+=letters.pixels[i*4+3]!=0;assert(ink>200);
    uint8_t *expected=malloc(1280*720*4),*actual=malloc(1280*720*4);assert(expected&&actual);KPresentGles pass={0};SDL_Renderer renderer={0};SDL_Rect dst={160,0,960,720};
    for(unsigned art=0;art<2;art++){
        if(art)for(unsigned y=50;y<550;y++)for(unsigned x=200;x<850;x++){uint8_t *q=letters.pixels+y*letters.stride+x*4;q[0]=(uint8_t)(x%256);q[1]=(uint8_t)(y%256);q[2]=200;q[3]=255;}
        ui_upload(&source);glDrawArrays(GL_TRIANGLE_STRIP,0,4);ui_upload(&letters);glDrawArrays(GL_TRIANGLE_STRIP,0,4);read_frame(expected);
        for(unsigned frame=0;frame<30;frame++){
            /* Leave the UI program, texture and VBO attribute state cached. */
            assert(!present_gles_draw(&pass,&renderer,&source,&dst,0));
            glDrawArrays(GL_TRIANGLE_STRIP,0,4);read_frame(actual);
            unsigned differences=0;for(unsigned y=0;y<720;y++)for(unsigned x=160;x<1120;x++)for(unsigned c=0;c<4;c++)differences+=actual[(y*1280+x)*4+c]!=expected[(y*1280+x)*4+c];
            assert(!differences);
        }
    }
    printf("Native 960x720 Japanese glyphs: %u ink pixels; 60 GLES story+text/menu frames match reference pixels exactly: PASS\n",ink);
    present_gles_clear(&pass);kfont_close(font);free(source.pixels);free(letters.pixels);free(expected);free(actual);glDeleteTextures(1,&ui_texture);glDeleteBuffers(1,&ui_vbo);glDeleteProgram(ui_shader);eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,context);eglDestroySurface(d,surface);eglTerminate(d);return 0;
}
