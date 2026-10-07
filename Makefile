# NayePlmTools - Native PS5 ELF Daemon Makefile

# Tools
PYTHON := python3
CC     := /opt/ps5-payload-sdk/bin/prospero-clang
STRIP  := /opt/ps5-payload-sdk/bin/prospero-strip

# Paths
SDK      := /opt/ps5-payload-sdk
TARGET   := $(SDK)/target
INCLUDES := -Iinclude -I$(TARGET)/include \
           -Isrc/psaccounts -Isrc/psaccounts/third_party \
           -Ithird_party/sqlite3 \
           -Ithird_party/libsmb2/include
# [CN-FORK] v0.5.2.99 PS5 Account Tools 源码并入需要的两个库（SDK 里是 .so）：
#   -lkernel_sys     sys_lite.c 的凭据提权（kernel_set_ucred / root / jail）
#   -lSceRemoteplay  accounts.c 生成远程游玩配对 PIN（只读，不调用即不占会话）
# [CN-FORK] v0.5.2.143 SMB：SDK 的 libcurl 没编 smb，这里补一个用户态的 SMB2/3 客户端
#   libsmb2（sahlberg/libsmb2 6.2，pacbrew 包名 ps5-payload-libsmb2）。
#   注意**别以为「libcurl 没有 smb」就等于「PS5 做不了 SMB」** —— 那是 .142 犯过的错。
#   预编译产物直接进仓，理由与 third_party/pkg-manager.elf 一样：出包不能依赖网络，
#   而 libsmb2 是 cmake 工程，不适合像 sqlite3 那样把源码并进 $(SRCS)。
#   要重编：bash third_party/libsmb2/build_ps5.sh（脚本内带 sha256 校验与构建步骤）。
#   源码副本也在同目录（libsmb2-6.2.tar.gz）。实测代价：ELF +约 241 KB。
LIBS     := $(TARGET)/lib/libcurl.a \
            $(TARGET)/lib/libmbedtls.a \
            $(TARGET)/lib/libmbedx509.a \
            $(TARGET)/lib/libmbedcrypto.a \
            $(TARGET)/lib/libmicrohttpd.a \
            third_party/libsmb2/libsmb2.a \
            -L$(TARGET)/lib -lpthread \
            -lSceNetCtl -lSceUserService -lSceSystemService \
            -lSceAppInstUtil -lSceHttp2 -lSceSsl -lSceNet -lSceRegMgr \
            -lkernel_sys -lSceRemoteplay

# Source Files
# [CN-FORK] v0.5.1.64 src/stats.c 已删除（主页硬件信息功能下线）
SRCS := src/main.c src/http_server.c src/config.c src/log_server.c \
        src/sha256.c src/json_helpers.c src/repository.c src/sources.c \
        src/payload_mgr.c src/process_mgr.c \
        src/ps5_launcher.c src/notification.c src/utils.c src/autoload.c \
        src/app_installer.c src/history_mgr.c \
        src/pkgmanager.c src/pkgmanager_blob.S src/quicklinks.c src/thermal.c src/game_mgr.c src/fan_control.c
# [CN-FORK] v0.5.2.99 PS5 Account Tools 源码并入主 ELF（常用配置「账号 / 远程游玩」板块）
# api.c 是原工程 websrv.c 的并入版：只留 /api/… 接口，监听循环/内嵌首页/exit 都不要了
SRCS += src/psaccounts/api.c src/psaccounts/accounts.c src/psaccounts/npsignin.c \
        src/psaccounts/offact.c src/psaccounts/avatar.c src/psaccounts/safety.c \
        src/psaccounts/sys_lite.c \
        src/psaccounts/third_party/cJSON.c src/psaccounts/third_party/stb_impl.c
# [CN-FORK] v0.5.2.115 游戏管理 → 多媒体管理（截图/录屏）后端
SRCS += src/game_media.c src/game_job.c src/zip_io.c src/inflate.c
# [CN-FORK] v0.5.2.116 游戏管理余下的后端：改名+磁贴两层图 / 存档 / 运行时（删源·挂载·卸载）
# 三者之间只靠 include 里的声明互相说话：game_title 写完要让 game_mgr 的列表缓存作废，
# game_runtime 删完同样要 —— 所以 game_title.o、game_runtime.o 都依赖 game_mgr.o。
SRCS += src/game_title.c src/game_saves.c src/game_runtime.c
# [CN-FORK] v0.5.2.142 存档的远程传输（FTP/HTTP）+ 自动存档执行引擎
# curl_init.c 单独一个 TU：utils.c 被一堆纯文件系统的宿主测试链着，curl 符号
# 不能进 utils.o（见 src/curl_init.c 文件头）。save_remote.c 与 game_runtime.c 都调它。
SRCS += src/save_remote.c src/game_auto.c src/curl_init.c
# [CN-FORK] v0.5.2.118 磁贴写回主机那一步：改名要写 app.db + appinfo.db 的平铺名字列，
# 换背景要写 app.db 的 pic0Info —— 都得真跑 SQL，见 third_party/sqlite3 那段规则。
SRCS += src/tile_db.c
OBJS := $(SRCS:.c=.o)
# [CN-FORK] 版本取自 pldmgr.h MENU_VERSION；ELF 产物名带版本号：NayePlmTools_<version>.elf
VERSION := $(shell grep '#define MENU_VERSION' include/pldmgr.h | awk '{print $$3}' | tr -d '"')
ELF  := NayePlmTools_$(VERSION).elf

