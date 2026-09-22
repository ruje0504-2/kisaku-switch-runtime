/* Scalar double precision reference adapted from AMD FSR1 v1.20210629.
   Copyright (c) 2021 Advanced Micro Devices, Inc. MIT, see THIRD_PARTY.md.
   Direct 4x4 sampling and indexed crosses deliberately differ from the GLSL
   named-vector implementation. Reference uses exact reciprocal/sqrt too. */
static double fsr_clamp(double v,double lo,double hi){return fmin(hi,fmax(lo,v));}
static void fsr_easu_reference(const KImage *src,int x,int y,double out[3]){
    double px=(x+.5)*640./960.-.5,py=(y+.5)*480./720.-.5;
    int bx=(int)floor(px),by=(int)floor(py);double fx=px-bx,fy=py-by;
    double rgb[4][4][3],lum[4][4];
    for(int j=0;j<4;j++)for(int i=0;i<4;i++){
        for(int c=0;c<3;c++)rgb[j][i][c]=source_channel(src,bx+i-1,by+j-1,(unsigned)c)/255.;
        lum[j][i]=rgb[j][i][1]+.5*(rgb[j][i][0]+rgb[j][i][2]);
    }
    double dx=0,dy=0,len=0;
    for(int j=1;j<=2;j++)for(int i=1;i<=2;i++){
        double w=(i==1?1-fx:fx)*(j==1?1-fy:fy),mid=lum[j][i];
        double gx=lum[j][i+1]-lum[j][i-1],gy=lum[j+1][i]-lum[j-1][i];
        double sx=fmax(fabs(lum[j][i+1]-mid),fabs(mid-lum[j][i-1]));
        double sy=fmax(fabs(lum[j+1][i]-mid),fabs(mid-lum[j-1][i]));
        double lx=fmin(1,fabs(gx)/fmax(sx,1e-8)),ly=fmin(1,fabs(gy)/fmax(sy,1e-8));
        dx+=w*gx;dy+=w*gy;len+=w*(lx*lx+ly*ly);
    }
    double mag=dx*dx+dy*dy;
    if(mag<1./32768)dx=1;else{dx/=sqrt(mag);dy/=sqrt(mag);}
    len*=.5;len*=len;
    double stretch=(dx*dx+dy*dy)/fmax(fabs(dx),fabs(dy));
    double lx=1+(stretch-1)*len,ly=1-.5*len,lob=.5-.29*len,aw=0;
    out[0]=out[1]=out[2]=0;
    for(int j=0;j<4;j++)for(int i=0;i<4;i++){
        if((i==0||i==3)&&(j==0||j==3))continue;
        double ox=i-1-fx,oy=j-1-fy,vx=(ox*dx+oy*dy)*lx,vy=(-ox*dy+oy*dx)*ly;
        double d=fmin(vx*vx+vy*vy,1/lob),a=.4*d-1,b=lob*d-1,w=(1.5625*a*a-.5625)*b*b;
        aw+=w;for(int c=0;c<3;c++)out[c]+=rgb[j][i][c]*w;
    }
    for(int c=0;c<3;c++){
        double lo=1,hi=0;for(int j=1;j<=2;j++)for(int i=1;i<=2;i++){lo=fmin(lo,rgb[j][i][c]);hi=fmax(hi,rgb[j][i][c]);}
        out[c]=fsr_clamp(out[c]/aw,lo,hi);
    }
}
static void test_fsr(KPresentGles *pass,SDL_Renderer *r,KImage *src,const SDL_Rect *dst,uint8_t *actual,uint8_t *base){
    /* EASU intermediate from actual FBO, independent of RCAS presentation. */
    pass->fsr1=1;pass->fsr_strength=80;assert(!present_gles_draw(pass,r,src,dst,0));read_frame(actual);
    GLint old;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&old);glBindFramebuffer(GL_FRAMEBUFFER,pass->fbo);
    glReadPixels(0,0,960,720,GL_RGBA,GL_UNSIGNED_BYTE,base);glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)old);
    assert(glGetError()==GL_NO_ERROR);
    unsigned max_easu=0,max_rcas=0;
    for(int y=0;y<720;y+=3)for(int x=0;x<960;x+=3){
        double ref[3];fsr_easu_reference(src,x,y,ref);
        for(int c=0;c<3;c++){
            unsigned e=(unsigned)abs(base[((719-y)*960+x)*4+c]-(int)floor(ref[c]*255+.5));if(e>max_easu)max_easu=e;
        }
    }
    static const int offsets[5][2]={{0,-1},{-1,0},{0,0},{1,0},{0,1}};
    for(int y=0;y<720;y++)for(int x=0;x<960;x++){
        double col[5][3];for(int n=0;n<5;n++){
            int ix=x+offsets[n][0],iy=y+offsets[n][1];if(ix<0)ix=0;if(ix>959)ix=959;if(iy<0)iy=0;if(iy>719)iy=719;
            for(int c=0;c<3;c++)col[n][c]=base[(iy*960+ix)*4+c]/255.;
        }
        double lobe=-.1875;
        for(int c=0;c<3;c++){
            double lo=1,hi=0;for(int n=0;n<5;n++)if(n!=2){lo=fmin(lo,col[n][c]);hi=fmax(hi,col[n][c]);}
            double v=fmax(-fmin(lo,col[2][c])/fmax(4*hi,1e-8),(1-fmax(hi,col[2][c]))/fmin(4*lo-4,-1e-8));
            lobe=fmax(lobe,fmin(v,0));
        }
        lobe*=exp2(-.4);
        for(int c=0;c<3;c++){
            double v=(col[2][c]+lobe*(col[0][c]+col[1][c]+col[3][c]+col[4][c]))/(1+4*lobe);
            unsigned e=(unsigned)abs(actual[(y*1280+160+x)*4+c]-(int)floor(fsr_clamp(v,0,1)*255+.5));if(e>max_rcas)max_rcas=e;
        }
    }
    printf("FSR1 CPU reference: EASU max %u/255, RCAS max %u/255\n",max_easu,max_rcas);
    assert(max_easu<=2&&max_rcas<=1);
    /* Exact switch back to the old two-pass mode on the same GL object. */
    pass->fsr1=0;pass->edge_strength=55;assert(!present_gles_draw(pass,r,src,dst,0));read_frame(base);
    pass->fsr1=1;assert(!present_gles_draw(pass,r,src,dst,0));read_frame(actual);
    unsigned changed=0;for(unsigned i=0;i<1280*720*4;i++)changed+=base[i]!=actual[i];assert(changed>10000);
    pass->fsr1=0;assert(!present_gles_draw(pass,r,src,dst,0));read_frame(actual);assert(!memcmp(actual,base,1280u*720u*4u));
    printf("FSR1 differs from edge in %u channels; live restoration byte-identical: PASS\n",changed);
    pass->fsr1=1;
    for(unsigned flat=0;flat<2;flat++){
        for(unsigned i=0;i<640*480;i++){
            src->pixels[i*4]=src->pixels[i*4+1]=src->pixels[i*4+2]=(uint8_t)(flat*255);
            src->pixels[i*4+3]=91;
        }
        assert(!present_gles_draw(pass,r,src,dst,0));read_frame(actual);
        for(unsigned y=0;y<720;y++)for(unsigned x=160;x<1120;x++){
            for(unsigned c=0;c<3;c++)assert(actual[(y*1280+x)*4+c]==flat*255);
            assert(actual[(y*1280+x)*4+3]==255);
        }
        for(unsigned i=0;i<640*480;i++)for(unsigned c=0;c<4;c++)assert(src->pixels[i*4+c]==(c==3?91:flat*255));
    }
    pass->fsr1=0;
    puts("FSR1 flat black/white finite, opaque and source immutable: PASS");
}
