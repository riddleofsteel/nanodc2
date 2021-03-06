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
#include <core/settings.h>
#include <ui/status_clock.h>
#include <utils/utils.h>
#include <utils/lock.h>

namespace ui {

StatusClock::StatusClock()
{
    set_name("clock");
    update_config();
    core::Settings::get()->add_listener(
            std::bind(&StatusClock::update_config, this));

    events::add_listener("client created",
            std::bind(&TimerManager::addListener,
                std::bind(&TimerManager::getInstance), this));

    update();
}

void StatusClock::on(TimerManagerListener::Second, u_int32_t)
    throw()
{
    update();
    events::emit("statusbar updated");
}

void StatusClock::update()
{
    utils::Lock l(m_mutex);
    set_text(utils::time_to_string(m_timeformat));
}

void StatusClock::update_config()
{
    utils::Lock l(m_mutex);
    m_timeformat = core::Settings::get()->find("clock_format", "%H:%M:%S");
}

StatusClock::~StatusClock()
{
}

} // namespace ui
