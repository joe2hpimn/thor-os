//=======================================================================
// Copyright Baptiste Wicht 2013-2016.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)
//=======================================================================

#include <errors.hpp>
#include <string.hpp>
#include <algorithms.hpp>

#include <directory_entry.hpp>
#include <mount_point.hpp>

#include "vfs/vfs.hpp"
#include "vfs/file_system.hpp"
#include "vfs/path.hpp"

#include "fs/fat32.hpp"
#include "fs/sysfs.hpp"
#include "fs/devfs.hpp"
#include "fs/procfs.hpp"

#include "scheduler.hpp"
#include "flags.hpp"

#include "console.hpp"
#include "logging.hpp"

namespace {

struct mounted_fs {
    vfs::partition_type fs_type;
    std::string device;
    std::string mount_point;
    vfs::file_system* file_system;

    std::vector<std::string> mp_vec;

    mounted_fs() = default;

    mounted_fs(vfs::partition_type type, std::string dev, std::string mp, vfs::file_system* fs) :
        fs_type(type), device(dev), mount_point(mp), file_system(fs)
    {
        mp_vec = std::split(mount_point, '/');
    }
};

std::string partition_type_to_string(vfs::partition_type type){
    switch(type){
        case vfs::partition_type::FAT32:
            return "FAT32";
        case vfs::partition_type::SYSFS:
            return "sysfs";
        case vfs::partition_type::DEVFS:
            return "devfs";
        case vfs::partition_type::PROCFS:
            return "procfs";
        case vfs::partition_type::UNKNOWN:
            return "Unknown";
        default:
            return "Invalid Type";
    }
}

std::vector<mounted_fs> mount_point_list;

void mount_root(){
    //TODO Get information about the root from a configuration file
    mount(vfs::partition_type::FAT32, "/", "/dev/hda1");
}

void mount_sys(){
    mount(vfs::partition_type::SYSFS, "/sys/", "none");
}

void mount_dev(){
    mount(vfs::partition_type::DEVFS, "/dev/", "none");
}

void mount_proc(){
    mount(vfs::partition_type::PROCFS, "/proc/", "none");
}

path get_path(const char* file_path){
    if(file_path[0] != '/'){
        return {scheduler::get_working_directory(), file_path};
    } else {
        return {file_path};
    }
}

mounted_fs& get_fs(const path& base_path){
    size_t best = 0;
    size_t best_match = 0;

    if(base_path.is_root()){
        for(auto& mp : mount_point_list){
            if(mp.mp_vec.empty()){
                return mp;
            }
        }
    }

    for(size_t i = 0; i < mount_point_list.size(); ++i){
        auto& mp = mount_point_list[i];

        bool match = true;
        for(size_t j = 0; j < mp.mp_vec.size() && j < base_path.size() ; ++j){
            if(mp.mp_vec[j] != base_path[j]){
                match = false;
                break;
            }
        }

        if(match && mp.mp_vec.size() > best){
            best = mp.mp_vec.size();
            best_match = i;
        }
    }

    return mount_point_list[best_match];;
}

path get_fs_path(const path& base_path, const mounted_fs& fs){
    return base_path.sub_path(fs.mp_vec.size());
}

vfs::file_system* get_new_fs(vfs::partition_type type, const std::string& mount_point, const std::string& device){
    switch(type){
        case vfs::partition_type::FAT32:
            return new fat32::fat32_file_system(mount_point, device);

        case vfs::partition_type::SYSFS:
            return new sysfs::sysfs_file_system(mount_point);

        case vfs::partition_type::DEVFS:
            return new devfs::devfs_file_system(mount_point);

        case vfs::partition_type::PROCFS:
            return new procfs::procfs_file_system(mount_point);

        default:
            return nullptr;
    }
}

} //end of anonymous namespace

void vfs::init(){
    mount_root();
    mount_sys();
    mount_dev();
    mount_proc();

    //Finish initilization of the file systems
    for(auto& mp : mount_point_list){
        mp.file_system->init();
    }
}

