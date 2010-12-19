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

#ifndef _WINDOWHUB_H_
#define _WINDOWHUB_H_

#include <client/stdinc.h>
#include <client/DCPlusPlus.h>
#include <client/Client.h>
#include <client/ClientManager.h>
#include <client/TimerManager.h>
#include <display/scrolled_window.h>
#include <utils/mutex.h>
#include <map>
#include <string>

namespace ui {

class WindowHub:
    public display::ScrolledWindow,
    public ClientListener,
    public TimerManagerListener
{
public:
    WindowHub(const std::string &address);

    virtual void update_config();

    void connect(std::string address, std::string nick, std::string password, std::string desc);
    void connect();
    void handle_line(const std::string &line);
    Client* get_client() const { return m_client; }
    void print_names();

    /** @todo regular expressions in filters */
    bool filter_messages(const std::string &nick, const std::string &msg);
    std::string get_nick() const;
    const OnlineUser *get_user(const std::string &nick) throw (std::out_of_range);
    ~WindowHub();

    virtual void on(TimerManagerListener::Second, u_int32_t) throw();

    // ClientListener stuff..
    virtual void on(UserUpdated, Client*, const OnlineUser&) throw();
    virtual void on(UsersUpdated, Client*, const OnlineUser::List&) throw();
    virtual void on(UserRemoved, Client*, const OnlineUser&) throw();
    virtual void on(Message, Client*, const OnlineUser&, const string&) throw(); 
    virtual void on(GetPassword, Client*) throw();
    virtual void on(PrivateMessage, Client*, const OnlineUser&, const OnlineUser&, const OnlineUser&, const string&) throw();

    virtual void on(StatusMessage, Client*, const string& line) throw();
    virtual void on(Connecting, Client*) throw() { add_line(display::LineEntry("Connecting...")); }
    virtual void on(Connected, Client*) throw();
    virtual void on(Redirect, Client*, const string &msg) throw() { add_line(display::LineEntry("Redirect: ")); }
    virtual void on(Failed, Client*, const string& msg) throw();
    virtual void on(HubUpdated, Client*) throw();
    virtual void on(HubFull, Client*) throw() { add_line(display::LineEntry("Hub full")); }
    virtual void on(NickTaken, Client*) throw() { add_line(display::LineEntry("Nick taken")); }
    virtual void on(SearchFlood, Client*, const string &msg) throw() { add_line(display::LineEntry(msg)); }
private:
    Client *m_client;
    int64_t m_lastJoin;
    bool m_joined;
    bool m_timer;
    typedef std::map<std::string, const OnlineUser*> Users;
    typedef Users::const_iterator UserIter;
    Users m_users;
    UserIter m_currentUser;
    core::StringVector m_showNicks;
    core::StringVector m_ignoreNicks;
    core::StringVector m_highlights;
    bool m_showJoins;
    bool m_showJoinsOnThisHub;
    bool m_showNickList;
    bool m_resolveIps;
    bool m_utf8;
    std::string m_nmdcCharset;

    mutable utils::Mutex m_mutex;
};

} // namespace ui

#endif // _WINDOWHUB_H_
