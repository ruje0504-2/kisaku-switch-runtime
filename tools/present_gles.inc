/* Presentation owns its RGBA texture. SDL textures may use a shader-side
   BGRA swizzle, so sampling their native storage with a custom shader is not
   portable. All modified GL state is restored before returning to SDL. */
#if defined(__SWITCH__) || defined(KPRESENT_GLES_TEST)
#ifndef KPRESENT_GLES_TEST
#include <GLES2/gl2.h>
#endif
typedef struct {
    GLuint program,source;
    uint8_t *rgba;
    GLint pos,uv,tex,texel,strength;
    int ready,disabled;
} KPresentGles;

static GLuint present_gles_shader(GLenum type,const char *source){
    GLuint shader=glCreateShader(type);if(!shader)return 0;
    glShaderSource(shader,1,&source,NULL);glCompileShader(shader);
    GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){glDeleteShader(shader);return 0;}return shader;
}
static int present_gles_init(KPresentGles *p){
    static const char vertex[]="attribute vec2 a_pos;attribute vec2 a_uv;varying vec2 v_uv;void main(){gl_Position=vec4(a_pos,0.0,1.0);v_uv=a_uv;}";
    static const char fragment[]=
        "precision mediump float;uniform sampler2D u_tex;uniform vec2 u_texel;uniform float u_strength;varying vec2 v_uv;"
        "void main(){vec3 c=texture2D(u_tex,v_uv).rgb;vec3 mn=c,mx=c;vec3 n0=texture2D(u_tex,v_uv+u_texel*vec2(-1.,-1.)).rgb;"
        "vec3 n1=texture2D(u_tex,v_uv+u_texel*vec2(0.,-1.)).rgb;vec3 n2=texture2D(u_tex,v_uv+u_texel*vec2(1.,-1.)).rgb;"
        "vec3 n3=texture2D(u_tex,v_uv+u_texel*vec2(-1.,0.)).rgb;vec3 n4=texture2D(u_tex,v_uv+u_texel*vec2(1.,0.)).rgb;"
        "vec3 n5=texture2D(u_tex,v_uv+u_texel*vec2(-1.,1.)).rgb;vec3 n6=texture2D(u_tex,v_uv+u_texel*vec2(0.,1.)).rgb;"
        "vec3 n7=texture2D(u_tex,v_uv+u_texel*vec2(1.,1.)).rgb;mn=min(mn,min(n0,min(n1,min(n2,min(n3,min(n4,min(n5,min(n6,n7))))))));"
        "mx=max(mx,max(n0,max(n1,max(n2,max(n3,max(n4,max(n5,max(n6,n7))))))));"
        "vec3 amp=clamp(min(mn,vec3(2.)-mx)/max(mx,vec3(.0001)),0.,1.);amp=sqrt(max(amp,vec3(.0001)));"
        "vec3 w=-.2*u_strength*amp;vec3 o=(c+w*(n1+n3+n4+n6))/(1.+4.*w);gl_FragColor=vec4(clamp(o,mn,mx),1.);}";
    GLuint vs=present_gles_shader(GL_VERTEX_SHADER,vertex),fs=present_gles_shader(GL_FRAGMENT_SHADER,fragment);
    if(!vs||!fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return -1;}
    p->program=glCreateProgram();if(!p->program){glDeleteShader(vs);glDeleteShader(fs);return -1;}
    glAttachShader(p->program,vs);glAttachShader(p->program,fs);glBindAttribLocation(p->program,0,"a_pos");glBindAttribLocation(p->program,1,"a_uv");glLinkProgram(p->program);glDeleteShader(vs);glDeleteShader(fs);
    GLint ok=0;glGetProgramiv(p->program,GL_LINK_STATUS,&ok);if(!ok){glDeleteProgram(p->program);p->program=0;return -1;}
    p->pos=0;p->uv=1;p->tex=glGetUniformLocation(p->program,"u_tex");p->texel=glGetUniformLocation(p->program,"u_texel");p->strength=glGetUniformLocation(p->program,"u_strength");
    p->ready=p->tex>=0&&p->texel>=0&&p->strength>=0;return p->ready?0:-1;
}
typedef struct {GLint enabled,size,type,normalized,stride,buffer;void *pointer;} KPresentAttrib;
static void present_gles_attrib_save(GLuint index,KPresentAttrib *a){
    glGetVertexAttribiv(index,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&a->enabled);
    glGetVertexAttribiv(index,GL_VERTEX_ATTRIB_ARRAY_SIZE,&a->size);
    glGetVertexAttribiv(index,GL_VERTEX_ATTRIB_ARRAY_TYPE,&a->type);
    glGetVertexAttribiv(index,GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,&a->normalized);
    glGetVertexAttribiv(index,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&a->stride);
    glGetVertexAttribiv(index,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&a->buffer);
    glGetVertexAttribPointerv(index,GL_VERTEX_ATTRIB_ARRAY_POINTER,&a->pointer);
}
static void present_gles_attrib_restore(GLuint index,const KPresentAttrib *a){
    glBindBuffer(GL_ARRAY_BUFFER,(GLuint)a->buffer);
    glVertexAttribPointer(index,a->size,(GLenum)a->type,(GLboolean)a->normalized,a->stride,a->pointer);
    if(a->enabled)glEnableVertexAttribArray(index);else glDisableVertexAttribArray(index);
}
static int present_gles_draw(KPresentGles *p,SDL_Renderer *renderer,const KImage *image,const SDL_Rect *dst,unsigned strength){
    if(p->disabled||!image||!image->pixels||image->width!=640||image->height!=480||image->stride<2560)return -1;
    SDL_RendererInfo info={0};if(SDL_GetRendererInfo(renderer,&info)||!info.name||!strstr(info.name,"opengles2"))return -1;
    int ow=0,oh=0;if(SDL_GetRendererOutputSize(renderer,&ow,&oh)||ow<=0||oh<=0)return -1;
    float sx=(float)ow/1280.0f,sy=(float)oh/720.0f,scale=sx<sy?sx:sy;
    int vx=(int)((ow-1280*scale)*0.5f+dst->x*scale+0.5f),vy=(int)((oh-720*scale)*0.5f+dst->y*scale+0.5f);
    int vw=(int)(dst->w*scale+0.5f),vh=(int)(dst->h*scale+0.5f);if(vw<=0||vh<=0)return -1;
    if(SDL_RenderFlush(renderer))return -1;
    if(!p->ready&&present_gles_init(p)){p->disabled=1;return -1;}
    if(!p->rgba){p->rgba=malloc(640u*480u*4u);if(!p->rgba)return -1;}
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
        const uint8_t *src=image->pixels+y*image->stride+x*4;uint8_t *out=p->rgba+(y*640+x)*4;
        out[0]=src[2];out[1]=src[1];out[2]=src[0];out[3]=255;
    }
    GLint program,active,texture0,viewport[4],array,unpack;KPresentAttrib attrib[2];
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
    glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture0);
    glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&array);glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpack);
    present_gles_attrib_save(0,&attrib[0]);present_gles_attrib_save(1,&attrib[1]);
    GLboolean blend=glIsEnabled(GL_BLEND),scissor=glIsEnabled(GL_SCISSOR_TEST);int failed=0;
    if(!p->source){
        glGenTextures(1,&p->source);
        if(!p->source){failed=1;goto restore;}
        glBindTexture(GL_TEXTURE_2D,p->source);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,640,480,0,GL_RGBA,GL_UNSIGNED_BYTE,p->rgba);
    }else{
        glBindTexture(GL_TEXTURE_2D,p->source);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,p->rgba);
    }
    glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glViewport(vx,oh-(vy+vh),vw,vh);glUseProgram(p->program);
    glUniform1i(p->tex,0);glUniform2f(p->texel,1.0f/640.0f,1.0f/480.0f);glUniform1f(p->strength,(float)(strength>100?100:strength)/100.0f);
    static const GLfloat pos[]={-1,-1,1,-1,-1,1,1,1},uv[]={0,1,1,1,0,0,1,0};
    /* SDL may have a VBO bound and cache enabled attributes. Client pointers
       are only valid with ARRAY_BUFFER=0; restore both pointers and enables. */
    glBindBuffer(GL_ARRAY_BUFFER,0);glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,pos);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,0,uv);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
 restore:
    present_gles_attrib_restore(0,&attrib[0]);present_gles_attrib_restore(1,&attrib[1]);glBindBuffer(GL_ARRAY_BUFFER,(GLuint)array);
    glPixelStorei(GL_UNPACK_ALIGNMENT,unpack);glUseProgram((GLuint)program);glBindTexture(GL_TEXTURE_2D,(GLuint)texture0);glActiveTexture((GLenum)active);
    glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
    return failed?-1:0;
}
static void present_gles_clear(KPresentGles *p){if(!p)return;if(p->source)glDeleteTextures(1,&p->source);if(p->program)glDeleteProgram(p->program);free(p->rgba);memset(p,0,sizeof(*p));}

#else
typedef struct {int unused;} KPresentGles;
static int present_gles_draw(KPresentGles *p,SDL_Renderer *renderer,const KImage *image,const SDL_Rect *dst,unsigned strength){(void)p;(void)renderer;(void)image;(void)dst;(void)strength;return -1;}
static void present_gles_clear(KPresentGles *p){(void)p;}
#endif