# Assets
FRONTEND_DIST := frontend/dist/index.html
ASSET_HEADER  := include/assets_index_html.h
CA_HEADER     := include/assets_cacert_pem.h
MANIFEST_HEADER := include/assets_cache_appcache.h
FRONTEND_MANIFEST := frontend/dist/cache.appcache
FAVICON_SVG_HEADER := include/assets_favicon_svg.h
ICON_PNG_HEADER := include/assets_icon_png.h




# [CN-FORK] 磁贴变体开关：留空 = 媒体栏（NAYE00001）；-DTILE_VARIANT_GAME = 游戏栏（NAYE00004）
VARIANT_CFLAGS ?=

# Compiler Flags
CFLAGS := -Os -Wall -ffunction-sections -fdata-sections $(INCLUDES) $(VARIANT_CFLAGS)
LDFLAGS := -Wl,--gc-sections

all: $(ELF)

# Build the React frontend
.PHONY: frontend-build
frontend-build:
	@echo "Building frontend..."
	git submodule update --init --recursive
	@VERSION=$$(grep '#define MENU_VERSION' include/pldmgr.h | awk '{print $$3}' | tr -d '"'); \
	echo "Building frontend with APP_VERSION=$$VERSION (burned in for runtime version handshake)"; \
	cd frontend && npm install && APP_VERSION=$$VERSION npm run build
	@VERSION=$$(grep '#define MENU_VERSION' include/pldmgr.h | awk '{print $$3}' | tr -d '"'); \
	COMMIT=$$(git rev-parse --short HEAD 2>/dev/null || echo "unknown"); \
	git diff --quiet || COMMIT="DEV"; \
	DATE=$$(date -u +"%Y-%m-%d %H:%M:%S UTC"); \
	TITLE="NayePlmTools v$$VERSION by naye ($$COMMIT, built at $$DATE)"; \
	echo "Updating title in index.html to: $$TITLE"; \
	TMP=$$(mktemp "$${TMPDIR:-/tmp}/nayeplmtools.XXXXXX"); \
	sed "s|\[\[TITLE_PLACEHOLDER\]\]|$$TITLE|g" frontend/dist/index.html > $$TMP; \
	mv $$TMP frontend/dist/index.html; \
	echo "Updating build date in cache.appcache to: $$DATE"; \
	TMP=$$(mktemp "$${TMPDIR:-/tmp}/nayeplmtools.XXXXXX"); \
	sed "s|\[\[BUILD_DATE\]\]|$$DATE|g" frontend/dist/cache.appcache > $$TMP; \
	mv $$TMP frontend/dist/cache.appcache



$(ASSET_HEADER): $(FRONTEND_DIST)
	@echo "Injecting title into index.html..."
	@VERSION=$$(grep '#define MENU_VERSION' include/pldmgr.h | awk '{print $$3}' | tr -d '"'); \
	COMMIT=$$(git rev-parse --short HEAD 2>/dev/null || echo "unknown"); \
	DATE=$$(date -u +"%Y-%m-%d %H:%M:%S UTC"); \
	TITLE="NayePlmTools v$$VERSION by naye ($$COMMIT, built at $$DATE)"; \
	TMP=$$(mktemp "$${TMPDIR:-/tmp}/nayeplmtools.XXXXXX"); \
	sed "s|\[\[TITLE_PLACEHOLDER\]\]|$$TITLE|g" $(FRONTEND_DIST) > $$TMP && mv $$TMP $(FRONTEND_DIST); \
	echo "Title set to: $$TITLE"
	@echo "Generating asset header..."
	$(PYTHON) tools/gen_assets.py $(FRONTEND_DIST) $(ASSET_HEADER) index_html

