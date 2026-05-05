# microSD 卡实现进度记录

## 1. 当前进度

### 1.1 已完成

1. 新增 microSD/storage 基础模块。
2. 默认 SD 设备节点已设为 `/dev/mmcblk1p10`。
3. 默认挂载点已设为 `/mnt/sdcard`。
4. 实现 SD 状态、挂载/卸载/格式化命令封装、容量统计、可写检查。
5. 实现写入路由：`AUTO/eMMC/microSD`。
6. 实现策略：SD 不可用时 `AUTO` 不降级 eMMC，直接报错。
7. 实现同名写入拒绝：目标文件存在时返回 `STORAGE_E_ALREADY_EXISTS`，不覆盖、不改名。
8. 实现路径安全检查：拒绝绝对路径、`..`、控制字符等。
9. 实现写入 token：空间检查、generation 检查、弹出/异常移除取消。
10. 接入 app 初始化：`storage_init` 已加入 `source/app/main/main.c`。
11. 新增方案路径适配层 `project_storage_adapter`。
12. 实现方案记录模型 `StorageProjectRecord`。
13. 实现方案路径构造：package/workdir/base image/index。
14. 实现方案列表聚合纯逻辑：eMMC 优先，SD 同名项屏蔽。
15. `comif_get_project_list` 现有 eMMC 方案记录新增 `StorageMedia`、`StorageStatus`、`IndexStatus` 字段，旧字段保持不变。
16. `comif_get_project_list` 的 eMMC base image 路径改为通过 `project_storage_adapter` 解析。
17. 新增 microSD 状态文件输出 API：`storage_save_micro_sd_info_to_file`。
18. 新增 comif 层 microSD 状态查询函数：`comif_get_micro_sd_state`，输出 `comif_micro_sd_state.json`。
19. 新增 comif 层安全弹出函数：`comif_safe_eject_micro_sd`。
20. 新增 comif 层格式化函数：`comif_format_micro_sd`。
21. 增加 host 单元测试。
22. 新增方案保存路径集合解析 API：`storage_project_resolve_save_paths`，统一返回方案包、工作目录、base image、`project_mng.json` 路径和介质 generation。
23. `scfw_save_project_to_path` 默认方案保存分支已接入 `project_storage_adapter`，`.sln`、base image、`project_mng.json` 写入路径按 `AUTO/eMMC/microSD` 解析。
24. `scfw_save_as_project_to_ddr` 已接入 `project_storage_adapter`，`.sln`、base image、`project_mng.json` 写入路径按 `AUTO/eMMC/microSD` 解析。
25. `scfw_upload_project` 已接入 `project_storage_adapter`，上传 `.sln`、base image hook、`project_mng.json` 写入路径按 `AUTO/eMMC/microSD` 解析。
26. 新增方案访问路径解析 API：`storage_project_resolve_access_paths`。写入路径 `AUTO` 仍按 SD 优先；加载/删除/重命名等访问已有方案路径时 `AUTO` 按旧协议 eMMC 优先，显式 `microSD` 时要求 SD 在线。
27. `scfw_load_project` 已接入方案访问路径解析，解包源 `.sln`、工作目录、base image 路径改为通过 `project_storage_adapter` 获取。
28. `delete_saved_project` 和 `scfw_delete_project` 的方案包、工作目录、base image 删除路径已改为通过 `project_storage_adapter` 获取。
29. 新增方案重命名路径解析 API：`storage_project_resolve_rename_paths`，统一返回旧方案和新方案的 package/workdir/base image/index 路径；`AUTO` 保持旧协议 eMMC 优先，显式 microSD 时要求 SD 可写。
30. `modify_emmc_sln_file_info` 已接入方案重命名路径解析，重命名 `.sln`、工作目录、base image 和失败回滚路径改为通过 `project_storage_adapter` 获取。
31. `modify_emmc_sln_switch_info` 的 `.sln` 访问路径已改为通过 `project_storage_adapter` 获取。
32. 新增方案复制路径解析 API：`storage_project_resolve_copy_paths`，统一返回源方案和目标方案的 package/workdir/base image/index 路径；`AUTO` 保持旧协议 eMMC 优先，显式 microSD 时要求 SD 可写。
33. `scfw_copy_project_to_ddr` 已接入方案复制路径解析，源 `.sln`、目标 `.sln`、目标 base image 路径改为通过 `project_storage_adapter` 获取。
34. 新增带尾斜杠方案工作目录解析 API：`storage_project_get_workdir_dir_path`，用于兼容现有调用方期望的 `project_dir/<name>/` 格式。
35. `scfw_get_cur_project_path` 和 `scfw_get_project_path_ddr` 已接入 `project_storage_adapter`，不再直接拼接 `PROJ_DIR`。
36. 新增方案工作目录子路径解析 API：`storage_project_get_workdir_child_path`，用于安全构造 `project_dir/<name>/<child>` 形式路径。
37. `scfw_load_default_project_with_PR`、`scfw_load_default_project`、`load_untitle_project` 的当前方案目录清理、默认方案目录重建路径已接入 `project_storage_adapter`；默认方案 `vtool/ctool` 子目录创建路径也改为通过 adapter 构造。
38. `scfw_load_last_used_project` 启动时默认方案目录创建路径已接入 `project_storage_adapter`。
39. `scfw_switch_project` 的 SC2000E 旧方案工作目录清理路径已接入 `project_storage_adapter`；切换失败恢复路径通过 `scfw_load_project`/`scfw_load_default_project` 间接使用 adapter 解析。
40. 新增方案工作目录根路径解析 API：`storage_project_get_workdir_root_dir_path`，用于兼容现有调用方期望的 `project_dir/` 格式。
41. `scfw_free_cur_project` 的方案工作目录根目录计数、打开、遍历和超限清理路径已接入 `project_storage_adapter`。
42. 新增方案包根目录解析 API：`storage_project_get_package_root_dir_path`，用于兼容现有调用方期望的 `project/` 尾斜杠目录格式。
43. `scfw_set_cur_project_name` 的当前方案工作目录移动/默认方案目录重建路径已接入 `project_storage_adapter`，不再直接拼接 `PROJ_DIR`。
44. `scfw_collect_sln_files` 和 `scfw_sync_project_mng_with_sln_files` 的 eMMC 方案包目录扫描、`.sln/.scsln` 文件读取路径已接入 `project_storage_adapter`，不再直接拼接 `DEVSLN_DIR`。
45. `save_algo_project_mng_into_file` 和 `load_algo_project_mng_from_file` 的默认 eMMC `project_mng.json` 路径已改为通过 `storage_project_get_index_path` 解析。
46. `delete_saved_project`、`scfw_delete_project` 和 `scfw_check_is_solution_file_exist` 的默认 eMMC `project_mng.json` 判断/删除路径已改为通过 `project_storage_adapter` 解析。
47. 新增 legacy eMMC 方案包根目录识别 API：`storage_project_is_emmc_package_root_path`，用于替换业务代码中直接比较 `DEVSLN_DIR` 的默认保存分支判断。
48. `scfw_save_as_project_to_ddr` 和 `scfw_save_project_to_path` 的运行工作目录移动、默认方案目录重建、失败回滚复制/删除路径已改为通过 `project_storage_adapter` 解析，不再直接拼接 `PROJ_DIR`/`DEFAULT_PROJ_DIR`。
49. `scfw_load_project` 失败清理工作目录和写回 `.sln` 上传/加载错误状态的路径已改为通过 `project_storage_adapter` 解析，不再直接拼接 `PROJ_DIR`/`DEVSLN_DIR`。
50. `scfw_delete_all_projects` 的 eMMC 方案包根目录删除/重建路径已改为通过 `storage_project_get_package_root_dir_path` 解析。
51. 新增按介质读取方案索引的框架接口：`scfw_get_project_num_by_media`、`scfw_get_project_info_by_media`、`scfw_get_project_switch_info_by_media`，并通过 `fwif_*_by_media` 导出。eMMC 仍走旧全局缓存；microSD 按 `storage_project_get_index_path(media)` 临时读取独立 `project_mng.json`。
52. `comif_get_project_list` 已抽出方案 JSON 输出 helper，并开始接入 eMMC + microSD 聚合列表：先输出 eMMC 方案，再尝试读取 microSD 独立 `project_mng.json`，SD 同名方案按 eMMC 优先策略屏蔽。
53. `scfw_save_project_to_path` 的非默认保存路径分支已改为通过 `storage_project_get_external_save_file_path` 构造外部 `.sln` 文件路径，不再直接拼接 `save_path + project_name.sln`。
54. 新增方案加载/删除 media-aware 框架接口：`scfw_load_project_by_media`、`scfw_delete_project_by_media`，并通过 `fwif_load_project_by_media`、`fwif_delete_project_by_media` 导出；旧 `AUTO` 入口保持 eMMC 优先兼容行为。
55. `scfw_load_project_by_media` 显式 microSD 时会按 SD `project_mng.json` 读取方案记录，加载/失败清理/错误状态写回路径使用解析出的目标介质路径。
56. `scfw_delete_project_by_media` 和内部 `delete_saved_project_by_media` 显式 microSD 时会读取并写回 SD 独立 `project_mng.json`，删除对应介质下的方案包、base image 和工作目录，不更新 eMMC 的全局 switch 管理缓存。
57. 新增方案重命名 media-aware 框架接口：`scfw_modify_project_name_by_media`，并通过 `fwif_modify_project_name_by_media` 导出；旧重命名接口保持 `AUTO` 兼容行为。
58. `modify_sln_file_info_by_media` 重命名后会按解析出的目标介质调用 `modify_algo_project_info_by_media` 写回对应 `project_mng.json`；显式 microSD 重命名不再写 eMMC index，也不更新 eMMC switch 管理缓存。