int64_t vfs::mount(partition_type type, size_t mp_fd, size_t dev_fd){
    if(!scheduler::has_handle(mp_fd)){
        return -std::ERROR_INVALID_FILE_DESCRIPTOR;
    }

    if(!scheduler::has_handle(dev_fd)){
        return -std::ERROR_INVALID_FILE_DESCRIPTOR;
    }

    auto& mp_path = scheduler::get_handle(mp_fd);
    auto& dev_path = scheduler::get_handle(dev_fd);

    std::string mp("/");

    for(auto& p : mp_path){
        mp += p;
        mp += "/";
    }

    std::string device("/");

    for(auto& p : dev_path){
        device += p;
        device += "/";
    }

    for(auto& m : mount_point_list){
        if(m.mount_point == mp){
            return -std::ERROR_ALREADY_MOUNTED;
        }
    }

    auto fs = get_new_fs(type, mp, device);

    if(!fs){
        return -std::ERROR_INVALID_FILE_SYSTEM;
    }

    mount_point_list.emplace_back(type, device, mp, fs);
    fs->init();

    logging::logf(logging::log_level::TRACE, "vfs: mounted file system %s at %s \n", device.c_str(), mp.c_str());

    return 0;
}

int64_t vfs::mount(partition_type type, const char* mount_point, const char* device){
    auto fs = get_new_fs(type, mount_point, device);

    if(!fs){
        return -std::ERROR_INVALID_FILE_SYSTEM;
    }

    mount_point_list.emplace_back(type, device, mount_point, fs);

    return 0;
}

int64_t vfs::statfs(const char* mount_point, statfs_info& info){
    if(std::str_len(mount_point) == 0){
        return -std::ERROR_INVALID_FILE_PATH;
    }

    auto base_path = get_path(mount_point);
    auto& fs = get_fs(base_path);

    return fs.file_system->statfs(info);
}

int64_t vfs::open(const char* file_path, size_t flags){
    std::string file(file_path);

    if(file.empty()){
        return -std::ERROR_INVALID_FILE_PATH;
    }

    auto base_path = get_path(file_path);
    auto& fs = get_fs(base_path);
    auto fs_path = get_fs_path(base_path, fs);

    //Special handling for opening the root
    if(fs_path.is_root()){
        return scheduler::register_new_handle(base_path);
    }

    int64_t sub_result;
    if(flags & std::OPEN_CREATE){
        vfs::file file;
        sub_result = fs.file_system->get_file(fs_path, file);

        if(sub_result == std::ERROR_NOT_EXISTS){
            sub_result = fs.file_system->touch(fs_path);
        }
    } else {
        vfs::file file;
        sub_result = fs.file_system->get_file(fs_path, file);
    }

    if(sub_result > 0){
        return -sub_result;
    } else {
        return scheduler::register_new_handle(base_path);
    }
}

void vfs::close(size_t fd){
    if(scheduler::has_handle(fd)){
        scheduler::release_handle(fd);
    }
}

int64_t vfs::mkdir(const char* file_path){
    std::string file(file_path);

    if(file.empty()){
        return -std::ERROR_INVALID_FILE_PATH;
    }

    auto base_path = get_path(file_path);
    auto& fs = get_fs(base_path);
    auto fs_path = get_fs_path(base_path, fs);

#ifdef THOR_CONFIG_DEBUG_VFS
    logging::logf(logging::log_level::TRACE, "vfs: mkdir: %s \n", file_path);

    for(auto& p : base_path){
        logging::logf(logging::log_level::TRACE, "vfs: mkdir base_path: %s\n", p.c_str());
    }

    for(auto& p : fs_path){
        logging::logf(logging::log_level::TRACE, "vfs: mkdir fs_path: %s\n", p.c_str());
    }
#endif

    return fs.file_system->mkdir(fs_path);
}

int64_t vfs::rm(const char* file_path){
    std::string file(file_path);

    if(file.empty()){
        return -std::ERROR_INVALID_FILE_PATH;
    }

    auto base_path = get_path(file_path);
    auto& fs = get_fs(base_path);
    auto fs_path = get_fs_path(base_path, fs);

    return fs.file_system->rm(fs_path);
}

