/* accounts — 写前备份 / 写后还原层实现
 *
 * 只依赖 POSIX + libSceRegMgr。备份日志是纯文本索引 + 原始字节 blob，
 * 这样即使工具本身坏了，人也能从 FTP 里直接把原值读出来手工回灌。
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "safety.h"

#define MAXREC 512

/* 权威签名第一参数是 long；这里与 npsignin.c 保持一致用 uint32_t，
 * entity 编号都 < 0x08000000，低 32 位足够。 */
int sceRegMgrGetInt(uint32_t key, int32_t *value);
int sceRegMgrGetStr(uint32_t key, char *value, size_t size);
int sceRegMgrGetBin(uint32_t key, void *value, size_t size);
int sceRegMgrSetInt(uint32_t key, int32_t value);
int sceRegMgrSetStr(uint32_t key, const char *value, size_t size);
int sceRegMgrSetBin(uint32_t key, const void *value, size_t size);

typedef struct {
  uint32_t ent;
  int      type;
  int      size;
  int      len;
  uint32_t scope;   /* 发起这次写入的 uid；0 = 无归属 */
} rec_t;

static rec_t  g_rec[MAXREC];
static int    g_nrec = -1;      /* -1 = 还没从磁盘读进来 */
static int    g_ready = 0;
static uint32_t g_scope = 0;    /* 当前写入归属，由 safety_set_scope() 设定 */

/* ------------------------------------------------------------------ *
 * 小工具                                                              *
 * ------------------------------------------------------------------ */

