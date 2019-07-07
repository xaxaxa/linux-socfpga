/*
 * Simplest possible simple frame-buffer driver, as a platform device
 *
 * Copyright (c) 2013, Stephen Warren
 *
 * Based on q40fb.c, which was:
 * Copyright (C) 2001 Richard Zidlicky <rz@linux-m68k.org>
 *
 * Also based on offb.c, which was:
 * Copyright (C) 1997 Geert Uytterhoeven
 * Copyright (C) 1996 Paul Mackerras
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/io.h>
#include <linux/module.h>
#include "xaxaxafb.h"
#include <linux/platform_device.h>
#include <linux/dma-contiguous.h>
#include <linux/dma-mapping.h>


//module parameters
int default_width=1024,default_height=768;


static struct fb_fix_screeninfo xaxaxafb_fix = {
	.id		= "xaxaxa",
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_TRUECOLOR,
	.accel		= FB_ACCEL_NONE,
};

static struct fb_var_screeninfo xaxaxafb_var = {
	.height		= -1,
	.width		= -1,
	.activate	= FB_ACTIVATE_NOW,
	.vmode		= FB_VMODE_NONINTERLACED,
};

static int xaxaxafb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			      u_int transp, struct fb_info *info) {
	u32 *pal = info->pseudo_palette;
	u32 cr = red >> (16 - info->var.red.length);
	u32 cg = green >> (16 - info->var.green.length);
	u32 cb = blue >> (16 - info->var.blue.length);
	u32 value;

	if (regno >= 16)
		return -EINVAL;

	value = (cr << info->var.red.offset) |
		(cg << info->var.green.offset) |
		(cb << info->var.blue.offset);
	if (info->var.transp.length > 0) {
		u32 mask = (1 << info->var.transp.length) - 1;
		mask <<= info->var.transp.offset;
		value |= mask;
	}
	pal[regno] = value;

	return 0;
}
static void xaxaxafb_freefb(struct fb_info* info) {
	dma_free_coherent(info->dev,info->fix.smem_len,info->screen_base,info->fix.smem_start);
}
static int xaxaxafb_allocfb(void** out_vaddr,dma_addr_t* out_addr,struct fb_info* info,int fbsize) {
	//ugly hack; there seems no documentation about why dev.dma_mask is a pointer at all, and
	//what it is supposed to point to (who manages the allocation, etc)
	int ret;
	printk(KERN_WARNING "xaxaxafb: info->dev=%x\n",(u32)info->dev);
	info->dev->dma_mask=&(info->dev->coherent_dma_mask);
	if((ret=dma_set_mask_and_coherent(info->dev,DMA_BIT_MASK(32)))!=0) {
		printk(KERN_WARNING "xaxaxafb: dma_set_mask_and_coherent() failed: %i\n",ret);
	}
	*out_vaddr=dma_alloc_coherent(info->dev,fbsize,out_addr,GFP_KERNEL);
	if (*out_vaddr==NULL) {
		dev_err(info->dev, "framebuffer memory allocation failed; size=%i\n",fbsize);
		return -ENOMEM;
	}
	return 0;
}
static void xaxaxafb_setfbaddr(struct fb_info* info,void* vaddr,dma_addr_t addr,int fbsize) {
	//int w=info->var.xres_virtual;
	//int y=info->var.yres_virtual;
	printk(KERN_WARNING "xaxaxafb: fb location: %lx\n",addr);
	info->fix.smem_start = addr;
	info->fix.smem_len = fbsize;
	info->apertures->ranges[0].base = info->fix.smem_start;
	info->apertures->ranges[0].size = info->fix.smem_len;
	info->screen_base = vaddr;
	//info->screen_size = fbsize;
}
static void xaxaxafb_destroy(struct fb_info *info) {
	if (info->screen_base) xaxaxafb_freefb(info);
}
static int xaxaxafb_check_var(struct fb_var_screeninfo *var, struct fb_info *info) {
	printk(KERN_WARNING "xaxaxafb_check_var()\n");
	if (var->vmode & FB_VMODE_CONUPDATE) {
		var->xoffset = info->var.xoffset;
		var->yoffset = info->var.yoffset;
	}
	/*
	 *  Some very basic checks
	 */
	if (!var->xres)
		var->xres = 1;
	if (!var->yres)
		var->yres = 1;
	if (var->xres > var->xres_virtual)
		var->xres_virtual = var->xres;
	if (var->yres > var->yres_virtual)
		var->yres_virtual = var->yres;
	var->bits_per_pixel = 32;

	if (var->xres_virtual < var->xoffset + var->xres)
		var->xres_virtual = var->xoffset + var->xres;
	if (var->yres_virtual < var->yoffset + var->yres)
		var->yres_virtual = var->yoffset + var->yres;

	/*
	 * Now that we checked it we alter var. The reason being is that the video
	 * mode passed in might not work but slight changes to it might make it 
	 * work. This way we let the user know what is acceptable.
	 */
	var->red.offset = 0;
	var->red.length = 8;
	var->green.offset = 8;
	var->green.length = 8;
	var->blue.offset = 16;
	var->blue.length = 8;
	var->transp.offset = 0;
	var->transp.length = 0;
	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;
	return 0;
}

/* This routine actually sets the video mode. It's in here where we
 * the hardware state info->par and fix which can be affected by the 
 * change in par. For this driver it doesn't do much. 
 */
static int xaxaxafb_set_par(struct fb_info *info) {
	int fbsize=info->var.xres_virtual*info->var.yres_virtual*4;
	void* vaddr;
	dma_addr_t addr;
	if(xaxaxafb_allocfb(&vaddr,&addr,info,fbsize)<0) {
		return -ENOMEM;
	}
	xaxaxafb_freefb(info);
	xaxaxafb_setfbaddr(info,vaddr,addr,fbsize);
	info->fix.line_length=4*info->var.xres_virtual;
	return 0;
}

