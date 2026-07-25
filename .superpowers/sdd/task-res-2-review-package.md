# Task 2 review (after fix)
src/resolver.cpp:271:            appimage_url_from_download_landing_html(fetch_text(link), link, arch);
src/yai.hpp:385:std::string appimage_url_from_download_landing_html(
src/resolver_url.cpp:130:std::vector<std::string> html_appimage_urls(const std::string& html, const std::string& base_url) {
src/resolver_url.cpp:341:bool text_looks_like_html(std::string text) {
src/resolver_url.cpp:353:std::string appimage_url_from_download_landing_html(
src/resolver_url.cpp:357:    if (!text_looks_like_html(html)) {
src/resolver_url.cpp:360:    return best_appimage_url_from_candidates(html_appimage_urls(html, base_url), arch);
src/resolver_url.cpp:370:    return appimage_url_from_download_landing_html(read_text_file(path), base_url, arch);
	src/resolver_url.cpp \
  666 src/resolver.cpp
  371 src/resolver_url.cpp
 1037 总计
