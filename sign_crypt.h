#ifndef SIGN_CRYPT_H
#define SIGN_CRYPT_H

enum segment_mark
{
	segment_first  = 0,  ///< 第一块
	segment_comm   = 1,  ///< 普通块
	segment_last   = 2,  ///< 最后一块
};

#ifdef __cplusplus
extern "C" {
#endif 

/**
 * @brief 获取库版本号
 * @return 整型版本号，例如100标识1.0.0版本
 */
int get_lib_version();

/**
 * @brief 把文件导入到内存
 * @param[in] path 文件路径
 * @return   返回非负数-文件长度， 负数-错误
 */
int32_t get_filesize(const char *path, size_t *size);

/**
 * @brief 把文件导入到内存
 * @param[in] path 文件路径
 * @param[in] buf   内存
 * @param[in] buf_len  内存大小
 * @return   返回0 成功， 返回 -4  内存不够返回-2 句柄操作失败
 */
int32_t file_load(const char *path,  unsigned char *buf, size_t *buf_len);

/**
 * @brief  把内存写入文件
 * @param[in] save_file   文件名称
 * @param[in] buf   内存
 * @param[in] buf_len
 * @return  成功 返回0
 */
int32_t file_write(const char *save_file,  unsigned char *buf, size_t buf_len);

/**
 * @brief  sha字符串然后输出hex编码的sha值
 * @param[in] str  待签名字符串
 * @param[out] outbuf  输出数据缓存
 * @param[out] outlen  输出数据长度
 * @return  返回0  成功
 */
int32_t sha_string(const unsigned char *str, unsigned char *outhash, size_t *outlen);

/**
 * @brief  sha字符串然后输出hex编码的sha值
 * @param[in] buf  待签名数据buf
 * @param[in] datalen  待签名数据长度
 * @param[out] outbuf  输出数据缓存
 * @param[out] outlen  输出数据长度
 * @return  返回0  成功
 */
int32_t sha_data(const unsigned char *buf, size_t datalen, unsigned char *outhash, size_t *outlen);

/**
 * @brief   把文件sha后输出hex编码的sha值
 * @param[in] path  sha的文件
 * @param[in] sha_hexfile  输出hex编码的sha值到文件，为NULL  不保存文件
 * @param[in] output  输出hex编码sha值到内存为NULL 不输出cp
 * @param[in] outlen  输出长度
 * @return 返回0  成功
 */
int32_t sha_file_hex(const char *path, const char *sha_hexfile,  char *output, size_t *outlen);

/**
 * @brief  验签
 * @param[in] pubkey_pem_key  公钥key
 * @param[in] key_len   公钥key长度
 * @param[in] sig_buf    hex编码的签名文件buf
 * @param[in] sig_len   签名文件长度
 * @param[in] verify_buf   待验签的文件buf
 * @param[in] verify_len   待验签的文件长度 
 * @return
 */
int32_t sign_verify(const unsigned char *pubkey_pem_key, size_t key_len, const unsigned char *sig_buf, size_t sig_len, const unsigned char *verify_buf, size_t verify_len);

/**
 * @brief  验签
 * @param[in] pubkey_path  公钥文件
 * @param[in] sig_buf    hex编码的签名文件buf
 * @param[in] sig_len   签名文件长度
 * @param[in] verify_buf   待验签的文件buf
 * @param[in] verify_len   待验签的文件长度 
 * @return
 */
int32_t sign_verify_by_keyfile(const char *pubkey_path, const unsigned char *sig_buf, size_t sig_len, const unsigned char *verify_buf, size_t verify_len);

/**
 * @brief  验签
 * @param[in] pubkey_path  公钥文件
 * @param[in] sig_path    hex编码的签名文件
 * @param[in] verify_file   待验证的文件
 * @return  返回0  成功
 */
int32_t sign_verify_by_file(const char *pubkey_path, const char *sig_path, const char *verify_file);

/**
 * @brief 对比der格式的公钥和pem格式的公钥类容是否一致，将der格式转化为pem格式再做比较
 * @param[in] pubkey_path_bin  公钥的二进制格式
 * @param[in] pubkey_path_str  公钥的字符串格式
 * @return   0 相同 -1 转化格式失败 -2读取文件失败 -3 不相同
 */
int32_t compare_key_bin_to_str(const char *pubkey_path_bin, const char *pubkey_path_str);

/**
 * @brief 公钥格式转换
 * @param[in] pubkey_path  待转换的公钥文件
 * @param[in] trans_pubkey_path  转换后的公钥文件
 * @param[in] format    1: pem格式 2:der格式
 * @return  0 成功
 */
int32_t transfor_format(const char *pubkey_path, const char *trans_pubkey_path, int32_t format);

/**
 * @brief  二进制数据经过base64加密成ascii
 * @param[in] bindata  二进制数据
 * @param[in] base64  加密后输出数据
 * @param[in] binlength  二进制数据长度
 * @return  加密后的数据长度
 */
size_t base64_encode(const unsigned char *bindata, unsigned char *base64, size_t binlength);

/**
 * @brief  经过base64加密成ascii转二进制数据
 * @param[in] base64  加密后输出数据
 * @param[in] bindata  二进制数据
 * @return  解密后的数据长度 
 */
size_t base64_decode(const unsigned char *base64, unsigned char *bindata);

/**
 * @brief  对数据进行md5加密后转hex输出
 * @param[in] inbuf  待加密数据
 * @param[in] inlen  待加密数据长度
 * @param[out] outbuf  输出数据缓存
 * @param[out] outlen  输出数据长度
 * @return
 */
int32_t md5_encrypt(const unsigned char *inbuf, size_t inlen,  unsigned char *outbuf, size_t *outlen);

/**
 * @brief  采用cbc模式的aes128加密base64加密输出
 * @param[in] key   密钥 
 * @param[in] inbuf  待加密数据
 * @param[in] inlen  待加密数据长度
 * @param[out] outbuf  输出数据缓存（base64）
 * @param[out] outlen  输出数据长度
 * @return 0 成功 非0 失败
 */
int32_t aes128_cbc_encrypt(
	const unsigned char *key, 
	const unsigned char *iv,
	const unsigned char *inbuf, 
	size_t inlen,
	unsigned char *outbuf, 
	size_t *outlen
);

/**
 * @brief  采用cbc模式的aes128加密hex输出
 * @param[in] key   密钥 
 * @param[in] iv  初始化向量 
 * @param[in] inbuf  待加密数据
 * @param[in] inlen  待加密数据长度
 * @param[out] outbuf  输出数据缓存（hex）
 * @param[out] outlen  输出数据长度
 * @return 0 成功 非0 失败
 */
int32_t aes128_cbc_encrypt_hex(
    const unsigned char *key, 
    const unsigned char *iv,
    const unsigned char *inbuf, 
	size_t inlen,
    unsigned char *outbuf, 
	size_t *outlen
);

/**
 * @brief  采用cbc模式的aes128进行块加密hex输出
 * @param[in] size_t sec_len 块加密长度，不小于16字节
 * @param[in] key   密钥 
 * @param[in] iv  初始化向量 
 * @param[in] inbuf  待加密数据
 * @param[in] inlen  待加密数据长度
 * @param[out] outbuf  输出数据缓存（hex）
 * @param[out] outlen  输出数据长度
 * @return 0 成功 非0 失败
 */
int32_t aes128_cbc_segment_encrypt_hex(
	size_t segment_size,
    const unsigned char *key, 
    const unsigned char *iv,
    const unsigned char *inbuf, 
	size_t inlen,
    unsigned char *outbuf, 
	size_t *outlen
);

/**
 * @brief  采用cbc模式的aes128解密
 * @param[in] key   密钥 
 * @param[in] inbuf  待解密数据（base64）
 * @param[in] inlen  待解密数据长度
 * @param[out] outbuf  输出数据缓存（hex）
 * @param[out] outlen  输出数据长度
 * @return 0 成功 非0 失败
 */
int32_t aes128_cbc_decrypt(
	const unsigned char *key, 
	const unsigned char *iv,
	const unsigned char *inbuf, 
	size_t inlen,
	unsigned char *outbuf, 
	size_t *outlen
);

/**
 * @brief  采用cbc模式的aes128解密
 * @param[in] key   密钥 
 * @param[in] iv  初始化向量  
 * @param[in] inbuf  待解密数据（hex）
 * @param[in] inlen  待解密数据长度
 * @param[out] outbuf  输出数据缓存（hex）
 * @param[out] outlen  输出数据长度
 * @return 0 成功 非0 失败
 */
int32_t aes128_cbc_decrypt_hex(
    const unsigned char *key, 
    const unsigned char *iv,
    const unsigned char *inbuf, 
	size_t inlen,
    unsigned char *outbuf, 
	size_t *outlen
);

/**
 * @brief  采用cbc模式的aes128分块解密
 * @param[in] key   密钥 
 * @param[in] iv  初始化向量  
 * @param[in] inbuf  待解密数据（hex）
 * @param[in] inlen  待解密数据长度
 * @param[in] segmark 待解密块序号标识 0-第1个 1-中间包 2-最后1个
 * @param[out] outbuf  输出数据缓存（hex）
 * @param[out] outlen  输出数据长度
 * @return 0 成功 非0 失败
 */

int32_t aes128_cbc_segment_decrypt_hex(
    const unsigned char *key, 
    const unsigned char *iv,
    const unsigned char *inbuf, 
	size_t inlen,
	enum segment_mark segmark,
    unsigned char *outbuf, 
	size_t *outlen
);

/**
 * @brief  采用ecb模式的aes128加密base64加密输出
 * @param[in] key   密钥 
 * @param[in] inbuf  待加密数据
 * @param[in] inlen  待加密数据长度(16的倍数)
 * @param[out] outbuf  输出数据缓存
 * @param[out] outlen  输出数据长度
 * @return 0 成功 非0 失败
 */
int32_t aes128_ecb_encrypt(
	const unsigned char *key, 
	const unsigned char *inbuf, 
	size_t inlen,
	unsigned char *outbuf, 
	size_t *outlen
);

/**
 * @brief  采用ecb模式的aes128解密
 * @param[in] key   密钥 
 * @param[in] inbuf  待解密数据（base64）
 * @param[in] inlen  待解密数据长度，16的倍数
 * @param[out] outbuf  输出数据缓存
 * @param[out] outlen  输出数据长度
 * @return 0 成功 非0 失败
 */
int32_t aes128_ecb_decrypt(
	const unsigned char *key, 
	const unsigned char *inbuf, 
	size_t inlen,
	unsigned char *outbuf, 
	size_t *outlen
);

/**
 * @brief  生成RSA3072的pem格式的rsa公私钥对
 * @param[out] public_key_pem  公钥
 * @param[in] public_key_len  公钥buf长度
 * @param[out] private_key_pem  私钥
 * @param[in] private_key_len  私钥buf长度 
 * @return 0 成功 非0 失败
 */
int generate_rsa_key_pair(unsigned char* public_key_pem, size_t public_key_len, unsigned char* private_key_pem, size_t private_key_len);

/**
 * @brief  生成RSA3072的pem格式的rsa公私钥对
 * @param[out] public_key_file  公钥文件存储路径
 * @param[out] private_key_file  私钥文件存储路径
 * @return 0 成功 非0 失败
 */
int generate_rsa_key_file(const char* public_key_file, const char* private_key_file);

/**
 * @brief  rsa公钥对数据进行加密base64输出
 * @param[out] public_pem_file  公钥文件路径
 * @param[in] inbuf  待加密数据
 * @param[in] inlen  待加密数据长度
 * @param[out] outbuf  加密后数据缓存(base64)
 * @param[out] outlen  加密后数据长度
 * @return 0 成功 非0 失败
 */
int rsa_encrypt_by_keyfile(	
	const unsigned char *public_pem_file, 
	const unsigned char *inbuf, 
	size_t inlen,  
	unsigned char *outbuf, 
	size_t *outlen
);

/**
 * @brief  rsa公钥对数据进行加密hex输出
 * @param[out] public_pem_file  公钥文件路径
 * @param[in] inbuf  待加密数据
 * @param[in] inlen  待加密数据长度
 * @param[out] outbuf  加密后数据缓存(base64)
 * @param[out] outlen  加密后数据长度
 * @return 0 成功 非0 失败
 */
int rsa_encrypt_by_keyfile_hex(    
    const unsigned char *public_pem_file, 
    const unsigned char *inbuf, 
    size_t inlen,  
    unsigned char *outbuf, 
    size_t *outlen
);

/**
 * @brief  rsa公钥对数据进行加密base64输出
 * @param[out] public_pem_key  公钥key
 * @param[in] inbuf  待加密数据
 * @param[in] inlen  待加密数据长度
 * @param[out] outbuf  加密后数据缓存(base64)
 * @param[out] outlen  加密后数据长度
 * @return 0 成功 非0 失败
 */
int rsa_encrypt_by_key(	
	const unsigned char *public_pem_key, 
	const unsigned char *inbuf, 
	size_t inlen,  
	unsigned char *outbuf, 
	size_t *outlen
);

/**
 * @brief  rsa私钥对数据（base64后）进行解密
 * @param[out] private_key    私钥key
 * @param[in] inbuf  待解密数据(base64)
 * @param[in] inlen  待解密数据长度
 * @param[out] outbuf  解密后数据缓存
 * @param[out] outlen  解密后数据长度
 * @return 0 成功 非0 失败
 */
int rsa_decrypt_by_key(	
	const unsigned char *private_key, 
	const unsigned char *inbuf, 
	size_t inlen,  
	unsigned char *outbuf, 
	size_t *outlen,
	size_t outbuf_len
);

/**
 * @brief  采用sha256方式对待加密串进行e1计算
 * @param[in] inbuf  待加密串，这里一般是password
 * @param[in] salt  盐值
 * @param[out] outbuf  输出数据缓存(大于64)
 * @param[in] buflen  输出buf的长度
 * @param[out] outlen  输出数据长度
 * @return 0 成功 非0 失败
 */
int generate_e1_encrypt(
    const unsigned char *inbuf,
    const unsigned char *salt,
    unsigned char *outbuf, 
    size_t buflen,
    size_t *outlen    
);

/**
 * @brief  采用sha256方式对e1进行en计算后base64输出
 * @param[in] e1
 * @param[in] randbuf  随机串
 * @param[out] outbuf  输出数据缓存
 * @param[in] buflen  输出buf的长度 
 * @param[out] outlen  输出数据长度(大于64)
 * @return 0 成功 非0 失败
 */
int generate_en_encrypt(
    const unsigned char *e1,
    const unsigned char *randbuf,
    unsigned char *outbuf, 
    size_t buflen,    
    size_t *outlen    
);

/**
 * @brief  生成目标长度的随机字符串
 * @param[in] output 随机串输出
 * @param[in] len  目标随机串长度+1
 * @return 0 成功 非0 失败
 */
void generate_random_string(unsigned char *output, size_t len);

/**
 * @brief  生成设备加密串base64输出
 * @param[in] public_pem_file pem格式公钥路径
 * @param[in] serial  设备序列号
 * @param[out] encryptstring  加密串
 * @param[out] encryptlen  加密串长度(传入要填充encryptstring buffer的长度)
 * @param[out] passwd  重置口令
 * @return 0 成功 非0 失败
 */
int generate_encryptstring(
	const unsigned char* public_pem_file, 
	const unsigned char* serial, 
	unsigned char* encryptstring, 
	size_t* encryptlen, 
	unsigned char passwd[8]
);

/**
 * @brief  生成密码重置加密串
 * @param[in] encryptstring 设备加密串base64格式
 * @param[in] encryptlen  加密串长度
 * @param[in] rsaver  密钥版本号 
 * @param[in] serial  设备序列号
 * @param[out] resetbuf  重置密文缓冲区
 * @param[out] resetlen  重置密文缓冲区长度
 * @return 0 成功 非0 失败
 */
int generate_resetstring(
	const unsigned char* encryptstring, 
	size_t encryptlen, 
	uint32_t rsaver, 
	const unsigned char* serial, 
	unsigned char * resetbuf, 
	size_t* resetlen
);

#ifdef __cplusplus
}
#endif

#endif