int64_t vfs::stat(size_t fd, stat_info& info){
    if(!scheduler::has_handle(fd)){
        return -std::ERROR_INVALID_FILE_DESCRIPTOR;
    }

    auto& base_path = scheduler::get_handle(fd);
    auto& fs = get_fs(base_path);
    auto fs_path = get_fs_path(base_path, fs);

    //Special handling for root
    if(fs_path.empty()){
        //TODO Add file system support for stat of the root directory
        info.size = 4096;
        info.flags = STAT_FLAG_DIRECTORY;

        return 0;
    }

    vfs::file f;
    auto result = fs.file_system->get_file(fs_path, f);

    if(result > 0){
        return -result;
    }

    info.size = f.size;
    info.flags = 0;

    if(f.directory){
        info.flags |= STAT_FLAG_DIRECTORY;
    }

    if(f.system){
        info.flags |= STAT_FLAG_SYSTEM;
    }

    if(f.hidden){
        info.flags |= STAT_FLAG_HIDDEN;
    }

    info.created = f.created;
    info.modified = f.modified;
    info.accessed = f.accessed;

    return 0;
}

int64_t vfs::read(size_t fd, char* buffer, size_t count, size_t offset){
    if(!scheduler::has_handle(fd)){
        return -std::ERROR_INVALID_FILE_DESCRIPTOR;
    }

    auto& base_path = scheduler::get_handle(fd);

    if(base_path.empty()){
        return -std::ERROR_INVALID_FILE_PATH;
    }

    auto& fs = get_fs(base_path);
    auto fs_path = get_fs_path(base_path, fs);

    size_t read = 0;
    auto result = fs.file_system->read(fs_path, buffer, count, offset, read);

    if(result > 0){
        return -result;
    }

    return read;
}

int64_t vfs::direct_read(const char* file, char* buffer, size_t count, size_t offset){
    auto base_path = get_path(file);
    auto& fs = get_fs(base_path);
    auto fs_path = get_fs_path(base_path, fs);

    size_t read = 0;
    auto result = fs.file_system->read(fs_path, buffer, count, offset, read);

    if(result > 0){
        return -result;
    }

    return read;
}

int64_t vfs::write(size_t fd, const char* buffer, size_t count, size_t offset){
    if(!scheduler::has_handle(fd)){
        return -std::ERROR_INVALID_FILE_DESCRIPTOR;
    }

    auto& base_path = scheduler::get_handle(fd);

    if(base_path.empty()){
        return -std::ERROR_INVALID_FILE_PATH;
    }

    auto& fs = get_fs(base_path);
    auto fs_path = get_fs_path(base_path, fs);

    size_t written = 0;
    auto result = fs.file_system->write(fs_path, buffer, count, offset, written);

    if(result > 0){
        return -result;
    }

    return written;
}

int64_t vfs::clear(size_t fd, size_t count, size_t offset){
    if(!scheduler::has_handle(fd)){
        return -std::ERROR_INVALID_FILE_DESCRIPTOR;
    }

    auto& base_path = scheduler::get_handle(fd);

    if(base_path.empty()){
        return -std::ERROR_INVALID_FILE_PATH;
    }

    auto& fs = get_fs(base_path);
    auto fs_path = get_fs_path(base_path, fs);

    size_t written = 0;
    auto result = fs.file_system->clear(fs_path, count, offset, written);

    if(result > 0){
        return -result;
    }

    return written;
}

int64_t vfs::direct_write(const char* file, const char* buffer, size_t count, size_t offset){
    auto base_path = get_path(file);
    auto& fs = get_fs(base_path);
    auto fs_path = get_fs_path(base_path, fs);

    size_t written = 0;
    auto result = fs.file_system->write(fs_path, buffer, count, offset, written);

    if(result > 0){
        return -result;
    }

    return written;
}

