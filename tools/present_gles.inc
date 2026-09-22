/* Own texture storage is GL_RGBA containing authored BGRA bytes. The shader
   explicitly swizzles .bgr; SDL textures and script surfaces remain untouched.
   Stable frames reuse the upload; all modified GL state is still restored. */
#if defined(__SWITCH__) || defined(KPRESENT_GLES_TEST)
#ifndef KPRESENT_GLES_TEST
#include <GLES2/gl2.h>
#endif
typedef struct {
    GLuint program,source,target,fbo;
    unsigned edge_strength; /* 0 keeps the original single-pass filter. */
    int target_w,target_h;
    uint8_t *bgra;
    GLint pos,uv,tex,texel,strength,output_stage;
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
    /* Catmull-Rom's two positive middle weights share one bilinear fetch,
       leaving a 3x3 grid (9 fetches). Four central texels bound ringing.
       This uses source texel centres and highp to retain subpixel accuracy. */
    static const char fragment[]=
        "precision highp float;uniform sampler2D u_tex;uniform vec2 u_texel;uniform float u_strength;uniform int u_output_stage;varying vec2 v_uv;"
        "vec3 px(vec2 p){return texture2D(u_tex,p*u_texel).bgr;}"
        /* Output stage reads an RGBA render target, without the source BGRA
           swizzle. Luma-only detail avoids tint shifts. Smooth gating suppresses
           weak texture/noise; local bounds and 6/255 limit contain halos. */
        "float lum(vec3 c){return dot(c,vec3(.299,.587,.114));}"
        "void main(){if(u_output_stage!=0){"
        "vec3 c=texture2D(u_tex,v_uv).rgb;float l=lum(c),lo=l,hi=l,sum=0.;"
        "for(int y=-1;y<=1;y++){for(int x=-1;x<=1;x++){"
        "float v=lum(texture2D(u_tex,v_uv+vec2(float(x),float(y))*u_texel).rgb);"
        "lo=min(lo,v);hi=max(hi,v);sum+=v;}}"
        "float gate=smoothstep(.02,.16,hi-lo);"
        "float delta=clamp((l-sum/9.)*u_strength*.9*gate,-.0235294,.0235294);"
        "delta=clamp(l+delta,lo,hi)-l;"
        "gl_FragColor=vec4(clamp(c+vec3(delta),0.,1.),1.);return;}"
        "vec2 p=v_uv/u_texel-.5;vec2 base=floor(p);vec2 f=p-base;"
        "vec2 w0=f*(-.5+f*(1.-.5*f));vec2 w1=1.+f*f*(-2.5+1.5*f);"
        "vec2 w2=f*(.5+f*(2.-1.5*f));vec2 w3=f*f*(-.5+.5*f);vec2 wm=w1+w2;"
        "vec2 a=base-.5;vec2 m=base+.5+w2/wm;vec2 z=base+2.5;"
        "vec3 row0=px(vec2(a.x,a.y))*w0.x+px(vec2(m.x,a.y))*wm.x+px(vec2(z.x,a.y))*w3.x;"
        "vec3 row1=px(vec2(a.x,m.y))*w0.x+px(vec2(m.x,m.y))*wm.x+px(vec2(z.x,m.y))*w3.x;"
        "vec3 row2=px(vec2(a.x,z.y))*w0.x+px(vec2(m.x,z.y))*wm.x+px(vec2(z.x,z.y))*w3.x;"
        "vec3 s00=px(base+.5),s10=px(base+vec2(1.5,.5)),s01=px(base+vec2(.5,1.5)),s11=px(base+1.5);"
        "vec3 lo=min(min(s00,s10),min(s01,s11)),hi=max(max(s00,s10),max(s01,s11));"
        "vec3 c=clamp(row0*w0.y+row1*wm.y+row2*w3.y,lo,hi);"
        "if(u_strength>0.){vec3 mn=c,mx=c;vec3 n0=texture2D(u_tex,v_uv+u_texel*vec2(-1.,-1.)).bgr;"
        "vec3 n1=texture2D(u_tex,v_uv+u_texel*vec2(0.,-1.)).bgr;vec3 n2=texture2D(u_tex,v_uv+u_texel*vec2(1.,-1.)).bgr;"
        "vec3 n3=texture2D(u_tex,v_uv+u_texel*vec2(-1.,0.)).bgr;vec3 n4=texture2D(u_tex,v_uv+u_texel*vec2(1.,0.)).bgr;"
        "vec3 n5=texture2D(u_tex,v_uv+u_texel*vec2(-1.,1.)).bgr;vec3 n6=texture2D(u_tex,v_uv+u_texel*vec2(0.,1.)).bgr;"
        "vec3 n7=texture2D(u_tex,v_uv+u_texel*vec2(1.,1.)).bgr;mn=min(mn,min(n0,min(n1,min(n2,min(n3,min(n4,min(n5,min(n6,n7))))))));"
        "mx=max(mx,max(n0,max(n1,max(n2,max(n3,max(n4,max(n5,max(n6,n7))))))));"
        "vec3 amp=clamp(min(mn,vec3(2.)-mx)/max(mx,vec3(.0001)),0.,1.);amp=sqrt(max(amp,vec3(.0001)));"
        "vec3 w=-.2*u_strength*amp;c=clamp((c+w*(n1+n3+n4+n6))/(1.+4.*w),mn,mx); }"
        "gl_FragColor=vec4(c,1.);}";
    GLuint vs=present_gles_shader(GL_VERTEX_SHADER,vertex),fs=present_gles_shader(GL_FRAGMENT_SHADER,fragment);
    if(!vs||!fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return -1;}
    p->program=glCreateProgram();if(!p->program){glDeleteShader(vs);glDeleteShader(fs);return -1;}
    glAttachShader(p->program,vs);glAttachShader(p->program,fs);glBindAttribLocation(p->program,0,"a_pos");glBindAttribLocation(p->program,1,"a_uv");glLinkProgram(p->program);glDeleteShader(vs);glDeleteShader(fs);
    GLint ok=0;glGetProgramiv(p->program,GL_LINK_STATUS,&ok);if(!ok){glDeleteProgram(p->program);p->program=0;return -1;}
    p->pos=0;p->uv=1;p->tex=glGetUniformLocation(p->program,"u_tex");p->texel=glGetUniformLocation(p->program,"u_texel");p->strength=glGetUniformLocation(p->program,"u_strength");
    p->output_stage=glGetUniformLocation(p->program,"u_output_stage");
    p->ready=p->output_stage>=0&&p->tex>=0&&p->texel>=0&&p->strength>=0;return p->ready?0:-1;
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
    if(!p->bgra){p->bgra=malloc(640u*480u*4u);if(!p->bgra)return -1;}
    int changed=!p->source;
    if(!changed){
        if(image->stride==2560)changed=memcmp(p->bgra,image->pixels,640u*480u*4u)!=0;
        else for(unsigned y=0;y<480&&!changed;y++)changed=memcmp(p->bgra+y*2560,image->pixels+y*image->stride,2560)!=0;
    }
    if(changed)for(unsigned y=0;y<480;y++)memcpy(p->bgra+y*2560,image->pixels+y*image->stride,2560);
    GLint program,active,texture0,viewport[4],array,unpack,framebuffer;KPresentAttrib attrib[2];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&framebuffer);glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
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
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,640,480,0,GL_RGBA,GL_UNSIGNED_BYTE,p->bgra);
    }else if(changed){
        glBindTexture(GL_TEXTURE_2D,p->source);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,p->bgra);
    }
    if(p->edge_strength){
        if(!p->target||p->target_w!=vw||p->target_h!=vh){
            if(p->target)glDeleteTextures(1,&p->target);
            p->target=0;p->target_w=p->target_h=0;
            glGenTextures(1,&p->target);
            if(!p->target){failed=1;goto restore;}
            glBindTexture(GL_TEXTURE_2D,p->target);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,vw,vh,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
            if(glGetError()!=GL_NO_ERROR){glDeleteTextures(1,&p->target);p->target=0;failed=1;goto restore;}
            p->target_w=vw;p->target_h=vh;
        }
        if(!p->fbo)glGenFramebuffers(1,&p->fbo);
        if(!p->fbo){failed=1;goto restore;}
        glBindFramebuffer(GL_FRAMEBUFFER,p->fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,p->target,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){failed=1;goto restore;}
    }
    glBindTexture(GL_TEXTURE_2D,p->source);
    glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glViewport(vx,oh-(vy+vh),vw,vh);glUseProgram(p->program);
    if(p->edge_strength)glViewport(0,0,vw,vh);
    glUniform1i(p->output_stage,0);glUniform1i(p->tex,0);glUniform2f(p->texel,1.0f/640.0f,1.0f/480.0f);glUniform1f(p->strength,p->edge_strength?0.f:(float)(strength>100?100:strength)/100.0f);
    static const GLfloat pos[]={-1,-1,1,-1,-1,1,1,1},uv[]={0,1,1,1,0,0,1,0};
    /* SDL may have a VBO bound and cache enabled attributes. Client pointers
       are only valid with ARRAY_BUFFER=0; restore both pointers and enables. */
    glBindBuffer(GL_ARRAY_BUFFER,0);glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,pos);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,0,uv);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    if(p->edge_strength){
        /* Render-target Y is bottom-up, unlike the authored upload. */
        static const GLfloat target_uv[]={0,0,1,0,0,1,1,1};
        glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)framebuffer);
        glViewport(vx,oh-(vy+vh),vw,vh);glBindTexture(GL_TEXTURE_2D,p->target);
        glUniform1i(p->output_stage,1);glUniform2f(p->texel,1.f/vw,1.f/vh);
        glUniform1f(p->strength,(float)(p->edge_strength>100?100:p->edge_strength)/100.f);
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,0,target_uv);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    }
 restore:
    glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)framebuffer);
    present_gles_attrib_restore(0,&attrib[0]);present_gles_attrib_restore(1,&attrib[1]);glBindBuffer(GL_ARRAY_BUFFER,(GLuint)array);
    glPixelStorei(GL_UNPACK_ALIGNMENT,unpack);glUseProgram((GLuint)program);glBindTexture(GL_TEXTURE_2D,(GLuint)texture0);glActiveTexture((GLenum)active);
    glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
    return failed?-1:0;
}
static void present_gles_clear(KPresentGles *p){if(!p)return;if(p->fbo)glDeleteFramebuffers(1,&p->fbo);if(p->target)glDeleteTextures(1,&p->target);if(p->source)glDeleteTextures(1,&p->source);if(p->program)glDeleteProgram(p->program);free(p->bgra);memset(p,0,sizeof(*p));}

#else
typedef struct {unsigned edge_strength;} KPresentGles;
static int present_gles_draw(KPresentGles *p,SDL_Renderer *renderer,const KImage *image,const SDL_Rect *dst,unsigned strength){(void)p;(void)renderer;(void)image;(void)dst;(void)strength;return -1;}
static void present_gles_clear(KPresentGles *p){(void)p;}
#endif
