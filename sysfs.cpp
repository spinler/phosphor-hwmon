/**
 * Copyright © 2016 IBM Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <cstdlib>
#include <memory>
#include <phosphor-logging/log.hpp>
#include "sysfs.hpp"
#include "util.hpp"
#include "directory.hpp"

std::string findHwmon(const std::string& ofNode)
{
    static constexpr auto hwmonRoot = "/sys/class/hwmon";

    std::string fullOfPath{"/sys/firmware/devicetree/base"};
    fullOfPath.append(ofNode);

    std::string hwmonInst;
    Directory d(hwmonRoot);

    while (d.next(hwmonInst))
    {
        std::string hwmonPath{hwmonRoot};
        hwmonPath.append(1, '/');
        hwmonPath.append(hwmonInst);
        std::string path{hwmonPath};
        path.append(1, '/');
        path.append("of_node");

        auto real = std::unique_ptr<char, phosphor::utility::Free<char>>(realpath(
                        path.c_str(), nullptr));
        if (!real || real.get() != fullOfPath)
        {
            continue;
        }

        return hwmonPath;
    }

    return std::string();
}

int readSysfsWithCallout(const std::string& root,
                         const std::string& instance,
                         const std::string& type,
                         const std::string& id,
                         const std::string& sensor)
{
    int value = 0;
    std::string instancePath = root + '/' + instance;
    std::string fullPath = make_sysfs_path(instancePath,
                                           type, id, sensor);
    std::ifstream ifs;

    ifs.exceptions(std::ifstream::failbit
                   | std::ifstream::badbit
                   | std::ifstream::eofbit);
    try
    {
        ifs.open(fullPath);
        ifs >> value;
    }
    catch (const std::exception& e)
    {
        // Too many GCC bugs (53984, 66145) to do
        // this the right way...
        using Cleanup = phosphor::utility::Free<char>;

        // errno should still reflect the error from the failing open
        // or read system calls that got us here.
        auto rc = errno;
        std::string devicePath = instancePath + "/device";
        auto real = std::unique_ptr<char, Cleanup>(
                        realpath(devicePath.c_str(), nullptr));
        phosphor::logging::log<phosphor::logging::level::ERR>(
            strerror(rc),
            phosphor::logging::entry("CALLOUT_DEVICE_PATH=%s", real.get()),
            phosphor::logging::entry("CALLOUT_ERRNO=%d", rc));
        exit(EXIT_FAILURE);
    }

    return value;
}

// vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4
