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

#include <iostream>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <tr1/functional>
#include <display/screen.h>
#include <display/manager.h>
#include <utils/utils.h>
#include <core/log.h>
#include <core/events.h>

namespace display {

Manager::Manager():
    m_windows(new Windows()),
    m_current(m_windows->end()),
    m_statusbar(display::StatusBar::create()),
    m_altPressed(false)
{
    events::add_listener("key pressed",
            std::tr1::bind(&display::Window::handle,
                std::tr1::bind(&display::Manager::get_current_window, this),
                std::tr1::bind(&events::arg<wint_t>, 1)));

    events::add_listener_first("key pressed",
            std::tr1::bind(&display::Manager::handle_key, this));

    events::add_listener_last("window closed",
            std::tr1::bind(&display::Manager::window_closed, this));

    m_inputWindow.set_input(&display::Window::m_input);
}

void Manager::push_back(display::Window *window)
{
    m_mutex.lock();
    m_windows->push_back(window);
    m_mutex.unlock();
    window->resize();
}

void Manager::set_active_window(unsigned int n)
    throw(std::out_of_range)
{
    if(n >= m_windows->size())
        throw std::out_of_range("Manager::set_active_window: Window index out of range");

    set_current(m_windows->begin()+n);
}

void Manager::set_current(Windows::iterator current)
{
    if(current == m_current)
        return;

    display::Window *old = *m_current;

    (*m_current)->set_state(STATE_NO_ACTIVITY);
    (*m_current)->erase();
    m_current = current;
    (*m_current)->set_state(STATE_IS_ACTIVE);
    (*m_current)->refresh();

    events::emit("window status updated", *current, STATE_IS_ACTIVE);
    events::emit("window updated", *current);
    events::emit("window changed", old, *current);
}

void Manager::handle_key()
{
    wint_t key = events::arg<wint_t>(1);
    if(key == 0x1B) {
        m_altPressed = true;
        events::stop();
        return;
    }

    if(m_altPressed == false)
        return;

    m_altPressed = false;
    if(key >= '1' && key <= '9') {
        try {
            set_active_window(key - '1');
        } catch(std::out_of_range &e) {

        }
        events::stop();
    }
    else if(key == KEY_LEFT) {
        if(m_current == m_windows->begin())
            set_current(m_windows->end()-1);
        else
            set_current(--m_current);
    }
    else if(key == KEY_RIGHT) {
        if(m_current == m_windows->end()-1)
            set_current(m_windows->begin());
        else
            set_current(++m_current);
    }
}

void Manager::redraw()
{
    if(display::Screen::is_resized()) {
        resize();
    }

    (*m_current)->draw();
    m_statusbar->redraw();

    if((*m_current)->insert_mode()) {
        m_inputWindow.set_prompt((*m_current)->get_prompt());
        m_inputWindow.redraw();
        curs_set((*m_current)->insert_mode() ? 1 : 0);
    }
    else {
        m_inputWindow.erase();
        m_inputWindow.refresh();
        curs_set((*m_current)->insert_mode() ? 1 : 0);
    }
    display::Screen::do_update();
}

void Manager::resize()
{
    int h = display::Screen::get_height();
    int w = display::Screen::get_width();
    m_statusbar->resize(0, h-2 < 1 ? 1 : h-2, w, 1);
    m_inputWindow.resize(0, h-1 < 1 ? 1 : h-1, w, 1);

    std::for_each(m_windows->begin(), m_windows->end(),
            std::mem_fun(&Window::resize));
}

void Manager::remove(display::Window *window)
{
    /* it would crash if all windows were closed */
    if(m_windows->at(0) == window) {
        core::Log::get()->log("Don't close log window", core::MT_DEBUG);
        return;
    }

    m_mutex.lock();
    Windows::iterator it = std::remove(m_windows->begin(), m_windows->end(), window);
    m_windows->erase(it, m_windows->end());
    m_mutex.unlock();
    events::emit("window closed", window);

    if(*m_current == window)
        set_current(--m_current);
}

Windows::iterator Manager::find(const std::string &name)
{
    using std::tr1::placeholders::_1;
    utils::Lock lock(m_mutex);
    return std::find_if(m_windows->begin(), m_windows->end(),
                std::tr1::bind(
                    std::equal_to<std::string>(),
                    std::tr1::bind(&Window::get_name, _1),
                    name
                ));
}

void Manager::window_closed()
{
    display::Window *window = events::arg<display::Window*>(0);
    delete window;
}

Manager::~Manager()
{
    m_mutex.lock();
    std::for_each(m_windows->begin(), m_windows->end(),
            utils::delete_functor<Window>());
    m_windows->clear();
    m_mutex.unlock();

    display::InputWindow::destroy();
    display::StatusBar::destroy();
}

} // namespace display

