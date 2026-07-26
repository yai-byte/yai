CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -pthread
LDFLAGS ?=

TARGET := yai
SRC := \
	src/arch.cpp \
	src/batch_progress_event.cpp \
	src/batch_ui.cpp \
	src/appimage.cpp \
	src/appimage_desktop.cpp \
	src/appimage_runtime.cpp \
	src/cli_download.cpp \
	src/commands.cpp \
	src/commands_doctor.cpp \
	src/commands_lifecycle.cpp \
	src/commands_query.cpp \
	src/commands_repo.cpp \
	src/commands_update.cpp \
	src/commands_upgrade.cpp \
	src/core.cpp \
	src/download_progress.cpp \
	src/i18n.cpp \
	src/json.cpp \
	src/main.cpp \
	src/process.cpp \
	src/repo.cpp \
	src/repo_feed.cpp \
	src/terminal_color.cpp \
	src/url_freshness.cpp \
	src/resolver.cpp \
	src/resolver_github.cpp \
	src/resolver_url.cpp \
	src/resolver_website.cpp

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

install: $(TARGET)
	install -Dm755 $(TARGET) "$(HOME)/.local/bin/$(TARGET)"
	for file in po/*.po; do install -Dm644 "$$file" "$(HOME)/.local/share/yai/po/$$(basename "$$file")"; done

clean:
	rm -f $(TARGET)
