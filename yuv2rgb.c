#include <arm_neon.h>
#include <stdint.h>
#include <stdio.h>

#define LOGE(fmt, ...)  do { printf(fmt, ##__VA_ARGS__); } while(0)

static inline int16x8_t lut_8_s16(const int32_t *table, uint8x8_t idx)
{
    /*
     * 针对 idx 中的 8 个 uint8 索引，通过循环查表把 table[idx[i]] 放入一个 int16 数组中，
     * 然后再一次性使用 vld1q_s16 加载到 NEON 寄存器。
     */
    uint8_t  tmp_idx[8];
    int16_t  tmp_val[8];
    vst1_u8(tmp_idx, idx);  // 将8个索引存到内存

    for (int i = 0; i < 8; i++)
    {
        tmp_val[i] = (int16_t)table[tmp_idx[i]];
    }

    return vld1q_s16(tmp_val);
}

// 假设这几个表依旧是全局或静态可见，与原代码中相同
extern int32_t Table_fv1[256];
extern int32_t Table_fv2[256];
extern int32_t Table_fu1[256];
extern int32_t Table_fu2[256];

// 假设我们有一个结构体，和原代码中一致
struct vproc_image_info
{
    int width;
    int height;
    unsigned char* vir_base;
};

// 示例：将 NV12 转为平面 RGB，或者先转为 BGR 再做二次处理
int hal_vproc_ive_csc_neon(struct vproc_image_info *yuv_image_info,
                           struct vproc_image_info *rgb_image_info)
{
    if (yuv_image_info->width < 1 || yuv_image_info->height < 1 ||
        yuv_image_info->vir_base == NULL || rgb_image_info->vir_base == NULL)
    {
        LOGE("input pixel data is error!\n");
        return -1;
    }

    // 分辨率
    int width  = yuv_image_info->width;
    int height = yuv_image_info->height;

    // NV12: Y 数据连续，之后紧跟 UV (交错) 数据
    unsigned char *yData  = yuv_image_info->vir_base;
    unsigned char *uvData = yData + (long)width * height;

    // 用来保存最终 (B, G, R) 三通道数据的临时 buffer(行 x 列 x 3)，也可以直接写回 rgbData
    // 下面演示分两步：1) 计算并写入临时 bgr buffer；2) 重排到 planar RGB。
    // 如果想直接转成平面 R/G/B 可省略中间步骤，但代码会和原逻辑略有不同。
    static uint8_t p_data_tmp[4096*2160*3]; // 仅示例，请根据最大分辨率动态分配或使用其他方式
    uint8_t *p_data = p_data_tmp;           // 中转用

    unsigned char *rgbData = rgb_image_info->vir_base;

