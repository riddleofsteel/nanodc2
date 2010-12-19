/* vim:set ts=4 sw=4 sts=4 et cindent: */
/*
 * nanodc - The ncurses DC++ client
 * Copyright © 2005-2006 Markus Lindqvist <nanodc.developer@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Contributor(s):
 *  
 */

#include <sstream>
#include <stdexcept>
#include <tr1/functional>
#include <client/stdinc.h>
#include <client/DCPlusPlus.h>
#include <client/SettingsManager.h>
#include <core/events.h>
#include <core/log.h>
#include <utils/utils.h>

namespace modules {

typedef struct Table {
    const char *name;
    int num;
};

static Table stringSettings[] = {
    { "speed", SettingsManager::UPLOAD_SPEED },
    { "description", SettingsManager::DESCRIPTION },
    { "dl_dir", SettingsManager::DOWNLOAD_DIRECTORY },
    { "email", SettingsManager::EMAIL },
    { "ext_ip", SettingsManager::EXTERNAL_IP },
    { "http_proxy", SettingsManager::HTTP_PROXY },
    { "log_dir", SettingsManager::LOG_DIRECTORY },
    { "temp_dir", SettingsManager::TEMP_DOWNLOAD_DIRECTORY },
    { "bind_address", SettingsManager::BIND_ADDRESS },
    { "socks_server", SettingsManager::SOCKS_SERVER },
    { "socks_user", SettingsManager::SOCKS_USER },
    { "socks_password", SettingsManager::SOCKS_PASSWORD },
    { 0, 0 }
};

static Table intSettings[] = {
    { "connection", SettingsManager::INCOMING_CONNECTIONS },
    { "tcp_port", SettingsManager::TCP_PORT },
    { "rollback", SettingsManager::ROLLBACK },
    { "share_hidden", SettingsManager::SHARE_HIDDEN },
    { "auto_search", SettingsManager::AUTO_SEARCH },
    { "buffer_size", SettingsManager::BUFFER_SIZE },
    { "down_slots", SettingsManager::DOWNLOAD_SLOTS },
    { "down_speed", SettingsManager::MAX_DOWNLOAD_SPEED },
    { "up_speed", SettingsManager::MIN_UPLOAD_SPEED },
    { "socks_port", SettingsManager::SOCKS_PORT },
    { "socks_resolve", SettingsManager::SOCKS_RESOLVE },
    { "keep_lists", SettingsManager::KEEP_LISTS },
    { "compress_transfers", SettingsManager::COMPRESS_TRANSFERS },
    { "max_compression", SettingsManager::MAX_COMPRESSION },
    { "anti_frag", SettingsManager::ANTI_FRAG},
    { "skip_zero_byte", SettingsManager::SKIP_ZERO_BYTE },
    { "auto_search_auto_match", SettingsManager::AUTO_SEARCH_AUTO_MATCH },
    { "max_hash_speed", SettingsManager::MAX_HASH_SPEED },
    { "add_finished", SettingsManager::ADD_FINISHED_INSTANTLY },
    { "dont_dl_shared", SettingsManager::DONT_DL_ALREADY_SHARED },
    { "udp_port", SettingsManager::UDP_PORT },
    { "advanced_resume", SettingsManager::ADVANCED_RESUME },
    { "minislot_size", SettingsManager::SET_MINISLOT_SIZE },
    { "max_filelist", SettingsManager::MAX_FILELIST_SIZE },
    { "socket_read_buffer", SettingsManager::SOCKET_IN_BUFFER },
    { "socket_write_buffer", SettingsManager::SOCKET_OUT_BUFFER },
    { "dl_tth_only", SettingsManager::ONLY_DL_TTH_FILES },
    { "refresh_time", SettingsManager::AUTO_REFRESH_TIME },
    { 0, 0 }
};


class DcSet
{
public:
    DcSet() {
        events::add_listener("command dcset",
                std::tr1::bind(&DcSet::set, this));

        events::add_listener("command nick",
                std::tr1::bind(&DcSet::set_nick, this));
    }

    /** "command nick" event handler. */
    void set_nick() {
        if(events::args() < 1) {
            core::Log::get()->log("Not enough parameters given");
            return;
        }

        SettingsManager::getInstance()->set(SettingsManager::NICK, events::arg<std::string>(0));
    }

    /** Find enumeration value corresponding the given setting.
     * @param table table to search from 
     * @param setting name of the setting
     * @return enumeration value of the given setting or 0 if the setting doesn't exist */
    int find_setting(Table *table, const std::string &setting)
    {
        for(int i=0; table[i].name; ++i) {
            if(table[i].name == setting)
                return table[i].num;
        }
        return 0;
    }

    /** "command dcset" event handler. */
    void set()
    {
        bool set = events::args() > 1;
        std::string var = events::args() > 0 ? events::arg<std::string>(0) : "";
        std::string val = set ? events::arg<std::string>(1) : "";

        int num = find_setting(stringSettings, var);
        if(num && set) {
            SettingsManager::getInstance()->set((SettingsManager::StrSetting)num, val);
        }
        else if(num) {
            val = SettingsManager::getInstance()->get((SettingsManager::StrSetting)num);
        }
        if(num == 0) {
            num = find_setting(intSettings, var);
            if(num && set)
                SettingsManager::getInstance()->set((SettingsManager::IntSetting)num, val);
            else if(num)
                val = utils::to_string(SettingsManager::getInstance()->get((SettingsManager::IntSetting)num));
        }
        if(num == 0) {
            core::Log::get()->log("Unknown setting: " + var);
        }
        else {
            core::Log::get()->log(var + " = %21" + val);
        }


    }
};

} // namespace modules

static modules::DcSet initialize;

