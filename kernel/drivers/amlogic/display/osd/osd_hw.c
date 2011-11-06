/*
 * Amlogic Apollo
 * frame buffer driver
 *
 * Copyright (C) 2009 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <mach/am_regs.h>
#include <linux/irqreturn.h>
#include <linux/errno.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/osd/osd.h>
#include <linux/amports/canvas.h>
#include "osd_log.h"
#include <linux/amlog.h>
#include "osd_hw_def.h"

#define  FIQ_VSYNC


static DECLARE_WAIT_QUEUE_HEAD(osd_vsync_wq);
static bool vsync_hit = false;

/********************************************************************/
/***********		osd psedu frame provider 			*****************/
/********************************************************************/
static vframe_t *osd_vf_peek(void)
{
	vf_w=vf;
	return (vf.width ==0 && vf.height==0) ? NULL:&vf ;
}

static vframe_t *osd_vf_get(void)
{
	if (vf_w.width !=0 && vf_w.height !=0) {
		memset(&vf,0,sizeof(vframe_t));
		return &vf_w;
	}
	return NULL;
}

static const struct vframe_provider_s osd_vf_provider =
{
    .peek = osd_vf_peek,
    .get  = osd_vf_get,
    .put  = NULL,
};

static inline void  osd_update_3d_mode(int enable_osd1,int enable_osd2)
{
	if(enable_osd1)
	{
		osd1_update_disp_3d_mode();
	}
	if(enable_osd2)
	{
		osd2_update_disp_3d_mode();
	}
}

static inline void wait_vsync_wakeup(void)
{
	vsync_hit = true;
	wake_up_interruptible(&osd_vsync_wq);
}

/**********************************************************************/
/**********          osd vsync irq handler              ***************/
/**********************************************************************/
#ifdef FIQ_VSYNC
static irqreturn_t vsync_isr(int irq, void *dev_id)
{
	wait_vsync_wakeup();

	return IRQ_HANDLED;
}
#endif

#ifdef FIQ_VSYNC
irqreturn_t osd_fiq_isr(void)
#else
static irqreturn_t vsync_isr(int irq, void *dev_id)
#endif
{
	unsigned  int  fb0_cfg_w0,fb1_cfg_w0;
	unsigned  int  current_field;
	hw_list_t		*p_update_list,*tmp;
	
	if (READ_MPEG_REG(ENCI_VIDEO_EN) & 1)
		osd_hw.scan_mode= SCAN_MODE_INTERLACE;
	else if (READ_MPEG_REG(ENCP_VIDEO_MODE) & (1 << 12))
		osd_hw.scan_mode= SCAN_MODE_INTERLACE;
	else
		osd_hw.scan_mode= SCAN_MODE_PROGRESSIVE;
	
	if (osd_hw.scan_mode== SCAN_MODE_INTERLACE)
	{
		fb0_cfg_w0=READ_MPEG_REG(VIU_OSD1_BLK0_CFG_W0);
		fb1_cfg_w0=READ_MPEG_REG(VIU_OSD1_BLK0_CFG_W0+ REG_OFFSET);
		if (READ_MPEG_REG(ENCP_VIDEO_MODE) & (1 << 12))
        	{
       		 /* 1080I */
			 
        		if (READ_MPEG_REG(VENC_ENCP_LINE) >= 562) {
           		 /* bottom field */
            			current_field = 0;
        		} else {
           			current_field = 1;
        		}
    		} else {
        		current_field = READ_MPEG_REG(VENC_STATA) & 1;
    		}
		fb0_cfg_w0 &=~1;
		fb1_cfg_w0 &=~1;
		fb0_cfg_w0 |=current_field;
		fb1_cfg_w0 |=current_field;
		WRITE_MPEG_REG(VIU_OSD1_BLK0_CFG_W0, fb0_cfg_w0);
		WRITE_MPEG_REG(VIU_OSD1_BLK0_CFG_W0+ REG_OFFSET, fb1_cfg_w0);
	}
	//go through update list
	if(!list_empty(&update_list))
	{
		list_for_each_entry_safe(p_update_list,tmp, &update_list, list)
		{
			p_update_list->update_func();
		}
	}
	osd_update_3d_mode(osd_hw.mode_3d[OSD1].enable,osd_hw.mode_3d[OSD2].enable);
	
	if (READ_MPEG_REG(VENC_ENCI_LINE) >= 12)
		READ_MPEG_REG(VENC_ENCI_LINE);

	if (!vsync_hit)
	{
#ifdef FIQ_VSYNC
		fiq_bridge_pulse_trigger(&osd_hw.fiq_handle_item);
#else
		wait_vsync_wakeup();
#endif
	}

	return  IRQ_HANDLED ;
}

#ifdef FIQ_VSYNC
EXPORT_SYMBOL(osd_fiq_isr);
#endif

void osd_wait_vsync_hw(void)
{
	vsync_hit = false;

	wait_event_interruptible_timeout(osd_vsync_wq, vsync_hit, HZ);
}

