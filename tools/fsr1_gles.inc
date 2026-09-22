//------------------------------------------------------------------------------------------------------------------------------
// FidelityFX Super Resolution Sample
//
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// GLES2: explicit texel-centre samples instead of gather/load; FP32 exact
// reciprocal/sqrt instead of bit approximations; zero denominators guarded.
// Derived from AMD ffx_fsr1.h v1.20210629 (FSR 1.0), MIT.
"\n"
"float fsrLum(vec3 c){return c.b*.5+(c.r*.5+c.g);}\n"
"void fsrSet(inout vec2 dir,inout float len,float w,float a,float b,float c,float d,float e){\n"
"    vec2 grad=vec2(d-b,e-a);\n"
"    vec2 span=max(abs(vec2(d-c,e-c)),abs(vec2(c-b,c-a)));\n"
"    dir+=grad*w;\n"
"    vec2 v=clamp(abs(grad)/max(span,vec2(1.e-8)),0.,1.);\n"
"    len+=dot(v,v)*w;\n"
"}\n"
"void fsrTap(inout vec3 ac,inout float aw,vec2 off,vec2 dir,vec2 len,float lob,float clp,vec3 c){\n"
"    vec2 v=vec2(dot(off,dir),dot(off,vec2(-dir.y,dir.x)))*len;\n"
"    float d2=min(dot(v,v),clp);\n"
"    float wb=.4*d2-1.,wa=lob*d2-1.;\n"
"    float w=(1.5625*wb*wb-.5625)*wa*wa;\n"
"    ac+=c*w;aw+=w;\n"
"}\n"
"vec3 fsrEasu(){\n"
"    vec2 p=v_uv/u_texel-.5,fp=floor(p),pp=p-fp;\n"
"    vec3 b=px(fp+vec2(.5,-.5)),c=px(fp+vec2(1.5,-.5));\n"
"    vec3 e=px(fp+vec2(-.5,.5)),f=px(fp+vec2(.5,.5)),g=px(fp+vec2(1.5,.5)),h=px(fp+vec2(2.5,.5));\n"
"    vec3 i=px(fp+vec2(-.5,1.5)),j=px(fp+vec2(.5,1.5)),k=px(fp+vec2(1.5,1.5)),l=px(fp+vec2(2.5,1.5));\n"
"    vec3 n=px(fp+vec2(.5,2.5)),o=px(fp+vec2(1.5,2.5));\n"
"    float bl=fsrLum(b),cl=fsrLum(c),el=fsrLum(e),fl=fsrLum(f),gl=fsrLum(g),hl=fsrLum(h);\n"
"    float il=fsrLum(i),jl=fsrLum(j),kl=fsrLum(k),ll=fsrLum(l),nl=fsrLum(n),ol=fsrLum(o);\n"
"    vec2 dir=vec2(0.);float len=0.;\n"
"    fsrSet(dir,len,(1.-pp.x)*(1.-pp.y),bl,el,fl,gl,jl);\n"
"    fsrSet(dir,len,pp.x*(1.-pp.y),cl,fl,gl,hl,kl);\n"
"    fsrSet(dir,len,(1.-pp.x)*pp.y,fl,il,jl,kl,nl);\n"
"    fsrSet(dir,len,pp.x*pp.y,gl,jl,kl,ll,ol);\n"
"    float dr=dot(dir,dir);\n"
"    if(dr<1./32768.)dir.x=1.;else dir*=inversesqrt(dr);\n"
"    len*=.5;len*=len;\n"
"    float stretch=dot(dir,dir)/max(abs(dir.x),abs(dir.y));\n"
"    vec2 len2=vec2(1.+(stretch-1.)*len,1.-.5*len);\n"
"    float lob=.5+(.25-.04-.5)*len,clp=1./lob;\n"
"    vec3 ac=vec3(0.);float aw=0.;\n"
"    fsrTap(ac,aw,vec2(0.,-1.)-pp,dir,len2,lob,clp,b);\n"
"    fsrTap(ac,aw,vec2(1.,-1.)-pp,dir,len2,lob,clp,c);\n"
"    fsrTap(ac,aw,vec2(-1.,1.)-pp,dir,len2,lob,clp,i);\n"
"    fsrTap(ac,aw,vec2(0.,1.)-pp,dir,len2,lob,clp,j);\n"
"    fsrTap(ac,aw,vec2(0.,0.)-pp,dir,len2,lob,clp,f);\n"
"    fsrTap(ac,aw,vec2(-1.,0.)-pp,dir,len2,lob,clp,e);\n"
"    fsrTap(ac,aw,vec2(1.,1.)-pp,dir,len2,lob,clp,k);\n"
"    fsrTap(ac,aw,vec2(2.,1.)-pp,dir,len2,lob,clp,l);\n"
"    fsrTap(ac,aw,vec2(2.,0.)-pp,dir,len2,lob,clp,h);\n"
"    fsrTap(ac,aw,vec2(1.,0.)-pp,dir,len2,lob,clp,g);\n"
"    fsrTap(ac,aw,vec2(1.,2.)-pp,dir,len2,lob,clp,o);\n"
"    fsrTap(ac,aw,vec2(0.,2.)-pp,dir,len2,lob,clp,n);\n"
"    return clamp(ac/aw,min(min(f,g),min(j,k)),max(max(f,g),max(j,k)));\n"
"}\n"
"vec3 fsrRcas(){\n"
"    vec3 b=texture2D(u_tex,v_uv+vec2(0.,-u_texel.y)).rgb;\n"
"    vec3 d=texture2D(u_tex,v_uv+vec2(-u_texel.x,0.)).rgb;\n"
"    vec3 e=texture2D(u_tex,v_uv).rgb;\n"
"    vec3 f=texture2D(u_tex,v_uv+vec2(u_texel.x,0.)).rgb;\n"
"    vec3 h=texture2D(u_tex,v_uv+vec2(0.,u_texel.y)).rgb;\n"
"    vec3 mn=min(min(b,d),min(f,h)),mx=max(max(b,d),max(f,h));\n"
"    vec3 hitMin=min(mn,e)/max(4.*mx,vec3(1.e-8));\n"
"    vec3 hitMax=(vec3(1.)-max(mx,e))/min(4.*mn-4.,vec3(-1.e-8));\n"
"    vec3 lobes=max(-hitMin,hitMax);\n"
"    float lobe=max(-.1875,min(max(lobes.r,max(lobes.g,lobes.b)),0.))*exp2(-2.*(1.-u_strength));\n"
"    return (lobe*(b+d+h+f)+e)/(4.*lobe+1.);\n"
"}\n"
