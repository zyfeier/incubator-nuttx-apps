/**
 * @file lv_jpeg_turbo.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_jpeg_turbo.h"
#include "lvgl/lvgl.h"
#include <stdio.h>
#include <jpeglib.h>
#include <setjmp.h>
#ifdef CONFIG_LV_USE_GPU_INTERFACE
#include "lv_porting/gpu/lv_gpu_decoder.h"
#endif

/*********************
 *      DEFINES
 *********************/
#define JPEG_DEC_PIXEL_SIZE 3

/**********************
 *      TYPEDEFS
 **********************/
typedef struct error_mgr_s {
    struct jpeg_error_mgr pub;
    jmp_buf jb;
} error_mgr_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static lv_res_t decoder_info(lv_img_decoder_t * decoder, const void * src, lv_img_header_t * header);
static lv_res_t decoder_open(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc);
static void decoder_close(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc);
static lv_color_t * open_jpeg_file(const char * filename);
static bool get_jpeg_size(const char * filename, uint32_t * width, uint32_t * height);
static void convert_color_depth(lv_color_t * dest_buf, const uint8_t * src_buf, uint32_t px_cnt);
static void error_exit(j_common_ptr cinfo);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Register the JPEG decoder functions in LVGL
 */
void lv_jpeg_turbo_init(void)
{
    lv_img_decoder_t * dec = lv_img_decoder_create();
    lv_img_decoder_set_info_cb(dec, decoder_info);
    lv_img_decoder_set_open_cb(dec, decoder_open);
    lv_img_decoder_set_close_cb(dec, decoder_close);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Get info about a JPEG image
 * @param src can be file name or pointer to a C array
 * @param header store the info here
 * @return LV_RES_OK: no error; LV_RES_INV: can't get the info
 */
static lv_res_t decoder_info(lv_img_decoder_t * decoder, const void * src, lv_img_header_t * header)
{
    LV_UNUSED(decoder); /*Unused*/
    lv_img_src_t src_type = lv_img_src_get_type(src);          /*Get the source type*/

    /*If it's a JPEG file...*/
    if(src_type == LV_IMG_SRC_FILE) {
        const char * fn = src;

        lv_fs_file_t f;
        lv_fs_res_t res = lv_fs_open(&f, fn, LV_FS_MODE_RD);
        if(res != LV_FS_RES_OK) return LV_RES_INV;

        uint8_t buf[10];
        uint32_t rn;
        lv_fs_read(&f, buf, sizeof(buf), &rn);
        lv_fs_close(&f);

        if(rn != sizeof(buf)) return LV_RES_INV;

        const uint8_t jpg_signature[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46};
        if(memcmp(buf, jpg_signature, sizeof(jpg_signature)) != 0) return LV_RES_INV;

        uint32_t width;
        uint32_t height;

        if(!get_jpeg_size(fn, &width, &height)) {
            return LV_RES_INV;
        }

        /*Save the data in the header*/
        header->always_zero = 0;
        header->cf = LV_IMG_CF_TRUE_COLOR;
        header->w = width;
        header->h = height;

        return LV_RES_OK;
    }

    return LV_RES_INV;         /*If didn't succeeded earlier then it's an error*/
}

/**
 * Open a JPEG image and return the decided image
 * @param src can be file name or pointer to a C array
 * @param style style of the image object (unused now but certain formats might use it)
 * @return pointer to the decoded image or  `LV_IMG_DECODER_OPEN_FAIL` if failed
 */
static lv_res_t decoder_open(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc)
{
    LV_UNUSED(decoder); /*Unused*/

    /*If it's a JPEG file...*/
    if(dsc->src_type == LV_IMG_SRC_FILE) {
        const char * fn = dsc->src;

        lv_color_t * img_data = open_jpeg_file(fn);

        if(img_data == NULL) {
            return LV_RES_INV;
        }

        /*Convert the image to the system's color depth*/
#ifdef CONFIG_LV_USE_GPU_INTERFACE
        lv_img_dsc_t img_dsc;
        img_dsc.header = dsc->header;
        img_dsc.data = (const uint8_t *)img_data;
        img_dsc.data_size = 0;
        const void* tmp = dsc->src;
        dsc->src = &img_dsc;
        dsc->src_type = LV_IMG_SRC_VARIABLE;
        lv_res_t res = lv_gpu_decoder_open(decoder, dsc);
        dsc->src = tmp;
        if (res == LV_RES_OK) {
            lv_mem_free(img_data);
        } else {
            dsc->img_data = (const uint8_t *)img_data;
        }
#else
        dsc->img_data = (const uint8_t *)img_data;
#endif
        return LV_RES_OK;     /*The image is fully decoded. Return with its pointer*/
    }

    return LV_RES_INV;    /*If not returned earlier then it failed*/
}

/**
 * Free the allocated resources
 */
static void decoder_close(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc)
{
    LV_UNUSED(decoder); /*Unused*/
#ifdef CONFIG_LV_USE_GPU_INTERFACE
    lv_gpu_decoder_close(decoder, dsc);
#else
    if(dsc->img_data) {
        lv_mem_free((uint8_t *)dsc->img_data);
        dsc->img_data = NULL;
    }
#endif
}

static lv_color_t * open_jpeg_file(const char * filename)
{
    /* This struct contains the JPEG decompression parameters and pointers to
     * working space (which is allocated as needed by the JPEG library).
     */
    struct jpeg_decompress_struct cinfo;
    /* We use our private extension JPEG error handler.
     * Note that this struct must live as long as the main JPEG parameter
     * struct, to avoid dangling-pointer problems.
     */
    error_mgr_t jerr;

    /* More stuff */
    FILE * infile;      /* source file */
    JSAMPARRAY buffer;  /* Output row buffer */
    int row_stride;     /* physical row width in output buffer */

    lv_color_t * output_buffer = NULL;

    /* In this example we want to open the input file before doing anything else,
     * so that the setjmp() error recovery below can assume the file is open.
     * VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
     * requires it in order to read binary files.
     */

    infile = fopen(filename, "rb");

    if(infile == NULL) {
        LV_LOG_WARN("can't open %s", filename);
        return NULL;
    }

    /* allocate and initialize JPEG decompression object */

    /* We set up the normal JPEG error routines, then override error_exit. */
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = error_exit;
    /* Establish the setjmp return context for my_error_exit to use. */
    if(setjmp(jerr.jb)) {

        LV_LOG_WARN("decoding error");

        if(output_buffer) {
            lv_mem_free(output_buffer);
        }

        /* If we get here, the JPEG code has signaled an error.
        * We need to clean up the JPEG object, close the input file, and return.
        */
        jpeg_destroy_decompress(&cinfo);
        fclose(infile);
        return NULL;
    }
    /* Now we can initialize the JPEG decompression object. */
    jpeg_create_decompress(&cinfo);

    /* specify data source (eg, a file) */

    jpeg_stdio_src(&cinfo, infile);

    /* read file parameters with jpeg_read_header() */

    jpeg_read_header(&cinfo, TRUE);

    /* We can ignore the return value from jpeg_read_header since
     *   (a) suspension is not possible with the stdio data source, and
     *   (b) we passed TRUE to reject a tables-only JPEG file as an error.
     * See libjpeg.doc for more info.
     */

    /* set parameters for decompression */

    /* In this example, we don't need to change any of the defaults set by
     * jpeg_read_header(), so we do nothing here.
     */

    /* Start decompressor */

    jpeg_start_decompress(&cinfo);

    /* We can ignore the return value since suspension is not possible
     * with the stdio data source.
     */

    /* We may need to do some setup of our own at this point before reading
     * the data.  After jpeg_start_decompress() we have the correct scaled
     * output image dimensions available, as well as the output colormap
     * if we asked for color quantization.
     * In this example, we need to make an output work buffer of the right size.
     */
    /* JSAMPLEs per row in output buffer */
    row_stride = cinfo.output_width * cinfo.output_components;
    /* Make a one-row-high sample array that will go away when done with image */
    buffer = (*cinfo.mem->alloc_sarray)
             ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

    output_buffer = lv_mem_alloc(LV_IMG_BUF_SIZE_TRUE_COLOR(cinfo.output_width, cinfo.output_height));

    LV_ASSERT_MALLOC(output_buffer);

    if(output_buffer) {
        lv_color_t * cur_pos = output_buffer;

        /* while (scan lines remain to be read) */
        /* jpeg_read_scanlines(...); */

        /* Here we use the library's state variable cinfo.output_scanline as the
         * loop counter, so that we don't have to keep track ourselves.
         */
        while(cinfo.output_scanline < cinfo.output_height) {
            /* jpeg_read_scanlines expects an array of pointers to scanlines.
             * Here the array is only one element long, but you could ask for
             * more than one scanline at a time if that's more convenient.
             */
            jpeg_read_scanlines(&cinfo, buffer, 1);

            /* Assume put_scanline_someplace wants a pointer and sample count. */

            convert_color_depth(cur_pos, buffer[0], cinfo.output_width);
            cur_pos += cinfo.output_width;
        }
    }

    /* Finish decompression */

    jpeg_finish_decompress(&cinfo);

    /* We can ignore the return value since suspension is not possible
     * with the stdio data source.
     */

    /* Release JPEG decompression object */

    /* This is an important step since it will release a good deal of memory. */
    jpeg_destroy_decompress(&cinfo);

    /* After finish_decompress, we can close the input file.
    * Here we postpone it until after no more JPEG errors are possible,
    * so as to simplify the setjmp error logic above.  (Actually, I don't
    * think that jpeg_destroy can do an error exit, but why assume anything...)
    */
    fclose(infile);

    /* At this point you may want to check to see whether any corrupt-data
    * warnings occurred (test whether jerr.pub.num_warnings is nonzero).
    */

    /* And we're done! */
    return output_buffer;
}

static bool get_jpeg_size(const char * filename, uint32_t * width, uint32_t * height)
{
    struct jpeg_decompress_struct cinfo;
    error_mgr_t jerr;

    FILE * infile = fopen(filename, "rb");

    if(infile == NULL) {
        LV_LOG_WARN("can't open %s", filename);
        return false;
    }

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = error_exit;

    if(setjmp(jerr.jb)) {
        LV_LOG_WARN("read jpeg head failed");
        jpeg_destroy_decompress(&cinfo);
        fclose(infile);
        return false;
    }

    jpeg_create_decompress(&cinfo);

    jpeg_stdio_src(&cinfo, infile);

    int ret = jpeg_read_header(&cinfo, TRUE);

    if(ret == JPEG_HEADER_OK) {
        *width = cinfo.image_width;
        *height = cinfo.image_height;
    }
    else {
        LV_LOG_WARN("read jpeg head failed: %d", ret);
    }

    jpeg_destroy_decompress(&cinfo);

    fclose(infile);

    return (ret == JPEG_HEADER_OK);
}

static void convert_color_depth(lv_color_t * dest_buf, const uint8_t * src_buf, uint32_t px_cnt)
{
    while(px_cnt--) {
        *dest_buf = lv_color_make(src_buf[0], src_buf[1], src_buf[2]);
        dest_buf++;
        src_buf += JPEG_DEC_PIXEL_SIZE;
    }
}

static void error_exit(j_common_ptr cinfo)
{
    error_mgr_t * myerr = (error_mgr_t *)cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(myerr->jb, 1);
}