void  osd_set_gbl_alpha_hw(u32 index,u32 gbl_alpha)
{
	if(osd_hw.gbl_alpha[index] != gbl_alpha)
	{
		
		osd_hw.gbl_alpha[index]=gbl_alpha;
		add_to_update_list(index,OSD_GBL_ALPHA);
		
		osd_wait_vsync_hw();
	}
}
u32  osd_get_gbl_alpha_hw(u32  index)
{
	return osd_hw.gbl_alpha[index];
}
void  osd_set_colorkey_hw(u32 index,u32 color_index,u32 colorkey )
{
	u8  r=0,g=0,b=0,a=(colorkey&0xff000000)>>24;
	u32	data32;

	colorkey&=0x00ffffff;
	 switch(color_index)
	  {
	 		case COLOR_INDEX_16_655:
			r=(colorkey>>10&0x3f)<<2;
			g=(colorkey>>5&0x1f)<<3;
			b=(colorkey&0x1f)<<3;
			break;	
			case COLOR_INDEX_16_844:
			r=colorkey>>8&0xff;
			g=(colorkey>>4&0xf)<<4;
			b=(colorkey&0xf)<<4;	
			break;	
			case COLOR_INDEX_16_565:
			r=(colorkey>>11&0x1f)<<3;
			g=(colorkey>>5&0x3f)<<2;
			b=(colorkey&0x1f)<<3;		
			break;	
			case COLOR_INDEX_24_888_B:
			b=colorkey>>16&0xff;
			g=colorkey>>8&0xff;
			r=colorkey&0xff;			
			break;
			case COLOR_INDEX_24_RGB:
			case COLOR_INDEX_YUV_422:	
			r=colorkey>>16&0xff;
			g=colorkey>>8&0xff;
			b=colorkey&0xff;			
			break;	
	 }	
	data32=r<<24|g<<16|b<<8|a;
	if( osd_hw.color_key[index]!=data32)
	{
		 osd_hw.color_key[index]=data32;
		amlog_mask_level(LOG_MASK_HARDWARE,LOG_LEVEL_LOW,"bpp:%d--r:0x%x g:0x%x b:0x%x ,a:0x%x\r\n",color_index,r,g,b,a);
		add_to_update_list(index,OSD_COLOR_KEY);
		
		osd_wait_vsync_hw();
	}

	return ;
}
void  osd_srckey_enable_hw(u32  index,u8 enable)
{
	if(enable != osd_hw.color_key_enable[index])
	{
		osd_hw.color_key_enable[index]=enable;
		add_to_update_list(index,OSD_COLOR_KEY_ENABLE);
		
		osd_wait_vsync_hw();
	}
	
}

