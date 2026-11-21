#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t base_rgb[16][3]={{0,0,0},{255,255,255},{85,85,85},{170,170,170},{255,0,0},{0,255,0},{0,0,255},{255,255,0},{0,255,255},{255,0,255},{128,0,0},{0,128,0},{0,0,128},{128,128,0},{0,128,128},{128,0,128}};
static uint8_t q5[32],q6[64],direct_pen[65536];
static uint32_t er(unsigned r0,unsigned g0,unsigned b0,unsigned r1,unsigned g1,unsigned b1){int32_t dr=(int32_t)r0-r1,dg=(int32_t)g0-g1,db=(int32_t)b0-b1;return 3*dr*dr+4*dg*dg+2*db*db;}
static void init(void){unsigned p,i;for(i=0;i<32;i++)q5[i]=(i*15+15)/31;for(i=0;i<64;i++)q6[i]=(i*15+31)/63;for(p=0;p<65536;p++){unsigned tr=q5[(p>>11)&31],tg=q6[(p>>5)&63],tb=q5[p&31];uint32_t best=~0u;uint8_t bp=0;for(i=0;i<16;i++){uint32_t e=er(tr,tg,tb,base_rgb[i][0]>>4,base_rgb[i][1]>>4,base_rgb[i][2]>>4);if(e<best){best=e;bp=i;}}direct_pen[p]=bp;}}

/* Proven 68K numerical UWORD output. */
static void old_encode(const uint16_t *src,uint16_t out[6][20]){unsigned word,pr=0,pg=0,pb=0;for(word=0;word<20;word++){uint16_t pw[6]={0};unsigned bit;for(bit=0;bit<16;bit++){unsigned x=word*16+bit;uint16_t px=src[x];unsigned tr=q5[(px>>11)&31],tg=q6[(px>>5)&63],tb=q5[px&31];uint8_t d=direct_pen[px];unsigned dr=base_rgb[d][0]>>4,dg=base_rgb[d][1]>>4,db=base_rgb[d][2]>>4;uint32_t best=er(tr,tg,tb,dr,dg,db);uint8_t code=d;unsigned br=dr,bg=dg,bb=db;uint16_t mask=(uint16_t)(0x8000u>>bit);uint32_t e=er(tr,tg,tb,pr,pg,tb);if(e<best){best=e;code=0x10|tb;br=pr;bg=pg;bb=tb;}e=er(tr,tg,tb,tr,pg,pb);if(e<best){best=e;code=0x20|tr;br=tr;bg=pg;bb=pb;}e=er(tr,tg,tb,pr,tg,pb);if(e<best){code=0x30|tg;br=pr;bg=tg;bb=pb;}for(unsigned pl=0;pl<6;pl++)if(code&(1u<<pl))pw[pl]|=mask;pr=br;pg=bg;pb=bb;}for(unsigned pl=0;pl<6;pl++)out[pl][word]=pw[pl];}}

/* CPU3-style byte input/output copied from the firmware algorithm. */
static void arm_encode(const uint8_t *src,uint8_t *dst){uint32_t pr=0,pg=0,pb=0;for(uint32_t word=0;word<20;word++){uint16_t pw[6]={0};for(uint32_t bit=0;bit<16;bit++){uint32_t x=word*16+bit,s=x*2;uint16_t px=(uint16_t)(((uint16_t)src[s]<<8)|src[s+1]);uint32_t tr=(((px>>11)&31u)*15u+15u)/31u,tg=(((px>>5)&63u)*15u+31u)/63u,tb=((px&31u)*15u+15u)/31u;uint8_t d=direct_pen[px];uint32_t dr=base_rgb[d][0]>>4,dg=base_rgb[d][1]>>4,db=base_rgb[d][2]>>4,best=er(tr,tg,tb,dr,dg,db);uint8_t code=d;uint32_t br=dr,bg=dg,bb=db;uint16_t mask=(uint16_t)(0x8000u>>bit);uint32_t e=er(tr,tg,tb,pr,pg,tb);if(e<best){best=e;code=0x10|tb;br=pr;bg=pg;bb=tb;}e=er(tr,tg,tb,tr,pg,pb);if(e<best){best=e;code=0x20|tr;br=tr;bg=pg;bb=pb;}e=er(tr,tg,tb,pr,tg,pb);if(e<best){code=0x30|tg;br=pr;bg=tg;bb=pb;}for(uint32_t pl=0;pl<6;pl++)if(code&(1u<<pl))pw[pl]|=mask;pr=br;pg=bg;pb=bb;}for(uint32_t pl=0;pl<6;pl++){uint32_t d=pl*40+word*2;dst[d]=pw[pl]>>8;dst[d+1]=(uint8_t)pw[pl];}}}

static uint32_t rng=0x68030029u;static uint32_t rr(void){rng=rng*1664525u+1013904223u;return rng;}
static int one(const uint16_t *line){uint16_t old[6][20];uint8_t in[640],arm[240],want[240];for(unsigned i=0;i<320;i++){in[i*2]=line[i]>>8;in[i*2+1]=(uint8_t)line[i];}old_encode(line,old);for(unsigned p=0;p<6;p++)for(unsigned w=0;w<20;w++){unsigned d=p*40+w*2;want[d]=old[p][w]>>8;want[d+1]=(uint8_t)old[p][w];}arm_encode(in,arm);return memcmp(arm,want,240)==0;}
int main(void){uint16_t line[320];init();for(unsigned i=0;i<320;i++)line[i]=0;if(!one(line))return 1;for(unsigned i=0;i<320;i++)line[i]=(uint16_t)(((i&31)<<11)|(((i*3)&63)<<5)|((i*7)&31));if(!one(line))return 2;for(unsigned n=0;n<1000;n++){for(unsigned i=0;i<320;i++)line[i]=(uint16_t)rr();if(!one(line)){fprintf(stderr,"FAIL random line %u\n",n);return 3;}}puts("PASS: C differential test 1002 lines exact");return 0;}
