/****************************************************************************
 * apps/examples/dma2d/dma2d_main.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#include <nuttx/video/fb.h>
#include <nuttx/video/rgbcolors.h>

/****************************************************************************
 * Preprocessor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct dma2d_overlay_s
{
    uint8_t fmt;                    /* DMA2D pixel format */
    uint8_t transp_mode;            /* DMA2D transparency mode */
    fb_coord_t xres;                /* X-resolution overlay */
    fb_coord_t yres;                /* Y-resolution overlay */
    struct fb_overlayinfo_s *oinfo; /* Framebuffer overlay information */
};

struct dma2d_layer_s
{
/*   Set the cmap table for both foreground and background layer.
 *   Up to 256 colors supported.
 */

#ifdef CONFIG_FB_CMAP
    int (*setclut) (FAR const struct fb_cmap_s * cmap);
    int (*getclut) (FAR const struct fb_cmap_s * cmap);
#endif

#ifdef CONFIG_DMA2D_FB_COLORDITHER
    int (*setdither) (FAR const struct fb_cdithermap_s * cmap);
#endif

/*   Fill a specific memory region with a color.
 *   The caller must ensure that the memory region (area) is
 *   within the entire overlay.
 */

    int (*fillcolor) (FAR struct dma2d_overlay_s * oinfo,
              FAR const struct fb_area_s * area, uint32_t argb);

/* Copies memory from a source overlay (defined by sarea) to destination
 *   overlay position (defined by destxpos and destypos) without pixelformat
 *   conversion. The caller must ensure that the memory region (area) is
 *   within the entire overlay.
 */

    int (*blit) (FAR struct dma2d_overlay_s * doverlay,
             FAR const struct fb_area_s * darea,
             FAR struct dma2d_overlay_s * soverlay,
             FAR const struct fb_area_s * sarea);

/* Blends two source memory areas to a destination memory area with
 *   pixelformat conversion if necessary. The caller must ensure that the
 *   memory region (area) is within the entire overlays.
 */

