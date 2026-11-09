#include "runtime/launchd_daemon_manager.h"

#include <algorithm>
#include <sstream>

namespace winios::runtime {

const char* launchd_state_string(LaunchdJobState state) noexcept {
    switch (state) {
        case LaunchdJobState::Idle:       return "Idle";
        case LaunchdJobState::Starting:   return "Starting";
        case LaunchdJobState::Running:    return "Running";
        case LaunchdJobState::Crashed:    return "Crashed";
        case LaunchdJobState::Restarting: return "Restarting";
        case LaunchdJobState::Stopped:    return "Stopped";
        default:                          return "Unknown";
    }
}

LaunchdDaemonManager::LaunchdDaemonManager() {
    populate_default_ios_daemons();
}

void LaunchdDaemonManager::populate_default_ios_daemons() {
    // 1. syslogd
    LaunchdJobConfig syslogd;
    syslogd.label = "com.apple.syslogd";
    syslogd.program_arguments = {"/usr/sbin/syslogd"};
    syslogd.run_at_load = true;
    syslogd.keep_alive = true;
    syslogd.mach_services = {"com.apple.system.logger"};
    register_job(std::move(syslogd));

    // 2. notifyd
    LaunchdJobConfig notifyd;
    notifyd.label = "com.apple.notifyd";
    notifyd.program_arguments = {"/usr/sbin/notifyd"};
    notifyd.run_at_load = true;
    notifyd.keep_alive = true;
    notifyd.mach_services = {"com.apple.system.notification_center"};
    notifyd.dependencies = {"com.apple.syslogd"};
    register_job(std::move(notifyd));

    // 3. securityd
    LaunchdJobConfig securityd;
    securityd.label = "com.apple.securityd";
    securityd.program_arguments = {"/usr/libexec/securityd"};
    securityd.run_at_load = true;
    securityd.keep_alive = true;
    securityd.mach_services = {"com.apple.securityd"};
    securityd.dependencies = {"com.apple.notifyd"};
    register_job(std::move(securityd));

    // 4. WindowServer
    LaunchdJobConfig ws;
    ws.label = "com.apple.WindowServer";
    ws.program_arguments = {"/System/Library/Frameworks/CoreGraphics.framework/WindowServer"};
    ws.run_at_load = true;
    ws.keep_alive = true;
    ws.mach_services = {"com.apple.windowserver", "com.apple.iohideventsystem"};
    ws.dependencies = {"com.apple.securityd"};
    register_job(std::move(ws));

    // 5. SpringBoard
    LaunchdJobConfig sb;
    sb.label = "com.apple.SpringBoard";
    sb.program_arguments = {"/System/Library/CoreServices/SpringBoard.app/SpringBoard"};
    sb.run_at_load = true;
    sb.keep_alive = true;
    sb.mach_services = {"com.apple.springboard.services"};
    sb.dependencies = {"com.apple.WindowServer"};
    register_job(std::move(sb));
}

bool LaunchdDaemonManager::register_job(LaunchdJobConfig config) {
    if (config.label.empty() || config.program_arguments.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> guard(lock_);
    LaunchdJobInstance instance;
    instance.config = std::move(config);
    instance.state = LaunchdJobState::Idle;
    jobs_[instance.config.label] = std::move(instance);
    return true;
}

namespace {

inline std::string extract_tag_value(std::string_view xml, std::string_view key_name, std::string_view val_tag) {
    std::string key_pattern = "<key>" + std::string(key_name) + "</key>";
    auto pos = xml.find(key_pattern);
    if (pos == std::string_view::npos) return "";

    std::string open_tag = "<" + std::string(val_tag) + ">";
    std::string close_tag = "</" + std::string(val_tag) + ">";

    auto search_from = pos + key_pattern.size();
    auto val_start = xml.find(open_tag, search_from);
    if (val_start == std::string_view::npos) return "";
    val_start += open_tag.size();

    auto val_end = xml.find(close_tag, val_start);
    if (val_end == std::string_view::npos) return "";

    return std::string(xml.substr(val_start, val_end - val_start));
}

} // anonymous namespace

bool LaunchdDaemonManager::load_job_from_plist(std::string_view plist_xml) {
    std::string label = extract_tag_value(plist_xml, "Label", "string");
    if (label.empty()) return false;

    LaunchdJobConfig cfg;
    cfg.label = label;

    // Check Program or ProgramArguments
    std::string program = extract_tag_value(plist_xml, "Program", "string");
    if (!program.empty()) {
        cfg.program_arguments.push_back(program);
    }

    // Check ProgramArguments array
    std::string key_pattern = "<key>ProgramArguments</key>";
    auto pos = plist_xml.find(key_pattern);
    if (pos != std::string_view::npos) {
        auto arr_start = plist_xml.find("<array>", pos);
        auto arr_end = plist_xml.find("</array>", pos);
        if (arr_start != std::string_view::npos && arr_end != std::string_view::npos && arr_start < arr_end) {
            std::size_t cur = arr_start + 7;
            while (cur < arr_end) {
                auto s_start = plist_xml.find("<string>", cur);
                if (s_start == std::string_view::npos || s_start >= arr_end) break;
                s_start += 8;
                auto s_end = plist_xml.find("</string>", s_start);
                if (s_end == std::string_view::npos || s_end > arr_end) break;
                cfg.program_arguments.emplace_back(plist_xml.substr(s_start, s_end - s_start));
                cur = s_end + 9;
            }
        } else {
            std::string arg = extract_tag_value(plist_xml, "ProgramArguments", "string");
            if (!arg.empty()) {
                cfg.program_arguments.push_back(arg);
            }
        }
    }

    if (cfg.program_arguments.empty()) {
        cfg.program_arguments.push_back("/usr/libexec/" + label);
    }

    // KeepAlive
    if (plist_xml.find("<key>KeepAlive</key>") != std::string_view::npos) {
        cfg.keep_alive = (plist_xml.find("<key>KeepAlive</key>\n<false/>") == std::string_view::npos &&
                          plist_xml.find("<key>KeepAlive</key><false/>") == std::string_view::npos);
    }

    // RunAtLoad
    if (plist_xml.find("<key>RunAtLoad</key>") != std::string_view::npos) {
        cfg.run_at_load = (plist_xml.find("<key>RunAtLoad</key>\n<false/>") == std::string_view::npos &&
                           plist_xml.find("<key>RunAtLoad</key><false/>") == std::string_view::npos);
    }

    // MachServices
    std::string mach_key_pattern = "<key>MachServices</key>";
    auto mpos = plist_xml.find(mach_key_pattern);
    if (mpos != std::string_view::npos) {
        auto dict_start = plist_xml.find("<dict>", mpos);
        auto dict_end = plist_xml.find("</dict>", mpos);
        if (dict_start != std::string_view::npos && dict_end != std::string_view::npos && dict_start < dict_end) {
            std::size_t cur = dict_start + 6;
            while (cur < dict_end) {
                auto k_start = plist_xml.find("<key>", cur);
                if (k_start == std::string_view::npos || k_start >= dict_end) break;
                k_start += 5;
                auto k_end = plist_xml.find("</key>", k_start);
                if (k_end == std::string_view::npos || k_end > dict_end) break;
                cfg.mach_services.emplace_back(plist_xml.substr(k_start, k_end - k_start));
                cur = k_end + 6;
            }
        } else {
            std::string service = extract_tag_value(plist_xml, "MachServices", "key");
            if (!service.empty()) {
                cfg.mach_services.push_back(service);
            }
        }
    }

    return register_job(std::move(cfg));
}

bool LaunchdDaemonManager::has_job(std::string_view label) const noexcept {
    return get_job(label) != nullptr;
}

const LaunchdJobInstance* LaunchdDaemonManager::get_job(std::string_view label) const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = jobs_.find(std::string(label));
    if (it != jobs_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<std::string> LaunchdDaemonManager::get_all_labels() const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<std::string> labels;
    for (const auto& [label, _] : jobs_) {
        labels.push_back(label);
    }
    return labels;
}

bool LaunchdDaemonManager::start_job(
    std::string_view label,
    DarwinBsdSubsystem& bsd,
    MachIpcSubsystem& ipc,
    Memory& memory) {

    (void)memory;
    std::unique_lock<std::mutex> lock(lock_);
    auto it = jobs_.find(std::string(label));
    if (it == jobs_.end()) {
        return false;
    }

    auto& job = it->second;
    if (job.state == LaunchdJobState::Running) {
        return true; // Already active
    }

    // Start dependencies first
    for (const auto& dep : job.config.dependencies) {
        auto dep_it = jobs_.find(dep);
        if (dep_it != jobs_.end() && dep_it->second.state != LaunchdJobState::Running) {
            lock.unlock();
            start_job(dep, bsd, ipc, memory);
            lock.lock();
        }
    }

    // Re-verify after releasing lock
    it = jobs_.find(std::string(label));
    if (it == jobs_.end()) return false;
    auto& target_job = it->second;

    std::string exec_path = target_job.config.program_arguments.empty() 
                                ? "/sbin/launchd" 
                                : target_job.config.program_arguments[0];

    // Spawn process under launchd (PID 1)
    std::uint32_t pid = bsd.create_process(1, exec_path, 0);
    target_job.pid = pid;
    target_job.state = LaunchdJobState::Running;

    // Register Mach services in bootstrap catalog
    auto task_space = ipc.get_task_port_space(pid);
    if (!task_space) {
        task_space = ipc.create_task_port_space(pid);
    }

    for (const auto& sname : target_job.config.mach_services) {
        auto obj = std::make_shared<MachPortObject>(pid * 1000, pid);
        mach_port_t port = task_space->allocate_port(obj, kMachPortRightReceive);
        task_space->insert_right(port, obj, kMachPortRightSend);
        ipc.bootstrap_register(pid, sname, port);
    }

    return true;
}

bool LaunchdDaemonManager::stop_job(std::string_view label, DarwinBsdSubsystem& bsd) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = jobs_.find(std::string(label));
    if (it == jobs_.end()) {
        return false;
    }

    auto& job = it->second;
    if (job.state == LaunchdJobState::Running && job.pid > 0) {
        bsd.kill(job.pid, kDarwinSigTerm);
    }
    job.state = LaunchdJobState::Stopped;
    return true;
}

void LaunchdDaemonManager::handle_process_exit(
    std::uint32_t pid,
    int exit_code,
    DarwinBsdSubsystem& bsd,
    MachIpcSubsystem& ipc,
    Memory& memory) {

    std::unique_lock<std::mutex> lock(lock_);
    for (auto& [label, job] : jobs_) {
        if (job.pid == pid) {
            job.last_exit_code = exit_code;
            if (exit_code != 0) {
                job.state = LaunchdJobState::Crashed;
                job.crash_count++;
            } else {
                job.state = LaunchdJobState::Stopped;
            }

            if (job.config.keep_alive) {
                job.state = LaunchdJobState::Restarting;
                lock.unlock();
                start_job(label, bsd, ipc, memory);
                return;
            }
            break;
        }
    }
}

std::size_t LaunchdDaemonManager::launch_system_boot_daemons(
    DarwinBsdSubsystem& bsd,
    MachIpcSubsystem& ipc,
    Memory& memory) {

    std::vector<std::string> to_start;
    {
        std::lock_guard<std::mutex> guard(lock_);
        for (const auto& [label, job] : jobs_) {
            if (job.config.run_at_load) {
                to_start.push_back(label);
            }
        }
    }

    std::size_t count = 0;
    for (const auto& label : to_start) {
        if (start_job(label, bsd, ipc, memory)) {
            count++;
        }
    }

    return count;
}

} // namespace winios::runtime
