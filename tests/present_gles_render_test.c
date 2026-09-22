/* Real GLES2 pbuffer regression: filtered story followed by a cached,
   VBO-backed UI shader drawing native-resolution font pixels and menu art.
   This tests GL execution; it does not stand in for Switch hardware. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
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
/* Independent scalar 16-tap kernel, rather than the shader's 9-fetch
   bilinear grouping. Validate subpixel phase, channel order and edge clamp. */
static double cubic_kernel(double x){
    x=fabs(x);if(x<1)return 1.+x*x*(1.5*x-2.5);
    if(x<2)return 2.+x*(-4.+x*(2.5-.5*x));return 0;
}
static double source_channel(const KImage *im,int x,int y,unsigned channel){
    if(x<0)x=0;if(y<0)y=0;if(x>639)x=639;if(y>479)y=479;
    return im->pixels[(size_t)y*im->stride+(unsigned)x*4+2-channel];
}
static void test_cubic(KPresentGles *pass,SDL_Renderer *renderer,KImage *source,const SDL_Rect *dst,uint8_t *actual){
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
        uint8_t *p=source->pixels+y*source->stride+x*4;
        p[0]=(uint8_t)(20+(x%17)*12);p[1]=(uint8_t)(30+(y%19)*10);
        p[2]=(uint8_t)((x<320?((x+y)%9<4):((x/2+y/2)%2))?215:35);p[3]=91;
    }
    assert(!present_gles_draw(pass,renderer,source,dst,0));read_frame(actual);
    unsigned max_error=0,changed=0;
    for(unsigned y=0;y<720;y++)for(unsigned x=0;x<960;x++){
        double sx=((double)x+.5)*640/960-.5,sy=((double)y+.5)*480/720-.5;
        int bx=(int)floor(sx),by=(int)floor(sy);double fx=sx-bx,fy=sy-by;
        for(unsigned c=0;c<3;c++){
            double out=0,lo=255,hi=0;
            for(int oy=-1;oy<=2;oy++)for(int ox=-1;ox<=2;ox++)
                out+=source_channel(source,bx+ox,by+oy,c)*cubic_kernel(sx-(bx+ox))*cubic_kernel(sy-(by+oy));
            for(int oy=0;oy<2;oy++)for(int ox=0;ox<2;ox++){
                double v=source_channel(source,bx+ox,by+oy,c);if(v<lo)lo=v;if(v>hi)hi=v;
            }
            if(out<lo)out=lo;if(out>hi)out=hi;
            int ref=(int)floor(out+.5),got=actual[((719-y)*1280+160+x)*4+c];
            unsigned error=(unsigned)abs(got-ref);if(error>max_error)max_error=error;
            double linear=(1-fy)*((1-fx)*source_channel(source,bx,by,c)+fx*source_channel(source,bx+1,by,c))+
                fy*((1-fx)*source_channel(source,bx,by+1,c)+fx*source_channel(source,bx+1,by+1,c));
            changed+=abs(got-(int)floor(linear+.5))>2;
            assert(got>=(int)lo-1&&got<=(int)hi+1);
        }
        assert(actual[((719-y)*1280+160+x)*4+3]==255);
    }
    printf("Bicubic GPU vs independent 16-tap CPU reference: max error %u/255, %u channels differ from bilinear\n",max_error,changed);
    assert(max_error<=2&&changed>10000);
    assert(!present_gles_draw(pass,renderer,source,dst,100));read_frame(actual);
    for(unsigned i=0;i<640*480;i++)assert(source->pixels[i*4+3]==91);
}
static double smooth_gate(double v){
    double t=(v-.02)/.14;if(t<0)t=0;if(t>1)t=1;return t*t*(3-2*t);
}
static double output_luma(const uint8_t *frame,int x,int y){
    if(x<0)x=0;if(x>959)x=959;if(y<0)y=0;if(y>719)y=719;
    const uint8_t *p=frame+((size_t)y*1280+160+x)*4;
    return (.299*p[0]+.587*p[1]+.114*p[2])/255.;
}
static void test_edges(KPresentGles *pass,SDL_Renderer *r,KImage *src,const SDL_Rect *dst,uint8_t *actual,uint8_t *base){
    /* Asymmetric ramps, flat areas, texture and hard edges catch Y/channel
       reversal and check bounded luma changes across the entire output. */
    pass->edge_strength=0;assert(!present_gles_draw(pass,r,src,dst,0));read_frame(base);
    for(unsigned strength=25;strength<=100;strength+=25){
        pass->edge_strength=strength;assert(!present_gles_draw(pass,r,src,dst,100));read_frame(actual);
        unsigned changes=0,max_error=0;
        for(int y=0;y<720;y++)for(int x=0;x<960;x++){
            double l=output_luma(base,x,y),lo=l,hi=l,sum=0;
            for(int j=-1;j<=1;j++)for(int i=-1;i<=1;i++){
                double v=output_luma(base,x+i,y+j);sum+=v;if(v<lo)lo=v;if(v>hi)hi=v;
            }
            double delta=(l-sum/9.)*(strength/100.)*.9*smooth_gate(hi-lo);
            if(delta>6./255)delta=6./255;if(delta< -6./255)delta= -6./255;
            double out=l+delta;if(out<lo)out=lo;if(out>hi)out=hi;delta=out-l;
            size_t at=((size_t)y*1280+160+x)*4;
            for(unsigned c=0;c<3;c++){
                int ref=(int)floor(base[at+c]+delta*255+.5);if(ref<0)ref=0;if(ref>255)ref=255;
                unsigned error=(unsigned)abs(actual[at+c]-ref);if(error>max_error)max_error=error;
                assert(abs(actual[at+c]-base[at+c])<=6);changes+=actual[at+c]!=base[at+c];
            }
            assert(actual[at+3]==255);
        }
        assert(max_error<=1&&changes>10000);
        printf("Output edge pass %u: independent CPU max error %u/255; changed channels %u; bound 6/255: PASS\n",strength,max_error,changes);
    }
    /* Reallocation follows viewport dimensions; old mode remains available. */
    SDL_Rect smaller={320,120,640,480};assert(!present_gles_draw(pass,r,src,&smaller,0));
    assert(pass->target_w==640&&pass->target_h==480);
    assert(!present_gles_draw(pass,r,src,dst,0));assert(pass->target_w==960&&pass->target_h==720);
    pass->edge_strength=0;assert(!present_gles_draw(pass,r,src,dst,0));read_frame(actual);
    assert(!memcmp(actual,base,1280u*720u*4u));
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
        uint8_t *p=src->pixels+y*src->stride+x*4;
        p[0]=p[1]=p[2]=(uint8_t)(119+(x+y)%3);
    }
    assert(!present_gles_draw(pass,r,src,dst,0));read_frame(base);
    pass->edge_strength=100;assert(!present_gles_draw(pass,r,src,dst,0));read_frame(actual);
    assert(!memcmp(actual,base,1280u*720u*4u));
    puts("Weak 2/255 texture unchanged at maximum edge strength: PASS");
}
#include "fsr1_reference.inc"
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
    uint8_t *expected=malloc(1280*720*4),*actual=malloc(1280*720*4);assert(expected&&actual);KPresentGles pass={.edge_strength=55,.fsr1=1,.fsr_strength=80};SDL_Renderer renderer={0};SDL_Rect dst={160,0,960,720};
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
    pass.fsr1=0;pass.edge_strength=0;
    test_cubic(&pass,&renderer,&source,&dst,actual);
    test_fsr(&pass,&renderer,&source,&dst,actual,expected);
    pass.edge_strength=0;
    test_cubic(&pass,&renderer,&source,&dst,actual);
    test_edges(&pass,&renderer,&source,&dst,actual,expected);
    present_gles_clear(&pass);kfont_close(font);free(source.pixels);free(letters.pixels);free(expected);free(actual);glDeleteTextures(1,&ui_texture);glDeleteBuffers(1,&ui_vbo);glDeleteProgram(ui_shader);eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,context);eglDestroySurface(d,surface);eglTerminate(d);return 0;
}
