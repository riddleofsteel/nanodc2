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
#include <iomanip>
#include <functional>
#include <utils/utils.h>
#include <core/log.h>
#include <core/events.h>
#include <ui/window_favorites.h>

namespace ui {

WindowFavorites::WindowFavorites():
    m_editState(EDIT_NONE),
    m_confirmRemove(-1)
{
    set_title("Favorite hubs: " + utils::to_string(FavoriteManager::getInstance()->getFavoriteHubs().size()));
    set_name("favorites");

    m_bindings['c'] = std::bind(&WindowFavorites::connect, this, true);
    m_bindings['C'] = std::bind(&WindowFavorites::connect, this, false);
    m_bindings['d'] = std::bind(&WindowFavorites::rmfav, this, true);
    m_bindings['D'] = std::bind(&WindowFavorites::rmfav, this, false);
    m_bindings['e'] = std::bind(&WindowFavorites::edit, this);
    m_bindings['n'] = std::bind(&WindowFavorites::add_new, this);
    m_bindings[' '] = std::bind(&WindowFavorites::toggle_connect, this);

    insert_column(new display::Column("Auto", 5, 5, 5));
    insert_column(new display::Column("Nick", 10, 15, 20));
    insert_column(new display::Column("Address", 10, 30, 45));
    insert_column(new display::Column("Name", 10, 25, 200));

    FavoriteHubEntry::List favhubs = FavoriteManager::getInstance()->getFavoriteHubs();
    FavoriteHubEntry::List::iterator it = favhubs.begin();
    for(;it != favhubs.end(); ++it)
        on(FavoriteManagerListener::FavoriteAdded(), *it);
    m_state = display::STATE_NO_ACTIVITY;
    FavoriteManager::getInstance()->addListener(this);
}

void WindowFavorites::toggle_connect()
{
    int row = get_selected_row();
    if(row == -1)
        return;

    FavoriteHubEntry *entry = find_entry(row);
    entry->setConnect(!entry->getConnect());
    FavoriteManager *favman = FavoriteManager::getInstance();
    favman->save();

    set_text(0, row, entry->getConnect());
}

void WindowFavorites::add_new()
{
    m_tempFav = FavoriteHubEntry();
    m_insertMode = true;
    handle_line("");
}

FavoriteHubEntry *WindowFavorites::find_entry(int row)
{
    FavoriteHubEntry::List favhubs = FavoriteManager::getInstance()->getFavoriteHubs();
    return *std::find_if(favhubs.begin(), favhubs.end(),
        std::bind(
            std::equal_to<std::string>(),
            std::bind(&FavoriteHubEntry::getServer, std::placeholders::_1),
            get_text(2, row)
        ));
}

void WindowFavorites::edit()
{
    int row = get_selected_row();
    if(row == -1)
        return;

    FavoriteHubEntry *entry = find_entry(row);

    m_tempFav = *entry;
    m_insertMode = true;
    handle_line("");
}

void WindowFavorites::set_promt_text(const std::string &text)
{
    m_input.assign(text);
    m_input.set_pos(text.length());
}

void WindowFavorites::handle_line(const std::string &line)
{
    /* if we are confirming hub removal */
    if(m_confirmRemove != -1) {
        if(line == "y")
            rmfav(false);
        set_prompt();
        m_insertMode = false;
        return;
    }

    const char* questions[] = { 
        "Hub address?",
        "Hub name?",
        "Hub description?",
        "Your nick?",
        "Your description?",
        "Hub password?",
        "Autoconnect (y/n)?",
        ""
    };
    /* ask one question at a time */
    set_prompt(questions[m_editState]);

    m_editState++;

    switch(m_editState) {
        case EDIT_START:
            set_promt_text(m_tempFav.getServer());
            break;
        case SERVER_URL:
            m_tempFav.setServer(line);
            set_promt_text(m_tempFav.getName());
            break;
        case SERVER_NAME:
        {
            m_tempFav.setName(line);
            std::string desc = m_tempFav.getDescription();
            set_promt_text(m_tempFav.getDescription());
            break;}
        case SERVER_DESCRIPTION:
            m_tempFav.setDescription(line);
            set_promt_text(m_tempFav.getNick());
            break;
        case USER_NICK:
            m_tempFav.setNick(line);
            set_promt_text(m_tempFav.getUserDescription());
            break;
        case USER_DESCRIPTION:
            m_tempFav.setUserDescription(line);
            set_promt_text(m_tempFav.getPassword());
            break;
        case SERVER_PASSWORD:
        {
            m_tempFav.setPassword(line);
            std::string text = m_tempFav.getConnect() == true ? "y" : "n";
            set_promt_text(text);
            break;
        }
        case SERVER_AUTOCONNECT:
        {
            m_tempFav.setConnect((line == "y"));
            FavoriteManager *favman = FavoriteManager::getInstance();
            if(favman->checkFavHubExists(m_tempFav)) {
                try {
                    int row = get_selected_row();
                    FavoriteHubEntry *entry = find_entry(row);
                    entry->setServer(m_tempFav.getServer());
                    entry->setName(m_tempFav.getName());
                    entry->setDescription(m_tempFav.getDescription());
                    entry->setNick(m_tempFav.getNick());
                    entry->setUserDescription(m_tempFav.getUserDescription());
                    entry->setPassword(m_tempFav.getPassword());
                    entry->setConnect(m_tempFav.getConnect());

                    set_text(0, row, entry->getConnect());
                    set_text(1, row, entry->getNick());
                    set_text(2, row, entry->getServer());
                    set_text(3, row, entry->getName());
                }
                catch(std::exception &e) {
                    throw std::logic_error(std::string("Should not happen: ") + e.what());
                }
            }
            else {
                favman->addFavorite(m_tempFav);
            }
            favman->save();
            m_editState = EDIT_NONE;
            m_insertMode = false;
            set_prompt();
            break;
        }
        default:
            throw std::logic_error("should not be reached");
    }
}

void WindowFavorites::connect(bool activate)
{
    int row = get_selected_row();
    if(row == -1)
        return;

    FavoriteHubEntry *hub = find_entry(row);

    events::emit("command connect", hub->getServer(), hub->getNick(),
            hub->getPassword(), hub->getDescription(), activate);
}

void WindowFavorites::rmfav(bool confirm)
{
    int row = get_selected_row();
    if(row == -1)
        return;

    if(confirm) {
        m_confirmRemove = get_current();
        m_prompt = "Really remove (y/n)?";
        m_insertMode = true;
        return;
    }

    FavoriteManager::getInstance()->removeFavorite(find_entry(row));
    m_confirmRemove = -1;
}

void WindowFavorites::on(FavoriteManagerListener::FavoriteAdded, const FavoriteHubEntry *entry) throw()
{
    int row = insert_row();
    set_text(0, row, entry->getConnect());
    set_text(1, row, entry->getNick());
    set_text(2, row, entry->getServer());
    set_text(3, row, entry->getName());
}

void WindowFavorites::on(FavoriteManagerListener::FavoriteRemoved, const FavoriteHubEntry *entry) throw()
{
    delete_row(2, entry->getServer());
}

WindowFavorites::~WindowFavorites()
{
}


std::string WindowFavorites::get_infobox_line(unsigned int n)
{
    FavoriteHubEntry *entry = find_entry(get_selected_row());

    std::stringstream ss;
    switch(n) {
        case 1:
            ss << "%21Hub name:%21 " << entry->getName();
            break;
        case 2:
            ss << "%21Nick:%21 " << std::left << std::setw(20) << entry->getNick()
                << " %21Description:%21 " << entry->getUserDescription();
            break;
        case 3:
        {
            std::string passwd(entry->getPassword().length(), '*');
            ss << "%21Password:%21 " << std::left << std::setw(17) 
                << (passwd.empty()?"no password":passwd)
                << "%21Autoconnect:%21 " << (entry->getConnect()?"yes":"no");
            break;
        }
        case 4:
            ss << "%21Hub description:%21 " << entry->getDescription();
            break;
            
    }
    return ss.str();
}

} // namespace ui
