# LOOT SMITH build file.
#   make deps    - (optional) fetch + build raylib into third_party/raylib
#   make         - build ./lootsmith
#   make test    - build and run the logic tests (no raylib needed)
#   make run     - build and start the game
#   make install - install to $(PREFIX) (default /usr/local): bin/lootsmith + share/lootsmith/assets
# raylib lookup order: RAYLIB_PATH=<raylib checkout> > third_party/raylib > pkg-config.

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2
# ソース一覧。ディレクトリごとの責務は docs/DEVELOPMENT.md を参照。
# core: ゲームロジック (raylib 不要)
CORE_SRC = $(addprefix src/core/, content_tables.c content_validate.c rng.c game_state.c upgrades.c market.c \
           crafting.c inventory.c customers.c day_flow.c events.c guild_rank.c guild.c save.c icons.c)
# audio の純 C 部分 (音量設定・効果音 ID・アセット探索): raylib なしでテストできる
AUDIO_C  = $(addprefix src/audio/, settings.c sfx.c asset_path.c)
BOT_SRC  = src/bot/bot.c
UI_SRC   = $(addprefix src/ui/, ui_draw.c ui_widgets.c ui_chrome.c ui_app.c screen_home.c screen_shop.c screen_craft.c \
           screen_inventory.c screen_customer.c screen_report.c screen_guild.c screen_options.c screen_help.c screen_bot.c)
UI       = $(UI_SRC) src/main.c src/audio/audio.c
LOGIC    = $(CORE_SRC)
# どのヘッダを変えても再ビルドする
HEADERS  = $(wildcard src/core/*.h src/audio/*.h src/ui/*.h src/bot/*.h)
# 各ファイルは "core/simulation.h" のようにモジュール名付きで include する
INCLUDES = -Isrc
PREFIX  ?= /usr/local
UNAME   := $(shell uname -s)

ifdef RAYLIB_PATH
  RL_DIR = $(RAYLIB_PATH)
else ifneq ($(wildcard third_party/raylib/src/libraylib.a),)
  RL_DIR = third_party/raylib
endif

ifdef RL_DIR
  RL_CFLAGS = -I$(RL_DIR)/src
  ifeq ($(UNAME),Darwin)
    RL_LIBS = $(RL_DIR)/src/libraylib.a -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
  else
    RL_LIBS = $(RL_DIR)/src/libraylib.a -lGL -lpthread -ldl -lrt -lX11
  endif
else
  RL_CFLAGS = $(shell pkg-config --cflags raylib 2>/dev/null)
  RL_LIBS   = $(shell pkg-config --libs raylib 2>/dev/null || echo -lraylib)
endif

all: lootsmith

# ゲームは assets/ を実行ファイルの隣、../assets、../share/lootsmith/assets、./assets、コンパイル時既定値の順に探す (src/audio/audio.c)。
lootsmith: $(LOGIC) $(BOT_SRC) $(AUDIO_C) $(UI) $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) $(RL_CFLAGS) $(LOGIC) $(BOT_SRC) $(AUDIO_C) $(UI) -o $@ $(RL_LIBS) -lm

tests/test_sim: tests/test_sim.c $(LOGIC) $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) -g -fsanitize=address,undefined $< $(LOGIC) -o $@ -lm

tests/test_ascendant: tests/test_ascendant.c $(LOGIC) $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) -g -fsanitize=address,undefined $< $(LOGIC) -o $@ -lm

tests/test_audio: tests/test_audio.c $(AUDIO_C) $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) -g -fsanitize=address,undefined -DLOOTSMITH_ASSET_DIR=\"assets\" $< $(AUDIO_C) -o $@ -lm

tests/test_bot: tests/test_bot.c $(BOT_SRC) $(LOGIC) $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) -g -fsanitize=address,undefined $< $(BOT_SRC) $(LOGIC) -o $@ -lm

test: tests/test_sim tests/test_ascendant tests/test_audio tests/test_bot
	./tests/test_sim
	./tests/test_ascendant
	./tests/test_audio
	./tests/test_bot

run: lootsmith
	./lootsmith

install: lootsmith
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/lootsmith
	install -m 755 lootsmith $(DESTDIR)$(PREFIX)/bin/lootsmith
	cp -R assets $(DESTDIR)$(PREFIX)/share/lootsmith/

deps:
	@if [ -d third_party/raylib/.git ]; then \
		echo "Using existing third_party/raylib checkout"; \
	else \
		mkdir -p third_party && git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git third_party/raylib; \
	fi
	$(MAKE) -C third_party/raylib/src PLATFORM=PLATFORM_DESKTOP

clean:
	rm -f lootsmith tests/test_sim tests/test_audio tests/test_bot *.tmp

.PHONY: all test run install deps clean
