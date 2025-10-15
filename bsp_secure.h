#ifndef __BSP_SCRURE_H
#define __BSP_SCRURE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

enum secure_errno
{
	SCR_CRYPT_ERR          = -201,   /**< data encrypt or decrypt error */

	SCR_USER_NOT_EXT       = -102,   /**< user does not exist */
	SCR_PASSWD_LEN_ERR     = -101,   /**< password length error */

	SCR_CMD_ERR            = -8,     /**< command execution error */
	SCR_OUT_OF_RANGE       = -7,     /**< param out of range */
	SCR_OUT_OF_MEM         = -6,     /**< memory not enough */
	SCR_FILE_WR_ERR        = -5,     /**< file write error */
	SCR_FILE_RD_ERR        = -4,     /**< file read error, IO error or EOF */
	SCR_FILE_OPEN_ERR      = -3,     /**< file open error */
	SCR_INVALID_PARAM      = -2,     /**< input param not invalid */
	SCR_ERROR              = -1,     /**< generic error */
};

/**
 * @brief      Set system and uboot password. If the user is not the root user, 
 *             only the system password of the user will be set.
 * @param[in]  username  user name string
 * @param[in]  passwd  password string
 * @return     0 on success, others on failure
 */
int bsp_set_passwd(const char *username, const char *passwd);

/**
 * @brief      Reset the passwords of all users.
 * @return     0 on success, others on failure
 */
int bsp_reset_passwd(void);

/**
 * @brief      AES cbc decrypt   
 * @param[in]  iv     initialized vector
 * @param[in]  input  cipher data buf
 * @param[in]  len    length of input data, the same with output data
 * @param[out] output plain data buf
 * @return     0 on success, others on failure
 */

int bsp_decrypt_aes128_cbc(const uint8_t iv[16], const uint8_t *input, 
							uint32_t len, uint8_t *output);

/**
 * @brief      AES cbc encrypt
 * @param[in]  iv     initialized vector
 * @param[in]  input  plain data buf
 * @param[in]  len    length of input data, the same with output data
 * @param[out] output cipher data buf
 * @return     0 on success, others on failure
 */
int bsp_encrypt_aes128_cbc(const uint8_t iv[16], const uint8_t *input, 
							uint32_t len, uint8_t *output);

/**
 * @brief      Set syslog parameter, such as log path, single log file size and file num.
 *             After setting, device needs to be restarted.
 * @param[in]  path     log file path
 * @param[in]  max_size single log file size limit
 * @param[in]  file_num log file num limit
 * @return     0 on success, others on failure
 */
int bsp_set_syslog_para(const char *path, uint32_t max_size, uint32_t file_num);

/**
 * @brief      Set login parameter. After setting, device needs to be restarted.
 * @param[in]  max_fail_cnt  fail cnt limit
 * @param[in]  timeout_sec   operation timeout limit in second
 * @param[in]  lock_time_sec lock time in second while max fail cnt expires
 * @return     0 on success, others on failure
 */
int bsp_set_login_para(uint32_t max_fail_cnt, uint32_t timeout_sec, uint32_t lock_time_sec);

/**
 * @brief      Set the print level of the security library
 * @param[in]  log_prio log level to be set
 */
void bsp_secure_set_log_priority(uint8_t log_prio);

/**
 * @brief      Get the print level of the security library
 * @return     current log level
 */
uint8_t bsp_secure_get_log_priority(void);

#ifdef __cplusplus
}
#endif

#endif

