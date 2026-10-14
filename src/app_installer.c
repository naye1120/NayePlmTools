/*
 * PS5 App Installer for Payload Manager
 * Based on the original implementation in ftpsrv by John Törnblom.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "app_installer.h"
#include "pldmgr.h"
#include <ps5/kernel.h>

#define INCASSET(name, file)                                                   \
  __asm__(".section .rodata\n"                                                 \
          ".global " #name "\n"                                                \
          ".global " #name "_end\n"                                            \
          ".global " #name "_size\n"                                           \
          ".align 16\n" #name ":\n"                                            \
          ".incbin \"" file "\"\n" #name "_end:\n" #name "_size:\n"            \
          ".quad " #name "_end - " #name "\n"                                  \
          ".previous\n");                                                      \
  extern const uint8_t name[];                                                 \
  extern const size_t name##_size;

/* [CN-FORK] 一套面板源码出两个主屏磁贴变体：默认 = 媒体栏（NAYE00001，
 * param.json 里 applicationCategoryType 65536）；加 -DTILE_VARIANT_GAME =
 * 游戏栏（NAYE00004，applicationCategoryType 0）。titleId 必须与所选
 * param 文件保持一致，否则 needs_update 比对与安装都会错位。 */
#ifdef TILE_VARIANT_GAME
#define TILE_TITLE_ID "NAYE00004"
#define TILE_PARAM_FILE "assets/param.game.json"
#else
#define TILE_TITLE_ID "NAYE00001"
#define TILE_PARAM_FILE "assets/param.json"
#endif

INCASSET(param_json, TILE_PARAM_FILE);
INCASSET(icon0_png, "assets/icon0.png");
/* 磁贴背景图：同一张 1920x1080 画布出两种格式一起安装（pic0.png + 真 BC1/DXT1
 * 的 pic0.dds），一次装机覆盖两种可能；VSH 到底只认哪个待实机定论。 */
INCASSET(pic0_png, "assets/pic0.png");
INCASSET(pic0_dds, "assets/pic0.dds");

int sceAppInstUtilInitialize(void);
int sceAppInstUtilTerminate(void);
int sceAppInstUtilAppInstallAll(void *);
int sceAppInstUtilAppUnInstall(const char *);

static int mkdir_p(const char *path, mode_t mode) {
  char tmp[256];
  snprintf(tmp, sizeof(tmp), "%s", path);
  size_t len = strlen(tmp);
  if (len == 0)
    return 0;
  if (tmp[len - 1] == '/')
    tmp[len - 1] = '\0';
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
        return -1;
      }
      *p = '/';
    }
  }
  if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
    return -1;
  }
  return 0;
}

static int install_file(const char *path, const uint8_t *data, size_t size) {
  FILE *f;
  if (!(f = fopen(path, "w"))) {
    return -1;
  }
  if (fwrite(data, size, 1, f) != 1) {
    fclose(f);
    return -1;
  }
  fclose(f);
  return 0;
}

static int install_app(const char *title_id, const char *dir) {
  int (*sceAppInstUtilAppInstallTitleDir)(const char *, const char *, void *) =
      0;
  const char *nid = "Wudg3Xe3heE";
  uint32_t handle;

  if (!kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle)) {
    sceAppInstUtilAppInstallTitleDir =
        (void *)kernel_dynlib_resolve(-1, handle, nid);
  }

  if (sceAppInstUtilAppInstallTitleDir) {
    return sceAppInstUtilAppInstallTitleDir(title_id, dir, 0);
  }

  return sceAppInstUtilAppInstallAll(0);
}

static int needs_update(const char *path, const uint8_t *expected_data,
                        size_t expected_size) {
  struct stat st;
  if (stat(path, &st) != 0)
    return 1;
  if (st.st_size != expected_size)
    return 1;

  FILE *f = fopen(path, "r");
  if (!f)
    return 1;

  uint8_t *buf = malloc(expected_size);
  if (!buf) {
    fclose(f);
    return 1;
  }

  if (fread(buf, 1, expected_size, f) != expected_size) {
    free(buf);
    fclose(f);
    return 1;
  }
  fclose(f);

  int mismatch = memcmp(buf, expected_data, expected_size);
  free(buf);

  return mismatch != 0;
}

