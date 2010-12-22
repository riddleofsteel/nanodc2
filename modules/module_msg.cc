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
#include <core/argparser.h>
#include <ui/window_hub.h>
#include <ui/window_privatemessage.h>
#include <display/manager.h>
#include <client/FavoriteManager.h>

namespace modules {

class Msg
{
public:
    Msg() {
        events::add_listener("command msg",
                std::bind(&Msg::msg_callback, this));
    }

    /** /msg nick message */
    void msg_callback()
    {
        core::ArgParser parser(events::args() > 0 ? events::arg<std::string>(0) : "");
        parser.parse();

        if(parser.args() < 2) {
            core::Log::get()->log("Not enough parameters given");
            return;
        }

        std::string nick = parser.arg(0);

        display::Manager *dm = display::Manager::get();
        display::Windows::iterator it = dm->get_current();
        if((*it)->get_type() != display::TYPE_HUBWINDOW) {
            core::Log::get()->log("Not connected to a hub");
            return;
        }

        ui::WindowHub *hub = dynamic_cast<ui::WindowHub*>(*it);
        User::Ptr user;

        try {
            user = hub->get_user(nick)->getUser();
        }
        catch(std::out_of_range &e) {
            core::Log::get()->log("Unknown user: " + nick);
            return;
        }
        std::string name = "PM:" + user->getFirstNick();
        it = dm->find(name);
        if(it == dm->end()) {
            ui::WindowPrivateMessage *pm = new ui::WindowPrivateMessage(user, hub->get_client()->getMyNick());
            dm->push_back(pm);
            dm->set_current(it);
        }
        /* all text after first argument (nick) */
        std::string line = parser.get_text(1);
        (*it)->handle_line(line);
    }
};

} // namespace modules

static modules::Msg initialize;