static int xaxaxafb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info) {
	if (var->vmode & FB_VMODE_YWRAP) {
		if (var->yoffset >= info->var.yres_virtual ||
		    var->xoffset)
			return -EINVAL;
	} else {
		if (var->xoffset + info->var.xres > info->var.xres_virtual ||
		    var->yoffset + info->var.yres > info->var.yres_virtual)
			return -EINVAL;
	}
	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;
	if (var->vmode & FB_VMODE_YWRAP)
		info->var.vmode |= FB_VMODE_YWRAP;
	else
		info->var.vmode &= ~FB_VMODE_YWRAP;
	return 0;
}

static struct fb_ops xaxaxafb_ops = {
	.owner		= THIS_MODULE,
	.fb_destroy	= xaxaxafb_destroy,
	.fb_setcolreg	= xaxaxafb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_check_var	= xaxaxafb_check_var,
	.fb_set_par		= xaxaxafb_set_par,
	.fb_pan_display	= xaxaxafb_pan_display,
};

static struct xaxaxafb_format xaxaxafb_formats[] = XAXAXAFB_FORMATS;

struct xaxaxafb_params {
	u32 width;
	u32 height;
	u32 stride;
	struct xaxaxafb_format *format;
};

static int xaxaxafb_probe(struct platform_device *pdev) {
	printk(KERN_WARNING "xaxaxafb_probe\n");
	int ret;
	struct xaxaxafb_params params;
	struct fb_info *info;
	
	params.width=default_width;
	params.height=default_height;
	int fbsize=params.width*params.height*4;
	params.format=&xaxaxafb_formats[0];
	params.stride=4*params.width;
	
	ret = 0;
	
	info = framebuffer_alloc(sizeof(u32) * 16, &pdev->dev);
	if (!info) return -ENOMEM;
	platform_set_drvdata(pdev, info);
	info->dev=&pdev->dev;
	info->fix = xaxaxafb_fix;
	info->fix.line_length = params.stride;
	info->var = xaxaxafb_var;
	info->var.xres = params.width;
	info->var.yres = params.height;
	info->var.xres_virtual = params.width;
	info->var.yres_virtual = params.height;
	info->var.bits_per_pixel = params.format->bits_per_pixel;
	info->var.red = params.format->red;
	info->var.green = params.format->green;
	info->var.blue = params.format->blue;
	info->var.transp = params.format->transp;
	info->apertures = alloc_apertures(1);
	if (!info->apertures) {
		framebuffer_release(info); return -ENOMEM;
	}
	void* vaddr;
	dma_addr_t addr;
	if(xaxaxafb_allocfb(&vaddr,&addr,info,fbsize)<0) {
		framebuffer_release(info); return -ENOMEM;
	}
	xaxaxafb_setfbaddr(info,vaddr,addr,fbsize);
	
	info->fbops = &xaxaxafb_ops;
	info->flags = FBINFO_DEFAULT | FBINFO_MISC_FIRMWARE;
	info->pseudo_palette = (void *)(info + 1);

	dev_info(&pdev->dev, "framebuffer at 0x%lx, 0x%x bytes, mapped to 0x%p\n",
			     info->fix.smem_start, info->fix.smem_len,
			     info->screen_base);
	dev_info(&pdev->dev, "format=%s, mode=%dx%dx%d, linelength=%d\n",
			     params.format->name,
			     info->var.xres, info->var.yres,
			     info->var.bits_per_pixel, info->fix.line_length);

	ret = register_framebuffer(info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to register xaxaxafb: %d\n", ret);
		framebuffer_release(info);
		return ret;
	}
	dev_info(&pdev->dev, "fb%d: xaxaxafb registered!\n", info->node);
	return 0;
}
static int xaxaxafb_remove(struct platform_device *pdev) {
	struct fb_info *info = platform_get_drvdata(pdev);
	unregister_framebuffer(info);
	framebuffer_release(info);
	return 0;
}

static struct platform_driver xaxaxafb_driver = {
	.driver = {
		.name = "xaxaxafb",
		//.owner = THIS_MODULE,
	},
	.probe = xaxaxafb_probe,
	.remove = xaxaxafb_remove,
};

static struct platform_device* xaxaxafb_device;
static int __init xaxaxafb_init(void)
{
	int ret = 0;
	ret = platform_driver_register(&xaxaxafb_driver);

	if (!ret) {
		xaxaxafb_device = platform_device_alloc("xaxaxafb", 0);

		if (xaxaxafb_device)
			ret = platform_device_add(xaxaxafb_device);
		else
			ret = -ENOMEM;

		if (ret) {
			platform_device_put(xaxaxafb_device);
			platform_driver_unregister(&xaxaxafb_driver);
		}
	}

	return ret;
}

module_init(xaxaxafb_init);

static void __exit xaxaxafb_exit(void)
{
	platform_device_unregister(xaxaxafb_device);
	platform_driver_unregister(&xaxaxafb_driver);
}

module_exit(xaxaxafb_exit);


module_param(default_width, int, 0444);
MODULE_PARM_DESC(default_width, "default width of the framebuffer in pixels");

module_param(default_height, int, 0444);
MODULE_PARM_DESC(default_height, "default height of the framebuffer in pixels");

MODULE_AUTHOR("Stephen Warren <swarren@wwwdotorg.org>");
MODULE_DESCRIPTION("Simple framebuffer driver");
MODULE_LICENSE("GPL v2");
