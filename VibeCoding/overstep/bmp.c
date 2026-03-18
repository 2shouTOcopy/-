/**
 * @note Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
 * @brief Bmp Encode Module Api
 *
 * @author luyi
 * @date   2018/05/16
 */

#include <string.h>
#include <stdio.h>
#include <arm_neon.h>
#include "bmp.h"

#ifndef COLOR_IMAGE_PIXEL_BIT
#define COLOR_IMAGE_PIXEL_BIT 	(3)
#endif

#ifndef MONO_IMAGE_PIXEL_BIT
#define MONO_IMAGE_PIXEL_BIT 	(1)
#endif

// 灰度图直接加bmp头即可
int mono8_2_bmp(BMPENCPARAM* bmp_enc_param)
{
	if(NULL == bmp_enc_param || NULL == bmp_enc_param->input_data || NULL == bmp_enc_param->output_data)
	{
		return -1;
	}

	if ((bmp_enc_param->image_pixel_bit != MONO_IMAGE_PIXEL_BIT)
		&& (bmp_enc_param->image_pixel_bit != COLOR_IMAGE_PIXEL_BIT))
	{
		return -1;
	}

	RGBQUAD rgbquad[256] = {0};
    for(int i=0;i<256;i++)
    {
        rgbquad[i].rgbBlue = i;
        rgbquad[i].rgbGreen = i;
        rgbquad[i].rgbRed = i;
        rgbquad[i].rgbReserved = 0;
    }

	// 只有1/4/8位图才会使用调色板，16/24/32位图没有调色板
	int nHaveRgbQuad = ((8 * bmp_enc_param->image_pixel_bit) <= 8) ? 1 : 0;

	unsigned int nTotalBytesPerLine = (bmp_enc_param->image_width * bmp_enc_param->image_pixel_bit + 3) / 4 * 4;  // 位图每行总的字节数（4的倍数）
    unsigned int nPixelsPerLine     = bmp_enc_param->image_width;  // 位图每行像素个数
    unsigned int nBytesPerLine = nPixelsPerLine * bmp_enc_param->image_pixel_bit;  // 位图每行实际像素对应的字节数
    unsigned int nBmBitsSize   = nTotalBytesPerLine * bmp_enc_param->image_height;  // 位图数据段总长度
    unsigned long long nInputNeed = (unsigned long long)nBytesPerLine * (unsigned long long)bmp_enc_param->image_height;

	if ((unsigned long long)bmp_enc_param->input_len < nInputNeed)
	{
		return -3;
	}
    
	int nDIBSize   = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + ((nHaveRgbQuad) ? sizeof(RGBQUAD)*256 : 0) + nBmBitsSize;
    if (bmp_enc_param->output_bufsize < nDIBSize)
    {
        bmp_enc_param->output_len = nDIBSize;
        return -2;  // 溢出
    }

    //这里写入bitmap头
    BITMAPFILEHEADER BitMapFileHeader;
    BitMapFileHeader.bfType   =   0x4D42;   //   "BM "
    BitMapFileHeader.bfSize   =   nDIBSize;
    BitMapFileHeader.bfReserved1   =   0;
    BitMapFileHeader.bfReserved2   =   0;
	BitMapFileHeader.bfOffBits     = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + ((nHaveRgbQuad) ? sizeof(RGBQUAD)*256 : 0);

    // BITMAPFILEHEADER这个结构考虑到内存对齐之后为字节
    memcpy(bmp_enc_param->output_data, &BitMapFileHeader, sizeof(BITMAPFILEHEADER));

    BITMAPINFOHEADER BitMapInfoHeader;
    BitMapInfoHeader.biSize     =   sizeof(BITMAPINFOHEADER); 
    BitMapInfoHeader.biWidth    = bmp_enc_param->image_width;//pstParam->nWidth; 
    BitMapInfoHeader.biHeight   = bmp_enc_param->image_height; 
    BitMapInfoHeader.biPlanes   =   1; 
    BitMapInfoHeader.biBitCount =   8 * bmp_enc_param->image_pixel_bit;
    BitMapInfoHeader.biCompression  =   0;
    BitMapInfoHeader.biSizeImage    =   0; 
    BitMapInfoHeader.biXPelsPerMeter=   0; 
    BitMapInfoHeader.biYPelsPerMeter=   0; 
    BitMapInfoHeader.biClrImportant =   0; 
    BitMapInfoHeader.biClrUsed      =   0; //彩色表中的颜色索引数
    //BITMAPFILEHEADER这个结构考虑到内存对齐之后为字节
    memcpy(bmp_enc_param->output_data + sizeof(BITMAPFILEHEADER), &BitMapInfoHeader, sizeof(BITMAPINFOHEADER));

	// 拷贝调色板数据
	if (nHaveRgbQuad)
	{
    	memcpy(bmp_enc_param->output_data + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER), rgbquad, sizeof(RGBQUAD)*256);
	}

	// 位图数据需要水平镜像
    unsigned char* pBufDst = bmp_enc_param->output_data + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + ((nHaveRgbQuad) ? sizeof(RGBQUAD)*256 : 0);
    unsigned char* pBufSrc = bmp_enc_param->input_data + (bmp_enc_param->image_height - 1) * (bmp_enc_param->image_width * bmp_enc_param->image_pixel_bit);

	if (COLOR_IMAGE_PIXEL_BIT == bmp_enc_param->image_pixel_bit)
	{
		for (int i = 0; i < bmp_enc_param->image_height; i++)
		{
			int j;
			for (j = 0; j <= nPixelsPerLine - 8; j += 8)
			{
				uint8x8x3_t rgb = vld3_u8(pBufSrc + j * 3);
		
				uint8x8_t temp = rgb.val[0];
				rgb.val[0] = rgb.val[2];
				rgb.val[2] = temp;
		
				vst3_u8(pBufDst + j * 3, rgb);
			}
		
			for (; j < nPixelsPerLine; j++)
			{
				pBufDst[j * 3 + 0] = pBufSrc[j * 3 + 2];
				pBufDst[j * 3 + 1] = pBufSrc[j * 3 + 1];
				pBufDst[j * 3 + 2] = pBufSrc[j * 3 + 0];
			}
		
			pBufSrc -= nBytesPerLine;
			pBufDst += nTotalBytesPerLine;
		}
	}
	else
	{
		for (int i = 0; i < bmp_enc_param->image_height; i++)
		{
			memcpy(pBufDst, pBufSrc, nBytesPerLine);
			pBufSrc -= nBytesPerLine;
			pBufDst += nTotalBytesPerLine;
		}
	}

	bmp_enc_param->output_len = nDIBSize;

	return 0;
}

