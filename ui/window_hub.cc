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

#include <iomanip>
#include <tr1/functional>
#include <display/manager.h>
#include <ui/window_hub.h>
#include <ui/window_privatemessage.h>
#include <utils/strings.h>
#include <core/log.h>
#include <utils/utils.h>
#include <utils/lock.h>
#include <utils/strings.h>

namespace ui {


WindowHub::WindowHub(const std::string &address):
    m_client(0),
    m_lastJoin(0),
    m_joined(false),
    m_timer(false),
    m_currentUser(m_users.end())
{
    set_title(address);
    set_name(address);
    m_type = display::TYPE_HUBWINDOW;
    update_config();
}

void WindowHub::update_config()
{
    utils::Lock l(m_mutex);

    this->ScrolledWindow::update_config();

    core::Settings *settings = core::Settings::get();
    m_showNicks = settings->find_vector("show_joins_nicks");
    m_ignoreNicks = settings->find_vector("ignore_nicks");
    m_highlights = settings->find_vector("hilight_words");

    if(m_client) {
        core::StringVector vec = settings->find_vector("show_joins_hubs");
        m_showJoinsOnThisHub = utils::find_in_string(m_client->getAddress(), vec.begin(), vec.end());
    }

    m_showJoins = settings->find_bool("show_joins", false);
    m_showNickList = settings->find_bool("show_nicklist", false);
    m_resolveIps = settings->find_bool("resolve_ips", false);
    m_utf8 = settings->find_bool("utf8_input", false);
    m_nmdcCharset = settings->find("nmdc_charset", "ISO-8859-1");
}

void WindowHub::handle_line(const std::string &line)
{
    utils::Lock l(m_mutex);

    if(!m_client || !m_client->isConnected())
        return;

    if(m_utf8 && m_client->getHubUrl().find("adc://") != 0)
    {
        try {
            m_client->hubMessage(utils::convert(line, "UTF-8", m_nmdcCharset));
        } catch(std::exception &e) {
            core::Log::get()->log(std::string("WindowHub::handle_line():") + e.what());
        }
    }
    else {
        m_client->hubMessage(line);
    }
}

void WindowHub::on(TimerManagerListener::Second, u_int32_t)
    throw()
{
    utils::Lock l(m_mutex);

    // group users in 2 seconds
    if(m_lastJoin && !m_joined && m_lastJoin+2000 < TimerManager::getInstance()->getTick()) {
        m_joined = true;
        TimerManager::getInstance()->removeListener(this);
        m_timer = false;
        if(m_client->isConnected()) {
            add_line(display::LineEntry("Joined to the hub"));
            if(m_showNickList)
                print_names();
        }
    }
}

void WindowHub::on(Message, Client *, const OnlineUser& user, const std::string& msg)
    throw()
{
    utils::Lock l(m_mutex);

    std::string temp(msg);
    std::replace(temp.begin(), temp.end(), '\n', ' ');
    temp = strings::escape(temp);

    std::string nick = user.getUser()->getFirstNick();

    // don't show the message if the nick is ignored
    int matches = std::count_if(m_ignoreNicks.begin(), m_ignoreNicks.end(),
                    std::bind1st(std::equal_to<std::string>(), nick));
    if(matches)
        return;

    display::LineEntry::Type flag = display::LineEntry::MESSAGE;

    /* XXX: this is a bug in core?
     * m_client->getMyNick() returns the real nick
     * user.getUser()->getFirstNick() returns SETTING(NICK) */
    std::string realNick = m_client->getMyNick();
    std::string nick2 = SETTING(NICK);
    if(nick2 == nick || nick == realNick)
    {
        /* bold my own nick */
        nick = "%21" + realNick + "%21";
    }
    else if(utils::find_in_string(temp, m_highlights.begin(), m_highlights.end()))
    {
        nick = "%21%06" + nick + "%21%06";
        flag = display::LineEntry::HIGHLIGHT;
    }

    bool op = user.getIdentity().isOp();
    bool bot = user.getIdentity().isBot();
    char status = (op ? '@' : (bot ? '$' : ' '));

    std::ostringstream line;
    line << "%21%08<%21%08" << status << nick << "%21%08>%21%08 " << temp;

    int indent = 4 + g_utf8_strlen(nick.c_str(), -1);

    add_line(display::LineEntry(line.str(), indent, time(0), flag));
}

void WindowHub::on(StatusMessage, Client*, const string& line)
    throw()
{
    std::string tmp = strings::escape(line);
    if(!tmp.empty())
        add_line(display::LineEntry(tmp));
    else
        core::Log::get()->log("WindowHub::on(StatusMessage...): received empty line from " + m_client->getAddress());
}

void WindowHub::on(PrivateMessage, Client *cl, const OnlineUser &from,
    const OnlineUser &to, const OnlineUser &replyto, const string &msg)
    throw()
{
    utils::Lock l(m_mutex);

    core::Log::get()->log("privmsg: From:" + from.getUser()->getFirstNick() + 
           ", To: " + to.getUser()->getFirstNick() + " reply: " + replyto.getUser()->getFirstNick() +  " Message:" + msg);

    display::Manager *dm = display::Manager::get();
    ui::WindowPrivateMessage *pm;

    User::Ptr user = from.getUser();
    /* true if we sent the message */
    bool me = false;

    /* XXX: see the explanation in WindowHub(Message, ...) */
    std::string realNick = m_client->getMyNick();
    if(replyto.getUser() == ClientManager::getInstance()->getMe() ||
       user->getFirstNick() == SETTING(NICK))
    {
        me = true;
        user = to.getUser();
    }

    std::string name = "PM:" + user->getFirstNick();
    display::Windows::iterator it = dm->find(name);

    if(it == dm->end()) {
        /* don't filter if i'm the sender */
        if(!me && !filter_messages(user->getFirstNick(), msg)) {
            return;
        }

        pm = new ui::WindowPrivateMessage(user, realNick);
        dm->push_back(pm);
    } else {
        pm = dynamic_cast<ui::WindowPrivateMessage*>(*it);
    }

    std::string temp = msg;
    std::replace_if(temp.begin(), temp.end(),
        std::bind2nd(std::equal_to<char>(), '\n'), '\\');
    
    if(me) {
        pm->add_line("%21%08<%21%08%21" + realNick + "%21%21%08>%21%08 " + temp);
    }
    else {
        pm->add_line("%21%08<%21%08" + from.getUser()->getFirstNick() + "%21%08>%21%08 " + temp);
        if(pm->get_state() != display::STATE_IS_ACTIVE)
            pm->set_state(display::STATE_HIGHLIGHT);
    }
}

void WindowHub::on(UserUpdated, Client*, const OnlineUser &user)
    throw()
{
    utils::Lock l(m_mutex);

    std::string nick = user.getUser()->getFirstNick();
    if( m_joined
        && m_users.find(nick) == m_users.end()
        && (m_showJoins || m_showJoinsOnThisHub || 
            utils::find_in_string(nick, m_showNicks.begin(), m_showNicks.end())
         )
      )
    {
        // Lehmis [127.0.0.1] has joined the hub
        std::ostringstream oss;
        std::string ip = user.getIdentity().getIp();
        oss << "%03%21" << nick << "%03%21 ";
        if(!ip.empty()) {
            if(m_resolveIps) {
                try {
                    ip = utils::ip_to_host(ip);
                } catch(std::exception &e) {
                    //core::Log::get()->log("utils::ip_to_host(" + ip + "): " + std::string(e.what()));
                }
            }
            oss << "%21%08[%21%08%03" << ip 
                << "%03%21%08]%21%08 ";
        }

        oss << "has joined the hub";
        add_line(display::LineEntry(oss.str()));
    }

    m_lastJoin = TimerManager::getInstance()->getTick();
    m_users[nick] = &user;
}

void WindowHub::on(UserRemoved, Client*, const OnlineUser &user)
    throw()
{
    utils::Lock l(m_mutex);

    std::string nick = user.getUser()->getFirstNick();
    m_users.erase(m_users.find(nick));

    bool showJoin = m_showJoins || m_showJoinsOnThisHub ||
                    utils::find_in_string(nick, m_showNicks.begin(),
                            m_showNicks.end());

    if(m_users.find(nick) == m_users.end() && m_joined && showJoin)
    {
        // Lehmis [127.0.0.1] has left the hub
        std::ostringstream oss;
        std::string ip = user.getIdentity().getIp();
        oss << "%03" << nick << "%03 ";
        if(!ip.empty()) {
            if(m_resolveIps) {
                try {
                    ip = utils::ip_to_host(ip);
                } catch(std::exception &e) {
                    //core::Log::get()->log("utils::ip_to_host(" + ip + "): " + std::string(e.what()));
                }
            }
            oss << "%21%08[%21%08" << ip 
                << "%21%08]%21%08 ";
        }

        oss << "has left the hub";
        add_line(display::LineEntry(oss.str()));
    }

    if(!m_joined) {
        m_joined = true;
        if(m_timer) {
            TimerManager::getInstance()->removeListener(this);
            m_timer = false;
        }
        if(m_client->isConnected()) {
            add_line(display::LineEntry("Joined to the hub"));
            if(m_showNickList)
                print_names();
        }
        else {
            core::Log::get()->log(m_client->getAddress() + " is not connected");
        }
    }
}

void WindowHub::on(UsersUpdated, Client*, const OnlineUser::List &users)
    throw()
{
    utils::Lock l(m_mutex);
    
    OnlineUser::List::const_iterator it;
    for(it = users.begin(); it != users.end(); ++it) {
        m_users[(*it)->getUser()->getFirstNick()] = *it;
    }
}

void WindowHub::on(GetPassword, Client*)
    throw()
{
    utils::Lock l(m_mutex);

    add_line(display::LineEntry("Sending password"));
    m_client->password(m_client->getPassword());
}

void WindowHub::on(HubUpdated, Client *client)
    throw()
{
    utils::Lock l(m_mutex);

    std::string title = client->getHubName() + client->getHubDescription();

    set_title(title);
}

void WindowHub::on(Failed, Client*, const string& msg)
    throw()
{
    utils::Lock l(m_mutex);

    set_title(m_client->getAddress() + " (offline)");
    add_line(display::LineEntry(msg));
    m_joined = false;
    if(m_timer) {
        TimerManager::getInstance()->removeListener(this);
        m_timer = false;
    }
    m_lastJoin = 0;
    m_users.clear();
}

void WindowHub::connect(std::string address, std::string nick, std::string password, std::string desc)
{
    utils::Lock l(m_mutex);

    m_client = ClientManager::getInstance()->getClient(address);

    if(!password.empty())
        m_client->setPassword(password);
    if(!desc.empty())
        m_client->setCurrentDescription(desc);
    if(!nick.empty())
        m_client->setCurrentNick(nick);

    m_client->addListener(this);
    m_client->connect();

    if(!nick.empty())
        m_client->setCurrentNick(nick);

    core::StringVector vec = core::Settings::get()->find_vector("show_joins_hubs");
    m_showJoinsOnThisHub = utils::find_in_string(m_client->getAddress(), vec.begin(), vec.end());
}

void WindowHub::connect()
{
    utils::Lock l(m_mutex);

    m_client->connect();
}

struct _identity
{
    typedef bool result_type;
    _identity(bool (Identity::*__pf)()const) : _M_f(__pf) {}
    bool operator()(const std::pair<std::string, const OnlineUser*> &pair) const {
        return (pair.second->getIdentity().*_M_f)();
    }
    bool (Identity::*_M_f)()const;
};

void WindowHub::print_names()
{
    std::string time = utils::time_to_string("[%H:%M:%S] ");
    for(UserIter i=m_users.begin(); i != m_users.end();) {
        std::ostringstream oss;
        for(int j=0; j<3 && i != m_users.end(); ++j, ++i) {
            int length = strings::length(i->first) - 18;
            oss << "%21%08[%21%08" << i->first
                << std::string(std::max(0, length), ' ')
                << "%21%08]%21%08 ";
        }
        display::LineEntry line(oss.str(), 0, -1, display::LineEntry::MESSAGE);
        add_line(line, false);
    }

    int ops = std::count_if(m_users.begin(), m_users.end(), _identity(&Identity::isOp));
    int bots = std::count_if(m_users.begin(), m_users.end(), _identity(&Identity::isBot));
    int active = std::count_if(m_users.begin(), m_users.end(), _identity(&Identity::isTcpActive));
    int hidden = std::count_if(m_users.begin(), m_users.end(), _identity(&Identity::isHidden));

    std::ostringstream oss;
    oss << m_users.size() << " users " << ops << "/" << bots << "/" 
        << m_users.size()-active << "/" << hidden << " ops/bots/passive/hidden";
    add_line(display::LineEntry(oss.str()));
}

void WindowHub::on(Connected, Client*)
    throw()
{
    utils::Lock l(m_mutex);

    add_line(display::LineEntry("Connected"));
    set_title(m_client->getHubName());
    TimerManager::getInstance()->addListener(this);
    m_timer = true;
}

bool WindowHub::filter_messages(const std::string &nick, const std::string &msg)
{
    utils::Lock l(m_mutex);

    core::Settings *settings = core::Settings::get();
    core::StringVector blocked;

    if(!settings->exists("block_messages")) {
        // block urls by default
        core::Settings::get()->set("block_messages", "http://;www.");
    }
    blocked = core::Settings::get()->find_vector("block_messages");

    if (utils::find_in_string(msg, blocked.begin(), blocked.end()) ||
        utils::find_in_string(nick, m_ignoreNicks.begin(), m_ignoreNicks.end()))
    {
        core::Log::get()->log("%21Ignore:%21 from: " + nick + ", msg: " + msg, core::MT_DEBUG);
        return false;
    }
    return true;
}

std::string WindowHub::get_nick() const
{
    utils::Lock l(m_mutex);

    if(!m_client)
        return utils::empty_string;

    std::string nick;
    if(m_client->isOp())
        nick = "%21@%21";
    nick += m_client->getMyNick();
    return nick;
}

const OnlineUser *WindowHub::get_user(const std::string &nick)
    throw(std::out_of_range)
{
    utils::Lock l(m_mutex);

    Users::const_iterator it = m_users.find(nick);
    if(it == m_users.end())
        throw std::out_of_range("user not found");
    return it->second;
}

WindowHub::~WindowHub()
{
    if(m_timer)
        TimerManager::getInstance()->removeListener(this);

    if(m_client) {
        m_client->removeListener(this);
        ClientManager::getInstance()->putClient(m_client);
    }
    m_users.clear();
}

} // namespace ui
