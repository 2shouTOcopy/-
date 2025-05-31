#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include <unistd.h>
#include <stddef.h>
#include <dirent.h>

#include "utils.h"

#define P_ERROR(format, ...) fprintf(stderr, "ERROR: " format "\n", ##__VA_ARGS__)

#define STRERRNO strerror(errno)

int format_name(char *ret, int ret_len,
                const char *plugin, const char *plugin_instance,
                const char *type, const char *type_instance) {
  char *buffer;
  size_t buffer_size;

  buffer = ret;
  buffer_size = (size_t)ret_len;

#define APPEND(str)                                                            \
  do {                                                                         \
    size_t l = strlen(str);                                                    \
    if (l >= buffer_size)                                                      \
      return ENOBUFS;                                                          \
    memcpy(buffer, (str), l);                                                  \
    buffer += l;                                                               \
    buffer_size -= l;                                                          \
  } while (0)

  assert(plugin != NULL);
  assert(type != NULL);

  APPEND(plugin);
  if ((plugin_instance != NULL) && (plugin_instance[0] != 0)) {
    APPEND("-");
    APPEND(plugin_instance);
  }
  APPEND("/");
  APPEND(type);
  if ((type_instance != NULL) && (type_instance[0] != 0)) {
    APPEND("-");
    APPEND(type_instance);
  }
  assert(buffer_size > 0);
  buffer[0] = 0;

#undef APPEND
  return 0;
} /* int format_name */

void *smalloc(size_t size) {
  void *r;

  if ((r = malloc(size)) == NULL) {
    ERROR("Not enough memory.");
    exit(3);
  }

  return r;
} /* void *smalloc */

char *sstrdup(const char *s) {
  char *r;
  size_t sz;

  if (s == NULL)
    return NULL;

  /* Do not use `strdup' here, because it's not specified in POSIX. It's
   * ``only'' an XSI extension. */
  sz = strlen(s) + 1;
  r = malloc(sz);
  if (r == NULL) {
    ERROR("sstrdup: Out of memory.");
    exit(3);
  }
  memcpy(r, s, sz);

  return r;
} /* char *sstrdup */

size_t sstrnlen(const char *s, size_t n) {
  const char *p = s;

  while (n-- > 0 && *p)
    p++;

  return p - s;
} /* size_t sstrnlen */

char *sstrndup(const char *s, size_t n) {
  char *r;
  size_t sz;

  if (s == NULL)
    return NULL;

  sz = sstrnlen(s, n);
  r = malloc(sz + 1);
  if (r == NULL) {
    ERROR("sstrndup: Out of memory.");
    exit(3);
  }
  memcpy(r, s, sz);
  r[sz] = '\0';

  return r;
} /* char *sstrndup */

char *sstrncpy(char *dest, const char *src, size_t n) {
  strncpy(dest, src, n);
  dest[n - 1] = '\0';

  return dest;
} /* char *sstrncpy */

int escape_string(char *buffer, size_t buffer_size) {
  char *temp;
  size_t j;

  /* Check if we need to escape at all first */
  temp = strpbrk(buffer, " \t\"\\");
  if (temp == NULL)
    return 0;

  if (buffer_size < 3)
    return EINVAL;

  temp = calloc(1, buffer_size);
  if (temp == NULL)
    return ENOMEM;

  temp[0] = '"';
  j = 1;

  for (size_t i = 0; i < buffer_size; i++) {
    if (buffer[i] == 0) {
      break;
    } else if ((buffer[i] == '"') || (buffer[i] == '\\')) {
      if (j > (buffer_size - 4))
        break;
      temp[j] = '\\';
      temp[j + 1] = buffer[i];
      j += 2;
    } else {
      if (j > (buffer_size - 3))
        break;
      temp[j] = buffer[i];
      j++;
    }
  }

  assert((j + 1) < buffer_size);
  temp[j] = '"';
  temp[j + 1] = 0;

  sstrncpy(buffer, temp, buffer_size);
  sfree(temp);
  return 0;
} /* int escape_string */

/**
 * custom_strjoin: 将字符串数组中的元素用指定分隔符连接起来。
 *
 * @param dst           目标缓冲区。
 * @param dst_size      目标缓冲区大小。
 * @param fields        要连接的字符串数组。
 * @param fields_count  要连接的字符串数量。
 * @param separator     分隔符字符串。
 * @return 0 表示成功，-1 表示失败 (例如，缓冲区不足)。
 */
