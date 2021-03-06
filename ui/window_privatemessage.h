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

#ifndef _WINDOWPRIVATEMESSAGE_H_
#define _WINDOWPRIVATEMESSAGE_H_

#include <client/stdinc.h>
#include <client/DCPlusPlus.h>
#include <client/ClientManager.h>
#include <client/QueueManager.h>
#include <client/User.h>
#include <display/scrolled_window.h>

namespace ui {

class WindowPrivateMessage:
    public display::ScrolledWindow
{
public:
    WindowPrivateMessage(User::Ptr user, const std::string &mynick);

    /** Send private message to the user */
    virtual void handle_line(const std::string &line);

    /** Get my nick. */
    std::string get_nick() const { return m_nick; }

    /** Get user's file list. */
    void get_list();

    User::Ptr get_user() { return m_user; }

    ~WindowPrivateMessage();
private:
    User::Ptr m_user;
    std::string m_nick;
};

} // namespace ui

#endif // _WINDOWPRIVATEMESSAGE_H_