$(MANIFEST_HEADER): $(FRONTEND_DIST)
	@echo "Injecting build date into cache.appcache (AppCache update trigger)..."
	@DATE=$$(date -u +"%Y-%m-%d %H:%M:%S UTC"); \
	TMP=$$(mktemp "$${TMPDIR:-/tmp}/nayeplmtools.XXXXXX"); \
	sed "s|\[\[BUILD_DATE\]\]|$$DATE|g" $(FRONTEND_MANIFEST) > $$TMP && mv $$TMP $(FRONTEND_MANIFEST); \
	echo "Manifest build stamp: $$DATE"
	@echo "Generating Manifest asset header..."
	$(PYTHON) tools/gen_assets.py $(FRONTEND_MANIFEST) $(MANIFEST_HEADER) cache_appcache

$(FAVICON_SVG_HEADER): $(FRONTEND_DIST)
	@echo "Generating Favicon SVG asset header..."
	$(PYTHON) tools/gen_assets.py frontend/dist/favicon.svg $(FAVICON_SVG_HEADER) favicon_svg

$(ICON_PNG_HEADER): $(FRONTEND_DIST)
	@echo "Generating Icon PNG asset header..."
	$(PYTHON) tools/gen_assets.py frontend/dist/icon.png $(ICON_PNG_HEADER) icon_png

$(CA_HEADER):
	@echo "Downloading CA bundle..."
	wget -O include/cacert.pem https://curl.se/ca/cacert.pem
	$(PYTHON) tools/gen_assets.py include/cacert.pem $(CA_HEADER) cacert_pem
	rm include/cacert.pem

$(FRONTEND_DIST):
	@echo "ERROR: frontend/dist/index.html not found!"
	@echo "Please run 'make frontend-build' locally on your host machine first."
	@exit 1

# [CN-FORK] v0.5.2.118 静态 SQLite（合并源码）：改名要 UPDATE app.db / appinfo.db 的平铺
# 名字列，换背景要 UPDATE app.db 的 pic0Info —— 固件不带 libSceSqlite.sprx、SDK 也没有
# stub，sqlite3_* 只能整个编进来。单独一条规则、单独一套 CFLAGS，三个理由：
#   ① -Wall 套在合并版上会把上游既有的风格提示算进我们「零新增警告」的账里 → -w 关掉；
#   ② THREADSAFE=1 —— 参考工程是单线程独立 ELF 所以取 0，本进程是 MHD 线程池，
#      改名与换背景可能同时落到两条连接上，得让 SQLite 自己串行化；
#   ③ 一堆 SQLITE_OMIT_* 把 JSON / 扩展加载 / TCL 变量等用不到的功能摘掉，再靠
#      -ffunction-sections + --gc-sections 把死代码丢出 ELF，压住 ELF 体积。
SQLITE_DIR := third_party/sqlite3
SQLITE_OBJ := $(SQLITE_DIR)/sqlite3.o
SQLITE_CFLAGS := -I$(SQLITE_DIR) -w -Os -ffunction-sections -fdata-sections \
	-DSQLITE_OS_UNIX=1 -DSQLITE_THREADSAFE=1 -DSQLITE_TEMP_STORE=2 \
	-DSQLITE_OMIT_LOAD_EXTENSION=1 -DSQLITE_OMIT_DEPRECATED=1 \
	-DSQLITE_OMIT_JSON=1 -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_DQS=0 \
	-DSQLITE_UNTESTABLE=1 -DSQLITE_OMIT_TCL_VARIABLE=1 \
	-DSQLITE_OMIT_PROGRESS_CALLBACK=1 -DSQLITE_OMIT_TRACE

$(SQLITE_OBJ): $(SQLITE_DIR)/sqlite3.c $(SQLITE_DIR)/sqlite3.h
	$(CC) $(SQLITE_CFLAGS) -c $< -o $@

$(ELF): $(ASSET_HEADER) $(MANIFEST_HEADER) $(CA_HEADER) $(FAVICON_SVG_HEADER) $(ICON_PNG_HEADER) $(SRCS) $(SQLITE_OBJ)
	@echo "Building $(ELF)..."
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(ELF) $(SRCS) $(SQLITE_OBJ) $(LIBS)
	@echo "Stripping $(ELF)..."
	$(STRIP) $(ELF)

clean:
	rm -f $(ELF) $(ASSET_HEADER) $(MANIFEST_HEADER) $(CA_HEADER) $(FAVICON_SVG_HEADER) $(ICON_PNG_HEADER) src/*.o $(SQLITE_OBJ)





dist-clean: clean
	rm -rf frontend/dist

.PHONY: all clean frontend-build dist-clean