static int mkpath(const char *p)
{
  char buf[256];
  char *s;
  size_t n;

  n = strlen(p);
  if(n == 0 || n >= sizeof(buf)) return -1;
  memcpy(buf, p, n + 1);

  for(s = buf + 1; *s; s++) {
    if(*s != '/') continue;
    *s = 0;
    if(mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
    *s = '/';
  }
  if(mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
  return 0;
}

static int slurp(const char *path, unsigned char *buf, size_t cap)
{
  int fd, total = 0, n;

  fd = open(path, O_RDONLY);
  if(fd < 0) return -1;
  while((size_t)total < cap) {
    n = (int)read(fd, buf + total, cap - (size_t)total);
    if(n <= 0) break;
    total += n;
  }
  close(fd);
  return total;
}

static int spit(const char *path, const unsigned char *buf, size_t len)
{
  int fd;
  ssize_t n;

  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(fd < 0) return -1;
  n = write(fd, buf, len);
  close(fd);
  return (n == (ssize_t)len) ? 0 : -1;
}

static int exists(const char *path)
{
  struct stat st;
  return stat(path, &st) == 0;
}

static void blob_name(uint32_t ent, char *out, size_t cap)
{
  snprintf(out, cap, "%s/%u.bin", SAFETY_REG_DIR, ent);
}

/* 把索引文件读进 g_rec；只在第一次或写操作后失效时重读。 */
static int load_idx(void)
{
  char line[256];
  FILE *f;

  g_nrec = 0;
  f = fopen(SAFETY_REG_IDX, "r");
  if(!f) return 0;                     /* 没有日志 = 0 条，正常 */
  while(g_nrec < MAXREC && fgets(line, sizeof(line), f)) {
    unsigned int e, sc = 0;
    int t, sz, len;

    /* 第 5 个字段（归属）是后加的：老日志读不到就按"无归属"处理，
     * 这样全量还原仍然管得到它，绝不会因为格式换代把原值丢掉。 */
    if(sscanf(line, "%u %d %d %d %u", &e, &t, &sz, &len, &sc) < 4) continue;
    g_rec[g_nrec].ent = (uint32_t)e;
    g_rec[g_nrec].type = t;
    g_rec[g_nrec].size = sz;
    g_rec[g_nrec].len = len;
    g_rec[g_nrec].scope = sc;
    g_nrec++;
  }
  fclose(f);
  return g_nrec;
}

/* 把内存里的记录重写回索引文件。部分还原之后必须同步：已经回滚的那些 blob 已经
 * 删了，日志里再留着它们，重试就只会一直报"读不到原值"。 */
static int save_idx(void)
{
  FILE *f;
  int i;

  if(g_nrec < 0) return -1;
  f = fopen(SAFETY_REG_IDX, "w");
  if(!f) return -1;
  for(i = 0; i < g_nrec; i++)
    fprintf(f, "%u %d %d %d %u\n", g_rec[i].ent, g_rec[i].type,
            g_rec[i].size, g_rec[i].len, g_rec[i].scope);
  if(fclose(f) != 0) return -1;
  return 0;
}

static int find_rec(uint32_t ent)
{
  int i;

  if(g_nrec < 0) load_idx();
  for(i = 0; i < g_nrec; i++)
    if(g_rec[i].ent == ent) return i;
  return -1;
}

/* ------------------------------------------------------------------ *
 * 单个 entity 的读 / 写                                               *
 * ------------------------------------------------------------------ */

static int read_one(const safety_key_t *k, unsigned char *buf, int cap)
{
  int32_t iv = 0;

  memset(buf, 0, (size_t)cap);

  switch(k->type) {
  case SAFETY_INT:
    if(sceRegMgrGetInt(k->ent, &iv) != 0) return -1;
    memcpy(buf, &iv, sizeof(iv));
    return (int)sizeof(iv);
  case SAFETY_STR: {
    int n = k->size < cap ? k->size : cap;
    if(sceRegMgrGetStr(k->ent, (char *)buf, (size_t)n) != 0) return -1;
    return n;
  }
  case SAFETY_BIN: {
    int n = k->size < cap ? k->size : cap;
    if(sceRegMgrGetBin(k->ent, buf, (size_t)n) != 0) return -1;
    return n;
  }
  }
  return -1;
}

static int write_one(uint32_t ent, int type, const unsigned char *buf, int len)
{
  int32_t iv = 0;

  switch(type) {
  case SAFETY_INT:
    if(len < (int)sizeof(iv)) return -1;
    memcpy(&iv, buf, sizeof(iv));
    return sceRegMgrSetInt(ent, iv);
  case SAFETY_STR:
    return sceRegMgrSetStr(ent, (const char *)buf, (size_t)len);
  case SAFETY_BIN:
    return sceRegMgrSetBin(ent, buf, (size_t)len);
  }
  return -1;
}

/* ------------------------------------------------------------------ *
 * 公开接口                                                            *
 * ------------------------------------------------------------------ */

int safety_init(void)
{
  if(mkpath(SAFETY_DIR) != 0) return -1;
  if(mkpath(SAFETY_REG_DIR) != 0) return -1;
  if(mkpath(SAFETY_FILE_DIR) != 0) return -1;

  /* 探测可写：建不了就说明整层保护都是假的，必须让调用方知道 */
  if(spit(SAFETY_DIR "/.probe", (const unsigned char *)"x", 1) != 0) return -1;
  unlink(SAFETY_DIR "/.probe");

  g_ready = 1;
  if(g_nrec < 0) load_idx();
  return 0;
}

void safety_set_scope(uint32_t uid)
{
  g_scope = uid;
}

int safety_backup_reg(const safety_key_t *keys, int n, uint32_t off)
{
  unsigned char buf[4096];
  int i, done = 0, len;

  if(!g_ready && safety_init() != 0) return -1;
  if(g_nrec < 0) load_idx();

  for(i = 0; i < n; i++) {
    safety_key_t k = keys[i];
    char path[128];
    FILE *f;

    k.ent += off;                       /* 关键：备份的是将要写的确切 entity */

    if(find_rec(k.ent) >= 0) continue;  /* 已有记录，保留最早那份原值 */

    len = read_one(&k, buf, sizeof(buf));
    /* 读不出原值就整批拒绝。以前是 len=0 也照样记账放行，等于"备份成功"是假的：
     * 后面还原时无值可回，写入却已经发生 —— 这一条不能放行。 */
    if(len < 0) return -1;

    blob_name(k.ent, path, sizeof(path));
    if(spit(path, buf, (size_t)len) != 0) return -1;

    f = fopen(SAFETY_REG_IDX, "a");
    if(!f) return -1;
    fprintf(f, "%u %d %d %d %u\n", k.ent, k.type, k.size, len, g_scope);
    fclose(f);

    if(g_nrec < MAXREC) {
      g_rec[g_nrec].ent = k.ent;
      g_rec[g_nrec].type = k.type;
      g_rec[g_nrec].size = k.size;
      g_rec[g_nrec].len = len;
      g_rec[g_nrec].scope = g_scope;
      g_nrec++;
    } else {
      g_nrec = -1;                      /* 内存表满了，下次重新从磁盘读 */
    }
    done++;
  }
  return done;
}

/* 三个入口的共同前提：该 entity 必须已有备份，否则拒绝。 */
static int guarded(uint32_t ent)
{
  if(!g_ready && safety_init() != 0) return -1;
  if(find_rec(ent) < 0) return -1;
  return 0;
}

int safety_set_int(uint32_t ent, int32_t v)
{
  unsigned char b[4];

  if(guarded(ent) != 0) return -1;
  memcpy(b, &v, sizeof(v));
  return write_one(ent, SAFETY_INT, b, sizeof(b));
}

int safety_set_str(uint32_t ent, const char *s, int size)
{
  if(guarded(ent) != 0) return -1;
  return sceRegMgrSetStr(ent, s, (size_t)size);
}

int safety_set_bin(uint32_t ent, const void *p, int size)
{
  if(guarded(ent) != 0) return -1;
  return sceRegMgrSetBin(ent, p, (size_t)size);
}

int safety_reg_dirty(void)
{
  if(g_nrec < 0) load_idx();
  return g_nrec > 0 ? 1 : 0;
}

int safety_reg_count(void)
{
  if(g_nrec < 0) load_idx();
  return g_nrec;
}

int safety_reg_count_scope(uint32_t uid)
{
  int i, n = 0;

  if(g_nrec < 0) load_idx();
  for(i = 0; i < g_nrec; i++)
    if(g_rec[i].scope == uid) n++;
  return n;
}

int safety_reg_at(int i, uint32_t *ent, int *type, int *size, int *len,
                  uint32_t *scope)
{
  if(g_nrec < 0) load_idx();
  if(i < 0 || i >= g_nrec) return -1;
  if(ent)   *ent   = g_rec[i].ent;
  if(type)  *type  = g_rec[i].type;
  if(size)  *size  = g_rec[i].size;
  if(len)   *len   = g_rec[i].len;
  if(scope) *scope = g_rec[i].scope;
  return 0;
}

/* scope == SAFETY_SCOPE_ALL 时不看归属、全部回滚；否则只动这个 uid 名下的记录，
 * 别人的记录连 blob 一起原样留在盘上。 */
static int restore_matching(uint32_t scope)
{
  unsigned char orig[4096], now[4096];
  char drop[MAXREC];
  char path[128];
  int i, w, ok = 0, bad = 0;

  if(g_nrec < 0) load_idx();
  memset(drop, 0, sizeof(drop));

  /* 逆序：后写的先还原，避免两个槽共用一条记录时被覆盖顺序影响判断 */
  for(i = g_nrec - 1; i >= 0; i--) {
    int rl, gl;
    safety_key_t k;

    if(scope != SAFETY_SCOPE_ALL && g_rec[i].scope != scope) continue;

    blob_name(g_rec[i].ent, path, sizeof(path));

    memset(orig, 0, sizeof(orig));
    rl = slurp(path, orig, sizeof(orig));
    if(rl < 0) { bad++; continue; }

    if(write_one(g_rec[i].ent, g_rec[i].type, orig, rl) != 0) { bad++; continue; }

    /* 回读比对：不信任 Set 的返回值，只信任读回来的字节 */
    memset(now, 0, sizeof(now));
    k.ent = g_rec[i].ent;
    k.type = g_rec[i].type;
    k.size = g_rec[i].size;
    gl = read_one(&k, now, sizeof(now));
    if(gl < 0) { bad++; continue; }
    if(gl != rl || memcmp(orig, now, (size_t)rl) != 0) { bad++; continue; }

    unlink(path);
    drop[i] = 1;
    ok++;
  }

  if(ok > 0) {
    /* 成功的必须从日志里摘掉：它们的 blob 已经没了，留着只会让下一次重试
     * 一直报"读不到原值"。失败的那些继续留在日志里，等人工重试。 */
    for(i = 0, w = 0; i < g_nrec; i++) {
      if(drop[i]) continue;
      if(w != i) g_rec[w] = g_rec[i];
      w++;
    }
    g_nrec = w;
    if(save_idx() != 0) { g_nrec = -1; return bad ? -bad : -1; }
  }
  return bad ? -bad : ok;
}

int safety_restore_scope(uint32_t uid)
{
  return restore_matching(uid);
}

int safety_restore_reg(void)
{
  return restore_matching(SAFETY_SCOPE_ALL);
}

/* ------------------------------------------------------------------ *
 * 文件层                                                              *
 * ------------------------------------------------------------------ */

static void file_blob(const char *path, char *out, size_t cap)
{
  char clean[256];
  size_t i;

  for(i = 0; path[i] && i + 1 < sizeof(clean); i++)
    clean[i] = (path[i] == '/') ? '%' : path[i];
  clean[i] = 0;
  snprintf(out, cap, "%s/%s.bak", SAFETY_FILE_DIR, clean);
}

static void idx_line(const char *path, const char *state)
{
  FILE *f = fopen(SAFETY_FILE_IDX, "a");
  if(!f) return;
  fprintf(f, "%s %s\n", state, path);
  fclose(f);
}

int safety_backup_file(const char *path)
{
  char bak[288];
  unsigned char *buf;
  struct stat st;
  int len;
  FILE *f;

  if(!g_ready && safety_init() != 0) return -1;
  if(!exists(path)) {
    /* 也要记：这条 EXIST/ABSENT 记录是"只删自造文件"的唯一判据 */
    f = fopen(SAFETY_FILE_IDX, "a");
    if(f) { fprintf(f, "ABSENT %s\n", path); fclose(f); }
    return 0;
  }
  if(stat(path, &st) != 0) return -1;
  if(st.st_size <= 0 || st.st_size > (off_t)(4 << 20)) return -1;

  buf = malloc((size_t)st.st_size);
  if(!buf) return -1;
  len = slurp(path, buf, (size_t)st.st_size);
  if(len != (int)st.st_size) { free(buf); return -1; }

  file_blob(path, bak, sizeof(bak));
  if(spit(bak, buf, (size_t)len) != 0) { free(buf); return -1; }
  free(buf);

  idx_line(path, "EXIST");
  return 0;
}

/* 在 files.idx 里查这条路径最后一次登记的状态。
 * 1 = ABSENT（自造的），0 = EXIST（有原件），-1 = 没登记过或已经还原过(DONE)。 */
int safety_file_was_absent(const char *path)
{
  char line[512];
  FILE *f;
  int res = -1;

  f = fopen(SAFETY_FILE_IDX, "r");
  if(!f) return -1;
  while(fgets(line, sizeof(line), f)) {
    char *sp, *nl;
    size_t plen = strlen(path);

    nl = strpbrk(line, "\r\n");
    if(nl) *nl = 0;
    sp = strchr(line, ' ');
    if(!sp) continue;
    *sp = 0;
    if(!strcmp(sp + 1, path) && strlen(sp + 1) == plen) {
      if(!strcmp(line, "ABSENT"))      res = 1;
      else if(!strcmp(line, "EXIST"))  res = 0;
      else                             res = -1;   /* DONE：本次已经还原过了 */
    }
  }
  fclose(f);
  return res;
}

int safety_restore_file(const char *path)
{
  char bak[288];
  unsigned char *buf = NULL;
  int rl, rc = -1;
  struct stat st;

  /* 没登记过、或已经标过 DONE 的路径都无事可做。此时 blob 早已被消费掉，
   * 再走一遍只会把"无事可做"报成"还原失败"。 */
  if(safety_file_was_absent(path) < 0) return -1;

  file_blob(path, bak, sizeof(bak));
  if(stat(bak, &st) == 0 && st.st_size > 0) {
    buf = malloc((size_t)st.st_size);
    if(!buf) return -1;
    rl = slurp(bak, buf, (size_t)st.st_size);
    if(rl == (int)st.st_size && spit(path, buf, (size_t)rl) == 0) rc = 0;
    free(buf);
    if(rc == 0) {
      unlink(bak);
      idx_line(path, "DONE");        /* append-only：把这条登记标成已消费 */
    }
    return rc;
  }

  /* 没有 blob 但登记过 ABSENT = 文件是我们造的，删掉 */
  if(safety_file_was_absent(path) == 1) {
    unlink(path);
    idx_line(path, "DONE");
    return 0;
  }
  return -1;
}

int safety_file_dirty(void)
{
  /* 光看索引文件在不在不行：还原成功后行还在（DONE），要按"还欠几条"算。 */
  return safety_file_count() > 0 ? 1 : 0;
}

/* 把 files.idx 里"还欠着还原"的路径收集起来：按首次出现顺序去重，
 * 每条取最后一次登记的状态，DONE 的跳过（已经消费过了）。 */
static int collect_file_paths(char paths[][256], int cap)
{
  char line[512];
  FILE *f;
  int n = 0;

  f = fopen(SAFETY_FILE_IDX, "r");
  if(!f) return 0;
  while(fgets(line, sizeof(line), f)) {
    char *sp, *nl, *state, *path;
    int i, seen = -1;

    nl = strpbrk(line, "\r\n");
    if(nl) *nl = 0;
    sp = strchr(line, ' ');
    if(!sp || !sp[1]) continue;
    *sp = 0;                       /* line = 状态，sp+1 = 路径 */
    state = line;
    path = sp + 1;
    if(strlen(path) >= 256) continue;

    for(i = 0; i < n; i++)
      if(!strcmp(paths[i], path)) { seen = i; break; }

    if(seen >= 0) {
      /* 同一路径后来的状态说了算：DONE 把坑位清空，EXIST/ABSENT 再写回 */
      if(strcmp(state, "EXIST") && strcmp(state, "ABSENT")) paths[seen][0] = 0;
      else strncpy(paths[seen], path, 255);
      continue;
    }
    if(!strcmp(state, "EXIST") || !strcmp(state, "ABSENT")) {
      if(n >= cap) continue;
      strncpy(paths[n], path, 255);
      paths[n][255] = 0;
      n++;
    }
  }
  fclose(f);

  /* 把被标成 DONE 的坑位挤掉，保证返回的都是待还原路径 */
  {
    int i, m = 0;
    for(i = 0; i < n; i++)
      if(paths[i][0]) { if(m != i) memcpy(paths[m], paths[i], 256); m++; }
    return m;
  }
}

int safety_file_count(void)
{
  static char paths[128][256];
  return collect_file_paths(paths, 128);
}

int safety_restore_all_files(void)
{
  static char paths[128][256];
  int n, i, ok = 0, bad = 0;

  n = collect_file_paths(paths, 128);
  if(n <= 0) return 0;

  for(i = n - 1; i >= 0; i--) {
    if(safety_restore_file(paths[i]) == 0) ok++;
    else bad++;
  }

  if(bad == 0) {
    truncate(SAFETY_FILE_IDX, 0);
    return ok;
  }
  return -bad;
}

void safety_status_line(char *out, size_t cap)
{
  int r, fv;

  if(g_nrec < 0) load_idx();
  r = g_nrec;
  fv = safety_file_count();

  snprintf(out, cap,
           "safety: reg_backups=%d file_backups=%d", r, fv);
}
