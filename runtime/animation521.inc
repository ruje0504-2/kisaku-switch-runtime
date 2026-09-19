/* CFuncExec 31/521 (0x4fa060) applies the lower page's blue-channel mask
 * to the alpha byte of the upper page in one CDIB.  The native path is:
 *
 *   4fa060 -> 503100/502c00 -> CDIB::virtual_180(0,0,0x280,0x1e0)
 *
 * CDIB stores its bitmap bottom-up.  virtual_180 reads the page beginning at
 * logical y=0x1e0 and writes the page at y=0, preserving BGR.  KImage is
 * top-down, so the equivalent portable operation reads rows 480..959 and
 * writes rows 0..479.  The native lookup is
 *   alpha = (31 - (blue >> 3)) * 8, with 248 remapped to 255.
 *
 * The source and destination pages do not overlap, so no temporary copy is
 * needed.  Invalid selectors or undersized surfaces remain explicit errors;
 * the dispatcher consumes neither argument until this function succeeds.
 */
static int animation521_draw(KBootstrap *b,int selector){
    KImage *im=surface(b,selector);
    if(!im||!im->pixels)return error(b,"31/521 target surface missing (arguments preserved)");
    if(im->width<0x280||im->height<0x3c0||im->stride<(size_t)im->width*4)
        return error(b,"31/521 target surface must contain two 640x480 pages (arguments preserved)");
    for(unsigned y=0;y<0x1e0;y++){
        const uint8_t *mask=im->pixels+(size_t)(y+0x1e0)*im->stride;
        uint8_t *dst=im->pixels+(size_t)y*im->stride;
        for(unsigned x=0;x<0x280;x++){
            unsigned alpha=(31u-((unsigned)mask[x*4]>>3))*8u;
            if(alpha==0xf8u)alpha=0xffu;
            dst[x*4+3]=(uint8_t)alpha;
        }
    }
    return 0;
}
