// Read-only diagnostic: print the color-management image-description info a
// Hyprland output advertises (luminances + target_luminance etc). Benign.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wayland-client.h>
#include "color-management-v1-client-protocol.h"

static struct wp_color_manager_v1 *mgr;
struct out { struct wl_output *wl; uint32_t name; char conn[64]; struct out *next; };
static struct out *outs;
static const char *want;
static int done_count;

static void li_done(void *d, struct wp_image_description_info_v1 *i){ (void)d;(void)i; printf("  [done]\n"); done_count++; }
static void li_icc(void *d, struct wp_image_description_info_v1 *i, int32_t fd, uint32_t s){(void)d;(void)i;(void)fd; printf("  icc_file size=%u\n", s);}
static void li_prim(void *d, struct wp_image_description_info_v1 *i, int32_t r_x,int32_t r_y,int32_t g_x,int32_t g_y,int32_t b_x,int32_t b_y,int32_t w_x,int32_t w_y){(void)d;(void)i; printf("  primaries R(%d,%d) G(%d,%d) B(%d,%d) W(%d,%d) [*1e6]\n",r_x,r_y,g_x,g_y,b_x,b_y,w_x,w_y);}
static void li_primn(void *d, struct wp_image_description_info_v1 *i, uint32_t p){(void)d;(void)i; printf("  primaries_named=%u\n",p);}
static void li_tfp(void *d, struct wp_image_description_info_v1 *i, uint32_t e){(void)d;(void)i; printf("  tf_power=%u [*1e4]\n",e);}
static void li_tfn(void *d, struct wp_image_description_info_v1 *i, uint32_t tf){(void)d;(void)i; printf("  tf_named=%u\n",tf);}
static void li_lum(void *d, struct wp_image_description_info_v1 *i, uint32_t mn,uint32_t mx,uint32_t ref){(void)d;(void)i; printf("  luminances: min=%.4f max=%u reference=%u (cd/m2)\n", mn/10000.0, mx, ref);}
static void li_tprim(void *d, struct wp_image_description_info_v1 *i, int32_t a,int32_t b,int32_t c,int32_t e,int32_t f,int32_t g,int32_t h,int32_t j){(void)d;(void)i; printf("  target_primaries R(%d,%d) G(%d,%d) B(%d,%d) W(%d,%d) [*1e6]\n",a,b,c,e,f,g,h,j);}
static void li_tlum(void *d, struct wp_image_description_info_v1 *i, uint32_t mn,uint32_t mx){(void)d;(void)i; printf("  target_luminance: min=%.4f max=%u (cd/m2)  <<<\n", mn/10000.0, mx);}
static void li_cll(void *d, struct wp_image_description_info_v1 *i, uint32_t v){(void)d;(void)i; printf("  target_max_cll=%u\n",v);}
static void li_fall(void *d, struct wp_image_description_info_v1 *i, uint32_t v){(void)d;(void)i; printf("  target_max_fall=%u\n",v);}
static const struct wp_image_description_info_v1_listener ilis={li_done,li_icc,li_prim,li_primn,li_tfp,li_tfn,li_lum,li_tprim,li_tlum,li_cll,li_fall};

static void dready(void *d, struct wp_image_description_v1 *desc, uint32_t id){(void)d;(void)id; struct wp_image_description_info_v1 *inf=wp_image_description_v1_get_information(desc); wp_image_description_info_v1_add_listener(inf,&ilis,NULL);}
static void dfail(void *d, struct wp_image_description_v1 *desc, uint32_t c, const char *m){(void)d;(void)desc; printf("  image description FAILED cause=%u %s\n",c,m?m:""); done_count++;}
static const struct wp_image_description_v1_listener dlis={dfail,dready};

static void oname(void *d, struct wl_output *o, const char *n){(void)o; struct out *e=d; snprintf(e->conn,sizeof e->conn,"%s",n);}
static void ogeo(void*d,struct wl_output*o,int32_t x,int32_t y,int32_t pw,int32_t ph,int32_t sp,const char*mk,const char*md,int32_t tr){(void)d;(void)o;(void)x;(void)y;(void)pw;(void)ph;(void)sp;(void)mk;(void)md;(void)tr;}
static void omode(void*d,struct wl_output*o,uint32_t f,int32_t w,int32_t h,int32_t r){(void)d;(void)o;(void)f;(void)w;(void)h;(void)r;}
static void odone(void*d,struct wl_output*o){(void)d;(void)o;}
static void oscale(void*d,struct wl_output*o,int32_t s){(void)d;(void)o;(void)s;}
static void odesc(void*d,struct wl_output*o,const char*s){(void)d;(void)o;(void)s;}
static const struct wl_output_listener olis={ogeo,omode,odone,oscale,oname,odesc};

static void reg(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver){(void)d;
  if(!strcmp(iface,"wp_color_manager_v1")) mgr=wl_registry_bind(r,name,&wp_color_manager_v1_interface,ver<1?ver:1);
  else if(!strcmp(iface,"wl_output")){ struct out *e=calloc(1,sizeof*e); e->wl=wl_registry_bind(r,name,&wl_output_interface, ver<4?ver:4); e->name=name; e->next=outs; outs=e; wl_output_add_listener(e->wl,&olis,e);} }
static void regr(void *d, struct wl_registry *r, uint32_t n){(void)d;(void)r;(void)n;}
static const struct wl_registry_listener rl={reg,regr};

int main(int argc,char**argv){ want=argc>1?argv[1]:"eDP-1";
  struct wl_display *dp=wl_display_connect(NULL); if(!dp){fprintf(stderr,"no display\n");return 1;}
  struct wl_registry *r=wl_display_get_registry(dp); wl_registry_add_listener(r,&rl,NULL);
  wl_display_roundtrip(dp); wl_display_roundtrip(dp);
  if(!mgr){fprintf(stderr,"no wp_color_manager_v1\n");return 2;}
  int matched=0;
  for(struct out *e=outs;e;e=e->next){ if(strcmp(e->conn,want))continue; matched=1;
    printf("output %s:\n", e->conn);
    struct wp_color_management_output_v1 *co=wp_color_manager_v1_get_output(mgr,e->wl);
    struct wp_image_description_v1 *desc=wp_color_management_output_v1_get_image_description(co);
    wp_image_description_v1_add_listener(desc,&dlis,NULL);
  }
  if(!matched){fprintf(stderr,"output %s not found. have:",want); for(struct out *e=outs;e;e=e->next)fprintf(stderr," %s",e->conn); fprintf(stderr,"\n"); return 3;}
  for(int k=0;k<8 && done_count<1;k++) wl_display_roundtrip(dp);
  return 0; }