int64_t vfs::truncate(size_t fd, size_t size){
    if(!scheduler::has_handle(fd)){
        return -std::ERROR_INVALID_FILE_DESCRIPTOR;
    }

    auto& base_path = scheduler::get_handle(fd);

    if(base_path.empty()){
        return -std::ERROR_INVALID_FILE_PATH;
    }

    auto& fs = get_fs(base_path);
    auto fs_path = get_fs_path(base_path, fs);

    auto result = fs.file_system->truncate(fs_path, size);
    return result > 0 ? -result : 0;
}

int64_t vfs::direct_read(const std::string& file_path, std::string& content){
    auto base_path = get_path(file_path.c_str());
    auto& fs = get_fs(base_path);
    auto fs_path = get_fs_path(base_path, fs);

    vfs::file f;
    auto result = fs.file_system->get_file(fs_path, f);

    if(result > 0){
        return -result;
    }

    content.reserve(f.size + 1);

    size_t read = 0;
    result = fs.file_system->read(fs_path, content.c_str(), f.size, 0, read);

    if(result > 0){
        return -result;
    }

    content[read] = '\0';
    content.adjust_size(read);

    return read;
}

int64_t vfs::entries(size_t fd, char* buffer, size_t size){
    if(!scheduler::has_handle(fd)){
        return -std::ERROR_INVALID_FILE_DESCRIPTOR;
    }

    auto& base_path = scheduler::get_handle(fd);
    auto& fs = get_fs(base_path);
    auto fs_path = get_fs_path(base_path, fs);

    std::vector<vfs::file> files;
    auto result = fs.file_system->ls(fs_path, files);

    if(result > 0){
        return -result;
    }

    size_t total_size = 0;

    for(auto& f : files){
        total_size += sizeof(directory_entry) + f.file_name.size();
    }

    if(size < total_size){
        return -std::ERROR_BUFFER_SMALL;
    }

    size_t position = 0;

    for(size_t i = 0; i < files.size(); ++i){
        auto& file = files[i];

        auto entry = reinterpret_cast<directory_entry*>(buffer + position);

        entry->type = 0; //TODO Fill that
        entry->length = file.file_name.size();

        if(i + 1 < files.size()){
            entry->offset_next = file.file_name.size() + 1 + 3 * 8;
            position += entry->offset_next;
        } else {
            entry->offset_next = 0;
        }

        char* name_buffer = &(entry->name);
        for(size_t j = 0; j < file.file_name.size(); ++j){
            name_buffer[j] = file.file_name[j];
        }
        name_buffer[file.file_name.size()] = '\0';
    }

    return total_size;
}

int64_t vfs::mounts(char* buffer, size_t size){
    size_t total_size = 0;

    for(auto& mp : mount_point_list){
        total_size += 4 * sizeof(size_t) + 3 + mp.device.size() + mp.mount_point.size() + partition_type_to_string(mp.fs_type).size();
    }

    if(size < total_size){
        return -std::ERROR_BUFFER_SMALL;
    }

    size_t position = 0;

    for(size_t i = 0; i < mount_point_list.size(); ++i){
        auto& mp = mount_point_list[i];

        auto entry = reinterpret_cast<mount_point*>(buffer + position);

        auto fs_type = partition_type_to_string(mp.fs_type);

        entry->length_mp = mp.mount_point.size();
        entry->length_dev = mp.device.size();
        entry->length_type = fs_type.size();

        if(i + 1 < mount_point_list.size()){
            entry->offset_next = 4 * sizeof(size_t) + 3 + mp.device.size() + mp.mount_point.size() + fs_type.size();
            position += entry->offset_next;
        } else {
            entry->offset_next = 0;
        }

        char* name_buffer = &(entry->name);
        size_t str_pos = 0;

        for(size_t j = 0; j < mp.mount_point.size(); ++j){
            name_buffer[str_pos++] = mp.mount_point[j];
        }
        name_buffer[str_pos++] = '\0';
        for(size_t j = 0; j < mp.device.size(); ++j){
            name_buffer[str_pos++] = mp.device[j];
        }
        name_buffer[str_pos++] = '\0';
        for(size_t j = 0; j < fs_type.size(); ++j){
            name_buffer[str_pos++] = fs_type[j];
        }
        name_buffer[str_pos++] = '\0';
    }

    return total_size;
}