static int custom_strjoin(char *dst, size_t dst_size,
                          char *const fields[], size_t fields_count,
                          const char *separator)
{
    if (dst == NULL || dst_size == 0)
    {
        return -1;
    }
    // 初始化目标缓冲区为空字符串，以便 strcat 等函数可以安全使用
    dst[0] = '\0';

    if (fields_count == 0)
    {
        return 0; // 没有要连接的字段
    }

    size_t current_len = 0;
    size_t separator_len = strlen(separator);

    for (size_t k = 0; k < fields_count; ++k)
    {
        if (fields[k] == NULL)
        { // 根据需要处理 NULL 字段，这里选择跳过
            continue;
        }
        size_t field_len = strlen(fields[k]);

        // 检查空间是否足够：当前长度 + 字段长度 + (如果需要分隔符 ? 分隔符长度 : 0) + null终止符
        size_t needed_for_field = field_len;
        size_t needed_for_separator = 0;

        if (k < fields_count - 1)
        { // 如果不是最后一个字段，则需要分隔符
            needed_for_separator = separator_len;
        }

        if (current_len + needed_for_field + needed_for_separator + 1 > dst_size)
        {
            // 空间不足，可以选择截断或返回错误
            // 为安全起见，确保已写入部分以null结尾并返回错误
            dst[current_len] = '\0';
            return -1;
        }

        // 复制字段
        memcpy(dst + current_len, fields[k], field_len);
        current_len += field_len;

        // 如果不是最后一个字段，复制分隔符
        if (needed_for_separator > 0)
        {
            memcpy(dst + current_len, separator, separator_len);
            current_len += separator_len;
        }
    }

    dst[current_len] = '\0'; // 确保最终结果以 null 结尾
    return 0;
}

// --- 您提供的 check_create_dir 函数 ---
// (已将 sstrncpy 和 strjoin 替换为自定义版本)
int check_create_dir(const char *file_orig)
{
    struct stat statbuf;

    char file_copy[PATH_MAX];
    char dir[PATH_MAX];
    char *fields[16]; // 最多处理 15 个目录层级 + 文件名 (或16个目录层级)
    int fields_num;
    char *ptr;
    char *saveptr;
    int last_is_file = 1;
    int path_is_absolute = 0;
    size_t len;

    /*
     * Sanity checks first
     */
    if (file_orig == NULL)
        return -1;

    if ((len = strlen(file_orig)) < 1)
        return -1;
    else if (len >= sizeof(file_copy))
    { // len 是 strlen 的结果，不包括 \0
      // sizeof(file_copy) 包括 \0 的空间
      // 所以如果 len == sizeof(file_copy)-1, 字符串正好填满(有\0)
      // 如果 len >= sizeof(file_copy), 则太长，无法容纳\0
        P_ERROR("check_create_dir: name (%s) is too long.", file_orig);
        return -1;
    }

    /*
     * If `file_orig' ends in a slash the last component is a directory,
     * otherwise it's a file. Act accordingly..
     */
    if (file_orig[len - 1] == '/')
        last_is_file = 0;
    if (file_orig[0] == '/')
        path_is_absolute = 1;

    /*
     * Create a copy for `strtok_r' to destroy
     * 由于已检查 len < sizeof(file_copy)，标准的 strncpy 在这里是安全的，
     * 它会复制包括 null 终止符在内的整个字符串，并用 null 填充剩余部分。
     * 如果您想确保 sstrncpy_custom 的行为，也可以使用它。
     */
    // sstrncpy_custom(file_copy, file_orig, sizeof(file_copy));
    // 实际上，因为 len < sizeof(file_copy)，strcpy 也是安全的：
    strcpy(file_copy, file_orig);

    /*
     * Break into components. This will eat up several slashes in a row and
     * remove leading and trailing slashes..
     */
    ptr = file_copy;
    saveptr = NULL;
    fields_num = 0;
    while ((fields[fields_num] = strtok_r(ptr, "/", &saveptr)) != NULL)
    {
        ptr = NULL; // strtok_r 的后续调用需要第一个参数为 NULL
        fields_num++;

        if (fields_num >= (sizeof(fields) / sizeof(fields[0]))) // 避免超出 fields 数组边界
            break;
    }

    /*
     * For each component, do..
     */
    for (int i = 0; i < (fields_num - last_is_file); i++)
    {
        /*
         * Do not create directories that start with a dot. This
         * prevents `../../' attacks and other likely malicious
         * behavior.
         */
        if (fields[i][0] == '.')
        {
            P_ERROR("Cowardly refusing to create a directory that "
                  "begins with a `.' (dot): `%s'",
                  file_orig); // 打印原始路径以获取上下文
            return -2;
        }

        /*
         * Join the components together again to form the current path part.
         * 例如，如果 file_orig 是 "/usr/local/bin", 且 i=0, dir="/usr"
         * i=1, dir="/usr/local"
         * i=2 (如果 last_is_file=0), dir="/usr/local/bin"
         */
        // dir[0] 的设置和 dir+path_is_absolute 的用法很巧妙：
        // 1. 如果是绝对路径 (path_is_absolute = 1):
        //    dir[0] = '/';
        //    custom_strjoin 连接到 dir+1。结果： "/comp1/comp2"
        // 2. 如果是相对路径 (path_is_absolute = 0):
        //    dir[0] = '/'; (临时设置)
        //    custom_strjoin 连接到 dir+0，会覆盖 dir[0]。结果： "comp1/comp2"
        dir[0] = '/'; // 仅当 path_is_absolute=1 时，这个 '/' 才会保留。
                      // 对于相对路径，它会被 custom_strjoin 的结果覆盖。
        if (custom_strjoin(dir + path_is_absolute,
                           (size_t)(sizeof(dir) - path_is_absolute), fields,
                           (size_t)(i + 1), "/") < 0)
        {
            P_ERROR("custom_strjoin failed: `%s', component #%i (%s)", file_orig, i, fields[i]);
            return -1;
        }

        while (42)
        { // 这是一个无限循环，依赖内部的 break 来退出
            // 尝试 stat 和 lstat 来检查路径状态
            // 如果两者都失败，通常意味着路径不存在或者存在其他错误
            struct stat current_statbuf; // 每次循环使用新的 statbuf，避免混淆
            if ((stat(dir, &current_statbuf) == -1) && (lstat(dir, &current_statbuf) == -1))
            {
                // 既然 lstat 也失败了，errno 应该是由 lstat 设置的。
                // 如果 dir 是一个指向不存在目标的悬空符号链接，stat 会失败 (ENOENT)，但 lstat 会成功。
                // 所以这个条件 ((stat == -1) && (lstat == -1)) 主要捕获“确实不存在”或权限问题。
                if (errno == ENOENT)
                {                                                     // 路径组件不存在
                    if (mkdir(dir, S_IRWXU | S_IRWXG | S_IRWXO) == 0) // 尝试创建目录
                        break;                                        // 创建成功，跳出 while(42)

                    // mkdir 可能因为其他线程同时创建了目录而失败 (EEXIST)
                    if (EEXIST == errno)
                        continue; // 重新执行 while(42) 循环，再次 stat 检查

                    P_ERROR("check_create_dir: mkdir (%s): %s", dir, STRERRNO);
                    return -1; // mkdir 因其他原因失败
                }
                else
                { // stat/lstat 因 ENOENT 以外的原因失败 (例如，权限不足去访问父目录)
                    P_ERROR("check_create_dir: stat/lstat (%s): %s", dir, STRERRNO);
                    return -1;
                }
            }
            else if (!S_ISDIR(current_statbuf.st_mode))
            { // 路径存在，但不是目录
                // 注意: 如果 stat() 成功，current_statbuf 来自 stat()。
                // 如果 stat() 失败但 lstat() 成功 (例如悬空链接), current_statbuf 来自 lstat()。
                // 对于悬空链接，S_ISDIR 会是 false。
                P_ERROR("check_create_dir: `%s' exists but is not "
                      "a directory!",
                      dir);
                return -1;
            }
            // 如果路径存在且是目录，则一切正常
            break; // 跳出 while(42)
        }          // end while(42)
    }              // end for loop

    return 0;
} /* check_create_dir */