    int (*blend) (FAR struct dma2d_overlay_s * doverlay,
              FAR const struct fb_area_s * darea,
              FAR struct dma2d_overlay_s * foverlay,
              FAR const struct fb_area_s * farea,
              FAR struct dma2d_overlay_s * boverlay,
              FAR const struct fb_area_s * barea);
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: usage
 *
 * Description:
 *   Commandline info
 *
 * Parameters:
 *   progname - Name of the program
 *
 ****************************************************************************/

static void usage(const char * progname)
{
  fprintf(stderr,
          "usage: %s <option> -d <fbdev>\n"
          "Note:overlay number is fixed to 0\n"
          "\n"
          "  -vinfo\n"
          "  -pinfo\n"
          "  -oinfo\n"
          "  -fill <color> <xpos> <ypos> <xres> <yres>\n"
          "  -blit <dxpos> <dypos> <dxres> <dyres>\n"
          "        <sxpos> <sypos> <sxres> <syres>\n"
          "  -blend <dxpos> <dypos> <dxres> <dyres>\n"
          "         <fxpos> <fypos> <fxres> <fyres>\n"
          "         <bxpos> <bypos> <bxres> <byres>\n"
          "\n"
          "  -d <fbdev> optional, default: \"/dev/fb0\"\n",
          progname);
}

/****************************************************************************
 * Name: fbopen
 *
 * Description:
 *   Open framebuffer device
 *
 * Parameters:
 *   device - Path to framebuffer device
 *
 * Return:
 *   Open filehandle to framebuffer device or ERROR when failed
 ****************************************************************************/

static int fbopen(const char * device)
{
  int fb = open(device, O_RDWR);

  if (fb < 0)
    {
      fprintf(stderr, "Unable to open framebuffer device: %s\n", device);
      return EXIT_FAILURE;
    }

  return fb;
}

/****************************************************************************
 * Name: print_video_info
 *
 * Description:
 *   Prints video information
 *
 * Parameters:
 *   fb        - Open framebuffer filehandle
 *
 ****************************************************************************/

static int print_video_info(int fb)
{
  int ret;
  struct fb_videoinfo_s vinfo;

  ret = ioctl(fb, FBIOGET_VIDEOINFO, (unsigned long)(uintptr_t)&vinfo);
  if (ret != OK)
    {
      fprintf(stderr, "Unable to get video information\n");
      return EXIT_FAILURE;
    }

  printf("VideoInfo:\n"
         "      fmt: %u\n"
         "     xres: %u\n"
         "     yres: %u\n"
         "  nplanes: %u\n"
         "noverlays: %u\n",
         vinfo.fmt, vinfo.xres, vinfo.yres, vinfo.nplanes, vinfo.noverlays);

  ret = EXIT_SUCCESS;
}

/****************************************************************************
 * Name: print_plane_info
 *
 * Description:
 *   Prints plane information
 *
 * Parameters:
 *   fb - Open framebuffer filehandle
 *
 ****************************************************************************/

static int print_plane_info(int fb)
{
  int ret;
  struct fb_planeinfo_s pinfo;

  ret = ioctl(fb, FBIOGET_PLANEINFO, (unsigned long)(uintptr_t)&pinfo);
  if (ret != OK)
    {
      fprintf(stderr, "Unable to get plane information\n");
      return EXIT_FAILURE;
    }

  printf("PlaneInfo:\n"
         "    fbmem: %p\n"
         "    fblen: %zu\n"
         "   stride: %u\n"
         "  display: %u\n"
         "      bpp: %u\n",
         pinfo.fbmem, pinfo.fblen, pinfo.stride,
         pinfo.display,
         pinfo.bpp);

  return EXIT_SUCCESS;
}

/****************************************************************************
 * Name: print_overlay_info
 *
 * Description:
 *   Prints overlay information
 *
 * Parameters:
 *   fb        - Open framebuffer filehandle
 *   overlayno - Overlay number
 *
 ****************************************************************************/

static int print_overlay_info(int fb, uint8_t overlayno)
{
  int ret;
  struct fb_overlayinfo_s oinfo;

  /* Select overlay to compare fbmem from overlayinfo and the one by mmap */

  ret = ioctl(fb, FBIO_SELECT_OVERLAY, overlayno);
  if (ret != OK)
    {
      fprintf(stderr, "Unable to select overlay: %d\n", overlayno);
      return EXIT_FAILURE;
    }

  oinfo.overlay = overlayno;
  ret = ioctl(fb, FBIOGET_OVERLAYINFO, (unsigned long)(uintptr_t)&oinfo);
  if (ret != OK)
    {
      fprintf(stderr, "Unable to get overlay information\n");
      return EXIT_FAILURE;
    }

  printf("OverlayInfo:\n"
         "    fbmem: %p\n"
         "    fblen: %zu\n"
         "   stride: %u\n"
         "  overlay: %u\n"
         "      bpp: %u\n"
         "    blank: %08x\n"
         "chromakey: %08" PRIx32 "\n"
         "    color: %08" PRIx32 "\n"
         "   transp: %02x\n"
         "     mode: %08x\n"
         "     accl: %08" PRIx32 "\n",
         oinfo.fbmem, oinfo.fblen, oinfo.stride,
         oinfo.overlay,
         oinfo.bpp, oinfo.blank, oinfo.chromakey, oinfo.color,
         oinfo.transp.transp, oinfo.transp.transp_mode, oinfo.accl);

      return EXIT_SUCCESS;
}

/****************************************************************************
 * Name: dma2d_fillcolor
 *
 * Description:
 *   Fill the overlay area with a user defined color
 *
 * Parameters:
 *   fb        - Open framebuffer filehandle
 *   overlayno - Overlay number
 *   color     - color
 *   area      - the area need to fill, it's width should be 4 bytes align
 *
 ****************************************************************************/

static int dma2d_fillcolor(int fb, uint8_t overlayno, uint32_t color,
                        FAR const struct fb_area_s * area)
{
  int ret;
  struct fb_videoinfo_s vinfo;
  struct fb_overlayinfo_s oinfo;

  ret = ioctl(fb, FBIOGET_VIDEOINFO, (unsigned long)((uintptr_t)&vinfo));
  if (ret < 0)
    {
      fprintf(stderr, "Unable to get video info\n");
      return EXIT_FAILURE;
    }

  ret = ioctl(fb, FBIO_SELECT_OVERLAY, overlayno);
  if (ret != OK)
    {
      fprintf(stderr, "Unable to select overlay: %d\n", overlayno);
      return EXIT_FAILURE;
    }

  oinfo.overlay = overlayno;
  ret = ioctl(fb, FBIOGET_OVERLAYINFO, (unsigned long)((uintptr_t)&oinfo));
  if (ret < 0)
    {
      fprintf(stderr, "Unable to get overlay info\n");
      return EXIT_FAILURE;
    }

  FAR void *fbmem = mmap(NULL, oinfo.fblen, PROT_READ | PROT_WRITE,
               MAP_SHARED | MAP_FILE, fb, 0);
  if (fbmem == MAP_FAILED)
    {
      fprintf(stderr, "ERROR: ioctl(FBIO_MMAP) failed\n");
      return EXIT_FAILURE;
    }
  else
    {
      dma2dinitialize();
      struct dma2d_overlay_s dma2d_info;
      dma2d_info.fmt = vinfo.fmt;
      dma2d_info.transp_mode = 0;
      dma2d_info.xres = vinfo.xres;
      dma2d_info.yres = vinfo.yres;
      dma2d_info.oinfo = &oinfo;
      dma2d_info.oinfo->transp.transp_mode = 0;
      dma2d_info.oinfo->transp.transp = 100;
      dma2d_info.oinfo->fbmem = fbmem;

      struct dma2d_layer_s *dma2dapi = dma2ddev();
      dma2dapi->fillcolor(&dma2d_info, area, color);

      munmap(fbmem, oinfo.fblen);

      ret = EXIT_SUCCESS;
    }

  return ret;
}

/****************************************************************************
 * Name: dma2d_blit
 *
 * Description:
 *   Blit content from source to destination overlay
 *
 * Parameters:
 *   fb         - Open framebuffer filehandle
 *   blit       - Blit control information
 *
 ****************************************************************************/

static int dma2d_blit(int fb, FAR struct fb_overlayblit_s *blit)
{
  int ret;
  struct fb_videoinfo_s vinfo;
  struct fb_overlayinfo_s oinfo;
  uint8_t overlayno = blit->dest.overlay;

  ret = ioctl(fb, FBIOGET_VIDEOINFO, (unsigned long)((uintptr_t)&vinfo));
  if (ret < 0)
    {
      fprintf(stderr, "Unable to get video info\n");
      return EXIT_FAILURE;
    }

  ret = ioctl(fb, FBIO_SELECT_OVERLAY, overlayno);
  if (ret != OK)
    {
      fprintf(stderr, "Unable to select overlay: %d\n", overlayno);
      return EXIT_FAILURE;
    }

  oinfo.overlay = overlayno;
  ret = ioctl(fb, FBIOGET_OVERLAYINFO, (unsigned long)((uintptr_t)&oinfo));
  if (ret < 0)
    {
      fprintf(stderr, "Unable to get overlay info\n");
      return EXIT_FAILURE;
    }

  FAR void *fbmem = mmap(NULL, oinfo.fblen, PROT_READ | PROT_WRITE,
               MAP_SHARED | MAP_FILE, fb, 0);
  if (fbmem == MAP_FAILED)
    {
      fprintf(stderr, "ERROR: ioctl(FBIO_MMAP) failed\n");
      return EXIT_FAILURE;
    }
  else
    {
      dma2dinitialize();
      struct dma2d_overlay_s dma2d_info;
      dma2d_info.fmt = vinfo.fmt;
      dma2d_info.transp_mode = 0;
      dma2d_info.xres = vinfo.xres;
      dma2d_info.yres = vinfo.yres;
      dma2d_info.oinfo = &oinfo;
      dma2d_info.oinfo->transp.transp_mode = 0;
      dma2d_info.oinfo->transp.transp = 100;
      dma2d_info.oinfo->fbmem = fbmem;

      struct dma2d_layer_s *dma2dapi = dma2ddev();
      dma2dapi->blit(&dma2d_info, &(blit->dest.area),
                    &dma2d_info, &(blit->src.area));

      munmap(fbmem, oinfo.fblen);

      return EXIT_SUCCESS;
    }

  return ret;
}

/****************************************************************************
 * Name: dma2d_blend
 *
 * Description:
 *   Blend content from foreground and background overlay to a destination
 *   overlay
 *
 * Parameters:
 *   fb         - Open framebuffer filehandle
 *   blend      - Blend operation information
 *
 ****************************************************************************/

static int dma2d_blend(int fb, FAR struct fb_overlayblend_s *blend)
{
  int ret;
  struct fb_videoinfo_s vinfo;
  struct fb_overlayinfo_s oinfo;
  uint8_t overlayno = blend->dest.overlay;

  ret = ioctl(fb, FBIOGET_VIDEOINFO, (unsigned long)((uintptr_t)&vinfo));
  if (ret < 0)
    {
      fprintf(stderr, "Unable to get video info\n");
      return EXIT_FAILURE;
    }

  ret = ioctl(fb, FBIO_SELECT_OVERLAY, overlayno);
  if (ret != OK)
    {
      fprintf(stderr, "Unable to select overlay: %d\n", overlayno);
      return EXIT_FAILURE;
    }

  oinfo.overlay = overlayno;
  ret = ioctl(fb, FBIOGET_OVERLAYINFO, (unsigned long)((uintptr_t)&oinfo));
  if (ret < 0)
    {
      fprintf(stderr, "Unable to get overlay info\n");
      return EXIT_FAILURE;
    }

  FAR void *fbmem = mmap(NULL, oinfo.fblen, PROT_READ | PROT_WRITE,
               MAP_SHARED | MAP_FILE, fb, 0);
  if (fbmem == MAP_FAILED)
    {
      fprintf(stderr, "ERROR: ioctl(FBIO_MMAP) failed\n");
      ret = EXIT_FAILURE;
    }
  else
    {
      dma2dinitialize();
      struct dma2d_overlay_s dma2d_info;
      dma2d_info.fmt = vinfo.fmt;
      dma2d_info.transp_mode = 0;
      dma2d_info.xres = vinfo.xres;
      dma2d_info.yres = vinfo.yres;
      dma2d_info.oinfo = &oinfo;
      dma2d_info.oinfo->transp.transp_mode = 0;
      dma2d_info.oinfo->transp.transp = 100;
      dma2d_info.oinfo->fbmem = fbmem;

      struct dma2d_layer_s *dma2dapi = dma2ddev();
      dma2dapi->blend(&dma2d_info, &(blend->dest.area),
                      &dma2d_info, &(blend->foreground.area),
                      &dma2d_info, &(blend->background.area));

      munmap(fbmem, oinfo.fblen);
      ret = EXIT_SUCCESS;
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * fb_main
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  char *fbdevice;
  int  fd = -1;
  int ret = EXIT_FAILURE;

  if (argc < 2)
    {
      usage(argv[0]);
      return EXIT_FAILURE;
    }

  if (argc >= 2 && !strcmp(argv[argc - 2], "-d"))
    {
      fbdevice = argv[argc - 1];
    }
  else
    {
      fbdevice = "/dev/fb0";
    }

  fd = fbopen(fbdevice);
  if (fd < 0)
    {
      printf("Error, fail to open fb device %s!", fbdevice);
      return EXIT_FAILURE;
    }

  if (!strcmp(argv[1], "-vinfo"))
    {
      ret = print_video_info(fd);
    }
  else if (!strcmp(argv[1], "-pinfo"))
    {
      ret = print_plane_info(fd);
    }
  else if (!strcmp(argv[1], "-oinfo"))
    {
      int overlayno = 0;
      ret = print_overlay_info(fd, overlayno);
    }
  else if (!strcmp(argv[1], "-fill") && argc >= 7)
    {
      struct fb_area_s area;
      int overlayno = 0;
      uint32_t argb = strtoul(argv[2], NULL, 16);

      area.x = strtoul(argv[3], NULL, 10);
      area.y = strtoul(argv[4], NULL, 10);
      area.w = strtoul(argv[5], NULL, 10);
      area.h = strtoul(argv[6], NULL, 10);

      printf("fill: 0x%x, %d, %d, %d, %d\n",
            argb, area.x, area.y, area.w, area.h);

      ret = dma2d_fillcolor(fd, overlayno, argb, &area);
    }
  else if (!strcmp(argv[1], "-blit") && argc >= 10)
    {
      struct fb_overlayblit_s blit;

      blit.dest.overlay = 0;
      blit.dest.area.x  = strtoul(argv[2], NULL, 10);
      blit.dest.area.y  = strtoul(argv[3], NULL, 10);
      blit.dest.area.w  = strtoul(argv[4], NULL, 10);
      blit.dest.area.h  = strtoul(argv[5], NULL, 10);

      blit.src.overlay = 0;
      blit.src.area.x  = strtoul(argv[6], NULL, 10);
      blit.src.area.y  = strtoul(argv[7], NULL, 10);
      blit.src.area.w  = strtoul(argv[8], NULL, 10);
      blit.src.area.h  = strtoul(argv[9], NULL, 10);

      printf("blit, dest: %d, %d, %d, %d\n",
            blit.dest.area.x, blit.dest.area.y,
            blit.dest.area.w, blit.dest.area.h);
      printf("blit, src: %d, %d, %d, %d\n",
            blit.src.area.x, blit.src.area.y,
            blit.src.area.w, blit.src.area.h);

      ret = dma2d_blit(fd, &blit);
    }
  else if (!strcmp(argv[1], "-blend") && argc >= 14)
    {
      struct fb_overlayblend_s blend;

      blend.dest.overlay       = 0;
      blend.dest.area.x        = strtoul(argv[2], NULL, 10);
      blend.dest.area.y        = strtoul(argv[3], NULL, 10);
      blend.dest.area.w        = strtoul(argv[4], NULL, 10);
      blend.dest.area.h        = strtoul(argv[5], NULL, 10);

      blend.foreground.overlay = 0;
      blend.foreground.area.x  = strtoul(argv[6], NULL, 10);
      blend.foreground.area.y  = strtoul(argv[7], NULL, 10);
      blend.foreground.area.w  = strtoul(argv[8], NULL, 10);
      blend.foreground.area.h  = strtoul(argv[9], NULL, 10);

      blend.background.overlay = 0;
      blend.background.area.x  = strtoul(argv[10], NULL, 10);
      blend.background.area.y  = strtoul(argv[11], NULL, 10);
      blend.background.area.w  = strtoul(argv[12], NULL, 10);
      blend.background.area.h  = strtoul(argv[13], NULL, 10);

      printf("blend, dest: %d, %d, %d, %d\n",
            blend.dest.area.x, blend.dest.area.y,
            blend.dest.area.w, blend.dest.area.h);
      printf("blend, fg: %d, %d, %d, %d\n",
            blend.foreground.area.x, blend.foreground.area.y,
            blend.foreground.area.w, blend.foreground.area.h);
      printf("blend, bg: %d, %d, %d, %d\n",
            blend.background.area.x, blend.background.area.y,
            blend.background.area.w, blend.background.area.h);

      ret = dma2d_blend(fd, &blend);
    }

  if (fd) close(fd);
  return EXIT_SUCCESS;
}
