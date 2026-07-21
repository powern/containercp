#include "operations/SiteDatabaseVolumeGuard.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace containercp::operations {
namespace {

std::string trim_copy(std::string value) {
    auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::vector<std::string> split_lines(const std::string& value) {
    std::vector<std::string> lines;
    std::istringstream ss(value);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(trim_copy(line));
    }
    return lines;
}

bool is_missing_label(const std::string& value) {
    return value.empty() || value == "<no value>";
}

bool mounted_by_target_container(const DockerCommandRunner& runner,
                                 const std::string& volume_name,
                                 const std::string& project,
                                 uint64_t site_id) {
    const std::string container = "site-" + std::to_string(site_id) + "-db";
    const auto inspect = runner.run({
        "docker", "inspect", container,
        "--format",
        "{{ index .Config.Labels \"containercp.site.id\" }}\n"
        "{{ index .Config.Labels \"com.docker.compose.project\" }}\n"
        "{{ index .Config.Labels \"com.docker.compose.service\" }}\n"
        "{{range .Mounts}}{{.Type}}|{{.Name}}|{{.Destination}}|{{.RW}}{{println}}{{end}}"
    });
    if (inspect.exit_code != 0) {
        return false;
    }
    const auto lines = split_lines(inspect.out);
    if (lines.size() < 3) {
        return false;
    }
    if (lines[0] != std::to_string(site_id) || lines[1] != project || lines[2] != "mariadb") {
        return false;
    }
    for (std::size_t i = 3; i < lines.size(); ++i) {
        if (lines[i] == "volume|" + volume_name + "|/var/lib/mysql|true") {
            return true;
        }
    }
    return false;
}

} // namespace

runtime::CommandResult CommandExecutorDockerRunner::run(const std::vector<std::string>& args) const {
    runtime::CommandExecutor exec;
    return exec.run(args);
}

std::string site_compose_project_name(const std::string& domain) {
    std::string project;
    project.reserve(domain.size());
    for (unsigned char c : domain) {
        if (std::isalnum(c) != 0 || c == '-' || c == '_') {
            project.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return project;
}

std::string site_database_volume_name(const std::string& domain) {
    return site_compose_project_name(domain) + "_db-data";
}

SiteDatabaseVolumePlan inspect_site_database_volume(const DockerCommandRunner& runner,
                                                    const std::string& domain,
                                                    uint64_t site_id) {
    SiteDatabaseVolumePlan plan;
    plan.volume_name = site_database_volume_name(domain);
    const std::string project = site_compose_project_name(domain);

    const auto volume = runner.run({
        "docker", "volume", "inspect", plan.volume_name,
        "--format",
        "{{ index .Labels \"containercp.site.id\" }}\n"
        "{{ index .Labels \"containercp.domain\" }}\n"
        "{{ index .Labels \"com.docker.compose.project\" }}\n"
        "{{ index .Labels \"com.docker.compose.volume\" }}"
    });
    if (volume.exit_code != 0) {
        plan.exists = false;
        plan.removable = true;
        plan.reason = "database_volume_absent";
        return plan;
    }
    plan.exists = true;

    const auto labels = split_lines(volume.out);
    const std::string site_label = labels.size() > 0 ? labels[0] : "";
    const std::string domain_label = labels.size() > 1 ? labels[1] : "";
    const std::string project_label = labels.size() > 2 ? labels[2] : "";
    const std::string volume_label = labels.size() > 3 ? labels[3] : "";

    const bool compose_matches = project_label == project && volume_label == "db-data";
    const bool containercp_labels_match = site_label == std::to_string(site_id) && domain_label == domain;
    if (compose_matches && containercp_labels_match) {
        plan.removable = true;
        plan.reason = "database_volume_owned_by_labels";
        return plan;
    }

    const bool missing_containercp_labels = is_missing_label(site_label) && is_missing_label(domain_label);
    if (compose_matches && missing_containercp_labels && mounted_by_target_container(runner, plan.volume_name, project, site_id)) {
        plan.removable = true;
        plan.legacy_mount_proven = true;
        plan.reason = "database_volume_owned_by_legacy_mount";
        return plan;
    }

    plan.removable = false;
    plan.reason = compose_matches ? "database_volume_ownership_mismatch" : "database_volume_not_owned_by_site";
    return plan;
}

core::OperationResult ensure_database_volume_absent_for_create(const DockerCommandRunner& runner,
                                                               const std::string& domain,
                                                               uint64_t site_id) {
    (void)site_id;
    const auto plan = inspect_site_database_volume(runner, domain, site_id);
    if (!plan.exists) {
        return {true, ""};
    }
    return {false, "Database volume collision detected for " + plan.volume_name + "; refusing to reuse existing MariaDB data."};
}

core::OperationResult remove_site_database_volume(const DockerCommandRunner& runner,
                                                  const SiteDatabaseVolumePlan& plan) {
    if (!plan.exists) {
        return {true, ""};
    }
    if (!plan.removable) {
        return {false, "Database volume cleanup refused: " + plan.reason};
    }
    const auto remove = runner.run({"docker", "volume", "rm", plan.volume_name});
    if (remove.exit_code != 0) {
        return {false, "Database volume cleanup failed for " + plan.volume_name};
    }
    return {true, ""};
}

} // namespace containercp::operations
