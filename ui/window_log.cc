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

#include <core/events.h>
#include <utils/utils.h>
#include <ui/window_log.h>
#include <sstream>

namespace ui {

WindowLog::WindowLog()
{
    set_title("Logger window");
    set_name("nanodc");
    set_state(display::STATE_IS_ACTIVE);
    std::function<void (const std::string &, core::MessageType)> listener;
    listener = std::bind(&WindowLog::on_log, this,
            std::placeholders::_1, std::placeholders::_2);

    core::Log::get()->add_listener(std::bind(&WindowLog::on_log, this,
                std::placeholders::_1, std::placeholders::_2));

    events::add_listener("client created",
            std::bind(&LogManager::addListener,
                std::bind(&LogManager::getInstance),
                this));
}

void WindowLog::on_log(const std::string &msg, core::MessageType mt)
{
    std::string message = msg;
    if(mt == core::MT_DEBUG)
        message = "[debug] " + message;

    message = utils::to_string(utils::gettid()) + ": " + message;

    add_line(display::LineEntry(message, 0, -1, display::LineEntry::MESSAGE));
}

void WindowLog::on(Message, time_t, const std::string &message)
    throw()
{
    add_line(display::LineEntry(message));
}

void WindowLog::handle_line(const std::string &line)
{
    add_line(display::LineEntry(line, 0, -1, display::LineEntry::MESSAGE));
}

void WindowLog::close()
{
    handle_line("Do not close this window");
}

WindowLog::~WindowLog()
{
    LogManager::getInstance()->removeListener(this);
}

} // namespace ui
