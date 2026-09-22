/* Execute the real GLES presentation code with a stateful driver fixture.
   SDL may leave VBO-backed attributes, texture unit 3 and clipping enabled. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <GLES2/gl2.h>
#include "rmt.h"
typedef struct {int unused;} SDL_Renderer;
typedef struct {const char *name;} SDL_RendererInfo;
typedef struct {int x,y,w,h;} SDL_Rect;
static int SDL_GetRendererInfo(SDL_Renderer *r,SDL_RendererInfo *i){(void)r;i->name="opengles2";return 0;}
static int SDL_GetRendererOutputSize(SDL_Renderer *r,int *w,int *h){(void)r;*w=1280;*h=720;return 0;}
static int SDL_RenderFlush(SDL_Renderer *r){(void)r;return 0;}
typedef struct {GLint enabled,size,type,normalized,stride,buffer;void *ptr;} Attr;
typedef struct {GLint program,active,tex[4],viewport[4],array,unpack,blend,scissor;Attr attr[2];} State;
static State state;static int draws,uploads,fail_texture;static uint8_t uploaded[640*480*4];
GLuint glCreateShader(GLenum t){(void)t;return 11;}
void glShaderSource(GLuint s,GLsizei n,const GLchar *const *v,const GLint *l){(void)s;(void)n;(void)v;(void)l;}
void glCompileShader(GLuint s){(void)s;}
void glGetShaderiv(GLuint s,GLenum p,GLint *v){(void)s;(void)p;*v=1;}
void glDeleteShader(GLuint s){(void)s;}
GLuint glCreateProgram(void){return 12;}
void glAttachShader(GLuint p,GLuint s){(void)p;(void)s;}
void glBindAttribLocation(GLuint p,GLuint i,const GLchar *n){(void)p;(void)i;(void)n;}
void glLinkProgram(GLuint p){(void)p;}
void glGetProgramiv(GLuint p,GLenum n,GLint *v){(void)p;(void)n;*v=1;}
void glDeleteProgram(GLuint p){(void)p;}
GLint glGetUniformLocation(GLuint p,const GLchar *n){(void)p;(void)n;return 2;}
void glGetIntegerv(GLenum p,GLint *v){switch(p){case GL_CURRENT_PROGRAM:*v=state.program;break;case GL_ACTIVE_TEXTURE:*v=state.active;break;case GL_TEXTURE_BINDING_2D:*v=state.tex[state.active-GL_TEXTURE0];break;case GL_VIEWPORT:memcpy(v,state.viewport,sizeof(state.viewport));break;case GL_ARRAY_BUFFER_BINDING:*v=state.array;break;case GL_UNPACK_ALIGNMENT:*v=state.unpack;break;default:assert(0);}}
void glGetVertexAttribiv(GLuint i,GLenum p,GLint *v){assert(i<2);Attr *a=&state.attr[i];switch(p){case GL_VERTEX_ATTRIB_ARRAY_ENABLED:*v=a->enabled;break;case GL_VERTEX_ATTRIB_ARRAY_SIZE:*v=a->size;break;case GL_VERTEX_ATTRIB_ARRAY_TYPE:*v=a->type;break;case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:*v=a->normalized;break;case GL_VERTEX_ATTRIB_ARRAY_STRIDE:*v=a->stride;break;case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:*v=a->buffer;break;default:assert(0);}}
void glGetVertexAttribPointerv(GLuint i,GLenum p,void **v){assert(i<2&&p==GL_VERTEX_ATTRIB_ARRAY_POINTER);*v=state.attr[i].ptr;}
void glActiveTexture(GLenum t){state.active=(GLint)t;}
void glBindTexture(GLenum t,GLuint n){assert(t==GL_TEXTURE_2D);state.tex[state.active-GL_TEXTURE0]=(GLint)n;}
void glGenTextures(GLsizei n,GLuint *v){assert(n==1);*v=fail_texture?0:72;}
void glDeleteTextures(GLsizei n,const GLuint *v){(void)n;(void)v;}
void glTexParameteri(GLenum t,GLenum p,GLint v){(void)p;(void)v;assert(t==GL_TEXTURE_2D&&state.tex[0]==72);}
void glPixelStorei(GLenum p,GLint v){assert(p==GL_UNPACK_ALIGNMENT);state.unpack=v;}
void glTexImage2D(GLenum t,GLint l,GLint f,GLsizei w,GLsizei h,GLint b,GLenum fmt,GLenum type,const void *v){(void)l;(void)b;assert(t==GL_TEXTURE_2D&&f==GL_RGBA&&fmt==GL_RGBA&&type==GL_UNSIGNED_BYTE&&w==640&&h==480&&state.unpack==1);memcpy(uploaded,v,sizeof(uploaded));uploads++;}
void glTexSubImage2D(GLenum t,GLint l,GLint x,GLint y,GLsizei w,GLsizei h,GLenum f,GLenum type,const void *v){assert(x==0&&y==0);glTexImage2D(t,l,(GLint)f,w,h,0,f,type,v);}
GLboolean glIsEnabled(GLenum p){return (GLboolean)(p==GL_BLEND?state.blend:p==GL_SCISSOR_TEST?state.scissor:0);}
void glEnable(GLenum p){if(p==GL_BLEND)state.blend=1;else if(p==GL_SCISSOR_TEST)state.scissor=1;else assert(0);}
void glDisable(GLenum p){if(p==GL_BLEND)state.blend=0;else if(p==GL_SCISSOR_TEST)state.scissor=0;else assert(0);}
void glViewport(GLint x,GLint y,GLsizei w,GLsizei h){state.viewport[0]=x;state.viewport[1]=y;state.viewport[2]=w;state.viewport[3]=h;}
void glUseProgram(GLuint p){state.program=(GLint)p;}
void glUniform1i(GLint p,GLint v){(void)p;assert(v==0);}
void glUniform2f(GLint p,GLfloat x,GLfloat y){(void)p;assert(x>0&&y>0);}
void glUniform1f(GLint p,GLfloat v){(void)p;assert(v>=0&&v<=1);}
void glBindBuffer(GLenum p,GLuint b){assert(p==GL_ARRAY_BUFFER);state.array=(GLint)b;}
void glEnableVertexAttribArray(GLuint i){assert(i<2);state.attr[i].enabled=1;}
void glDisableVertexAttribArray(GLuint i){assert(i<2);state.attr[i].enabled=0;}
void glVertexAttribPointer(GLuint i,GLint size,GLenum type,GLboolean norm,GLsizei stride,const void *v){assert(i<2);state.attr[i].size=size;state.attr[i].type=(GLint)type;state.attr[i].normalized=norm;state.attr[i].stride=stride;state.attr[i].buffer=state.array;state.attr[i].ptr=(void *)v;}
void glDrawArrays(GLenum mode,GLint first,GLsizei count){assert(mode==GL_TRIANGLE_STRIP&&first==0&&count==4);assert(!state.array&&!state.blend&&!state.scissor&&state.active==GL_TEXTURE0&&state.tex[0]==72&&state.program==12);assert(state.attr[0].enabled&&state.attr[1].enabled&&state.attr[0].size==2&&state.attr[1].size==2);assert(state.viewport[0]==160&&state.viewport[2]==960&&state.viewport[3]==720);draws++;}
#define KPRESENT_GLES_TEST
#include "../tools/present_gles.inc"
int main(void){
    state=(State){.program=77,.active=GL_TEXTURE3,.tex={101,102,103,104},.viewport={5,9,1280,720},.array=49,.unpack=8,.blend=1,.scissor=1,
        .attr={{1,3,GL_FLOAT,0,28,49,(void *)(uintptr_t)12},{0,4,GL_UNSIGNED_BYTE,1,28,53,(void *)(uintptr_t)24}}};
    State original=state;KPresentGles pass={0};SDL_Renderer renderer={0};SDL_Rect dst={160,0,960,720};
    KImage source={0,0,640,480,640*4+16,NULL};source.pixels=malloc(source.stride*480);assert(source.pixels);
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){uint8_t *p=source.pixels+y*source.stride+x*4;p[0]=(uint8_t)(x%256);p[1]=127;p[2]=(uint8_t)(255-x%256);p[3]=91;}
    for(unsigned frame=0;frame<100;frame++){
        assert(!present_gles_draw(&pass,&renderer,&source,&dst,frame));assert(!memcmp(&state,&original,sizeof(state)));
        for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){const uint8_t *p=source.pixels+y*source.stride+x*4,*q=uploaded+(y*640+x)*4;assert(q[0]==p[0]&&q[1]==p[1]&&q[2]==p[2]&&q[3]==p[3]&&p[3]==91);}
        /* Subsequent cached SDL menu/text draws see exactly their old state. */
        assert(state.program==77&&state.array==49&&state.attr[0].ptr==(void *)(uintptr_t)12&&state.attr[0].enabled&&!state.attr[1].enabled);
    }
    assert(draws==100&&uploads==1);
    source.pixels[0]^=7;assert(!present_gles_draw(&pass,&renderer,&source,&dst,50)&&uploads==2&&uploaded[0]==source.pixels[0]);
    source.pixels[2560]^=1;assert(!present_gles_draw(&pass,&renderer,&source,&dst,50)&&uploads==2);present_gles_clear(&pass);fail_texture=1;
    assert(present_gles_draw(&pass,&renderer,&source,&dst,0)<0&&!memcmp(&state,&original,sizeof(state)));
    present_gles_clear(&pass);free(source.pixels);puts("GLES presentation: BGRA shader input, padded stride, 100 frames / 1 upload, SDL vertex/texture/scissor state and allocation failure restoration: PASS");return 0;
}
