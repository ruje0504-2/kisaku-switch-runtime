/* Optional GLES2 presentation pass.  SDL owns the context and texture; this
   code only borrows both between SDL_RenderFlush and SDL_GL_UnbindTexture.
   The CPU path remains the fallback for software/Metal renderers and tests. */
#ifdef __SWITCH__
#include <GLES2/gl2.h>
typedef struct {
    GLuint program;
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
static int present_gles_draw(KPresentGles *p,SDL_Renderer *renderer,SDL_Texture *texture,const SDL_Rect *dst,unsigned strength){
    (void)dst;
    if(p->disabled)return -1;
    SDL_RendererInfo info={0};if(SDL_GetRendererInfo(renderer,&info)||!info.name||(!strstr(info.name,"opengles")&&!strstr(info.name,"opengl")))return -1;
    if(!p->ready&&present_gles_init(p)){p->disabled=1;return -1;}
    float texw=0.0f,texh=0.0f;SDL_RenderFlush(renderer);
    GLint old_program=0,old_active=0,old_texture=0,old_viewport[4];
    glGetIntegerv(GL_CURRENT_PROGRAM,&old_program);glGetIntegerv(GL_ACTIVE_TEXTURE,&old_active);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&old_texture);glGetIntegerv(GL_VIEWPORT,old_viewport);
    GLboolean blend=glIsEnabled(GL_BLEND);
    if(SDL_GL_BindTexture(texture,&texw,&texh))return -1;
    int ow=0,oh=0;SDL_GetRendererOutputSize(renderer,&ow,&oh);if(ow<=0||oh<=0){SDL_GL_UnbindTexture(texture);return -1;}
    /* SDL_RenderSetLogicalSize keeps the authored viewport at 1280x720.  A
       direct GLES viewport is physical, so preserve the same aspect-fit
       letterbox on 1920x1080 and other backbuffer sizes. */
    float sx=(float)ow/1280.0f,sy=(float)oh/720.0f,scale=sx<sy?sx:sy;
    int vx=(int)((float)(ow-1280*scale)*0.5f+160.0f*scale+0.5f);
    int vy=(int)((float)(oh-720*scale)*0.5f+0.5f);
    int vw=(int)(960.0f*scale+0.5f),vh=(int)(720.0f*scale+0.5f);
    if(vw<=0||vh<=0){SDL_GL_UnbindTexture(texture);return -1;}
    /* SDL_GL_BindTexture leaves the source texture bound on texture unit 0
       on current SDL, but make that contract explicit before reading and
       changing its sampler state.  The source is the authored 640x480
       surface; the viewport is 960x720 (or aspect-fitted), so a one-source
       texel is 1/640,1/480 rather than one/output-pixel. */
    glActiveTexture(GL_TEXTURE0);
    GLint source_min=GL_LINEAR,source_mag=GL_LINEAR;glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&source_min);glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,&source_mag);
    glDisable(GL_BLEND);glViewport(vx,oh-(vy+vh),vw,vh);glUseProgram(p->program);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glUniform1i(p->tex,0);glUniform2f(p->texel,1.0f/640.0f,1.0f/480.0f);glUniform1f(p->strength,(float)strength/100.0f);
    static const GLfloat pos[]={-1,-1,1,-1,-1,1,1,1},uv[]={0,1,1,1,0,0,1,0};glEnableVertexAttribArray((GLuint)p->pos);glEnableVertexAttribArray((GLuint)p->uv);glVertexAttribPointer((GLuint)p->pos,2,GL_FLOAT,GL_FALSE,0,pos);glVertexAttribPointer((GLuint)p->uv,2,GL_FLOAT,GL_FALSE,0,uv);glDrawArrays(GL_TRIANGLE_STRIP,0,4);glDisableVertexAttribArray((GLuint)p->pos);glDisableVertexAttribArray((GLuint)p->uv);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,source_min);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,source_mag);glUseProgram((GLuint)old_program);glActiveTexture(GL_TEXTURE0);SDL_GL_UnbindTexture(texture);glActiveTexture((GLenum)old_active);glBindTexture(GL_TEXTURE_2D,(GLuint)old_texture);glViewport(old_viewport[0],old_viewport[1],old_viewport[2],old_viewport[3]);if(blend)glEnable(GL_BLEND);return 0;
}
static void present_gles_clear(KPresentGles *p){if(p&&p->program)glDeleteProgram(p->program);if(p)memset(p,0,sizeof(*p));}
#else
typedef struct {int unused;} KPresentGles;
static int present_gles_draw(KPresentGles *p,SDL_Renderer *renderer,SDL_Texture *texture,const SDL_Rect *dst,unsigned strength){(void)p;(void)renderer;(void)texture;(void)dst;(void)strength;return -1;}
static void present_gles_clear(KPresentGles *p){(void)p;}
#endif