### 1.2 已验证

```bash
make -C tests/storage test
# storage tests passed

g++ -std=c++11 -Wall -Wextra -Werror -I source/middleware/storage -I source/fwk/project -c source/fwk/project/project_storage_adapter.cpp -o /tmp/project_storage_adapter.o
# exit 0

g++ -std=c++11 -Wall -Wextra -Werror -I source/middleware/storage -c source/middleware/storage/StorageApi.cpp -o /tmp/StorageApi.o
# exit 0

make -C source PLAT=328 PROD=sc3000 fwk/project/framework_proj.o
# 当前本地环境失败，失败点为既有平台头/工具链问题：
# EnDimension、MAX_PARAM_NAME_LEN、MAX_STYLE_LEN 未定义；
# utils.h/Utils.h 大小写告警；
# C 编译路径包含 C++ <string>。

make -C source PLAT=328 PROD=sc3000 app/comif/communication_interface.o
# 当前 macOS 本地环境失败，失败点为缺少 Linux 目标头 linux/rtc.h。

```

### 1.3 当前阶段小结

当前已完成 storage 基础层、方案路径适配层和 `framework_proj.c` 主要方案操作路径的第一轮接入。

1. 写入类路径已接入：默认保存、另存、上传，写入目标通过 `storage_project_resolve_save_paths` 解析，`AUTO` 仍按 SD 优先且 SD 不可用时报错。
2. 访问类路径已接入：加载、删除、修改切换信息，访问已有方案时通过 `storage_project_resolve_access_paths` 解析，`AUTO` 保持旧协议 eMMC 优先。
3. 重命名/复制类路径已接入：重命名和复制方案通过 `storage_project_resolve_rename_paths`、`storage_project_resolve_copy_paths` 统一解析源/目标路径。
4. 工作目录工具接口已补齐：支持工作目录根路径、指定方案工作目录、带尾斜杠目录、工作目录子路径。
5. 默认方案、保存/另存和释放方案相关目录清理已部分接入 adapter：默认方案目录、`vtool/ctool` 子目录、保存/另存工作目录移动和回滚、方案工作目录超限清理已不再直接拼接 `PROJ_DIR`。
6. 方案索引读取已扩展到按介质读取：eMMC 保持旧全局缓存，microSD 可读取独立 `project_mng.json`。
7. `comif_get_project_list` 已开始返回 eMMC + microSD 聚合列表，并保留 `StorageMedia`、`StorageStatus`、`IndexStatus` 字段；同名方案按 eMMC 优先屏蔽 SD。
8. comif 层已具备 microSD 状态查询、安全弹出、格式化函数，但协议/寄存器入口尚未绑定。

