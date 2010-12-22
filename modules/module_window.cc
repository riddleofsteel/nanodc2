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

#include <utils/utils.h>
#include <core/events.h>
#include <core/log.h>
#include <display/manager.h>

namespace modules {

class Window
{
public:
    Window() {
        events::add_listener("command window",
            std::bind(&Window::window_callback, this));

#warning RE-enable this
//         events::add_listener("command wc",
//             std::bind(&events::emit, std::string("command window"),
//                 std::string("close")));
    }

    void window_callback() {
        std::string command = events::arg<std::string>(0);
        display::Manager *mger = display::Manager::get();
        core::Log::get()->log(command);
        display::Windows::iterator current = mger->get_current();

        if(command == "close") {
            display::Windows::iterator it = display::Manager::get()->get_current();
            display::Manager::get()->remove(*it);
        }
//        else if(command == "next")
    }

#if 0

        if(cmd == "window") {
            const char* str = param.get_param(0).c_str();
            if(param.get_param(0) == "move") {
                core::Log::get()->log("Not implemented");
            }
            else if((param.get_param(0) == "next") || (param.get_param(0) == "prev")) {
                if(param.get_param(0) == "next") 
                    current = (current == mger->end()-1 ? mger->begin() : current+1);
                else
                    current = (current == mger->begin() ? mger->end()-1 : current-1);

                mger->set_current(current);
            }
            else if(*str >= '0' && *str <= '9') {
                unsigned int pos = utils::to<int>(str)-1;
                if(pos <= mger->size()) {
                    display::Manager::iterator current = mger->begin()+pos;
                    mger->set_current(current);
                }
            }
        }

    void print_help(const Parameter &param) {
        if(param.get_command() == "window") {
            core::Log::get()->log("Usage: /window n|move|close");
        }
    }

    std::vector<std::string> complete(int n, const Parameter &param)
    {
        if(param.get_command() == "window") {
            if(n == 0) {
                return utils::make_vector(4, "move", "close", "next", "prev");
            }
            else if(n == 1) {
                if(param.get_param(0) == "move") {
                    return utils::make_vector(4, "next", "prev", "first", "last");
                }
            }
        }
        return std::vector<std::string>();
    }
#endif
};
} // namespace modules

static modules::Window initialize;