/**
 * @brief 通过进程名获取pid
 * @param[in] task_name 进程名
 * @param[in/out] pid pid是个数组 
 * @param[in/out] pid_array_len 既是输入也是输出，输入表示数组长度，输出表示找到进程个数
 * @return 成功返回0, 失败返回其他值
 */
int get_pid_by_name(const char *task_name, int *pid, int *pid_array_len)
{
	DIR *dir = NULL;
	struct dirent *ptr = NULL;
	FILE *fp = NULL;
	int ret = -2;
	char filepath[384] = {0};
	char cur_task_name[128] = {0};
	char buf[256] = {0};
	int array_len = 0;
	int idx = 0;
	if ((NULL == task_name) || (NULL == pid) 
		|| (NULL == pid_array_len) || (0 == *pid_array_len))
	{
		return -1;
	}
	
	array_len = *pid_array_len;
	
	dir = opendir("/proc");

	if (NULL != dir)
	{
		while ((ptr = readdir(dir)) != NULL)
		{

			if ((0 == strcmp(ptr->d_name, ".")) || (0 == strcmp(ptr->d_name, "..")))
			{
				continue;

			}

			if (DT_DIR != ptr->d_type)
			{
				continue;
			}

			snprintf(filepath, sizeof(filepath), "/proc/%s/status", ptr->d_name);
			
			fp = fopen(filepath, "r");

			if (NULL != fp)
			{
				memset(buf, 0, sizeof(buf));
				if (fgets(buf, sizeof(buf) - 1, fp) == NULL)
				{
					fclose(fp);
					fp = NULL;
					continue;
				}

				sscanf(buf, "%*s %s", cur_task_name);
				if (strcmp(task_name, cur_task_name) == 0)
				{
					pid[idx] = atoi(ptr->d_name);
					fclose(fp);
					fp = NULL;
					ret = 0;
					idx++;
					if (idx >= array_len)
					{
						break;
					}
				}
				else
				{
					fclose(fp);
					fp = NULL;
				}
			}
		}
		
		closedir(dir);
	}
	
	*pid_array_len = idx;
	return ret;
}

