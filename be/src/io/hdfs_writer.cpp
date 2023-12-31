// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "io/hdfs_writer.h"

#include <bthread/bthread.h>

#include <filesystem>

#include "common/logging.h"
#include "io/fs/err_utils.h"
#include "service/backend_options.h"
#include "util/stack_util.h"

namespace doris {

HDFSWriter::HDFSWriter(const std::map<std::string, std::string>& properties,
                       const std::string& path)
        : _properties(properties), _path(path), _hdfs_fs(nullptr) {
    _parse_properties(_properties);
}

HDFSWriter::~HDFSWriter() {
    close();
}

Status HDFSWriter::open() {
    if (_namenode.empty()) {
        LOG(WARNING) << "hdfs properties is incorrect.";
        return Status::InternalError("hdfs properties is incorrect");
    }
    // if the format of _path is hdfs://ip:port/path, replace it to /path.
    // path like hdfs://ip:port/path can't be used by libhdfs3.
    if (_path.find(_namenode) != _path.npos) {
        _path = _path.substr(_namenode.size());
    }
    RETURN_IF_ERROR(_connect());
    if (_hdfs_fs == nullptr) {
        return Status::InternalError("HDFS writer open without client");
    }
    int exists = hdfsExists(_hdfs_fs, _path.c_str());
    if (exists == 0) {
        // the path already exists
        return Status::AlreadyExist("{} already exists.", _path);
    }

    std::filesystem::path hdfs_path(_path);
    std::string hdfs_dir = hdfs_path.parent_path().string();
    LOG(INFO) << "hdfs write open: " << hdfs_dir << get_stack_trace();
    exists = hdfsExists(_hdfs_fs, hdfs_dir.c_str());
    if (exists != 0) {
        LOG(INFO) << "hdfs dir doesn't exist, create it: " << hdfs_dir << ", path: " << _path
                  << get_stack_trace();
        int ret = hdfsCreateDirectory(_hdfs_fs, hdfs_dir.c_str());
        if (ret != 0) {
            std::stringstream ss;
            ss << "create dir failed. "
               << "(BE: " << BackendOptions::get_localhost() << ")"
               << " namenode: " << _namenode << " path: " << hdfs_dir
               << ", err: " << io::hdfs_error();
            LOG(WARNING) << ss.str();
            return Status::InternalError(ss.str());
        }
    }
    // open file
    _hdfs_file = hdfsOpenFile(_hdfs_fs, _path.c_str(), O_WRONLY, 0, 0, 0);
    if (_hdfs_file == nullptr) {
        std::stringstream ss;
        ss << "open file failed. "
           << "(BE: " << BackendOptions::get_localhost() << ")"
           << " namenode:" << _namenode << " path:" << _path << ", err: " << io::hdfs_error();
        LOG(WARNING) << ss.str();
        return Status::InternalError(ss.str());
    }
    VLOG_NOTICE << "open file. namenode:" << _namenode << ", path:" << _path;
    return Status::OK();
}

Status HDFSWriter::write(const uint8_t* buf, size_t buf_len, size_t* written_len) {
    if (buf_len == 0) {
        *written_len = 0;
        return Status::OK();
    }
    int32_t result = hdfsWrite(_hdfs_fs, _hdfs_file, buf, buf_len);
    if (result < 0) {
        std::stringstream ss;
        ss << "write file failed. "
           << "(BE: " << BackendOptions::get_localhost() << ")"
           << "namenode:" << _namenode << " path:" << _path << ", err: " << io::hdfs_error();
        LOG(WARNING) << ss.str();
        return Status::InternalError(ss.str());
    }

    *written_len = (unsigned int)result;
    return Status::OK();
}

Status HDFSWriter::close() {
    if (_closed) {
        return Status::OK();
    }
    _closed = true;
    if (_hdfs_fs == nullptr) {
        return Status::OK();
    }
    if (_hdfs_file == nullptr) {
        // Even if there is an error, the resources associated with the hdfsFS will be freed.
        hdfsDisconnect(_hdfs_fs);
        return Status::OK();
    }
    int result = hdfsFlush(_hdfs_fs, _hdfs_file);
    if (result == -1) {
        std::stringstream ss;
        ss << "failed to flush hdfs file. "
           << "(BE: " << BackendOptions::get_localhost() << ")"
           << "namenode:" << _namenode << " path:" << _path << ", err: " << io::hdfs_error();
        LOG(WARNING) << ss.str();
        return Status::InternalError(ss.str());
    }
    hdfsCloseFile(_hdfs_fs, _hdfs_file);
    hdfsDisconnect(_hdfs_fs);

    _hdfs_file = nullptr;
    _hdfs_fs = nullptr;
    return Status::OK();
}

Status HDFSWriter::_connect() {
    HDFSCommonBuilder builder;
    THdfsParams hdfsParams = parse_properties(_properties);
    RETURN_IF_ERROR(createHDFSBuilder(hdfsParams, &builder));
    _hdfs_fs = hdfsBuilderConnect(builder.get());
    if (_hdfs_fs == nullptr) {
        return Status::InternalError("connect to hdfs failed. namenode address:{}, error {}",
                                     _namenode, io::hdfs_error());
    }
    return Status::OK();
}

void HDFSWriter::_parse_properties(const std::map<std::string, std::string>& prop) {
    auto iter = prop.find(FS_KEY);
    if (iter != prop.end()) {
        _namenode = iter->second;
    }
}

} // end namespace doris