## 2. 未完成任务

1. `framework_proj.c` 的方案保存事务尚未全部替换；当前已完成 `scfw_save_project_to_path` 默认保存分支、`scfw_save_as_project_to_ddr`、`scfw_upload_project`、`scfw_load_project`、`delete_saved_project`、`scfw_delete_project`、`modify_emmc_sln_file_info`、`modify_emmc_sln_switch_info`、`scfw_copy_project_to_ddr`、`scfw_get_cur_project_path`、`scfw_get_project_path_ddr`、默认方案加载相关目录路径、`scfw_switch_project` 切换清理/恢复相关路径、`scfw_free_cur_project` 目录清理路径的主要方案包/工作目录/base image 路径适配；剩余待处理点主要包括：
   - `framework_proj.c` 中 `PROJ_DIR`/`DEVSLN_DIR`/`PROJ_MNG_FNAME` 的 active 代码直接拼接已基本清理，当前仅剩宏定义和 `#if 0` 旧代码块；后续是否删除兼容宏需结合全文件/外部引用确认。
2. SD/eMMC 独立 `project_mng.json` 已具备按介质读取接口，保存/另存/上传默认保存路径已按目标介质写入对应 index；加载/删除/重命名已新增携带 media 的框架入口并按目标介质读写 index；后续仍需接入上层协议调用。
3. `comif_get_project_list` 已接入 SD `project_mng.json` 记录源并做 eMMC+SD 聚合；后续仍需补 SD manifest 强校验、索引异常状态和扫描/修复入口。
4. 尚未实现 SD manifest 校验、扫描/修复。
5. 方案加载/删除/重命名已新增 media-aware 框架入口；上层协议尚未传递 media。
6. 尚未适配存图模块 `save_proc.*`。
7. 尚未适配图片 DB 多介质。
8. 尚未适配训练图像/测试图 DB 多介质。
9. 已新增 comif 层 SD 状态文件输出、安全弹出、格式化函数，但尚未绑定寄存器/命令入口；保存介质配置也尚未接入 UI/协议。
10. 尚未做目标设备交叉编译验证。
11. BSP 热插拔 API 和格式化工具确认后，还要替换当前常用 Linux 命令封装。

