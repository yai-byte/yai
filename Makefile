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
	src/commands_repo_resolve.cpp \
	src/commands_update.cpp \
	src/commands_upgrade.cpp \
	src/core.cpp \
	src/download_progress.cpp \
	src/i18n.cpp \
	src/json.cpp \
	src/main.cpp \
	src/process.cpp \
	src/repo.cpp \
	src/repo_appimage_github.cpp \
	src/repo_feed.cpp \
	src/repo_index_urls.cpp \
	src/terminal_color.cpp \
	src/url_freshness.cpp \
	src/resolver.cpp \
	src/resolver_gitlab.cpp \
	src/resolver_github.cpp \
	src/resolver_url.cpp \
	src/resolver_website.cpp

# Library sources: everything except main.cpp, so smoke tests can link
# against libyai.a and automatically pick up new source files.
LIB_SRCS := $(filter-out src/main.cpp,$(SRC))
LIB_OBJS := $(LIB_SRCS:.cpp=.o)

.PHONY: all clean install

all: $(TARGET) libyai.a

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

libyai.a: $(LIB_OBJS)
	ar rcs $@ $^

install: $(TARGET)
	install -Dm755 $(TARGET) "$(HOME)/.local/bin/$(TARGET)"
	for file in po/*.po; do install -Dm644 "$$file" "$(HOME)/.local/share/yai/po/$$(basename "$$file")"; done

clean:
	rm -f $(TARGET) libyai.a $(LIB_OBJS)
