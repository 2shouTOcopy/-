#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <arm_neon.h>

// 调试输出宏
#define LOGE(fmt, ...)  do { printf(fmt, ##__VA_ARGS__); } while(0)

// 全局查找表（使用你提供的数据，这里仅给出部分示例初始化）
static int32_t Table_fv1[256] = {
    -180, -179, -177, -176, -174, -173, -172, -170,
    /* ... 剩余数据 ... */
    175, 176, 178
};

static int32_t Table_fv2[256] = {
    -92, -91, -91, -90, -89, -88, -88, -87,
    /* ... 剩余数据 ... */
    89, 90, 90
};

static int32_t Table_fu1[256] = {
    -44, -44, -44, -43, -43, -43, -42, -42,
    /* ... 剩余数据 ... */
    42, 42, 42, 43, 43
};

static int32_t Table_fu2[256] = {
    -227, -226, -224, -222, -220, -219, -217, -215,
    /* ... 剩余数据 ... */
    211, 212, 214, 216, 218, 219, 221, 223, 225
};

// 图像信息结构体，与原代码中一致
struct vproc_image_info
{
    int width;
    int height;
    unsigned char* vir_base;
};

// NEON查表函数：对8个索引进行查表，将查找结果转换为 int16x8_t 类型
static inline int16x8_t lut_8_s16(const int32_t *table, uint8x8_t idx)
{
    uint8_t  tmp_idx[8];
    int16_t  tmp_val[8];
    vst1_u8(tmp_idx, idx);
    for (int i = 0; i < 8; i++)
    {
        tmp_val[i] = (int16_t)table[tmp_idx[i]];
    }
    return vld1q_s16(tmp_val);
}

// NEON实现的 NV12 -> BGR 并转平面 RGB 的转换函数
int hal_vproc_ive_csc_neon(struct vproc_image_info *yuv_image_info,
                           struct vproc_image_info *rgb_image_info)
{
    if (yuv_image_info->width < 1 || yuv_image_info->height < 1 ||
        yuv_image_info->vir_base == NULL || rgb_image_info->vir_base == NULL)
    {
        LOGE("input pixel data is error!\n");
        return -1;
    }

    int width  = yuv_image_info->width;
    int height = yuv_image_info->height;
    unsigned char *yData  = yuv_image_info->vir_base;
    unsigned char *uvData = yData + (long)width * height;
    // 临时缓冲区，用于存放 BGR 交织数据，注意：实际应用中请根据分辨率动态分配
    static uint8_t p_data_tmp[4096*2160*3];
    uint8_t *p_data = p_data_tmp;
    unsigned char *rgbData = rgb_image_info->vir_base;

    for (int i = 0; i < height; i++)
    {
        for (int j = 0; j < width; j += 8)
        {
            int valid_pixels = (j + 8 <= width) ? 8 : (width - j);
            uint8x8_t y_u8 = vld1_u8(&yData[i * width + j]);

            int uv_row = i >> 1;
            int uv_col = (j >> 1) * 2;
            int base_uv_idx = uv_row * width + uv_col;
            uint8x8_t uv_block = vld1_u8(&uvData[base_uv_idx]);

            uint8x8x2_t uv_deinterleave = vuzp_u8(uv_block, uv_block);
            uint8x8_t u_u8 = uv_deinterleave.val[0];
            uint8x8_t v_u8 = uv_deinterleave.val[1];

            int16x8_t fv1_s16 = lut_8_s16(Table_fv1, v_u8);
            int16x8_t fv2_s16 = lut_8_s16(Table_fv2, v_u8);
            int16x8_t fu1_s16 = lut_8_s16(Table_fu1, u_u8);
            int16x8_t fu2_s16 = lut_8_s16(Table_fu2, u_u8);

            int16x8_t y_s16 = vmovl_u8(y_u8);

            int16x8_t b_s16 = vaddq_s16(y_s16, fv1_s16);
            int16x8_t g_s16 = vsubq_s16(y_s16, fu1_s16);
            g_s16 = vsubq_s16(g_s16, fv2_s16);
            int16x8_t r_s16 = vaddq_s16(y_s16, fu2_s16);

            int16x8_t zero_s16 = vdupq_n_s16(0);
            int16x8_t max_s16  = vdupq_n_s16(255);
            b_s16 = vminq_s16(vmaxq_s16(b_s16, zero_s16), max_s16);
            g_s16 = vminq_s16(vmaxq_s16(g_s16, zero_s16), max_s16);
            r_s16 = vminq_s16(vmaxq_s16(r_s16, zero_s16), max_s16);

            uint8x8_t b_u8_res = vmovn_u16(vreinterpretq_u16_s16(b_s16));
            uint8x8_t g_u8_res = vmovn_u16(vreinterpretq_u16_s16(g_s16));
            uint8x8_t r_u8_res = vmovn_u16(vreinterpretq_u16_s16(r_s16));

            int out_idx = (i * width + j) * 3;
            uint8_t* out_ptr = p_data + out_idx;

            if (valid_pixels == 8)
            {
                uint8x8x3_t bgr_val;
                bgr_val.val[0] = b_u8_res;
                bgr_val.val[1] = g_u8_res;
                bgr_val.val[2] = r_u8_res;
                vst3_u8(out_ptr, bgr_val);
            }
            else
            {
                for (int k = 0; k < valid_pixels; k++)
                {
                    out_ptr[3*k + 0] = vget_lane_u8(b_u8_res, k);
                    out_ptr[3*k + 1] = vget_lane_u8(g_u8_res, k);
                    out_ptr[3*k + 2] = vget_lane_u8(r_u8_res, k);
                }
            }
        }
    }

    // 将临时 BGR 数据拆分为平面格式：RGB分别存放
    long length = (long)width * height;
    int r_idx = 0;
    int g_idx = width * height;
    int b_idx = width * height * 2;
    for (long i = 0; i < length; i++)
    {
        uint8_t B = p_data[i * 3 + 0];
        uint8_t G = p_data[i * 3 + 1];
        uint8_t R = p_data[i * 3 + 2];
        rgbData[r_idx++] = R;
        rgbData[g_idx++] = G;
        rgbData[b_idx++] = B;
    }

    return 0;
}

// 用于测试的 main 函数
int main()
{
    // 设置一个较小的测试图片，例如 8x8 像素
    int width = 8;
    int height = 8;
    int yuv_size = width * height * 3 / 2;  // NV12 格式，Y + UV
    int rgb_size = width * height * 3;        // 平面RGB

    unsigned char *yuv_buf = malloc(yuv_size);
    unsigned char *rgb_buf = malloc(rgb_size);
    if (!yuv_buf || !rgb_buf)
    {
        printf("内存分配失败！\n");
        return -1;
    }

    // 简单初始化，Y 分量填充 128，UV 分量也填 128（纯灰图像）
    memset(yuv_buf, 128, yuv_size);
    memset(rgb_buf, 0, rgb_size);

    struct vproc_image_info yuv_image = { width, height, yuv_buf };
    struct vproc_image_info rgb_image = { width, height, rgb_buf };

    int ret = hal_vproc_ive_csc_neon(&yuv_image, &rgb_image);
    if (ret == 0)
    {
        printf("转换成功！\n");
    }
    else
    {
        printf("转换失败！\n");
    }

    // 此处可加入代码将 rgb_buf 中的平面数据保存为图片或打印部分数据进行验证

    free(yuv_buf);
    free(rgb_buf);
    return 0;
}