## 3. 当前涉及修改文件

### 3.1 本轮重点修改文件

1. `source/fwk/project/project_storage_adapter.h`
   - 新增和导出方案路径解析 API。
   - 当前包含 package、package root、workdir、workdir root、workdir dir、workdir child、base image、index、save/access/rename/copy 等路径解析接口。
   - 新增 legacy eMMC package root 判断接口，替代业务侧直接比较 `DEVSLN_DIR`。
   - 新增外部保存文件路径构造接口，用于 `scfw_save_project_to_path` 非默认保存目录。
2. `source/fwk/project/project_storage_adapter.cpp`
   - 实现所有方案路径解析逻辑。
   - 区分写入 `AUTO` 策略和访问已有方案 `AUTO` 策略。
   - 显式 `microSD` 访问/重命名/复制时增加 SD 状态或可写检查。
   - 统一做路径安全检查，复用 `StorageRouter::isSafeRelativePath`。
   - 实现外部保存 `.sln` 文件路径构造，兼容保存目录有无尾斜杠，并复用方案名安全校验。
3. `source/fwk/project/framework_proj.c`
   - 主要方案保存、加载、删除、重命名、复制、默认方案加载、当前方案路径查询、释放方案超限清理等路径逐步接入 `project_storage_adapter`。
   - 新增按目标文件创建父目录的辅助函数。
   - 新增按指定路径保存 `project_mng.json` 的辅助函数。
   - 新增访问路径解析辅助函数，统一记录 adapter 解析失败日志。
   - 当前方案工作目录移动、方案包目录扫描和默认 eMMC `project_mng.json` 读写路径继续替换为 adapter 解析。
4. `tests/storage/storage_unit_tests.cpp`
   - 增加方案路径解析 host 单元测试。
   - 覆盖 SD 优先写入、访问 eMMC 优先、显式 microSD、路径安全、重命名、复制、工作目录根路径/子路径等行为。
5. `source/fwk/interface/framework_if.c`
   - 导出按介质读取方案数量、方案信息、切换信息的 `fwif_*_by_media` 转发接口。
   - 导出按介质加载/删除方案的 `fwif_load_project_by_media`、`fwif_delete_project_by_media` 转发接口。
   - 导出按介质重命名方案的 `fwif_modify_project_name_by_media` 转发接口。
6. `source/fwk/interface/framework_if.h`
   - 声明按介质读取方案索引和方案信息的接口。
   - 声明按介质加载/删除方案的接口。
   - 声明按介质重命名方案的接口。
7. `source/app/comif/communication_interface.cpp`
   - `comif_get_project_list` 抽出统一 JSON 输出 helper。
   - `comif_get_project_list` 追加 microSD 记录源，并按 eMMC 优先屏蔽同名 SD 方案。
8. `doc/microSD_卡实现进度记录.md`
   - 持续记录 microSD 实现进度、验证命令、修改文件和剩余任务。

## 4. 文件变更清单

### 4.1 新增文件