int bmp_2_mono8(void* in_image, u32 in_image_len, u32* out_image_len)
{
	BITMAPFILEHEADER *bmp_file_head = NULL;
	BITMAPINFOHEADER *bmp_info_head = NULL;
	char* image_data = NULL;
	char* tmp_image1 = NULL;
	char* tmp_image2 = NULL;
	char tmp_data[256] = {0};
	u32 width = 0, height = 0;
	u32 w = 0, h = 0, block_num = 0, tail_size = 0;

	if (NULL == in_image)
	{
		return -1;
	}

	bmp_file_head = (BITMAPFILEHEADER*)in_image;
	bmp_info_head = (BITMAPINFOHEADER*)((char*)in_image + sizeof(BITMAPFILEHEADER));
	
	if (bmp_file_head->bfType != 0x4D42)
	{
		return -2;
	}

	width = bmp_info_head->biWidth;
	height = bmp_info_head->biHeight;

	image_data = (char*)((char*)in_image + bmp_file_head->bfOffBits);
	tmp_image1 = image_data;
	tmp_image2 = (char*)(image_data + (width * (height - 1)));

	block_num = width / 256;
	tail_size = width % 256;

	for (h = 0;h < height / 2;h++)
	{
		for (w = 0;w < block_num;w++)
		{
			memcpy(tmp_data, tmp_image1, 256);
			memcpy(tmp_image1, tmp_image2, 256);
			memcpy(tmp_image2, tmp_data, 256);
			tmp_image1 += 256;
			tmp_image2 += 256;
		}

		if (tail_size > 0)
		{
			memcpy(tmp_data, tmp_image1, tail_size);
			memcpy(tmp_image1, tmp_image2, tail_size);
			memcpy(tmp_image2, tmp_data, tail_size);
			tmp_image1 += tail_size;
			tmp_image2 += tail_size;			
		}

		tmp_image2 = tmp_image2 - (width * 2);
	}

	memmove(in_image, image_data, width * height);

	if (out_image_len)
	{
		*out_image_len = width * height;
	}

	return 0;
}