void  osddev_update_disp_axis_hw(
			u32 display_h_start,
                  	u32 display_h_end,
                  	u32 display_v_start,
                  	u32 display_v_end,
			u32 xoffset,
                  	u32 yoffset,
                  	u32 mode_change,
                  	u32 index)
{
	dispdata_t   disp_data;
	pandata_t    pan_data;

	if(NULL==osd_hw.color_info[index]) return ;
	if(mode_change)  //modify pandata .
	{
		add_to_update_list(index,OSD_COLOR_MODE);
	}
	disp_data.x_start=display_h_start;
	disp_data.y_start=display_v_start;
	disp_data.x_end=display_h_end;
	disp_data.y_end=display_v_end;

	pan_data.x_start=xoffset;
	pan_data.x_end=xoffset + (display_h_end - display_h_start);
	pan_data.y_start=yoffset;
	pan_data.y_end=yoffset + (display_v_end-display_v_start);
	
	//if output mode change then reset pan ofFfset.
	memcpy(&osd_hw.pandata[index],&pan_data,sizeof(pandata_t));
	memcpy(&osd_hw.dispdata[index],&disp_data,sizeof(dispdata_t));
	add_to_update_list(index,DISP_GEOMETRY);
	osd_wait_vsync_hw();
	
}
void osd_setup(struct osd_ctl_s *osd_ctl,
                u32 xoffset,
                u32 yoffset,
                u32 xres,
                u32 yres,
                u32 xres_virtual,
                u32 yres_virtual,
                u32 disp_start_x,
                u32 disp_start_y,
                u32 disp_end_x,
                u32 disp_end_y,
                u32 fbmem,
                const color_bit_define_t *color,
                int index 
                )
{
	u32  w=(color->bpp * xres_virtual + 7) >> 3;
	dispdata_t   disp_data;
	pandata_t    pan_data;
#ifdef CONFIG_AM_LOGO	
	static u32	    logo_setup_ok=0;
#endif

	pan_data.x_start=xoffset;
	pan_data.x_end=xoffset + (disp_end_x-disp_start_x);
	pan_data.y_start=yoffset;
	pan_data.y_end=yoffset + (disp_end_y-disp_start_y);

	disp_data.x_start=disp_start_x;
	disp_data.y_start=disp_start_y;
	disp_data.x_end=disp_end_x;
	disp_data.y_end=disp_end_y;
	
       if( osd_hw.fb_gem[index].addr!=fbmem || osd_hw.fb_gem[index].width !=w ||  osd_hw.fb_gem[index].height !=yres_virtual)
       	{
		osd_hw.fb_gem[index].addr=fbmem;
		osd_hw.fb_gem[index].width=w;
		osd_hw.fb_gem[index].height=yres_virtual;
		canvas_config(osd_hw.fb_gem[index].canvas_idx, osd_hw.fb_gem[index].addr,
	              			osd_hw.fb_gem[index].width, osd_hw.fb_gem[index].height,
	              			CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
       	}	
	
	if(color != osd_hw.color_info[index])
	{
		osd_hw.color_info[index]=color;
		add_to_update_list(index,OSD_COLOR_MODE);
	}
	if(osd_hw.enable[index] == DISABLE)
	{
		osd_hw.enable[index]=ENABLE;
		add_to_update_list(index,OSD_ENABLE);
		
	}
	if(memcmp(&pan_data,&osd_hw.pandata[index],sizeof(pandata_t))!= 0 ||
		memcmp(&disp_data,&osd_hw.dispdata[index],sizeof(dispdata_t))!=0)
	{
		if(!osd_hw.free_scale_enable[OSD1]) //in free scale mode ,adjust geometry para is abandoned.
		{
			memcpy(&osd_hw.pandata[index],&pan_data,sizeof(pandata_t));
			memcpy(&osd_hw.dispdata[index],&disp_data,sizeof(dispdata_t));
			add_to_update_list(index,DISP_GEOMETRY);
		}	
	}

#ifdef CONFIG_AM_LOGO
	if(!logo_setup_ok)
	{
#ifdef FIQ_VSYNC
		osd_fiq_isr();
#else
		vsync_isr(INT_VIU_VSYNC,NULL);
#endif
		logo_setup_ok++;
	}
#endif

	osd_wait_vsync_hw();
}

void osd_setpal_hw(unsigned regno,
                 unsigned red,
                 unsigned green,
                 unsigned blue,
                 unsigned transp,
                 int index 
                 )
{

    if (regno < 256) {
        u32 pal;
        pal = ((red   & 0xff) << 24) |
              ((green & 0xff) << 16) |
              ((blue  & 0xff) <<  8) |
              (transp & 0xff);

        WRITE_MPEG_REG(VIU_OSD1_COLOR_ADDR+REG_OFFSET*index, regno);
        WRITE_MPEG_REG(VIU_OSD1_COLOR+REG_OFFSET*index, pal);
    }
}
void osd_free_scale_enable_hw(u32 index,u32 enable)
{
	static  dispdata_t	save_disp_data;

	amlog_level(LOG_LEVEL_HIGH,"osd%d free scale %s\r\n",index,enable?"ENABLE":"DISABLE");
	osd_hw.free_scale_enable[index]=enable;
	if (index==OSD1)
	{
		if(enable)
		{
			vf.width=osd_hw.free_scale_width[OSD1];
			vf.height=osd_hw.free_scale_height[OSD1];
			vf.type = (osd_hw.scan_mode==SCAN_MODE_INTERLACE ?VIDTYPE_INTERLACE:VIDTYPE_PROGRESSIVE) | VIDTYPE_VIU_FIELD;
			vf.ratio_control=DISP_RATIO_FORCECONFIG|DISP_RATIO_NO_KEEPRATIO;
			vf_reg_provider(&osd_vf_provider);

			memcpy(&save_disp_data,&osd_hw.dispdata[OSD1],sizeof(dispdata_t));
			osd_hw.pandata[OSD1].x_end =osd_hw.pandata[OSD1].x_start + vf.width-1;
			osd_hw.pandata[OSD1].y_end =osd_hw.pandata[OSD1].y_start + vf.height-1;	
			osd_hw.dispdata[OSD1].x_end =osd_hw.dispdata[OSD1].x_start + vf.width-1;
			osd_hw.dispdata[OSD1].y_end =osd_hw.dispdata[OSD1].y_start + vf.height-1;
			add_to_update_list(OSD1,DISP_GEOMETRY);
		}
		else
		{
			memcpy(&osd_hw.dispdata[OSD1],&save_disp_data,sizeof(dispdata_t));
			add_to_update_list(OSD1,DISP_GEOMETRY);
			vf_unreg_provider();
		}
	}
	if (osd_hw.enable[index])
		osd_enable_hw(ENABLE,index);

}
void  osd_free_scale_width_hw(u32 index,u32 width)
{
	osd_hw.free_scale_width[index]=width;
}
void  osd_free_scale_height_hw(u32 index,u32 height)
{
	osd_hw.free_scale_height[index]=height;
}
void osd_enable_3d_mode_hw(int index,int enable)
{
	spin_lock_irqsave(&osd_lock, lock_flags);
	osd_hw.mode_3d[index].enable=enable;
	spin_unlock_irqrestore(&osd_lock, lock_flags);
	if(enable)  //when disable 3d mode ,we should return to stardard state.
	{
		osd_hw.mode_3d[index].left_right=LEFT;
		osd_hw.mode_3d[index].l_start=osd_hw.pandata[index].x_start;
		osd_hw.mode_3d[index].l_end= (osd_hw.pandata[index].x_end +  osd_hw.pandata[index].x_start)>>1;
		osd_hw.mode_3d[index].r_start=osd_hw.mode_3d[index].l_end + 1;
		osd_hw.mode_3d[index].r_end=osd_hw.pandata[index].x_end;
		osd_hw.mode_3d[index].origin_scale.h_enable=osd_hw.scale[index].h_enable;
		osd_hw.mode_3d[index].origin_scale.v_enable=osd_hw.scale[index].v_enable;
		osd_set_2x_scale_hw(index,1,0);
	}
	else
	{
		
		osd_set_2x_scale_hw(index,osd_hw.mode_3d[index].origin_scale.h_enable,
								osd_hw.mode_3d[index].origin_scale.v_enable);
	}
}
void osd_enable_hw(int enable ,int index )
{
   	osd_hw.enable[index]=enable;
	add_to_update_list(index,OSD_ENABLE);
		
	osd_wait_vsync_hw();
}
void osd_set_2x_scale_hw(u32 index,u16 h_scale_enable,u16 v_scale_enable)
{
	osd_hw.scale[index].h_enable=h_scale_enable;
	osd_hw.scale[index].v_enable=v_scale_enable;
	add_to_update_list(index,DISP_SCALE_ENABLE);	
	
	osd_wait_vsync_hw();
}
void osd_pan_display_hw(unsigned int xoffset, unsigned int yoffset,int index )
{
	long diff_x, diff_y;

#if defined(CONFIG_FB_OSD2_CURSOR)
	if (index >= 1)
#else
	if (index >= 2)
#endif
		return;
    	
    	if(xoffset!=osd_hw.pandata[index].x_start || yoffset !=osd_hw.pandata[index].y_start)
    	{
    		diff_x = xoffset - osd_hw.pandata[index].x_start;
		diff_y = yoffset - osd_hw.pandata[index].y_start;

		osd_hw.pandata[index].x_start += diff_x;
		osd_hw.pandata[index].x_end   += diff_x;
		osd_hw.pandata[index].y_start += diff_y;
		osd_hw.pandata[index].y_end   += diff_y;
		add_to_update_list(index,DISP_GEOMETRY);
		
		osd_wait_vsync_hw();
		
		amlog_mask_level(LOG_MASK_HARDWARE,LOG_LEVEL_LOW,"offset[%d-%d]x[%d-%d]y[%d-%d]\n", \
				xoffset,yoffset,osd_hw.pandata[index].x_start ,osd_hw.pandata[index].x_end , \
				osd_hw.pandata[index].y_start ,osd_hw.pandata[index].y_end );
    	}
}
static  void  osd1_update_disp_scale_enable(void)
{
	if(osd_hw.scale[OSD1].h_enable)
	{
		SET_MPEG_REG_MASK(VIU_OSD1_BLK0_CFG_W0, 3<<12);
	}
	else
	{
		CLEAR_MPEG_REG_MASK(VIU_OSD1_BLK0_CFG_W0, 3<<12);
	}
	if(osd_hw.scan_mode != SCAN_MODE_INTERLACE)
	{
		if(osd_hw.scale[OSD1].v_enable)
		{
			SET_MPEG_REG_MASK(VIU_OSD1_BLK0_CFG_W0, 1<<14);
		}
		else
		{
			CLEAR_MPEG_REG_MASK(VIU_OSD1_BLK0_CFG_W0, 1<<14);
		}
	}	
}
static  void  osd2_update_disp_scale_enable(void)
{
	if(osd_hw.scale[OSD2].h_enable)
	{
		SET_MPEG_REG_MASK(VIU_OSD2_BLK0_CFG_W0, 3<<12);
	}
	else
	{
		CLEAR_MPEG_REG_MASK(VIU_OSD2_BLK0_CFG_W0, 3<<12);
	}
	if(osd_hw.scan_mode != SCAN_MODE_INTERLACE)
	{
		if(osd_hw.scale[OSD2].v_enable)
		{
			SET_MPEG_REG_MASK(VIU_OSD2_BLK0_CFG_W0, 1<<14);
		}
		else
		{
			CLEAR_MPEG_REG_MASK(VIU_OSD2_BLK0_CFG_W0, 1<<14);
		}
	}
	
}
static  inline void  osd1_update_color_mode(void)
{
	u32  data32;

	data32= (osd_hw.scan_mode== SCAN_MODE_INTERLACE) ? 2 : 0;
	data32 |=READ_MPEG_REG(VIU_OSD1_BLK0_CFG_W0)&0x40;
	data32 |= osd_hw.fb_gem[OSD1].canvas_idx << 16 ;
	data32 |= OSD_DATA_LITTLE_ENDIAN	 <<15 ;
    	data32 |= osd_hw.color_info[OSD1]->hw_colormat<< 2;	
	if(osd_hw.color_info[OSD1]->color_index < COLOR_INDEX_YUV_422)
	data32 |= 1                      << 7; /* rgb enable */
	data32 |=  osd_hw.color_info[OSD1]->hw_blkmode<< 8; /* osd_blk_mode */
	WRITE_MPEG_REG(VIU_OSD1_BLK0_CFG_W0, data32);
	remove_from_update_list(OSD1,OSD_COLOR_MODE);
	
}
static  inline void  osd2_update_color_mode(void)
{
	u32  data32;
	data32= (osd_hw.scan_mode== SCAN_MODE_INTERLACE) ? 2 : 0;
	data32 |=READ_MPEG_REG(VIU_OSD2_BLK0_CFG_W0)&0x40;
	data32 |= osd_hw.fb_gem[OSD2].canvas_idx << 16 ;
	data32 |= OSD_DATA_LITTLE_ENDIAN	 <<15 ;
    	data32 |= osd_hw.color_info[OSD2]->hw_colormat<< 2;	
	if(osd_hw.color_info[OSD2]->color_index < COLOR_INDEX_YUV_422)
	data32 |= 1                      << 7; /* rgb enable */
	data32 |=  osd_hw.color_info[OSD2]->hw_blkmode<< 8; /* osd_blk_mode */
	WRITE_MPEG_REG(VIU_OSD2_BLK0_CFG_W0, data32);
	remove_from_update_list(OSD2,OSD_COLOR_MODE);
}

static inline  void  osd1_update_enable(void)
{
	u32  video_enable;

	video_enable=READ_MPEG_REG(VPP_MISC)&VPP_VD1_PREBLEND;
	if(osd_hw.enable[OSD1]==ENABLE)
	{
		if(osd_hw.free_scale_enable[OSD1])
		{
			CLEAR_MPEG_REG_MASK(VPP_MISC,VPP_OSD1_POSTBLEND);
			SET_MPEG_REG_MASK(VPP_MISC,VPP_OSD1_PREBLEND);
			SET_MPEG_REG_MASK(VPP_MISC,VPP_VD1_POSTBLEND);
		}
		else
		{
			CLEAR_MPEG_REG_MASK(VPP_MISC,VPP_OSD1_PREBLEND);
			if(!video_enable)
			{
				CLEAR_MPEG_REG_MASK(VPP_MISC,VPP_VD1_POSTBLEND);
			}
			SET_MPEG_REG_MASK(VPP_MISC,VPP_OSD1_POSTBLEND);
		}
		
	}
	else
	{	
		if(osd_hw.free_scale_enable[OSD1])
		{
			CLEAR_MPEG_REG_MASK(VPP_MISC,VPP_OSD1_PREBLEND);
		}
		else
		{
			CLEAR_MPEG_REG_MASK(VPP_MISC,VPP_OSD1_POSTBLEND);
		}
	}
	remove_from_update_list(OSD1,OSD_ENABLE);
}
static inline  void  osd2_update_enable(void)
{
	u32  video_enable;

	video_enable=READ_MPEG_REG(VPP_MISC)&VPP_VD1_PREBLEND;
	if(osd_hw.enable[OSD2]==ENABLE)
	{
		if(osd_hw.free_scale_enable[OSD2])
		{
			CLEAR_MPEG_REG_MASK(VPP_MISC,VPP_OSD2_POSTBLEND);
			SET_MPEG_REG_MASK(VPP_MISC,VPP_OSD2_PREBLEND);
			SET_MPEG_REG_MASK(VPP_MISC,VPP_VD2_POSTBLEND);
		}
		else
		{
			CLEAR_MPEG_REG_MASK(VPP_MISC,VPP_OSD2_PREBLEND);
			if(!video_enable)
			{
				CLEAR_MPEG_REG_MASK(VPP_MISC,VPP_VD2_POSTBLEND);
			}
			SET_MPEG_REG_MASK(VPP_MISC,VPP_OSD2_POSTBLEND);
		}
		
	}
	else
	{	
		if(osd_hw.free_scale_enable[OSD2])
		{
			CLEAR_MPEG_REG_MASK(VPP_MISC,VPP_OSD2_PREBLEND);
		}
		else
		{
			CLEAR_MPEG_REG_MASK(VPP_MISC,VPP_OSD2_POSTBLEND);
		}
	}
	remove_from_update_list(OSD2,OSD_ENABLE);
}
static inline void  osd1_update_color_key(void)
{
	WRITE_MPEG_REG(VIU_OSD1_TCOLOR_AG0,osd_hw.color_key[OSD1]);
	remove_from_update_list(OSD1,OSD_COLOR_KEY);
}
static inline  void  osd2_update_color_key(void)
{
	WRITE_MPEG_REG(VIU_OSD2_TCOLOR_AG0,osd_hw.color_key[OSD2]);
	remove_from_update_list(OSD2,OSD_COLOR_KEY);
}
static inline void  osd1_update_color_key_enable(void)
{
	u32  data32;

	data32=READ_MPEG_REG(VIU_OSD1_BLK0_CFG_W0);
	data32&=~(1<<6);
	data32|=(osd_hw.color_key_enable[OSD1]<<6);
	WRITE_MPEG_REG(VIU_OSD1_BLK0_CFG_W0,data32);
	remove_from_update_list(OSD1,OSD_COLOR_KEY_ENABLE);
}
static inline void  osd2_update_color_key_enable(void)
{
	u32  data32;

	data32=READ_MPEG_REG(VIU_OSD2_BLK0_CFG_W0);
	data32&=~(1<<6);
	data32|=(osd_hw.color_key_enable[OSD2]<<6);
	WRITE_MPEG_REG(VIU_OSD2_BLK0_CFG_W0,data32);
	remove_from_update_list(OSD2,OSD_COLOR_KEY_ENABLE);
}
static inline  void  osd1_update_gbl_alpha(void)
{

	u32  data32=READ_MPEG_REG(VIU_OSD1_CTRL_STAT);
	data32&=~(0x1ff<<12);
	data32|=osd_hw.gbl_alpha[OSD1] <<12;
	WRITE_MPEG_REG(VIU_OSD1_CTRL_STAT,data32);
	remove_from_update_list(OSD1,OSD_GBL_ALPHA);
}
static inline  void  osd2_update_gbl_alpha(void)
{

	u32  data32=READ_MPEG_REG(VIU_OSD2_CTRL_STAT);
	data32&=~(0x1ff<<12);
	data32|=osd_hw.gbl_alpha[OSD2] <<12;
	WRITE_MPEG_REG(VIU_OSD2_CTRL_STAT,data32);
	remove_from_update_list(OSD2,OSD_GBL_ALPHA);
}	
static inline  void  osd1_update_disp_geometry(void)
{
	u32 data32;
	
   	data32 = (osd_hw.dispdata[OSD1].x_start& 0xfff) | (osd_hw.dispdata[OSD1].x_end & 0xfff) <<16 ;
      	WRITE_MPEG_REG(VIU_OSD1_BLK0_CFG_W3 , data32);
	if(osd_hw.scan_mode== SCAN_MODE_INTERLACE)
	{
		data32=((osd_hw.dispdata[OSD1].y_start >>1) & 0xfff) | ((((osd_hw.dispdata[OSD1].y_end+1)>>1)-1) & 0xfff) <<16 ;
	}
	else
	{
   		data32 = (osd_hw.dispdata[OSD1].y_start & 0xfff) | (osd_hw.dispdata[OSD1].y_end & 0xfff) <<16 ;
	}
       WRITE_MPEG_REG(VIU_OSD1_BLK0_CFG_W4, data32);

	data32=(osd_hw.pandata[OSD1].x_start & 0x1fff) | (osd_hw.pandata[OSD1].x_end & 0x1fff) << 16;
	WRITE_MPEG_REG(VIU_OSD1_BLK0_CFG_W1,data32);
	data32=(osd_hw.pandata[OSD1].y_start & 0x1fff) | (osd_hw.pandata[OSD1].y_end & 0x1fff) << 16 ;
	WRITE_MPEG_REG(VIU_OSD1_BLK0_CFG_W2,data32);
	remove_from_update_list(OSD1,DISP_GEOMETRY);
}
static inline  void  osd2_update_disp_geometry(void)
{
	u32 data32;
   	data32 = (osd_hw.dispdata[OSD2].x_start& 0xfff) | (osd_hw.dispdata[OSD2].x_end & 0xfff) <<16 ;
      	WRITE_MPEG_REG(VIU_OSD2_BLK0_CFG_W3 , data32);
	if(osd_hw.scan_mode== SCAN_MODE_INTERLACE)
	{
		data32=((osd_hw.dispdata[OSD2].y_start >>1) & 0xfff) | ((((osd_hw.dispdata[OSD2].y_end+1)>>1)-1) & 0xfff) <<16 ;
	}
	else
	{
   		data32 = (osd_hw.dispdata[OSD2].y_start & 0xfff) | (osd_hw.dispdata[OSD2].y_end & 0xfff) <<16 ;
	}	
   	WRITE_MPEG_REG(VIU_OSD2_BLK0_CFG_W4, data32);

	data32=(osd_hw.pandata[OSD2].x_start & 0x1fff) | (osd_hw.pandata[OSD2].x_end & 0x1fff) << 16;
	WRITE_MPEG_REG(VIU_OSD2_BLK0_CFG_W1,data32);
	data32=(osd_hw.pandata[OSD2].y_start & 0x1fff) | (osd_hw.pandata[OSD2].y_end & 0x1fff) << 16 ;
	WRITE_MPEG_REG(VIU_OSD2_BLK0_CFG_W2,data32);
	remove_from_update_list(OSD2,DISP_GEOMETRY);
}
static  void  osd1_update_disp_3d_mode(void)
{
	/*step 1 . set pan data */
	u32  data32;
	
	if(osd_hw.mode_3d[OSD1].left_right==LEFT)
	{
		data32=(osd_hw.mode_3d[OSD1].l_start& 0x1fff) | (osd_hw.mode_3d[OSD1].l_end& 0x1fff) << 16;
		WRITE_MPEG_REG(VIU_OSD1_BLK0_CFG_W1,data32);
	}
	else
	{
		data32=(osd_hw.mode_3d[OSD1].r_start& 0x1fff) | (osd_hw.mode_3d[OSD1].r_end& 0x1fff) << 16;
		WRITE_MPEG_REG(VIU_OSD1_BLK0_CFG_W1,data32);		
	}
	osd_hw.mode_3d[OSD1].left_right^=1;
}
static  void  osd2_update_disp_3d_mode(void)
{
	u32  data32;
	
	if(osd_hw.mode_3d[OSD2].left_right==LEFT)
	{
		data32=(osd_hw.mode_3d[OSD2].l_start& 0x1fff) | (osd_hw.mode_3d[OSD2].l_end& 0x1fff) << 16;
		WRITE_MPEG_REG(VIU_OSD2_BLK0_CFG_W1,data32);
	}
	else
	{
		data32=(osd_hw.mode_3d[OSD2].r_start& 0x1fff) | (osd_hw.mode_3d[OSD2].r_end& 0x1fff) << 16;
		WRITE_MPEG_REG(VIU_OSD2_BLK0_CFG_W1,data32);		
	}
	osd_hw.mode_3d[OSD2].left_right^=1;
}
void osd_init_hw(void)
{
	u32 group,idx,data32;
	
	for(group=0;group<HW_OSD_COUNT;group++)
	for(idx=0;idx<HW_REG_INDEX_MAX;idx++)
	{
		osd_hw.reg[group][idx].update_func=hw_func_array[group][idx];
		osd_hw.reg[group][idx].list.next=NULL;
	}
	//here we will init default value ,these value only set once .
    	data32  = 4   << 5;  // hold_fifo_lines
    	data32 |= 3   << 10; // burst_len_sel: 3=64
    	data32 |= 32  << 12; // fifo_depth_val: 32*8=256
    	
    	WRITE_MPEG_REG(VIU_OSD1_FIFO_CTRL_STAT, data32);
	WRITE_MPEG_REG(VIU_OSD2_FIFO_CTRL_STAT, data32);

	SET_MPEG_REG_MASK(VPP_MISC,VPP_POSTBLEND_EN);
	CLEAR_MPEG_REG_MASK(VPP_MISC, VPP_PREBLEND_EN);  
#if defined(CONFIG_FB_OSD2_CURSOR)    
	SET_MPEG_REG_MASK(VPP_MISC, VPP_POST_FG_OSD2|VPP_PRE_FG_OSD2);
#else   
	CLEAR_MPEG_REG_MASK(VPP_MISC,VPP_POST_FG_OSD2|VPP_PRE_FG_OSD2);
#endif	
	CLEAR_MPEG_REG_MASK(VPP_MISC,VPP_OSD1_POSTBLEND|VPP_OSD2_POSTBLEND );

	osd_hw.enable[OSD2]=osd_hw.enable[OSD1]=DISABLE;
	osd_hw.fb_gem[OSD1].canvas_idx=OSD1_CANVAS_INDEX;
	osd_hw.fb_gem[OSD2].canvas_idx=OSD2_CANVAS_INDEX;
	osd_hw.gbl_alpha[OSD1]=OSD_GLOBAL_ALPHA_DEF;
	osd_hw.gbl_alpha[OSD2]=OSD_GLOBAL_ALPHA_DEF;
	osd_hw.color_info[OSD1]=NULL;
	osd_hw.color_info[OSD2]=NULL;
	vf.width =vf.height=0;
	osd_hw.color_key[OSD1]=osd_hw.color_key[OSD2]=0xffffffff;
	osd_hw.free_scale_enable[OSD1]=osd_hw.free_scale_enable[OSD2]=0;
	osd_hw.scale[OSD1].h_enable=osd_hw.scale[OSD1].v_enable=0;
	osd_hw.scale[OSD2].h_enable=osd_hw.scale[OSD2].v_enable=0;
	osd_hw.mode_3d[OSD2].enable=osd_hw.mode_3d[OSD1].enable=0;
	data32  = 0x1          << 0; // osd_blk_enable
    	data32 |= OSD_GLOBAL_ALPHA_DEF<< 12;
	data32 |= (1<<21)	;
    	WRITE_MPEG_REG(VIU_OSD1_CTRL_STAT , data32);
	WRITE_MPEG_REG(VIU_OSD2_CTRL_STAT , data32);
	
#ifdef FIQ_VSYNC
	osd_hw.fiq_handle_item.handle=vsync_isr;
	osd_hw.fiq_handle_item.key=(u32)vsync_isr;
	osd_hw.fiq_handle_item.name="osd_vsync";
	if(register_fiq_bridge_handle(&osd_hw.fiq_handle_item))	
#else
	if ( request_irq(INT_VIU_VSYNC, &vsync_isr,
		IRQF_SHARED , "am_osd_vsync", osd_setup))
#endif
	{
		amlog_level(LOG_LEVEL_HIGH,"can't request irq for vsync\r\n");
	}

	return ;
}


#if defined(CONFIG_FB_OSD2_CURSOR)
void osd_cursor_hw(s16 x, s16 y, s16 xstart, s16 ystart, u32 osd_w, u32 osd_h, int index)
{
	if (index != 1) return;

	
	if(osd_hw.scale[OSD1].h_enable && osd_hw.scale[OSD1].h_enable) {
		x *= 2;
		y *= 2;
	}
	x += xstart;
	y += ystart;
	/**
	 * Use pandata to show a partial cursor when it is at the edge because the
	 * registers can't have negative values and because we need to manually
	 * clip the cursor when it is past the edge.  The edge is hardcoded
	 * to the OSD0 area.
	 */
	osd_hw.dispdata[OSD2].x_start = x;
	osd_hw.dispdata[OSD2].y_start = y;
	if (x < osd_hw.dispdata[OSD1].x_start) {
		// if negative position, set osd to 0,y and pan.
		if ((osd_hw.dispdata[OSD1].x_start - x) < osd_w) {
			osd_hw.pandata[OSD2].x_start = osd_hw.dispdata[OSD1].x_start - x;
			osd_hw.pandata[OSD2].x_end = osd_w - 1;
		}
		osd_hw.dispdata[OSD2].x_start = 0;
	} else {
		osd_hw.pandata[OSD2].x_start = 0;
		if (x + osd_w > osd_hw.dispdata[OSD1].x_end) {
			// if past positive edge, set osd to inside of the edge and pan.
			if (x < osd_hw.dispdata[OSD1].x_end)
				osd_hw.pandata[OSD2].x_end = osd_hw.dispdata[OSD1].x_end - x;
		} else {
			osd_hw.pandata[OSD2].x_end = osd_w - 1;
		}
	}
	if (y < osd_hw.dispdata[OSD1].y_start) {
		if ((osd_hw.dispdata[OSD1].y_start - y) < osd_h) {
			osd_hw.pandata[OSD2].y_start = osd_hw.dispdata[OSD1].y_start - y;
			osd_hw.pandata[OSD2].y_end = osd_h - 1;
		}
		osd_hw.dispdata[OSD2].y_start = 0;
	} else {
		osd_hw.pandata[OSD2].y_start = 0;
		if (y + osd_h > osd_hw.dispdata[OSD1].y_end) {
			if (y < osd_hw.dispdata[OSD1].y_end)
				osd_hw.pandata[OSD2].y_end = osd_hw.dispdata[OSD1].y_end - y;
		} else {
			osd_hw.pandata[OSD2].y_end = osd_h - 1;
		}
	}
	osd_hw.dispdata[OSD2].x_end = osd_hw.dispdata[OSD2].x_start + osd_hw.pandata[OSD2].x_end - osd_hw.pandata[OSD2].x_start;
	osd_hw.dispdata[OSD2].y_end = osd_hw.dispdata[OSD2].y_start + osd_hw.pandata[OSD2].y_end - osd_hw.pandata[OSD2].y_start;
	add_to_update_list(OSD2,DISP_GEOMETRY);
	
	osd_wait_vsync_hw();
}
#endif //CONFIG_FB_OSD2_CURSOR

void  osd_suspend_hw(void)
{
	u32 i,j;
	u32 data;
	u32  *preg;
	
#ifndef FIQ_VSYNC
	//free irq ,we can not disable it ,maybe video still use it .
	free_irq(INT_VIU_VSYNC,(void *)osd_setup);
#else
    	unregister_fiq_bridge_handle(&osd_hw.fiq_handle_item);
#endif
	//save all status
	osd_hw.reg_status=(u32*)kmalloc(sizeof(u32)*RESTORE_MEMORY_SIZE,GFP_KERNEL);
	if(IS_ERR (osd_hw.reg_status))
	{
		amlog_level(LOG_LEVEL_HIGH,"can't alloc restore memory\r\n");
		return ;
	}
	preg=osd_hw.reg_status;
	for(i=0;i<ARRAY_SIZE(reg_index);i++)
	{
		switch(reg_index[i])
		{
			case VPP_MISC:
			data=READ_MPEG_REG(VPP_MISC);
			*preg=data&OSD_RELATIVE_BITS; //0x333f0 is osd0&osd1 relative bits
			WRITE_MPEG_REG(VPP_MISC,data&(~OSD_RELATIVE_BITS));
			break;
			case VIU_OSD1_BLK0_CFG_W4:
			data=READ_MPEG_REG(VIU_OSD1_BLK0_CFG_W4);
			*preg=data;
			data=READ_MPEG_REG(VIU_OSD2_BLK0_CFG_W4);
			*(preg+OSD1_OSD2_SOTRE_OFFSET)=data;
			break;
			case VIU_OSD1_COLOR_ADDR: //resotre palette value
			for(j=0;j<256;j++)
			{
				WRITE_MPEG_REG(VIU_OSD1_COLOR_ADDR, 1<<8|j);
				*preg=READ_MPEG_REG(VIU_OSD1_COLOR);
				WRITE_MPEG_REG(VIU_OSD1_COLOR_ADDR+REG_OFFSET, 1<<8|j);
				*(preg+OSD1_OSD2_SOTRE_OFFSET)=READ_MPEG_REG(VIU_OSD1_COLOR+REG_OFFSET);
				preg++;
			}
			break;
			default :
			data=READ_MPEG_REG(reg_index[i]);
			*preg=data;
			break;
		}
		preg++;
	}
    printk("osd_suspend\n");
	//disable osd relative clock
	return ;
	
}
void osd_resume_hw(void)
{
	u32 i,j;
	u32  *preg;

    printk("osd_resume\n");
	// enable osd relative clock	
	//restore status
	if(osd_hw.reg_status)
	{
		preg=osd_hw.reg_status;
		for(i=0;i<ARRAY_SIZE(reg_index);i++)
		{
			switch(reg_index[i])
			{
	       			case VPP_MISC:
	       			SET_MPEG_REG_MASK(VPP_MISC,*preg);
				break;
				case VIU_OSD1_BLK0_CFG_W4:
				WRITE_MPEG_REG(VIU_OSD1_BLK0_CFG_W4,*preg);
				WRITE_MPEG_REG(VIU_OSD2_BLK0_CFG_W4,*(preg+OSD1_OSD2_SOTRE_OFFSET));
				break;
				case VIU_OSD1_COLOR_ADDR: //resotre palette value
				for(j=0;j<256;j++)
				{
					WRITE_MPEG_REG(VIU_OSD1_COLOR_ADDR, j);
					WRITE_MPEG_REG(VIU_OSD1_COLOR,*preg);
					WRITE_MPEG_REG(VIU_OSD1_COLOR_ADDR+REG_OFFSET, j);
					WRITE_MPEG_REG(VIU_OSD1_COLOR+REG_OFFSET,*(preg+OSD1_OSD2_SOTRE_OFFSET));
					preg++;
				}
				break;
				default :
				WRITE_MPEG_REG(reg_index[i],*preg);
				break;
			}
			preg++;
		}
		kfree(osd_hw.reg_status);
		// osd relative clock	
	}
	
#ifdef FIQ_VSYNC
	if(register_fiq_bridge_handle(&osd_hw.fiq_handle_item))	
#else
	if ( request_irq(INT_VIU_VSYNC, &vsync_isr,
		IRQF_SHARED , "am_osd_vsync", osd_setup))
#endif
	{
		amlog_level(LOG_LEVEL_HIGH,"can't request irq when osd resume\r\n");
	}

	return ;
}