    // 遍历图像，每次处理 8 像素
    for (int i = 0; i < height; i++)
    {
        for (int j = 0; j < width; j += 8)
        {
            // 如果 width 不是 8 的倍数，末尾要单独处理，这里简单处理超出部分
            int valid_pixels = (j + 8 <= width) ? 8 : (width - j);

            // 1. 加载 8 个 Y
            uint8x8_t y_u8 = vld1_u8(&yData[i * width + j]);

            // 2. 找到对应的 U / V 起始位置
            //    NV12 => U 与 V 交错排列，每行占 width 字节，每2行对应同一组 UV
            int uv_row = (i >> 1);    // 对应 NV12 索引行
            int uv_col = (j >> 1) * 2;// 每2个像素对应 2 字节(U, V)
            int base_uv_idx = uv_row * width + uv_col;

            // 3. 分别加载 8 个 U / 8 个 V
            //    这一步要注意：对于 j 到 j+7 ，UV 索引不一定一一对应(因为 j/2)，但最简单的方法是逐像素。
            //    下面的示例为演示，直接从 base_uv_idx 开始取 8 字节 U + 8 字节 V，可能略有差异。
            //    若图像宽度很大，且 alignment 充足，可尝试优化对齐加载。
            //    如果对 UV 对应关系要求严格，可改用 scalar 方式 + neon_lut_8_s16() 混合。
            uint8x8_t uv_block = vld1_u8(&uvData[base_uv_idx]);

            // 从 uv_block 中奇偶拆分出 u_vec / v_vec (NV12: U在偶, V在奇)
            // 例如 uv_block = [U0, V0, U1, V1, U2, V2, U3, V3]
            // 拆分成 u_vec = [U0, U1, U2, U3, ...], v_vec = [V0, V1, V2, V3, ...]
            uint8x8x2_t uv_deinterleave = vuzp_u8(uv_block, uv_block);
            uint8x8_t u_u8 = uv_deinterleave.val[0]; // [U0, U1, U2, U3, ...]
            uint8x8_t v_u8 = uv_deinterleave.val[1]; // [V0, V1, V2, V3, ...]

            // 4. 分别进行查表：fv1, fv2, fu1, fu2
            //    结果都放到 int16x8_t (保留负数范围，后续相加/相减)
            int16x8_t fv1_s16 = lut_8_s16(Table_fv1, v_u8);
            int16x8_t fv2_s16 = lut_8_s16(Table_fv2, v_u8);
            int16x8_t fu1_s16 = lut_8_s16(Table_fu1, u_u8);
            int16x8_t fu2_s16 = lut_8_s16(Table_fu2, u_u8);

            // 5. 扩展 Y 到 int16
            int16x8_t y_s16 = vmovl_u8(y_u8); // Y ∈ [0,255]

            // 6. 用原先公式计算 B / G / R
            //    b = Y + fv1[V]
            //    g = Y - fu1[U] - fv2[V]
            //    r = Y + fu2[U]
            int16x8_t b_s16 = vaddq_s16(y_s16, fv1_s16);

            int16x8_t g_s16 = vsubq_s16(y_s16, fu1_s16);
            g_s16 = vsubq_s16(g_s16, fv2_s16);

            int16x8_t r_s16 = vaddq_s16(y_s16, fu2_s16);

            // 7. 饱和裁剪到 [0, 255]
            int16x8_t zero_s16 = vdupq_n_s16(0);
            int16x8_t max_s16  = vdupq_n_s16(255);

            b_s16 = vminq_s16(vmaxq_s16(b_s16, zero_s16), max_s16);
            g_s16 = vminq_s16(vmaxq_s16(g_s16, zero_s16), max_s16);
            r_s16 = vminq_s16(vmaxq_s16(r_s16, zero_s16), max_s16);

            // 8. 转回 uint8
            uint8x8_t b_u8_res = vmovn_u16(vreinterpretq_u16_s16(b_s16));
            uint8x8_t g_u8_res = vmovn_u16(vreinterpretq_u16_s16(g_s16));
            uint8x8_t r_u8_res = vmovn_u16(vreinterpretq_u16_s16(r_s16));

            // 9. 以 BGR 格式写入到临时 p_data (p_data[idx + 0] = B, idx + 1 = G, idx + 2 = R)
            //    若想直接写成 planar 格式，可自行拆分到 3 个平面数组
            int out_idx = (i * width + j) * 3;
            uint8_t* out_ptr = p_data + out_idx;

            // 如果恰好是8像素整倍数，可以直接用 vst3_u8。如果末尾不足8个像素，需要只处理 valid_pixels 个。
            if (valid_pixels == 8)
            {
                // 利用 NEON 的存储交织指令：将 b/g/r 交织存储
                uint8x8x3_t bgr_val;
                bgr_val.val[0] = b_u8_res;
                bgr_val.val[1] = g_u8_res;
                bgr_val.val[2] = r_u8_res;
                vst3_u8(out_ptr, bgr_val);
            }
            else
            {
                // 宽度不是 8 对齐，处理末尾像素
                for (int k = 0; k < valid_pixels; k++)
                {
                    out_ptr[3*k + 0] = vget_lane_u8(b_u8_res, k);
                    out_ptr[3*k + 1] = vget_lane_u8(g_u8_res, k);
                    out_ptr[3*k + 2] = vget_lane_u8(r_u8_res, k);
                }
            }
        }
    }

    // 下面演示：把中间的 BGR 三通道数据“打包”成平面排列(R平面, G平面, B平面)。
    // 与原代码一致的思路：三个通道依次存到 rgbData，
    // 这样： rgbData[0 .. len-1] = R, rgbData[len .. 2*len-1] = G, rgbData[2*len .. 3*len-1] = B
    // 如果你需要 BGR 其它格式，可自行调整
    long length = (long)width * height;
    int r_idx = 0;
    int g_idx = width * height;
    int b_idx = width * height * 2;

    // 可以再尝试用 NEON st3, st2 等指令做更复杂的拆分，这里简单演示标量循环。
    for (long i = 0; i < length; i++)
    {
        // p_data: B, G, R 顺序存放
        uint8_t B = p_data[i * 3 + 0];
        uint8_t G = p_data[i * 3 + 1];
        uint8_t R = p_data[i * 3 + 2];

        rgbData[r_idx++] = R;
        rgbData[g_idx++] = G;
        rgbData[b_idx++] = B;
    }

    return 0;
}