int bmp_2_mono8_v20(void* in_image, u32 in_image_len, u32* pwidth, u32* pheight, u32* out_image_len, u16* bit_count)
{
	BITMAPFILEHEADER *bmp_file_head = NULL;
	BITMAPINFOHEADER *bmp_info_head = NULL;
	char* image_data = NULL;
	char* tmp_image1 = NULL;
	char* tmp_image2 = NULL;
	char tmp_data[256] = {0};
	u32 width = 0, height = 0;
	u32 w = 0, h = 0, blocks = 0, tail = 0;

	if (NULL == in_image)
	{
		return -1;
	}

	bmp_file_head = (BITMAPFILEHEADER*)in_image;
	bmp_info_head = (BITMAPINFOHEADER*)((char*)in_image + sizeof(BITMAPFILEHEADER));
	
	if (bmp_file_head->bfType != 0x4D42)
	{
		return -2;
	}

	if (bmp_info_head->biBitCount / 8 == 1)
	{
		// mono
		width = bmp_info_head->biWidth;
		height = bmp_info_head->biHeight;		
	}
	else if (bmp_info_head->biBitCount / 8 == 3)
	{
		// 彩色
		width = bmp_info_head->biWidth * (bmp_info_head->biBitCount / 8);
		height = bmp_info_head->biHeight;

	}
	else
	{
		// 无法识别格式
		return -3;
	}

	if (pwidth)
	{
		*pwidth = bmp_info_head->biWidth;
	}

	if (pheight)
	{
		*pheight = bmp_info_head->biHeight;
	}

	if (bit_count)
	{
		*bit_count = bmp_info_head->biBitCount;
	}

	unsigned short nPixelsPerLine = bmp_info_head->biWidth;
	unsigned short ntotakBytesPerLine = (bmp_info_head->biWidth * bmp_info_head->biBitCount / 8 + 3) / 4 * 4;  // 位图每行总的字节数（4的倍数）

	image_data = (char*)((char*)in_image + bmp_file_head->bfOffBits);
	tmp_image1 = image_data;
	tmp_image2 = (char*)(image_data + (ntotakBytesPerLine * (height - 1)));

	// bgr转rgb
	if (bmp_info_head->biBitCount / 8 == 3)
	{
		for (h = 0; h < height; h++)
        {
            uint8_t *line = (uint8_t*)(image_data + h * ntotakBytesPerLine);
            
            // 每次处理8个像素（24字节）
            int pixels_per_neon = 8;
            int neon_loop = nPixelsPerLine / pixels_per_neon;
            int remain_pixels = nPixelsPerLine % pixels_per_neon;
            
            for (w = 0; w < neon_loop; w++)
            {
                uint8x8x3_t bgr = vld3_u8(line);
                uint8x8x3_t rgb = {bgr.val[2], bgr.val[1], bgr.val[0]};
                vst3_u8(line, rgb);
                line += 3 * pixels_per_neon;
            }
            
            // 处理剩余像素
            for (w = 0; w < remain_pixels; w++)
            {
                uint8_t tmp = line[0];
                line[0] = line[2];
                line[2] = tmp;
                line += 3;
            }
        }
	}

#if 0
	char tmp_data[width] = {0};
	for (int i = 0; i < bmp_info_head->biHeight / 2; i++)
	{
		memcpy(tmp_data, tmp_image1, width);
		memcpy(tmp_image1, tmp_image2, width);
		memcpy(tmp_image2, tmp_data, width);
		tmp_image2 -= width;
		tmp_image1 += width;
	}
#endif

	// 水平镜像
	blocks = ntotakBytesPerLine / 256;
	tail = ntotakBytesPerLine % 256;

	for (h = 0;h < height / 2;h++)
	{
		for (w = 0;w < blocks;w++)
		{
			memcpy(tmp_data, tmp_image1, 256);
			memcpy(tmp_image1, tmp_image2, 256);
			memcpy(tmp_image2, tmp_data, 256);
			tmp_image1 += 256;
			tmp_image2 += 256;
		}

		if (tail > 0)
		{
			memcpy(tmp_data, tmp_image1, tail);
			memcpy(tmp_image1, tmp_image2, tail);
			memcpy(tmp_image2, tmp_data, tail);
			tmp_image1 += tail;
			tmp_image2 += tail;			
		}

		tmp_image2 = tmp_image2 - (blocks * 256 + tail);
		tmp_image2 = tmp_image2 - ntotakBytesPerLine;
	}

	// 宽度不是4的倍数时需要去掉填充数据
	if (width != ntotakBytesPerLine)
	{
		tmp_image1 = tmp_image2 = image_data;
		for (h = 0;h < height;h++)
		{
			memmove(tmp_image1, tmp_image2, width);
			tmp_image1 += width;
			tmp_image2 += ntotakBytesPerLine;
		}
	}

	memmove(in_image, image_data, width * height);

	if (out_image_len)
	{
		*out_image_len = width * height;
	}

	return 0;
}