int pldmgr_install_app_if_needed(void) {
  const char *title_id = TILE_TITLE_ID; /* [CN-FORK] 与 TILE_PARAM_FILE 的 titleId 一致；PS5 格式 4字母+5数字 */
  char base_dir[256];
  char param_path[256];
  char icon_path[256];
  char pic0_path[256];
  char pic0_dds_path[256];

  snprintf(base_dir, sizeof(base_dir), "/user/app/%s", title_id);
  snprintf(param_path, sizeof(param_path), "/user/app/%s/sce_sys/param.json",
           title_id);
  snprintf(icon_path, sizeof(icon_path), "/user/app/%s/sce_sys/icon0.png",
           title_id);
  snprintf(pic0_path, sizeof(pic0_path), "/user/app/%s/sce_sys/pic0.png",
           title_id);
  snprintf(pic0_dds_path, sizeof(pic0_dds_path),
           "/user/app/%s/sce_sys/pic0.dds", title_id);

  int update_needed = 0;
  struct stat st;
  if (stat(base_dir, &st) != 0) {
    update_needed = 1;
  } else {
    if (needs_update(param_path, param_json, param_json_size))
      update_needed = 1;
    if (needs_update(icon_path, icon0_png, icon0_png_size))
      update_needed = 1;
    /* 老磁贴没有背景图文件，stat 失败即判定需更新，重启后自动补上 */
    if (needs_update(pic0_path, pic0_png, pic0_png_size))
      update_needed = 1;
    if (needs_update(pic0_dds_path, pic0_dds, pic0_dds_size))
      update_needed = 1;
  }

  if (!update_needed) {
    return 0; /* Already installed and up to date */
  }

  if (stat(base_dir, &st) == 0) {
    pldmgr_log("[PLDMGR] Updating existing app launcher (%s)...\n", title_id);
    pldmgr_notify("Updating NayePlmTools...");
  } else {
    pldmgr_log("[PLDMGR] Installing browser launcher app (%s)...\n", title_id);
    pldmgr_notify("Installing NayePlmTools...");
  }

  int err;
  if ((err = sceAppInstUtilInitialize())) {
    pldmgr_log("[PLDMGR] sceAppInstUtilInitialize: error 0x%08X\n", err);
    return -1;
  }

  char sce_sys_dir[256];
  snprintf(sce_sys_dir, sizeof(sce_sys_dir), "/user/app/%s/sce_sys", title_id);
  if (mkdir_p(sce_sys_dir, 0755) != 0) {
    pldmgr_log("[PLDMGR] Failed to create app dir: %s (errno: %d)\n",
           sce_sys_dir, errno);
    sceAppInstUtilTerminate();
    return -1;
  }

  if (install_file(param_path, param_json, param_json_size)) {
    pldmgr_log("[PLDMGR] Failed to install param.json\n");
    sceAppInstUtilTerminate();
    return -1;
  }

  if (install_file(icon_path, icon0_png, icon0_png_size)) {
    pldmgr_log("[PLDMGR] Failed to install icon0.png\n");
    sceAppInstUtilTerminate();
    return -1;
  }

  /* 背景图属于增强项：写失败只记日志，不能因此丢掉磁贴本身 */
  int bg_failed = 0;
  if (install_file(pic0_path, pic0_png, pic0_png_size)) {
    pldmgr_log("[PLDMGR] Failed to install %s (errno %d)\n", pic0_path, errno);
    bg_failed |= 1;
  }
  if (install_file(pic0_dds_path, pic0_dds, pic0_dds_size)) {
    pldmgr_log("[PLDMGR] Failed to install %s (errno %d)\n", pic0_dds_path,
               errno);
    bg_failed |= 2;
  }
  /* 无论成败都留迹，否则从日志看不出背景图那步走没走到 */
  pldmgr_log("[PLDMGR] Tile background %s: %s (%lu B) + %s (%lu B), flags=0x%X\n",
             bg_failed ? "staging FAILED" : "staged",
             pic0_path, (unsigned long)pic0_png_size,
             pic0_dds_path, (unsigned long)pic0_dds_size, bg_failed);

  if ((err = install_app(title_id, "/user/app/"))) {
    pldmgr_log("[PLDMGR] install_app: error 0x%08X\n", err);
    sceAppInstUtilTerminate();
    return -1;
  }

  pldmgr_log("[PLDMGR] Launcher app installed successfully.\n");
  pldmgr_notify("NayePlmTools"); /* [CN-FORK] 简短占位：随后的启动通知会补发 IP:端口 */
  
  sceAppInstUtilTerminate();
  return 0;
}
