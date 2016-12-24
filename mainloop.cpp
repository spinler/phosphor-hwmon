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
#include <iostream>
#include <memory>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include "sensorset.hpp"
#include "sensorcache.hpp"
#include "hwmon.hpp"
#include "sysfs.hpp"
#include "mainloop.hpp"
#include "interface.hpp"

using namespace std::literals::chrono_literals;

MainLoop::MainLoop(
    sdbusplus::bus::bus&& bus,
    const std::string& path,
    const char* prefix,
    const char* root)
    : _bus(std::move(bus)),
      _manager(sdbusplus::server::manager::manager(_bus, root)),
      _shutdown(false),
      _path(path),
      _prefix(prefix),
      _root(root),
      state()
{
    if (_path.back() == '/')
    {
        _path.pop_back();
    }
}

void MainLoop::shutdown() noexcept
{
    _shutdown = true;
}

void MainLoop::run()
{
    // Check sysfs for available sensors.
    auto sensors = std::make_unique<SensorSet>(_path);
    auto sensor_cache = std::make_unique<SensorCache>();

    for (auto& i : *sensors)
    {
        // Ignore inputs without a label.
        std::string envKey = "LABEL_" + i.first.first + i.first.second;
        std::string label;

        auto env = getenv(envKey.c_str());

        if (env)
        {
            label.assign(env);
        }

        if (label.empty())
        {
            continue;
        }

        auto value = std::make_tuple(std::move(i.second), std::move(label));

        state[std::move(i.first)] = std::move(value);
    }

    {
        struct Free
        {
            void operator()(char* ptr) const
            {
                free(ptr);
            }
        };

        auto copy = std::unique_ptr<char, Free>(strdup(_path.c_str()));
        auto busname = std::string(_prefix) + '.' + basename(copy.get());
        _bus.request_name(busname.c_str());
    }

    // TODO: Issue#3 - Need to make calls to the dbus sensor cache here to
    //       ensure the objects all exist?

    // Polling loop.
    while (!_shutdown)
    {
        // Iterate through all the sensors.
        for (auto& i : state)
        {
            auto& attrs = std::get<0>(i.second);
            if (attrs.find(hwmon::entry::input) != attrs.end())
            {
                // Read value from sensor.
                int value = 0;
                read_sysfs(make_sysfs_path(_path,
                                           i.first.first, i.first.second,
                                           hwmon::entry::input),
                           value);

                // Update sensor cache.
                if (sensor_cache->update(i.first, value))
                {
                    // TODO: Issue#4 - dbus event here.
                    std::cout << i.first.first << i.first.second << " = "
                              << value << std::endl;
                }
            }
        }

        // Respond to DBus
        _bus.process_discard();

        // Sleep until next interval.
        // TODO: Issue#5 - Make this configurable.
        // TODO: Issue#6 - Optionally look at polling interval sysfs entry.
        _bus.wait((1000000us).count());

        // TODO: Issue#7 - Should probably periodically check the SensorSet
        //       for new entries.
    }
}

// vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4