int form_bmp_head(u32 image_width, u32 image_height, u8* head, u32* bmp_len)
{
	if(NULL == head)
	{
		return -1;
	}
		
	RGBQUAD rgbquad[256] = {0};
    for(int i=0;i<256;i++)
    {
        rgbquad[i].rgbBlue = i;
        rgbquad[i].rgbGreen = i;
        rgbquad[i].rgbRed = i;
        rgbquad[i].rgbReserved = 0;
    }

    unsigned short nWidth   = MOD4(image_width);
    int nBmBitsSize     = nWidth * image_height;
	int nDIBSize   = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD)*256 + nBmBitsSize;
	
	//这里写入bitmap头
    BITMAPFILEHEADER BitMapFileHeader;
    BitMapFileHeader.bfType   =   0x4D42;   //   "BM "
    BitMapFileHeader.bfSize   =   nDIBSize;
    BitMapFileHeader.bfReserved1   =   0;
    BitMapFileHeader.bfReserved2   =   0;
    BitMapFileHeader.bfOffBits   = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD)*256; 

    // BITMAPFILEHEADER这个结构考虑到内存对齐之后为字节
    memcpy(head, &BitMapFileHeader, sizeof(BITMAPFILEHEADER));

    BITMAPINFOHEADER BitMapInfoHeader;
    BitMapInfoHeader.biSize     =   sizeof(BITMAPINFOHEADER); 
    BitMapInfoHeader.biWidth    = image_width;//pstParam->nWidth; 
    BitMapInfoHeader.biHeight   = image_height; 
    BitMapInfoHeader.biPlanes   =   1; 
    BitMapInfoHeader.biBitCount =   8; 
    BitMapInfoHeader.biCompression  =   0;
    BitMapInfoHeader.biSizeImage    =   0; 
    BitMapInfoHeader.biXPelsPerMeter=   0; 
    BitMapInfoHeader.biYPelsPerMeter=   0; 
    BitMapInfoHeader.biClrImportant =   0; 
    BitMapInfoHeader.biClrUsed      =   0; //彩色表中的颜色索引数
    //BITMAPFILEHEADER这个结构考虑到内存对齐之后为字节
    memcpy(head + sizeof(BITMAPFILEHEADER), &BitMapInfoHeader, sizeof(BITMAPINFOHEADER));

	// 拷贝调色板
    memcpy(head + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER), rgbquad, sizeof(RGBQUAD)*256);

	*bmp_len = nDIBSize;

	return 0;
}