1. `source/middleware/storage/StorageCommon.h`
2. `source/middleware/storage/StorageCommon.cpp`
3. `source/middleware/storage/FileOps.h`
4. `source/middleware/storage/FileOps.cpp`
5. `source/middleware/storage/MicroSdManager.h`
6. `source/middleware/storage/MicroSdManager.cpp`
7. `source/middleware/storage/StorageRouter.h`
8. `source/middleware/storage/StorageRouter.cpp`
9. `source/middleware/storage/WriteGuard.h`
10. `source/middleware/storage/WriteGuard.cpp`
11. `source/middleware/storage/StorageApi.h`
12. `source/middleware/storage/StorageApi.cpp`
13. `source/fwk/project/project_storage_adapter.h`
14. `source/fwk/project/project_storage_adapter.cpp`
15. `tests/storage/Makefile`
16. `tests/storage/storage_unit_tests.cpp`
17. `doc/microSD_卡实现进度记录.md`

### 4.2 修改文件

1. `source/app/main/main.c`
   - 新增 `extern int storage_init(void);`
   - 初始化表中新增 `{ 1, "storage_init", storage_init }`
2. `source/app/comif/communication_interface.cpp`
   - 引入 `StorageApi.h` 和 `project_storage_adapter.h`
   - 新增 `COMIF_FILE_MICROSD_STATE`
   - `comif_get_project_list` 增加 `StorageMedia`、`StorageStatus`、`IndexStatus`
   - `comif_get_project_list` 的 eMMC base image 路径改为通过 `storage_project_get_base_image_path` 获取
   - `comif_get_project_list` 抽出统一 JSON 输出 helper
   - `comif_get_project_list` 追加 microSD `project_mng.json` 记录源，按 eMMC 优先屏蔽同名 SD 方案
   - 新增 `comif_get_micro_sd_state`
   - 新增 `comif_safe_eject_micro_sd`
   - 新增 `comif_format_micro_sd`
3. `source/app/comif/communication_interface.h`
   - 新增 `comif_get_micro_sd_state` 声明
   - 新增 `comif_safe_eject_micro_sd` 声明
   - 新增 `comif_format_micro_sd` 声明
4. `source/fwk/project/project_storage_adapter.h`
   - 新增 `StorageProjectSavePaths`
   - 新增 `storage_project_get_workdir_root_dir_path` 声明
   - 新增 `storage_project_get_package_root_dir_path` 声明
   - 新增 `storage_project_get_workdir_dir_path` 声明
   - 新增 `storage_project_get_workdir_child_path` 声明
   - 新增 `storage_project_resolve_save_paths` 声明
   - 新增 `storage_project_resolve_access_paths` 声明
   - 新增 `storage_project_resolve_rename_paths` 声明
   - 新增 `storage_project_resolve_copy_paths` 声明
5. `source/fwk/project/project_storage_adapter.cpp`
   - 新增 `storage_project_get_workdir_root_dir_path` 实现，返回兼容旧调用方的尾斜杠工作目录根路径
   - 新增 `storage_project_get_package_root_dir_path` 实现，返回兼容旧调用方的尾斜杠方案包根路径
   - 新增 `storage_project_get_workdir_dir_path` 实现，返回兼容旧调用方的尾斜杠工作目录路径
   - 新增 `storage_project_get_workdir_child_path` 实现，安全拼接方案工作目录下的子路径
   - 新增 `storage_project_resolve_save_paths` 实现
   - 统一解析方案包、工作目录、base image、`project_mng.json` 路径和介质 generation
   - 新增 `storage_project_resolve_access_paths` 实现，区分写入 `AUTO` 策略和访问已有方案 `AUTO` 策略
   - 新增 `storage_project_resolve_rename_paths` 实现，统一解析重命名前后的方案路径
   - 新增 `storage_project_resolve_copy_paths` 实现，统一解析复制源和复制目标方案路径
