#include "yai.hpp"

// Architecture handling only normalizes user/host names and scores AppImage
// asset filenames. It rejects known mismatches during selection, but it does not
// make a foreign-architecture AppImage executable on the current machine.

struct ArchAliasRule {
    std::string canonical;
    std::vector<std::string> aliases;
    std::vector<std::string> asset_needles;
};

const std::vector<ArchAliasRule>& arch_alias_rules() {
    static const std::vector<ArchAliasRule> rules = {
        {"x86_64", {"x86_64", "amd64", "x64"}, {"x86_64", "amd64", "x64"}},
        {"aarch64", {"aarch64", "arm64"}, {"aarch64", "arm64"}},
        {"x86", {"i386", "i486", "i586", "i686", "x86"}, {"i386", "i686", "x86.appimage"}},
        {"armv7", {"armv7l", "armv7", "armhf", "arm32"}, {"armv7", "armhf", "arm32"}},
        {"riscv64", {"riscv64"}, {"riscv64"}},
        {"ppc64le", {"ppc64le", "ppc64el", "powerpc64le"}, {"ppc64le", "ppc64el", "powerpc64le"}},
        {"s390x", {"s390x"}, {"s390x"}},
        {"loongarch64", {"loongarch64"}, {"loongarch64"}},
    };
    return rules;
}

bool token_looks_like_arch(const std::string& token) {
    const std::string lower = to_lower(trim(token));
    if (lower.empty()) {
        return false;
    }
    for (const ArchAliasRule& rule : arch_alias_rules()) {
        if (lower == rule.canonical) {
            return true;
        }
        for (const std::string& alias : rule.aliases) {
            if (lower == alias) {
                return true;
            }
        }
    }
    return false;
}

bool contains_any_arch_needle(const std::string& value, const ArchAliasRule& rule) {
    for (const std::string& needle : rule.asset_needles) {
        if (value.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string normalize_arch(const std::string& value) {
    std::string arch = to_lower(trim(value));
    arch = replace_all(arch, "-", "_");

    for (const ArchAliasRule& rule : arch_alias_rules()) {
        for (const std::string& alias : rule.aliases) {
            if (arch == alias) {
                return rule.canonical;
            }
        }
    }
    return arch.empty() ? "unknown" : arch;
}

bool is_supported_arch(const std::string& value) {
    const std::string arch = normalize_arch(value);
    for (const ArchAliasRule& rule : arch_alias_rules()) {
        if (rule.canonical == arch) {
            return true;
        }
    }
    return false;
}

std::string supported_arch_list() {
    return "auto, x86_64, aarch64, x86, armv7, riscv64, ppc64le, s390x, or loongarch64";
}

std::string current_arch() {
    const ProcessResult result = run_process_capture({"uname", "-m"});
    if (result.exit_code != 0) {
        return "unknown";
    }
    return normalize_arch(result.output);
}

std::vector<std::string> canonical_arches() {
    std::vector<std::string> arches;
    arches.reserve(arch_alias_rules().size());
    for (const ArchAliasRule& rule : arch_alias_rules()) {
        arches.push_back(rule.canonical);
    }
    return arches;
}

int appimage_asset_score(const std::string& asset_name, const std::string& arch) {
    // Score is conservative: assets mentioning a different known architecture
    // are rejected, matching assets win, and generic AppImage names remain a
    // lower-confidence fallback.
    const std::string lower = to_lower(asset_name);
    if (lower.find(".appimage") == std::string::npos) {
        return -1;
    }

    const std::string canonical_arch = normalize_arch(arch);
    bool mentions_target_arch = false;

    for (const ArchAliasRule& rule : arch_alias_rules()) {
        if (!contains_any_arch_needle(lower, rule)) {
            continue;
        }
        if (rule.canonical != canonical_arch) {
            return -1;
        }
        mentions_target_arch = true;
    }

    return mentions_target_arch ? 100 : 20;
}
