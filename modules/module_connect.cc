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

#include <core/log.h>
#include <core/events.h>
#include <ui/window_hub.h>
#include <display/manager.h>
#include <client/FavoriteManager.h>

namespace modules {

class Connect
{
public:
    Connect() {
        events::add_listener("command connect",
                std::bind(&Connect::connect_callback, this));

        events::add_listener("command disconnect",
                std::bind(&Connect::disconnect, this,
                    std::bind(&events::arg<std::string>, 0)));

        events::add_listener("command reconnect",
                std::bind(&Connect::reconnect_callback, this));
    }

    void connect_callback()
    {
        unsigned args = events::args();
        connect(events::arg<std::string>(0),
                args>1 ? events::arg<std::string>(1) : "", // nick
                args>2 ? events::arg<std::string>(2) : "", // password
                args>3 ? events::arg<std::string>(3) : "", // description
                args>4 ? events::arg<bool>(4) : true); // activate
    }

    void reconnect_callback()
    {
        std::string address = events::arg<std::string>(0);
        disconnect(address);
        connect(address, "", "", "", false);
    }

    /** Connect to a hub */
    void connect(const std::string &address, const std::string &nick,
                const std::string &password, const std::string &description,
                bool activate)
    {
        display::Manager *mger = display::Manager::get();
        display::Windows::iterator it = mger->find(address);

        /* Check if the hub already exists. Try to reconnect and 
         * activate the hub window. */
        if(it != mger->end()) {
            if((*it)->get_type() == display::TYPE_HUBWINDOW) {
                ui::WindowHub *hub = dynamic_cast<ui::WindowHub*>(*it);
                if(!hub->get_client()->isConnected())
                    hub->connect();
                mger->set_current(it);
            }
        }
        else {
            ui::WindowHub *hub = new ui::WindowHub(address);
            hub->connect(address, nick, password, description);
            mger->push_back(hub);
            if(activate)
                mger->set_current(it);
        }
    }

    /** Disconnects a hub.
     * @param hub Hub url to disconnect */
    void disconnect(const std::string &hub) {
        display::Manager *mger = display::Manager::get();
        display::Windows::iterator it = mger->get_current();

        if(!hub.empty()) {
            it = mger->find(hub);
        }

        if(it != mger->end() && (*it)->get_type() == display::TYPE_HUBWINDOW) {
            ui::WindowHub *hub = dynamic_cast<ui::WindowHub*>(*it);
            if(hub->get_client()->isConnected())
                hub->get_client()->disconnect(false);
        }
    }

    std::vector<std::string> complete()
    {
        //int nth = events::arg<int>(0);
        std::string command = events::arg<std::string>(1);

        std::vector<std::string> ret;
        if(command == "connect") {
            FavoriteHubEntry::List list = FavoriteManager::getInstance()->getFavoriteHubs();
            for(FavoriteHubEntry::Iter i = list.begin(); i != list.end(); ++i) {
                ret.push_back((*i)->getServer());
            }
        }
        return ret;
    }
#if 0

    void print_help(const Parameter &param) {
        if(param.get_command() == "connect")
            core::Log::get()->log("Usage: /connect hub [nick] [password] [description]");
        else if(param.get_command() == "disconnect")
            core::Log::get()->log("Usage: /disconnect [hub]");
        else if(param.get_command() == "reconnect")
            core::Log::get()->log("Usage: /reconnect [hub]");
    }
#endif
};

} // namespace modules

static modules::Connect initialize;