6. `source/fwk/project/framework_proj.c`
   - 新增按目标文件创建父目录的内部辅助函数
   - 新增按指定路径保存 `project_mng.json` 的内部辅助函数
   - 新增方案访问路径解析内部辅助函数
   - `scfw_save_project_to_path` 默认保存分支接入 `project_storage_adapter`
   - `scfw_save_as_project_to_ddr` 接入 `project_storage_adapter`
   - `scfw_upload_project` 和 base image header hook 接入 `project_storage_adapter`
   - `scfw_load_project` 的解包源 `.sln`、工作目录、base image 路径接入 `project_storage_adapter`
   - `delete_saved_project` 和 `scfw_delete_project` 的删除路径接入 `project_storage_adapter`
   - `modify_emmc_sln_file_info` 的重命名 `.sln`、工作目录、base image、失败回滚路径接入 `project_storage_adapter`
   - `modify_emmc_sln_switch_info` 的 `.sln` 访问路径接入 `project_storage_adapter`
   - `scfw_copy_project_to_ddr` 的源 `.sln`、目标 `.sln`、目标 base image 路径接入 `project_storage_adapter`
   - `scfw_get_cur_project_path` 和 `scfw_get_project_path_ddr` 接入 `project_storage_adapter`
   - `scfw_load_default_project_with_PR`、`scfw_load_default_project`、`load_untitle_project`、`scfw_load_last_used_project` 的默认方案目录相关路径接入 `project_storage_adapter`
   - `scfw_switch_project` 的 SC2000E 旧方案工作目录清理路径接入 `project_storage_adapter`
   - `scfw_free_cur_project` 的工作目录根目录枚举和超限清理路径接入 `project_storage_adapter`
   - `scfw_set_cur_project_name` 的工作目录移动路径接入 `project_storage_adapter`
   - `scfw_collect_sln_files` 和 `scfw_sync_project_mng_with_sln_files` 的方案包目录扫描路径接入 `project_storage_adapter`
   - `save_algo_project_mng_into_file` 和 `load_algo_project_mng_from_file` 的默认 eMMC `project_mng.json` 路径接入 `project_storage_adapter`
   - `scfw_save_as_project_to_ddr` 和 `scfw_save_project_to_path` 的保存工作目录移动和失败回滚路径接入 `project_storage_adapter`
   - `scfw_load_project` 失败清理和 `.sln` 错误状态写回路径接入 `project_storage_adapter`
   - `scfw_delete_all_projects` 的方案包根目录删除/重建路径接入 `project_storage_adapter`
   - 新增按介质读取方案数量、方案信息、切换信息的内部和导出接口。
   - `scfw_save_project_to_path` 非默认保存路径分支改为通过 `storage_project_get_external_save_file_path` 构造 `.sln` 文件路径。
   - 新增按介质加载/删除方案接口，显式 microSD 时读写 SD 独立 `project_mng.json` 并使用 SD 路径清理文件。
   - 新增按介质重命名方案接口，显式 microSD 时从 SD index 读取旧方案信息并写回 SD 独立 `project_mng.json`。
7. `source/fwk/interface/framework_if.c`
   - 新增 `fwif_get_project_num_by_media`、`fwif_get_project_info_by_media`、`fwif_get_project_switch_info_by_media` 转发接口。
   - 新增 `fwif_load_project_by_media`、`fwif_delete_project_by_media` 转发接口。
   - 新增 `fwif_modify_project_name_by_media` 转发接口。
8. `source/fwk/interface/framework_if.h`
   - 新增按介质读取方案索引和方案信息的接口声明。
   - 新增按介质加载/删除方案接口声明。
   - 新增按介质重命名方案接口声明。
9. `source/app/comif/communication_interface.cpp`
   - `comif_get_project_list` 抽出统一 JSON 输出 helper。
   - 方案列表开始追加 microSD 介质记录，并按 eMMC 优先屏蔽同名 SD 方案。
10. `tests/storage/storage_unit_tests.cpp`
   - 新增 `storage_project_get_workdir_root_dir_path` 路径解析测试
   - 新增 `storage_project_get_package_root_dir_path` 路径解析测试
   - 新增 `storage_project_is_emmc_package_root_path` 默认保存路径识别测试
   - 新增 `storage_project_get_workdir_dir_path` 路径解析测试
   - 新增 `storage_project_get_workdir_child_path` 路径解析测试
   - 新增 `storage_project_resolve_save_paths` 路径解析测试
   - 新增 `storage_project_resolve_access_paths` 路径解析测试
   - 新增 `storage_project_resolve_rename_paths` 路径解析测试
   - 新增 `storage_project_resolve_copy_paths` 路径解析测试
   - 新增 `storage_project_get_external_save_file_path` 外部保存路径构造测试
   - 补充显式 microSD 重命名路径解析的 SD `project_mng.json` index 路径断言

### 4.3 删除文件

无源码文件删除。

测试生成物 `tests/storage/storage_unit_tests` 已通过以下命令清理：

```bash
make -C tests/storage clean
```
